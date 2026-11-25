#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>

// Génère des "prises" différentes du one-shot (round robin pré-calculé).
// La hauteur de base reste la même. Pas de bruit blanc : les textures viennent du son lui-même
// (grains, échos inversés, résonances, doublages), dans l'esprit des effets granulaires / cristallins.
namespace variation
{
    enum Style { Realistic = 0, Texture, Experimental };

    struct Settings
    {
        int style = Realistic;
        float amount = 0.5f;   // 0..1
        int count = 12;        // nombre de variantes (la 1re = l'original)
        juce::uint32 seed = 1;
    };

    using Buf = juce::AudioBuffer<float>;
    constexpr float twoPi = juce::MathConstants<float>::twoPi;

    //==========================================================================
    // Briques de traitement (hors temps réel)

    // Relecture avec micro-désaccord et point de départ décalé
    inline Buf resample (const Buf& src, int startOff, double ratio)
    {
        const int len = src.getNumSamples();
        const int outLen = juce::jmax (32, (int) ((len - startOff) / ratio) - 1);
        Buf out (2, outLen);
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* in = src.getReadPointer (juce::jmin (ch, src.getNumChannels() - 1));
            float* o = out.getWritePointer (ch);
            for (int i = 0; i < outLen; ++i)
            {
                const double p = startOff + i * ratio;
                const int p0 = (int) p;
                const float t = (float) (p - p0);
                const float a = p0 < len ? in[p0] : 0.0f, b = p0 + 1 < len ? in[p0 + 1] : 0.0f;
                o[i] = a + (b - a) * t;
            }
        }
        return out;
    }

    // Timbre plus clair / plus sombre
    inline void tilt (Buf& b, float amount, double sr)
    {
        const float c = 1.0f - std::exp (-twoPi * 1200.0f / (float) sr);
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            float lp = 0.0f; float* x = b.getWritePointer (ch);
            for (int i = 0; i < b.getNumSamples(); ++i) { lp += c * (x[i] - lp); x[i] += amount * (x[i] - 2.0f * lp); }
        }
    }

    // Saturation chaude ("thermal")
    inline void warm (Buf& b, float amount)
    {
        if (amount <= 0.0f) return;
        const float drive = 1.0f + amount * 8.0f, norm = 1.0f / std::tanh (drive);
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            float* x = b.getWritePointer (ch);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float s = std::tanh (drive * x[i] + 0.15f * amount) - std::tanh (0.15f * amount);
                x[i] = (1.0f - amount) * x[i] + amount * s * norm;
            }
        }
    }

    // Enveloppe : attaque adoucie + durée raccourcie
    inline void shape (Buf& b, float attackMs, float decayK, double sr)
    {
        const int n = b.getNumSamples();
        const int attN = juce::jmax (1, (int) (attackMs * 0.001f * (float) sr));
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            float* x = b.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
            {
                float g = std::exp (-decayK * (float) i / (float) n);
                if (i < attN) g *= (float) i / (float) attN;
                x[i] *= g;
            }
        }
    }

    // Nuage de grains tirés du son (ratio 2 = octave au-dessus, 0.5 = en dessous, 1.5 = quinte)
    inline void grainLayer (Buf& b, const Buf& src, double ratio, float grainMs, float level, float scatter,
                            bool reverse, juce::Random& rng, double sr)
    {
        if (level <= 0.0f) return;
        const int n = b.getNumSamples(), sl = src.getNumSamples();
        const int g = juce::jmax (64, (int) (grainMs * 0.001f * (float) sr));
        const int hop = g / 3;
        for (int start = 0; start < n; start += hop)
        {
            const int jitter = (int) (rng.nextFloat() * scatter * 0.08f * (float) sr);
            const double readStart = juce::jmax (0, start - jitter);
            const float pan = rng.nextFloat();
            for (int i = 0; i < g && start + i < n; ++i)
            {
                const double rp = reverse ? readStart + (g - i) * ratio : readStart + i * ratio;
                const int p0 = (int) rp;
                if (p0 + 1 >= sl || p0 < 0) continue;
                const float w = 0.5f - 0.5f * std::cos (twoPi * (float) i / (float) g);
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float* in = src.getReadPointer (juce::jmin (ch, src.getNumChannels() - 1));
                    const float s = in[p0] + (in[p0 + 1] - in[p0]) * (float) (rp - p0);
                    const float pg = ch == 0 ? 1.0f - 0.5f * pan : 0.5f + 0.5f * pan;
                    b.addSample (ch, start + i, s * w * level * pg * 0.5f);
                }
            }
        }
    }

    // Résonance métallique / cristalline (filtre en peigne)
    inline void comb (Buf& b, float delayMs, float feedback, float mix, double sr)
    {
        const int d = juce::jmax (2, (int) (delayMs * 0.001f * (float) sr));
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            std::vector<float> line ((size_t) d, 0.0f);
            int w = 0;
            float* x = b.getWritePointer (ch);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float y = line[(size_t) w];
                line[(size_t) w] = x[i] + y * feedback;
                w = (w + 1) % d;
                x[i] += mix * y;
            }
        }
    }

    // Doublage léger (comme deux prises superposées) + modulation de bande (flutter)
    inline void doubler (Buf& b, float baseMs, float depthMs, float rate, float mix, float flutter, double sr)
    {
        const int n = b.getNumSamples();
        Buf dry (b);
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* in = dry.getReadPointer (ch);
            float* x = b.getWritePointer (ch);
            const float ph = ch == 0 ? 0.0f : 1.7f;
            for (int i = 0; i < n; ++i)
            {
                const float t = (float) i / (float) sr;
                const float dMs = baseMs + depthMs * (0.5f + 0.5f * std::sin (twoPi * rate * t + ph));
                const float fMs = flutter * (0.5f + 0.5f * std::sin (twoPi * 6.3f * t + ph));
                const double rp = i - (dMs + fMs) * 0.001 * sr;
                const double fp = i - fMs * 0.001 * sr;
                auto read = [&] (double p) { const int p0 = (int) std::floor (p); if (p0 < 0 || p0 + 1 >= n) return 0.0f;
                                             return in[p0] + (in[p0 + 1] - in[p0]) * (float) (p - p0); };
                x[i] = (flutter > 0.0f ? read (fp) : in[i]) + mix * read (rp);
            }
        }
    }

    // Filtre "formant" résonant (passe-bande) qui balaie lentement
    inline void formant (Buf& b, float centreHz, float sweep, float mix, double sr)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            float low = 0.0f, band = 0.0f;
            float* x = b.getWritePointer (ch);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float t = (float) i / (float) b.getNumSamples();
                const float fc = centreHz * std::pow (2.0f, sweep * (t - 0.5f) * 2.0f);
                const float f = 2.0f * std::sin (juce::MathConstants<float>::pi * juce::jmin (fc, (float) sr * 0.2f) / (float) sr);
                const float q = 0.25f;
                low += f * band;
                const float high = x[i] - low - q * band;
                band += f * high;
                x[i] = (1.0f - mix) * x[i] + mix * band * 1.6f;
            }
        }
    }

    // Écho inversé qui "aspire" vers la fin du son (façon cristal)
    inline void reverseBloom (Buf& b, const Buf& src, float level, double sr)
    {
        const int n = b.getNumSamples(), sl = src.getNumSamples();
        const int len = juce::jmin (sl, (int) (0.35 * sr));
        const int at = juce::jmax (0, n - len - (int) (0.05 * sr));
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* in = src.getReadPointer (juce::jmin (ch, src.getNumChannels() - 1));
            for (int i = 0; i < len && at + i < n; ++i)
            {
                const float w = std::sin (juce::MathConstants<float>::pi * (float) i / (float) len);
                b.addSample (ch, at + i, in[len - 1 - i] * w * level);
            }
        }
    }

    // Bégaiement : répète un petit bout de l'attaque
    inline void stutter (Buf& b, float sliceMs, int repeats, double sr)
    {
        const int s = juce::jmax (64, (int) (sliceMs * 0.001f * (float) sr));
        Buf src (b);
        for (int r = 1; r <= repeats; ++r)
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < s && r * s + i < b.getNumSamples(); ++i)
                {
                    const float w = i < 32 ? (float) i / 32.0f : (i > s - 32 ? (float) (s - i) / 32.0f : 1.0f);
                    b.setSample (ch, r * s + i, src.getSample (ch, i) * w * std::pow (0.8f, (float) r));
                }
    }

    // Modulation en anneau légère (son de cloche)
    inline void ring (Buf& b, float freq, float mix, double sr)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            float* x = b.getWritePointer (ch);
            for (int i = 0; i < b.getNumSamples(); ++i)
                x[i] = (1.0f - mix) * x[i] + mix * x[i] * std::sin (twoPi * freq * (float) i / (float) sr);
        }
    }

    // Lo-fi (réduction de résolution)
    inline void crush (Buf& b, int factor, float bits, float mix)
    {
        const float q = std::pow (2.0f, bits);
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            float held = 0.0f; float* x = b.getWritePointer (ch);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                if (i % factor == 0) held = std::round (x[i] * q) / q;
                x[i] = (1.0f - mix) * x[i] + mix * held;
            }
        }
    }

    //==========================================================================
    inline std::vector<Buf> generate (const Buf& src, double sr, const Settings& s)
    {
        std::vector<Buf> out;
        out.push_back (src); // variante 0 = le son d'origine

        const int count = juce::jlimit (1, 16, s.count);
        if (count <= 1 || s.amount <= 0.0001f || src.getNumSamples() < 64)
            return out;

        const float a = juce::jlimit (0.0f, 1.0f, s.amount);
        const float srcPeak = juce::jmax (src.getMagnitude (0, src.getNumSamples()), 1.0e-6f);
        const bool tex = s.style == Texture, exp = s.style == Experimental;

        for (int v = 1; v < count; ++v)
        {
            juce::Random rng ((juce::int64) s.seed * 7919 + v * 104729 + s.style * 31337);
            auto bi = [&rng] { return rng.nextFloat() * 2.0f - 1.0f; };
            auto un = [&rng] { return rng.nextFloat(); };

            // --- base commune "réaliste" : micro-différences de jeu
            const float cents = bi() * (exp ? 14.0f : (tex ? 8.0f : 6.0f)) * a;
            const int startOff = (int) (un() * (exp ? 10.0f : 4.0f) * a * 0.001f * (float) sr);
            Buf b = resample (src, startOff, std::pow (2.0, cents / 1200.0));
            tilt (b, bi() * 0.35f * a, sr);
            warm (b, un() * 0.30f * a);

            // --- textures (on en pioche plusieurs au hasard pour chaque variante)
            if (tex || exp)
            {
                const float k = exp ? 2.2f : 1.5f;
                juce::Array<int> fx;
                for (int i = 0; i < 7; ++i) fx.add (i);
                for (int i = 0; i < 7; ++i) fx.swap (i, rng.nextInt (7));
                const int numFx = 2 + rng.nextInt (exp ? 4 : 3);

                for (int i = 0; i < numFx; ++i)
                {
                    switch (fx[i])
                    {
                        case 0: grainLayer (b, src, 2.0, 40.0f + un() * 60.0f, 0.35f * a * k, un(), false, rng, sr); break; // scintillement +1 octave
                        case 1: grainLayer (b, src, 0.5, 80.0f + un() * 120.0f, 0.25f * a * k, un(), false, rng, sr); break; // halo grave -1 octave
                        case 2: comb (b, 1.0f + un() * 4.0f, 0.55f + 0.3f * un(), 0.25f * a * k, sr); break;               // résonance cristal
                        case 3: doubler (b, 8.0f + un() * 10.0f, 2.0f + un() * 4.0f, 0.3f + un() * 0.8f, 0.45f * a, 0.25f * a * k, sr); break; // doublage + bande
                        case 4: formant (b, 500.0f + un() * 2500.0f, 0.5f + un(), 0.25f * a * k, sr); break;               // formant qui balaie
                        case 5: reverseBloom (b, src, 0.3f * a * k, sr); break;                                           // écho inversé
                        default: grainLayer (b, src, 1.4983, 60.0f + un() * 60.0f, 0.18f * a * k, un(), true, rng, sr); break; // quinte inversée
                    }
                }

                if (exp)
                {
                    if (un() < 0.5f) stutter (b, 25.0f + un() * 40.0f, 1 + rng.nextInt (3), sr);
                    if (un() < 0.4f) ring (b, 200.0f + un() * 900.0f, 0.15f * a, sr);
                    if (un() < 0.4f) crush (b, 2 + rng.nextInt (4), 6.0f + un() * 4.0f, 0.35f * a);
                }
            }

            shape (b, 0.3f + un() * (exp ? 14.0f : 4.0f) * a, un() * (exp ? 2.0f : 0.6f) * a, sr);

            // même niveau crête que l'original, puis petite variation de volume
            const float peak = juce::jmax (b.getMagnitude (0, b.getNumSamples()), 1.0e-6f);
            b.applyGain (srcPeak / peak * juce::Decibels::decibelsToGain (bi() * 2.5f * a));
            out.push_back (std::move (b));
        }
        return out;
    }
}
