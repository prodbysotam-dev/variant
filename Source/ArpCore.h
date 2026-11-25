#pragma once

#include <juce_core/juce_core.h>
#include <vector>
#include <array>
#include <algorithm>
#include "Theory.h"

// Définitions partagées par l'arpège temps réel et le générateur MIDI.
namespace arp
{
    enum Mode { Up = 0, Down, UpDown, DownUp, UpDownIncl, Random, Played, Converge, Diverge, Chord, PinkyUp, Thumb };

    inline juce::StringArray modeNames()
    {
        return { fr ("Montant"), fr ("Descendant"), fr ("Montant → Descendant"), fr ("Descendant → Montant"),
                 fr ("Aller-retour (inclusif)"), fr ("Aléatoire"), fr ("Ordre joué"), fr ("Convergent"), fr ("Divergent"),
                 fr ("Accord (toutes les notes)"), fr ("Pinky (note haute)"), fr ("Pouce (note basse)") };
    }

    enum Rate { R1_1 = 0, R1_2, R1_4, R1_4T, R1_8, R1_8D, R1_8T, R1_16, R1_16D, R1_16T, R1_32 };

    inline juce::StringArray rateNames()
    {
        return { "1/1", "1/2", "1/4", fr ("1/4 triolet"), "1/8", fr ("1/8 pointé"), fr ("1/8 triolet"),
                 "1/16", fr ("1/16 pointé"), fr ("1/16 triolet"), "1/32" };
    }

    inline double rateBeats (int r)
    {
        static const double b[] = { 4.0, 2.0, 1.0, 2.0 / 3.0, 0.5, 0.75, 1.0 / 3.0, 0.25, 0.375, 1.0 / 6.0, 0.125 };
        return b[juce::jlimit (0, 10, r)];
    }

    enum OctDir { OctUp = 0, OctDown, OctBoth, OctAuto };

    inline juce::StringArray octDirNames()
    {
        return { fr ("Vers le haut"), fr ("Vers le bas"), fr ("Haut et bas"), fr ("Auto (selon le registre)") };
    }

    struct Settings
    {
        int   mode = Up, rate = R1_16, octaves = 1, repeat = 1, numSteps = 16, source = 0, velMode = 0, octDir = OctAuto, sliceCount = 8;
        float gate = 0.6f, swing = 0.0f, humanize = 0.35f, freedom = 0.3f;
        bool  latch = false, snap = false;
        int   key = 0, scaleIdx = theory::Minor;
        // options en plus (valeurs par défaut = arpège classique)
        float prob = 1.0f, ratchet = 0.0f, accent = 0.0f, velRamp = 0.0f;
        int   ratchetN = 2, transpose = 0, rotate = 0;
        bool  restart = false, bass = false;
        std::array<float, 16> steps {};
    };

    constexpr int chordMarker = -1000;

    // Décalages d'octave : en mode Auto, on monte si on joue grave, on descend si on joue aigu.
    inline int octaveOffsets (int octaves, int dir, double avgNote, std::array<int, 4>& out)
    {
        octaves = juce::jlimit (1, 4, octaves);
        if (dir == OctAuto) dir = avgNote < 54.0 ? OctUp : (avgNote > 74.0 ? OctDown : OctBoth);

        static const int up[]   = { 0, 12, 24, 36 };
        static const int down[] = { 0, -12, -24, -36 };
        static const int both[] = { 0, 12, -12, 24 };
        const int* src = dir == OctUp ? up : (dir == OctDown ? down : both);
        for (int i = 0; i < octaves; ++i) out[(size_t) i] = src[i];
        return octaves;
    }

