#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "Theory.h"
#include "ArpCore.h"
#include "Moods.h"
#include <set>

// Génère une progression d'accords (+ basse, + arpège) selon une ambiance, et l'exporte en .mid.
// Inclut des accords hors gamme qui sonnent bien : dominantes secondaires, substitution tritonique,
// approches chromatiques, accords empruntés, altérations (b9, #9, #11, b13).
namespace gen
{
    constexpr int ppq = 960;

    enum Register { RegAuto = 0, RegLow, RegMid, RegHigh };

    inline juce::StringArray registerNames() { return { "Auto", fr ("Grave"), fr ("Médium"), fr ("Aigu") }; }

    struct Settings
    {
        int key = 0, scaleIdx = theory::Minor, mood = 0, bars = 4, richness = 1, reg = RegAuto;
        float spice = 0.5f;   // 0 = tout dans la gamme, 1 = très coloré / hors gamme
        float variety = 0.5f; // 0 = accords simples, 1 = sus, add9, quartes, renversements, accords de passage
        const std::vector<std::vector<int>>* liked = nullptr;      // degrés des progressions aimées
        const std::vector<std::vector<int>>* disliked = nullptr;
        float tasteAmount = 0.0f;                                   // chance de partir d'une progression aimée
        bool chords = true, bass = true, arp = false;
        bool noOverlap = false;   // chaque accord s'arrête quand le suivant commence (cut)
        const juce::StringArray* avoid = nullptr;   // progressions proposées récemment (signatures) : on en prend d'autres
        juce::uint32 seed = 1;
        arp::Settings arpSettings;
    };

    struct Chord
    {
        int degree = 0, root = 0;
        std::vector<int> close, voiced;   // close = notes en position fondamentale (root + intervalles)
        juce::String name, roman;
        double start = 0, length = 4;
        bool outside = false;   // accord hors gamme
        int inversion = -1;     // -1 = automatique (conduite des voix), sinon renversement imposé
        int octave = 0;         // décalage d'octave choisi par l'utilisateur
    };

    // Progression éditable (accord par accord)
    struct Progression
    {
        std::vector<Chord> chords;
        double lengthBeats = 16;
        int reg = RegMid;       // registre effectif
        juce::String signature; // d'où vient la progression (pour ne pas la reproposer tout de suite)
    };

    struct Result
    {
        std::vector<Chord> chords;
        juce::MidiMessageSequence chordSeq, bassSeq, arpSeq;
        double lengthBeats = 16;
    };

    // hauteur moyenne visée pour les accords (60 = C5 dans FL) : grave, auto, aigu
    inline double registerTarget (int reg) { return reg == RegLow ? 53.0 : (reg == RegHigh ? 70.0 : 62.0); }

    inline void registerRange (int reg, int& low, int& high, int& octBase)
    {
        low  = reg == RegLow ? 38 : (reg == RegHigh ? 57 : 48);
        high = low + 31;
        octBase = reg == RegLow ? 36 : (reg == RegHigh ? 60 : 48);
    }

    // Recalcule nom, chiffrage et "hors gamme" d'un accord (après une modification)
    inline void relabel (Chord& c, int key, int scaleIdx)
    {
        std::vector<int> sorted = c.close;
        std::sort (sorted.begin(), sorted.end());
        c.root = sorted.front();
        const bool flats = theory::prefersFlats (key, scaleIdx);
        const int rel = theory::mod12 (c.root - key);
        bool inside = true;
        for (int nn : sorted) if (! theory::inScale (nn, key, theory::scaleAt (scaleIdx))) inside = false;
        c.outside = ! inside;
        c.name = theory::chordName (sorted, flats || (c.outside && (rel == 1 || rel == 3 || rel == 8 || rel == 10)));
        c.roman = theory::romanChromatic (c.root, key, sorted);
    }

    // accord ramené dans la gamme (remplacé par l'accord de la gamme le plus proche, même taille) ; true si modifié
    inline bool conformToScale (Chord& c, int key, int scaleIdx)
    {
        if (c.close.empty()) return false;
        const auto& hs = theory::harmonyScale (scaleIdx);
        bool inside = true;
        for (int n : c.close) if (! theory::inScale (n, key, hs)) inside = false;
        if (inside) return false;
        const int size = juce::jlimit (3, 6, (int) c.close.size());
        const int root = *std::min_element (c.close.begin(), c.close.end());
        c.close = theory::diatonicChord (root, key, scaleIdx, size);
        relabel (c, key, scaleIdx);
        const int pc = theory::mod12 (c.root - key);
        for (int d = 0; d < (int) hs.steps.size(); ++d)
            if (hs.steps[(size_t) d] == pc) { c.degree = d; c.roman = theory::roman (d, c.close); }
        c.outside = false;
        return true;
    }

    inline void addNote (juce::MidiMessageSequence& seq, int note, double startBeat, double lenBeats, float vel, double maxBeat)
    {
        if (note < 0 || note > 127) return;
        startBeat = juce::jmax (0.0, startBeat);
        const double end = juce::jmin (startBeat + juce::jmax (0.05, lenBeats), maxBeat - 0.01);
        if (end <= startBeat) return;
        seq.addEvent (juce::MidiMessage::noteOn (1, note, juce::jlimit (0.05f, 1.0f, vel)).withTimeStamp (startBeat * ppq));
        seq.addEvent (juce::MidiMessage::noteOff (1, note).withTimeStamp (end * ppq));
    }

    inline bool isMajorish (int scaleIdx)
    {
        const auto& s = theory::harmonyScale (scaleIdx).steps;
        return std::find (s.begin(), s.end(), 4) != s.end();
    }

