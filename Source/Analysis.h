#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <cmath>
#include <algorithm>

// Outils d'analyse : tempo (BPM), tonalité, conversion audio -> MIDI.
namespace analysis
{
    constexpr double sr = 22050.0;

    // Charge un fichier en mono 22 050 Hz (10 minutes max)
    inline std::vector<float> loadMono (juce::AudioFormatManager& fm, const juce::File& f, double& seconds)
    {
        std::vector<float> out;
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
        if (r == nullptr || r->sampleRate <= 0) return out;
        const int len = (int) juce::jmin<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * 600.0));
        juce::AudioBuffer<float> b ((int) juce::jmax (1u, r->numChannels), len);
        r->read (&b, 0, len, 0, true, true);
        std::vector<float> mono ((size_t) len);
        for (int i = 0; i < len; ++i)
        {
            float s = 0;
            for (int ch = 0; ch < b.getNumChannels(); ++ch) s += b.getSample (ch, i);
            mono[(size_t) i] = s / (float) b.getNumChannels();
        }
        const double ratio = r->sampleRate / sr;
        const int outLen = (int) (len / ratio);
        out.resize ((size_t) outLen);
        juce::LagrangeInterpolator interp;
        interp.process (ratio, mono.data(), out.data(), outLen);
        seconds = len / r->sampleRate;
        return out;
    }

    inline std::vector<float> fromBuffer (const juce::AudioBuffer<float>& b, double rate, double& seconds)
    {
        const int len = b.getNumSamples();
        std::vector<float> mono ((size_t) len);
        for (int i = 0; i < len; ++i)
        {
            float s = 0;
            for (int ch = 0; ch < b.getNumChannels(); ++ch) s += b.getSample (ch, i);
            mono[(size_t) i] = s / (float) juce::jmax (1, b.getNumChannels());
        }
        const double ratio = rate / sr;
        std::vector<float> out ((size_t) (len / ratio));
        juce::LagrangeInterpolator interp;
        interp.process (ratio, mono.data(), out.data(), (int) out.size());
        seconds = len / rate;
        return out;
    }

    //==========================================================================
    // Spectrogramme (magnitudes) avec fenêtre de Hann
    struct Spectrogram
    {
        int fftOrder = 11, size = 2048, hop = 512, bins = 1025;
        std::vector<std::vector<float>> frames;
    };

    inline Spectrogram spectrogram (const std::vector<float>& x, int order, int hop)
    {
        Spectrogram s;
        s.fftOrder = order; s.size = 1 << order; s.hop = hop; s.bins = s.size / 2 + 1;
        juce::dsp::FFT fft (order);
        std::vector<float> win ((size_t) s.size), buf ((size_t) s.size * 2);
        for (int i = 0; i < s.size; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / (float) s.size);
        for (size_t pos = 0; pos + (size_t) s.size <= x.size(); pos += (size_t) hop)
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            for (int i = 0; i < s.size; ++i) buf[(size_t) i] = x[pos + (size_t) i] * win[(size_t) i];
            fft.performFrequencyOnlyForwardTransform (buf.data());
            s.frames.emplace_back (buf.begin(), buf.begin() + s.bins);
        }
        return s;
    }

    //==========================================================================
    // Tempo : flux spectral + autocorrélation
    struct TempoResult { double bpm = 0; float confidence = 0; };

    inline TempoResult detectTempo (const std::vector<float>& x)
    {
        TempoResult r;
        if (x.size() < (size_t) (sr * 3)) return r;
        const int hop = 256;
        auto s = spectrogram (x, 10, hop);
        const size_t n = s.frames.size();
        if (n < 64) return r;

        std::vector<float> flux (n, 0.0f);
        for (size_t f = 1; f < n; ++f)
        {
            float sum = 0;
            for (int b = 1; b < s.bins; ++b)
            {
                const float d = std::log1p (100.0f * s.frames[f][(size_t) b]) - std::log1p (100.0f * s.frames[f - 1][(size_t) b]);
                if (d > 0) sum += d;
            }
            flux[f] = sum;
        }
        // retire la moyenne locale (enveloppe d'attaques)
        std::vector<float> on (n, 0.0f);
        const int w = 16;
        for (size_t i = 0; i < n; ++i)
        {
            float m = 0; int c = 0;
            for (int k = -w; k <= w; ++k) { const long j = (long) i + k; if (j >= 0 && j < (long) n) { m += flux[(size_t) j]; ++c; } }
            on[i] = juce::jmax (0.0f, flux[i] - m / (float) c);
        }

        const double fps = sr / hop;
        const int minLag = (int) (fps * 60.0 / 200.0), maxLag = (int) (fps * 60.0 / 55.0);
        std::vector<double> ac ((size_t) maxLag + 2, 0.0);
        for (int lag = minLag; lag <= maxLag + 1; ++lag)
        {
            double sum = 0;
            for (size_t i = (size_t) lag; i < n; ++i) sum += on[i] * on[i - (size_t) lag];
            ac[(size_t) lag] = sum / (double) (n - (size_t) lag);
        }
        // préférence pour 80-160 BPM, et renforcement par les multiples (2x, 4x le battement)
        double best = -1; int bestLag = minLag;
        double mean = 0;
        for (int lag = minLag; lag <= maxLag; ++lag) mean += ac[(size_t) lag];
        mean /= (maxLag - minLag + 1);
        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            const double bpm = 60.0 * fps / lag;
            const double weight = std::exp (-0.5 * std::pow (std::log2 (bpm / 115.0) / 0.9, 2.0));
            double score = ac[(size_t) lag];
            if (lag * 2 <= maxLag) score += 0.5 * ac[(size_t) lag * 2];
            score *= weight;
            if (score > best) { best = score; bestLag = lag; }
        }
        // affinage parabolique
        double lagF = bestLag;
        if (bestLag > minLag && bestLag < maxLag)
        {
            const double a = ac[(size_t) bestLag - 1], b = ac[(size_t) bestLag], c = ac[(size_t) bestLag + 1];
            const double den = a - 2 * b + c;
            if (std::abs (den) > 1e-12) lagF = bestLag + 0.5 * (a - c) / den;
        }
        r.bpm = std::round (60.0 * fps / lagF * 10.0) / 10.0;
        if (std::abs (r.bpm - std::round (r.bpm)) < 0.35) r.bpm = std::round (r.bpm);
        r.confidence = (float) juce::jlimit (0.0, 1.0, (ac[(size_t) bestLag] / (mean + 1e-12) - 1.0) / 3.0);
        return r;
    }

    // Notes "fondamentales" présentes dans une trame (somme des harmoniques, puis on retire les harmoniques trouvées)
    inline int salientPitches (const std::vector<float>& m, int fftSize, int bins, float floorLevel, float relThreshold,
                               int maxNotes, int* outPitch, float* outSal)
    {
        float sal[128] {};
        float frameMax = 0;
        for (int p = 28; p <= 100; ++p)
        {
            const double f0 = 440.0 * std::pow (2.0, (p - 69) / 12.0);
            float sum = 0;
            for (int h = 1; h <= 5; ++h)
            {
                const int b = (int) std::round (f0 * h * fftSize / sr);
                if (b >= bins - 1) break;
                sum += juce::jmax (m[(size_t) b], juce::jmax (m[(size_t) b - 1], m[(size_t) b + 1])) / (float) h;
            }
            const int b0 = (int) std::round (f0 * fftSize / sr);
            if (b0 < 1 || m[(size_t) b0] < floorLevel) sum = 0;
            sal[p] = sum;
            frameMax = juce::jmax (frameMax, sum);
        }
        int count = 0;
        for (int k = 0; k < maxNotes && frameMax > 0; ++k)
        {
            int bestP = -1; float bestV = frameMax * relThreshold;
            for (int p = 28; p <= 100; ++p) if (sal[p] > bestV) { bestV = sal[p]; bestP = p; }
            if (bestP < 0) break;
            outPitch[count] = bestP; outSal[count] = bestV; ++count;
            for (int h = 2; h <= 6; ++h)
            {
                const int hp = bestP + (int) std::round (12.0 * std::log2 ((double) h));
                if (hp <= 127) sal[hp] *= 0.15f;
            }
            sal[bestP] = 0;
            if (bestP > 0) sal[bestP - 1] *= 0.3f;
            if (bestP < 127) sal[bestP + 1] *= 0.3f;
        }
        return count;
    }

    //==========================================================================
    // Tonalité : chromagramme + profils de Krumhansl
    struct KeyCandidate { int key = 0; bool minor = false; float score = 0; };

    inline std::vector<KeyCandidate> detectKey (const std::vector<float>& x)
    {
        std::vector<KeyCandidate> out;
        if (x.size() < (size_t) sr) return out;
        auto s = spectrogram (x, 12, 2048);
        double chroma[12] {};
        float global = 1e-9f;
        for (auto& frm : s.frames) for (float v : frm) global = juce::jmax (global, v);
        int pitches[8]; float sals[8];
        for (auto& frm : s.frames)
        {
            const int n = salientPitches (frm, s.size, s.bins, global * 0.003f, 0.2f, 6, pitches, sals);
            for (int i = 0; i < n; ++i) chroma[pitches[i] % 12] += std::sqrt (sals[i]);
        }        static const double maj[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
        static const double mnr[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
        auto corr = [&] (const double* prof, int key)
        {
            double mx = 0, my = 0;
            for (int i = 0; i < 12; ++i) { mx += chroma[i]; my += prof[i]; }
            mx /= 12; my /= 12;
            double sxy = 0, sxx = 0, syy = 0;
            for (int i = 0; i < 12; ++i)
            {
                const double a = chroma[(i + key) % 12] - mx, b = prof[i] - my;
                sxy += a * b; sxx += a * a; syy += b * b;
            }
            return sxy / std::sqrt (sxx * syy + 1e-12);
        };
        for (int k = 0; k < 12; ++k)
        {
            out.push_back ({ k, false, (float) corr (maj, k) });
            out.push_back ({ k, true,  (float) corr (mnr, k) });
        }
        std::sort (out.begin(), out.end(), [] (const KeyCandidate& a, const KeyCandidate& b) { return a.score > b.score; });
        out.resize (3);
        return out;
    }

    //==========================================================================
    // Audio -> MIDI
    struct Note { double start, length; int pitch; float vel; };

    // Hauteur par YIN (mélodie, une note à la fois)
    inline float yin (const float* x, int n, float threshold = 0.15f, double fMin = 60.0)
    {
        const int tauMax = juce::jmin (n / 2 - 2, (int) (sr / fMin)), tauMin = (int) (sr / 1500.0);
        std::vector<float> d ((size_t) tauMax + 1, 0.0f);
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            float s = 0;
            for (int i = 0; i < n / 2; ++i) { const float v = x[i] - x[i + tau]; s += v * v; }
            d[(size_t) tau] = s;
        }
        float run = 0;
        d[0] = 1;
        for (int tau = 1; tau <= tauMax; ++tau) { run += d[(size_t) tau]; d[(size_t) tau] = d[(size_t) tau] * tau / (run + 1e-12f); }
        for (int tau = tauMin; tau < tauMax; ++tau)
            if (d[(size_t) tau] < threshold)
            {
                while (tau + 1 < tauMax && d[(size_t) tau + 1] < d[(size_t) tau]) ++tau;
                const float a = d[(size_t) tau - 1], b = d[(size_t) tau], c = d[(size_t) tau + 1];
                const float den = a - 2 * b + c;
                const float t = std::abs (den) > 1e-9f ? tau + 0.5f * (a - c) / den : (float) tau;
                return (float) (sr / t);
            }
        return 0.0f;
    }

    inline std::vector<Note> melodyToMidi (const std::vector<float>& x, float sensitivity)
    {
        std::vector<Note> notes;
        const int frame = 1024, hop = 256;
        std::vector<float> pitch, rms;
        for (size_t pos = 0; pos + frame <= x.size(); pos += hop)
        {
            float e = 0;
            for (int i = 0; i < frame; ++i) e += x[pos + (size_t) i] * x[pos + (size_t) i];
            e = std::sqrt (e / frame);
            rms.push_back (e);
            const float f = yin (x.data() + pos, frame, 0.1f + 0.15f * sensitivity);
            pitch.push_back (f > 0 ? 69.0f + 12.0f * std::log2 (f / 440.0f) : -1.0f);
        }
        float peak = 1e-9f;
        for (float e : rms) peak = juce::jmax (peak, e);
        const float gate = peak * std::pow (10.0f, (-40.0f + 25.0f * (1.0f - sensitivity)) / 20.0f);

        // filtre médian (5) sur la hauteur
        std::vector<int> mp (pitch.size(), -1);
        for (size_t i = 0; i < pitch.size(); ++i)
        {
            float w[5]; int c = 0;
            for (int k = -2; k <= 2; ++k) { const long j = (long) i + k; if (j >= 0 && j < (long) pitch.size() && pitch[(size_t) j] > 0) w[c++] = pitch[(size_t) j]; }
            if (c >= 3 && rms[i] > gate) { std::sort (w, w + c); mp[i] = (int) std::round (w[c / 2]); }
        }

        const double fps = sr / hop;
        int cur = -1; size_t start = 0; float velMax = 0;
        auto close = [&] (size_t end)
        {
            if (cur >= 0 && (end - start) / fps >= 0.06)
                notes.push_back ({ start / fps, (end - start) / fps, cur, juce::jlimit (0.2f, 1.0f, velMax / peak * 1.3f) });
            cur = -1; velMax = 0;
        };
        for (size_t i = 0; i < mp.size(); ++i)
        {
            const bool reattack = i > 2 && rms[i] > rms[i - 2] * 1.8f && rms[i] > gate * 3;   // note répétée
            if (mp[i] != cur || (reattack && cur >= 0 && (i - start) > 4))
            {
                close (i);
                if (mp[i] >= 0) { cur = mp[i]; start = i; }
            }
            if (cur >= 0) velMax = juce::jmax (velMax, rms[i]);
        }
        close (mp.size());
        return notes;
    }

    // Accords / polyphonie : pics spectraux + suppression des harmoniques
    inline std::vector<Note> polyToMidi (const std::vector<float>& x, float sensitivity)
    {
        std::vector<Note> notes;
        auto s = spectrogram (x, 12, 512);
        const double fps = sr / 512.0;
        float global = 1e-9f;
        for (auto& fr : s.frames) for (float v : fr) global = juce::jmax (global, v);
        const float floorLevel = global * std::pow (10.0f, (-48.0f + 20.0f * (1.0f - sensitivity)) / 20.0f);

        std::vector<int> startFrame (128, -1), missing (128, 0);
        std::vector<float> vel (128, 0.0f);
        auto endNote = [&] (int p, size_t f)
        {
            if (startFrame[(size_t) p] >= 0)
            {
                const double len = (f - (size_t) startFrame[(size_t) p]) / fps;
                if (len >= 0.08) notes.push_back ({ startFrame[(size_t) p] / fps, len, p, juce::jlimit (0.2f, 1.0f, vel[(size_t) p] / global * 2.5f) });
                startFrame[(size_t) p] = -1; vel[(size_t) p] = 0;
            }
        };

        for (size_t f = 0; f < s.frames.size(); ++f)
        {
            const auto& m = s.frames[f];
            // saillance de chaque note MIDI = somme des 5 premières harmoniques
            float sal[128] {};
            float frameMax = 0;
            for (int p = 28; p <= 100; ++p)
            {
                const double f0 = 440.0 * std::pow (2.0, (p - 69) / 12.0);
                float sum = 0;
                for (int h = 1; h <= 5; ++h)
                {
                    const int b = (int) std::round (f0 * h * s.size / sr);
                    if (b >= s.bins - 1) break;
                    sum += juce::jmax (m[(size_t) b], juce::jmax (m[(size_t) b - 1], m[(size_t) b + 1])) / (float) h;
                }
                // il faut que la fondamentale elle-même soit présente
                const int b0 = (int) std::round (f0 * s.size / sr);
                if (b0 < 1 || m[(size_t) b0] < floorLevel) sum = 0;
                sal[p] = sum;
                frameMax = juce::jmax (frameMax, sum);
            }
            bool active[128] {};
            int count = 0;
            for (int k = 0; k < 6 && frameMax > 0; ++k)
            {
                int bestP = -1; float bestV = frameMax * (0.25f + 0.25f * (1.0f - sensitivity));
                for (int p = 28; p <= 100; ++p) if (! active[p] && sal[p] > bestV) { bestV = sal[p]; bestP = p; }
                if (bestP < 0) break;
                active[bestP] = true; ++count;
                for (int h = 2; h <= 6; ++h)   // retire les harmoniques de la note trouvée
                {
                    const int hp = bestP + (int) std::round (12.0 * std::log2 ((double) h));
                    if (hp <= 127) sal[hp] *= 0.15f;
                }
                sal[bestP] = 0;
                if (bestP > 0) sal[bestP - 1] *= 0.3f;
                if (bestP < 127) sal[bestP + 1] *= 0.3f;
            }
            for (int p = 0; p < 128; ++p)
            {
                if (active[p])
                {
                    missing[(size_t) p] = 0;
                    if (startFrame[(size_t) p] < 0) startFrame[(size_t) p] = (int) f;
                    vel[(size_t) p] = juce::jmax (vel[(size_t) p], sal[p] > 0 ? sal[p] : frameMax);
                }
                else if (startFrame[(size_t) p] >= 0 && ++missing[(size_t) p] > 2)
                    endNote (p, f - 2);
            }
            juce::ignoreUnused (count);
        }
        for (int p = 0; p < 128; ++p) endNote (p, s.frames.size());
        std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
        return notes;
    }

    // Convertit secondes -> temps (beats) et quantifie
    inline void toBeats (std::vector<Note>& notes, double bpm, double grid)
    {
        const double k = juce::jmax (20.0, bpm) / 60.0;
        for (auto& n : notes)
        {
            n.start *= k; n.length *= k;
            if (grid > 0)
            {
                const double s = std::round (n.start / grid) * grid;
                const double e = juce::jmax (s + grid, std::round ((n.start + n.length) / grid) * grid);
                n.start = s; n.length = e - s;
            }
        }
    }
}
