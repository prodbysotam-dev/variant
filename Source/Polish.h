#pragma once
// « Qualité+ » : chaque one-shot chargé est reconnu (basse, lead, pad, clavier…) puis
//  1. poli hors temps réel comme le ferait un ingé son : graves propres et en mono, moins de boue,
//     un peu d'air et d'harmoniques, dynamique tenue (sans écraser l'attaque), niveau homogène
//  2. reçoit des effets cohérents avec son type (rien sur les basses, petite plate + écho sur les leads…)
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <cmath>
#include <vector>

namespace polish
{
    enum Category { Unknown = 0, Bass, Lead, Pad, Keys, ElectricPiano, Pluck, Strings, Winds, Vocal, Arp, Percussive, NoFx };

    inline const char* categoryName (int c)
    {
        static const char* n[] = { "Inconnu", "Basse", "Lead", "Nappe", "Clavier", "Piano électrique", "Pluck", "Cordes", "Vents", "Voix", "Arpège", "Percussion", "Effet" };
        return n[juce::jlimit (0, 12, c)];
    }

    // type d'après le nom du fichier et de son dossier, puis d'après le son lui-même si rien n'est clair
    inline Category classify (const juce::File& file, const juce::AudioBuffer<float>& b, double sr)
    {
        const auto nm = (file.getFileNameWithoutExtension() + " " + file.getParentDirectory().getFileName()).toLowerCase()
                            .replaceCharacters ("_-.()[]#@", "          ");
        juce::StringArray w; w.addTokens (nm, " ", ""); w.removeEmptyStrings();
        auto has = [&w] (std::initializer_list<const char*> keys)
        {
            // mots courts (sub, ep, pad, fx…) : mot exact, sinon « Submundo » serait une basse
            for (auto& t : w) for (auto* k : keys) { const auto ks = juce::String::fromUTF8 (k); if (ks.length() <= 3 ? t == ks : t.startsWith (ks)) return true; }
            return false;
        };
        // l'ordre compte : "bass pluck" = basse, "pad arp" = arpège…
        if (has ({ "808", "bass", "basse", "sub", "reese", "wobble" })) return Bass;
        if (has ({ "kick", "snare", "clap", "hat", "hihat", "perc", "drum", "tom", "rim", "shaker", "cymbal", "crash", "ride" })) return Percussive;
        if (has ({ "fx", "sfx", "riser", "impact", "scratch", "noise", "sweep", "downlifter", "uplifter", "fun", "texture" })) return NoFx;
        if (has ({ "arp", "arps", "arpeggio", "seq", "sequence" })) return Arp;
        if (has ({ "vox", "vocal", "voice", "voix", "choir", "chœur", "choeur", "chant" })) return Vocal;
        if (has ({ "rhodes", "wurli", "ep", "epiano", "e-piano", "suitcase" })) return ElectricPiano;
        if (has ({ "piano", "key", "keys", "organ", "orgue", "clav", "jupiter", "synergy" })) return Keys;
        if (has ({ "pad", "pads", "nappe", "atmo", "ambient", "drone" })) return Pad;
        if (has ({ "string", "cordes", "violin", "violon", "cello", "viola", "orchestra", "spiccato", "pizz" })) return Strings;
        if (has ({ "horn", "brass", "cuivre", "trumpet", "trompette", "trombone", "sax", "flute", "flûte", "clarinet", "clarinette", "wind", "oboe" })) return Winds;
        if (has ({ "pluck", "bell", "cloche", "kalimba", "marimba", "mallet", "harp", "harpe", "guitar", "guitare", "koto", "glock", "vibra", "music box" })) return Pluck;
        if (has ({ "lead", "saw", "supersaw", "synth", "square", "hoover", "stab" })) return Lead;

        // rien dans le nom : on écoute le son (brillance, durée)
        const int len = b.getNumSamples();
        if (len < 64) return Unknown;
        double lowE = 0.0, allE = 0.0, lp = 0.0;
        const double c = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * 180.0 / sr);
        int lastLoud = 0; float peak = 1.0e-6f;
        for (int i = 0; i < len; ++i) { float m = 0; for (int ch = 0; ch < b.getNumChannels(); ++ch) m += b.getSample (ch, i); peak = juce::jmax (peak, std::abs (m)); }
        for (int i = 0; i < len; ++i)
        {
            float m = 0; for (int ch = 0; ch < b.getNumChannels(); ++ch) m += b.getSample (ch, i);
            lp += c * (m - lp);
            lowE += lp * lp; allE += (double) m * m;
            if (std::abs (m) > peak * 0.1f) lastLoud = i;
        }
        const double lowShare = lowE / juce::jmax (1.0e-12, allE);
        const double sustainSec = lastLoud / sr;
        if (lowShare > 0.6) return Bass;                // presque tout sous 180 Hz : on n'y touche pas comme un lead
        if (sustainSec < 0.45) return Pluck;            // son court et brillant
        if (sustainSec > 2.5) return Pad;               // son long et tenu
        return Unknown;
    }

    //==========================================================================
    // biquad (formules RBJ) appliqué hors temps réel
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        static Biquad make (int type, double f, double q, double gainDb, double sr)
        {
            Biquad r;
            const double w = 2.0 * juce::MathConstants<double>::pi * juce::jlimit (10.0, sr * 0.45, f) / sr;
            const double cw = std::cos (w), sw = std::sin (w), al = sw / (2.0 * q), A = std::pow (10.0, gainDb / 40.0);
            double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
            switch (type)
            {
                case 0: b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = b0; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; break;          // passe-haut
                case 1: b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = b0; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; break;             // passe-bas
                case 2: b0 = 1 + al * A; b1 = -2 * cw; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * cw; a2 = 1 - al / A; break;  // cloche
                default:   // plateau aigu
                {
                    const double s = 2.0 * std::sqrt (A) * al;
                    b0 = A * ((A + 1) + (A - 1) * cw + s); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - s);
                    a0 = (A + 1) - (A - 1) * cw + s; a1 = 2 * ((A - 1) - (A + 1) * cw); a2 = (A + 1) - (A - 1) * cw - s;
                    break;
                }
            }
            r.b0 = b0 / a0; r.b1 = b1 / a0; r.b2 = b2 / a0; r.a1 = a1 / a0; r.a2 = a2 / a0;
            return r;
        }
        float process (float x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return (float) y; }
        void run (float* d, int n) { for (int i = 0; i < n; ++i) d[i] = process (d[i]); }
    };

    //==========================================================================
    // Polissage hors temps réel (le son de base des variations, du stretch et de l'arpégiateur)
    inline void process (juce::AudioBuffer<float>& buf, double sr, Category cat)
    {
        const int n = buf.getNumSamples(), chs = buf.getNumChannels();
        if (n < 256 || cat == NoFx || cat == Percussive) return;   // percussions et effets : on garde le caractère d'origine
        const bool bass = cat == Bass;

        // niveau d'origine (pour rendre un son poli au même niveau perçu, un poil plus présent)
        auto rmsOf = [&] { double e = 0; for (int c = 0; c < chs; ++c) { const float r = buf.getRMSLevel (c, 0, n); e += r * r; } return std::sqrt (e / chs); };
        const double rms0 = rmsOf();
        const float peak0 = buf.getMagnitude (0, n);
        if (rms0 < 1.0e-6) return;

        // 1. graves propres : décalage DC retiré, infra-graves coupés (sans toucher au corps du son)
        for (int c = 0; c < chs; ++c)
        {
            auto hp = Biquad::make (0, bass ? 22.0 : 35.0, 0.707, 0, sr);
            hp.run (buf.getWritePointer (c), n);
        }
        // 2. graves au centre (mono sous ~120 Hz) : une basse qui tient en club, pas de phase qui bave
        if (chs >= 2)
        {
            float* L = buf.getWritePointer (0); float* R = buf.getWritePointer (1);
            auto sideHp = Biquad::make (0, bass ? 160.0 : 120.0, 0.707, 0, sr);
            const float width = (cat == Pad || cat == Lead || cat == Strings || cat == Vocal) ? 1.12f : 1.0f;   // un peu plus large en haut
            for (int i = 0; i < n; ++i)
            {
                const float m = 0.5f * (L[i] + R[i]);
                const float s = sideHp.process (0.5f * (L[i] - R[i])) * width;
                L[i] = m + s; R[i] = m - s;
            }
        }
        for (int c = 0; c < chs; ++c)
        {
            float* d = buf.getWritePointer (c);
            if (! bass)
            {
                // 3. moins de boue dans le bas-médium, un peu d'air en haut (façon EQ de console)
                auto mud = Biquad::make (2, 320.0, 0.8, cat == Pluck ? -1.0 : -1.8, sr);
                auto air = Biquad::make (3, 10000.0, 0.707, cat == Pad || cat == Strings ? 1.2 : 1.8, sr);
                mud.run (d, n); air.run (d, n);
                // 4. excitateur : harmoniques douces ajoutées au-dessus de 3,5 kHz (présence, sans dureté)
                auto hp = Biquad::make (0, 3500.0, 0.707, 0, sr);
                const float amount = cat == Pad ? 0.05f : 0.08f;
                for (int i = 0; i < n; ++i) { const float h = hp.process (d[i]); d[i] += amount * std::tanh (h * 3.0f) / 3.0f; }
            }
            else
            {
                // basse : harmoniques du grave (on l'entend aussi sur petits haut-parleurs), le sub reste intact
                auto lp = Biquad::make (1, 220.0, 0.707, 0, sr);
                auto hpH = Biquad::make (0, 180.0, 0.707, 0, sr);
                for (int i = 0; i < n; ++i)
                {
                    const float low = lp.process (d[i]);
                    const float harm = hpH.process (std::tanh (low * 2.5f) / 2.5f - low * 0.4f);
                    d[i] += 0.35f * harm;
                }
            }
        }
        // 5. dynamique tenue : compression douce (attaque lente = transitoires intactes), ratio 2:1 au-dessus de -20 dB
        {
            const double att = std::exp (-1.0 / (0.012 * sr)), rel = std::exp (-1.0 / (0.180 * sr));
            const double thr = std::pow (10.0, -20.0 / 20.0) * juce::jmax (0.05, (double) peak0);
            double env = 0.0;
            for (int i = 0; i < n; ++i)
            {
                double x = 0; for (int c = 0; c < chs; ++c) x = juce::jmax (x, (double) std::abs (buf.getSample (c, i)));
                env = x > env ? att * env + (1 - att) * x : rel * env + (1 - rel) * x;
                const double g = env > thr ? std::pow (env / thr, -0.5 * (cat == Pluck ? 0.6 : 1.0)) : 1.0;
                if (g < 0.999) for (int c = 0; c < chs; ++c) buf.setSample (c, i, (float) (buf.getSample (c, i) * g));
            }
        }
        // 6. même niveau perçu qu'avant (+1 dB de présence), crêtes gardées sous -1 dBFS avec un écrêtage doux
        const double rms1 = rmsOf();
        if (rms1 > 1.0e-9) buf.applyGain ((float) (rms0 / rms1 * 1.12));
        const float ceiling = 0.89f;
        for (int c = 0; c < chs; ++c)
        {
            float* d = buf.getWritePointer (c);
            for (int i = 0; i < n; ++i)
                if (std::abs (d[i]) > ceiling * 0.9f) d[i] = std::copysign (ceiling * 0.9f + ceiling * 0.1f * std::tanh ((std::abs (d[i]) - ceiling * 0.9f) / (ceiling * 0.1f)), d[i]);
        }
    }

    //==========================================================================
    // Effets cohérents par type de son (index des presets de FxSlots, mix) : rien sur basses / percussions / effets
    struct FxPick { int type; int preset; float mix; };
    // types FxSlots : Chorus = 5, Delay = 12, Reverb = 13
    inline std::vector<FxPick> effectsFor (Category c)
    {
        constexpr int Chorus = 5, Delay = 12, Reverb = 13;
        // Reverb : 0 Petite pièce, 1 Studio boisé, 2 Plate brillante, 3 Salle de concert
        // Delay  : 0 Écho 1/8 pointé, 1 Ping-pong 1/8 · Chorus : 0 Dimension I, 9 Chorus discret
        switch (c)
        {
            case Lead:          return { { Delay, 0, 0.14f }, { Reverb, 2, 0.16f } };
            case Pad:           return { { Chorus, 0, 0.30f }, { Reverb, 3, 0.26f } };
            case Keys:          return { { Reverb, 1, 0.17f } };
            case ElectricPiano: return { { Chorus, 9, 0.28f }, { Reverb, 1, 0.16f } };
            case Pluck:         return { { Delay, 1, 0.13f }, { Reverb, 2, 0.15f } };
            case Strings:       return { { Reverb, 3, 0.24f } };
            case Winds:         return { { Reverb, 1, 0.18f } };
            case Vocal:         return { { Delay, 0, 0.12f }, { Reverb, 2, 0.20f } };
            case Arp:           return { { Delay, 1, 0.11f }, { Reverb, 1, 0.14f } };
            case Unknown:       return { { Reverb, 0, 0.14f } };   // juste une petite pièce : de l'espace, pas d'effet de style
            default:            return {};                           // basse, percussion, effet : rien
        }
    }
}