    //==========================================================================
    // Chiffrage écrit (« bVImaj7 », « iv9 », « V7#9 », « iiø ») -> intervalles + demi-tons depuis la tonique.
    // false si le mot n'est pas compris.
    inline bool parseChordSymbol (const juce::String& tok0, int& rootSemis, std::vector<int>& iv, bool& minorQuality)
    {
        auto tok = tok0.trim();
        int i = 0, acc = 0;
        while (i < tok.length() && (tok[i] == 'b' || tok[i] == '#')) { acc += tok[i] == 'b' ? -1 : 1; ++i; }
        int j = i;
        while (j < tok.length() && juce::String ("IViv").containsChar (tok[j])) ++j;
        if (j == i) return false;
        const auto rom = tok.substring (i, j);
        const auto up = rom.toUpperCase();
        static const char* romans[] = { "I", "II", "III", "IV", "V", "VI", "VII" };
        static const int steps[] = { 0, 2, 4, 5, 7, 9, 11 };
        int deg = -1;
        for (int k = 0; k < 7; ++k) if (up == romans[k]) deg = k;
        if (deg < 0) return false;
        minorQuality = rom[0] == 'i' || rom[0] == 'v';
        rootSemis = steps[deg] + acc;
        auto suf = tok.substring (j);
        const bool mi = minorQuality;
        const std::vector<int> triad = mi ? std::vector<int> { 0, 3, 7 } : std::vector<int> { 0, 4, 7 };
        auto with = [&] (std::initializer_list<int> extra) { auto v = triad; for (int x : extra) v.push_back (x); return v; };
        const auto deg7 = juce::String::fromUTF8 ("\xc2\xb0"), half = juce::String::fromUTF8 ("\xc3\xb8");   // ° ø
        if (suf.isEmpty())                       iv = triad;
        else if (suf == deg7 + "7" || suf == "dim7") { iv = { 0, 3, 6, 9 }; minorQuality = true; }
        else if (suf == deg7 || suf == "dim")    { iv = { 0, 3, 6 }; minorQuality = true; }
        else if (suf == half || suf == "m7b5")   { iv = { 0, 3, 6, 10 }; minorQuality = true; }
        else if (suf == "aug")                   iv = { 0, 4, 8 };
        else if (suf == "sus2")                  iv = { 0, 2, 7 };
        else if (suf == "sus4")                  iv = { 0, 5, 7 };
        else if (suf == "7sus4")                 iv = { 0, 5, 7, 10 };
        else if (suf == "add9")                  iv = with ({ 14 });
        else if (suf == "6")                     iv = with ({ 9 });
        else if (suf == "69")                    iv = with ({ 9, 14 });
        else if (suf == "7")                     iv = with ({ 10 });
        else if (suf == "maj7" || suf == "(maj7)") iv = with ({ 11 });
        else if (suf == "9")                     iv = with ({ 10, 14 });
        else if (suf == "maj9")                  iv = with ({ 11, 14 });
        else if (suf == "11")                    iv = mi ? std::vector<int> { 0, 3, 7, 10, 14, 17 } : std::vector<int> { 0, 5, 7, 10, 14 };
        else if (suf == "13")                    iv = with ({ 10, 14, 21 });
        else if (suf == "7b9")                   iv = with ({ 10, 13 });
        else if (suf == "7#9")                   iv = with ({ 10, 15 });
        else if (suf == "7#11")                  iv = with ({ 10, 18 });
        else if (suf == "maj7#11")               iv = with ({ 11, 18 });
        else return false;
        return true;
    }

    // tonalité d'une progression écrite : +1 majeure (I…), -1 mineure (i…), 0 sans tonique
    inline int templateMode (const char* t)
    {
        juce::StringArray bars; bars.addTokens (juce::String::fromUTF8 (t), " +", ""); bars.removeEmptyStrings();
        for (auto& tok : bars)
        {
            int r = 0; std::vector<int> iv; bool mi = false;
            if (parseChordSymbol (tok, r, iv, mi) && r == 0 && ! tok.startsWithChar ('b') && ! tok.startsWithChar ('#'))
                return mi ? -1 : 1;
        }
        return 0;
    }

    // Progression tirée de la banque écrite de l'ambiance (et variée). false si rien d'utilisable.
    inline bool templateProgression (const Settings& s, const moods::Mood& m, int octBase, int bars, juce::Random& rng,
                                     std::vector<Chord>& out, juce::String& signature)
    {
        if (m.templates.empty()) return false;
        const int wanted = isMajorish (s.scaleIdx) ? 1 : -1;
        std::vector<int> ok, fresh;
        for (int i = 0; i < (int) m.templates.size(); ++i)
        {
            const int md = templateMode (m.templates[(size_t) i]);
            if (md == 0 || md == wanted) ok.push_back (i);
        }
        if (ok.empty()) for (int i = 0; i < (int) m.templates.size(); ++i) ok.push_back (i);
        for (int i : ok)
            if (s.avoid == nullptr || ! s.avoid->contains (juce::String (m.name) + ":T" + juce::String (i))) fresh.push_back (i);
        const auto& pool = fresh.empty() ? ok : fresh;
        const int pick = pool[(size_t) rng.nextInt ((int) pool.size())];
        signature = juce::String (m.name) + ":T" + juce::String (pick);

        auto barsOf = [] (const char* t) { juce::StringArray a; a.addTokens (juce::String::fromUTF8 (t), " ", ""); a.removeEmptyStrings(); return a; };
        juce::StringArray seq = barsOf (m.templates[(size_t) pick]);
        if (seq.isEmpty()) return false;
        // boucle qui démarre ailleurs (même couleur, autre départ)
        if (seq.size() == 4 && rng.nextFloat() < 0.18f) { const int rot = 1 + rng.nextInt (3); juce::StringArray r2; for (int k = 0; k < 4; ++k) r2.add (seq[(k + rot) % 4]); seq = r2; }
        // 8 mesures : la 2e moitié est une autre progression du même style (ou la même, variée)
        while (seq.size() < bars)
        {
            if (rng.nextFloat() < 0.55f && pool.size() > 1)
            {
                int other = pick;
                for (int t = 0; t < 6 && other == pick; ++t) other = pool[(size_t) rng.nextInt ((int) pool.size())];
                seq.addArray (barsOf (m.templates[(size_t) other]));
            }
            else
            {
                auto copy = barsOf (m.templates[(size_t) pick]);
                if (copy.size() >= 4) copy.set (3, copy[(1 + rng.nextInt (3)) % copy.size()]);   // fin de phrase différente
                seq.addArray (copy);
            }
        }
        while (seq.size() > bars) seq.remove (seq.size() - 1);

        const float v = juce::jlimit (0.0f, 1.0f, s.variety);
        for (int b = 0; b < bars; ++b)
        {
            juce::StringArray parts; parts.addTokens (seq[b], "+", ""); parts.removeEmptyStrings();
            if (parts.isEmpty()) continue;
            const double len = 4.0 / parts.size();
            for (int p = 0; p < parts.size(); ++p)
            {
                int semis = 0; std::vector<int> iv; bool mi = false;
                if (! parseChordSymbol (parts[p], semis, iv, mi)) { iv = { 0, 3, 7 }; semis = 0; }
                // richesse : 0 = accords plus simples, 2 = on ajoute la 9e aux accords nus
                if (s.richness <= 0 && iv.size() > 4) iv.resize (4);
                if (s.richness >= 2 && iv.size() <= 4 && std::find (iv.begin(), iv.end(), 14) == iv.end() && iv[1] != 2 && iv[1] != 5) iv.push_back (14);
                Chord c;
                c.start = b * 4.0 + p * len;
                c.length = len;
                const int root = octBase + s.key + ((semis % 12) + 12) % 12;
                for (int x : iv) c.close.push_back (root + x);
                if (rng.nextFloat() < v * 0.25f) c.close = theory::varyChord (c.close, rng);
                if (rng.nextFloat() < v * 0.3f) c.inversion = 1 + rng.nextInt (2);
                c.root = c.close.front();
                // degré dans la gamme (sinon : accord hors gamme)
                const auto& hs = theory::harmonyScale (s.scaleIdx);
                const int pc = theory::mod12 (c.root - s.key);
                c.outside = true;
                for (int d = 0; d < (int) hs.steps.size(); ++d) if (hs.steps[(size_t) d] == pc) { c.degree = d; c.outside = false; }
                if (! c.outside) for (int nn : c.close) if (! theory::inScale (nn, s.key, hs)) c.outside = true;
                out.push_back (c);
            }
        }
        return ! out.empty();
    }

