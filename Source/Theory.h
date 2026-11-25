#pragma once

#include <juce_core/juce_core.h>
#include <vector>
#include <array>
#include <algorithm>
#include <cmath>
#include "I18n.h"

// Théorie musicale : gammes, accords, nommage, voicings.
namespace theory
{
    inline int mod12 (int x) { return ((x % 12) + 12) % 12; }

    //==========================================================================
    struct Scale { const char* name; std::vector<int> steps; };

    // Gammes du piano roll de FL Studio + quelques classiques en plus.
    inline const std::vector<Scale>& scales()
    {
        // Ordre alphabétique (comme dans les menus)
        static const std::vector<Scale> s {
            { "Altérée (super locrien)",       { 0, 1, 3, 4, 6, 8, 10 } },
            { "Arabe (double harmonique)",     { 0, 1, 4, 5, 7, 8, 11 } },
            { "Bebop dominante",               { 0, 2, 4, 5, 7, 9, 10, 11 } },
            { "Bebop majeure",                 { 0, 2, 4, 5, 7, 8, 9, 11 } },
            { "Blues",                         { 0, 3, 5, 6, 7, 10 } },
            { "Blues majeur",                  { 0, 2, 3, 4, 7, 9 } },
            { "Chinoise",                      { 0, 4, 6, 7, 11 } },
            { "Chromatique",                   { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } },
            { "Diminuée (demi-ton / ton)",     { 0, 1, 3, 4, 6, 7, 9, 10 } },
            { "Diminuée (ton / demi-ton)",     { 0, 2, 3, 5, 6, 8, 9, 11 } },
            { "Dorien",                        { 0, 2, 3, 5, 7, 9, 10 } },
            { "Énigmatique",                   { 0, 1, 4, 6, 8, 10, 11 } },
            { "Hirajoshi",                     { 0, 2, 3, 7, 8 } },
            { "Hongroise majeure",             { 0, 3, 4, 6, 7, 9, 10 } },
            { "Hongroise mineure",             { 0, 2, 3, 6, 7, 8, 11 } },
            { "Japonaise (In Sen)",            { 0, 1, 5, 7, 10 } },
            { "Locrien",                       { 0, 1, 3, 5, 6, 8, 10 } },
            { "Lydien",                        { 0, 2, 4, 6, 7, 9, 11 } },
            { "Lydien dominant",               { 0, 2, 4, 6, 7, 9, 10 } },
            { "Majeur (Ionien)",               { 0, 2, 4, 5, 7, 9, 11 } },
            { "Mineur harmonique",             { 0, 2, 3, 5, 7, 8, 11 } },
            { "Mineur mélodique",              { 0, 2, 3, 5, 7, 9, 11 } },
            { "Mineur naturel (Éolien)",       { 0, 2, 3, 5, 7, 8, 10 } },
            { "Mixolydien",                    { 0, 2, 4, 5, 7, 9, 10 } },
            { "Napolitaine majeure",           { 0, 1, 3, 5, 7, 9, 11 } },
            { "Napolitaine mineure",           { 0, 1, 3, 5, 7, 8, 11 } },
            { "Par tons",                      { 0, 2, 4, 6, 8, 10 } },
            { "Pentatonique majeure",          { 0, 2, 4, 7, 9 } },
            { "Pentatonique mineure",          { 0, 3, 5, 7, 10 } },
            { "Persane",                       { 0, 1, 4, 5, 6, 8, 11 } },
            { "Phrygien",                      { 0, 1, 3, 5, 7, 8, 10 } },
            { "Phrygien dominant (espagnol)",  { 0, 1, 4, 5, 7, 8, 10 } },
            { "Tzigane",                       { 0, 2, 3, 6, 7, 8, 10 } },
        };
        return s;
    }

    enum ScaleIndex { Dorian = 10, Lydian = 17, Major = 19, HarmonicMinor = 20, MelodicMinor = 21, Minor = 22,
                      Mixolydian = 23, Phrygian = 30 };
    inline juce::StringArray scaleNames()
    {
        juce::StringArray a;
        for (auto& s : scales()) a.add (fr (s.name));
        return a;
    }

    inline const char* const pcNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    inline juce::StringArray keyNames()
    {
        juce::StringArray a;
        for (auto* n : pcNames) a.add (n);
        return a;
    }

