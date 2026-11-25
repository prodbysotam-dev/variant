#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_data_structures/juce_data_structures.h>
#include <map>
#include <algorithm>
#include <atomic>
#include <functional>
#include <cmath>

// Bibliothèque de one-shots : sons de base créés par synthèse (aucun souci de droits),
// rangés par catégorie dans Documents\Variant Bibliothèque, + dossiers perso.
namespace library
{
    // Test « nouvel utilisateur » (outil de dev, absent de la version publique) : VARIANT_TEST_PROFILE=<dossier>
    // -> Documents et réglages de Variant pris dans ce dossier, comme sur un PC qui n a jamais vu Variant.
    inline juce::File testProfile()
    {
       #if VARIANT_PUBLIC
        return {};
       #else
        const auto p = juce::SystemStats::getEnvironmentVariable ("VARIANT_TEST_PROFILE", {});
        return p.isEmpty() ? juce::File() : juce::File (p);
       #endif
    }
    inline juce::File documentsDir()
    {
        const auto t = testProfile();
        return t != juce::File() ? t.getChildFile ("Documents") : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    }
    inline juce::File appDataDir()
    {
        const auto t = testProfile();
        return t != juce::File() ? t.getChildFile ("AppData") : juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
    }

    constexpr double sr = 44100.0;
    constexpr double f0 = 261.6255653;   // C5 dans FL Studio (note MIDI 60)
    constexpr double twoPi = juce::MathConstants<double>::twoPi;
    using Buf = juce::AudioBuffer<float>;

    //==========================================================================
    // Petites briques de synthèse
    inline double polyBlep (double t, double dt)
    {
        if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
        return 0.0;
    }

    struct Saw
    {
        double ph = 0.0;
        double next (double f) { const double dt = f / sr; double v = 2.0 * ph - 1.0 - polyBlep (ph, dt); ph += dt; if (ph >= 1.0) ph -= 1.0; return v; }
    };

    struct Square
    {
        double ph = 0.0;
        double next (double f, double pw = 0.5)
        {
            const double dt = f / sr;
            double v = (ph < pw ? 1.0 : -1.0) + polyBlep (ph, dt) - polyBlep (std::fmod (ph + 1.0 - pw, 1.0), dt);
            ph += dt; if (ph >= 1.0) ph -= 1.0; return v;
        }
    };

    struct SVF   // filtre à variable d'état (TPT)
    {
        double ic1 = 0, ic2 = 0;
        double lp (double x, double fc, double q = 0.7)
        {
            const double g = std::tan (juce::MathConstants<double>::pi * juce::jlimit (20.0, sr * 0.45, fc) / sr);
            const double k = 1.0 / q, a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
            const double v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2; return v2;
        }
        double bp (double x, double fc, double q)
        {
            const double g = std::tan (juce::MathConstants<double>::pi * juce::jlimit (20.0, sr * 0.45, fc) / sr);
            const double k = 1.0 / q, a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
            const double v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2; return v1;
        }
    };

    inline double adsr (double t, double a, double d, double s, double dur, double r)
    {
        double e;
        if (t < a) e = t / a;
        else e = s + (1.0 - s) * std::exp (-(t - a) / juce::jmax (1.0e-4, d));
        if (t > dur - r) e *= juce::jmax (0.0, (dur - t) / r);
        return e;
    }

    // Corde pincée (Karplus-Strong)
    inline void ks (Buf& b, double f, double bright, double decay, double seed, double pick = 0.5)
    {
        const int n = b.getNumSamples();
        const int N = juce::jmax (2, (int) std::round (sr / f));
        std::vector<double> line ((size_t) N);
        juce::Random r ((juce::int64) (seed * 1000));
        SVF ex;
        for (int i = 0; i < N; ++i) line[(size_t) i] = ex.lp (r.nextDouble() * 2 - 1, 1500 + bright * 9000);
        // position de pincement (filtre en peigne)
        const int pk = juce::jmax (1, (int) (N * pick * 0.5));
        for (int i = N - 1; i >= pk; --i) line[(size_t) i] -= 0.5 * line[(size_t) (i - pk)];
        double last = 0;
        const double fb = std::pow (0.001, 1.0 / (decay * f));
        int idx = 0;
        for (int i = 0; i < n; ++i)
        {
            const double x = line[(size_t) idx];
            const double y = fb * ((0.5 + 0.45 * bright) * x + (0.5 - 0.45 * bright) * last);
            last = x;
            line[(size_t) idx] = y;
            idx = (idx + 1) % N;
            b.addSample (0, i, (float) x);
            b.addSample (1, i, (float) x);
        }
    }