    // à quel point deux progressions sont différentes (0 = identiques, 1 = aucun accord en commun, mesure par mesure)
    inline float difference (const std::vector<Chord>& a, const std::vector<Chord>& b, double lengthBeats)
    {
        auto pcsAt = [] (const std::vector<Chord>& v, double t)
        {
            int mask = 0, root = -1;
            for (auto& c : v) if (t >= c.start - 1.0e-6 && t < c.start + c.length - 1.0e-6) { for (int x : c.close) mask |= 1 << theory::mod12 (x); root = c.close.empty() ? -1 : theory::mod12 (c.close.front()); }
            return std::make_pair (mask, root);
        };
        int diff = 0, n = 0;
        for (double t = 0.0; t < lengthBeats - 1.0e-6; t += 2.0, ++n)
        {
            const auto x = pcsAt (a, t), y = pcsAt (b, t);
            const float common = (float) juce::countNumberOfBits ((uint32_t) (x.first & y.first)) / (float) juce::jmax (1, juce::countNumberOfBits ((uint32_t) (x.first | y.first)));
            if (x.second != y.second || common < 0.6f) ++diff;
        }
        return n > 0 ? (float) diff / (float) n : 1.0f;
    }

    inline Progression generateProgression (const Settings& s)
    {
        Progression r;
        const auto& moodList = moods::list();
        const int moodIdx = juce::jlimit (0, (int) moodList.size() - 1, s.mood);
        const auto& m = moodList[(size_t) moodIdx];
        const auto col = moods::colourFor (moodIdx);
        juce::Random rng ((juce::int64) s.seed);

        const float spiceMul = juce::jlimit (0.0f, 2.0f, s.spice * 2.0f);
        auto chance = [&] (float base) { return rng.nextFloat() < juce::jlimit (0.0f, 0.95f, base * spiceMul); };

        const int bars = s.bars <= 4 ? 4 : 8;
        r.lengthBeats = bars * 4.0;
        const int n = theory::degreeCount (s.scaleIdx);

        // --- registre (grave / médium / aigu), Auto = au hasard, plus souvent médium
        int reg = s.reg;
        if (reg == RegAuto) reg = RegMid;   // auto = médium : plus de progressions entières tirées au hasard dans le grave ou l'aigu
        r.reg = reg;
        int low, high, octBase;
        registerRange (reg, low, high, octBase);
        juce::ignoreUnused (low, high);

        // --- la plupart du temps : une progression de la banque écrite du style (variée), le reste : générateur libre
        const bool tasteFirst = s.liked != nullptr && ! s.liked->empty() && s.tasteAmount > 0.0f && rng.nextFloat() < s.tasteAmount;
        if (! tasteFirst && rng.nextFloat() < m.templateShare)
        {
            std::vector<Chord> tpl;
            if (templateProgression (s, m, octBase, bars, rng, tpl, r.signature))
            {
                // accords de passage / dominantes en fin de mesure, comme pour le générateur libre (un peu moins souvent)
                std::vector<Chord> all;
                for (size_t i = 0; i < tpl.size(); ++i)
                {
                    auto c = tpl[i];
                    const auto& next = tpl[(i + 1) % tpl.size()];
                    if (c.length >= 4.0 - 1.0e-6 && chance (col.split * 0.45f * (0.6f + s.variety)))
                    {
                        const double splitAt = rng.nextBool() ? 2.0 : 3.0;
                        Chord a;
                        a.start = c.start + splitAt; a.length = 4.0 - splitAt; c.length = splitAt;
                        const int target = next.root;
                        const float x = rng.nextFloat() * (col.secDom + col.tritone + col.chromatic + 0.25f);
                        if (x < col.secDom)                                   { a.close = theory::chordFrom (target + 7, { 0, 4, 7, 10 }); a.roman = "V7/" + theory::romanChromatic (target, s.key, next.close); }
                        else if (x < col.secDom + col.tritone)                { a.close = theory::chordFrom (target + 1, { 0, 4, 7, 10, 14 }); a.roman = "subV/" + theory::romanChromatic (target, s.key, next.close); }
                        else if (x < col.secDom + col.tritone + col.chromatic) { const int dir = rng.nextBool() ? 1 : -1; for (int nn : next.close) a.close.push_back (nn + dir); a.roman = juce::String::fromUTF8 ("chrom."); }
                        else                                                  { a.close = theory::chordFrom (c.root, { 0, 5, 7, 10 }); a.degree = c.degree; }
                        a.outside = true;
                        a.root = a.close.front();
                        all.push_back (c); all.push_back (a);
                    }
                    else all.push_back (c);
                }
                const bool flatsT = theory::prefersFlats (s.key, s.scaleIdx);
                const int nD = theory::degreeCount (s.scaleIdx);
                for (auto& c : all)
                {
                    std::vector<int> sorted = c.close;
                    std::sort (sorted.begin(), sorted.end());
                    c.close = sorted;
                    c.root = sorted.front();
                    const int rel = theory::mod12 (c.root - s.key);
                    c.name = theory::chordName (sorted, flatsT || (c.outside && (rel == 1 || rel == 3 || rel == 8 || rel == 10)));
                    if (c.roman.isEmpty())
                        c.roman = c.outside ? theory::romanChromatic (c.root, s.key, sorted) : theory::roman (((c.degree % nD) + nD) % nD, sorted);
                }
                r.chords = all;
                return r;
            }
        }

        // --- degrés : modèle de l'ambiance + substitutions (utilise plus de notes de la gamme)
        const auto& first = m.progs[(size_t) rng.nextInt ((int) m.progs.size())];
        std::vector<int> degrees (first.begin(), first.end());
        if (bars == 8)
        {
            const auto& second = m.progs[(size_t) rng.nextInt ((int) m.progs.size())];
            degrees.insert (degrees.end(), second.begin(), second.end());
        }
        // --- goûts de l'utilisateur : on part souvent d'une progression aimée (légèrement variée),
        //     et on évite celles qu'il a rejetées
        if (s.liked != nullptr && ! s.liked->empty() && rng.nextFloat() < s.tasteAmount)
        {
            const auto& pick = (*s.liked)[(size_t) rng.nextInt ((int) s.liked->size())];
            if (! pick.empty())
            {
                degrees.clear();
                while ((int) degrees.size() < bars) for (int d : pick) if ((int) degrees.size() < bars) degrees.push_back (d);
                for (size_t i = 1; i < degrees.size(); ++i)
                    if (rng.nextFloat() < 0.18f) degrees[i] = (degrees[i] + (rng.nextBool() ? 2 : 5)) % 7;   // petite variation (tierce / quarte)
            }
        }
        if (s.disliked != nullptr)
            for (int attempt = 0; attempt < 6; ++attempt)
            {
                bool bad = false;
                for (auto& d : *s.disliked) if (d.size() >= 4 && std::equal (d.begin(), d.begin() + 4, degrees.begin())) bad = true;
                if (! bad) break;
                const auto& alt = m.progs[(size_t) rng.nextInt ((int) m.progs.size())];
                for (size_t i = 0; i < degrees.size(); ++i) degrees[i] = alt[i % alt.size()];
            }
        static const int subs[7][2] = { { 5, 2 }, { 3, 3 }, { 0, 4 }, { 1, 1 }, { 6, 2 }, { 0, 3 }, { 4, 1 } };
        for (size_t i = 1; i < degrees.size(); ++i)
            if (chance (col.subst * 0.6f) && n == 7)
                degrees[i] = subs[(size_t) theory::mod12 (degrees[i]) % 7][rng.nextInt (2)];

        const int ext = juce::jlimit (3, 7, m.ext + (s.richness - 1));
        const bool major = isMajorish (s.scaleIdx);

        // --- éviter les accords qui se ressemblent trop (même degré, ou presque les mêmes notes)
        {
            auto pcs = [&] (int deg) { int mask = 0; for (int x : theory::degreeChord (deg, s.key, s.scaleIdx, ext, octBase)) mask |= 1 << theory::mod12 (x); return mask; };
            auto similarity = [&] (int a, int b)
            {
                const int ma = pcs (a), mb = pcs (b);
                return (float) juce::countNumberOfBits ((uint32_t) (ma & mb)) / (float) juce::jmax (1, juce::countNumberOfBits ((uint32_t) (ma | mb)));
            };
            for (size_t i = 1; i < degrees.size(); ++i)
            {
                const int prev = ((degrees[i - 1] % n) + n) % n;
                int cur = ((degrees[i] % n) + n) % n;
                if (cur == prev || similarity (cur, prev) > 0.6f)
                {
                    int best = cur; float bestSim = 2.0f;
                    for (int tries = 0; tries < 12; ++tries)
                    {
                        const int cand = rng.nextInt (n);
                        if (cand == prev || (i + 1 < degrees.size() && cand == ((degrees[i + 1] % n) + n) % n)) continue;
                        const float sim = similarity (cand, prev) + (i >= 2 && cand == ((degrees[i - 2] % n) + n) % n ? 0.3f : 0.0f);
                        if (sim < bestSim) { bestSim = sim; best = cand; }
                    }
                    degrees[i] = best;
                }
            }
            // au moins 3 couleurs différentes sur 4 accords
            for (size_t blk = 0; blk + 4 <= degrees.size(); blk += 4)
            {
                std::set<int> d;
                for (size_t k = 0; k < 4; ++k) d.insert (((degrees[blk + k] % n) + n) % n);
                if (d.size() < 3)
                    for (int cand : { 3, 4, 5, 1, 2, 6 })
                        if (d.count (cand % n) == 0 && cand % n != ((degrees[blk + 1] % n) + n) % n && cand % n != ((degrees[blk + 3] % n) + n) % n)
                        { degrees[blk + 2] = cand % n; break; }
            }
        }

        // --- accords principaux (1 par mesure)
        std::vector<Chord> mains;
        for (size_t i = 0; i < degrees.size(); ++i)
        {
            Chord c;
            c.degree = degrees[i];
            c.start = (double) i * 4.0;
            c.length = 4.0;
            c.close = theory::degreeChord (c.degree, s.key, s.scaleIdx, ext, octBase);
            c.root = c.close.front();

            // accord emprunté (couleur hors gamme)
            if (i > 0 && chance (col.borrow * 0.5f))
            {
                const int k = octBase + s.key;
                std::vector<int> b;
                if (major)
                {
                    switch (rng.nextInt (4))
                    {
                        case 0:  b = theory::chordFrom (k + 5,  { 0, 3, 7, 10 }); break;      // iv m7
                        case 1:  b = theory::chordFrom (k + 8,  { 0, 4, 7, 11 }); break;      // bVImaj7
                        case 2:  b = theory::chordFrom (k + 10, { 0, 4, 7, 10, 14 }); break;  // bVII9
                        default: b = theory::chordFrom (k + 3,  { 0, 4, 7, 11 }); break;      // bIIImaj7
                    }
                }
                else
                {
                    switch (rng.nextInt (4))
                    {
                        case 0:  b = theory::chordFrom (k + 5, { 0, 4, 7, 10, 14 }); break;   // IV9 (dorien)
                        case 1:  b = theory::chordFrom (k + 7, { 0, 4, 7, 10, 13 }); break;   // V7b9
                        case 2:  b = theory::chordFrom (k + 1, { 0, 4, 7, 11 }); break;       // bIImaj7 (napolitain)
                        default: b = theory::chordFrom (k + 9, { 0, 3, 6, 10 }); break;       // vi ø
                    }
                }
                c.close = b;
                c.root = b.front();
                c.outside = true;
            }

            c.close = theory::colourChord (c.close, juce::jlimit (0.0f, 1.0f, col.spice * spiceMul * 0.7f), rng);
            mains.push_back (c);
        }

        // --- variété : sus, add9, quartes, 6/9, renversements imposés (plus la variété est haute, plus ça bouge)
        {
            const float v = juce::jlimit (0.0f, 1.0f, s.variety);
            for (size_t i = 0; i < mains.size(); ++i)
            {
                auto& c = mains[i];
                if (rng.nextFloat() < v * 0.7f)
                {
                    c.close = theory::varyChord (c.close, rng);
                    c.root = c.close.front();
                }
                if (rng.nextFloat() < v * 0.35f) c.inversion = 1 + rng.nextInt (2);
            }
        }

        // --- accords d'approche (2e moitié de mesure) vers l'accord suivant : le côté groovy / jazzy
        std::vector<Chord> all;
        for (size_t i = 0; i < mains.size(); ++i)
        {
            auto c = mains[i];
            const auto& next = mains[(i + 1) % mains.size()];

            if (chance (col.split * (0.7f + s.variety * 0.8f)))
            {
                const double splitAt = rng.nextBool() ? 2.0 : 3.0;
                Chord a;
                a.start = c.start + splitAt;
                a.length = 4.0 - splitAt;
                c.length = splitAt;

                const int target = next.root;
                const float pSec = col.secDom, pTri = col.tritone, pChr = col.chromatic;
                const float total = pSec + pTri + pChr + 0.3f;
                const float x = rng.nextFloat() * total;

                if (x < pSec)
                {
                    a.close = theory::chordFrom (target + 7, { 0, 4, 7, 10 });                // V7 de l'accord suivant
                    a.close = theory::colourChord (a.close, juce::jlimit (0.0f, 1.0f, col.spice * spiceMul), rng);
                    a.outside = true;
                    a.roman = "V7/" + theory::romanChromatic (target, s.key, next.close);
                }
                else if (x < pSec + pTri)
                {
                    a.close = theory::chordFrom (target + 1, { 0, 4, 7, 10, 14 });            // substitution tritonique
                    a.outside = true;
                    a.roman = "subV/" + theory::romanChromatic (target, s.key, next.close);
                }
                else if (x < pSec + pTri + pChr)
                {
                    const int dir = rng.nextBool() ? 1 : -1;                                   // approche chromatique
                    a.close.clear();
                    for (int nn : next.close) a.close.push_back (nn + dir);
                    a.outside = true;
                    a.roman = juce::String::fromUTF8 ("chrom.");
                }
                else if (rng.nextFloat() < s.variety * 0.6f)
                {
                    if (rng.nextBool())
                    {
                        a.close = theory::chordFrom (target - 1, { 0, 3, 6, 9 });             // dim7 de passage (un demi-ton sous la cible)
                        a.outside = true;
                        a.roman = juce::String::fromUTF8 ("°7");
                    }
                    else
                    {
                        a.close = theory::chordFrom (c.root, { 0, 5, 7, 10 });                 // 7sus4 qui relance
                        a.degree = c.degree;
                    }
                }
                else
                {
                    a.degree = c.degree + (rng.nextBool() ? 1 : -1);                           // passage diatonique
                    a.close = theory::degreeChord (a.degree, s.key, s.scaleIdx, ext, octBase);
                }
                a.root = a.close.front();
                all.push_back (c);
                all.push_back (a);
            }
            else
            {
                all.push_back (c);
            }
        }

        // noms / chiffrages
        const bool flats = theory::prefersFlats (s.key, s.scaleIdx);
        const int nDeg = theory::degreeCount (s.scaleIdx);
        for (auto& c : all)
        {
            std::vector<int> sorted = c.close;
            std::sort (sorted.begin(), sorted.end());
            c.close = sorted;
            c.root = sorted.front();
            const int rel = theory::mod12 (c.root - s.key);
            c.name = theory::chordName (sorted, flats || (c.outside && (rel == 1 || rel == 3 || rel == 8 || rel == 10)));
            if (c.roman.isEmpty())
                c.roman = c.outside ? theory::romanChromatic (c.root, s.key, sorted)
                                    : theory::roman (((c.degree % nDeg) + nDeg) % nDeg, sorted);
        }
        r.chords = all;
        {
            juce::String sig (juce::String (m.name) + ":F");
            for (int d : degrees) sig << d;
            r.signature = sig;
        }
        return r;
    }