    inline const char* const pcNamesFlat[12] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
    inline const char* pcName (int pc, bool flats) { return flats ? pcNamesFlat[mod12 (pc)] : pcNames[mod12 (pc)]; }

    // Numérotation FL Studio : 60 = C5
    inline juce::String noteName (int midi, bool flats = false) { return juce::String (pcName (midi, flats)) + juce::String (midi / 12); }

    inline const Scale& scaleAt (int idx) { return scales()[(size_t) juce::jlimit (0, (int) scales().size() - 1, idx)]; }

    // Tonalités qui s'écrivent avec des bémols (Fa, Sib, Mib, Lab, Réb, Solb majeur et leurs relatives)
    inline bool prefersFlats (int key, int scaleIdx)
    {
        const auto& s = scaleAt (scaleIdx).steps;
        const bool has3 = std::find (s.begin(), s.end(), 3) != s.end();
        const bool has4 = std::find (s.begin(), s.end(), 4) != s.end();
        const int major = (has3 && ! has4) ? mod12 (key + 3) : mod12 (key);
        return major == 5 || major == 10 || major == 3 || major == 8 || major == 1 || major == 6;
    }

    inline bool inScale (int note, int key, const Scale& s)
    {
        const int pc = mod12 (note - key);
        return std::find (s.steps.begin(), s.steps.end(), pc) != s.steps.end();
    }

    inline int snapToScale (int note, int key, const Scale& s)
    {
        for (int d = 0; d < 12; ++d)
        {
            if (inScale (note - d, key, s)) return note - d;
            if (inScale (note + d, key, s)) return note + d;
        }
        return note;
    }

    // Gamme utilisée pour empiler des tierces (les gammes à 5/6/12 notes se rabattent sur majeur/mineur).
    inline const Scale& harmonyScale (int scaleIdx)
    {
        const auto& s = scaleAt (scaleIdx);
        const auto n = s.steps.size();
        if (n >= 7 && n <= 8)
            return s;
        const bool has3 = std::find (s.steps.begin(), s.steps.end(), 3) != s.steps.end();
        const bool has4 = std::find (s.steps.begin(), s.steps.end(), 4) != s.steps.end();
        return scales()[(has3 && ! has4) ? Minor : Major];
    }

    // Accord diatonique construit sur "note" (ramenée dans la gamme), "size" notes empilées en tierces.
    inline std::vector<int> diatonicChord (int note, int key, int scaleIdx, int size)
    {
        const auto& s = harmonyScale (scaleIdx);
        const int n = (int) s.steps.size();
        const int r = snapToScale (note, key, s);
        const int pc = mod12 (r - key);
        int idx = 0;
        for (int i = 0; i < n; ++i)
            if (s.steps[(size_t) i] == pc) idx = i;

        const int base = r - s.steps[(size_t) idx];
        std::vector<int> out;
        for (int i = 0; i < size; ++i)
        {
            const int step = idx + 2 * i;
            out.push_back (base + s.steps[(size_t) (step % n)] + 12 * (step / n));
        }

        // Notes "à éviter" : 9e mineure, et 11e juste au-dessus d'une tierce majeure
        if (out.size() >= 5)
        {
            const int root = out.front();
            const bool majorThird = out[1] - root == 4;
            out.erase (std::remove_if (out.begin() + 3, out.end(), [root, majorThird] (int x)
            {
                const int iv = x - root;
                return iv == 13 || (majorThird && iv == 17);
            }), out.end());
        }
        return out;
    }

    inline int degreeCount (int scaleIdx) { return (int) harmonyScale (scaleIdx).steps.size(); }

    // Accord sur le degré "degree" (0 = tonique) ; racine placée à partir de "octaveBase".
    inline std::vector<int> degreeChord (int degree, int key, int scaleIdx, int size, int octaveBase = 48)
    {
        const auto& s = harmonyScale (scaleIdx);
        const int n = (int) s.steps.size();
        const int d = ((degree % n) + n) % n;
        const int root = octaveBase + key + s.steps[(size_t) d] + 12 * (degree / n);
        return diatonicChord (root, key, scaleIdx, size);
    }

    //==========================================================================
    struct ChordType { const char* name; std::vector<int> iv; };