    // Construit l'ordre des notes. "base" = notes dans l'ordre joué.
    // Le résultat est écrit dans "out" (capacité réservée : pas d'allocation en temps réel).
    inline void buildPattern (const std::vector<int>& base, int mode, int octaves, int octDir, int repeat,
                              std::vector<int>& out, std::vector<int>& scratch)
    {
        out.clear();
        if (base.empty()) return;
        if (mode == Chord) { out.push_back (chordMarker); return; }

        double avg = 0.0;
        for (int n : base) avg += n;
        avg /= (double) base.size();

        std::array<int, 4> offs {};
        const int numOffs = octaveOffsets (octaves, octDir, avg, offs);

        scratch.clear();
        for (int o = 0; o < numOffs; ++o)
            for (int n : base)
            {
                const int v = n + offs[(size_t) o];
                if (v >= 0 && v <= 127) scratch.push_back (v);
            }

        if (mode != Played)
        {
            std::sort (scratch.begin(), scratch.end());
            scratch.erase (std::unique (scratch.begin(), scratch.end()), scratch.end());
        }
        if (scratch.empty()) return;

        const int n = (int) scratch.size();
        auto push = [&] (int v) { for (int r = 0; r < juce::jmax (1, repeat); ++r) out.push_back (v); };

        switch (mode)
        {
            case Down:       for (int i = n - 1; i >= 0; --i) push (scratch[(size_t) i]); break;
            case UpDown:     for (int i = 0; i < n; ++i) push (scratch[(size_t) i]);
                             for (int i = n - 2; i >= 1; --i) push (scratch[(size_t) i]); break;
            case DownUp:     for (int i = n - 1; i >= 0; --i) push (scratch[(size_t) i]);
                             for (int i = 1; i <= n - 2; ++i) push (scratch[(size_t) i]); break;
            case UpDownIncl: for (int i = 0; i < n; ++i) push (scratch[(size_t) i]);
                             for (int i = n - 1; i >= 0; --i) push (scratch[(size_t) i]); break;
            case Converge:
            {
                int lo = 0, hi = n - 1;
                while (lo <= hi) { push (scratch[(size_t) lo++]); if (lo <= hi) push (scratch[(size_t) hi--]); }
                break;
            }
            case Diverge:
            {
                const int mid = (n - 1) / 2;
                push (scratch[(size_t) mid]);
                for (int d = 1; d < n; ++d)
                {
                    if (mid + d < n)  push (scratch[(size_t) (mid + d)]);
                    if (mid - d >= 0) push (scratch[(size_t) (mid - d)]);
                }
                break;
            }
            case PinkyUp:
                if (n == 1) push (scratch[0]);
                for (int i = 0; i < n - 1; ++i) { push (scratch[(size_t) i]); push (scratch[(size_t) n - 1]); }
                break;
            case Thumb:
                if (n == 1) push (scratch[0]);
                for (int i = 1; i < n; ++i) { push (scratch[0]); push (scratch[(size_t) i]); }
                break;
            case Up: case Random: case Played: default:
                for (int i = 0; i < n; ++i) push (scratch[(size_t) i]);
                break;
        }
    }

    //==========================================================================
    // "Feeling" d'un pas : accents, petites imprécisions et libertés (jeu moins robotique).
    struct StepFeel
    {
        bool rest = false, ratchet = false;
        int octaveShift = 0, passing = 0;
        float vel = 1.0f, gate = 1.0f;
        double delayBeats = 0.0;
    };

    inline StepFeel decideStep (juce::Random& rng, const Settings& s, juce::int64 k)
    {
        StepFeel f;
        const float hu = juce::jlimit (0.0f, 1.0f, s.humanize);
        const float fr = juce::jlimit (0.0f, 1.0f, s.freedom);

        // accents naturels : temps forts un peu plus appuyés
        const int pos = (int) (k % 4);
        f.vel = pos == 0 ? 1.0f + 0.12f * hu : (pos == 2 ? 1.0f : 1.0f - 0.12f * hu);
        f.vel *= 1.0f + (rng.nextFloat() * 2.0f - 1.0f) * 0.2f * hu;
        f.gate = 1.0f + (rng.nextFloat() * 2.0f - 1.0f) * 0.3f * hu;
        f.delayBeats = rng.nextFloat() * 0.03 * hu;

        if (fr > 0.0f && k > 0)
        {
            const float r = rng.nextFloat();
            if (r < 0.05f * fr)                      f.rest = true;
            else if (r < 0.05f * fr + 0.08f * fr)    f.ratchet = true;

            if (rng.nextFloat() < 0.22f * fr) f.octaveShift = rng.nextBool() ? 12 : -12;
            if (rng.nextFloat() < 0.16f * fr) f.passing = rng.nextBool() ? 1 : -1;
        }
        return f;
    }

    // Note voisine dans la gamme (dir = +1 ou -1)
    inline int scaleNeighbour (int note, int dir, int key, int scaleIdx)
    {
        const auto& sc = theory::scaleAt (scaleIdx);
        for (int d = 1; d <= 4; ++d)
        {
            const int c = note + dir * d;
            if (theory::inScale (c, key, sc)) return c;
        }
        return note;
    }

    // Applique octave libre + note de passage, en restant dans une zone confortable
    inline int applyFeel (int note, const StepFeel& f, const Settings& s, int centre)
    {
        if (f.octaveShift != 0)
        {
            int shift = f.octaveShift;
            if (note + shift > centre + 19) shift = -12;   // trop aigu : on redescend
            if (note + shift < centre - 19) shift = 12;    // trop grave : on remonte
            note += shift;
        }
        if (f.passing != 0)
            note = scaleNeighbour (note, f.passing, s.key, s.scaleIdx);
        return juce::jlimit (0, 127, note);
    }

    //==========================================================================
    // Hasard musical : tirages pondérés qui sonnent bien sans effort
    struct RandomArp
    {
        int mode = Up, rate = R1_16, octaves = 1, octDir = OctAuto, repeat = 1, numSteps = 16;
        float gate = 0.6f, swing = 0.0f, human = 0.35f, freedom = 0.3f;
        std::array<float, 16> steps {};
    };