    // Résonateurs modaux (percussions accordées)
    inline void modal (Buf& b, std::initializer_list<std::array<double, 3>> partials, double strike = 0.003)
    {
        const int n = b.getNumSamples();
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr;
            double v = 0;
            for (auto& p : partials) v += p[1] * std::sin (twoPi * f0 * p[0] * t) * std::exp (-t / p[2]);
            v *= juce::jmin (1.0, t / strike);
            b.addSample (0, i, (float) v); b.addSample (1, i, (float) v);
        }
    }

    inline void fm (Buf& b, double ratio, double idx0, double idx1, double idxDecay, double ampDecay, double attack = 0.002,
                    double carrierRatio = 1.0)
    {
        const int n = b.getNumSamples();
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr;
            const double I = idx1 + (idx0 - idx1) * std::exp (-t / idxDecay);
            const double v = std::sin (twoPi * f0 * carrierRatio * t + I * std::sin (twoPi * f0 * ratio * t)) * std::exp (-t / ampDecay)
                           * juce::jmin (1.0, t / attack);
            b.addSample (0, i, (float) v); b.addSample (1, i, (float) v);
        }
    }

    // Plusieurs scies désaccordées (pads, supersaw) avec filtre et enveloppe
    inline void saws (Buf& b, int voices, double detuneCents, double cutoff, double cutEnvAmt, double cutEnvDecay,
                      double a, double d, double s, double r, double vib = 0.0)
    {
        const int n = b.getNumSamples();
        const double dur = n / sr;
        std::vector<Saw> osc ((size_t) voices);
        juce::Random rr (7);
        for (auto& o : osc) o.ph = rr.nextDouble();
        SVF fl, fr;
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr;
            const double vibr = 1.0 + vib * 0.004 * std::sin (twoPi * 5.2 * t) * juce::jmin (1.0, t / 0.6);
            double l = 0, rgt = 0;
            for (int v = 0; v < voices; ++v)
            {
                const double spread = voices > 1 ? (double) v / (voices - 1) * 2.0 - 1.0 : 0.0;
                const double x = osc[(size_t) v].next (f0 * vibr * std::pow (2.0, spread * detuneCents / 1200.0));
                const double pan = 0.5 + 0.45 * spread;
                l += x * (1.0 - pan); rgt += x * pan;
            }
            const double fc = cutoff * (1.0 + cutEnvAmt * std::exp (-t / cutEnvDecay));
            const double e = adsr (t, a, d, s, dur, r);
            b.addSample (0, i, (float) (fl.lp (l, fc, 0.8) * e / voices * 2.0));
            b.addSample (1, i, (float) (fr.lp (rgt, fc, 0.8) * e / voices * 2.0));
        }
    }

    inline void finish (Buf& b)
    {
        const int n = b.getNumSamples();
        const int fade = (int) (0.05 * sr);
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < fade && i < n; ++i)
                b.setSample (ch, n - 1 - i, b.getSample (ch, n - 1 - i) * (float) i / (float) fade);
        const float pk = b.getMagnitude (0, n);
        if (pk > 1.0e-6f) b.applyGain (0.89f / pk);
    }

    //==========================================================================
    struct Item { const char* category; const char* name; double seconds; std::function<void (Buf&)> render; };

    inline std::vector<Item> moreItems();
    inline const std::vector<Item>& items()
    {
        static const std::vector<Item> list = [] { std::vector<Item> v {
            // ---------------- Claviers
            { "Claviers", "Piano électrique", 3.0, [] (Buf& b) {
                fm (b, 1.0, 2.4, 0.3, 0.45, 1.5);
                Buf tine (2, b.getNumSamples()); tine.clear();
                fm (tine, 14.0, 1.2, 0.0, 0.03, 0.08, 0.001);
                b.addFrom (0, 0, tine, 0, 0, b.getNumSamples(), 0.18f); b.addFrom (1, 0, tine, 1, 0, b.getNumSamples(), 0.18f);
                for (int i = 0; i < b.getNumSamples(); ++i) { const float tr = (float) (0.06 * std::sin (twoPi * 4.8 * i / sr));
                    b.setSample (0, i, b.getSample (0, i) * (1 + tr)); b.setSample (1, i, b.getSample (1, i) * (1 - tr)); } } },
            { "Claviers", "Piano électrique doux", 3.0, [] (Buf& b) { fm (b, 1.0, 1.1, 0.15, 0.6, 1.8, 0.004); } },
            { "Claviers", "Orgue doux", 2.5, [] (Buf& b) {
                const int n = b.getNumSamples(); const double dur = n / sr;
                const double h[] = { 1, 2, 3, 4, 6, 8 }, a[] = { 1.0, 0.55, 0.35, 0.25, 0.12, 0.08 };
                for (int i = 0; i < n; ++i) { const double t = i / sr; double l = 0, r = 0;
                    for (int k = 0; k < 6; ++k) { l += a[k] * std::sin (twoPi * f0 * h[k] * t); r += a[k] * std::sin (twoPi * f0 * h[k] * 1.0015 * t + k); }
                    const double e = adsr (t, 0.012, 0.3, 0.85, dur, 0.15);
                    b.setSample (0, i, (float) (l * e)); b.setSample (1, i, (float) (r * e)); } } },
            { "Claviers", "Clavinet", 1.8, [] (Buf& b) { ks (b, f0, 0.95, 1.2, 3, 0.15); } },

            // ---------------- Plucks
            { "Plucks", "Pluck doux", 2.5, [] (Buf& b) { ks (b, f0, 0.35, 2.2, 1); } },
            { "Plucks", "Pluck brillant", 1.5, [] (Buf& b) { saws (b, 3, 14, 350, 18, 0.09, 0.002, 0.35, 0.0, 0.2); } },
            { "Plucks", "Harpe", 3.0, [] (Buf& b) { ks (b, f0, 0.55, 3.5, 2, 0.35); ks (b, f0 * 1.0012, 0.5, 3.2, 5, 0.3); } },
            { "Plucks", "Guitare nylon", 2.5, [] (Buf& b) {
                ks (b, f0, 0.3, 2.6, 4, 0.2);
                SVF b1, b2; for (int i = 0; i < b.getNumSamples(); ++i) { const double x = b.getSample (0, i);
                    const double y = x + 0.6 * b1.bp (x, 110, 4) + 0.4 * b2.bp (x, 220, 4);
                    b.setSample (0, i, (float) y); b.setSample (1, i, (float) y); } } },

            // ---------------- Percussions accordées
            { "Mallets", "Marimba", 1.6, [] (Buf& b) { modal (b, { { 1.0, 1.0, 0.55 }, { 3.99, 0.35, 0.14 }, { 10.65, 0.12, 0.04 } }, 0.001); } },
            { "Mallets", "Vibraphone", 3.5, [] (Buf& b) {
                modal (b, { { 1.0, 1.0, 2.8 }, { 4.0, 0.22, 0.9 }, { 10.0, 0.07, 0.3 } }, 0.002);
                for (int i = 0; i < b.getNumSamples(); ++i) { const float tr = (float) (1.0 - 0.28 * (0.5 + 0.5 * std::sin (twoPi * 5.5 * i / sr)));
                    b.setSample (0, i, b.getSample (0, i) * tr); b.setSample (1, i, b.getSample (1, i) * tr); } } },
            { "Mallets", "Kalimba", 2.2, [] (Buf& b) { modal (b, { { 1.0, 1.0, 1.4 }, { 5.4, 0.22, 0.22 }, { 14.5, 0.08, 0.06 } }, 0.0015); } },
            { "Mallets", "Glockenspiel", 2.8, [] (Buf& b) { modal (b, { { 1.0, 1.0, 2.4 }, { 2.76, 0.45, 0.9 }, { 5.4, 0.25, 0.45 }, { 8.93, 0.12, 0.25 } }, 0.0008); } },

            // ---------------- Cloches
            { "Cloches", "Cloche FM", 4.0, [] (Buf& b) { fm (b, 1.4, 4.0, 0.6, 0.9, 2.6, 0.002); } },
            { "Cloches", "Cloche cristal", 4.5, [] (Buf& b) {
                modal (b, { { 0.5, 0.35, 3.5 }, { 1.0, 1.0, 3.0 }, { 1.19, 0.45, 2.0 }, { 1.56, 0.35, 1.5 }, { 2.0, 0.4, 1.2 },
                            { 2.51, 0.22, 0.8 }, { 3.01, 0.15, 0.6 } }, 0.002); } },
            { "Cloches", "Célesta", 2.5, [] (Buf& b) { modal (b, { { 1.0, 1.0, 1.6 }, { 2.0, 0.3, 0.7 }, { 3.0, 0.15, 0.4 }, { 4.1, 0.08, 0.2 } }, 0.001); } },

            // ---------------- Nappes
            { "Nappes", "Pad doux", 4.0, [] (Buf& b) { saws (b, 7, 18, 1400, 0.4, 1.5, 0.45, 1.0, 0.8, 0.8, 1.0); } },
            { "Nappes", "Cordes", 3.5, [] (Buf& b) { saws (b, 5, 12, 2600, 0.2, 1.0, 0.25, 0.8, 0.85, 0.6, 1.5); } },
            { "Nappes", "Pad vitreux", 4.0, [] (Buf& b) {
                const int n = b.getNumSamples(); const double dur = n / sr;
                for (int i = 0; i < n; ++i) { const double t = i / sr; double l = 0, r = 0;
                    for (int k = 1; k <= 9; k += 2) { const double a = 1.0 / k; l += a * std::sin (twoPi * f0 * k * t); r += a * std::sin (twoPi * f0 * k * 1.002 * t + 0.7 * k); }
                    const double e = adsr (t, 0.6, 1.0, 0.9, dur, 1.0) * (1 + 0.1 * std::sin (twoPi * 0.7 * t));
                    b.setSample (0, i, (float) (l * e)); b.setSample (1, i, (float) (r * e)); } } },
            { "Nappes", "Chœur aah", 3.5, [] (Buf& b) {
                const int n = b.getNumSamples(); const double dur = n / sr;
                Saw s1, s2; SVF f[6];
                for (int i = 0; i < n; ++i) { const double t = i / sr;
                    const double vib = 1.0 + 0.005 * std::sin (twoPi * 5.0 * t) * juce::jmin (1.0, t / 0.8);
                    const double x1 = s1.next (f0 * vib), x2 = s2.next (f0 * vib * 1.004);
                    const double l = f[0].bp (x1, 700, 6) + 0.6 * f[1].bp (x1, 1220, 7) + 0.25 * f[2].bp (x1, 2600, 8);
                    const double r = f[3].bp (x2, 700, 6) + 0.6 * f[4].bp (x2, 1220, 7) + 0.25 * f[5].bp (x2, 2600, 8);
                    const double e = adsr (t, 0.3, 0.8, 0.85, dur, 0.6);
                    b.setSample (0, i, (float) (l * e)); b.setSample (1, i, (float) (r * e)); } } },

            // ---------------- Basses
            { "Basses", "Sub", 2.5, [] (Buf& b) {
                const int n = b.getNumSamples(); const double dur = n / sr;
                for (int i = 0; i < n; ++i) { const double t = i / sr;
                    const double v = (std::sin (twoPi * f0 * t) + 0.12 * std::sin (twoPi * 2 * f0 * t)) * adsr (t, 0.004, 1.5, 0.6, dur, 0.3);
                    b.setSample (0, i, (float) v); b.setSample (1, i, (float) v); } } },
            { "Basses", "808", 2.8, [] (Buf& b) {
                const int n = b.getNumSamples(); double ph = 0;
                for (int i = 0; i < n; ++i) { const double t = i / sr;
                    const double f = f0 * std::pow (2.0, (12.0 * std::exp (-t / 0.035)) / 12.0);
                    ph += twoPi * f / sr;
                    const double v = std::tanh (2.2 * std::sin (ph) * std::exp (-t / 1.6)) * juce::jmin (1.0, t / 0.001);
                    b.setSample (0, i, (float) v); b.setSample (1, i, (float) v); } } },
            { "Basses", "Basse pluck", 1.5, [] (Buf& b) {
                const int n = b.getNumSamples(); const double dur = n / sr; Saw s; Square q; SVF fl;
                for (int i = 0; i < n; ++i) { const double t = i / sr;
                    const double x = 0.6 * s.next (f0) + 0.4 * q.next (f0 * 0.5);
                    const double v = fl.lp (x, 180 + 2400 * std::exp (-t / 0.08), 1.1) * adsr (t, 0.002, 0.5, 0.3, dur, 0.2);
                    b.setSample (0, i, (float) v); b.setSample (1, i, (float) v); } } },
            { "Basses", "Basse FM", 1.8, [] (Buf& b) { fm (b, 1.0, 3.0, 0.4, 0.12, 0.9, 0.002); } },

            // ---------------- Leads & synthés
            { "Synthés", "Lead carré", 2.0, [] (Buf& b) {
                const int n = b.getNumSamples(); const double dur = n / sr; Square q; SVF fl;
                for (int i = 0; i < n; ++i) { const double t = i / sr;
                    const double vib = 1.0 + 0.006 * std::sin (twoPi * 5.5 * t) * juce::jlimit (0.0, 1.0, (t - 0.25) / 0.4);
                    const double v = fl.lp (q.next (f0 * vib, 0.42 + 0.08 * std::sin (twoPi * 0.8 * t)), 3800, 0.8) * adsr (t, 0.01, 0.4, 0.75, dur, 0.25);
                    b.setSample (0, i, (float) v); b.setSample (1, i, (float) v); } } },
            { "Synthés", "Supersaw", 1.6, [] (Buf& b) { saws (b, 7, 28, 5000, 0.5, 0.3, 0.003, 0.6, 0.35, 0.3); } },
            { "Synthés", "Cuivres", 2.0, [] (Buf& b) { saws (b, 3, 8, 600, 5.0, 0.12, 0.03, 0.5, 0.7, 0.25, 0.6); } },
            { "Synthés", "Flûte", 2.5, [] (Buf& b) {
                const int n = b.getNumSamples(); const double dur = n / sr; juce::Random r (11); SVF br;
                for (int i = 0; i < n; ++i) { const double t = i / sr;
                    const double vib = 1.0 + 0.005 * std::sin (twoPi * 5.0 * t) * juce::jlimit (0.0, 1.0, (t - 0.3) / 0.5);
                    const double tone = std::sin (twoPi * f0 * vib * t) + 0.18 * std::sin (twoPi * 2 * f0 * vib * t) + 0.06 * std::sin (twoPi * 3 * f0 * vib * t);
                    const double breath = br.bp (r.nextDouble() * 2 - 1, f0 * 2, 2) * 0.12;
                    const double v = (tone + breath) * adsr (t, 0.07, 0.6, 0.85, dur, 0.25);
                    b.setSample (0, i, (float) v); b.setSample (1, i, (float) v); } } },
            { "Synthés", "Chiptune", 1.2, [] (Buf& b) {
                const int n = b.getNumSamples(); const double dur = n / sr; Square q;
                for (int i = 0; i < n; ++i) { const double t = i / sr;
                    const double v = q.next (f0, 0.25) * adsr (t, 0.001, 0.4, 0.5, dur, 0.1) * 0.6;
                    b.setSample (0, i, (float) v); b.setSample (1, i, (float) v); } } },
        }; for (auto& x : moreItems()) v.push_back (x); return v; }();
        return list;
    }

    //==========================================================================
    // Instruments "réels" modélisés : vents, cuivres, cordes frottées
    struct WindCfg
    {
        int osc = 0;                 // 0 scie, 1 carré, 2 pulse étroite, 3 scie + carré
        double scoop = -40, scoopTime = 0.05;          // attaque qui monte vers la note (cents)
        double vibRate = 5.2, vibDepth = 18, vibDelay = 0.35;
        double breath = 0.08, breathFc = 2500;
        double cutBase = 900, cutAmt = 3500;           // brillance qui suit le souffle (cuivres)
        double growl = 0.0;
        std::vector<std::array<double, 3>> formants;   // fréquence, Q, gain
        double formantMix = 0.6;
        double a = 0.04, d = 0.4, s = 0.85, r = 0.18;
    };

    inline void wind (Buf& b, const WindCfg& c)
    {
        const int n = b.getNumSamples();
        const double dur = n / sr;
        Saw saw; Square sq; SVF lp, br, gr;
        std::vector<SVF> fs (c.formants.size());
        juce::Random rnd (19);
        double drift = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr;
            const double env = adsr (t, c.a, c.d, c.s, dur, c.r);
            drift += (rnd.nextDouble() * 2.0 - 1.0) * 0.02 - drift * 0.0005;   // petite dérive naturelle
            const double vib = c.vibDepth * std::sin (twoPi * c.vibRate * t) * juce::jlimit (0.0, 1.0, (t - c.vibDelay) / 0.45);
            const double cents = c.scoop * std::exp (-t / c.scoopTime) + vib + juce::jlimit (-4.0, 4.0, drift);
            const double f = f0 * std::pow (2.0, cents / 1200.0);
            double x;
            switch (c.osc)
            {
                case 1:  x = sq.next (f, 0.5); break;
                case 2:  x = sq.next (f, 0.18); break;
                case 3:  x = 0.6 * saw.next (f) + 0.4 * sq.next (f, 0.5); break;
                default: x = saw.next (f); break;
            }
            double y = lp.lp (x, c.cutBase + c.cutAmt * env * env, 0.7);
            if (! c.formants.empty())
            {
                double fo = 0.0;
                for (size_t k = 0; k < c.formants.size(); ++k) fo += c.formants[k][2] * fs[k].bp (y, c.formants[k][0], c.formants[k][1]);
                y = (1.0 - c.formantMix) * y + c.formantMix * fo * 2.0;
            }
            const double noise = rnd.nextDouble() * 2.0 - 1.0;
            const double breath = br.bp (noise, c.breathFc, 1.2) * c.breath * (0.5 * env + 1.5 * std::exp (-t / 0.06));
            const double growl = 1.0 + c.growl * gr.lp (noise, 35.0) * 6.0;
            const double v = (y * growl + breath) * env;
            b.setSample (0, i, (float) v);
            b.setSample (1, i, (float) v);
        }
    }

    // petite pièce : réflexions précoces décalées entre gauche et droite (son moins "sec")
    inline void room (Buf& b, double amount = 0.25)
    {
        const int n = b.getNumSamples();
        Buf src; src.makeCopyOf (b);
        const double tapsL[] = { 0.0113, 0.0197, 0.0291 }, tapsR[] = { 0.0137, 0.0231, 0.0337 };
        const double g[] = { 0.55, 0.35, 0.22 };
        for (int k = 0; k < 3; ++k)
        {
            const int dl = (int) (tapsL[k] * sr), dr = (int) (tapsR[k] * sr);
            for (int i = 0; i < n; ++i)
            {
                if (i >= dl) b.addSample (0, i, (float) (amount * g[k]) * src.getSample (1, i - dl));
                if (i >= dr) b.addSample (1, i, (float) (amount * g[k]) * src.getSample (0, i - dr));
            }
        }
    }

    // piano : partiels inharmoniques, 2 cordes un peu désaccordées, bruit de marteau
    inline void piano (Buf& b, double bright, double felt, double decay)
    {
        const int n = b.getNumSamples();
        const double B = 0.00035;
        juce::Random rnd (3);
        SVF ham;
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr;
            double l = 0.0, r = 0.0;
            for (int k = 1; k <= 14; ++k)
            {
                const double fk = f0 * k * std::sqrt (1.0 + B * k * k);
                const double amp = std::pow ((double) k, -(1.6 - bright)) * (k > 1 ? 1.0 : 1.2);
                const double dk = decay / (1.0 + 0.35 * (k - 1)) * (1.0 - 0.5 * felt);
                const double e = std::exp (-t / dk) * 0.75 + 0.25 * std::exp (-t / (dk * 0.25));
                l += amp * e * std::sin (twoPi * fk * t + k);
                r += amp * e * std::sin (twoPi * fk * 1.0006 * t + 1.7 * k);
            }
            const double hammer = ham.lp (rnd.nextDouble() * 2.0 - 1.0, 1800 + bright * 4000) * std::exp (-t / 0.012) * (0.6 - 0.4 * felt);
            const double att = juce::jmin (1.0, t / (0.0015 + felt * 0.006));
            b.setSample (0, i, (float) ((l * 0.5 + hammer) * att));
            b.setSample (1, i, (float) ((r * 0.5 + hammer) * att));
        }
    }

    // piano électrique type Rhodes : corps FM + "bark" de la lame + trémolo stéréo
    inline void epiano (Buf& b, double bark, double bell, double trem, double decay)
    {
        const int n = b.getNumSamples();
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr;
            const double I = 0.4 + bark * 2.6 * std::exp (-t / 0.18);
            const double body = std::sin (twoPi * f0 * t + I * std::sin (twoPi * f0 * t)) * std::exp (-t / decay);
            const double tine = bell * std::sin (twoPi * f0 * 7.0 * t + 1.5 * std::sin (twoPi * f0 * 1.0 * t)) * std::exp (-t / 0.09);
            const double v = (body + tine) * juce::jmin (1.0, t / 0.0015);
            const double tr = trem * std::sin (twoPi * 4.6 * t);
            b.setSample (0, i, (float) (v * (1.0 + tr)));
            b.setSample (1, i, (float) (v * (1.0 - tr)));
        }
    }

    // TB-303 : scie/carré + filtre résonant qui claque
    inline void acid (Buf& b, bool square, double res, double envAmt, double decay)
    {
        const int n = b.getNumSamples(); const double dur = n / sr;
        Saw s; Square q; SVF f1, f2;
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr;
            const double x = square ? q.next (f0, 0.5) : s.next (f0);
            const double fc = 220 + envAmt * std::exp (-t / decay);
            const double y = f2.lp (f1.lp (x, fc, res), fc * 1.3, 0.6);
            const double v = std::tanh (y * 1.8) * adsr (t, 0.002, 0.25, 0.55, dur, 0.08);
            b.setSample (0, i, (float) v); b.setSample (1, i, (float) v);
        }
    }

    // oscillateur esclave synchronisé : le son "lead sync" qui balaie
    inline void syncLead (Buf& b, double sweepFrom, double sweepTo, double sweepTime)
    {
        const int n = b.getNumSamples(); const double dur = n / sr;
        double master = 0, slave = 0;
        SVF fl;
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr;
            const double ratio = sweepTo + (sweepFrom - sweepTo) * std::exp (-t / sweepTime);
            master += f0 / sr;
            slave += f0 * ratio / sr;
            if (master >= 1.0) { master -= 1.0; slave = 0.0; }
            slave -= std::floor (slave);
            const double v = fl.lp (2.0 * slave - 1.0, 6000, 0.7) * adsr (t, 0.005, 0.5, 0.7, dur, 0.2) * 0.8;
            b.setSample (0, i, (float) v); b.setSample (1, i, (float) v);
        }
    }

    inline std::vector<Item> moreItems()
    {
        auto sax = [] (double formShift, double breath, double growl)
        {
            WindCfg c; c.osc = 3; c.scoop = -60; c.scoopTime = 0.06; c.vibDepth = 22; c.vibDelay = 0.4; c.breath = breath; c.breathFc = 2200;
            c.cutBase = 700; c.cutAmt = 3200; c.growl = growl;
            c.formants = { { 520 * formShift, 4, 1.0 }, { 1480 * formShift, 5, 0.7 }, { 2550 * formShift, 6, 0.35 } };
            c.a = 0.05; c.d = 0.5; c.s = 0.8; c.r = 0.15;
            return c;
        };
        auto brass = [] (double formShift, double bright, double mute)
        {
            WindCfg c; c.osc = 0; c.scoop = -90; c.scoopTime = 0.035; c.vibDepth = 12; c.vibDelay = 0.5; c.breath = 0.03; c.breathFc = 3000;
            c.cutBase = 500 + 300 * bright; c.cutAmt = 3000 + 4000 * bright;
            c.formants = { { 1150 * formShift, 3, 1.0 }, { 2300 * formShift, 4, 0.55 } };
            if (mute > 0) c.formants.push_back ({ 1750, 8, 1.4 * mute });
            c.formantMix = 0.45 + 0.4 * mute; c.a = 0.03; c.d = 0.35; c.s = 0.85; c.r = 0.12;
            return c;
        };
        auto bowed = [] (double formShift, double vib)
        {
            WindCfg c; c.osc = 0; c.scoop = -15; c.scoopTime = 0.08; c.vibDepth = vib; c.vibRate = 5.6; c.vibDelay = 0.25;
            c.breath = 0.05; c.breathFc = 3500; c.cutBase = 2500; c.cutAmt = 2500;
            c.formants = { { 280 * formShift, 3, 0.9 }, { 1000 * formShift, 3, 0.8 }, { 2900 * formShift, 4, 0.5 } };
            c.formantMix = 0.5; c.a = 0.12; c.d = 0.6; c.s = 0.9; c.r = 0.3;
            return c;
        };

        std::vector<Item> v {
            // ---------------- Rhodes & keys
            { "Rhodes & Keys", "Rhodes classique", 3.5, [] (Buf& b) { epiano (b, 0.5, 0.25, 0.12, 1.8); room (b, 0.2); } },
            { "Rhodes & Keys", "Rhodes sombre", 3.5, [] (Buf& b) { epiano (b, 0.2, 0.08, 0.08, 2.2); room (b, 0.2); } },
            { "Rhodes & Keys", "Rhodes qui grogne", 3.0, [] (Buf& b) { epiano (b, 1.0, 0.35, 0.18, 1.5); room (b, 0.15); } },
            { "Rhodes & Keys", "Wurlitzer", 2.5, [] (Buf& b) {
                const int n = b.getNumSamples(); Square q; SVF fl;
                for (int i = 0; i < n; ++i) { const double t = i / sr;
                    const double x = 0.55 * q.next (f0, 0.5) + 0.45 * std::sin (twoPi * f0 * t);
                    const double v = fl.lp (x, 900 + 3500 * std::exp (-t / 0.12), 1.0) * std::exp (-t / 1.4) * juce::jmin (1.0, t / 0.002);
                    b.setSample (0, i, (float) v); b.setSample (1, i, (float) v); }
                room (b, 0.2); } },
            { "Rhodes & Keys", "Piano à queue", 4.0, [] (Buf& b) { piano (b, 0.65, 0.0, 2.6); room (b, 0.3); } },
            { "Rhodes & Keys", "Piano feutre", 3.5, [] (Buf& b) { piano (b, 0.2, 0.9, 2.2); room (b, 0.25); } },
            { "Rhodes & Keys", "Keys DX", 3.0, [] (Buf& b) { fm (b, 1.0, 1.6, 0.2, 0.25, 1.4, 0.002); Buf h (2, b.getNumSamples()); h.clear();
                fm (h, 15.0, 2.0, 0.0, 0.02, 0.06, 0.0008); b.addFrom (0, 0, h, 0, 0, b.getNumSamples(), 0.25f); b.addFrom (1, 0, h, 1, 0, b.getNumSamples(), 0.25f); room (b, 0.2); } },
            { "Rhodes & Keys", "Keys lo-fi", 3.0, [] (Buf& b) {
                epiano (b, 0.3, 0.1, 0.0, 1.6);
                juce::Random r (4); SVF hp;
                for (int i = 0; i < b.getNumSamples(); ++i) { const double t = i / sr;
                    const float wow = (float) (1.0 + 0.004 * std::sin (twoPi * 0.6 * t));
                    const float hiss = (float) (hp.bp (r.nextDouble() * 2 - 1, 5000, 0.7) * 0.015);
                    b.setSample (0, i, b.getSample (0, i) * wow + hiss); b.setSample (1, i, b.getSample (1, i) * wow + hiss); } } },

            // ---------------- Vents
            { "Vents", "Sax ténor", 2.5, [sax] (Buf& b) { wind (b, sax (1.0, 0.08, 0.02)); room (b, 0.3); } },
            { "Vents", "Sax alto", 2.5, [sax] (Buf& b) { wind (b, sax (1.25, 0.07, 0.01)); room (b, 0.3); } },
            { "Vents", "Sax soufflé", 2.5, [sax] (Buf& b) { wind (b, sax (1.1, 0.22, 0.0)); room (b, 0.3); } },
            { "Vents", "Sax qui grogne", 2.0, [sax] (Buf& b) { wind (b, sax (1.0, 0.1, 0.12)); room (b, 0.25); } },
            { "Vents", "Clarinette", 2.5, [] (Buf& b) { WindCfg c; c.osc = 1; c.scoop = -20; c.vibDepth = 8; c.breath = 0.05; c.cutBase = 1500; c.cutAmt = 2000;
                c.formants = { { 1500, 3, 0.8 }, { 3000, 4, 0.3 } }; c.formantMix = 0.35; wind (b, c); room (b, 0.3); } },
            { "Vents", "Flûte traversière", 2.5, [] (Buf& b) { WindCfg c; c.osc = 2; c.scoop = -10; c.vibDepth = 14; c.vibRate = 5.0; c.breath = 0.18; c.breathFc = 3500;
                c.cutBase = 900; c.cutAmt = 1200; c.formantMix = 0.0; c.a = 0.07; wind (b, c); room (b, 0.35); } },
            { "Vents", "Flûte de pan", 2.0, [] (Buf& b) { WindCfg c; c.osc = 2; c.scoop = 0; c.vibDepth = 6; c.breath = 0.3; c.breathFc = 2600;
                c.cutBase = 700; c.cutAmt = 400; c.formantMix = 0.0; c.a = 0.03; c.d = 0.6; c.s = 0.6; wind (b, c); room (b, 0.4); } },

            // ---------------- Cuivres
            { "Cuivres", "Trompette", 2.2, [brass] (Buf& b) { wind (b, brass (1.0, 0.7, 0.0)); room (b, 0.3); } },
            { "Cuivres", "Trompette douce", 2.2, [brass] (Buf& b) { wind (b, brass (0.9, 0.2, 0.0)); room (b, 0.3); } },
            { "Cuivres", "Trompette sourdine", 2.0, [brass] (Buf& b) { wind (b, brass (1.1, 0.5, 1.0)); room (b, 0.3); } },
            { "Cuivres", "Trombone", 2.5, [brass] (Buf& b) { wind (b, brass (0.6, 0.4, 0.0)); room (b, 0.3); } },
            { "Cuivres", "Section cuivres", 2.2, [brass] (Buf& b) {
                Buf t (2, b.getNumSamples());
                for (double sh : { 1.0, 0.82, 1.18 }) { t.clear(); auto c = brass (sh, 0.6, 0.0); c.vibDepth = 10 + 6 * sh; wind (t, c);
                    b.addFrom (0, 0, t, 0, 0, t.getNumSamples(), (float) (0.4 + 0.3 * (sh - 0.8))); b.addFrom (1, 0, t, 1, 0, t.getNumSamples(), (float) (0.7 - 0.3 * (sh - 0.8))); }
                room (b, 0.35); } },

            // ---------------- Cordes
            { "Cordes", "Violon", 2.5, [bowed] (Buf& b) { wind (b, bowed (1.3, 20)); room (b, 0.35); } },
            { "Cordes", "Violoncelle", 3.0, [bowed] (Buf& b) { wind (b, bowed (0.7, 16)); room (b, 0.35); } },
            { "Cordes", "Pizzicato", 1.5, [] (Buf& b) { ks (b, f0, 0.25, 0.9, 7, 0.4); room (b, 0.3); } },
            { "Cordes", "Guitare acoustique", 3.0, [] (Buf& b) {
                ks (b, f0, 0.6, 3.0, 8, 0.25); ks (b, f0 * 1.0015, 0.55, 2.8, 9, 0.22);
                SVF b1, b2; for (int i = 0; i < b.getNumSamples(); ++i) { const double x = b.getSample (0, i);
                    const double y = x + 0.5 * b1.bp (x, 100, 3) + 0.35 * b2.bp (x, 400, 2);
                    b.setSample (0, i, (float) y); b.setSample (1, i, (float) y); } room (b, 0.25); } },
            { "Cordes", "Guitare électrique clean", 2.5, [] (Buf& b) { ks (b, f0, 0.75, 2.4, 12, 0.12); room (b, 0.2);
                for (int i = 0; i < b.getNumSamples(); ++i) { const float tr = (float) (0.04 * std::sin (twoPi * 0.9 * i / sr));
                    b.setSample (0, i, b.getSample (0, i) * (1 + tr)); b.setSample (1, i, b.getSample (1, i) * (1 - tr)); } } },
            { "Cordes", "Basse électrique", 2.0, [] (Buf& b) { ks (b, f0 * 0.5, 0.3, 2.2, 13, 0.3);
                SVF fl; for (int i = 0; i < b.getNumSamples(); ++i) { const double y = fl.lp (b.getSample (0, i), 1600, 0.8);
                    b.setSample (0, i, (float) y); b.setSample (1, i, (float) y); } } },

            // ---------------- Leads
            { "Leads", "Lead sync", 2.0, [] (Buf& b) { syncLead (b, 3.5, 1.5, 0.35); room (b, 0.15); } },
            { "Leads", "Lead acid", 1.5, [] (Buf& b) { acid (b, false, 4.5, 3500, 0.18); } },
            { "Leads", "Lead PWM", 2.0, [] (Buf& b) {
                const int n = b.getNumSamples(); const double dur = n / sr; Square q1, q2; SVF fl;
                for (int i = 0; i < n; ++i) { const double t = i / sr; const double pw = 0.3 + 0.18 * std::sin (twoPi * 1.3 * t);
                    const double x = 0.5 * q1.next (f0, pw) + 0.5 * q2.next (f0 * 1.003, 1.0 - pw);
                    const double v = fl.lp (x, 3200, 0.8) * adsr (t, 0.01, 0.4, 0.75, dur, 0.25);
                    b.setSample (0, i, (float) v); b.setSample (1, i, (float) v); } room (b, 0.2); } },
            { "Leads", "Lead voix robot", 2.0, [] (Buf& b) {
                const int n = b.getNumSamples(); const double dur = n / sr; Saw s; SVF f1, f2;
                for (int i = 0; i < n; ++i) { const double t = i / sr; const double m = 0.5 - 0.5 * std::cos (twoPi * 1.2 * t);
                    const double x = s.next (f0 * (1.0 + 0.005 * std::sin (twoPi * 5.5 * t)));
                    const double v = (f1.bp (x, 400 + 400 * m, 6) + 0.7 * f2.bp (x, 800 + 1200 * m, 7)) * adsr (t, 0.02, 0.4, 0.8, dur, 0.2) * 2.5;
                    b.setSample (0, i, (float) v); b.setSample (1, i, (float) v); } room (b, 0.2); } },
            { "Leads", "Lead sifflet", 2.0, [] (Buf& b) { WindCfg c; c.osc = 2; c.scoop = -120; c.scoopTime = 0.08; c.vibDepth = 30; c.vibRate = 6.0; c.vibDelay = 0.2;
                c.breath = 0.06; c.cutBase = 600; c.cutAmt = 300; c.formantMix = 0.0; wind (b, c); room (b, 0.3); } },
            { "Leads", "Lead hyper saw", 1.8, [] (Buf& b) { saws (b, 9, 38, 7000, 0.3, 0.2, 0.004, 0.5, 0.75, 0.3, 0.8); } },

            // ---------------- Séquenceur (sons courts pour arpèges / séquences)
            { "Séquenceur", "Seq Berlin", 0.8, [] (Buf& b) {
                const int n = b.getNumSamples(); const double dur = n / sr; Square q; SVF fl;
                for (int i = 0; i < n; ++i) { const double t = i / sr;
                    const double v = fl.lp (q.next (f0, 0.4), 300 + 4000 * std::exp (-t / 0.06), 3.0) * adsr (t, 0.001, 0.18, 0.0, dur, 0.05);
                    b.setSample (0, i, (float) v); b.setSample (1, i, (float) v); } } },
            { "Séquenceur", "Seq acid", 0.7, [] (Buf& b) { acid (b, true, 6.0, 2800, 0.09); } },
            { "Séquenceur", "Seq pluck FM", 0.8, [] (Buf& b) { fm (b, 2.0, 5.0, 0.0, 0.05, 0.25, 0.001); } },
            { "Séquenceur", "Seq verre", 1.0, [] (Buf& b) { fm (b, 3.5, 2.5, 0.0, 0.08, 0.35, 0.001); room (b, 0.3); } },
            { "Séquenceur", "Seq bulle", 0.6, [] (Buf& b) {
                const int n = b.getNumSamples(); double ph = 0;
                for (int i = 0; i < n; ++i) { const double t = i / sr; ph += twoPi * f0 * (1.0 + 0.5 * std::exp (-t / 0.02)) / sr;
                    const double v = std::sin (ph) * std::exp (-t / 0.12) * juce::jmin (1.0, t / 0.001);
                    b.setSample (0, i, (float) v); b.setSample (1, i, (float) v); } room (b, 0.3); } },
            { "Séquenceur", "Seq trance", 0.9, [] (Buf& b) { saws (b, 5, 20, 400, 14, 0.07, 0.001, 0.2, 0.0, 0.08); } },
            { "Séquenceur", "Seq chip arp", 0.5, [] (Buf& b) {
                const int n = b.getNumSamples(); Square q;
                for (int i = 0; i < n; ++i) { const double t = i / sr; const int step = (int) (t / 0.03) % 3; const double r = step == 0 ? 1.0 : (step == 1 ? 1.26 : 1.5);
                    const double v = q.next (f0 * r, 0.125) * std::exp (-t / 0.25) * 0.5;
                    b.setSample (0, i, (float) v); b.setSample (1, i, (float) v); } } },

            // ---------------- Synthés en plus
            { "Synthés", "Keys Juno", 2.5, [] (Buf& b) { saws (b, 2, 9, 2400, 1.5, 0.2, 0.004, 0.6, 0.6, 0.3); room (b, 0.25);
                for (int i = 0; i < b.getNumSamples(); ++i) { const float ch = (float) (0.08 * std::sin (twoPi * 0.5 * i / sr));
                    b.setSample (0, i, b.getSample (0, i) * (1 + ch)); b.setSample (1, i, b.getSample (1, i) * (1 - ch)); } } },
            { "Synthés", "Stab house", 1.0, [] (Buf& b) { saws (b, 4, 10, 1800, 3.0, 0.08, 0.002, 0.3, 0.0, 0.1); room (b, 0.3); } },
            { "Basses", "Basse Reese", 3.0, [] (Buf& b) {
                const int n = b.getNumSamples(); const double dur = n / sr; Saw s1, s2; SVF fl;
                for (int i = 0; i < n; ++i) { const double t = i / sr;
                    const double x = s1.next (f0 * 0.5 * 1.008) + s2.next (f0 * 0.5 * 0.992);
                    const double v = std::tanh (fl.lp (x, 700 + 300 * std::sin (twoPi * 0.3 * t), 1.0) * 1.5) * adsr (t, 0.005, 0.5, 0.85, dur, 0.2);
                    b.setSample (0, i, (float) v); b.setSample (1, i, (float) v); } } },
            { "Basses", "808 saturée", 3.0, [] (Buf& b) {
                const int n = b.getNumSamples(); double ph = 0;
                for (int i = 0; i < n; ++i) { const double t = i / sr;
                    ph += twoPi * f0 * 0.5 * std::pow (2.0, (7.0 * std::exp (-t / 0.03)) / 12.0) / sr;
                    const double v = std::tanh (5.0 * std::sin (ph) * std::exp (-t / 2.0)) * juce::jmin (1.0, t / 0.001);
                    b.setSample (0, i, (float) v); b.setSample (1, i, (float) v); } } },
            { "Nappes", "Pad aurore", 5.0, [] (Buf& b) { saws (b, 9, 22, 1800, 0.6, 2.0, 1.2, 1.5, 0.85, 1.2, 1.0);
                Buf o (2, b.getNumSamples()); o.clear(); { const int n = o.getNumSamples(); for (int i = 0; i < n; ++i) { const double t = i / sr;
                    const double v = 0.12 * std::sin (twoPi * f0 * 2.0 * t) * adsr (t, 2.0, 1.0, 0.9, n / sr, 1.2); o.setSample (0, i, (float) v); o.setSample (1, i, (float) v); } }
                b.addFrom (0, 0, o, 0, 0, o.getNumSamples()); b.addFrom (1, 0, o, 1, 0, o.getNumSamples()); room (b, 0.4); } },
        };
        return v;
    }

    inline juce::PropertiesFile& settings();
    inline juce::String presetFolderName();
    inline juce::String userPresetFolderName();

    // emplacement d'origine (Documents\Variant Bibliothèque)
    inline juce::File standardFolder()
    {
        return documentsDir().getChildFile (juce::String::fromUTF8 ("Variant Bibliothèque"));
    }

    // dossiers qui ne sont pas des catégories de sons
    inline bool isSpecialFolderName (const juce::String& n)
    {
        return n == presetFolderName() || n == userPresetFolderName() || n == juce::String::fromUTF8 ("Presets de synthé") || n == "Wavetables";
    }

    // vraie bibliothèque Variant = au moins une catégorie de sons (pas juste les dossiers de presets)
    inline int soundCategoryCount (const juce::File& d)
    {
        if (! d.isDirectory()) return 0;
        int n = 0;
        for (auto& c : d.findChildFiles (juce::File::findDirectories, false)) if (! isSpecialFolderName (c.getFileName())) ++n;
        return n;
    }
    inline bool looksLikeLibrary (const juce::File& d)
    {
        if (soundCategoryCount (d) == 0) return false;
        return d.getChildFile (".variant-cree").existsAsFile() || d.getChildFile (".renommages.txt").existsAsFile()
            || d.getChildFile (presetFolderName()).isDirectory() || d.getChildFile (userPresetFolderName()).isDirectory()
            || d.getFileName().startsWithIgnoreCase ("Variant Biblioth");
    }

    inline bool isBackupName (const juce::String& n)
    {
        const auto l = n.toLowerCase();
        return l.contains ("sauvegarde") || l.contains ("backup") || l.contains ("copie") || l.contains (" old") || l.contains ("ancien");
    }

    // Bibliothèque déplacée à la main : on la cherche (Documents, Bureau, Musique, Téléchargements, racines des disques)
    // dans un dossier dont le nom commence par « Variant Bibliothèque » (ex. « Variant Folder\Variant Bibliothèque @moi »).
    // Les copies de sauvegarde sont ignorées.
    inline juce::File searchMovedLibrary()
    {
        juce::Array<juce::File> starts;
        if (testProfile() != juce::File()) starts.add (testProfile());   // test : seulement le faux profil (Documents, Bureau, autres dossiers…)
        else {
        for (auto w : { juce::File::userDocumentsDirectory, juce::File::userDesktopDirectory, juce::File::userMusicDirectory, juce::File::userHomeDirectory })
            starts.addIfNotAlreadyThere (juce::File::getSpecialLocation (w));
        starts.addIfNotAlreadyThere (juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Downloads"));
        juce::Array<juce::File> drives; juce::File::findFileSystemRoots (drives);
        for (auto& d : drives) starts.addIfNotAlreadyThere (d);
       #if JUCE_MAC
        for (auto& v : juce::File ("/Volumes").findChildFiles (juce::File::findDirectories, false)) starts.addIfNotAlreadyThere (v);
       #endif
        }

        juce::File best; juce::Time bestTime;
        std::function<void (const juce::File&, int)> walk = [&] (const juce::File& dir, int depth)
        {
            for (auto& c : dir.findChildFiles (juce::File::findDirectories, false))
            {
                const auto n = c.getFileName();
                if (n.startsWithChar ('.') || n.startsWithChar ('$') || n.equalsIgnoreCase ("AppData") || n.equalsIgnoreCase ("Windows")
                    || n.equalsIgnoreCase ("Library") || n.equalsIgnoreCase ("Applications") || n.equalsIgnoreCase ("System") || n.equalsIgnoreCase ("private")   // Mac
                    || n.startsWithIgnoreCase ("Program") || n.equalsIgnoreCase ("System Volume Information") || n.equalsIgnoreCase ("node_modules")
                    || (dir.isRoot() && juce::StringArray { "usr", "bin", "sbin", "opt", "cores", "dev", "etc", "var", "tmp", "Network" }.contains (n))   // Mac : système
                    || n.equalsIgnoreCase ("Recovery") || n.equalsIgnoreCase ("PerfLogs") || n.equalsIgnoreCase ("Volumes") || isBackupName (n)) continue;
                const bool named = n.startsWithIgnoreCase ("Variant Biblioth");
                if ((named || c.getChildFile (".variant-cree").existsAsFile()) && c != standardFolder() && looksLikeLibrary (c))
                {
                    const auto t = c.getLastModificationTime();
                    if (best == juce::File() || t > bestTime) { best = c; bestTime = t; }
                    continue;
                }
                if (depth > 0) walk (c, depth - 1);
            }
        };
        // partout : la bibliothèque peut avoir été rangée n importe où (autre disque, dossier perso…)
        for (auto& s : starts) if (s.isDirectory()) walk (s, s.isRoot() ? 5 : 4);
        return best;
    }

    inline juce::CriticalSection& rootLock() { static juce::CriticalSection c; return c; }
    inline juce::File& rootCache() { static juce::File f; return f; }

    // Dossier de la bibliothèque : celui choisi avec « Localiser un dossier… », sinon Documents\Variant Bibliothèque,
    // sinon la bibliothèque déplacée retrouvée toute seule (et retenue pour la suite).
    inline juce::File defaultFolder()
    {
        const juce::ScopedLock sl (rootLock());
        auto& cache = rootCache();
        if (cache != juce::File() && cache.isDirectory()) return cache;

        const juce::File chosen (settings().getValue ("libraryRoot"));
        if (chosen != juce::File() && chosen.isDirectory()) return cache = chosen;
        // emplacement noté par l installateur (Windows / Mac), où que l utilisateur ait mis la bibliothèque
        {
            const auto hintFile = appDataDir().getChildFile ("Variant").getChildFile ("library.txt");
            const juce::File hint (hintFile.loadFileAsString().trim());
            if (hintFile.existsAsFile() && hint != juce::File() && hint.isDirectory() && soundCategoryCount (hint) > 0)
            {
                settings().setValue ("libraryRoot", hint.getFullPathName());
                settings().saveIfNeeded();
                return cache = hint;
            }
        }
        const auto std0 = standardFolder();
        if (soundCategoryCount (std0) > 0) return cache = std0;
        static bool searched = false;   // recherche sur le disque : une seule fois par session
        if (searched) return std0;
        searched = true;
        const auto moved = searchMovedLibrary();
        if (moved != juce::File())
        {
            settings().setValue ("libraryRoot", moved.getFullPathName());
            settings().saveIfNeeded();
            return cache = moved;
        }
        if (std0.isDirectory()) cache = std0;
        return std0;
    }

    inline void setLibraryRoot (const juce::File& d)
    {
        const juce::ScopedLock sl (rootLock());
        settings().setValue ("libraryRoot", d.getFullPathName());
        settings().saveIfNeeded();
        rootCache() = d;
    }

    // son introuvable (bibliothèque déplacée) : même chemin relatif dans la bibliothèque actuelle
    inline juce::File relocateMoved (const juce::File& f, const juce::File& libRoot)
    {
        if (f == juce::File() || f.existsAsFile()) return f;
        // « …\Variant Bibliothèque…\Catégorie\son.wav » -> « <bibliothèque>\Catégorie\son.wav »
        juce::StringArray parts; parts.addTokens (f.getFullPathName().replaceCharacter ('\\', '/'), "/", "");
        for (int i = parts.size() - 2; i >= 0; --i)
            if (parts[i].startsWithIgnoreCase ("Variant Biblioth"))
            {
                auto cand = libRoot;
                for (int j = i + 1; j < parts.size(); ++j) cand = cand.getChildFile (parts[j]);
                if (cand.existsAsFile()) return cand;
                break;
            }
        // sinon : même dossier + même nom dans la bibliothèque (ex. « Variant Basses\son.wav »)
        const auto cand = libRoot.getChildFile (f.getParentDirectory().getFileName()).getChildFile (f.getFileName());
        if (cand.existsAsFile()) return cand;
        return f;
    }

    // Sons renommés dans la bibliothèque : « .renommages.txt » (une ligne « Dossier/ancien.wav|Dossier/nouveau.wav »).
    // Un projet FL ou un preset qui pointe vers l'ancien nom retrouve le son sous son nouveau nom.
    inline juce::File resolveRenamed (const juce::File& f, const juce::File& libRoot = defaultFolder())
    {
        if (f == juce::File() || f.existsAsFile()) return f;
        {
            const auto moved = relocateMoved (f, libRoot);
            if (moved.existsAsFile()) return moved;
        }
        const auto mapFile = libRoot.getChildFile (".renommages.txt");
        if (! mapFile.existsAsFile()) return f;
        const auto key = (f.getParentDirectory().getFileName() + "/" + f.getFileName()).toLowerCase();
        juce::StringArray lines;
        lines.addLines (mapFile.loadFileAsString());
        for (auto& l : lines)
        {
            const auto oldRel = l.upToFirstOccurrenceOf ("|", false, false).trim();
            const auto newRel = l.fromFirstOccurrenceOf ("|", false, false).trim();
            if (oldRel.isEmpty() || newRel.isEmpty() || oldRel.toLowerCase() != key) continue;
            // même dossier que l'ancien chemin (bibliothèque déplacée ou non), sinon la bibliothèque par défaut
            auto cand = f.getParentDirectory().getChildFile (newRel.fromLastOccurrenceOf ("/", false, false));
            if (cand.existsAsFile()) return cand;
            cand = libRoot.getChildFile (newRel);
            if (cand.existsAsFile()) return cand;
        }
        return f;
    }

    // nom d'origine d'un son renommé (avant le renommage) : il contenait souvent la note (« _D# », « - C »…)
    inline juce::String originalName (const juce::File& f, const juce::File& libRoot = defaultFolder())
    {
        const auto mapFile = libRoot.getChildFile (".renommages.txt");
        if (! mapFile.existsAsFile()) return {};
        const auto key = (f.getParentDirectory().getFileName() + "/" + f.getFileName()).toLowerCase();
        juce::StringArray lines;
        lines.addLines (mapFile.loadFileAsString());
        for (auto& l : lines)
            if (l.fromFirstOccurrenceOf ("|", false, false).trim().toLowerCase() == key)
                return l.upToFirstOccurrenceOf ("|", false, false).fromLastOccurrenceOf ("/", false, false).upToLastOccurrenceOf (".", false, false);
        return {};
    }

    // Note du son d'après son nom : 0 (C) … 11 (B), ou -1 si le nom ne dit rien.
    // - son exporté d'un plugin (« … ( Hive ) », « ( Serum ) », ancien « Variant … ») : exporté en C -> C
    // - note écrite dans le nom (« _D# », « - C », « [C] », « (Dmin 155) », « Em7 », « C3 ») : cette note
    inline int noteFromName (const juce::String& baseName)
    {
        static const char* plugins[] = { "hive", "diva", "zen", "dx7", "jup8v", "srx", "purity", "sem v3", "semv3", "surge", "acidv", "serum",
                                         "jd800", "jv1080", "triton", "martinic", "podloski", "loungel", "variant", "pigments", "vital", "massive",
                                         "omnisphere", "nexus", "sylenth", "spire", "kontakt", "keyscape", "zenology", "analog lab", "phaseplant" };
        const auto low = baseName.toLowerCase().trim();
        if (low.startsWith ("variant ")) return 0;
        if (low.endsWithChar (')') && low.contains ("("))
        {
            const auto src = low.fromLastOccurrenceOf ("(", false, false).upToLastOccurrenceOf (")", false, false).trim();
            for (auto* p : plugins) if (src == p) return 0;
        }
        // dernière note isolée du nom : lettre A-G, # ou b, puis m / min / maj / chiffres, entourée de séparateurs
        static const int pcOf[] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
        int found = -1;
        const auto s = " " + baseName + " ";
        auto isSep = [] (juce::juce_wchar c) { return c == ' ' || c == '_' || c == '-' || c == '[' || c == ']' || c == '(' || c == ')' || c == '.' || c == ','; };
        for (int i = 1; i < s.length() - 1; ++i)
        {
            const auto c = s[i];
            if (c < 'A' || c > 'G' || ! isSep (s[i - 1])) continue;
            int j = i + 1, pc = pcOf[c - 'A'];
            if (s[j] == '#') { ++pc; ++j; }
            else if (s[j] == 'b' && (isSep (s[j + 1]) || s[j + 1] == 'm' || juce::CharacterFunctions::isDigit (s[j + 1]))) { --pc; ++j; }
            if (s.substring (j, j + 3).equalsIgnoreCase ("min") || s.substring (j, j + 3).equalsIgnoreCase ("maj")) j += 3;
            else if (s[j] == 'm' || s[j] == 'M') ++j;
            while (juce::CharacterFunctions::isDigit (s[j])) ++j;
            if (isSep (s[j])) found = (pc + 12) % 12;
        }
        return found;
    }

    // Crée les fichiers manquants (rapide : quelques secondes au premier lancement)
    inline int buildDefaultLibrary (const std::function<bool()>& shouldStop = {})
    {
        int created = 0;
        // la bibliothèque n'est créée qu'une seule fois : si le dossier existe déjà (même renommé / réorganisé
        // à la main), on n'y touche plus jamais
        const auto root = defaultFolder();
        if (root.getChildFile (".variant-cree").existsAsFile()) return 0;
        if (root.isDirectory() && root.findChildFiles (juce::File::findDirectories, false).size() > 0)
        {
            root.getChildFile (".variant-cree").create();
            return 0;
        }
        juce::WavAudioFormat wav;
        for (auto& it : items())
        {
            if (shouldStop && shouldStop()) break;
            auto dir = defaultFolder().getChildFile (juce::String::fromUTF8 (it.category));
            dir.createDirectory();
            auto file = dir.getChildFile (juce::String::fromUTF8 (it.name) + ".wav");
            if (file.existsAsFile()) continue;

            Buf b (2, (int) (it.seconds * sr));
            b.clear();
            it.render (b);
            finish (b);

            std::unique_ptr<juce::OutputStream> os (new juce::FileOutputStream (file));
            if (! static_cast<juce::FileOutputStream*> (os.get())->openedOk()) continue;
            if (auto* w = wav.createWriterFor (os.get(), sr, 2, 24, {}, 0))
            {
                os.release();
                std::unique_ptr<juce::AudioFormatWriter> writer (w);
                writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
                ++created;
            }
        }
        root.getChildFile (".variant-cree").create();
        return created;
    }

    //==========================================================================
    // Dossiers perso, mémorisés pour tous les projets
    // Créés à la demande et refermés quand le dernier Variant se ferme (releaseSettings) : un PropertiesFile laissé
    // en variable statique était détruit pendant que Windows décharge le plugin, et son minuteur attendait
    // un thread déjà arrêté -> FL figé à la fermeture.
    inline std::unique_ptr<juce::PropertiesFile>& settingsHolder() { static std::unique_ptr<juce::PropertiesFile> p; return p; }
    inline juce::PropertiesFile& settings()
    {
        auto& p = settingsHolder();
        if (p == nullptr)
        {
            juce::PropertiesFile::Options x;
            x.applicationName = "Variant";
            x.filenameSuffix = ".settings";
            x.folderName = "Variant";
            x.osxLibrarySubFolder = "Application Support";
            if (testProfile() != juce::File()) p = std::make_unique<juce::PropertiesFile> (appDataDir().getChildFile ("Variant").getChildFile ("Variant.settings"), x);
            else p = std::make_unique<juce::PropertiesFile> (x);
        }
        return *p;
    }
    inline void releaseSettings()
    {
        auto& p = settingsHolder();
        if (p != nullptr) { p->saveIfNeeded(); p.reset(); }
    }

    inline juce::StringArray userFolders()
    {
        juce::StringArray a;
        a.addTokens (settings().getValue ("libraryFolders"), "|", "");
        a.removeEmptyStrings();
        return a;
    }

    inline void addUserFolder (const juce::File& f)
    {
        auto a = userFolders();
        a.addIfNotAlreadyThere (f.getFullPathName());
        settings().setValue ("libraryFolders", a.joinIntoString ("|"));
        settings().saveIfNeeded();
    }


    inline void removeUserFolder (const juce::String& path)
    {
        auto a = userFolders();
        a.removeString (path);
        settings().setValue ("libraryFolders", a.joinIntoString ("|"));
        settings().saveIfNeeded();
    }
    // Une version de test (3.25 interne) ajoutait le drumkit et le loopkit rangés à côté de la bibliothèque
    // dans la banque : ce n'est pas voulu (les kits vont dans le navigateur du DAW). On les retire une fois.
    inline void undoSiblingKits()
    {
        if (! settings().getBoolValue ("siblingKitsAdded", false)) return;
        const auto parent = defaultFolder().getParentDirectory();
        for (auto& p : userFolders())
        {
            const juce::File f (p);
            if (f.getParentDirectory() == parent && f.getFileName().startsWithIgnoreCase ("Variant ") && ! f.getFileName().startsWithIgnoreCase ("Variant Biblioth"))
                removeUserFolder (p);
        }
        settings().removeValue ("siblingKitsAdded");
        settings().saveIfNeeded();
    }


    inline bool hasAudio (const juce::File& d, bool recursive = true)
    {
        return ! d.findChildFiles (juce::File::findFiles, recursive, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3").isEmpty();
    }

    // Bouton « Localiser un dossier… » : dossier choisi =
    //  - ta bibliothèque Variant (déplacée)       -> devient la bibliothèque
    //  - un dossier qui la contient (« Variant Folder » avec bibliothèque + loops + drumkit)
    //                                              -> la bibliothèque, et les autres dossiers de sons à côté sont ajoutés
    //  - n'importe quel dossier de sons            -> ajouté à la banque (un kit en sous-dossiers = une catégorie par sous-dossier)
    // Renvoie le message à afficher.
    inline juce::String locateFolder (const juce::File& f)
    {
        if (! f.isDirectory()) return {};
        auto addOthers = [] (const juce::File& parent, const juce::File& lib, juce::StringArray& added)
        {
            for (auto& c : parent.findChildFiles (juce::File::findDirectories, false))
                if (c != lib && ! c.getFileName().startsWithChar ('.') && ! isBackupName (c.getFileName()) && ! looksLikeLibrary (c) && hasAudio (c))
                { addUserFolder (c); added.add (c.getFileName()); }
        };
        juce::StringArray added;
        if (looksLikeLibrary (f) && (f.getFileName().startsWithIgnoreCase ("Variant Biblioth") || f.getChildFile (".variant-cree").existsAsFile()
                                     || f.getChildFile (".renommages.txt").existsAsFile() || f.getChildFile (presetFolderName()).isDirectory()))
        {
            setLibraryRoot (f);
            return fr ("Bibliothèque trouvée : ") + f.getFullPathName();
        }
        juce::File lib;
        for (auto& c : f.findChildFiles (juce::File::findDirectories, false))
            if (! isBackupName (c.getFileName()) && looksLikeLibrary (c)
                && (c.getFileName().startsWithIgnoreCase ("Variant Biblioth") || c.getChildFile (".variant-cree").existsAsFile()))
            { lib = c; break; }
        if (lib != juce::File())
        {
            setLibraryRoot (lib);
            addOthers (f, lib, added);
            auto m = fr ("Bibliothèque trouvée : ") + lib.getFullPathName();
            if (! added.isEmpty()) m << fr ("\nAjoutés à ta banque : ") << added.joinIntoString (", ");
            return m;
        }
        if (f == defaultFolder() || f.isAChildOf (defaultFolder()))
            return fr ("Ce dossier fait déjà partie de ta bibliothèque.");
        if (! hasAudio (f))
            return fr ("Aucun son trouvé dans ce dossier (wav, aif, flac, ogg, mp3).");
        addUserFolder (f);
        return fr ("Ajouté à ta banque : ") + f.getFileName();
    }

    // les presets voyagent avec la banque de sons : sous-dossier dédié (pas une catégorie de sons)
    inline juce::String presetFolderName() { return juce::String::fromUTF8 ("Presets d'arpège"); }
    // presets complets de chaque utilisateur (un sous-dossier par personne)
    inline juce::String userPresetFolderName() { return juce::String::fromUTF8 ("Presets utilisateur"); }

    // folder = dossier lu pour cette catégorie ; root = dossier ajouté par l'utilisateur (celui qu'on retire)
    struct Category { juce::String name; juce::File folder; bool user = false; juce::File root; };


    // dossier où Métamorphose range ses sons (anciens noms : « Destroy », « Métamorphoses ») : toujours en fin de liste
    inline juce::String morphFolderName() { return juce::String::fromUTF8 ("Métamorphose"); }
    inline juce::File morphFolder()
    {
        const auto root = defaultFolder();
        auto dst = root.getChildFile (morphFolderName());
        for (auto old : { juce::String ("Destroy"), juce::String::fromUTF8 ("Métamorphoses") })
        {
            const auto d = root.getChildFile (old);
            if (! d.isDirectory()) continue;
            if (! dst.isDirectory() && d.moveFileTo (dst)) continue;   // simple renommage
            dst.createDirectory();
            for (auto& f : d.findChildFiles (juce::File::findFiles, false))
                f.moveFileTo (dst.getNonexistentChildFile (f.getFileNameWithoutExtension(), f.getFileExtension(), false));
            if (d.findChildFiles (juce::File::findFilesAndDirectories, true).isEmpty()) d.deleteRecursively();
        }
        return dst;
    }
    inline const char* audioWildcard() { return "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3"; }

    inline std::vector<Category> categories()
    {
        std::vector<Category> c;
        static std::atomic<bool> morphMoved { false };
        if (! morphMoved.exchange (true)) morphFolder();   // ancien dossier « Destroy » renommé une fois
        static std::atomic<bool> kitsChecked { false };
        if (! kitsChecked.exchange (true)) undoSiblingKits();
        for (auto& d : defaultFolder().findChildFiles (juce::File::findDirectories, false))
            if (! isSpecialFolderName (d.getFileName())) c.push_back ({ d.getFileName(), d, false, {} });
        std::sort (c.begin(), c.end(), [] (const Category& a, const Category& b) { return a.name.compareNatural (b.name) < 0; });
        for (auto& p : userFolders())
        {
            const juce::File f (p);
            if (! f.isDirectory()) continue;
            // kit rangé en sous-dossiers (Kicks, Snares, 808s…) sans sons à la racine : une catégorie par sous-dossier
            auto subs = f.findChildFiles (juce::File::findDirectories, false);
            subs.removeIf ([] (const juce::File& s) { return s.getFileName().startsWithChar ('.')
                                                          || s.findChildFiles (juce::File::findFiles, true, audioWildcard()).isEmpty(); });
            if (! subs.isEmpty() && f.findChildFiles (juce::File::findFiles, false, audioWildcard()).isEmpty())
            {
                struct Cmp { static int compareElements (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()); } };
                Cmp cmp; subs.sort (cmp);
                for (auto& s : subs) c.push_back ({ s.getFileName(), s, true, f });
            }
            else c.push_back ({ f.getFileName(), f, true, f });
        }
        // les sons de Métamorphose tout à la fin (les flèches de la banque tombent d abord sur les vrais sons)
        std::stable_partition (c.begin(), c.end(), [] (const Category& k) { return k.user || k.name != morphFolderName(); });
        return c;
    }

    inline juce::Array<juce::File> soundsIn (const juce::File& folder)
    {
        juce::Array<juce::File> files;
        for (auto& f : folder.findChildFiles (juce::File::findFiles, true, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3"))
            files.add (f);
        struct Cmp { static int compareElements (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()); } };
        Cmp cmp;
        files.sort (cmp);
        return files;
    }

    //==========================================================================
    // Catégories perso (« Trap », « Ambiant »…) : un son peut être dans plusieurs, rien n'est déplacé sur le disque
    // (gardées dans les réglages de Variant : tes dossiers de sons ne sont jamais touchés)
    namespace tags
    {
        inline std::unique_ptr<juce::XmlElement> load()
        {
            auto x = juce::parseXML (settings().getValue ("soundTags"));
            if (x == nullptr || ! x->hasTagName ("TAGS")) x = std::make_unique<juce::XmlElement> ("TAGS");
            return x;
        }
        inline void save (const juce::XmlElement& x) { settings().setValue ("soundTags", x.toString (juce::XmlElement::TextFormat().singleLine())); settings().saveIfNeeded(); }
        inline juce::XmlElement* find (juce::XmlElement& x, const juce::String& name)
        {
            for (auto* t : x.getChildWithTagNameIterator ("TAG")) if (t->getStringAttribute ("name") == name) return t;
            return nullptr;
        }
        inline juce::StringArray names()
        {
            juce::StringArray a; auto x = load();
            for (auto* t : x->getChildWithTagNameIterator ("TAG")) a.add (t->getStringAttribute ("name"));
            return a;
        }
        inline bool create (const juce::String& name)
        {
            const auto n = name.trim();
            auto x = load();
            if (n.isEmpty() || find (*x, n) != nullptr) return false;
            x->createNewChildElement ("TAG")->setAttribute ("name", n);
            save (*x); return true;
        }
        inline bool rename (const juce::String& from, const juce::String& to)
        {
            auto x = load();
            auto* t = find (*x, from);
            if (t == nullptr || to.trim().isEmpty() || find (*x, to.trim()) != nullptr) return false;
            t->setAttribute ("name", to.trim()); save (*x); return true;
        }
        inline void remove (const juce::String& name) { auto x = load(); if (auto* t = find (*x, name)) { x->removeChildElement (t, true); save (*x); } }
        inline bool contains (const juce::String& name, const juce::File& f)
        {
            auto x = load();
            if (auto* t = find (*x, name)) for (auto* s : t->getChildWithTagNameIterator ("S")) if (s->getStringAttribute ("p") == f.getFullPathName()) return true;
            return false;
        }
        inline void toggle (const juce::String& name, const juce::File& f)
        {
            auto x = load();
            auto* t = find (*x, name);
            if (t == nullptr) return;
            for (auto* s : t->getChildWithTagNameIterator ("S"))
                if (s->getStringAttribute ("p") == f.getFullPathName()) { t->removeChildElement (s, true); save (*x); return; }
            t->createNewChildElement ("S")->setAttribute ("p", f.getFullPathName());
            save (*x);
        }
        inline juce::Array<juce::File> sounds (const juce::String& name)
        {
            juce::Array<juce::File> a; auto x = load();
            if (auto* t = find (*x, name)) for (auto* s : t->getChildWithTagNameIterator ("S")) { const juce::File f (s->getStringAttribute ("p")); if (f.existsAsFile()) a.add (f); }
            return a;
        }
        inline std::map<juce::String, juce::StringArray> byPath()   // chemin du son -> ses catégories
        {
            std::map<juce::String, juce::StringArray> m; auto x = load();
            for (auto* t : x->getChildWithTagNameIterator ("TAG"))
                for (auto* s : t->getChildWithTagNameIterator ("S")) m[s->getStringAttribute ("p")].addIfNotAlreadyThere (t->getStringAttribute ("name"));
            return m;
        }
        inline juce::StringArray of (const juce::File& f)
        {
            juce::StringArray a; auto x = load();
            for (auto* t : x->getChildWithTagNameIterator ("TAG"))
                for (auto* s : t->getChildWithTagNameIterator ("S")) if (s->getStringAttribute ("p") == f.getFullPathName()) { a.add (t->getStringAttribute ("name")); break; }
            return a;
        }
    }
}