    inline const std::vector<ChordType>& chordTypes()
    {
        static const std::vector<ChordType> t {
            { "Majeur",         { 0, 4, 7 } },
            { "Mineur",         { 0, 3, 7 } },
            { "7",              { 0, 4, 7, 10 } },
            { "Maj7",           { 0, 4, 7, 11 } },
            { "m7",             { 0, 3, 7, 10 } },
            { "m7b5",           { 0, 3, 6, 10 } },
            { "Diminué",        { 0, 3, 6 } },
            { "dim7",           { 0, 3, 6, 9 } },
            { "Augmenté",       { 0, 4, 8 } },
            { "sus2",           { 0, 2, 7 } },
            { "sus4",           { 0, 5, 7 } },
            { "7sus4",          { 0, 5, 7, 10 } },
            { "add9",           { 0, 4, 7, 14 } },
            { "m(add9)",        { 0, 3, 7, 14 } },
            { "6",              { 0, 4, 7, 9 } },
            { "m6",             { 0, 3, 7, 9 } },
            { "9",              { 0, 4, 7, 10, 14 } },
            { "Maj9",           { 0, 4, 7, 11, 14 } },
            { "m9",             { 0, 3, 7, 10, 14 } },
            { "11",             { 0, 4, 7, 10, 14, 17 } },
            { "m11",            { 0, 3, 7, 10, 14, 17 } },
            { "13",             { 0, 4, 7, 10, 14, 21 } },
            { "Maj7#11",        { 0, 4, 7, 11, 18 } },
            { "Quinte (power)", { 0, 7 } },
            { "Octave",         { 0, 12 } },
            { "m(maj7)",        { 0, 3, 7, 11 } },
            { "7b9",            { 0, 4, 7, 10, 13 } },
            { "7#9",            { 0, 4, 7, 10, 15 } },
            { "7#11",           { 0, 4, 7, 10, 18 } },
            { "m9b5",           { 0, 3, 6, 10, 14 } },
            { "6/9",            { 0, 4, 7, 9, 14 } },
            { "Maj13",          { 0, 4, 7, 11, 14, 21 } },
            { "m13",            { 0, 3, 7, 10, 14, 21 } },
        };
        return t;
    }

    inline juce::StringArray chordTypeNames()
    {
        juce::StringArray a;
        for (auto& c : chordTypes()) a.add (fr (c.name));
        return a;
    }

    //==========================================================================
    // Nommage d'accord à partir des notes
    struct NamedChord { const char* symbol; std::vector<int> iv; };