    // Un nouvel accord au hasard pour une case (bouton "changer cet accord")
    inline Chord randomChord (const Settings& s, const Chord& current, int reg, juce::Random& rng)
    {
        const auto col = moods::colourFor (juce::jlimit (0, 12, s.mood));
        const auto& m = moods::list()[(size_t) juce::jlimit (0, (int) moods::list().size() - 1, s.mood)];
        const int ext = juce::jlimit (3, 7, m.ext + (s.richness - 1));
        int low, high, octBase;
        registerRange (reg, low, high, octBase);
        const int n = theory::degreeCount (s.scaleIdx);

        Chord c = current;
        for (int tries = 0; tries < 8; ++tries)
        {
            c.degree = rng.nextInt (n);
            c.close = theory::degreeChord (c.degree, s.key, s.scaleIdx, ext, octBase);
            if (rng.nextFloat() < col.spice * s.spice * 1.4f)
                c.close = theory::colourChord (c.close, juce::jlimit (0.0f, 1.0f, col.spice * s.spice * 2.0f), rng);
            if (rng.nextFloat() < s.variety * 0.6f) c.close = theory::varyChord (c.close, rng);
            if (c.close.front() % 12 != current.root % 12) break;
        }
        c.roman.clear();
        c.inversion = -1;
        c.octave = 0;
        relabel (c, s.key, s.scaleIdx);
        if (! c.outside) c.roman = theory::roman (((c.degree % n) + n) % n, c.close);
        return c;
    }

