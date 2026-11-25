#pragma once
// Stretch de qualité pour les one-shots enregistrés à un tempo (140 BPM) :
// chaque note est pré-calculée en arrière-plan (transposition SANS changer la durée + calage sur le tempo de FL).
// Une note aiguë n'est donc plus plus rapide, une grave plus lente : les accords d'arpèges restent ensemble.
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <array>
#include <vector>
#include <memory>
#include <functional>

namespace stretch
{
    // WSOLA (overlap-add avec recherche de la meilleure jonction) : durée x factor, hauteur inchangée
    inline juce::AudioBuffer<float> wsola (const juce::AudioBuffer<float>& in, double factor, double sr)
    {
        const int ch = in.getNumChannels(), len = in.getNumSamples();
        if (len < 64 || std::abs (factor - 1.0) < 1.0e-4) { juce::AudioBuffer<float> c; c.makeCopyOf (in); return c; }
        const int N = juce::jmax (256, (int) std::round (sr * 0.040)) & ~1, hop = N / 2;
        const int tol = (int) std::round (sr * 0.012);
        const int outLen = (int) std::round (len * factor);
        juce::AudioBuffer<float> out (ch, outLen + N);
        out.clear();
        std::vector<float> win ((size_t) N);
        for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N);
        std::vector<float> mono ((size_t) len);
        for (int i = 0; i < len; ++i) { float s = 0.0f; for (int c = 0; c < ch; ++c) s += in.getSample (c, i); mono[(size_t) i] = s / (float) ch; }
        auto at = [&] (int k) { return (k >= 0 && k < len) ? mono[(size_t) k] : 0.0f; };
        auto corr = [&] (int a, int b, int step)
        {
            float s = 0.0f;
            for (int i = 0; i < hop; i += step) s += at (a + i) * at (b + i);
            return s;
        };
        int prev = 0;
        for (int op = 0; op < outLen; op += hop)
        {
            int best = (int) std::round (op / factor);
            if (op > 0)
            {
                const int natural = prev + hop;   // ce qui suivrait naturellement le grain précédent
                const int ideal = best;
                float bv = -1.0e30f;
                for (int d = -tol; d <= tol; d += 4) { const float v = corr (ideal + d, natural, 4); if (v > bv) { bv = v; best = ideal + d; } }
                const int coarse = best;
                for (int d = -3; d <= 3; ++d) { const float v = corr (coarse + d, natural, 2); if (v > bv) { bv = v; best = coarse + d; } }
            }
            best = juce::jlimit (0, juce::jmax (0, len - 1), best);
            for (int c = 0; c < ch; ++c)
            {
                const float* src = in.getReadPointer (c);
                float* dst = out.getWritePointer (c);
                for (int i = 0; i < N && op + i < outLen + N; ++i)
                {
                    const int k = best + i;
                    if (k >= len) break;
                    dst[op + i] += src[k] * win[(size_t) i];
                }
            }
            prev = best;
        }
        // le tout début reste tel quel (attaque intacte) : fondu vers le WSOLA sur 5 ms
        const int keep = juce::jmin (hop, len, outLen);
        for (int c = 0; c < ch; ++c)
            for (int i = 0; i < keep; ++i)
            {
                const float t = (float) i / (float) keep;
                out.setSample (c, i, in.getSample (c, i) * (1.0f - t) + out.getSample (c, i) * t);
            }
        out.setSize (ch, outLen, true);
        return out;
    }

    // relecture à vitesse "speed" (transposition) avec filtre anti-repliement quand on monte
    inline juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>& in, double speed, double sr)
    {
        const int ch = in.getNumChannels(), len = in.getNumSamples();
        const int outLen = juce::jmax (1, (int) std::floor ((len - 2) / speed));
        juce::AudioBuffer<float> src;
        src.makeCopyOf (in);
        if (speed > 1.02)
        {
            // 4 pôles (2 biquads) sous la nouvelle fréquence de Nyquist
            const double fc = juce::jmin (0.45 * sr / speed, 0.45 * sr), w = juce::MathConstants<double>::twoPi * fc / sr;
            for (double q : { 0.5412, 1.3066 })
            {
                const double cw = std::cos (w), al = std::sin (w) / (2.0 * q), a0 = 1.0 + al;
                const float b0 = (float) ((1.0 - cw) / 2.0 / a0), b1 = (float) ((1.0 - cw) / a0), b2 = b0, a1 = (float) (-2.0 * cw / a0), a2 = (float) ((1.0 - al) / a0);
                for (int c = 0; c < ch; ++c)
                {
                    float* d = src.getWritePointer (c); float z1 = 0, z2 = 0;
                    for (int i = 0; i < len; ++i) { const float x = d[i]; const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; d[i] = y; }
                }
            }
        }
        juce::AudioBuffer<float> out (ch, outLen);
        for (int c = 0; c < ch; ++c)
        {
            const float* d = src.getReadPointer (c);
            float* o = out.getWritePointer (c);
            auto at = [d, len] (int k) { return (k >= 0 && k < len) ? d[k] : 0.0f; };
            for (int i = 0; i < outLen; ++i)
            {
                const double p = i * speed;
                const int k = (int) p; const float t = (float) (p - k);
                const float xm1 = at (k - 1), x0 = at (k), x1 = at (k + 1), x2 = at (k + 2);
                const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
                o[i] = ((c3 * t + c2) * t + c1) * t + x0;
            }
        }
        return out;
    }

    // Phase vocoder à phase verrouillée (Laroche-Dolson) + recalage des phases sur les attaques :
    // durée x factor sans changer la hauteur, propre même pour de grands écarts (là où le WSOLA grésillait)
    // shouldAbort : appelé régulièrement, true = on arrête tout de suite (fermeture de FL, autre son) -> buffer vide
    inline juce::AudioBuffer<float> pvStretch (const juce::AudioBuffer<float>& in, double factor, const std::function<bool()>& shouldAbort = {})
    {
        const int ch = in.getNumChannels(), len = in.getNumSamples();
        if (len < 64 || std::abs (factor - 1.0) < 1.0e-4) { juce::AudioBuffer<float> c; c.makeCopyOf (in); return c; }
        constexpr int order = 12, N = 1 << order, Hs = N / 4, D = 256;   // D : petit décalage pour mesurer la fréquence exacte
        const double Ha = Hs / factor;
        const int outLen = (int) std::round (len * factor);
        juce::dsp::FFT fft (order);
        std::vector<float> win ((size_t) N);
        for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N);
        juce::AudioBuffer<float> out (ch, outLen + N);
        out.clear();
        std::vector<float> norm ((size_t) (outLen + N), 0.0f);
        const int bins = N / 2 + 1;
        std::vector<float> buf ((size_t) N * 2), buf2 ((size_t) N * 2), mag ((size_t) bins), ph ((size_t) bins), omega ((size_t) bins), prevMag ((size_t) bins);
        std::vector<float> synth ((size_t) bins);
        std::vector<int> peakOf ((size_t) bins);
        const double twoPi = juce::MathConstants<double>::twoPi;
        auto princ = [twoPi] (double a) { return a - twoPi * std::round (a / twoPi); };
        const int frames = (int) std::ceil ((double) outLen / Hs) + 2;
        for (int c = 0; c < ch; ++c)
        {
            const float* x = in.getReadPointer (c);
            auto at = [x, len] (int k) { return (k >= 0 && k < len) ? x[k] : 0.0f; };
            std::fill (synth.begin(), synth.end(), 0.0f);
            std::fill (prevMag.begin(), prevMag.end(), 0.0f);
            for (int f = 0; f < frames; ++f)
            {
                if (shouldAbort && (f & 7) == 0 && shouldAbort()) return juce::AudioBuffer<float>();
                const int tOut = f * Hs - N / 2;                         // trame centrée sur f * Hs
                const int tIn = (int) std::round (f * Ha) - N / 2;
                for (int i = 0; i < N; ++i) { buf[(size_t) i] = at (tIn + i) * win[(size_t) i]; buf2[(size_t) i] = at (tIn - D + i) * win[(size_t) i]; }
                std::fill (buf.begin() + N, buf.end(), 0.0f); std::fill (buf2.begin() + N, buf2.end(), 0.0f);
                fft.performRealOnlyForwardTransform (buf.data(), true);
                fft.performRealOnlyForwardTransform (buf2.data(), true);
                double flux = 0.0, energy = 1.0e-9;
                for (int k = 0; k < bins; ++k)
                {
                    const float re = buf[(size_t) (2 * k)], im = buf[(size_t) (2 * k + 1)];
                    mag[(size_t) k] = std::sqrt (re * re + im * im);
                    ph[(size_t) k] = std::atan2 (im, re);
                    const float p2 = std::atan2 (buf2[(size_t) (2 * k + 1)], buf2[(size_t) (2 * k)]);
                    const double expected = twoPi * k * D / N;
                    omega[(size_t) k] = (float) ((expected + princ (ph[(size_t) k] - p2 - expected)) / D);   // radians par échantillon
                    flux += juce::jmax (0.0f, mag[(size_t) k] - prevMag[(size_t) k]);
                    energy += mag[(size_t) k];
                }
                const bool transient = f == 0 || flux / energy > 0.35;   // attaque : on reprend les phases d'origine (attaque nette)
                if (transient)
                    for (int k = 0; k < bins; ++k) synth[(size_t) k] = ph[(size_t) k];
                else
                {
                    // pics du spectre : leur phase avance à leur vraie fréquence, les voisins suivent leur pic
                    for (int k = 0; k < bins; ++k) peakOf[(size_t) k] = -1;
                    std::vector<int> peaks;
                    for (int k = 2; k < bins - 2; ++k)
                        if (mag[(size_t) k] > mag[(size_t) k - 1] && mag[(size_t) k] >= mag[(size_t) k + 1] && mag[(size_t) k] > mag[(size_t) k - 2] && mag[(size_t) k] >= mag[(size_t) k + 2])
                            peaks.push_back (k);
                    if (peaks.empty()) peaks.push_back (1);
                    size_t pi = 0;
                    for (int k = 0; k < bins; ++k)
                    {
                        while (pi + 1 < peaks.size() && std::abs (peaks[pi + 1] - k) < std::abs (peaks[pi] - k)) ++pi;
                        peakOf[(size_t) k] = peaks[pi];
                    }
                    std::vector<float> nextPeakPhase ((size_t) bins);
                    for (int p : peaks) nextPeakPhase[(size_t) p] = (float) princ (synth[(size_t) p] + omega[(size_t) p] * Hs);
                    for (int k = 0; k < bins; ++k)
                    {
                        const int p = peakOf[(size_t) k];
                        synth[(size_t) k] = (float) princ (nextPeakPhase[(size_t) p] + ph[(size_t) k] - ph[(size_t) p]);
                    }
                }
                for (int k = 0; k < bins; ++k) prevMag[(size_t) k] = mag[(size_t) k];
                for (int k = 0; k < bins; ++k)
                {
                    buf[(size_t) (2 * k)] = mag[(size_t) k] * std::cos (synth[(size_t) k]);
                    buf[(size_t) (2 * k + 1)] = mag[(size_t) k] * std::sin (synth[(size_t) k]);
                }
                fft.performRealOnlyInverseTransform (buf.data());
                float* o = out.getWritePointer (c);
                for (int i = 0; i < N; ++i)
                {
                    const int t = tOut + i;
                    if (t < 0 || t >= outLen + N) continue;
                    o[t] += buf[(size_t) i] * win[(size_t) i];
                    if (c == 0) norm[(size_t) t] += win[(size_t) i] * win[(size_t) i];
                }
            }
        }
        for (int c = 0; c < ch; ++c)
        {
            float* o = out.getWritePointer (c);
            for (int t = 0; t < outLen; ++t) o[t] /= juce::jmax (0.1f, norm[(size_t) t]);
        }
        // l'attaque du tout début reste celle d'origine (fondu sur 3 ms)
        const int keep = juce::jmin (132, len, outLen);
        for (int c = 0; c < ch; ++c)
            for (int i = 0; i < keep; ++i) { const float t = (float) i / (float) keep; out.setSample (c, i, in.getSample (c, i) * (1.0f - t) + out.getSample (c, i) * t); }
        out.setSize (ch, outLen, true);
        return out;
    }

    // Analyse d'un one-shot d'arpège : les attaques (début de chaque note) et la hauteur de chaque note
    struct ArpAnalysis { std::vector<int> onsets; std::vector<float> midi; bool arpLike = false; };   // midi < 0 : pas de hauteur claire ; arpLike = vraiment un motif de notes
    inline ArpAnalysis analyseArp (const juce::AudioBuffer<float>& in, double sr)
    {
        ArpAnalysis a;
        const int len = in.getNumSamples(), ch = in.getNumChannels();
        if (len < 512) return a;
        std::vector<float> mono ((size_t) len);
        for (int i = 0; i < len; ++i) { float s = 0; for (int c = 0; c < ch; ++c) s += in.getSample (c, i); mono[(size_t) i] = s / (float) ch; }
        // flux spectral sur trames de 1024, pas de 256
        constexpr int order = 10, N = 1 << order, H = 256;
        juce::dsp::FFT fft (order);
        std::vector<float> win ((size_t) N), buf ((size_t) N * 2), prev ((size_t) N / 2 + 1, 0.0f);
        for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N);
        std::vector<float> flux;
        for (int t = 0; t + N <= len; t += H)
        {
            for (int i = 0; i < N; ++i) buf[(size_t) i] = mono[(size_t) (t + i)] * win[(size_t) i];
            std::fill (buf.begin() + N, buf.end(), 0.0f);
            fft.performFrequencyOnlyForwardTransform (buf.data(), true);
            float f = 0.0f;
            for (int k = 1; k <= N / 2; ++k) { const float m = std::log1p (buf[(size_t) k] * 10.0f); f += juce::jmax (0.0f, m - prev[(size_t) k]); prev[(size_t) k] = m; }
            flux.push_back (f);
        }
        a.onsets.push_back (0);
        const int minGap = (int) (0.07 * sr / H);
        float maxFlux = 0.0f;
        for (size_t i = 2; i < flux.size(); ++i) maxFlux = juce::jmax (maxFlux, flux[i]);
        const float floorFlux = juce::jmax (1.0f, 0.04f * maxFlux);   // seuil relatif au son : sons doux ou très compressés aussi
        for (size_t i = 2; i + 2 < flux.size(); ++i)
        {
            float mean = 0.0f; int cnt = 0;
            for (int j = (int) i - 8; j <= (int) i + 8; ++j) if (j >= 0 && j < (int) flux.size()) { mean += flux[(size_t) j]; ++cnt; }
            mean /= (float) juce::jmax (1, cnt);
            const bool peak = flux[i] > flux[i - 1] && flux[i] >= flux[i + 1] && flux[i] > mean * 1.4f + floorFlux;
            const int pos = (int) i * H + N / 2 - H;
            if (! peak || pos - a.onsets.back() < minGap * H) continue;
            // une fin de note coupée net fait aussi un "clic" : ce n'est pas une attaque si le son baisse juste après
            // (sinon la vraie note suivante, trop proche, était ignorée et tout le motif se décalait)
            auto energy = [&] (int from, int to)
            {
                from = juce::jlimit (0, len, from); to = juce::jlimit (0, len, to);
                double e = 0.0; for (int k = from; k < to; ++k) e += (double) mono[(size_t) k] * mono[(size_t) k];
                return to > from ? e / (to - from) : 0.0;
            };
            const double before = energy (pos - (int) (0.035 * sr), pos - (int) (0.008 * sr));
            const double after = energy (pos + (int) (0.004 * sr), pos + (int) (0.040 * sr));
            if (after < before * 0.12) continue;   // (en énergie : -9 dB : le son s arrête presque, ce n est pas une nouvelle note)
            a.onsets.push_back (pos);
        }
        // position exacte de chaque attaque : là où l'enveloppe (64 échantillons) dépasse la moitié de son maximum local
        for (size_t s = 1; s < a.onsets.size(); ++s)
        {
            const int c = a.onsets[s], from = juce::jmax (a.onsets[s - 1] + 64, c - N), to = juce::jmin (len - 64, c + N);
            float mx = 0.0f;
            std::vector<float> env;
            for (int t = from; t < to; t += 16) { float e = 0.0f; for (int i = 0; i < 64; ++i) e = juce::jmax (e, std::abs (mono[(size_t) (t + i)])); env.push_back (e); mx = juce::jmax (mx, e); }
            float mn = 1.0e9f; size_t mnAt = 0;   // le creux juste avant l'attaque
            size_t peakAt = 0; for (size_t i = 0; i < env.size(); ++i) if (env[i] >= mx * 0.999f) { peakAt = i; break; }
            for (size_t i = 0; i <= peakAt; ++i) if (env[i] < mn) { mn = env[i]; mnAt = i; }
            for (size_t i = mnAt; i <= peakAt; ++i) if (env[i] >= mn + (mx - mn) * 0.5f) { a.onsets[s] = from + (int) i * 16; break; }
        }
        // hauteur de chaque note (autocorrélation normalisée sur le début de la note)
        for (size_t s = 0; s < a.onsets.size(); ++s)
        {
            const int st = a.onsets[s] + (int) (0.015 * sr);
            const int end = s + 1 < a.onsets.size() ? a.onsets[s + 1] : len;
            const int W = juce::jmin (2048, end - st);
            float best = 0.0f; int bestLag = 0;
            const int minLag = (int) (sr / 1500.0), maxLag = juce::jmin ((int) (sr / 40.0), W / 2);
            std::vector<float> rs;
            for (int lag = minLag; lag < maxLag; ++lag)
            {
                double num = 0, e1 = 0, e2 = 0;
                for (int i = 0; i + lag < W; i += 2) { const float x0 = mono[(size_t) (st + i)], x1 = mono[(size_t) (st + i + lag)]; num += x0 * x1; e1 += x0 * x0; e2 += x1 * x1; }
                const float r = (float) (num / std::sqrt (e1 * e2 + 1.0e-12));
                rs.push_back (r);
                if (r > best) best = r;
            }
            // la première crête presque aussi haute que la meilleure = la vraie période (pas un multiple : erreur d'octave)
            for (size_t j = 1; j + 1 < rs.size(); ++j)
                if (rs[j] >= best * 0.92f && rs[j] >= rs[j - 1] && rs[j] >= rs[j + 1]) { bestLag = minLag + (int) j; break; }
            a.midi.push_back (W > 256 && best > 0.75f && bestLag > 0 ? (float) (69.0 + 12.0 * std::log2 (sr / bestLag / 440.0)) : -1.0f);
        }
        {
            // vrai motif de notes (au moins 3 attaques et 2 hauteurs différentes) : sinon c est un son simple (piano, pad…)
            std::vector<int> pcs;
            for (float m : a.midi) if (m >= 0.0f) { const int nt = (int) std::round (m); if (std::find (pcs.begin(), pcs.end(), nt) == pcs.end()) pcs.push_back (nt); }
            a.arpLike = a.onsets.size() >= 3 && (pcs.size() >= 2 || a.onsets.size() >= 4);   // (4 coups sur la même note = séquence rythmique)
        }
        return a;
    }

    // Le son ressemble-t-il à un arpège (même sans "arp" dans le nom) : au moins 4 notes, au moins 2 hauteurs
    // différentes, et des attaques régulières (calées sur une grille rythmique), pas une phrase libre
    inline bool looksLikeArp (const ArpAnalysis& a)
    {
        if (a.onsets.size() < 4) return false;
        std::vector<int> gaps;
        for (size_t i = 1; i < a.onsets.size(); ++i) gaps.push_back (a.onsets[i] - a.onsets[i - 1]);
        auto sorted = gaps;
        std::sort (sorted.begin(), sorted.end());
        const double med = (double) sorted[sorted.size() / 2];
        if (med <= 0.0) return false;
        int regular = 0;
        for (int g : gaps)
        {
            const double r = g / med;
            const double nearest = juce::jmax (0.5, std::round (r * 2.0) / 2.0);   // 1/2, 1, 1,5, 2… du pas le plus courant
            if (std::abs (r - nearest) < 0.15 * nearest) ++regular;
        }
        int pitched = 0; std::vector<int> notes;
        for (float m : a.midi) if (m >= 0.0f) { ++pitched; const int nt = (int) std::round (m); if (std::find (notes.begin(), notes.end(), nt) == notes.end()) notes.push_back (nt); }
        return regular * 10 >= (int) gaps.size() * 7 && notes.size() >= 2 && pitched * 2 >= (int) a.midi.size();
    }

    // note transposée de "semis" demi-tons, durée multipliée par tempoFactor (= BPM du son / BPM de FL)
    inline juce::AudioBuffer<float> render (const juce::AudioBuffer<float>& src, double sr, int semis, double tempoFactor,
                                            const std::function<bool()>& shouldAbort = {})
    {
        const double ratio = std::pow (2.0, semis / 12.0);
        if (semis == 0) return pvStretch (src, tempoFactor, shouldAbort);
        auto st = pvStretch (src, ratio * tempoFactor, shouldAbort);
        if (st.getNumSamples() == 0) return st;   // interrompu
        return resample (st, ratio, sr);
    }

    //==========================================================================
    // Versions pré-calculées par couche et par note (thread de fond). Le thread audio ne fait que lire des pointeurs.
    // Chaque son est reconnu par un identifiant unique (et plus par son adresse mémoire : un nouveau son chargé
    // à la même adresse que l'ancien reprenait les notes calculées de l'ancien son).
    class Cache : private juce::Thread
    {
    public:
        static constexpr int span = 36, numSemis = span * 2 + 1;   // -36 .. +36 demi-tons
        static constexpr int numLayers = 8;

        // fournit la source d'une couche, son identifiant (et garde le son vivant pendant le calcul)
        std::function<std::shared_ptr<void> (int layer, const juce::AudioBuffer<float>*& src, double& rate, juce::uint32& id)> getSource;

        Cache() : juce::Thread ("Variant stretch") {}
        ~Cache() override { stop(); }
        void start() { if (! isThreadRunning()) startThread (juce::Thread::Priority::low); }
        void stop() { signalThreadShouldExit(); notify(); stopThread (4000); }

        // thread audio : la version prête (ou nullptr, et la demande part au thread de fond)
        const juce::AudioBuffer<float>* get (int layer, int semis, juce::uint32 srcId, int tempoKey)
        {
            if (layer < 0 || layer >= numLayers || std::abs (semis) > span || srcId == 0) return nullptr;
            auto& L = layers[(size_t) layer];
            L.wantSrc.store (srcId); L.wantTempo.store (tempoKey);
            if (L.srcKey.load() == srcId && L.tempoKey.load() == tempoKey)
                if (auto* b = L.slots[(size_t) (semis + span)].load()) return b;
            L.wanted[(size_t) (semis + span)].store (true);
            notify();
            return nullptr;
        }
        // lecture seule (sans demander le calcul)
        const juce::AudioBuffer<float>* peek (int layer, int semis, juce::uint32 srcId, int tempoKey) const
        {
            if (layer < 0 || layer >= numLayers || std::abs (semis) > span || srcId == 0) return nullptr;
            auto& L = layers[(size_t) layer];
            if (L.srcKey.load() == srcId && L.tempoKey.load() == tempoKey) return L.slots[(size_t) (semis + span)].load();
            return nullptr;
        }
        // stretch allumé sur une couche : on prépare d'avance les notes autour de la note racine
        void prepareLayer (int layer, juce::uint32 srcId, int tempoKey)
        {
            if (layer < 0 || layer >= numLayers || srcId == 0) return;
            auto& L = layers[(size_t) layer];
            const bool changed = L.wantSrc.load() != srcId || L.wantTempo.load() != tempoKey || ! L.warm.load();
            L.wantSrc.store (srcId); L.wantTempo.store (tempoKey);
            L.warm.store (true);
            if (changed) notify();
        }
        int readyCount (int layer) const
        {
            int n = 0;
            for (auto& s : layers[(size_t) juce::jlimit (0, numLayers - 1, layer)].slots) if (s.load() != nullptr) ++n;
            return n;
        }

    private:
        struct Layer
        {
            std::array<std::atomic<const juce::AudioBuffer<float>*>, numSemis> slots {};
            std::array<std::atomic<bool>, numSemis> wanted {};
            std::atomic<juce::uint32> srcKey { 0 }, wantSrc { 0 };
            std::atomic<int> tempoKey { 0 }, wantTempo { 0 };
            std::atomic<bool> warm { false };
        };
        std::array<Layer, numLayers> layers;
        std::vector<std::unique_ptr<juce::AudioBuffer<float>>> owned;
        struct Dead { std::unique_ptr<juce::AudioBuffer<float>> buf; juce::uint32 at; };
        std::vector<Dead> graveyard;   // les voix peuvent encore lire une ancienne version : libérée plus tard

        void retire (const juce::AudioBuffer<float>* b)
        {
            for (auto it = owned.begin(); it != owned.end(); ++it)
                if (it->get() == b) { graveyard.push_back ({ std::move (*it), juce::Time::getMillisecondCounter() }); owned.erase (it); return; }
        }

        void run() override
        {
            while (! threadShouldExit())
            {
                bool did = false;
                for (int l = 0; l < numLayers && ! threadShouldExit(); ++l)
                {
                    auto& L = layers[(size_t) l];
                    const juce::uint32 ws = L.wantSrc.load();
                    const int wt = L.wantTempo.load();
                    if (ws == 0) continue;
                    if (ws != L.srcKey.load() || wt != L.tempoKey.load())
                    {
                        // autre son ou autre tempo : tout est à refaire
                        L.srcKey.store (0);
                        for (auto& s : L.slots) { if (auto* b = s.exchange (nullptr)) retire (b); }
                        L.tempoKey.store (wt);
                        L.srcKey.store (ws);
                    }
                    // quelle note calculer : d'abord celles demandées, puis autour de la racine (-24..+24)
                    int pick = -1000;
                    for (int k = 0; k < numSemis; ++k) if (L.wanted[(size_t) k].load() && L.slots[(size_t) k].load() == nullptr) { pick = k - span; break; }
                    if (pick == -1000 && L.warm.load())
                        for (int d = 0; d <= 12 && pick == -1000; ++d)   // d avance : une octave autour (le reste à la demande)
                            for (int s : { d, -d }) if (L.slots[(size_t) (s + span)].load() == nullptr) { pick = s; break; }
                    if (pick == -1000) continue;
                    const juce::AudioBuffer<float>* src = nullptr; double rate = 44100.0; juce::uint32 id = 0;
                    auto keepAlive = getSource ? getSource (l, src, rate, id) : nullptr;
                    if (src == nullptr || id != ws) { juce::uint32 expected = ws; L.wantSrc.compare_exchange_strong (expected, 0u); continue; }
                    // calcul interrompu tout de suite si FL se ferme ou si le son / le tempo change (sinon FL restait figé à la fermeture)
                    auto abort = [this, &L, ws, wt] { return threadShouldExit() || L.wantSrc.load() != ws || L.wantTempo.load() != wt; };
                    auto buf = std::make_unique<juce::AudioBuffer<float>> (render (*src, rate, pick, wt / 1000.0, abort));
                    if (buf->getNumSamples() == 0 || threadShouldExit()) continue;
                    if (L.srcKey.load() != ws || L.tempoKey.load() != wt) continue;   // changé pendant le calcul
                    L.slots[(size_t) (pick + span)].store (buf.get());
                    L.wanted[(size_t) (pick + span)].store (false);
                    owned.push_back (std::move (buf));
                    did = true;
                }
                const auto now = juce::Time::getMillisecondCounter();
                graveyard.erase (std::remove_if (graveyard.begin(), graveyard.end(), [now] (const Dead& d) { return now - d.at > 60000; }), graveyard.end());
                if (! did) wait (200);
            }
        }
    };
}