    inline const std::vector<NamedChord>& namingTable()
    {
        static const std::vector<NamedChord> t {
            { "",        { 0, 4, 7 } },       { "m",       { 0, 3, 7 } },
            { "dim",     { 0, 3, 6 } },       { "aug",     { 0, 4, 8 } },
            { "sus2",    { 0, 2, 7 } },       { "sus4",    { 0, 5, 7 } },
            { "5",       { 0, 7 } },
            { "7",       { 0, 4, 7, 10 } },   { "maj7",    { 0, 4, 7, 11 } },
            { "m7",      { 0, 3, 7, 10 } },   { "m7b5",    { 0, 3, 6, 10 } },
            { "dim7",    { 0, 3, 6, 9 } },    { "mMaj7",   { 0, 3, 7, 11 } },
            { "maj7#5",  { 0, 4, 8, 11 } },   { "7sus4",   { 0, 5, 7, 10 } },
            { "7b5",     { 0, 4, 6, 10 } },   { "6",       { 0, 4, 7, 9 } },
            { "m6",      { 0, 3, 7, 9 } },    { "add9",    { 0, 2, 4, 7 } },
            { "m(add9)", { 0, 2, 3, 7 } },
            { "9",       { 0, 2, 4, 7, 10 } },  { "maj9",    { 0, 2, 4, 7, 11 } },
            { "m9",      { 0, 2, 3, 7, 10 } },  { "7b9",     { 0, 1, 4, 7, 10 } },
            { "m7b9",    { 0, 1, 3, 7, 10 } },  { "m9b5",    { 0, 2, 3, 6, 10 } },
            { "mMaj9",   { 0, 2, 3, 7, 11 } },  { "maj9#5",  { 0, 2, 4, 8, 11 } },
            { "7#9",     { 0, 3, 4, 7, 10 } },  { "6/9",     { 0, 2, 4, 7, 9 } },
            { "m6/9",    { 0, 2, 3, 7, 9 } },   { "dim(b9)", { 0, 1, 3, 6, 9 } },
            { "11",      { 0, 2, 4, 5, 7, 10 } },  { "m11",     { 0, 2, 3, 5, 7, 10 } },
            { "maj11",   { 0, 2, 4, 5, 7, 11 } },  { "maj7#11", { 0, 4, 6, 7, 11 } },
            { "maj9#11", { 0, 2, 4, 6, 7, 11 } },  { "m11b5",   { 0, 2, 3, 5, 6, 10 } },
            { "7b9b13",  { 0, 1, 4, 7, 8, 10 } },  { "m7b9b13", { 0, 1, 3, 7, 8, 10 } },
            { "13",      { 0, 2, 4, 7, 9, 10 } },  { "maj13",   { 0, 2, 4, 7, 9, 11 } },
            { "m13",     { 0, 2, 3, 7, 9, 10 } },
            { "7#11",    { 0, 4, 6, 7, 10 } },     { "7b13",    { 0, 4, 7, 8, 10 } },
            { "9#11",    { 0, 2, 4, 6, 7, 10 } },  { "13b9",    { 0, 1, 4, 7, 9, 10 } },
            { "7#9b13",  { 0, 3, 4, 8, 10 } },     { "7#5",     { 0, 4, 8, 10 } },
            { "9sus4",   { 0, 2, 5, 7, 10 } },     { "7b9",     { 0, 1, 4, 10 } },
            { "7#9",     { 0, 3, 4, 10 } },        { "m9b5",    { 0, 2, 3, 6, 10 } },
            { "6/9",     { 0, 2, 4, 9 } },         { "maj7",    { 0, 4, 11 } },
            { "9",       { 0, 2, 4, 10 } },        { "13",      { 0, 2, 4, 9, 10 } },
            { "13",      { 0, 2, 4, 5, 7, 9, 10 } }, { "maj13",   { 0, 2, 4, 5, 7, 9, 11 } },
            { "m13",     { 0, 2, 3, 5, 7, 9, 10 } }, { "maj13#11",{ 0, 2, 4, 6, 7, 9, 11 } },
        };
        return t;
    }

    inline int maskOf (const std::vector<int>& iv)
    {
        int m = 0;
        for (int i : iv) m |= 1 << mod12 (i);
        return m;
    }

    inline juce::String chordName (std::vector<int> notes, bool flats = false)
    {
        if (notes.empty()) return {};
        std::sort (notes.begin(), notes.end());

        int setMask = 0;
        for (int n : notes) setMask |= 1 << mod12 (n);

        const int bass = mod12 (notes.front());
        juce::String best;
        int bestScore = -1000;

        for (int r = 0; r < 12; ++r)
        {
            if ((setMask & (1 << r)) == 0) continue;
            int rel = 0;
            for (int i = 0; i < 12; ++i)
                if (setMask & (1 << i)) rel |= 1 << mod12 (i - r);

            for (auto& c : namingTable())
            {
                const int cm = maskOf (c.iv);
                const int match   = juce::countNumberOfBits ((uint32_t) (cm & rel));
                const int missing = juce::countNumberOfBits ((uint32_t) (cm & ~rel));
                const int extra   = juce::countNumberOfBits ((uint32_t) (rel & ~cm));
                int score = match * 3 - missing * 4 - extra * 3 + (r == bass ? 2 : 0);
                if (score > bestScore)
                {
                    bestScore = score;
                    best = juce::String (pcName (r, flats)) + c.symbol;
                    if (r != bass) best << "/" << pcName (bass, flats);
                }
            }
        }
        return best;
    }

    inline juce::String roman (int degree, const std::vector<int>& closeChord)
    {
        static const char* r[] = { "I", "II", "III", "IV", "V", "VI", "VII", "VIII" };
        juce::String s (r[juce::jlimit (0, 7, degree)]);
        if (closeChord.size() >= 3)
        {
            const int third = closeChord[1] - closeChord[0];
            const int fifth = closeChord[2] - closeChord[0];
            if (third == 3) s = s.toLowerCase();
            const bool halfDim = closeChord.size() >= 4 && closeChord[3] - closeChord[0] == 10;
            if (third == 3 && fifth == 6) s << juce::String::fromUTF8 (halfDim ? "ø" : "°");
            if (third == 4 && fifth == 8) s << "+";
        }
        return s;
    }

