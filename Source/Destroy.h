#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <vector>

// « Métamorphose » : transforme un one-shot en un autre son, toujours propre et musical.
// Des transformations « de studio » (pas de bitcrush ni de distorsion dure) : nappe étirée, cristal, grave, reverse swell,
// pluck, grains doux, chorus large, résonateur accordé, bande douce. Fondus partout, saturation suréchantillonnée,
// nettoyage final (coupe-bas, aigus adoucis, limiteur doux) : pas de clic, pas de grésillement.
// intensity : 0 = garde le caractère (1 transformation, mélangée), 1 = moyen (2), 2 = méconnaissable (3).
namespace destroy
{
    using Buf = juce::AudioBuffer<float>;
    constexpr float twoPi = juce::MathConstants<float>::twoPi;
    constexpr float pi = juce::MathConstants<float>::pi;

    inline float peakOf (const Buf& b) { float p = 0.0f; for (int c = 0; c < b.getNumChannels(); ++c) p = juce::jmax (p, b.getMagnitude (c, 0, b.getNumSamples())); return p; }
    inline void normalise (Buf& b, float target = 0.89f) { const float p = peakOf (b); if (p > 1.0e-6f) b.applyGain (target / p); }
    inline Buf stereo (const Buf& in) { Buf b (2, in.getNumSamples()); for (int c = 0; c < 2; ++c) b.copyFrom (c, 0, in, juce::jmin (c, in.getNumChannels() - 1), 0, in.getNumSamples()); return b; }

    // filtre à variable d'état (TPT) : 0 passe-bas, 1 passe-haut, 2 passe-bande
    struct Svf
    {
        float ic1 = 0.0f, ic2 = 0.0f;
        float process (float x, float fc, float q, double sr, int mode)
        {
            const float g = std::tan (pi * juce::jlimit (10.0f, (float) sr * 0.45f, fc) / (float) sr), k = 1.0f / juce::jmax (0.3f, q);
            const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
            const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
            return mode == 0 ? v2 : (mode == 1 ? x - k * v1 - v2 : v1);
        }
    };

    inline void filter (Buf& b, double sr, float fc, int mode, float q = 0.707f)
    {
        for (int c = 0; c < b.getNumChannels(); ++c) { Svf f; auto* d = b.getWritePointer (c); for (int i = 0; i < b.getNumSamples(); ++i) d[i] = f.process (d[i], fc, q, sr, mode); }
    }

    // lecture à vitesse variable, interpolation cubique (pas d'aliasing grossier)
    inline float cubic (const float* s, int n, double p)
    {
        const int i = (int) p; const float t = (float) (p - i);
        auto at = [&] (int k) { return s[juce::jlimit (0, n - 1, k)]; };
        const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
        return y1 + 0.5f * t * (y2 - y0 + t * (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3 + t * (3.0f * (y1 - y2) + y3 - y0)));
    }
    inline Buf resample (const Buf& in, double ratio, double sr)
    {
        Buf src (in);
        if (ratio > 1.0) filter (src, sr, (float) (sr * 0.45 / ratio), 0, 0.6f);   // anti-repliement avant de monter
        const int n = juce::jmax (16, (int) (in.getNumSamples() / ratio));
        Buf out (in.getNumChannels(), n);
        for (int c = 0; c < in.getNumChannels(); ++c)
        {
            const float* s = src.getReadPointer (c); auto* d = out.getWritePointer (c);
            for (int i = 0; i < n; ++i) { const double p = i * ratio; d[i] = p < in.getNumSamples() - 1 ? cubic (s, in.getNumSamples(), p) : 0.0f; }
        }
        return out;
    }

