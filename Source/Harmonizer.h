#pragma once
// « Accords autour de mes notes » : tes notes restent, des accords sont posés en dessous (et une basse).
// - quelques notes longues isolées  -> ce sont des fondamentales : un accord est construit sur chacune
// - une mélodie                     -> un accord par segment (2 temps ou 1 mesure) qui contient les notes importantes
#include "MidiGenerator.h"

namespace gen
{
    struct Harmony { std::vector<Chord> chords; std::vector<Note> notes; double lengthBeats = 4.0; };

    inline Harmony harmonize (std::vector<Note> src, int key, int scale, int richness, bool withBass, int variant)
    {
        Harmony res;
        if (src.empty()) return res;
        std::sort (src.begin(), src.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
        std::vector<Chord> chords;
        const auto& sc = theory::scaleAt (scale);
        juce::Random rng ((juce::int64) variant * 7919 + 13);
        double end = 0;
        for (auto& n : src) end = juce::jmax (end, n.start + n.length);
        const double total = juce::jlimit (4.0, 64.0, std::ceil (end / 4.0) * 4.0);
    
        // ---- segments
        struct Seg { double start, end; std::vector<const Note*> ns; bool rootGiven = false; int givenRoot = 0; };
        std::vector<Seg> segs;
        {
            struct Group { double start; std::vector<const Note*> ns; };
            std::vector<Group> groups;
            for (auto& n : src)
            {
                if (! groups.empty() && n.start < groups.back().start + 0.12) groups.back().ns.push_back (&n);
                else groups.push_back ({ n.start, { &n } });
            }
            double avgLen = 0; for (auto& n : src) avgLen += n.length; avgLen /= (double) src.size();
            const double perBar = (double) groups.size() / juce::jmax (1.0, total / 4.0);
            const bool rootsMode = perBar <= 2.01 && avgLen >= 0.9;
            if (rootsMode)
            {
                for (size_t i = 0; i < groups.size(); ++i)
                {
                    Seg s;
                    s.start = groups[i].start;
                    s.end = i + 1 < groups.size() ? groups[i + 1].start : juce::jmax (total, groups[i].start + 1.0);
                    s.ns = groups[i].ns;
                    int lowest = 127; for (auto* n : s.ns) lowest = juce::jmin (lowest, n->pitch);
                    s.rootGiven = true; s.givenRoot = lowest;
                    segs.push_back (s);
                }
            }
            else
            {
                const double step = perBar > 6.0 ? 4.0 : 2.0;   // mélodie rapide : un accord par mesure
                for (double t = 0; t < total - 1.0e-6; t += step)
                {
                    Seg s; s.start = t; s.end = t + step;
                    for (auto& n : src) if (n.start < s.end - 1.0e-6 && n.start + n.length > s.start + 1.0e-6) s.ns.push_back (&n);
                    segs.push_back (s);
                }
                std::vector<Seg> merged;   // segment sans notes : on prolonge l'accord précédent
                for (auto& s : segs)
                {
                    if (s.ns.empty() && ! merged.empty()) merged.back().end = s.end;
                    else merged.push_back (s);
                }
                segs = merged;
            }
        }
    
        // ---- choix des accords
        const auto& types = theory::chordTypes();
        const int maxSize = richness == 0 ? 3 : (richness == 1 ? 4 : 5);
        chords.clear();
        int prevRoot = -1;
        for (size_t si = 0; si < segs.size(); ++si)
        {
            auto& s = segs[si];
            float w[12] = {};
            for (auto* n : s.ns)
            {
                const double a = juce::jmax (n->start, s.start), b = juce::jmin (n->start + n->length, s.end);
                float wt = (float) juce::jmax (0.05, b - a);
                if (std::abs (n->start - s.start) < 0.13) wt *= 2.0f;                        // posée sur le changement d'accord
                else if (std::abs (std::fmod (n->start, 1.0)) < 0.05) wt *= 1.3f;           // sur un temps
                w[theory::mod12 (n->pitch)] += wt;
            }
            struct Cand { float score; int root, type; };
            std::vector<Cand> cands;
            for (int root = 0; root < 12; ++root)
            {
                if (s.rootGiven && root != theory::mod12 (s.givenRoot)) continue;
                for (int ti = 0; ti < (int) types.size(); ++ti)
                {
                    const auto& ty = types[(size_t) ti];
                    if (ty.iv.size() < 3 || (int) ty.iv.size() > maxSize) continue;
                    int tm = 0; for (int iv : ty.iv) tm |= 1 << theory::mod12 (root + iv);
                    float score = 0;
                    for (int pc = 0; pc < 12; ++pc)
                    {
                        if (w[pc] > 0.0f) score += (tm & (1 << pc)) ? 2.0f * w[pc] : (theory::inScale (pc, key, sc) ? -0.5f * w[pc] : -2.5f * w[pc]);
                        if (tm & (1 << pc)) score += theory::inScale (pc, key, sc) ? 0.6f : -3.0f;   // reste dans la gamme
                    }
                    if (! s.rootGiven && ! theory::inScale (root, key, sc)) score -= 4.0f;
                    if (ty.iv.size() == 3) score += 0.4f;
                    if (juce::String (ty.name) == juce::String::fromUTF8 ("Diminué") || juce::String (ty.name) == juce::String::fromUTF8 ("Augmenté")) score -= 1.5f;
                    if (root == prevRoot) score -= 2.0f;
                    if (prevRoot >= 0)
                    {
                        const int mv = theory::mod12 (root - prevRoot);
                        if (mv == 5 || mv == 7) score += 0.8f;
                        else if (mv == 3 || mv == 4 || mv == 8 || mv == 9) score += 0.4f;
                    }
                    if (si == 0 && root == theory::mod12 (key)) score += 1.0f;
                    cands.push_back ({ score, root, ti });
                }
            }
            if (cands.empty()) continue;
            std::sort (cands.begin(), cands.end(), [] (const Cand& a, const Cand& b) { return a.score > b.score; });
            int pick = 0;
            if (variant > 0)   // « Autre idée » : pioche parmi les meilleurs choix
            {
                int n = 1; while (n < (int) cands.size() && n < 4 && cands[(size_t) n].score > cands.front().score - 3.5f) ++n;
                pick = rng.nextInt (n);
            }
            const int bestRoot = cands[(size_t) pick].root, bestType = cands[(size_t) pick].type;
    
            // voicing : sous la mélodie, ou au-dessus de la fondamentale que tu as posée
            std::vector<int> voiced;
            if (s.rootGiven)
            {
                for (int iv : types[(size_t) bestType].iv) if (iv != 0) voiced.push_back (s.givenRoot + 12 + iv);
                voiced.push_back (s.givenRoot + 12);
                for (auto& x : voiced) while (x > 79) x -= 12;
            }
            else
            {
                int melLow = 127; for (auto* n : s.ns) melLow = juce::jmin (melLow, n->pitch);
                const int top = melLow == 127 ? 67 : juce::jlimit (55, 76, melLow - 1);
                for (int iv : types[(size_t) bestType].iv)
                {
                    const int pc = theory::mod12 (bestRoot + iv);
                    int x = top - theory::mod12 (top - pc);
                    if (x < 48) x += 12;
                    voiced.push_back (x);
                }
            }
            std::sort (voiced.begin(), voiced.end());
            voiced.erase (std::unique (voiced.begin(), voiced.end()), voiced.end());
    
            Chord c;
            for (int iv : types[(size_t) bestType].iv) c.close.push_back (48 + bestRoot + iv);
            c.start = s.start;
            c.length = juce::jmax (0.5, s.end - s.start);
            relabel (c, key, scale);
            c.voiced = voiced;
            chords.push_back (c);
            prevRoot = bestRoot;
        }
    
        std::vector<Note> out = src;
        for (auto& c : chords)
        {
            for (int x : c.voiced) out.push_back ({ c.start, c.length, x, 0.62f, 0 });
            if (withBass) out.push_back ({ c.start, c.length, 36 + theory::mod12 (c.root), 0.75f, 1 });
        }
        res.chords = chords;
        res.notes = out;
        res.lengthBeats = total;
        return res;
    }
}