    //==========================================================================
    enum Voicing { Close = 0, Open, Drop2, BassPlus };

    inline juce::StringArray voicingNames()
    {
        return { fr ("Serré"), fr ("Ouvert"), "Drop 2", fr ("Basse + accord") };
    }

    inline std::vector<int> applyVoicing (std::vector<int> notes, int voicing, int inversion)
    {
        std::sort (notes.begin(), notes.end());
        const int n = (int) notes.size();
        if (n == 0) return notes;
        const int root = notes.front();

        for (int i = 0; i < inversion % n; ++i)
        {
            notes.front() += 12;
            std::sort (notes.begin(), notes.end());
        }

        if (voicing == Open && n >= 3)       { notes[1] += 12; std::sort (notes.begin(), notes.end()); }
        else if (voicing == Drop2 && n >= 3) { notes[(size_t) n - 2] -= 12; std::sort (notes.begin(), notes.end()); }
        else if (voicing == BassPlus)
        {
            int b = root;
            while (b > notes.front() - 12) b -= 12;
            notes.insert (notes.begin(), b);
        }
        return notes;
    }

    // Choisit renversement + octave pour bouger le moins possible depuis l'accord précédent.
    // target : hauteur moyenne visée (ancre) ; sans ancre la conduite des voix dérive vers le grave ou l'aigu
    inline std::vector<int> voiceLead (const std::vector<int>& close, const std::vector<int>& prev, int low = 48, int high = 81, double target = -1.0)
    {
        const int n = (int) close.size();
        if (n == 0) return close;
        if (target < 0.0) target = (low + high) * 0.5 - 3.0;

        std::vector<int> best;
        double bestCost = 1e9;
        const double prevMean = prev.empty() ? (low + high) * 0.5 - 4.0 : [&] { double m = 0; for (int p : prev) m += p; return m / (double) prev.size(); }();

        for (int inv = 0; inv < n; ++inv)
        {
            for (int oct = -3; oct <= 3; ++oct)
            {
                std::vector<int> c = close;
                for (int i = 0; i < inv; ++i) { c.front() += 12; std::sort (c.begin(), c.end()); }
                for (auto& x : c) x += 12 * oct;
                if (c.front() < low || c.back() > high) continue;

                double cost = 0;
                if (! prev.empty() && prev.size() == c.size())
                    for (size_t i = 0; i < c.size(); ++i) cost += std::abs (c[i] - prev[i]);
                else
                {
                    double m = 0; for (int x : c) m += x;
                    cost = std::abs (m / n - prevMean) * n;
                }
                {
                    double m = 0; for (int x : c) m += x;
                    cost += std::abs (m / n - target) * n * 1.2;            // rester près du registre visé
                    cost += juce::jmax (0, (c.back() - c.front()) - 19) * 2.0;   // pas d'accords trop étalés
                }
                if (cost < bestCost) { bestCost = cost; best = c; }
            }
        }
        return best.empty() ? close : best;
    }