    inline int weightedPick (juce::Random& rng, std::initializer_list<std::pair<int, float>> choices)
    {
        float total = 0.0f;
        for (auto& c : choices) total += c.second;
        float x = rng.nextFloat() * total;
        for (auto& c : choices) { x -= c.second; if (x <= 0.0f) return c.first; }
        return choices.begin()->first;
    }

    // Motif rythmique : euclidien, syncopé 3-3-2, droit accentué ou "ghost notes".
    // Le premier pas est toujours joué (accent) : le motif reste calé sur le temps.
    inline void randomPattern (juce::Random& rng, int numSteps, std::array<float, 16>& steps)
    {
        steps.fill (0.0f);
        const int n = juce::jlimit (1, 16, numSteps);
        const int kind = weightedPick (rng, { { 0, 4.0f }, { 1, 2.5f }, { 2, 2.0f }, { 3, 1.5f } });
        auto accentOf = [n] (int i) { return i == 0 || (n % 4 == 0 && i % (n / 4) == 0); };

        if (kind == 0)   // euclidien : k coups répartis au mieux sur n pas
        {
            const int k = juce::jlimit (2, n, (int) std::round (n * (0.45f + rng.nextFloat() * 0.4f)));
            const int rot = 0;
            for (int i = 0; i < n; ++i)
                if (((i + rot) * k) % n < k) steps[(size_t) i] = accentOf (i) ? 1.0f : 0.62f + rng.nextFloat() * 0.25f;
        }
        else if (kind == 1)   // syncopé 3-3-2 (ou 3-3-3-3-2-2 sur 16)
        {
            static const int g8[] = { 3, 3, 2 }, g16[] = { 3, 3, 3, 3, 2, 2 };
            const int* g = n >= 12 ? g16 : g8;
            const int gc = n >= 12 ? 6 : 3;
            int pos = 0;
            for (int j = 0; pos < n; j = (j + 1) % gc)
            {
                steps[(size_t) pos] = pos == 0 ? 1.0f : 0.8f + rng.nextFloat() * 0.15f;
                if (rng.nextFloat() < 0.35f && pos + 1 < n) steps[(size_t) pos + 1] = 0.4f + rng.nextFloat() * 0.15f;   // petite note fantôme
                pos += g[j];
            }
        }
        else if (kind == 2)   // droit, accents sur les temps, quelques silences
        {
            for (int i = 0; i < n; ++i)
                steps[(size_t) i] = accentOf (i) ? 1.0f : (rng.nextFloat() < 0.18f ? 0.0f : 0.55f + rng.nextFloat() * 0.3f);
        }
        else   // ghost notes : tout joue, dynamique très contrastée
        {
            for (int i = 0; i < n; ++i)
                steps[(size_t) i] = accentOf (i) ? 1.0f : (i % 2 == 0 ? 0.7f + rng.nextFloat() * 0.2f : 0.3f + rng.nextFloat() * 0.2f);
        }
        steps[0] = 1.0f;
    }

    inline RandomArp randomArp (juce::Random& rng)
    {
        RandomArp a;
        a.mode = weightedPick (rng, { { Up, 3.0f }, { UpDown, 3.0f }, { Down, 1.5f }, { DownUp, 1.0f }, { UpDownIncl, 1.0f },
                                      { Played, 1.0f }, { Random, 0.8f }, { Converge, 1.0f }, { Diverge, 1.0f },
                                      { PinkyUp, 1.0f }, { Thumb, 1.0f }, { Chord, 0.4f } });
        a.rate = weightedPick (rng, { { R1_16, 5.0f }, { R1_8, 3.0f }, { R1_8T, 1.0f }, { R1_16T, 0.8f }, { R1_8D, 0.8f }, { R1_4, 0.5f } });
        const bool triplet = a.rate == R1_8T || a.rate == R1_16T;
        a.numSteps = triplet ? (rng.nextBool() ? 12 : 6) : weightedPick (rng, { { 16, 5.0f }, { 8, 3.0f }, { 12, 0.6f }, { 6, 0.4f } });
        a.octaves = weightedPick (rng, { { 1, 3.0f }, { 2, 4.0f }, { 3, 1.2f } });
        a.octDir = weightedPick (rng, { { OctAuto, 3.0f }, { OctUp, 2.0f }, { OctBoth, 1.5f }, { OctDown, 0.8f } });
        a.repeat = rng.nextFloat() < 0.82f ? 1 : 2;
        a.gate = 0.35f + rng.nextFloat() * 0.6f;
        a.swing = (triplet || rng.nextFloat() < 0.45f) ? 0.0f : 0.08f + rng.nextFloat() * 0.27f;
        a.human = 0.2f + rng.nextFloat() * 0.35f;
        a.freedom = 0.0f;   // pas d'improvisation hors gamme
        randomPattern (rng, a.numSteps, a.steps);
        return a;
    }
}