    // Transforme une progression en séquences MIDI (accords, basse, arpège)
    inline Result render (const Progression& prog, const Settings& s)
    {
        Result r;
        const auto& moodList = moods::list();
        const int moodIdx = juce::jlimit (0, (int) moodList.size() - 1, s.mood);
        const auto& m = moodList[(size_t) moodIdx];
        juce::Random rng ((juce::int64) s.seed + 17);
        r.lengthBeats = prog.lengthBeats;
        const double maxBeat = r.lengthBeats;
        int low, high, octBase;
        registerRange (prog.reg, low, high, octBase);
        juce::ignoreUnused (octBase);

        std::vector<Chord> all = prog.chords;
        std::sort (all.begin(), all.end(), [] (const Chord& a, const Chord& b) { return a.start < b.start; });
        // --- conduite des voix dans le registre choisi, voicing de l'ambiance, noms
        std::vector<int> prev;
        for (auto& c : all)
        {
            std::vector<int> led;
            if (c.inversion < 0) led = theory::voiceLead (c.close, prev, low, high, registerTarget (prog.reg));
            else
            {
                led = c.close;
                std::sort (led.begin(), led.end());
                for (int k = 0; k < c.inversion % (int) led.size(); ++k) { led.front() += 12; std::sort (led.begin(), led.end()); }
                while (led.front() < low) for (auto& x : led) x += 12;
                while (led.front() >= low + 12) for (auto& x : led) x -= 12;
            }
            if (c.octave != 0) for (auto& x : led) x += 12 * c.octave;
            prev = led;
            if (m.voicing == theory::Open && led.size() >= 3)       { led[1] += 12; std::sort (led.begin(), led.end()); }
            else if (m.voicing == theory::Drop2 && led.size() >= 3) { led[led.size() - 2] -= 12; std::sort (led.begin(), led.end()); }
            c.voiced = led;

        }
        // --- recentrage : aucun accord ne doit "sauter" d'une octave par rapport aux autres
        if (all.size() > 1)
        {
            auto mean = [] (const std::vector<int>& v) { double m = 0; for (int x : v) m += x; return v.empty() ? 0.0 : m / (double) v.size(); };
            std::vector<double> means;
            for (auto& c : all) means.push_back (mean (c.voiced));
            auto sorted = means;
            std::sort (sorted.begin(), sorted.end());
            const double median = registerTarget (prog.reg);   // ancre fixe : ni trop grave ni trop aigu
            for (auto& c : all)
            {
                if (c.octave != 0 || c.voiced.empty()) continue;   // octave choisie à la main : on respecte
                double mm = mean (c.voiced);
                while (mm - median > 6.0) { for (auto& x : c.voiced) x -= 12; mm -= 12.0; }
                while (median - mm > 6.0) { for (auto& x : c.voiced) x += 12; mm += 12.0; }
            }
            // 2e passe : pas d'écart d'octave entre accords voisins (tout en restant près du registre visé)
            std::vector<double> ms2;
            for (auto& c : all) ms2.push_back (mean (c.voiced));
            std::sort (ms2.begin(), ms2.end());
            const double med2 = ms2[ms2.size() / 2];
            for (auto& c : all)
            {
                if (c.octave != 0 || c.voiced.empty()) continue;
                double mm = mean (c.voiced);
                if (mm - med2 > 6.5 && mm - 12.0 >= median - 8.0) { for (auto& x : c.voiced) x -= 12; }
                else if (med2 - mm > 6.5 && mm + 12.0 <= median + 8.0) { for (auto& x : c.voiced) x += 12; }
            }
        }
        r.chords = all;

        const double strumBeats = m.strumMs / 500.0;
        const float hum = m.humanize;

        // Rythme d'un segment : on garde les frappes de l'ambiance qui tombent dedans
        auto segmentHits = [] (const Chord& c, const auto& hits, auto&& fn)
        {
            const double end = c.start + c.length;
            bool any = false;
            for (int bar = (int) std::floor (c.start / 4.0); bar * 4.0 < end; ++bar)
                for (auto& h : hits)
                {
                    const double t = bar * 4.0 + h.start;
                    if (t >= c.start - 1.0e-6 && t < end - 1.0e-6)
                    {
                        fn (t, juce::jmin ((double) h.len, end - t), h);
                        any = true;
                    }
                }
            if (! any) fn (c.start, c.length * 0.9, hits.front());
        };

        // début "réel" de chaque accord (une anticipation le fait entendre plus tôt) : l'arpège suivra le même timing
        std::vector<double> effStart;
        for (auto& c : r.chords) effStart.push_back (c.start);

        // --- accords : motifs de groove de l'ambiance (A A B A' ...), indépendants des changements d'accords
        if (s.chords && ! r.chords.empty())
        {
            const auto& bank = moods::grooves (moodIdx);
            const int pa = rng.nextInt ((int) bank.size());
            int pb = rng.nextInt ((int) bank.size());
            if (bank.size() > 1 && pb == pa) pb = (pa + 1) % (int) bank.size();

            struct Hit2 { double t, len; float vel; bool push; };
            std::vector<Hit2> hits;
            for (int phrase = 0; phrase * 8.0 < maxBeat; ++phrase)
            {
                const auto& pat = bank[(size_t) ((phrase % 4 == 2) ? pb : pa)];
                const bool vary = phrase % 4 == 3;
                for (size_t k = 0; k < pat.size(); ++k)
                {
                    const auto& h = pat[k];
                    if (vary && k > 0 && rng.nextFloat() < 0.25f) continue;   // petite variation : on retire une frappe
                    hits.push_back ({ phrase * 8.0 + h.start, (double) h.len, h.vel, h.push });
                }
                if (vary && rng.nextFloat() < 0.6f)   // ... ou on ajoute une note fantôme
                    hits.push_back ({ phrase * 8.0 + 6.5 + 0.25 * rng.nextInt (4), 0.2, 0.45f, false });
            }
            // chaque accord doit sonner : si aucune frappe ne tombe sur son début, on en ajoute une
            for (auto& c : r.chords)
            {
                bool covered = false;
                for (auto& h : hits)
                    if (std::abs (h.t - c.start) < 0.3 || (h.push && std::abs (h.t + 0.5 - c.start) < 0.3)) { covered = true; break; }
                if (! covered && c.start < maxBeat) hits.push_back ({ c.start, c.length, 0.68f, false });
            }
            std::sort (hits.begin(), hits.end(), [] (const Hit2& a, const Hit2& b) { return a.t < b.t; });

            auto chordAt = [&] (double t) -> const Chord*
            {
                for (auto& c : r.chords) if (t >= c.start - 1.0e-6 && t < c.start + c.length - 1.0e-6) return &c;
                return nullptr;
            };
            for (size_t k = 0; k < hits.size(); ++k)
            {
                const auto& h = hits[k];
                if (h.t >= maxBeat) continue;
                const Chord* c = chordAt (h.push ? h.t + 0.5 : h.t);
                if (c == nullptr) c = chordAt (h.t);
                if (c == nullptr) continue;
                {
                    const size_t ci = (size_t) (c - r.chords.data());
                    if (h.t < effStart[ci] && h.t >= c->start - 1.0) effStart[ci] = h.t;
                }
                double len = h.len;
                const double segEnd = h.push ? c->start + c->length : juce::jmin (c->start + c->length, maxBeat);
                len = juce::jmin (len, segEnd - h.t);
                if (s.noOverlap && k + 1 < hits.size()) len = juce::jmin (len, hits[k + 1].t - h.t - 0.02);
                if (len <= 0.05) continue;
                const double jitter = (rng.nextFloat() * 2.0 - 1.0) * 0.02 * hum;
                for (size_t j = 0; j < c->voiced.size(); ++j)
                {
                    const float vel = h.vel * (1.0f + (rng.nextFloat() * 2.0f - 1.0f) * 0.12f * hum);
                    addNote (r.chordSeq, c->voiced[j], h.t + jitter + strumBeats * (double) j, len * 0.97, vel, maxBeat);
                }
            }
        }

        // --- basse
        if (s.bass)
        {
            const int bassBase = prog.reg == RegLow ? 28 : 36;
            for (auto& c : r.chords)
                segmentHits (c, m.bass, [&] (double t, double len, const moods::BassHit& h)
                {
                    std::vector<int> sorted = c.close;
                    std::sort (sorted.begin(), sorted.end());
                    for (auto& x : sorted) x = theory::mod12 (x - c.root) + c.root;   // tons de l'accord au-dessus de la fondamentale
                    std::sort (sorted.begin(), sorted.end());
                    const int tone = sorted[(size_t) juce::jmin (h.tone, (int) sorted.size() - 1)];
                    const int note = bassBase + theory::mod12 (tone - bassBase) + 12 * h.octave;
                    const double jitter = (rng.nextFloat() * 2.0 - 1.0) * 0.01 * hum;
                    addNote (r.bassSeq, note, t + jitter, len, h.vel, maxBeat);
                });
        }

        // --- arpège (réglages de l'onglet Arpège : feeling, liberté, octaves auto)
        if (s.arp)
        {
            const auto& a = s.arpSettings;
            const double step = arp::rateBeats (a.rate);
            const double sw = juce::jlimit (0.0f, 1.0f, a.swing) * 0.33;
            std::vector<int> pattern, scratch;
            juce::Random arng ((juce::int64) s.seed * 3 + 1);

            for (size_t ci = 0; ci < r.chords.size(); ++ci)
            {
                const auto& c = r.chords[ci];
                // fenêtre de l'accord, calée sur le moment où il est vraiment entendu
                const double wStart = effStart[ci];
                const double wEnd = ci + 1 < r.chords.size() ? effStart[ci + 1] : c.start + c.length;
                if (wEnd - wStart < 0.05) continue;
                std::vector<int> base;
                if (a.source == 1)
                {
                    const auto& sc = theory::scaleAt (s.scaleIdx);
                    int note = theory::snapToScale (c.voiced.front(), s.key, sc);
                    for (int i = 0; i < (int) sc.steps.size() && note < 128; ++note)
                        if (theory::inScale (note, s.key, sc)) { base.push_back (note); ++i; }
                }
                else
                {
                    base = c.voiced;
                }

                arp::buildPattern (base, a.mode, a.octaves, a.octDir, a.repeat, pattern, scratch);
                if (pattern.empty()) continue;

                int centre = 0;
                for (int b : base) centre += b;
                centre /= (int) base.size();

                int ppos = 0, lastRnd = -1;
                const int numSteps = juce::jlimit (1, 16, a.numSteps);
                const auto k0 = (juce::int64) std::ceil (wStart / step - 1.0e-9);

                for (auto k = k0; (double) k * step < wEnd - 1.0e-9; ++k)
                {
                    const double t = (double) k * step + ((k & 1) ? sw * step : 0.0);
                    if (t >= wEnd - 0.02) continue;
                    const float level = a.steps[(size_t) (k % numSteps)];
                    if (level <= 0.001f) continue;

                    auto feel = arp::decideStep (arng, a, k);
                    feel.delayBeats = 0.0;   // le MIDI généré reste pile sur la grille (les accents restent)

                    int entry;
                    if (a.mode == arp::Random)
                    {
                        int rr = arng.nextInt ((int) pattern.size());
                        if (pattern.size() > 1 && rr == lastRnd) rr = (rr + 1) % (int) pattern.size();
                        lastRnd = rr;
                        entry = pattern[(size_t) rr];
                    }
                    else
                    {
                        entry = pattern[(size_t) (ppos % (int) pattern.size())];
                    }
                    ++ppos;
                    if (feel.rest) continue;

                    const float vel = juce::jlimit (0.05f, 1.0f, 0.82f * (0.3f + 0.7f * level) * feel.vel);
                    const double len = juce::jmin (step * a.gate * feel.gate, wEnd - t - 0.01);
                    const double t0 = t;
                    if (len <= 0.03) continue;

                    auto play = [&] (int note, double when, double l, float v)
                    {
                        if (a.snap) note = theory::snapToScale (note, s.key, theory::scaleAt (s.scaleIdx));
                        if (when >= wEnd - 0.01) return;
                        addNote (r.arpSeq, note, when, juce::jmin (l, wEnd - when - 0.01), v, maxBeat);
                    };

                    if (entry == arp::chordMarker)
                    {
                        for (int b : base) play (b, t0, len, vel);
                        continue;
                    }

                    const int note = arp::applyFeel (entry, feel, a, centre);
                    if (feel.ratchet)
                    {
                        play (note, t0, juce::jmin (len, step * 0.4), vel);
                        play (note, t0 + step * 0.5, juce::jmin (len, step * 0.4), vel * 0.75f);
                    }
                    else
                    {
                        play (note, t0, len, vel);
                    }
                }
            }
        }

        r.chordSeq.updateMatchedPairs();
        r.bassSeq.updateMatchedPairs();
        r.arpSeq.updateMatchedPairs();
        return r;
    }