    //==========================================================================
    // Couleurs "jazzy" : extensions et altérations, y compris hors gamme.
    // amount 0..1 = probabilité/importance des ajouts.
    // Variété : sus2 / sus4, add9, add11, 6/9, quartes empilées, accord "shell".
    // Garde la fondamentale ; les accords obtenus restent consonants dans la plupart des styles.
    inline std::vector<int> varyChord (std::vector<int> notes, juce::Random& rng)
    {
        if (notes.size() < 3) return notes;
        std::sort (notes.begin(), notes.end());
        const int root = notes.front();
        auto has = [&] (int iv) { for (int n : notes) if (mod12 (n - root) == mod12 (iv)) return true; return false; };
        auto add = [&] (int iv) { if (! has (iv)) notes.push_back (root + iv); };
        auto drop = [&] (int iv) { notes.erase (std::remove_if (notes.begin() + 1, notes.end(),
                                                                 [&] (int n) { return mod12 (n - root) == mod12 (iv); }), notes.end()); };
        const bool maj3 = has (4), min3 = has (3) && ! maj3, perfect5 = has (7), sev = has (10) || has (11);
        switch (rng.nextInt (7))
        {
            case 0: if (perfect5) { drop (3); drop (4); add (2); } break;                  // sus2
            case 1: if (perfect5) { drop (3); drop (4); add (5); } break;                  // sus4
            case 2: add (14); break;                                                         // add9
            case 3: if (min3) { add (17); } else add (14); break;                            // m(add11)
            case 4: if (maj3 && ! sev) { add (9); add (14); } else add (14); break;          // 6/9
            case 5: if (perfect5)                                                            // quartes empilées (son moderne / modal)
                    {
                        const bool minorish = min3;
                        notes = { root, root + 5, root + 10, root + (minorish ? 15 : 14) };
                        if (rng.nextBool()) notes.push_back (root + 19);
                    }
                    break;
            default: if (sev) { drop (7); drop (14); } break;                                // "shell" : fondamentale, tierce, septième
        }
        std::sort (notes.begin(), notes.end());
        notes.erase (std::unique (notes.begin(), notes.end()), notes.end());
        return notes;
    }

    inline std::vector<int> colourChord (std::vector<int> notes, float amount, juce::Random& rng)
    {
        if (notes.size() < 3 || amount <= 0.001f) return notes;
        std::sort (notes.begin(), notes.end());
        const int root = notes.front();

        auto has = [&] (int iv) { for (int n : notes) if (mod12 (n - root) == mod12 (iv)) return true; return false; };
        auto add = [&] (int iv) { if (! has (iv)) notes.push_back (root + iv); };
        auto drop = [&] (int iv) { notes.erase (std::remove_if (notes.begin() + 1, notes.end(),
                                                                 [&] (int n) { return mod12 (n - root) == mod12 (iv); }), notes.end()); };

        const bool maj3 = has (4), min3 = has (3) && ! has (4), b7 = has (10), dim5 = has (6) && ! has (7);
        const bool dominant = maj3 && b7;

        if (dominant && rng.nextFloat() < amount * 0.8f)
        {
            // altérations typiques jazz / funk / gospel
            switch (rng.nextInt (5))
            {
                case 0:  drop (14); add (13); break;           // b9
                case 1:  drop (14); add (15); break;           // #9 (accord "Hendrix")
                case 2:  add (14); add (18); break;            // 9 #11
                case 3:  add (20); drop (7); break;            // b13
                default: add (14); add (21); break;            // 13
            }
        }
        else if (maj3 && ! b7 && rng.nextFloat() < amount * 0.6f)
        {
            if (rng.nextBool()) { add (11); add (14); }        // maj9
            else                { add (14); add (21); }        // 6/9
            if (rng.nextFloat() < amount * 0.3f) add (18);     // #11 (lydien)
        }
        else if (min3 && ! dim5 && rng.nextFloat() < amount * 0.6f)
        {
            add (10); add (14);                                // m9
            if (rng.nextFloat() < amount * 0.5f) add (17);     // m11
        }
        else if (dim5 && rng.nextFloat() < amount * 0.4f)
        {
            add (10); add (14);                                // m9b5
        }

        std::sort (notes.begin(), notes.end());
        notes.erase (std::unique (notes.begin(), notes.end()), notes.end());
        if (notes.size() > 7) notes.resize (7);
        return notes;
    }

    // Accord construit à partir d'une fondamentale et d'intervalles
    inline std::vector<int> chordFrom (int root, std::initializer_list<int> iv)
    {
        std::vector<int> v;
        for (int i : iv) v.push_back (root + i);
        return v;
    }

    // Chiffrage romain d'un accord quelconque (y compris hors gamme) par rapport à la tonalité
    inline juce::String romanChromatic (int root, int key, const std::vector<int>& close)
    {
        static const char* r[12] = { "I", "bII", "II", "bIII", "III", "IV", "#IV", "V", "bVI", "VI", "bVII", "VII" };
        juce::String s (r[mod12 (root - key)]);
        if (close.size() >= 3)
        {
            const int third = mod12 (close[1] - close[0]);
            if (third == 3) s = s.replace ("I", "i").replace ("V", "v");
        }
        return s;
    }
}