    // saturation douce suréchantillonnée x4 (pas de grésillement)
    inline void warmth (Buf& b, float drive)
    {
        juce::dsp::Oversampling<float> os ((size_t) b.getNumChannels(), 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
        os.initProcessing ((size_t) b.getNumSamples());
        juce::dsp::AudioBlock<float> block (b);
        auto up = os.processSamplesUp (block);
        const float norm = 1.0f / std::tanh (drive);
        for (size_t c = 0; c < up.getNumChannels(); ++c)
        {
            auto* d = up.getChannelPointer (c);
            for (size_t i = 0; i < up.getNumSamples(); ++i) d[i] = std::tanh (d[i] * drive) * norm;
        }
        os.processSamplesDown (block);
    }

    // hauteur du son (autocorrélation sur le début) : pour accorder le résonateur
    inline double pitchOf (const Buf& b, double sr)
    {
        const int n = juce::jmin (b.getNumSamples(), (int) (sr * 0.35));
        const float* s = b.getReadPointer (0);
        const int lo = (int) (sr / 1000.0), hi = juce::jmin (n / 2, (int) (sr / 50.0));
        double best = 0.0; int bestLag = 0;
        for (int lag = lo; lag < hi; ++lag)
        {
            double acc = 0.0;
            for (int i = 0; i + lag < n; i += 2) acc += s[i] * s[i + lag];
            if (acc > best) { best = acc; bestLag = lag; }
        }
        return bestLag > 0 ? sr / bestLag : 261.63;
    }

    // étirement sans changer la hauteur (grains Hann recouvrants à 75 %)
    inline Buf stretch (const Buf& in, double sr, double factor, juce::Random& r, float jitter)
    {
        const int n = in.getNumSamples();
        const int g = (int) (sr * 0.09), hop = g / 4;
        const int outLen = juce::jmin ((int) (n * factor), (int) (sr * 14.0));
        Buf out (in.getNumChannels(), outLen + g); out.clear();
        std::vector<float> win ((size_t) g);
        for (int k = 0; k < g; ++k) win[(size_t) k] = 0.5f - 0.5f * std::cos (twoPi * (float) k / (float) g);
        for (int pos = 0; pos < outLen; pos += hop)
        {
            int src = (int) (pos / factor) + (int) ((r.nextFloat() - 0.5f) * jitter * sr * 0.05f);
            src = juce::jlimit (0, juce::jmax (0, n - g - 1), src);
            for (int c = 0; c < in.getNumChannels(); ++c)
            {
                const float* s = in.getReadPointer (c); auto* d = out.getWritePointer (c);
                for (int k = 0; k < g && src + k < n; ++k) d[pos + k] += s[src + k] * win[(size_t) k] * 0.5f;   // 4 grains Hann = 2
            }
        }
        out.setSize (in.getNumChannels(), outLen, true);
        return out;
    }

    // enveloppe de volume (attaque, déclin exponentiel)
    inline void envelope (Buf& b, double sr, double attackS, double decayS)
    {
        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            auto* d = b.getWritePointer (c);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const double t = i / sr;
                const double a = attackS > 0.0 ? juce::jmin (1.0, t / attackS) : 1.0;
                const double e = decayS > 0.0 ? std::exp (-t / decayS) : 1.0;
                d[i] *= (float) (a * a * (3.0 - 2.0 * a) * e);
            }
        }
    }

    // petite diffusion (4 passe-tout) : un peu d'espace, sans réverbe envahissante
    inline void diffuse (Buf& b, double sr, float amount)
    {
        static const double ms[4] = { 4.7, 7.3, 11.1, 17.3 };
        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            auto* d = b.getWritePointer (c);
            for (int k = 0; k < 4; ++k)
            {
                const int len = juce::jmax (1, (int) (sr * ms[k] * 0.001 * (c ? 1.13 : 1.0)));
                std::vector<float> line ((size_t) len, 0.0f); int w = 0;
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const float del = line[(size_t) w];
                    const float y = -0.6f * d[i] + del;
                    line[(size_t) w] = d[i] + 0.6f * y;
                    w = (w + 1) % len;
                    d[i] = d[i] * (1.0f - amount) + y * amount;
                }
            }
        }
    }

    //--------------------------------------------------------------------------
    // les transformations (toutes « propres »)
    inline Buf tNappe (const Buf& in, double sr, juce::Random& r)
    {
        auto b = stretch (in, sr, 2.5 + 2.0 * r.nextFloat(), r, 0.8f);
        envelope (b, sr, 0.35 + 0.6 * r.nextFloat(), 0.0);
        filter (b, sr, 3500.0f + 4000.0f * r.nextFloat(), 0, 0.6f);
        diffuse (b, sr, 0.5f);
        return b;
    }
    inline Buf tCristal (const Buf& in, double sr, juce::Random& r)
    {
        Buf b (in);
        auto up = resample (in, 2.0, sr);
        envelope (up, sr, 0.004, 0.6 + r.nextFloat());
        for (int c = 0; c < b.getNumChannels(); ++c) b.addFrom (c, 0, up, c, 0, juce::jmin (b.getNumSamples(), up.getNumSamples()), 0.55f);
        filter (b, sr, 180.0f, 1);
        diffuse (b, sr, 0.35f);
        return b;
    }
    inline Buf tGrave (const Buf& in, double sr, juce::Random& r)
    {
        auto down = resample (in, 0.5, sr);
        Buf b (in.getNumChannels(), down.getNumSamples()); b.clear();
        for (int c = 0; c < b.getNumChannels(); ++c) { b.addFrom (c, 0, down, c, 0, down.getNumSamples(), 0.8f); b.addFrom (c, 0, in, c, 0, juce::jmin (in.getNumSamples(), b.getNumSamples()), 0.45f); }
        warmth (b, 1.6f + r.nextFloat());
        filter (b, sr, 2500.0f + 2500.0f * r.nextFloat(), 0, 0.6f);
        return b;
    }
    inline Buf tReverseSwell (const Buf& in, double sr, juce::Random& r)
    {
        const int n = in.getNumSamples();
        const int tail = juce::jmin (n, (int) (sr * (0.6 + 0.8 * r.nextFloat())));
        Buf b (in.getNumChannels(), tail + n); b.clear();
        for (int c = 0; c < in.getNumChannels(); ++c)
        {
            const float* s = in.getReadPointer (c); auto* d = b.getWritePointer (c);
            for (int i = 0; i < tail; ++i) { const float w = (float) i / (float) tail; d[i] = s[tail - 1 - i] * w * w; }   // montée inversée
            for (int i = 0; i < n; ++i) d[tail + i] += s[i];
        }
        diffuse (b, sr, 0.3f);
        return b;
    }
    inline Buf tPluck (const Buf& in, double sr, juce::Random& r)
    {
        Buf b (in);
        envelope (b, sr, 0.002, 0.12 + 0.25 * r.nextFloat());
        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            Svf f; auto* d = b.getWritePointer (c);
            for (int i = 0; i < b.getNumSamples(); ++i) { const float t = (float) (i / sr); d[i] = f.process (d[i], 400.0f + 9000.0f * std::exp (-t * 9.0f), 1.4f, sr, 0); }
        }
        b.setSize (b.getNumChannels(), juce::jmin (b.getNumSamples(), (int) (sr * 1.5)), true);
        return b;
    }
    inline Buf tGrains (const Buf& in, double sr, juce::Random& r)
    {
        const int n = in.getNumSamples();
        const int g = (int) (sr * (0.06 + 0.09 * r.nextFloat())), hop = g / 4;
        const int outLen = juce::jmin ((int) (sr * 8.0), juce::jmax (n, (int) (n * 1.8)));
        Buf out (in.getNumChannels(), outLen + g); out.clear();
        static const double ratios[] = { 1.0, 1.0, 2.0, 0.5, 1.4983 };
        for (int pos = 0; pos < outLen; pos += hop)
        {
            const double ratio = ratios[r.nextInt (5)];
            const int src = juce::jlimit (0, juce::jmax (0, n - (int) (g * ratio) - 2), (int) (pos * (double) n / outLen) + (int) ((r.nextFloat() - 0.5f) * sr * 0.15f));
            const float gain = 0.5f * (0.6f + 0.4f * r.nextFloat());
            for (int c = 0; c < in.getNumChannels(); ++c)
            {
                const float* s = in.getReadPointer (c); auto* d = out.getWritePointer (c);
                const float pan = c == 0 ? 0.8f + 0.2f * r.nextFloat() : 0.8f + 0.2f * r.nextFloat();
                for (int k = 0; k < g; ++k)
                {
                    const double p = src + k * ratio;
                    if (p >= n - 2) break;
                    const float w = 0.5f - 0.5f * std::cos (twoPi * (float) k / (float) g);
                    d[pos + k] += cubic (s, n, p) * w * gain * pan;
                }
            }
        }
        out.setSize (in.getNumChannels(), outLen, true);
        filter (out, sr, 9000.0f, 0, 0.6f);
        return out;
    }
    inline Buf tChorus (const Buf& in, double sr, juce::Random& r)
    {
        const int n = in.getNumSamples();
        Buf b (2, n); b.clear();
        const float depth = (float) (sr * 0.004), base = (float) (sr * 0.012);
        for (int v = 0; v < 3; ++v)
        {
            const float rate = 0.15f + 0.35f * r.nextFloat(), ph = r.nextFloat() * twoPi;
            for (int c = 0; c < 2; ++c)
            {
                const float* s = in.getReadPointer (juce::jmin (c, in.getNumChannels() - 1)); auto* d = b.getWritePointer (c);
                const float pan = v == 1 ? 0.6f : (v == 0 ? (c == 0 ? 0.9f : 0.3f) : (c == 0 ? 0.3f : 0.9f));
                for (int i = 0; i < n; ++i)
                {
                    const double delay = base + depth * (0.5f + 0.5f * std::sin (twoPi * rate * (float) (i / sr) + ph + c * 0.7f));
                    const double p = i - delay;
                    if (p > 0.0) d[i] += cubic (s, n, p) * pan * 0.55f;
                }
            }
        }
        for (int c = 0; c < 2; ++c) b.addFrom (c, 0, in, juce::jmin (c, in.getNumChannels() - 1), 0, n, 0.5f);
        return b;
    }
    inline Buf tResonateur (const Buf& in, double sr, juce::Random& r)
    {
        Buf b (in);
        const double f0 = pitchOf (in, sr) * (r.nextBool() ? 1.0 : 2.0);
        const float fb = 0.86f + 0.08f * r.nextFloat();
        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            const double period = sr / f0 * (c ? 1.002 : 1.0);
            std::vector<float> line ((size_t) ((int) period + 4), 0.0f);
            const int len = (int) line.size();
            int w = 0; float lp = 0.0f;
            auto* d = b.getWritePointer (c);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                double rp = w - period; while (rp < 0) rp += len;
                const int a = (int) rp; const float t = (float) (rp - a);
                const float del = line[(size_t) a] * (1.0f - t) + line[(size_t) ((a + 1) % len)] * t;
                lp += 0.35f * (del - lp);   // amorti : la résonance reste douce
                line[(size_t) w] = d[i] * 0.3f + lp * fb;
                w = (w + 1) % len;
                d[i] = d[i] * 0.5f + lp * 0.8f;
            }
        }
        return b;
    }
    inline Buf tBande (const Buf& in, double sr, juce::Random& r)
    {
        const int n = in.getNumSamples();
        Buf b (in.getNumChannels(), n);
        const float rate = 0.4f + 0.8f * r.nextFloat(), depth = (float) (sr * 0.0025);
        for (int c = 0; c < in.getNumChannels(); ++c)
        {
            const float* s = in.getReadPointer (c); auto* d = b.getWritePointer (c);
            for (int i = 0; i < n; ++i)
            {
                const double p = i - depth * (1.0 + std::sin (twoPi * rate * (float) (i / sr)));
                d[i] = p > 0.0 ? cubic (s, n, p) : 0.0f;
            }
        }
        warmth (b, 1.4f);
        filter (b, sr, 5500.0f + 2500.0f * r.nextFloat(), 0, 0.6f);
        return b;
    }

    //--------------------------------------------------------------------------
    // nettoyage final : coupe-bas, aigus adoucis, pas de DC, fondus, limiteur doux, niveau
    inline void finish (Buf& b, double sr)
    {
        filter (b, sr, 28.0f, 1, 0.6f);
        filter (b, sr, 16000.0f, 0, 0.5f);
        const int n = b.getNumSamples();
        const int fin = juce::jmin (n / 4, (int) (0.004 * sr)), fout = juce::jmin (n / 3, (int) (0.08 * sr));
        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            auto* d = b.getWritePointer (c);
            for (int i = 0; i < fin; ++i) d[i] *= (float) i / (float) juce::jmax (1, fin);
            for (int i = 0; i < fout; ++i) { const float w = (float) i / (float) juce::jmax (1, fout); d[n - 1 - i] *= w * w; }
            for (int i = 0; i < n; ++i) if (! std::isfinite (d[i])) d[i] = 0.0f;
        }
        normalise (b, 1.0f);
        for (int c = 0; c < b.getNumChannels(); ++c) { auto* d = b.getWritePointer (c); for (int i = 0; i < n; ++i) d[i] = std::tanh (d[i] * 1.1f) / std::tanh (1.1f); }
        normalise (b, 0.89f);
    }

    // la chaîne : 1 à 3 transformations tirées au hasard ; « Doux » garde une part du son d'origine
    inline Buf process (const Buf& source, double sr, int intensity, juce::Random& rIn, juce::StringArray& recipe)
    {
        // tirage bien mélangé (deux graines proches donnaient presque le même premier choix)
        juce::Random r ((rIn.nextInt64() * 6364136223846793005LL) ^ (rIn.nextInt64() >> 7) ^ 0x2545F4914F6CDD1DLL);
        for (int k = 0; k < 4; ++k) r.nextInt();
        intensity = juce::jlimit (0, 2, intensity);
        auto in = stereo (source);
        if (in.getNumSamples() < (int) (sr * 0.05)) { auto b = tCristal (in, sr, r); finish (b, sr); recipe.add ("cristal"); return b; }   // son très court
        enum Op { Nappe, Cristal, Grave, Swell, Pluck, Grains, Chorus, Reso, Bande, NumOps };
        static const char* names[] = { "nappe", "cristal", "grave", "reverse swell", "pluck", "grains doux", "chorus large", "résonateur", "bande douce" };
        std::vector<int> pool;
        for (int o = 0; o < NumOps; ++o) pool.push_back (o);
        const int count = intensity + 1;
        Buf b (in);
        for (int k = 0; k < count && ! pool.empty(); ++k)
        {
            const int idx = r.nextInt ((int) pool.size());
            const int op = pool[(size_t) idx];
            pool.erase (pool.begin() + idx);
            // deux étirements à la suite donneraient un son trop long et flou
            if (op == Nappe) for (int x : { (int) Grains, (int) Swell }) pool.erase (std::remove (pool.begin(), pool.end(), x), pool.end());
            if (op == Grains || op == Swell) pool.erase (std::remove (pool.begin(), pool.end(), (int) Nappe), pool.end());
            recipe.add (juce::String::fromUTF8 (names[op]));
            switch (op)
            {
                case Nappe:   b = tNappe (b, sr, r); break;
                case Cristal: b = tCristal (b, sr, r); break;
                case Grave:   b = tGrave (b, sr, r); break;
                case Swell:   b = tReverseSwell (b, sr, r); break;
                case Pluck:   b = tPluck (b, sr, r); break;
                case Grains:  b = tGrains (b, sr, r); break;
                case Chorus:  b = tChorus (b, sr, r); break;
                case Reso:    b = tResonateur (b, sr, r); break;
                default:      b = tBande (b, sr, r); break;
            }
            normalise (b, 0.9f);
        }
        if (intensity == 0)   // doux : on garde le son d'origine sous la transformation
        {
            normalise (in, 0.9f);
            for (int c = 0; c < 2; ++c) b.addFrom (c, 0, in, c, 0, juce::jmin (in.getNumSamples(), b.getNumSamples()), 0.6f);
        }
        finish (b, sr);
        return b;
    }
}