    inline Result generate (const Settings& s) { return render (generateProgression (s), s); }

    // Notes éditables (piano roll du plugin). part : 0 accords, 1 basse, 2 arpège
    struct Note { double start = 0, length = 1; int pitch = 60; float vel = 0.8f; int part = 0; bool slide = false; };

    inline std::vector<Note> notesFromResult (const Result& r)
    {
        std::vector<Note> out;
        const juce::MidiMessageSequence* seqs[] = { &r.chordSeq, &r.bassSeq, &r.arpSeq };
        for (int part = 0; part < 3; ++part)
            for (int i = 0; i < seqs[part]->getNumEvents(); ++i)
            {
                auto* e = seqs[part]->getEventPointer (i);
                if (! e->message.isNoteOn() || e->noteOffObject == nullptr) continue;
                const double s = e->message.getTimeStamp() / ppq;
                out.push_back ({ s, e->noteOffObject->message.getTimeStamp() / ppq - s, e->message.getNoteNumber(), e->message.getFloatVelocity(), part });
            }
        return out;
    }

    // playback = pour la lecture dans Variant : on ajoute la partie de chaque note (CC3) et la durée des slides (CC5),
    // pour faire glisser les notes de la même couleur comme dans FL (pas dans les .mid exportés)
    inline juce::MidiMessageSequence seqFromNotes (const std::vector<Note>& notes, std::array<bool, 3> parts, bool playback = false)
    {
        juce::MidiMessageSequence seq;
        for (auto& n : notes)
        {
            if (! parts[(size_t) juce::jlimit (0, 2, n.part)]) continue;
            const double t = n.start * ppq;
            if (playback) seq.addEvent (juce::MidiMessage::controllerEvent (1, 3, juce::jlimit (0, 2, n.part)).withTimeStamp (juce::jmax (0.0, t - 1.0)));
            if (n.slide)   // CC65 juste avant : note slide (comme dans FL)
            {
                if (playback) seq.addEvent (juce::MidiMessage::controllerEvent (1, 5, juce::jlimit (1, 127, (int) std::round (n.length * 16.0))).withTimeStamp (juce::jmax (0.0, t - 1.0)));
                seq.addEvent (juce::MidiMessage::controllerEvent (1, 65, 127).withTimeStamp (juce::jmax (0.0, t - 1.0)));
            }
            seq.addEvent (juce::MidiMessage::noteOn (1, n.pitch, juce::jlimit (0.05f, 1.0f, n.vel)).withTimeStamp (t));
            seq.addEvent (juce::MidiMessage::noteOff (1, n.pitch).withTimeStamp ((n.start + juce::jmax (0.05, n.length)) * ppq));
        }
        seq.updateMatchedPairs();
        return seq;
    }
    inline bool writeMidiFile (const juce::MidiMessageSequence& seq, double lengthBeats, const juce::String& trackName,
                               const juce::File& file)
    {
        juce::MidiMessageSequence track;
        track.addEvent (juce::MidiMessage::textMetaEvent (3, trackName).withTimeStamp (0));
        track.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4).withTimeStamp (0));
        track.addSequence (seq, 0.0);
        track.addEvent (juce::MidiMessage::endOfTrack().withTimeStamp (lengthBeats * ppq));
        track.updateMatchedPairs();

        juce::MidiFile mf;
        mf.setTicksPerQuarterNote (ppq);
        mf.addTrack (track);

        file.deleteFile();
        juce::FileOutputStream out (file);
        if (! out.openedOk()) return false;
        return mf.writeTo (out, 1);
    }
}
