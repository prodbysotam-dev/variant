#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <complex>
#include <array>
#include <memory>
#include <functional>
#include "I18n.h"

// Effets en "slots" : on ajoute seulement ceux qu'on veut, chacun avec ses presets.
namespace fxs
{
    // listes de textes avec accents : lues en UTF-8 (sinon « AccÃ©lÃ©ration » à l'écran)
    inline juce::StringArray utf8List (std::initializer_list<const char*> l) { juce::StringArray a; for (auto* s : l) a.add (juce::String::fromUTF8 (s)); return a; }
    constexpr int numSlots = 16;   // places dans la chaîne d effets (l effet n est créé que quand on l utilise)
    constexpr int numP = 8;
    constexpr float pi = juce::MathConstants<float>::pi;
    constexpr float twoPi = juce::MathConstants<float>::twoPi;

    enum Type { None = 0, Saturation, Filter, Compressor, EQ, Chorus, Flanger, Phaser, Tremolo, Lofi, Portal, Crystal, Delay, Reverb, Stereo,
                HalfTime, Shaper, Glitch, Harmonic, Pad, Liquid, Noise, NumTypes };

    inline float expMap (float v, float lo, float hi) { return lo * std::pow (hi / lo, juce::jlimit (0.0f, 1.0f, v)); }
    inline float lin (float v, float lo, float hi)    { return lo + (hi - lo) * juce::jlimit (0.0f, 1.0f, v); }
    inline int   step (float v, int n)                { return juce::jlimit (0, n - 1, (int) std::round (v * (n - 1))); }

    inline juce::String hzText (float f) { return f < 1000.0f ? juce::String (juce::roundToInt (f)) + " Hz" : juce::String (f / 1000.0f, 1) + " kHz"; }
    //--------------------------------------------------------------------------
    // Synchro au tempo du DAW : toutes les modulations tombent sur des valeurs de notes,
    // et suivent en direct les changements de BPM (et la position de lecture de FL)
    struct HostSync { double bpm = 120.0, ppq = 0.0; bool playing = false; int key = 0; int scaleMask = 0xAB5; int heldMask = 0; };
    inline HostSync& host() { static HostSync h; return h; }

    // divisions possibles, en temps (noires)
    inline const std::array<double, 14>& divisions()
    {
        static const std::array<double, 14> d { 16.0, 8.0, 4.0, 3.0, 2.0, 1.5, 1.0, 0.75, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 0.0625 };
        return d;
    }

    inline juce::String divisionName (double beats)
    {
        static const char* names[] = { "4 mes.", "2 mes.", "1 mes.", "1/2.", "1/2", "1/4.", "1/4", "1/8.", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/64" };
        int best = 0;
        for (int i = 0; i < 14; ++i) if (std::abs (std::log (divisions()[(size_t) i] / beats)) < std::abs (std::log (divisions()[(size_t) best] / beats))) best = i;
        return fr (names[best]);
    }

    // vitesse libre (Hz) -> division la plus proche au tempo actuel
    inline double nearestDivision (double hz, double bpm)
    {
        const double beatHz = juce::jmax (20.0, bpm) / 60.0;
        double best = 1.0, bestErr = 1.0e9;
        for (double d : divisions())
        {
            const double err = std::abs (std::log ((beatHz / d) / juce::jmax (1.0e-4, hz)));
            if (err < bestErr) { bestErr = err; best = d; }
        }
        return best;
    }
    inline double syncedHz (double hz, double bpm, double* divOut = nullptr)
    {
        const double d = nearestDivision (hz, bpm);
        if (divOut != nullptr) *divOut = d;
        return juce::jmax (20.0, bpm) / 60.0 / d;
    }
    // cale la phase d'un LFO sur la position de lecture du DAW
    inline void lockPhase (double divBeats, float& phase)
    {
        const auto& h = host();
        if (! h.playing || divBeats <= 0.0) return;
        const double c = h.ppq / divBeats;
        phase = (float) (juce::MathConstants<double>::twoPi * (c - std::floor (c)));
    }
    // durée libre (ms) -> durée musicale la plus proche (grains, fenêtres)
    inline int syncedSamples (double ms, double bpm, double sr)
    {
        const double beatMs = 60000.0 / juce::jmax (20.0, bpm);
        double best = ms, bestErr = 1.0e9;
        for (double d : divisions())
        {
            const double cand = beatMs * d;
            const double err = std::abs (std::log (cand / juce::jmax (1.0, ms)));
            if (err < bestErr) { bestErr = err; best = cand; }
        }
        return (int) (best * 0.001 * sr);
    }
    // texte d'un bouton "Vitesse" : division au tempo actuel
    inline juce::String rateText (float hz)
    {
        double d = 1.0;
        const double r = syncedHz (hz, host().bpm, &d);
        return divisionName (d) + juce::String::fromUTF8 ("  ·  ") + juce::String (r, 2) + " Hz";
    }

    //--------------------------------------------------------------------------
    // Effets créatifs : formes du Shaper, événements du Glitch, tables du Halftime / Harmoniseur
    inline const juce::StringArray& shapeNames()
    {
        static const juce::StringArray n = utf8List ({ "Sidechain", "Pompe douce", "Gate 1/2", "Trance gate", "Rampe montante", "Rampe descendante",
                                           "Triangle", "Escalier", "Stutter", "Rebond", "Syncopé", "Aléatoire" });
        return n;
    }

    // gain 0..1 sur un cycle (x de 0 à 1)
    inline float shaperShape (int shape, float x)
    {
        x = x - std::floor (x);
        auto pattern = [x] (const char* p, int n) { return p[juce::jlimit (0, n - 1, (int) (x * (float) n))] == '1' ? 1.0f : 0.0f; };
        switch (shape)
        {
            case 0:  return 1.0f - std::exp (-x * 9.0f);
            case 1:  return 1.0f - 0.75f * std::exp (-x * 4.0f);
            case 2:  return x < 0.5f ? 1.0f : 0.0f;
            case 3:  return pattern ("11011010", 8);
            case 4:  return x;
            case 5:  return 1.0f - x;
            case 6:  return 1.0f - std::abs (2.0f * x - 1.0f);
            case 7:  return std::floor (x * 4.0f) / 3.0f;
            case 8:  return std::fmod (x * 16.0f, 1.0f) < 0.5f ? 1.0f : 0.0f;
            case 9:  return std::abs (std::cos (3.0f * juce::MathConstants<float>::pi * x)) * (1.0f - 0.5f * x);
            case 10: return pattern ("1001001000101000", 16);
            default:
            {
                const int k = (int) (x * 8.0f);
                juce::uint32 h = (juce::uint32) (k * 2654435761u); h ^= h >> 15;
                return 0.15f + 0.85f * (float) (h % 1000) / 1000.0f;
            }
        }
    }

    // phase avec swing (pivot déplacé)
    inline float swingPhase (float x, float swing)
    {
        const float p = 0.5f + 0.22f * swing;
        return x < p ? x / p * 0.5f : 0.5f + (x - p) / (1.0f - p) * 0.5f;
    }

    // Glitch : événement d'un pas (motif répétable de 16 pas, change avec "Variation")
    enum GlitchEvent { GNone = 0, GStutter, GReverse, GTapeStop, GCrush, GPitch };
    inline int glitchEventFor (juce::int64 stepIndex, const float* p)
    {
        const int pos = (int) (((stepIndex % 16) + 16) % 16);
        const int seed = (int) std::round (p[7] * 99.0f);
        juce::uint32 h = (juce::uint32) (pos * 73856093) ^ (juce::uint32) ((seed + 1) * 19349663);
        h ^= h >> 13; h *= 0x5bd1e995; h ^= h >> 15;
        const float chance = (float) (h % 1000) / 1000.0f;
        if (chance >= p[0] * 0.9f) return GNone;
        const float w[5] = { p[2], p[3], p[4], p[5], p[6] };
        float total = 0.0f;
        for (float x : w) total += x;
        if (total <= 0.001f) return GNone;
        float r = (float) ((h >> 10) % 1000) / 1000.0f * total;
        for (int i = 0; i < 5; ++i) { r -= w[i]; if (r <= 0.0f) return 1 + i; }
        return GStutter;
    }

    inline double halftimeRate (int i)
    {
        static const double r[] = { 0.5, 2.0 / 3.0, 0.75, 1.0 / 3.0, 0.25 };
        return r[juce::jlimit (0, 4, i)];
    }
    inline double halftimeBeats (int i)
    {
        static const double b[] = { 1.0, 2.0, 4.0, 8.0 };
        return b[juce::jlimit (0, 3, i)];
    }

    // Harmoniseur : gammes (masques de 12 notes), 0 = gamme du plugin
    inline int harmonicMask (int i)
    {
        // comme Chroma : naturelle* (sans la quarte), phrygien dominant, et les notes jouées en MIDI (le son suit tes accords)
        static const int m[] = { -1, 0xAB5, 0x5AD, 0x295, 0x4A9, 0x6AD, 0x9AD, 0x091, 0x089, 0x081, 0x001, 0xA95, 0x5B3, -2 };
        const int v = m[juce::jlimit (0, 13, i)];
        if (v == -2) return host().heldMask != 0 ? host().heldMask : host().scaleMask;
        return v < 0 ? host().scaleMask : v;
    }

    //--------------------------------------------------------------------------
    // Façon Gross Beat : enveloppes de TEMPS (décalage dans un tampon de 2 mesures) et de VOLUME
    struct EnvPt { float x = 0.0f, y = 0.0f; };
    struct Envelope
    {
        std::array<EnvPt, 32> pts {};
        int n = 0;
        float at (float x) const
        {
            if (n <= 0) return 0.0f;
            if (x <= pts[0].x) return pts[0].y;
            for (int i = 1; i < n; ++i)
                if (x <= pts[(size_t) i].x)
                {
                    const auto& a = pts[(size_t) i - 1]; const auto& b = pts[(size_t) i];
                    if (b.x - a.x < 1.0e-5f) return b.y;   // marche verticale (saut)
                    return a.y + (b.y - a.y) * (x - a.x) / (b.x - a.x);
                }
            return pts[(size_t) n - 1].y;
        }
    };

    //--------------------------------------------------------------------------
    // Liquide (façon ShaperBox Liquid) : courbe dessinée qui pilote un flanger / chorus / phaser
    inline const juce::StringArray& liquidCurveNames()
    {
        static const juce::StringArray n = utf8List ({ "Perso", "Sinus", "Montée", "Descente", "Accélération", "Vagues", "Rampes x4", "Rebonds", "Plateau" });
        return n;
    }
    inline float liquidShape (int idx, float x)
    {
        const float tp = juce::MathConstants<float>::twoPi;
        switch (idx)
        {
            case 1: return 0.5f - 0.5f * std::cos (tp * x);
            case 2: return x;
            case 3: return 1.0f - x;
            case 4: return 0.5f + 0.5f * std::sin (tp * (x + 3.0f * x * x * x));   // oscillations de plus en plus rapides
            case 5: return 0.5f + 0.35f * std::sin (tp * 2.0f * x) + 0.15f * std::sin (tp * 7.0f * x);
            case 6: return std::fmod (x * 4.0f, 1.0f);
            case 7: return std::abs (std::sin (tp * 2.0f * x)) * (1.0f - 0.5f * x);
            default: return x < 0.15f ? x / 0.15f : (x > 0.85f ? (1.0f - x) / 0.15f : 1.0f);
        }
    }
    struct LiquidCurves
    {
        juce::SpinLock lock;
        std::array<Envelope, numSlots> env {};
        std::atomic<int> version { 0 };
        LiquidCurves()
        {
            for (auto& e : env)
            {
                e.n = 9;
                for (int i = 0; i < 9; ++i) e.pts[(size_t) i] = { i / 8.0f, liquidShape (1, i / 8.0f) };
            }
        }
    };

    inline const juce::StringArray& timeSlotNames()
    {
        static const juce::StringArray n = utf8List ({ "Perso", "Normal", "Halftime", "Reverse", "Stutter 1/4", "Stutter 1/8", "Stutter 1/16",
                                           "Tape stop", "Tape start", "Rewind", "Triolets", "Halftime fin", "Scratch" });
        return n;
    }
    inline const juce::StringArray& volumeSlotNames()
    {
        static juce::StringArray n;
        if (n.isEmpty()) { n.add ("Perso"); for (auto& s : shapeNames()) n.add (s); }
        return n;
    }

    // décalage dans le temps (en cycles, 0 = son en direct) pour les slots d'usine
    inline float factoryTime (int slot, float x)
    {
        switch (slot)
        {
            case 2:  return x * 0.5f;
            case 3:  return juce::jmin (2.0f * x, 1.95f);
            case 4:  return std::fmod (x, 0.25f);
            case 5:  return std::fmod (x, 0.125f);
            case 6:  return std::fmod (x, 0.0625f);
            case 7:  return x * x * 0.5f;
            case 8:  return x - x * x * 0.5f;
            case 9:  return x < 0.5f ? 0.0f : 2.0f * (x - 0.5f);
            case 10: return std::fmod (x, 1.0f / 3.0f);
            case 11: return x < 0.5f ? 0.0f : (x - 0.5f) * 0.5f;
            case 12: return 0.06f * (1.0f - std::cos (juce::MathConstants<float>::twoPi * 4.0f * x));
            default: return 0.0f;
        }
    }

    // enveloppes "Perso" dessinées par l'utilisateur, une paire (temps, volume) par emplacement d'effet
    struct CustomEnvelopes
    {
        juce::SpinLock lock;
        std::array<std::array<Envelope, 2>, numSlots> env;
        std::atomic<int> version { 0 };
        CustomEnvelopes()
        {
            for (auto& s : env)
            {
                s[0].n = 2; s[0].pts[0] = { 0.0f, 0.0f }; s[0].pts[1] = { 1.0f, 0.0f };   // temps : son direct
                s[1].n = 2; s[1].pts[0] = { 0.0f, 1.0f }; s[1].pts[1] = { 1.0f, 1.0f };   // volume : plein
            }
        }
    };
    inline CustomEnvelopes& customEnvelopes() { static CustomEnvelopes c; return c; }

    // valeur d'un slot (0 = perso) : temps en cycles, volume 0..1
    inline float slotValue (int fxSlot, bool volume, int slot, float x, const Envelope* custom = nullptr)
    {
        if (slot == 0)
        {
            if (custom != nullptr) return custom->at (x);
            auto& c = customEnvelopes();
            const juce::SpinLock::ScopedLockType l (c.lock);
            return c.env[(size_t) juce::jlimit (0, fxs::numSlots - 1, fxSlot)][volume ? 1 : 0].at (x);
        }
        return volume ? shaperShape (slot - 1, x) : factoryTime (slot, x);
    }

    //--------------------------------------------------------------------------
    // Glitch façon Digitalis : 6 pistes x 16 pas, motifs d'usine + motif "Perso" cliquable
    enum GlitchLane { LRepeat = 0, LReverse, LTape, LPitch, LCrush, LGate, NumLanes };
    inline const juce::StringArray& glitchLaneNames()
    {
        static const juce::StringArray n = utf8List ({ "Répète", "Reverse", "Tape stop", "Pitch", "Crush", "Gate" });
        return n;
    }
    inline const juce::StringArray& glitchPatternNames()
    {
        static const juce::StringArray n = utf8List ({ "Perso", "Bégaiement", "IDM", "Reverse fin", "Tape stop fin", "Hachoir", "Vocal chop",
                                           "Chiptune", "Breakbeat", "Roulement", "Dub", "Trap", "Chaos" });
        return n;
    }
    struct GlitchPattern
    {
        std::array<juce::uint16, NumLanes> lanes {};
        bool on (int lane, int stepIdx) const { return ((lanes[(size_t) lane] >> (stepIdx & 15)) & 1) != 0; }
        void toggle (int lane, int stepIdx) { lanes[(size_t) lane] = (juce::uint16) (lanes[(size_t) lane] ^ (1u << (stepIdx & 15))); }
    };
    inline GlitchPattern factoryGlitch (int idx)
    {
        // une chaîne par piste : Répète, Reverse, Tape, Pitch, Crush, Gate (pas 1 = 1er caractère)
        static const char* p[13][NumLanes] = {
            { "", "", "", "", "", "" },
            { "0000000100000011", "", "", "", "", "" },
            { "0010010000100100", "0000001000000010", "", "1000000000001000", "0001000000010000", "" },
            { "", "0000000000001111", "", "", "", "" },
            { "", "", "0000000000000011", "", "", "" },
            { "", "", "", "", "", "0101010101010101" },
            { "0100010001000100", "", "", "0001000100010001", "", "" },
            { "", "", "", "0000100000001000", "1111111111111111", "" },
            { "0000000100000101", "0000010000000000", "", "", "", "0010000000100000" },
            { "0000000000001111", "", "", "0000000000000011", "", "" },
            { "", "0000000010000000", "0000000000000001", "", "", "0001000000010000" },
            { "0000000000110011", "", "", "0000000000000011", "", "0000100000001000" },
            { "1001011000101101", "0100000100010000", "0000000000000001", "0010100001000100", "0001001010000010", "0100010000101000" },
        };
        GlitchPattern g;
        idx = juce::jlimit (0, 12, idx);
        for (int l = 0; l < NumLanes; ++l)
            for (int s = 0; p[idx][l][s] != 0 && s < 16; ++s)
                if (p[idx][l][s] == '1') g.lanes[(size_t) l] = (juce::uint16) (g.lanes[(size_t) l] | (1u << s));
        return g;
    }
    struct GlitchPatterns
    {
        juce::SpinLock lock;
        std::array<GlitchPattern, numSlots> perso {};
        std::atomic<int> version { 0 };
    };
    inline GlitchPatterns& glitchPatterns() { static GlitchPatterns g; return g; }
    inline GlitchPattern glitchPatternFor (GlitchPatterns& gp, int fxSlot, int idx)
    {
        if (idx > 0) return factoryGlitch (idx);
        const juce::SpinLock::ScopedLockType l (gp.lock);
        return gp.perso[(size_t) juce::jlimit (0, fxs::numSlots - 1, fxSlot)];
    }
    inline GlitchPattern glitchPatternFor (int fxSlot, int idx)
    {
        if (idx > 0) return factoryGlitch (idx);
        auto& gp = glitchPatterns();
        const juce::SpinLock::ScopedLockType l (gp.lock);
        return gp.perso[(size_t) juce::jlimit (0, fxs::numSlots - 1, fxSlot)];
    }
    // pistes actives d'un pas (motif + hasard optionnel, différent à chaque mesure mais répétable)
    inline std::array<bool, NumLanes> glitchStep (const GlitchPattern& pat, juce::int64 stepIndex, float hasard)
    {
        std::array<bool, NumLanes> a {};
        const int pos = (int) (((stepIndex % 16) + 16) % 16);
        for (int l = 0; l < NumLanes; ++l) a[(size_t) l] = pat.on (l, pos);
        if (hasard > 0.001f)
        {
            juce::uint32 h = (juce::uint32) (stepIndex * 2654435761u) ^ 0x9e3779b9u; h ^= h >> 15; h *= 0x85ebca6bu; h ^= h >> 13;
            if ((float) (h % 1000) / 1000.0f < hasard * 0.4f) a[(size_t) ((h >> 12) % NumLanes)] = true;
        }
        return a;
    }
    inline juce::String msText (float m) { return m < 1000.0f ? juce::String (m, m < 10.0f ? 1 : 0) + " ms" : juce::String (m / 1000.0f, 2) + " s"; }
    inline juce::String pct (float v)    { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
    inline juce::String db (float d)     { return (d > 0 ? "+" : "") + juce::String (d, 1) + " dB"; }

    inline const juce::StringArray& divisionNames()
    {
        static const juce::StringArray d = utf8List ({ "1/32", "1/16 T", "1/16", "1/16 .", "1/8 T", "1/8", "1/8 .", "1/4 T", "1/4", "1/4 .", "1/2", "1/1" });
        return d;
    }
    inline double divisionBeats (int i)
    {
        static const double b[] = { 0.125, 1.0 / 6.0, 0.25, 0.375, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1.0, 1.5, 2.0, 4.0 };
        return b[juce::jlimit (0, 11, i)];
    }

    //==========================================================================
    // Description des effets (noms, affichage des valeurs, presets)
    struct ParamDef { const char* name; std::function<juce::String (float)> text; };
    struct Preset   { const char* name; std::array<float, numP> p; float mix; };
    struct TypeDef
    {
        const char* name;
        const char* desc;
        bool wetOnly;
        std::array<ParamDef, numP> params;
        std::vector<Preset> presets;
        bool selfMix = false;   // l'effet mélange lui-même sec / traité (latence FFT)
    };

    inline const std::vector<TypeDef>& baseDefs()
    {
        static const char* satModels[] = { "Lampe", "Transfo", "Console", "Bande", "Fuzz" };
        static const char* fltTypes[]  = { "Passe-bas", "Passe-haut", "Passe-bande", "Coupe-bande", "Ladder 24 dB" };
        static const char* choModes[]  = { "I", "II", "I + II", "Ensemble" };
        static const char* stages[]    = { "2", "4", "6", "8", "12" };

        static const std::vector<TypeDef> d {
            { "Vide", "", false, {}, {} },

            { "Saturation", "chaleur analogique, suréchantillonnée", false,
              { { { "Modèle",  [] (float v) { return fr (satModels[step (v, 5)]); } },
                  { "Drive",   [] (float v) { return db (lin (v, 0, 36)); } },
                  { "Ton",     [] (float v) { const float t = lin (v, -1, 1); return t < -0.05f ? fr ("Sombre") : (t > 0.05f ? fr ("Brillant") : fr ("Neutre")); } },
                  { "Coupe-bas", [] (float v) { return hzText (expMap (v, 20, 600)); } },
                  { "Dureté",  [] (float v) { return pct (v); } },
                  { "Sortie",  [] (float v) { return db (lin (v, -12, 12)); } } } },
              { { "Chaleur lampe",        { 0.0f, 0.30f, 0.50f, 0.10f, 0.25f, 0.5f }, 1.0f },
                { "Lampe poussée",        { 0.0f, 0.62f, 0.55f, 0.20f, 0.55f, 0.5f }, 1.0f },
                { "Transfo gras",         { 0.25f, 0.45f, 0.40f, 0.05f, 0.30f, 0.5f }, 1.0f },
                { "Console de mix",       { 0.5f, 0.35f, 0.50f, 0.00f, 0.10f, 0.5f }, 1.0f },
                { "Bande chaude",         { 0.75f, 0.45f, 0.38f, 0.10f, 0.20f, 0.5f }, 1.0f },
                { "Bande saturée",        { 0.75f, 0.75f, 0.35f, 0.15f, 0.40f, 0.5f }, 1.0f },
                { "Fuzz vintage",         { 1.0f, 0.60f, 0.45f, 0.30f, 0.60f, 0.45f }, 1.0f },
                { "Fuzz écrasé",          { 1.0f, 0.95f, 0.60f, 0.40f, 1.00f, 0.4f }, 1.0f },
                { "Parallèle (punch)",    { 0.0f, 0.70f, 0.60f, 0.25f, 0.40f, 0.5f }, 0.4f },
                { "Lo-fi sombre",         { 0.75f, 0.55f, 0.10f, 0.35f, 0.30f, 0.5f }, 1.0f } } },

            { "Filtre", "qui bouge, avec drive", false,
              { { { "Type",       [] (float v) { return fr (fltTypes[step (v, 5)]); } },
                  { "Fréquence",  [] (float v) { return hzText (expMap (v, 20, 20000)); } },
                  { "Résonance",  [] (float v) { return pct (v); } },
                  { "Drive",      [] (float v) { return pct (v); } },
                  { "Mouvement",  [] (float v) { return pct (v); } },
                  { "Vitesse",    [] (float v) { return rateText (expMap (v, 0.05f, 10.0f)); } } } },
              { { "Douceur (passe-bas)",  { 0.0f, 0.62f, 0.15f, 0.0f, 0.0f, 0.3f }, 1.0f },
                { "Sombre feutré",        { 0.0f, 0.45f, 0.10f, 0.1f, 0.0f, 0.3f }, 1.0f },
                { "Balayage lent",        { 0.0f, 0.55f, 0.45f, 0.1f, 0.55f, 0.2f }, 1.0f },
                { "Wah rapide",           { 2.0f / 4, 0.55f, 0.65f, 0.2f, 0.5f, 0.65f }, 1.0f },
                { "Ladder acide",         { 1.0f, 0.42f, 0.80f, 0.5f, 0.35f, 0.45f }, 1.0f },
                { "Ladder rond",          { 1.0f, 0.55f, 0.35f, 0.3f, 0.0f, 0.3f }, 1.0f },
                { "Allègement (passe-haut)", { 0.25f, 0.30f, 0.10f, 0.0f, 0.0f, 0.3f }, 1.0f },
                { "Radio",                { 0.5f, 0.62f, 0.40f, 0.4f, 0.0f, 0.3f }, 1.0f },
                { "Téléphone",            { 0.5f, 0.68f, 0.55f, 0.6f, 0.0f, 0.3f }, 1.0f },
                { "Notch mouvant",        { 0.75f, 0.55f, 0.50f, 0.0f, 0.6f, 0.25f }, 1.0f } } },

            { "Compresseur", "colle et densité", false,
              { { { "Seuil",    [] (float v) { return db (lin (v, -48, 0)); } },
                  { "Ratio",    [] (float v) { return juce::String (expMap (v, 1, 20), 1) + ":1"; } },
                  { "Attaque",  [] (float v) { return msText (expMap (v, 0.1f, 100)); } },
                  { "Relâche",  [] (float v) { return msText (expMap (v, 10, 1000)); } },
                  { "Gain",     [] (float v) { return db (lin (v, 0, 24)); } },
                  { "Caractère",[] (float v) { return v < 0.33f ? fr ("Propre") : (v < 0.66f ? fr ("Vintage") : fr ("Opto")); } } } },
              { { "Colle douce",          { 0.70f, 0.25f, 0.45f, 0.45f, 0.10f, 0.4f }, 1.0f },
                { "Densité",              { 0.55f, 0.40f, 0.35f, 0.40f, 0.20f, 0.5f }, 1.0f },
                { "Punch (attaque lente)",{ 0.60f, 0.45f, 0.70f, 0.30f, 0.20f, 0.3f }, 1.0f },
                { "Écrasement",           { 0.35f, 0.85f, 0.15f, 0.35f, 0.40f, 0.6f }, 1.0f },
                { "Parallèle NY",         { 0.35f, 0.80f, 0.20f, 0.40f, 0.40f, 0.5f }, 0.45f },
                { "Opto vocal",           { 0.60f, 0.35f, 0.50f, 0.60f, 0.15f, 0.9f }, 1.0f },
                { "Sustain long",         { 0.40f, 0.60f, 0.30f, 0.75f, 0.30f, 0.8f }, 1.0f },
                { "Contrôle léger",       { 0.80f, 0.15f, 0.40f, 0.50f, 0.05f, 0.2f }, 1.0f },
                { "Pompage",              { 0.40f, 0.70f, 0.10f, 0.25f, 0.30f, 0.5f }, 1.0f },
                { "Limiteur",             { 0.85f, 1.00f, 0.00f, 0.35f, 0.10f, 0.1f }, 1.0f } } },

            { "Égaliseur", "paramétrique façon FL : attrape les points sur la courbe", false,
              { { { "Sortie",     [] (float v) { return db (lin (v, -18, 18)); } } } },
              { { "Neutre",               { 0.5f }, 1.0f },
                { "Chaleur",              { 0.5f }, 1.0f },
                { "Brillance",            { 0.5f }, 1.0f },
                { "Air soyeux",           { 0.5f }, 1.0f },
                { "Moins de boue",        { 0.5f }, 1.0f },
                { "Présence en avant",    { 0.5f }, 1.0f },
                { "Sourire",              { 0.5f }, 1.0f },
                { "Téléphone",            { 0.5f }, 1.0f },
                { "Graves profonds",      { 0.5f }, 1.0f },
                { "Nettoyage pour mix",   { 0.5f }, 1.0f } } },

            { "Chorus", "épaisseur de chorus de synthé vintage", false,
              { { { "Mode",      [] (float v) { return fr (choModes[step (v, 4)]); } },
                  { "Vitesse",   [] (float v) { return rateText (expMap (v, 0.05f, 6)); } },
                  { "Profondeur",[] (float v) { return pct (v); } },
                  { "Ton",       [] (float v) { return hzText (expMap (v, 1500, 16000)); } },
                  { "Largeur",   [] (float v) { return pct (v); } },
                  { "Retard",    [] (float v) { return msText (lin (v, 2, 25)); } } } },
              { { "Double voix I",          { 0.0f, 0.30f, 0.35f, 0.55f, 0.8f, 0.3f }, 0.5f },
                { "Double voix II",         { 1.0f / 3, 0.40f, 0.50f, 0.55f, 0.9f, 0.3f }, 0.5f },
                { "Double voix I + II",     { 2.0f / 3, 0.45f, 0.55f, 0.55f, 1.0f, 0.35f }, 0.5f },
                { "Ensemble cordes",      { 1.0f, 0.35f, 0.55f, 0.60f, 1.0f, 0.4f }, 0.55f },
                { "Doublage subtil",      { 0.0f, 0.20f, 0.20f, 0.70f, 0.7f, 0.55f }, 0.35f },
                { "Vintage sombre",       { 1.0f / 3, 0.35f, 0.60f, 0.25f, 0.8f, 0.3f }, 0.5f },
                { "Tourbillon",           { 1.0f, 0.70f, 0.75f, 0.60f, 1.0f, 0.3f }, 0.6f },
                { "Large & lent",         { 2.0f / 3, 0.15f, 0.65f, 0.60f, 1.0f, 0.6f }, 0.5f },
                { "Vibrato",              { 0.0f, 0.62f, 0.55f, 0.80f, 0.5f, 0.2f }, 1.0f },
                { "Brillant",             { 1.0f / 3, 0.45f, 0.40f, 0.95f, 0.9f, 0.3f }, 0.45f } } },

            { "Flanger", "effet avion, through-zero", false,
              { { { "Vitesse",    [] (float v) { return rateText (expMap (v, 0.02f, 8)); } },
                  { "Profondeur", [] (float v) { return pct (v); } },
                  { "Réinjection",[] (float v) { return juce::String (juce::roundToInt (lin (v, -95, 95))) + " %"; } },
                  { "Retard",     [] (float v) { return msText (lin (v, 0.2f, 8.0f)); } },
                  { "Stéréo",     [] (float v) { return juce::String (juce::roundToInt (v * 180)) + juce::String::fromUTF8 ("°"); } },
                  { "Through-zero", [] (float v) { return v > 0.5f ? fr ("Oui") : fr ("Non"); } } } },
              { { "Avion classique",      { 0.20f, 0.70f, 0.80f, 0.25f, 0.5f, 0.0f }, 0.5f },
                { "Jet négatif",          { 0.20f, 0.70f, 0.15f, 0.25f, 0.5f, 0.0f }, 0.5f },
                { "Through-zero",         { 0.18f, 0.80f, 0.60f, 0.20f, 0.3f, 1.0f }, 0.5f },
                { "Métallique",           { 0.35f, 0.40f, 0.95f, 0.10f, 0.5f, 0.0f }, 0.5f },
                { "Doux & lent",          { 0.08f, 0.50f, 0.65f, 0.35f, 0.8f, 0.0f }, 0.4f },
                { "Rapide",               { 0.65f, 0.35f, 0.70f, 0.20f, 0.5f, 0.0f }, 0.45f },
                { "Stéréo large",         { 0.15f, 0.60f, 0.70f, 0.30f, 1.0f, 0.0f }, 0.5f },
                { "Résonateur",           { 0.05f, 0.10f, 0.92f, 0.60f, 0.2f, 0.0f }, 0.5f },
                { "Chorus flangé",        { 0.30f, 0.50f, 0.55f, 0.70f, 0.7f, 0.0f }, 0.45f },
                { "Balayage profond",     { 0.12f, 1.00f, 0.85f, 0.40f, 0.5f, 1.0f }, 0.55f } } },

            { "Phaser", "tourbillon analogique", false,
              { { { "Étages",     [] (float v) { return juce::String (stages[step (v, 5)]); } },
                  { "Vitesse",    [] (float v) { return rateText (expMap (v, 0.02f, 8)); } },
                  { "Profondeur", [] (float v) { return pct (v); } },
                  { "Réinjection",[] (float v) { return juce::String (juce::roundToInt (lin (v, -90, 90))) + " %"; } },
                  { "Centre",     [] (float v) { return hzText (expMap (v, 100, 4000)); } },
                  { "Stéréo",     [] (float v) { return juce::String (juce::roundToInt (v * 180)) + juce::String::fromUTF8 ("°"); } } } },
              { { "Phase orange",             { 0.25f, 0.25f, 0.60f, 0.55f, 0.45f, 0.3f }, 0.5f },
                { "Phase caillou",          { 0.25f, 0.35f, 0.75f, 0.80f, 0.50f, 0.5f }, 0.5f },
                { "Lent et large",        { 0.50f, 0.10f, 0.80f, 0.65f, 0.45f, 1.0f }, 0.5f },
                { "12 étages profond",    { 1.0f, 0.15f, 0.90f, 0.75f, 0.40f, 0.5f }, 0.5f },
                { "Rapide",               { 0.25f, 0.65f, 0.50f, 0.60f, 0.55f, 0.3f }, 0.5f },
                { "Subtil",               { 0.25f, 0.20f, 0.35f, 0.50f, 0.50f, 0.4f }, 0.35f },
                { "Négatif creux",        { 0.50f, 0.25f, 0.70f, 0.15f, 0.45f, 0.5f }, 0.5f },
                { "Vibe ondulante",              { 0.50f, 0.45f, 0.60f, 0.55f, 0.35f, 0.25f }, 0.6f },
                { "Aigu scintillant",     { 0.75f, 0.30f, 0.60f, 0.80f, 0.85f, 0.7f }, 0.45f },
                { "Vague",                { 0.75f, 0.06f, 1.00f, 0.70f, 0.40f, 1.0f }, 0.55f } } },

            { "Trémolo", "pulsation / auto-pan", false,
              { { { "Vitesse",    [] (float v) { return rateText (expMap (v, 0.1f, 20)); } },
                  { "Profondeur", [] (float v) { return pct (v); } },
                  { "Forme",      [] (float v) { return v < 0.2f ? fr ("Sinus") : (v > 0.8f ? fr ("Carré") : fr ("Mixte")); } },
                  { "Auto-pan",   [] (float v) { return pct (v); } },
                  { "Synchro",    [] (float v) { return v < 0.04f ? fr ("Libre") : divisionNames()[step (v, 12)]; } },
                  { "",           {} } } },
              { { "Trémolo vintage",      { 0.45f, 0.45f, 0.0f, 0.0f, 0.0f, 0 }, 1.0f },
                { "Auto-pan lent",        { 0.20f, 0.60f, 0.0f, 1.0f, 0.0f, 0 }, 1.0f },
                { "Hélicoptère",          { 0.70f, 0.80f, 0.9f, 0.0f, 0.0f, 0 }, 1.0f },
                { "Pulsation 1/8",        { 0.45f, 0.70f, 0.5f, 0.0f, 5.0f / 11, 0 }, 1.0f },
                { "Pulsation 1/16",       { 0.45f, 0.70f, 0.7f, 0.0f, 2.0f / 11, 0 }, 1.0f },
                { "Gate 1/16",            { 0.45f, 1.00f, 1.0f, 0.0f, 2.0f / 11, 0 }, 1.0f },
                { "Pan 1/4",              { 0.45f, 0.70f, 0.3f, 1.0f, 8.0f / 11, 0 }, 1.0f },
                { "Vibe lent",            { 0.25f, 0.30f, 0.0f, 0.4f, 0.0f, 0 }, 1.0f },
                { "Rotatif",              { 0.55f, 0.50f, 0.0f, 0.7f, 0.0f, 0 }, 1.0f },
                { "Respiration",          { 0.10f, 0.35f, 0.0f, 0.2f, 0.0f, 0 }, 1.0f } } },

            { "Lo-fi", "bande usée, grain numérique", false,
              { { { "Bits",       [] (float v) { return juce::String (juce::roundToInt (lin (v, 3, 16))) + " bits"; } },
                  { "Échantillon.",[] (float v) { return "/" + juce::String (juce::roundToInt (expMap (v, 1, 40))); } },
                  { "Wow",        [] (float v) { return pct (v); } },
                  { "Flutter",    [] (float v) { return pct (v); } },
                  { "Ton",        [] (float v) { return hzText (expMap (v, 800, 20000)); } },
                  { "Saturation", [] (float v) { return pct (v); } } } },
              { { "Cassette",             { 1.0f, 0.0f, 0.35f, 0.25f, 0.55f, 0.30f }, 1.0f },
                { "Vinyle chaud",         { 1.0f, 0.0f, 0.20f, 0.10f, 0.50f, 0.20f }, 1.0f },
                { "Bande fatiguée",       { 1.0f, 0.0f, 0.65f, 0.45f, 0.40f, 0.40f }, 1.0f },
                { "12 bits vintage",      { 0.70f, 0.25f, 0.10f, 0.05f, 0.62f, 0.15f }, 1.0f },
                { "8 bits",               { 0.38f, 0.35f, 0.00f, 0.00f, 0.80f, 0.10f }, 1.0f },
                { "Console de jeu",       { 0.20f, 0.55f, 0.00f, 0.00f, 0.90f, 0.00f }, 1.0f },
                { "Lo-fi hip-hop",        { 0.75f, 0.15f, 0.30f, 0.20f, 0.45f, 0.35f }, 1.0f },
                { "Radio AM",             { 0.85f, 0.10f, 0.15f, 0.30f, 0.25f, 0.45f }, 1.0f },
                { "Mémoire floue",        { 0.90f, 0.05f, 0.85f, 0.60f, 0.35f, 0.25f }, 1.0f },
                { "Écrasé",               { 0.15f, 0.75f, 0.10f, 0.10f, 0.70f, 0.60f }, 1.0f } } },

            { "Grain", "granulaire : grains, nuages, échos", true,
              { { { "Grains",     [] (float v) { return msText (expMap (v, 10, 500)); } },
                  { "Densité",    [] (float v) { return pct (v); } },
                  { "Dispersion", [] (float v) { return pct (v); } },
                  { "Hauteurs",   [] (float v) { return pct (v); } },
                  { "Inversion",  [] (float v) { return pct (v); } },
                  { "Réinjection",[] (float v) { return pct (v); } } } },
              { { "Nuage doux",           { 0.55f, 0.55f, 0.35f, 0.00f, 0.10f, 0.20f }, 0.40f },
                { "Scintillement",        { 0.35f, 0.65f, 0.30f, 0.80f, 0.00f, 0.25f }, 0.35f },
                { "Octaves cristal",      { 0.45f, 0.60f, 0.45f, 1.00f, 0.20f, 0.35f }, 0.40f },
                { "Halo grave",           { 0.65f, 0.45f, 0.40f, 0.45f, 0.10f, 0.30f }, 0.35f },
                { "Poussière",            { 0.15f, 0.85f, 0.70f, 0.30f, 0.40f, 0.10f }, 0.35f },
                { "Bégaiement",           { 0.30f, 0.20f, 0.05f, 0.00f, 0.00f, 0.60f }, 0.45f },
                { "Rêve inversé",         { 0.70f, 0.50f, 0.50f, 0.30f, 1.00f, 0.40f }, 0.45f },
                { "Texture large",        { 0.50f, 0.75f, 1.00f, 0.40f, 0.30f, 0.30f }, 0.40f },
                { "Glitch",               { 0.12f, 0.40f, 0.90f, 0.70f, 0.60f, 0.15f }, 0.45f },
                { "Ambiance infinie",     { 0.80f, 0.70f, 0.60f, 0.60f, 0.40f, 0.75f }, 0.50f } } },

            { "Givre", "échos inversés et transposés", true,
              { { { "Temps",      [] (float v) { return msText (expMap (v, 40, 1500)); } },
                  { "Hauteur",    [] (float v) { static const int s[] = { -12, -7, -5, 0, 5, 7, 12, 19, 24 };
                                                 const int x = s[step (v, 9)]; return (x > 0 ? "+" : "") + juce::String (x) + " st"; } },
                  { "Réinjection",[] (float v) { return pct (v); } },
                  { "Inversion",  [] (float v) { return pct (v); } },
                  { "Largeur",    [] (float v) { return pct (v); } },
                  { "Ton",        [] (float v) { return hzText (expMap (v, 1000, 18000)); } } } },
              { { "Cristal octave",       { 0.50f, 6.0f / 8, 0.40f, 1.00f, 0.8f, 0.70f }, 0.40f },
                { "Quinte scintillante",  { 0.45f, 5.0f / 8, 0.35f, 1.00f, 0.9f, 0.75f }, 0.35f },
                { "Double octave",        { 0.40f, 1.0f,     0.30f, 1.00f, 1.0f, 0.80f }, 0.30f },
                { "Inversé sans hauteur", { 0.55f, 3.0f / 8, 0.45f, 1.00f, 0.7f, 0.60f }, 0.40f },
                { "Sous-octave sombre",   { 0.60f, 0.0f,     0.35f, 1.00f, 0.6f, 0.40f }, 0.35f },
                { "Cascade",              { 0.35f, 6.0f / 8, 0.75f, 1.00f, 1.0f, 0.65f }, 0.40f },
                { "Écho avant",           { 0.50f, 3.0f / 8, 0.40f, 0.00f, 0.8f, 0.70f }, 0.35f },
                { "Glace fine",           { 0.25f, 7.0f / 8, 0.30f, 1.00f, 1.0f, 0.90f }, 0.30f },
                { "Lent et immense",      { 0.85f, 6.0f / 8, 0.60f, 1.00f, 1.0f, 0.55f }, 0.45f },
                { "Quarte rêveuse",       { 0.55f, 4.0f / 8, 0.50f, 0.70f, 0.9f, 0.65f }, 0.40f } } },

            { "Delay", "écho à bande calé sur le tempo", true,
              { { { "Temps",      [] (float v) { return divisionNames()[step (v, 12)]; } },
                  { "Réinjection",[] (float v) { return pct (v); } },
                  { "Ping-pong",  [] (float v) { return pct (v); } },
                  { "Ton",        [] (float v) { return v < 0.45f ? fr ("Sombre") : (v > 0.55f ? fr ("Brillant") : fr ("Neutre")); } },
                  { "Usure",      [] (float v) { return pct (v); } },
                  { "Diffusion",  [] (float v) { return pct (v); } },
                  { "Ducking",    [] (float v) { return pct (v); } } } },
              { { "Écho 1/8 pointé",      { 6.0f / 11, 0.40f, 0.30f, 0.50f, 0.20f, 0.10f }, 0.30f },
                { "Ping-pong 1/8",        { 5.0f / 11, 0.45f, 1.00f, 0.55f, 0.10f, 0.05f }, 0.30f },
                { "Bande vintage",        { 8.0f / 11, 0.45f, 0.20f, 0.35f, 0.60f, 0.15f }, 0.30f },
                { "Slapback",             { 0.0f,      0.10f, 0.00f, 0.55f, 0.20f, 0.00f }, 0.35f },
                { "Écho sombre",          { 6.0f / 11, 0.55f, 0.40f, 0.20f, 0.40f, 0.20f }, 0.30f },
                { "Dub",                  { 8.0f / 11, 0.75f, 0.50f, 0.30f, 0.50f, 0.20f }, 0.35f },
                { "Nuage diffus",         { 5.0f / 11, 0.60f, 0.70f, 0.45f, 0.30f, 0.90f }, 0.35f },
                { "Triolets",             { 4.0f / 11, 0.40f, 0.80f, 0.55f, 0.10f, 0.10f }, 0.30f },
                { "Longue traîne",        { 10.0f / 11,0.70f, 0.60f, 0.40f, 0.35f, 0.50f }, 0.30f },
                { "Brillant numérique",   { 5.0f / 11, 0.35f, 0.50f, 0.85f, 0.00f, 0.00f }, 0.25f } } },

            { "Réverb", "espace profond et vivant", true,
              { { { "Durée",      [] (float v) { return juce::String (expMap (v, 0.2f, 20.0f), 1) + " s"; } },
                  { "Pré-délai",  [] (float v) { return msText (lin (v, 0, 250)); } },
                  { "Amorti",     [] (float v) { return hzText (expMap (1.0f - v, 1500, 18000)); } },
                  { "Modulation", [] (float v) { return pct (v); } },
                  { "Shimmer",    [] (float v) { return pct (v); } },
                  { "Espace",     [] (float v) { return pct (v); } },
                  { "Pré-délai tempo", [] (float v) { static const char* n[] = { "Libre", "1/64", "1/32", "1/16", "1/8" }; return fr (n[step (v, 5)]); } },
                  { "Durée tempo",     [] (float v) { static const char* n[] = { "Libre", "1/2 mesure", "1 mesure", "2 mesures", "4 mesures" }; return fr (n[step (v, 5)]); } } } },
              { { "Pièce intime",         { 0.20f, 0.04f, 0.45f, 0.20f, 0.0f, 0.25f }, 0.25f },
                { "Studio boisé",         { 0.30f, 0.08f, 0.55f, 0.25f, 0.0f, 0.40f }, 0.25f },
                { "Plate brillante",      { 0.40f, 0.05f, 0.20f, 0.35f, 0.0f, 0.55f }, 0.30f },
                { "Salle de concert",     { 0.55f, 0.12f, 0.45f, 0.30f, 0.0f, 0.75f }, 0.30f },
                { "Cathédrale",           { 0.78f, 0.15f, 0.50f, 0.35f, 0.0f, 1.00f }, 0.35f },
                { "Shimmer céleste",      { 0.75f, 0.10f, 0.35f, 0.45f, 0.70f, 0.90f }, 0.40f },
                { "Nuage infini",         { 0.95f, 0.05f, 0.40f, 0.60f, 0.20f, 1.00f }, 0.45f },
                { "Sombre et lointain",   { 0.65f, 0.20f, 0.85f, 0.30f, 0.0f, 0.80f }, 0.35f },
                { "Vintage chorusé",      { 0.50f, 0.06f, 0.55f, 0.90f, 0.0f, 0.60f }, 0.30f },
                { "Ambiance subtile",     { 0.35f, 0.10f, 0.50f, 0.25f, 0.0f, 0.50f }, 0.15f } } },

            { "Stéréo", "largeur et placement", false,
              { { { "Largeur",    [] (float v) { return pct (v * 2.0f); } },
                  { "Haas",       [] (float v) { return msText (lin (v, 0, 30)); } },
                  { "Graves mono",[] (float v) { return v < 0.01f ? fr ("Off") : hzText (expMap (v, 40, 400)); } },
                  { "Balance",    [] (float v) { const int b = juce::roundToInt (lin (v, -100, 100)); return b == 0 ? juce::String ("C") : (b < 0 ? fr ("G ") + juce::String (-b) : fr ("D ") + juce::String (b)); } },
                  { "", {} }, { "", {} } } },
              { { "Neutre",               { 0.50f, 0.00f, 0.00f, 0.5f, 0, 0 }, 1.0f },
                { "Large",                { 0.75f, 0.00f, 0.35f, 0.5f, 0, 0 }, 1.0f },
                { "Très large",           { 0.95f, 0.15f, 0.45f, 0.5f, 0, 0 }, 1.0f },
                { "Mono",                 { 0.00f, 0.00f, 0.00f, 0.5f, 0, 0 }, 1.0f },
                { "Étroit",               { 0.30f, 0.00f, 0.00f, 0.5f, 0, 0 }, 1.0f },
                { "Haas gauche",          { 0.50f, 0.40f, 0.30f, 0.4f, 0, 0 }, 1.0f },
                { "Haas droite",          { 0.50f, 0.40f, 0.30f, 0.6f, 0, 0 }, 1.0f },
                { "Graves au centre",     { 0.65f, 0.00f, 0.55f, 0.5f, 0, 0 }, 1.0f },
                { "À gauche",             { 0.50f, 0.00f, 0.00f, 0.25f, 0, 0 }, 1.0f },
                { "À droite",             { 0.50f, 0.00f, 0.00f, 0.75f, 0, 0 }, 1.0f } } },
        };
        return d;
    }

    // Presets retravaillés : chacun a un vrai caractère, bien différent des autres
    inline const std::vector<TypeDef>& defs()
    {
        static const std::vector<TypeDef> d = []
        {
            auto v = baseDefs();
            auto set = [&v] (int type, std::vector<Preset> p) { v[(size_t) type].presets = std::move (p); };
            set (Saturation, {
                { "Velours",              { 0.50f, 0.18f, 0.55f, 0.00f, 0.05f, 0.50f }, 1.0f },
                { "Lampe chaude",         { 0.00f, 0.42f, 0.42f, 0.12f, 0.30f, 0.50f }, 1.0f },
                { "Cassette 70s",         { 0.75f, 0.50f, 0.25f, 0.25f, 0.20f, 0.50f }, 1.0f },
                { "Transfo grave",        { 0.25f, 0.60f, 0.35f, 0.00f, 0.50f, 0.48f }, 1.0f },
                { "Brillance saturée",    { 0.50f, 0.55f, 0.90f, 0.45f, 0.10f, 0.45f }, 0.7f },
                { "Fuzz doux",            { 1.00f, 0.35f, 0.40f, 0.30f, 0.20f, 0.50f }, 0.6f },
                { "Fuzz destroy",         { 1.00f, 1.00f, 0.70f, 0.55f, 1.00f, 0.38f }, 1.0f },
                { "Parallèle épais",      { 0.25f, 0.85f, 0.50f, 0.20f, 0.70f, 0.45f }, 0.35f },
                { "Radio cassée",         { 0.75f, 0.80f, 0.95f, 0.85f, 0.60f, 0.42f }, 1.0f },
                { "Souffle analogique",   { 0.75f, 0.25f, 0.10f, 0.05f, 0.15f, 0.52f }, 1.0f } });
            set (Filter, {
                { "Coussin",              { 0.00f, 0.78f, 0.10f, 0.00f, 0.00f, 0.30f }, 1.0f },
                { "Sous l'eau",           { 0.00f, 0.45f, 0.35f, 0.10f, 0.15f, 0.20f }, 1.0f },
                { "Balayage 8 temps",     { 0.00f, 0.60f, 0.55f, 0.15f, 0.70f, 0.18f }, 1.0f },
                { "Wah funky",            { 0.50f, 0.62f, 0.75f, 0.25f, 0.60f, 0.72f }, 1.0f },
                { "Acide",             { 1.00f, 0.40f, 0.92f, 0.60f, 0.45f, 0.55f }, 1.0f },
                { "Ladder crémeux",       { 1.00f, 0.62f, 0.30f, 0.35f, 0.00f, 0.30f }, 1.0f },
                { "Fin et aérien",        { 0.25f, 0.50f, 0.20f, 0.00f, 0.00f, 0.30f }, 1.0f },
                { "Talkie-walkie",        { 0.50f, 0.68f, 0.60f, 0.80f, 0.00f, 0.30f }, 1.0f },
                { "Creux qui bouge",      { 0.75f, 0.55f, 0.65f, 0.00f, 0.80f, 0.35f }, 1.0f },
                { "Pulsation rapide",     { 0.00f, 0.55f, 0.60f, 0.20f, 0.90f, 0.88f }, 1.0f } });
            set (Compressor, {
                { "Colle de bus",         { 0.75f, 0.20f, 0.60f, 0.45f, 0.08f, 0.30f }, 1.0f },
                { "Punch de batterie",    { 0.55f, 0.45f, 0.75f, 0.25f, 0.20f, 0.20f }, 1.0f },
                { "Écrasement total",     { 0.20f, 0.95f, 0.05f, 0.30f, 0.50f, 0.60f }, 1.0f },
                { "Opto velours",         { 0.60f, 0.30f, 0.50f, 0.70f, 0.15f, 0.95f }, 1.0f },
                { "Pompage house",        { 0.35f, 0.75f, 0.10f, 0.35f, 0.30f, 0.40f }, 1.0f },
                { "Parallèle New York",   { 0.30f, 0.85f, 0.15f, 0.40f, 0.45f, 0.50f }, 0.4f },
                { "Sustain infini",       { 0.25f, 0.60f, 0.40f, 0.85f, 0.40f, 0.80f }, 1.0f },
                { "Attaque préservée",    { 0.60f, 0.50f, 0.90f, 0.15f, 0.12f, 0.10f }, 1.0f },
                { "Limiteur",             { 0.90f, 1.00f, 0.00f, 0.30f, 0.12f, 0.05f }, 1.0f },
                { "Vintage FET",          { 0.45f, 0.70f, 0.20f, 0.30f, 0.30f, 0.60f }, 1.0f } });
            set (EQ, {
                { "Plat",                 { 0.50f, 0.50f, 0.50f, 0.50f, 0.50f, 0.00f }, 1.0f },
                { "Chaleur boisée",       { 0.66f, 0.60f, 0.42f, 0.38f, 0.40f, 0.05f }, 1.0f },
                { "Cristallin",           { 0.42f, 0.40f, 0.60f, 0.68f, 0.78f, 0.30f }, 1.0f },
                { "Sourire radio",        { 0.70f, 0.36f, 0.45f, 0.66f, 0.60f, 0.00f }, 1.0f },
                { "Vintage étroit",       { 0.30f, 0.62f, 0.66f, 0.28f, 0.20f, 0.60f }, 1.0f },
                { "Sub profond",          { 0.78f, 0.45f, 0.50f, 0.48f, 0.50f, 0.00f }, 1.0f },
                { "Voix en avant",        { 0.45f, 0.40f, 0.72f, 0.56f, 0.55f, 0.35f }, 1.0f },
                { "Feutré",               { 0.55f, 0.55f, 0.38f, 0.25f, 0.20f, 0.10f }, 1.0f },
                { "Air de studio",        { 0.50f, 0.48f, 0.52f, 0.56f, 0.85f, 0.20f }, 1.0f },
                { "Vieux téléphone",      { 0.10f, 0.70f, 0.75f, 0.10f, 0.05f, 0.75f }, 1.0f } });
            set (Chorus, {
                { "Double voix I",          { 0.00f, 0.35f, 0.30f, 0.60f, 0.70f, 0.30f }, 0.5f },
                { "Double voix II",         { 0.33f, 0.45f, 0.50f, 0.60f, 0.85f, 0.30f }, 0.5f },
                { "Synthé 80s brillant",        { 0.67f, 0.50f, 0.60f, 0.85f, 1.00f, 0.25f }, 0.55f },
                { "Ensemble cordes",      { 1.00f, 0.30f, 0.65f, 0.55f, 1.00f, 0.45f }, 0.6f },
                { "Doublage large",       { 0.00f, 0.15f, 0.20f, 0.70f, 1.00f, 0.70f }, 0.4f },
                { "Vibrato mélancolique", { 0.00f, 0.62f, 0.70f, 0.60f, 0.40f, 0.15f }, 1.0f },
                { "Chorus sombre 80s",    { 0.33f, 0.40f, 0.55f, 0.15f, 0.80f, 0.35f }, 0.5f },
                { "Haut-parleur tournant lent",          { 1.00f, 0.25f, 0.80f, 0.50f, 1.00f, 0.20f }, 0.6f },
                { "Tourbillon rapide",    { 1.00f, 0.85f, 0.70f, 0.70f, 1.00f, 0.30f }, 0.55f },
                { "Chorus discret",       { 0.33f, 0.30f, 0.20f, 0.75f, 0.60f, 0.35f }, 0.3f } });
            set (Flanger, {
                { "Jet classique",        { 0.35f, 0.75f, 0.85f, 0.30f, 0.50f, 0.00f }, 0.5f },
                { "Avion négatif",        { 0.35f, 0.75f, 0.12f, 0.30f, 0.50f, 0.00f }, 0.5f },
                { "Through-zero studio",  { 0.25f, 0.85f, 0.65f, 0.25f, 0.25f, 1.00f }, 0.55f },
                { "Robot métallique",     { 0.60f, 0.25f, 0.97f, 0.05f, 0.50f, 0.00f }, 0.5f },
                { "Lent et stéréo",       { 0.10f, 0.65f, 0.70f, 0.45f, 1.00f, 0.00f }, 0.45f },
                { "Vent",                 { 0.18f, 1.00f, 0.50f, 0.90f, 0.70f, 0.00f }, 0.4f },
                { "Nerveux",              { 0.82f, 0.40f, 0.75f, 0.20f, 0.30f, 0.00f }, 0.45f },
                { "Résonateur figé",      { 0.00f, 0.05f, 0.95f, 0.60f, 0.00f, 0.00f }, 0.5f },
                { "Flanger doux",         { 0.30f, 0.50f, 0.58f, 0.80f, 0.80f, 0.00f }, 0.35f },
                { "Balayage géant",       { 0.15f, 1.00f, 0.90f, 0.50f, 0.50f, 1.00f }, 0.6f } });
            set (Phaser, {
                { "Phase orange",             { 0.25f, 0.30f, 0.60f, 0.55f, 0.45f, 0.20f }, 0.5f },
                { "Phase caillou",          { 0.25f, 0.38f, 0.75f, 0.85f, 0.50f, 0.50f }, 0.5f },
                { "Vibe ondulante",              { 0.50f, 0.50f, 0.60f, 0.50f, 0.35f, 0.25f }, 0.65f },
                { "12 étages liquide",    { 1.00f, 0.15f, 0.90f, 0.80f, 0.40f, 0.60f }, 0.5f },
                { "Disco rapide",         { 0.50f, 0.72f, 0.55f, 0.65f, 0.60f, 0.30f }, 0.5f },
                { "Négatif creux",        { 0.75f, 0.25f, 0.70f, 0.10f, 0.45f, 0.50f }, 0.5f },
                { "Lent cosmique",        { 1.00f, 0.05f, 1.00f, 0.70f, 0.35f, 1.00f }, 0.55f },
                { "Aigu scintillant",     { 0.75f, 0.40f, 0.60f, 0.80f, 0.85f, 0.70f }, 0.45f },
                { "Discret",              { 0.25f, 0.25f, 0.35f, 0.50f, 0.50f, 0.40f }, 0.3f },
                { "Tremblement",          { 0.50f, 0.95f, 0.40f, 0.60f, 0.55f, 0.20f }, 0.5f } });
            set (Tremolo, {
                { "Ampli années 60",      { 0.62f, 0.50f, 0.00f, 0.00f, 0.00f, 0.0f }, 1.0f },
                { "Auto-pan lent",        { 0.25f, 0.70f, 0.00f, 1.00f, 0.00f, 0.0f }, 1.0f },
                { "Hélicoptère",          { 0.80f, 0.90f, 0.90f, 0.00f, 0.00f, 0.0f }, 1.0f },
                { "Gate 1/16",            { 0.50f, 1.00f, 1.00f, 0.00f, 2.0f / 11, 0.0f }, 1.0f },
                { "Pulse 1/8 pointé",     { 0.50f, 0.75f, 0.60f, 0.00f, 6.0f / 11, 0.0f }, 1.0f },
                { "Ping-pong 1/8",        { 0.50f, 0.80f, 0.40f, 1.00f, 5.0f / 11, 0.0f }, 1.0f },
                { "Respiration lente",    { 0.50f, 0.55f, 0.00f, 0.30f, 1.0f, 0.0f }, 1.0f },
                { "Vibe rotatif",         { 0.66f, 0.45f, 0.00f, 0.70f, 0.00f, 0.0f }, 1.0f },
                { "Triolets hachés",      { 0.50f, 0.95f, 0.85f, 0.20f, 4.0f / 11, 0.0f }, 1.0f },
                { "Pan scintillant",      { 0.85f, 0.35f, 0.00f, 1.00f, 0.00f, 0.0f }, 1.0f } });
            set (Lofi, {
                { "Cassette usée",        { 1.00f, 0.00f, 0.45f, 0.30f, 0.50f, 0.30f }, 1.0f },
                { "Vinyle 1972",          { 1.00f, 0.00f, 0.20f, 0.10f, 0.42f, 0.25f }, 1.0f },
                { "Sampler 12 bits",      { 0.68f, 0.22f, 0.00f, 0.00f, 0.62f, 0.10f }, 1.0f },
                { "Arcade 8 bits",        { 0.38f, 0.40f, 0.00f, 0.00f, 0.85f, 0.00f }, 1.0f },
                { "Console 4 bits",       { 0.08f, 0.60f, 0.00f, 0.00f, 0.95f, 0.00f }, 1.0f },
                { "Lo-fi hip-hop",        { 0.80f, 0.12f, 0.30f, 0.20f, 0.42f, 0.35f }, 1.0f },
                { "Radio lointaine",      { 0.85f, 0.08f, 0.15f, 0.40f, 0.20f, 0.50f }, 1.0f },
                { "VHS fondue",           { 0.90f, 0.05f, 0.90f, 0.50f, 0.35f, 0.20f }, 1.0f },
                { "Mémoire qui flanche",  { 0.75f, 0.18f, 0.70f, 0.70f, 0.30f, 0.40f }, 1.0f },
                { "Bitcrush brutal",      { 0.15f, 0.85f, 0.00f, 0.00f, 0.70f, 0.70f }, 1.0f } });
            set (Portal, {
                { "Nuage doux",           { 0.60f, 0.55f, 0.30f, 0.00f, 0.10f, 0.20f }, 0.4f },
                { "Scintillement octave", { 0.35f, 0.70f, 0.35f, 0.85f, 0.00f, 0.25f }, 0.35f },
                { "Halo sous-grave",      { 0.75f, 0.45f, 0.40f, 0.50f, 0.10f, 0.30f }, 0.35f },
                { "Poussière d'étoiles",  { 0.12f, 0.90f, 0.80f, 0.60f, 0.40f, 0.10f }, 0.3f },
                { "Bégaiement",           { 0.30f, 0.15f, 0.03f, 0.00f, 0.00f, 0.65f }, 0.5f },
                { "Rêve à l'envers",      { 0.70f, 0.50f, 0.50f, 0.20f, 1.00f, 0.45f }, 0.5f },
                { "Texture immense",      { 0.85f, 0.80f, 1.00f, 0.40f, 0.30f, 0.60f }, 0.55f },
                { "Glitch nerveux",       { 0.08f, 0.40f, 0.95f, 0.90f, 0.60f, 0.15f }, 0.45f },
                { "Granulaire figé",      { 0.95f, 0.90f, 0.10f, 0.00f, 0.00f, 0.85f }, 0.5f },
                { "Chœur de grains",      { 0.50f, 0.85f, 0.60f, 0.70f, 0.20f, 0.40f }, 0.45f } });
            set (Crystal, {
                { "Cristal octave",       { 0.50f, 6.0f / 8, 0.40f, 1.00f, 0.80f, 0.70f }, 0.4f },
                { "Quinte glacée",        { 0.40f, 5.0f / 8, 0.35f, 1.00f, 0.90f, 0.80f }, 0.35f },
                { "Fée double octave",    { 0.30f, 1.0f,     0.30f, 1.00f, 1.00f, 0.90f }, 0.3f },
                { "Miroir inversé",       { 0.60f, 3.0f / 8, 0.50f, 1.00f, 0.70f, 0.55f }, 0.45f },
                { "Abysse",               { 0.70f, 0.0f,     0.45f, 1.00f, 0.60f, 0.35f }, 0.4f },
                { "Cascade infinie",      { 0.35f, 6.0f / 8, 0.85f, 1.00f, 1.00f, 0.65f }, 0.4f },
                { "Écho brillant",        { 0.45f, 6.0f / 8, 0.40f, 0.00f, 0.80f, 0.80f }, 0.35f },
                { "Quarte rêveuse",       { 0.55f, 4.0f / 8, 0.55f, 0.70f, 0.90f, 0.60f }, 0.4f },
                { "Grains de glace",      { 0.15f, 7.0f / 8, 0.30f, 1.00f, 1.00f, 0.95f }, 0.3f },
                { "Cathédrale de verre",  { 0.90f, 6.0f / 8, 0.65f, 1.00f, 1.00f, 0.50f }, 0.5f } });
            set (Delay, {
                { "Écho 1/8 pointé",      { 6.0f / 11, 0.40f, 0.30f, 0.55f, 0.15f, 0.05f }, 0.3f },
                { "Ping-pong 1/8",        { 5.0f / 11, 0.45f, 1.00f, 0.60f, 0.05f, 0.00f }, 0.3f },
                { "Bande vintage",        { 8.0f / 11, 0.50f, 0.20f, 0.35f, 0.60f, 0.15f }, 0.3f },
                { "Slapback",             { 0.0f,      0.08f, 0.00f, 0.60f, 0.25f, 0.00f }, 0.35f },
                { "Dub infini",           { 9.0f / 11, 0.85f, 0.60f, 0.25f, 0.50f, 0.25f }, 0.35f },
                { "Nuage diffus",         { 5.0f / 11, 0.65f, 0.70f, 0.45f, 0.25f, 0.95f }, 0.35f },
                { "Triolets rebondis",    { 4.0f / 11, 0.45f, 0.85f, 0.60f, 0.10f, 0.05f }, 0.3f },
                { "Long et sombre",       { 10.0f / 11,0.70f, 0.50f, 0.15f, 0.35f, 0.50f }, 0.3f },
                { "Grains rapides",       { 2.0f / 11, 0.60f, 0.90f, 0.75f, 0.00f, 0.30f }, 0.25f },
                { "Écho lointain",        { 1.0f,      0.55f, 0.40f, 0.30f, 0.45f, 0.70f }, 0.25f } });
            set (Reverb, {
                { "Petite pièce",         { 0.20f, 0.03f, 0.40f, 0.15f, 0.00f, 0.20f }, 0.2f },
                { "Studio boisé",         { 0.32f, 0.06f, 0.55f, 0.20f, 0.00f, 0.40f }, 0.25f },
                { "Plate brillante",      { 0.45f, 0.08f, 0.15f, 0.30f, 0.00f, 0.55f }, 0.3f },
                { "Salle de concert",     { 0.58f, 0.12f, 0.45f, 0.30f, 0.00f, 0.80f }, 0.3f },
                { "Cathédrale",           { 0.78f, 0.18f, 0.50f, 0.35f, 0.00f, 1.00f }, 0.35f },
                { "Shimmer céleste",      { 0.75f, 0.10f, 0.30f, 0.50f, 0.75f, 0.90f }, 0.4f },
                { "Nuage infini",         { 0.97f, 0.05f, 0.40f, 0.70f, 0.25f, 1.00f }, 0.5f },
                { "Lointain et sombre",   { 0.66f, 0.35f, 0.85f, 0.25f, 0.00f, 0.85f }, 0.35f },
                { "Ressort vintage",      { 0.35f, 0.00f, 0.30f, 0.90f, 0.00f, 0.30f }, 0.3f },
                { "Gated années 80",      { 0.28f, 0.02f, 0.20f, 0.10f, 0.00f, 1.00f }, 0.4f } });
            set (Stereo, {
                { "Neutre",               { 0.50f, 0.00f, 0.00f, 0.50f, 0, 0 }, 1.0f },
                { "Large",                { 0.75f, 0.00f, 0.35f, 0.50f, 0, 0 }, 1.0f },
                { "Très large",           { 0.95f, 0.15f, 0.45f, 0.50f, 0, 0 }, 1.0f },
                { "Mono",                 { 0.00f, 0.00f, 0.00f, 0.50f, 0, 0 }, 1.0f },
                { "Étroit",               { 0.30f, 0.00f, 0.00f, 0.50f, 0, 0 }, 1.0f },
                { "Haas gauche",          { 0.50f, 0.45f, 0.30f, 0.40f, 0, 0 }, 1.0f },
                { "Haas droite",          { 0.50f, 0.45f, 0.30f, 0.60f, 0, 0 }, 1.0f },
                { "Graves au centre",     { 0.70f, 0.00f, 0.60f, 0.50f, 0, 0 }, 1.0f },
                { "Côté gauche",          { 0.55f, 0.10f, 0.20f, 0.20f, 0, 0 }, 1.0f },
                { "Côté droit",           { 0.55f, 0.10f, 0.20f, 0.80f, 0, 0 }, 1.0f } });
            // ---------------- effets créatifs (v1.3)
            static const char* htRates[] = { "1/2 (halftime)", "2/3", "3/4", "1/3", "1/4 (quart)" };
            static const char* htLens[]  = { "1 temps", "2 temps", "1 mesure", "2 mesures" };
            static const char* keys[]    = { "Plugin", "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            static const char* hScales[] = { "Gamme du plugin", "Majeur", "Mineur", "Penta majeure", "Penta mineure", "Dorien",
                                             "Mineur harmonique", "Accord majeur", "Accord mineur", "Quintes", "Fondamentale", "Naturelle* (sans quarte)", "Phrygien dominant", "Notes jouées (MIDI)" };
            v.push_back ({ "Ralenti", "ralentit le groove sans ralentir le projet", true,
                { { { "Vitesse",   [] (float x) { return juce::String (htRates[step (x, 5)]); } },
                    { "Boucle",    [] (float x) { return fr (htLens[step (x, 4)]); } },
                    { "Mode",      [] (float x) { return x < 0.5f ? fr ("Bande (grave)") : fr ("Garde la hauteur"); } },
                    { "Fondu",     [] (float x) { return juce::String (juce::roundToInt (lin (x, 2, 60))) + " ms"; } },
                    { "Inversion", [] (float x) { return pct (x); } },
                    { "Couleur",   [] (float x) { return hzText (expMap (x, 800, 20000)); } },
                    { "Wobble",    [] (float x) { return pct (x); } },
                    { "Variation", [] (float x) { return pct (x); } } } },
                { { "Halftime classique",   { 0.00f, 0.67f, 0.0f, 0.30f, 0.00f, 1.00f, 0.00f, 0.00f }, 1.0f },
                  { "Halftime propre",      { 0.00f, 0.67f, 1.0f, 0.40f, 0.00f, 1.00f, 0.00f, 0.00f }, 1.0f },
                  { "Trap lourd",           { 0.00f, 1.00f, 0.0f, 0.25f, 0.00f, 0.55f, 0.10f, 0.00f }, 1.0f },
                  { "Vinyle fatigué",       { 0.00f, 0.33f, 0.0f, 0.35f, 0.00f, 0.35f, 0.60f, 0.00f }, 1.0f },
                  { "Triolet bizarre",      { 0.25f, 0.67f, 1.0f, 0.40f, 0.10f, 0.80f, 0.10f, 0.20f }, 1.0f },
                  { "Quart de temps",       { 1.00f, 1.00f, 0.0f, 0.50f, 0.00f, 0.45f, 0.20f, 0.00f }, 1.0f },
                  { "Reverse fantôme",      { 0.00f, 0.33f, 1.0f, 0.50f, 0.70f, 0.70f, 0.00f, 0.20f }, 1.0f },
                  { "Alterné (1 sur 2)",    { 0.00f, 0.67f, 0.0f, 0.35f, 0.00f, 0.90f, 0.00f, 0.50f }, 1.0f },
                  { "Ralenti planant",      { 0.75f, 1.00f, 1.0f, 0.70f, 0.20f, 0.40f, 0.35f, 0.00f }, 0.7f },
                  { "Glissement 3/4",       { 0.50f, 0.33f, 0.0f, 0.30f, 0.00f, 0.75f, 0.25f, 0.15f }, 1.0f } } });

            v.push_back ({ "Shaper", "volume, filtre et pan qui suivent une forme rythmique", false,
                { { { "Forme",      [] (float x) { return fr (shapeNames()[step (x, 12)]); } },
                    { "Vitesse",    [] (float x) { return juce::String (divisionNames()[step (x, 12)]); } },
                    { "Volume",     [] (float x) { return pct (x); } },
                    { "Filtre",     [] (float x) { return pct (x); } },
                    { "Pan",        [] (float x) { return pct (x); } },
                    { "Lissage",    [] (float x) { return juce::String (lin (x, 0.5f, 40.0f), 1) + " ms"; } },
                    { "Décalage",   [] (float x) { return pct (x); } },
                    { "Swing",      [] (float x) { return pct (x); } } } },
                { { "Sidechain 1/4",        { 0.00f, 0.73f, 0.90f, 0.00f, 0.00f, 0.15f, 0.00f, 0.00f }, 1.0f },
                  { "Pompe douce",          { 0.09f, 0.73f, 0.60f, 0.00f, 0.00f, 0.30f, 0.00f, 0.00f }, 1.0f },
                  { "Trance gate",          { 0.27f, 0.91f, 1.00f, 0.20f, 0.00f, 0.08f, 0.00f, 0.00f }, 1.0f },
                  { "Gate 1/16 swing",      { 0.18f, 0.18f, 1.00f, 0.00f, 0.00f, 0.10f, 0.00f, 0.60f }, 1.0f },
                  { "Filtre qui respire",   { 0.55f, 0.91f, 0.20f, 0.90f, 0.00f, 0.50f, 0.00f, 0.00f }, 1.0f },
                  { "Autopan rebond",       { 0.82f, 0.73f, 0.30f, 0.00f, 1.00f, 0.40f, 0.00f, 0.00f }, 1.0f },
                  { "Stutter vocal",        { 0.73f, 0.91f, 1.00f, 0.30f, 0.20f, 0.05f, 0.00f, 0.00f }, 1.0f },
                  { "Syncopé reggaeton",    { 0.91f, 1.00f, 0.85f, 0.25f, 0.30f, 0.12f, 0.00f, 0.00f }, 1.0f },
                  { "Escalier filtré",      { 0.64f, 0.91f, 0.40f, 0.80f, 0.00f, 0.20f, 0.00f, 0.00f }, 1.0f },
                  { "Chaos aléatoire",      { 1.00f, 0.73f, 0.80f, 0.60f, 0.60f, 0.20f, 0.00f, 0.30f }, 1.0f } } });

            v.push_back ({ "Glitch", "bégaiements, reverse, tape stop, crush : motif répétable", false,
                { { { "Densité",    [] (float x) { return pct (x); } },
                    { "Grille",     [] (float x) { return juce::String (divisionNames()[step (x, 12)]); } },
                    { "Stutter",    [] (float x) { return pct (x); } },
                    { "Reverse",    [] (float x) { return pct (x); } },
                    { "Tape stop",  [] (float x) { return pct (x); } },
                    { "Crush",      [] (float x) { return pct (x); } },
                    { "Pitch",      [] (float x) { return pct (x); } },
                    { "Variation",  [] (float x) { return "#" + juce::String (juce::roundToInt (x * 99.0f)); } } } },
                { { "Bégaiement léger",     { 0.25f, 0.45f, 1.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.10f }, 1.0f },
                  { "IDM nerveux",          { 0.60f, 0.18f, 0.80f, 0.40f, 0.20f, 0.40f, 0.50f, 0.37f }, 1.0f },
                  { "Reverse surprise",     { 0.30f, 0.73f, 0.10f, 1.00f, 0.00f, 0.00f, 0.00f, 0.52f }, 1.0f },
                  { "Tape stop trap",       { 0.25f, 0.73f, 0.00f, 0.00f, 1.00f, 0.00f, 0.00f, 0.21f }, 1.0f },
                  { "Chiptune cassé",       { 0.50f, 0.45f, 0.40f, 0.00f, 0.00f, 1.00f, 0.60f, 0.64f }, 1.0f },
                  { "Vocal chop",           { 0.55f, 0.45f, 0.90f, 0.30f, 0.00f, 0.00f, 0.70f, 0.88f }, 1.0f },
                  { "Fin de phrase",        { 0.15f, 0.91f, 0.30f, 0.30f, 0.80f, 0.00f, 0.00f, 0.05f }, 1.0f },
                  { "Tout casser",          { 0.90f, 0.27f, 0.70f, 0.70f, 0.50f, 0.70f, 0.70f, 0.77f }, 1.0f },
                  { "Hachoir 1/32",         { 0.45f, 0.00f, 1.00f, 0.20f, 0.00f, 0.30f, 0.20f, 0.33f }, 1.0f },
                  { "Dub glitch",           { 0.35f, 0.73f, 0.60f, 0.50f, 0.40f, 0.00f, 0.00f, 0.46f }, 0.8f } } });

            v.push_back ({ "Harmoniseur", "fait chanter les notes de la gamme dans le son", false,
                { { { "Tonalité",   [] (float x) { return fr (keys[step (x, 13)]); } },
                    { "Gamme",      [] (float x) { return fr (hScales[step (x, 11)]); } },
                    { "Résonance",  [] (float x) { return pct (x); } },
                    { "Quantité",   [] (float x) { return pct (x); } },
                    { "Focus",      [] (float x) { return hzText (expMap (x, 120, 6000)); } },
                    { "Excitation", [] (float x) { return pct (x); } },
                    { "Brillance",  [] (float x) { return pct (x); } },
                    { "Largeur",    [] (float x) { return pct (x); } } } },
                { { "Chroma doux",          { 0.0f, 0.00f, 0.45f, 0.40f, 0.50f, 0.20f, 0.40f, 0.50f }, 1.0f },
                  { "Gamme qui brille",     { 0.0f, 0.00f, 0.65f, 0.60f, 0.65f, 0.50f, 0.70f, 0.60f }, 1.0f },
                  { "Accord fantôme",       { 0.0f, 0.70f, 0.80f, 0.55f, 0.45f, 0.50f, 0.30f, 0.70f }, 1.0f },
                  { "Penta cristal",        { 0.0f, 0.30f, 0.75f, 0.50f, 0.75f, 0.40f, 0.80f, 0.80f }, 1.0f },
                  { "Grave résonant",       { 0.0f, 0.00f, 0.70f, 0.50f, 0.20f, 0.50f, 0.10f, 0.30f }, 1.0f },
                  { "Drone de quintes",     { 0.0f, 0.90f, 0.85f, 0.60f, 0.35f, 0.40f, 0.30f, 0.50f }, 0.8f },
                  { "Batterie accordée",    { 0.0f, 0.00f, 0.90f, 0.70f, 0.40f, 0.50f, 0.40f, 0.40f }, 1.0f },
                  { "Voix de verre",        { 0.0f, 0.40f, 0.95f, 0.45f, 0.85f, 0.20f, 0.90f, 0.90f }, 0.7f },
                  { "Excité saturé",        { 0.0f, 0.00f, 0.55f, 0.65f, 0.55f, 0.90f, 0.60f, 0.50f }, 1.0f },
                  { "Fondamentale sub",     { 0.0f, 1.00f, 0.70f, 0.60f, 0.10f, 0.30f, 0.00f, 0.00f }, 1.0f } } });
            // ---------------- v1.4 : OTT, Temps & Volume (façon Gross Beat), Chroma
            v[(size_t) Compressor] = { "Compressor", "compression haut + bas sur 3 bandes : son dense et brillant en un geste", false,
                { { { "Profondeur", [] (float x) { return pct (x); } },
                    { "Vitesse",    [] (float x) { const float t = expMap (x, 0.25f, 4.0f); return t < 0.7f ? fr ("Rapide") : (t > 1.5f ? fr ("Lente") : fr ("Normale")); } },
                    { "Écrase (haut)", [] (float x) { return pct (x); } },
                    { "Remonte (bas)", [] (float x) { return pct (x); } },
                    { "Graves",     [] (float x) { return db (lin (x, -12, 12)); } },
                    { "Médiums",    [] (float x) { return db (lin (x, -12, 12)); } },
                    { "Aigus",      [] (float x) { return db (lin (x, -12, 12)); } },
                    { "Sortie",     [] (float x) { return db (lin (x, -12, 12)); } } } },
                { { "Compressor classique",        { 0.60f, 0.50f, 1.00f, 1.00f, 0.50f, 0.50f, 0.50f, 0.50f }, 0.6f },
                  { "Compressor à fond",           { 1.00f, 0.50f, 1.00f, 1.00f, 0.50f, 0.50f, 0.50f, 0.42f }, 1.0f },
                  { "Juste un peu",         { 0.25f, 0.50f, 1.00f, 1.00f, 0.50f, 0.50f, 0.50f, 0.50f }, 1.0f },
                  { "Écrase seulement",     { 0.70f, 0.40f, 1.00f, 0.00f, 0.50f, 0.50f, 0.50f, 0.55f }, 1.0f },
                  { "Remonte les détails",  { 0.70f, 0.60f, 0.00f, 1.00f, 0.50f, 0.50f, 0.50f, 0.45f }, 1.0f },
                  { "Basse dense",          { 0.70f, 0.40f, 1.00f, 0.80f, 0.62f, 0.45f, 0.40f, 0.48f }, 0.8f },
                  { "Aigus scintillants",   { 0.65f, 0.35f, 0.80f, 1.00f, 0.42f, 0.50f, 0.65f, 0.47f }, 0.7f },
                  { "Vocal en avant",       { 0.50f, 0.45f, 0.90f, 0.70f, 0.42f, 0.58f, 0.55f, 0.50f }, 0.6f },
                  { "Pompe lente",          { 0.80f, 0.90f, 1.00f, 0.60f, 0.50f, 0.50f, 0.50f, 0.48f }, 1.0f },
                  { "Colle de bus",         { 0.35f, 0.60f, 1.00f, 0.30f, 0.50f, 0.50f, 0.50f, 0.50f }, 1.0f } } };

            static const char* cycleNames[] = { "1 temps", "2 temps", "1 mesure", "2 mesures" };
            v[(size_t) Shaper] = { "Shape", "rejoue le passé (halftime, reverse, stutter…) et sculpte le volume", false,
                { { { "Slot temps",  [] (float x) { return fr (timeSlotNames()[step (x, 13)]); } },
                    { "Slot volume", [] (float x) { return fr (volumeSlotNames()[step (x, 13)]); } },
                    { "Mix temps",   [] (float x) { return pct (x); } },
                    { "Mix volume",  [] (float x) { return pct (x); } },
                    { "Cycle",       [] (float x) { return fr (cycleNames[step (x, 4)]); } },
                    { "Lissage",     [] (float x) { return juce::String (lin (x, 0.5f, 40.0f), 1) + " ms"; } },
                    { "Filtre",      [] (float x) { return pct (x); } },
                    { "Pan",         [] (float x) { return pct (x); } } } },
                { { "Halftime",             { 2 / 12.f, 1 / 12.f, 1.0f, 0.0f, 0.67f, 0.2f, 0.0f, 0.0f }, 1.0f },
                  { "Reverse fin de mesure",{ 9 / 12.f, 1 / 12.f, 1.0f, 0.0f, 0.67f, 0.2f, 0.0f, 0.0f }, 1.0f },
                  { "Stutter 1/8",          { 5 / 12.f, 1 / 12.f, 1.0f, 0.0f, 0.33f, 0.1f, 0.0f, 0.0f }, 1.0f },
                  { "Tape stop",            { 7 / 12.f, 1 / 12.f, 1.0f, 0.0f, 0.67f, 0.2f, 0.0f, 0.0f }, 1.0f },
                  { "Sidechain",            { 1 / 12.f, 1 / 12.f, 0.0f, 0.9f, 0.00f, 0.15f, 0.0f, 0.0f }, 1.0f },
                  { "Trance gate",          { 1 / 12.f, 4 / 12.f, 0.0f, 1.0f, 0.33f, 0.08f, 0.2f, 0.0f }, 1.0f },
                  { "Halftime + pompe",     { 2 / 12.f, 2 / 12.f, 1.0f, 0.6f, 0.67f, 0.3f, 0.0f, 0.0f }, 1.0f },
                  { "Triolets hachés",      { 10 / 12.f, 9 / 12.f, 1.0f, 0.8f, 0.33f, 0.1f, 0.0f, 0.2f }, 1.0f },
                  { "Scratch",              { 12 / 12.f, 1 / 12.f, 1.0f, 0.0f, 0.33f, 0.3f, 0.0f, 0.0f }, 1.0f },
                  { "Filtre qui respire",   { 1 / 12.f, 7 / 12.f, 0.0f, 0.3f, 0.67f, 0.5f, 0.9f, 0.0f }, 1.0f } } };

            static const char* padStyles[] = { "Nappe douce", "Cordes", "Chœur", "Glace (shimmer)", "Sombre", "Granulaire" };
            v.push_back ({ "Nappe", "transforme n'importe quel one-shot en pad : gel, montée douce, chœur et espace", true,
                { { { "Style",     [] (float x) { return fr (padStyles[step (x, 6)]); } },
                    { "Taille",    [] (float x) { return pct (x); } },
                    { "Montée",    [] (float x) { return msText (expMap (x, 30, 3000)); } },
                    { "Gel",       [] (float x) { return pct (x); } },
                    { "Brillance", [] (float x) { const float t = lin (x, -1, 1); return t < -0.05f ? fr ("Sombre") : (t > 0.05f ? fr ("Brillant") : fr ("Neutre")); } },
                    { "Mouvement", [] (float x) { return pct (x); } },
                    { "Octave",    [] (float x) { static const char* n[] = { "-1 octave", "Aucune", "+1 octave", "-1 et +1" }; return fr (n[step (x, 4)]); } },
                    { "Largeur",   [] (float x) { return pct (x); } } } },
                { { "Nappe de one-shot",    { 0.0f, 0.60f, 0.45f, 0.55f, 0.50f, 0.35f, 1 / 3.f, 0.80f }, 0.85f },
                  { "Cordes lentes",        { 0.2f, 0.55f, 0.65f, 0.40f, 0.45f, 0.55f, 1 / 3.f, 0.90f }, 0.85f },
                  { "Chœur d'église",       { 0.4f, 0.75f, 0.55f, 0.50f, 0.50f, 0.30f, 1 / 3.f, 0.85f }, 0.80f },
                  { "Glace qui brille",     { 0.6f, 0.80f, 0.40f, 0.65f, 0.75f, 0.40f, 2 / 3.f, 1.00f }, 0.80f },
                  { "Sous-marin sombre",    { 0.8f, 0.70f, 0.60f, 0.60f, 0.20f, 0.25f, 0.0f, 0.80f }, 0.85f },
                  { "Grains en suspension", { 1.0f, 0.55f, 0.35f, 0.75f, 0.55f, 0.70f, 1 / 3.f, 0.95f }, 0.85f },
                  { "Gel infini",           { 0.0f, 0.90f, 0.30f, 1.00f, 0.50f, 0.20f, 1 / 3.f, 0.90f }, 1.00f },
                  { "Ambient (octaves)",    { 0.6f, 0.85f, 0.70f, 0.70f, 0.60f, 0.45f, 1.0f, 1.00f }, 0.80f } } });
            static const char* liqTypes[] = { "Flanger", "Chorus", "Phaser" };
            static const char* liqLens[] = { "1/2 mesure", "1 mesure", "2 mesures", "4 mesures" };
            v.push_back ({ "Wobble", "dessine la courbe qui fait bouger un flanger, chorus ou phaser", false,
                { { { "Type",       [] (float x) { return juce::String (liqTypes[step (x, 3)]); } },
                    { "Longueur",   [] (float x) { return fr (liqLens[step (x, 4)]); } },
                    { "Centre",     [] (float x) { return pct (x); } },
                    { "Profondeur", [] (float x) { return pct (x); } },
                    { "Stéréo",     [] (float x) { return pct (x); } },
                    { "Feedback",   [] (float x) { return pct (x); } },
                    { "Lissage",    [] (float x) { return pct (x); } },
                    { "Courbe",     [] (float x) { return fr (liquidCurveNames()[step (x, liquidCurveNames().size())]); } } } },
                { { "Flanger qui accélère", { 0.0f, 2 / 3.f, 0.35f, 0.70f, 0.30f, 0.55f, 0.35f, 4 / 8.f }, 1.0f },
                  { "Chorus vagues",        { 0.5f, 1 / 3.f, 0.40f, 0.60f, 0.50f, 0.20f, 0.50f, 5 / 8.f }, 0.8f },
                  { "Phaser montée",        { 1.0f, 2 / 3.f, 0.45f, 0.80f, 0.25f, 0.50f, 0.40f, 2 / 8.f }, 1.0f },
                  { "Flanger sinus lent",   { 0.0f, 1.0f,    0.50f, 0.60f, 0.40f, 0.45f, 0.60f, 1 / 8.f }, 0.9f },
                  { "Phaser rampes",        { 1.0f, 1 / 3.f, 0.50f, 0.70f, 0.30f, 0.60f, 0.15f, 6 / 8.f }, 1.0f },
                  { "Chorus rebonds",       { 0.5f, 1 / 3.f, 0.35f, 0.70f, 0.60f, 0.25f, 0.30f, 7 / 8.f }, 0.8f },
                  { "Jet (flanger fort)",   { 0.0f, 2 / 3.f, 0.20f, 0.90f, 0.20f, 0.80f, 0.40f, 3 / 8.f }, 1.0f } } });
            static const char* nzTypes[] = { "Blanc", "Rose", "Vinyle", "Bande", "Radio" };
            static const char* nzRhythm[] = { "Continu", "Noires", "Croches", "Doubles" };
            v.push_back ({ "Bruit", "ajoute du souffle (blanc, rose, vinyle, bande, radio) qui suit le son ou pulse en rythme", false,
                { { { "Type",       [] (float x) { return fr (nzTypes[step (x, 5)]); } },
                    { "Volume",     [] (float x) { return db (lin (x, -60.0f, -6.0f)); } },
                    { "Ton",        [] (float x) { const float t = lin (x, -1, 1); return t < -0.05f ? fr ("Sombre") : (t > 0.05f ? fr ("Brillant") : fr ("Neutre")); } },
                    { "Suit le son",[] (float x) { return pct (x); } },
                    { "Largeur",    [] (float x) { return pct (x); } },
                    { "Craquements",[] (float x) { return pct (x); } },
                    { "Rythme",     [] (float x) { return fr (nzRhythm[step (x, 4)]); } } } },
                { { "Souffle blanc léger",  { 0.00f, 0.45f, 0.55f, 0.00f, 0.80f, 0.00f, 0.0f }, 1.0f },
                  { "Vinyle chaud",         { 0.50f, 0.50f, 0.35f, 0.20f, 0.40f, 0.60f, 0.0f }, 1.0f },
                  { "Bande cassette",       { 0.75f, 0.45f, 0.45f, 0.30f, 0.60f, 0.00f, 0.0f }, 1.0f },
                  { "Bruit rose qui suit",  { 0.25f, 0.55f, 0.50f, 0.90f, 0.70f, 0.00f, 0.0f }, 1.0f },
                  { "Radio lointaine",      { 1.00f, 0.50f, 0.50f, 0.40f, 0.20f, 0.00f, 0.0f }, 1.0f },
                  { "Souffle en croches",   { 0.00f, 0.55f, 0.65f, 0.00f, 0.90f, 0.00f, 2 / 3.f }, 1.0f },
                  { "Riser de bruit",       { 0.25f, 0.60f, 0.70f, 0.00f, 1.00f, 0.00f, 1 / 3.f }, 1.0f } } });
            static const char* chromaModes[] = { "Zéro latence", "Normal", "Haute qualité" };
            v[(size_t) Harmonic] = { "Prisme", "réaccorde n'importe quel son sur ta gamme : les fréquences hors gamme vont sur la note juste la plus proche", false,
                { { { "Tonalité",    [] (float x) { return fr (keys[step (x, 13)]); } },
                    { "Gamme",       [] (float x) { return fr (hScales[step (x, 14)]); } },
                    { "Couleur",     [] (float x) { return juce::String (juce::roundToInt (x * 200.0f)) + " %"; } },
                    { "Correction",  [] (float x) { return pct (x); } },
                    { "Focus",       [] (float x) { return pct (x); } },
                    { "Décalage",    [] (float x) { const int st = step (x, 25) - 12; return (st > 0 ? "+" : "") + juce::String (st) + " st"; } },
                    { "Mode",        [] (float x) { return fr (chromaModes[step (x, 3)]); } },
                    { "Brillance",   [] (float x) { const float t = lin (x, -1, 1); return t < -0.05f ? fr ("Sombre") : (t > 0.05f ? fr ("Brillant") : fr ("Neutre")); } } } },
                //                                 ton   gamme  couleur corr  focus décal mode  brill.
                { { "Réaccordage",          { 0.0f, 0.00f, 0.50f, 1.00f, 0.50f, 0.50f, 0.5f, 0.50f }, 1.0f },
                  { "Couleur douce",        { 0.0f, 0.00f, 0.35f, 1.00f, 0.50f, 0.50f, 0.5f, 0.50f }, 1.0f },
                  { "Gamme qui résonne",    { 0.0f, 0.00f, 0.70f, 1.00f, 0.60f, 0.50f, 0.5f, 0.55f }, 1.0f },
                  { "Batterie accordée",    { 0.0f, 0.00f, 0.80f, 1.00f, 0.80f, 0.50f, 0.5f, 0.50f }, 1.0f },
                  { "Basse dubstep juste",  { 0.0f, 0.00f, 0.55f, 1.00f, 0.40f, 0.50f, 1.0f, 0.45f }, 1.0f },
                  { "Pad sur l'accord MIDI",{ 0.0f, 1.00f, 0.75f, 1.00f, 0.60f, 0.50f, 1.0f, 0.55f }, 1.0f },
                  { "Boucle atonale → gamme", { 0.0f, 0.00f, 0.60f, 1.00f, 0.70f, 0.50f, 0.5f, 0.50f }, 1.0f },
                  { "Direct (zéro latence)",{ 0.0f, 0.00f, 0.60f, 0.70f, 0.50f, 0.50f, 0.0f, 0.50f }, 1.0f },
                  { "Quintes (power)",      { 0.0f, 0.70f, 0.75f, 1.00f, 0.60f, 0.50f, 0.5f, 0.50f }, 1.0f },
                  { "Octave en dessous",    { 0.0f, 0.00f, 0.50f, 1.00f, 0.50f, 0.00f, 0.5f, 0.45f }, 1.0f } } };
            v[(size_t) Harmonic].selfMix = true;
            static const char* repSizes[] = { "1/2 pas", "1/3 pas", "1/4 pas", "1/6 pas", "1/8 pas" };
            v[(size_t) Glitch] = { "Glitch", "6 pistes d'effets sur 16 pas, clique la grille", false,
                { { { "Motif",     [] (float x) { return fr (glitchPatternNames()[step (x, 13)]); } },
                    { "Grille",    [] (float x) { return juce::String (divisionNames()[step (x, 12)]); } },
                    { "Répétition",[] (float x) { return fr (repSizes[step (x, 5)]); } },
                    { "Pitch",     [] (float x) { const int st = step (x, 25) - 12; return (st > 0 ? "+" : "") + juce::String (st) + " st"; } },
                    { "Crush",     [] (float x) { return pct (x); } },
                    { "Gate",      [] (float x) { return pct (x); } },
                    { "Lissage",   [] (float x) { return juce::String (juce::roundToInt (lin (x, 2, 25))) + " ms"; } },
                    { "Hasard",    [] (float x) { return pct (x); } } } },
                { { "Bégaiement",          { 1 / 12.f, 0.45f, 0.50f, 0.50f, 0.0f, 0.0f, 0.35f, 0.00f }, 1.0f },
                  { "IDM nerveux",         { 2 / 12.f, 0.18f, 0.50f, 1.00f, 0.5f, 0.0f, 0.25f, 0.30f }, 1.0f },
                  { "Reverse fin",         { 3 / 12.f, 0.45f, 0.50f, 0.50f, 0.0f, 0.0f, 0.45f, 0.00f }, 1.0f },
                  { "Tape stop fin",       { 4 / 12.f, 0.73f, 0.50f, 0.50f, 0.0f, 0.0f, 0.45f, 0.00f }, 1.0f },
                  { "Hachoir 1/16",        { 5 / 12.f, 0.18f, 0.50f, 0.50f, 0.0f, 1.0f, 0.30f, 0.00f }, 1.0f },
                  { "Vocal chop",          { 6 / 12.f, 0.45f, 0.75f, 0.75f, 0.0f, 0.0f, 0.35f, 0.15f }, 1.0f },
                  { "Chiptune",            { 7 / 12.f, 0.45f, 0.50f, 1.00f, 0.7f, 0.0f, 0.20f, 0.00f }, 1.0f },
                  { "Breakbeat",           { 8 / 12.f, 0.45f, 0.50f, 0.50f, 0.0f, 0.8f, 0.35f, 0.20f }, 1.0f },
                  { "Roulement trap",      { 11 / 12.f, 0.18f, 1.00f, 0.75f, 0.0f, 0.6f, 0.25f, 0.10f }, 1.0f },
                  { "Chaos contrôlé",      { 12 / 12.f, 0.45f, 0.50f, 0.25f, 0.5f, 0.6f, 0.30f, 0.50f }, 0.8f } } };

            // ---------------- presets en plus (v3.20) : des idées franchement différentes pour chaque effet
            auto more = [&v] (int type, std::vector<Preset> p) { for (auto& x : p) v[(size_t) type].presets.push_back (x); };
            //                               p0     p1     p2     p3     p4     p5     p6     p7
            more (Saturation, {
                { "Ampli de guitare cassé",  { 1.00f, 0.70f, 0.30f, 0.45f, 0.85f, 0.42f }, 1.0f },
                { "Chaleur de console 80s",  { 0.50f, 0.28f, 0.62f, 0.08f, 0.15f, 0.50f }, 1.0f },
                { "808 qui grogne",          { 0.25f, 0.78f, 0.30f, 0.00f, 0.65f, 0.40f }, 0.55f },
                { "Cassette de walkman",     { 0.75f, 0.35f, 0.15f, 0.40f, 0.10f, 0.52f }, 1.0f },
                { "Haut-parleur de téléphone",{ 1.00f, 0.55f, 0.95f, 1.00f, 0.50f, 0.45f }, 1.0f },
                { "Juste une miette",        { 0.00f, 0.12f, 0.55f, 0.02f, 0.05f, 0.50f }, 1.0f } });
            more (Filter, {
                { "Ouverture sur 2 mesures", { 0.00f, 0.38f, 0.30f, 0.10f, 0.85f, 0.10f }, 1.0f },
                { "Voix de robot (bande)",   { 0.50f, 0.58f, 0.75f, 0.30f, 0.40f, 0.62f }, 1.0f },
                { "Haut-parleur de club voisin",{ 0.00f, 0.30f, 0.20f, 0.05f, 0.00f, 0.30f }, 1.0f },
                { "Sifflement résonant",     { 1.00f, 0.66f, 0.95f, 0.20f, 0.25f, 0.70f }, 1.0f },
                { "Coupe-bas qui respire",   { 0.25f, 0.30f, 0.25f, 0.00f, 0.55f, 0.48f }, 1.0f },
                { "Encoche fantôme",         { 0.75f, 0.55f, 0.60f, 0.00f, 0.70f, 0.30f }, 1.0f } });
            more (Compressor, {
                { "Écrasé façon radio",      { 1.00f, 0.15f, 0.85f, 0.70f, 0.50f, 0.50f, 0.60f, 0.48f }, 1.0f },
                { "Velours (très doux)",     { 0.30f, 0.90f, 0.20f, 0.30f, 0.50f, 0.52f, 0.48f, 0.50f }, 1.0f },
                { "Kick qui claque",         { 0.60f, 0.10f, 0.50f, 0.10f, 0.62f, 0.45f, 0.52f, 0.50f }, 1.0f },
                { "Air qui ressort",         { 0.70f, 0.50f, 0.30f, 0.90f, 0.45f, 0.50f, 0.68f, 0.48f }, 0.7f },
                { "Pad qui gonfle",          { 0.85f, 1.00f, 0.40f, 0.80f, 0.50f, 0.50f, 0.50f, 0.50f }, 0.8f },
                { "Bus de batterie sale",    { 0.90f, 0.25f, 0.95f, 0.55f, 0.58f, 0.55f, 0.55f, 0.47f }, 0.6f } });
            more (Chorus, {
                { "Cassette qui tangue",     { 0.00f, 0.15f, 0.70f, 0.30f, 0.40f, 0.55f }, 1.0f },
                { "Doublage de voix",        { 2 / 3.f, 0.35f, 0.25f, 0.85f, 1.00f, 0.80f }, 0.5f },
                { "Synthé 80s géant",        { 1.00f, 0.45f, 0.55f, 0.70f, 1.00f, 0.45f }, 0.6f },
                { "Guitare acoustique riche",{ 1 / 3.f, 0.30f, 0.30f, 0.90f, 0.70f, 0.60f }, 0.4f },
                { "Mer agitée",              { 2 / 3.f, 0.75f, 0.85f, 0.50f, 0.85f, 0.35f }, 0.7f },
                { "Mono élargi (subtil)",    { 0.00f, 0.25f, 0.12f, 0.80f, 0.95f, 0.40f }, 0.35f } });
            more (Flanger, {
                { "Réacteur au décollage",   { 0.20f, 1.00f, 0.92f, 0.70f, 0.40f, 1.00f }, 0.8f },
                { "Chant métallique",        { 0.05f, 0.25f, -0.0f, 0.20f, 0.10f, 0.00f }, 0.6f },
                { "Vague très lente",        { 0.02f, 0.80f, 0.70f, 0.55f, 1.00f, 0.00f }, 0.5f },
                { "Peigne figé (son de tuyau)",{ 0.00f, 0.00f, 0.95f, 0.15f, 0.00f, 0.00f }, 0.6f },
                { "Hachures rapides",        { 0.75f, 0.60f, 0.30f, 0.30f, 0.50f, 0.00f }, 0.5f },
                { "Flange négatif creux",    { 0.15f, 0.70f, 0.05f, 0.45f, 0.70f, 1.00f }, 0.7f } });
            more (Phaser, {
                { "Bulles sous l'eau",       { 0.75f, 0.45f, 0.90f, 0.80f, 0.20f, 0.60f }, 0.8f },
                { "Clavier Rhodes 70s",      { 0.25f, 0.25f, 0.55f, 0.55f, 0.45f, 0.30f }, 0.6f },
                { "Spirale infinie",         { 1.00f, 0.10f, 1.00f, 0.95f, 0.50f, 1.00f }, 0.7f },
                { "Phase qui pulse en croches",{ 0.50f, 0.70f, 0.75f, 0.60f, 0.55f, 0.50f }, 0.7f },
                { "Voile aigu",              { 0.25f, 0.30f, 0.40f, 0.50f, 0.95f, 0.40f }, 0.45f },
                { "Grave qui tourne",        { 0.50f, 0.20f, 0.70f, 0.30f, 0.05f, 0.20f }, 0.6f } });
            more (Tremolo, {
                { "Respiration de pad",      { 0.10f, 0.65f, 0.10f, 0.20f, 0.42f, 0.00f }, 1.0f },
                { "Morse",                   { 0.70f, 1.00f, 1.00f, 0.00f, 0.83f, 0.00f }, 1.0f },
                { "Ping-pong en triolets",   { 0.55f, 0.60f, 0.50f, 1.00f, 0.91f, 0.00f }, 1.0f },
                { "Ampli à lampe qui palpite",{ 0.45f, 0.35f, 0.00f, 0.00f, 0.00f, 0.00f }, 1.0f },
                { "Hachoir doubles-croches", { 0.80f, 0.90f, 0.90f, 0.10f, 0.83f, 0.00f }, 1.0f },
                { "Va-et-vient sur 2 mesures",{ 0.05f, 0.40f, 0.20f, 1.00f, 0.17f, 0.00f }, 1.0f } });
            more (Lofi, {
                { "Dictaphone",              { 0.35f, 0.35f, 0.30f, 0.40f, 0.20f, 0.45f }, 1.0f },
                { "Jeu vidéo GameBoy",       { 0.10f, 0.45f, 0.00f, 0.00f, 0.80f, 0.20f }, 1.0f },
                { "Bande qui fond au soleil",{ 0.85f, 0.05f, 0.85f, 0.30f, 0.45f, 0.30f }, 1.0f },
                { "Sampler SP des 90s",      { 0.62f, 0.25f, 0.10f, 0.10f, 0.55f, 0.35f }, 1.0f },
                { "Radio AM de nuit",        { 0.55f, 0.20f, 0.25f, 0.55f, 0.10f, 0.60f }, 1.0f },
                { "Poussière légère",        { 0.90f, 0.02f, 0.15f, 0.15f, 0.75f, 0.10f }, 1.0f } });
            more (Portal, {
                { "Pluie de cristaux",       { 0.15f, 0.85f, 0.70f, 0.75f, 0.10f, 0.25f }, 0.5f },
                { "Choeur fantôme",          { 0.55f, 0.60f, 0.50f, 0.35f, 0.40f, 0.55f }, 0.55f },
                { "Machine qui hoquette",    { 0.05f, 0.30f, 0.10f, 0.00f, 0.00f, 0.10f }, 0.7f },
                { "Brouillard à l'envers",   { 0.80f, 0.50f, 0.90f, 0.20f, 1.00f, 0.60f }, 0.6f },
                { "Écho granuleux",          { 0.35f, 0.40f, 0.30f, 0.10f, 0.20f, 0.75f }, 0.45f },
                { "Galaxie (tout à fond)",   { 0.95f, 1.00f, 1.00f, 1.00f, 0.50f, 0.85f }, 0.7f } });
            more (Crystal, {
                { "Boîte à musique",         { 0.25f, 0.75f, 0.40f, 0.00f, 0.60f, 0.85f }, 0.45f },
                { "Tunnel de glace",         { 0.60f, 0.50f, 0.85f, 0.30f, 0.90f, 0.50f }, 0.5f },
                { "Sous-sol (octave en bas)",{ 0.45f, 0.00f, 0.50f, 0.10f, 0.50f, 0.25f }, 0.45f },
                { "Ange à l'envers",         { 0.70f, 0.88f, 0.60f, 1.00f, 1.00f, 0.80f }, 0.55f },
                { "Écho en quinte qui monte",{ 0.40f, 0.62f, 0.75f, 0.00f, 0.70f, 0.70f }, 0.45f },
                { "Scintillement court",     { 0.08f, 0.75f, 0.30f, 0.50f, 0.80f, 1.00f }, 0.35f } });
            more (Delay, {
                { "Dub jamaïcain",           { 6 / 11.f, 0.82f, 0.30f, 0.25f, 0.70f, 0.20f, 0.00f }, 0.4f },
                { "Écho de stade",           { 8 / 11.f, 0.55f, 0.60f, 0.45f, 0.15f, 0.80f, 0.40f }, 0.35f },
                { "Rebond de balle",         { 2 / 11.f, 0.45f, 1.00f, 0.70f, 0.00f, 0.00f, 0.00f }, 0.4f },
                { "Voix qui s'efface (ducking)",{ 5 / 11.f, 0.60f, 0.50f, 0.55f, 0.25f, 0.35f, 1.00f }, 0.45f },
                { "Spirale de bande",        { 4 / 11.f, 0.95f, 0.40f, 0.35f, 1.00f, 0.50f, 0.20f }, 0.35f },
                { "Ombre (très loin)",       { 9 / 11.f, 0.40f, 0.20f, 0.10f, 0.40f, 1.00f, 0.60f }, 0.3f } });
            more (Reverb, {
                { "Salle de bain carrelée",  { 0.18f, 0.02f, 0.10f, 0.10f, 0.00f, 0.15f, 0.00f, 0.00f }, 0.35f },
                { "Parking souterrain",      { 0.45f, 0.20f, 0.55f, 0.30f, 0.00f, 0.75f, 0.00f, 0.00f }, 0.4f },
                { "Ciel ouvert (shimmer doux)",{ 0.70f, 0.30f, 0.20f, 0.60f, 0.45f, 0.90f, 0.00f, 0.00f }, 0.45f },
                { "Queue d'une mesure",      { 0.40f, 0.10f, 0.40f, 0.40f, 0.00f, 0.60f, 0.50f, 0.50f }, 0.35f },
                { "Grotte qui goutte",       { 0.62f, 0.55f, 0.75f, 0.85f, 0.10f, 1.00f, 0.00f, 0.00f }, 0.45f },
                { "Gel éternel",             { 1.00f, 0.00f, 0.30f, 0.50f, 0.70f, 1.00f, 0.00f, 0.00f }, 0.55f } });
            more (Stereo, {
                { "Super large (graves au centre)",{ 0.85f, 0.25f, 0.45f, 0.50f, 0, 0 }, 1.0f },
                { "Vieille radio (mono étroit)",   { 0.10f, 0.00f, 0.00f, 0.50f, 0, 0 }, 1.0f },
                { "Double voix (Haas léger)",      { 0.60f, 0.40f, 0.30f, 0.50f, 0, 0 }, 1.0f },
                { "Penché à gauche",               { 0.55f, 0.00f, 0.30f, 0.30f, 0, 0 }, 1.0f },
                { "Penché à droite",               { 0.55f, 0.00f, 0.30f, 0.70f, 0, 0 }, 1.0f },
                { "Pièce voisine",                 { 0.30f, 0.70f, 0.60f, 0.50f, 0, 0 }, 1.0f } });
            more (HalfTime, {
                { "Halftime de 4 temps planant",   { 0.00f, 2 / 3.f, 1.00f, 0.40f, 0.00f, 0.55f, 0.25f, 0.00f }, 1.0f },
                { "Cassette qui ralentit",         { 0.25f, 1 / 3.f, 0.00f, 0.30f, 0.00f, 0.25f, 0.70f, 0.20f }, 1.0f },
                { "Ralenti à l'envers",            { 0.00f, 1 / 3.f, 1.00f, 0.50f, 1.00f, 0.80f, 0.00f, 0.00f }, 1.0f },
                { "Quart de temps sombre",         { 1.00f, 1.00f, 0.00f, 0.60f, 0.00f, 0.20f, 0.15f, 0.00f }, 1.0f },
                { "Halftime une mesure sur deux",  { 0.00f, 2 / 3.f, 1.00f, 0.30f, 0.00f, 0.90f, 0.00f, 0.70f }, 1.0f },
                { "Groove 3/4 bancal",             { 0.50f, 1 / 3.f, 1.00f, 0.35f, 0.25f, 0.70f, 0.35f, 0.40f }, 1.0f } });
            more (Shaper, {
                { "Pompe 1 temps + filtre",        { 1 / 12.f, 1 / 12.f, 1.00f, 0.80f, 0.40f, 0.30f, 0.70f, 0.00f }, 1.0f },
                { "Stutter puis reverse",          { 5 / 12.f, 3 / 12.f, 0.90f, 0.70f, 0.30f, 0.20f, 0.00f, 0.00f }, 1.0f },
                { "Tape stop toutes les 2 mesures",{ 7 / 12.f, 0.00f, 1.00f, 0.00f, 1.00f, 0.25f, 0.00f, 0.00f }, 1.0f },
                { "Scratch DJ",                    { 1.00f, 0.00f, 1.00f, 0.00f, 0.75f, 0.10f, 0.20f, 0.30f }, 1.0f },
                { "Triolets qui hachent",          { 10 / 12.f, 5 / 12.f, 1.00f, 1.00f, 0.50f, 0.15f, 0.40f, 0.50f }, 1.0f },
                { "Respiration lente filtrée",     { 1 / 12.f, 1 / 12.f, 0.00f, 0.70f, 0.75f, 0.40f, 1.00f, 0.80f }, 1.0f } });
            more (Glitch, {
                { "Dub qui se répète",             { 10 / 12.f, 0.45f, 0.25f, 0.50f, 0.0f, 0.0f, 0.50f, 0.10f }, 1.0f },
                { "Trap 1/32 roulé",               { 11 / 12.f, 0.09f, 1.00f, 0.60f, 0.0f, 0.4f, 0.20f, 0.20f }, 1.0f },
                { "Jeu vidéo qui plante",          { 7 / 12.f, 0.18f, 0.75f, 0.90f, 0.9f, 0.5f, 0.10f, 0.60f }, 1.0f },
                { "Chop vocal aigu",               { 6 / 12.f, 0.45f, 0.75f, 0.92f, 0.0f, 0.2f, 0.35f, 0.30f }, 1.0f },
                { "Breakbeat sale",                { 8 / 12.f, 0.27f, 0.50f, 0.40f, 0.6f, 0.7f, 0.25f, 0.40f }, 1.0f },
                { "Chaos total",                   { 12 / 12.f, 0.18f, 0.25f, 0.00f, 0.8f, 0.8f, 0.15f, 1.00f }, 0.85f } });
            more (Harmonic, {
                { "Voix de robot accordée",        { 0.0f, 0.00f, 0.90f, 1.00f, 0.90f, 0.50f, 0.5f, 0.70f }, 1.0f },
                { "Pad en accord mineur",          { 0.0f, 8 / 13.f, 0.70f, 1.00f, 0.55f, 0.50f, 1.0f, 0.45f }, 1.0f },
                { "Quinte au-dessus",              { 0.0f, 9 / 13.f, 0.60f, 1.00f, 0.60f, 0.79f, 0.5f, 0.55f }, 1.0f },
                { "Penta mineure (trap)",          { 0.0f, 4 / 13.f, 0.65f, 1.00f, 0.65f, 0.50f, 0.5f, 0.50f }, 1.0f },
                { "Fondamentale seule (drone)",    { 0.0f, 10 / 13.f, 0.85f, 1.00f, 0.80f, 0.50f, 1.0f, 0.40f }, 1.0f },
                { "Phrygien dominant (oriental)",  { 0.0f, 12 / 13.f, 0.60f, 1.00f, 0.55f, 0.50f, 0.5f, 0.55f }, 1.0f } });
            more (Pad, {
                { "Nappe qui monte lentement",     { 0.00f, 0.80f, 0.95f, 0.30f, 0.50f, 0.40f, 1 / 3.f, 0.80f }, 0.7f },
                { "Orgue d'église",                { 0.40f, 0.70f, 0.40f, 0.60f, 0.40f, 0.20f, 1.00f, 0.60f }, 0.7f },
                { "Brume grave",                   { 0.80f, 0.90f, 0.70f, 0.70f, 0.15f, 0.30f, 0.00f, 0.90f }, 0.65f },
                { "Cordes lointaines",             { 0.20f, 0.60f, 0.75f, 0.40f, 0.55f, 0.50f, 1 / 3.f, 0.70f }, 0.6f },
                { "Étoiles qui tremblent",         { 0.60f, 0.85f, 0.50f, 0.50f, 0.90f, 0.85f, 2 / 3.f, 1.00f }, 0.6f },
                { "Gel total (drone)",             { 1.00f, 1.00f, 0.20f, 1.00f, 0.40f, 0.10f, 1 / 3.f, 0.80f }, 0.7f } });
            more (Liquid, {
                { "Chorus qui respire",            { 0.50f, 2 / 3.f, 0.50f, 0.45f, 0.70f, 0.20f, 0.70f, 1 / 8.f }, 0.6f },
                { "Phaser en vagues",              { 1.00f, 1 / 3.f, 0.40f, 0.80f, 0.60f, 0.50f, 0.50f, 5 / 8.f }, 0.7f },
                { "Flanger qui ralentit",          { 0.00f, 1 / 3.f, 0.55f, 0.85f, 0.50f, 0.70f, 0.40f, 3 / 8.f }, 0.65f },
                { "Rebonds métalliques",           { 0.00f, 0.00f, 0.30f, 0.90f, 0.30f, 0.85f, 0.20f, 7 / 8.f }, 0.6f },
                { "Plateau qui monte",             { 1.00f, 1.00f, 0.60f, 0.70f, 0.80f, 0.40f, 0.60f, 8 / 8.f }, 0.6f },
                { "Rampes rapides",                { 0.50f, 0.00f, 0.50f, 0.60f, 0.40f, 0.30f, 0.30f, 6 / 8.f }, 0.55f } });
            more (Noise, {
                { "Pluie sur la vitre",            { 0.25f, 0.40f, 0.70f, 0.00f, 0.95f, 0.85f, 0.00f }, 1.0f },
                { "Cassette des années 90",        { 0.75f, 0.42f, 0.40f, 0.50f, 0.50f, 0.20f, 0.00f }, 1.0f },
                { "Souffle qui suit la voix",      { 0.25f, 0.55f, 0.60f, 1.00f, 0.60f, 0.00f, 0.00f }, 1.0f },
                { "Radio entre deux stations",     { 1.00f, 0.58f, 0.65f, 0.20f, 0.30f, 0.30f, 0.00f }, 1.0f },
                { "Vinyle poussiéreux",            { 0.50f, 0.40f, 0.30f, 0.10f, 0.50f, 1.00f, 0.00f }, 1.0f },
                { "Charleston de bruit (doubles)", { 0.00f, 0.50f, 0.85f, 0.00f, 0.80f, 0.00f, 1.00f }, 1.0f } });
            return v;
        }();
        return d;
    }

    //==========================================================================
    // Icône animée de chaque effet (aperçu visuel, t = temps en secondes)
    inline void drawIcon (juce::Graphics& g, int type, juce::Rectangle<float> r, float t, juce::Colour col)
    {
        const float cx = r.getCentreX(), cy = r.getCentreY(), w = r.getWidth(), h = r.getHeight();
        juce::Path p;
        const juce::PathStrokeType stroke (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        auto wave = [&] (std::function<float (float)> f, juce::Colour c, float thick = 2.0f)
        {
            juce::Path q;
            for (int i = 0; i <= 48; ++i)
            {
                const float x = (float) i / 48.0f;
                const float y = f (x);
                if (i == 0) q.startNewSubPath (r.getX() + x * w, cy - y * h * 0.4f);
                else        q.lineTo (r.getX() + x * w, cy - y * h * 0.4f);
            }
            g.setColour (c);
            g.strokePath (q, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };
        switch (type)
        {
            case Saturation:
            {
                const float drive = 1.5f + 2.5f * (0.5f + 0.5f * std::sin (t * 1.6f));
                wave ([&] (float x) { return std::sin (x * twoPi * 2.0f) * 0.9f; }, col.withAlpha (0.25f), 1.5f);
                wave ([&] (float x) { return std::tanh (drive * std::sin (x * twoPi * 2.0f)) / std::tanh (drive); }, col, 2.2f);
                break;
            }
            case Filter:
            {
                const float fc = 0.35f + 0.3f * std::sin (t * 1.2f);
                wave ([&] (float x) { const float d = x - fc; return x < fc ? 0.55f + 0.35f * std::exp (-d * d * 300.0f) : 0.55f - (x - fc) * 4.0f + 0.35f * std::exp (-d * d * 300.0f); }, col, 2.2f);
                break;
            }
            case Compressor:
            {
                for (int i = 0; i < 9; ++i)
                {
                    const float a = 0.3f + 0.6f * std::abs (std::sin (t * 2.0f + (float) i * 0.7f));
                    const float c = juce::jmin (a, 0.55f + 0.1f * std::sin (t * 2.0f));
                    const float x = r.getX() + w * (0.08f + 0.1f * (float) i);
                    g.setColour (col.withAlpha (0.25f));
                    g.fillRoundedRectangle (x, cy - a * h * 0.4f, w * 0.06f, a * h * 0.8f, 2.0f);
                    g.setColour (col);
                    g.fillRoundedRectangle (x, cy - c * h * 0.4f, w * 0.06f, c * h * 0.8f, 2.0f);
                }
                break;
            }
            case EQ:
            {
                const float m = std::sin (t * 1.3f);
                wave ([&] (float x) { return 0.35f * std::exp (-std::pow ((x - 0.2f) * 6.0f, 2.0f)) * (1 + 0.5f * m)
                                             - 0.3f * std::exp (-std::pow ((x - 0.5f) * 7.0f, 2.0f))
                                             + 0.4f * std::exp (-std::pow ((x - 0.8f) * 6.0f, 2.0f)) * (1 - 0.4f * m); }, col, 2.2f);
                g.setColour (col.withAlpha (0.25f));
                g.drawHorizontalLine ((int) cy, r.getX(), r.getRight());
                break;
            }
            case Chorus:
                wave ([&] (float x) { return 0.7f * std::sin (x * twoPi * 2.0f + t * 2.0f); }, col, 2.0f);
                wave ([&] (float x) { return 0.7f * std::sin (x * twoPi * 2.0f + t * 2.0f + 0.6f + 0.4f * std::sin (t)); }, col.withAlpha (0.5f), 2.0f);
                break;
            case Flanger:
            {
                const float sweep = 0.5f + 0.5f * std::sin (t * 1.4f);
                for (int i = 0; i < 12; ++i)
                {
                    const float x = r.getX() + w * ((float) i + sweep) / 12.0f;
                    const float a = 0.3f + 0.7f * std::abs (std::sin ((float) i * 0.5f + t));
                    g.setColour (col.withAlpha (0.3f + 0.6f * a));
                    g.drawLine (x, cy - a * h * 0.35f, x, cy + a * h * 0.35f, 2.0f);
                }
                break;
            }
            case Phaser:
            {
                for (int k = 0; k < 3; ++k)
                {
                    const float rad = h * (0.15f + 0.12f * (float) k);
                    juce::Path a;
                    a.addCentredArc (cx, cy, rad, rad, 0.0f, t * (1.5f + (float) k * 0.5f), t * (1.5f + (float) k * 0.5f) + 3.5f, true);
                    g.setColour (col.withAlpha (1.0f - (float) k * 0.25f));
                    g.strokePath (a, stroke);
                }
                break;
            }
            case Tremolo:
                wave ([&] (float x) { return std::sin (x * twoPi * 6.0f) * (0.35f + 0.55f * (0.5f + 0.5f * std::sin (x * twoPi * 1.5f - t * 3.0f))); }, col, 1.8f);
                break;
            case Lofi:
            {
                const float steps = 6.0f + 3.0f * std::sin (t);
                wave ([&] (float x) { const float v = std::sin (x * twoPi * 2.0f + t); return std::round (v * steps) / steps; }, col, 2.2f);
                break;
            }
            case Portal:
            {
                juce::Random rnd (7);
                for (int i = 0; i < 16; ++i)
                {
                    const float ph = std::fmod (t * 0.6f + rnd.nextFloat(), 1.0f);
                    const float x = r.getX() + w * rnd.nextFloat(), y = r.getY() + h * rnd.nextFloat();
                    const float a = std::sin (ph * pi);
                    const float d = 3.0f + 5.0f * rnd.nextFloat() * a;
                    g.setColour (col.withAlpha (a * 0.9f));
                    g.fillEllipse (x - d * 0.5f, y - d * 0.5f, d, d);
                }
                break;
            }
            case Crystal:
            {
                juce::Random rnd (11);
                for (int i = 0; i < 9; ++i)
                {
                    const float ph = std::fmod (t * 0.5f + rnd.nextFloat(), 1.0f);
                    const float x = r.getX() + w * rnd.nextFloat(), y = r.getY() + h * rnd.nextFloat();
                    const float s = (2.0f + 6.0f * std::sin (ph * pi)) ;
                    juce::Path star;
                    star.addStar ({ x, y }, 4, s * 0.25f, s, ph * 2.0f);
                    g.setColour (col.withAlpha (std::sin (ph * pi)));
                    g.fillPath (star);
                }
                break;
            }
            case Delay:
            {
                const float off = std::fmod (t * 0.4f, 1.0f / 5.0f) * 5.0f;
                for (int i = 0; i < 5; ++i)
                {
                    const float x = r.getX() + w * (0.1f + 0.18f * ((float) i + off * 0.2f));
                    const float a = std::pow (0.65f, (float) i);
                    g.setColour (col.withAlpha (a));
                    g.fillRoundedRectangle (x, cy - a * h * 0.4f, 4.0f, a * h * 0.8f, 2.0f);
                }
                break;
            }
            case Reverb:
            {
                for (int k = 0; k < 4; ++k)
                {
                    const float ph = std::fmod (t * 0.35f + (float) k * 0.25f, 1.0f);
                    const float rad = h * 0.1f + ph * h * 0.55f;
                    g.setColour (col.withAlpha ((1.0f - ph) * 0.9f));
                    g.drawEllipse (cx - rad, cy - rad, rad * 2.0f, rad * 2.0f, 1.8f);
                }
                break;
            }
            case Stereo:
            {
                const float wdt = 0.3f + 0.25f * (0.5f + 0.5f * std::sin (t * 1.4f));
                for (int side : { -1, 1 })
                {
                    juce::Path a;
                    a.addCentredArc (cx + side * w * wdt * 0.5f, cy, h * 0.28f, h * 0.28f, 0.0f, side < 0 ? 3.6f : 0.5f, side < 0 ? 5.8f : 2.6f, true);
                    g.setColour (col);
                    g.strokePath (a, stroke);
                }
                g.setColour (col);
                g.fillEllipse (cx - 3.0f, cy - 3.0f, 6.0f, 6.0f);
                break;
            }
            case HalfTime:
            {
                // une onde qui s'étire à moitié vitesse
                wave ([t] (float x) { return std::sin (x * 12.0f + t * 4.0f) * 0.35f; }, col.withAlpha (0.35f), 1.5f);
                wave ([t] (float x) { return std::sin (x * 6.0f + t * 2.0f) * 0.8f; }, col, 2.2f);
                break;
            }
            case Shaper:
            {
                const int shape = (int) (t * 0.5f) % 12;
                juce::Path q;
                for (int i = 0; i <= 64; ++i)
                {
                    const float x = (float) i / 64.0f;
                    const float y = shaperShape (shape, x * 2.0f);
                    const float px = r.getX() + x * w, py = r.getBottom() - h * 0.15f - y * h * 0.7f;
                    if (i == 0) q.startNewSubPath (px, py); else q.lineTo (px, py);
                }
                g.setColour (col); g.strokePath (q, stroke);
                const float ph = std::fmod (t * 0.6f, 1.0f);
                g.fillEllipse (r.getX() + ph * w - 3.0f, r.getBottom() - h * 0.15f - shaperShape (shape, ph * 2.0f) * h * 0.7f - 3.0f, 6.0f, 6.0f);
                break;
            }
            case Glitch:
            {
                juce::Random rnd ((juce::int64) (t * 6.0f));
                for (int k = 0; k < 8; ++k)
                {
                    const float x = r.getX() + rnd.nextFloat() * w * 0.85f, y = r.getY() + rnd.nextFloat() * h * 0.8f;
                    g.setColour (col.withAlpha (0.4f + 0.6f * rnd.nextFloat()));
                    g.fillRect (x, y, 4.0f + rnd.nextFloat() * w * 0.25f, 3.0f + rnd.nextFloat() * 5.0f);
                }
                break;
            }
            case Harmonic:
            {
                for (int k = 0; k < 7; ++k)
                {
                    const float x = r.getX() + w * (0.1f + 0.8f * (float) k / 6.0f);
                    const float lvl = 0.3f + 0.7f * (0.5f + 0.5f * std::sin (t * 3.0f + (float) k * 1.3f));
                    g.setColour (col.withAlpha (0.4f + 0.5f * lvl));
                    g.fillRoundedRectangle (x - 3.0f, cy + h * 0.35f - lvl * h * 0.7f, 6.0f, lvl * h * 0.7f, 3.0f);
                }
                break;
            }
            case Noise:
            {
                juce::Random rr (7);
                for (int k = 0; k < 40; ++k) { g.setColour (col.withAlpha (0.3f + 0.6f * rr.nextFloat())); const float x = r.getX() + rr.nextFloat() * w, y = cy + (rr.nextFloat() - 0.5f) * h * (0.5f + 0.4f * std::sin (t * 3.0f + k)); g.fillRect (x, y, 2.0f, 2.0f); }
                break;
            }
            case Liquid:
            {
                wave ([&] (float x) { return std::sin (twoPi * (x * (1.0f + 2.0f * x * x) + t * 0.3f)) * 0.8f; }, col, 2.2f);
                break;
            }
            case Pad:
            {
                for (int k = 0; k < 3; ++k)
                    wave ([&, k] (float x) { return (1.0f - std::exp (-x * 5.0f)) * (0.5f + 0.3f * std::sin (x * 5.0f + t * (0.7f + k * 0.3f) + k)) - 0.3f; }, col.withAlpha (0.35f + 0.25f * k), 2.0f);
                break;
            }
            default: break;
        }
    }

    inline const TypeDef& def (int t) { return defs()[(size_t) juce::jlimit (0, NumTypes - 1, t)]; }

    inline juce::StringArray typeNames()
    {
        juce::StringArray a;
        for (auto& d : defs()) a.add (fr (d.name));
        return a;
    }

    //==========================================================================
    // DSP
    struct Biquad   // RBJ, sans allocation
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1[2] {}, z2[2] {};
        void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
        void copyCoefs (const Biquad& o) { b0 = o.b0; b1 = o.b1; b2 = o.b2; a1 = o.a1; a2 = o.a2; }   // coefficients seulement (pas l'état !)
        float process (float x, int ch) { const float y = b0 * x + z1[ch]; z1[ch] = b1 * x - a1 * y + z2[ch]; z2[ch] = b2 * x - a2 * y; return y; }

        void set (int type, double fs, double f, double q, double gainDb)
        {
            const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * juce::MathConstants<double>::pi * juce::jlimit (10.0, fs * 0.45, f) / fs;
            const double cw = std::cos (w), sw = std::sin (w), al = sw / (2.0 * q);
            double B0, B1, B2, A0, A1, A2;
            switch (type)
            {
                case 0: { // low shelf
                    const double sq = 2.0 * std::sqrt (A) * al;
                    B0 = A * ((A + 1) - (A - 1) * cw + sq); B1 = 2 * A * ((A - 1) - (A + 1) * cw); B2 = A * ((A + 1) - (A - 1) * cw - sq);
                    A0 = (A + 1) + (A - 1) * cw + sq; A1 = -2 * ((A - 1) + (A + 1) * cw); A2 = (A + 1) + (A - 1) * cw - sq; break; }
                case 1: { // high shelf
                    const double sq = 2.0 * std::sqrt (A) * al;
                    B0 = A * ((A + 1) + (A - 1) * cw + sq); B1 = -2 * A * ((A - 1) + (A + 1) * cw); B2 = A * ((A + 1) + (A - 1) * cw - sq);
                    A0 = (A + 1) - (A - 1) * cw + sq; A1 = 2 * ((A - 1) - (A + 1) * cw); A2 = (A + 1) - (A - 1) * cw - sq; break; }
                case 2: // peak
                    B0 = 1 + al * A; B1 = -2 * cw; B2 = 1 - al * A; A0 = 1 + al / A; A1 = -2 * cw; A2 = 1 - al / A; break;
                case 3: // high-pass
                    B0 = (1 + cw) / 2; B1 = -(1 + cw); B2 = (1 + cw) / 2; A0 = 1 + al; A1 = -2 * cw; A2 = 1 - al; break;
                case 5: // notch
                    B0 = 1; B1 = -2 * cw; B2 = 1; A0 = 1 + al; A1 = -2 * cw; A2 = 1 - al; break;
                default: // low-pass
                    B0 = (1 - cw) / 2; B1 = 1 - cw; B2 = (1 - cw) / 2; A0 = 1 + al; A1 = -2 * cw; A2 = 1 - al; break;
            }
            b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0); a1 = (float) (A1 / A0); a2 = (float) (A2 / A0);
        }
        // réponse en dB à la fréquence f (pour dessiner les courbes de l'égaliseur)
        double magnitudeDb (double f, double fs) const
        {
            const double w = 2.0 * juce::MathConstants<double>::pi * f / fs;
            const std::complex<double> z1c = std::polar (1.0, -w), z2c = std::polar (1.0, -2.0 * w);
            const auto num = (double) b0 + (double) b1 * z1c + (double) b2 * z2c;
            const auto den = 1.0 + (double) a1 * z1c + (double) a2 * z2c;
            return 20.0 * std::log10 (juce::jmax (1.0e-9, std::abs (num / den)));
        }
    };

    //--------------------------------------------------------------------------
    // Égaliseur paramétrique façon FL (Parametric EQ 2) : 7 bandes à attraper sur la courbe
    struct EqBand { bool on = true; int type = 2; float freq = 1000.0f, gain = 0.0f, q = 0.9f; int slope = 2; };   // slope : 1 = 12 dB/oct, 2 = 24, 4 = 48
    enum EqType { EqLowCut = 0, EqLowShelf, EqBell, EqHighShelf, EqHighCut, EqNotch, NumEqTypes };
    inline const char* eqTypeName (int t)
    {
        static const char* n[] = { "Coupe-bas", "Plateau grave", "Cloche", "Plateau aigu", "Coupe-haut", "Encoche" };
        return n[juce::jlimit (0, NumEqTypes - 1, t)];
    }
    constexpr int eqBandsCount = 7;
    using EqCurve = std::array<EqBand, eqBandsCount>;
    inline EqCurve defaultEq()
    {
        EqCurve c;
        const int types[] = { EqLowCut, EqLowShelf, EqBell, EqBell, EqBell, EqHighShelf, EqHighCut };
        const float f[] = { 30.0f, 90.0f, 300.0f, 1000.0f, 3000.0f, 8000.0f, 18000.0f };
        for (int i = 0; i < eqBandsCount; ++i) { c[(size_t) i].type = types[i]; c[(size_t) i].freq = f[i]; c[(size_t) i].on = types[i] != EqLowCut && types[i] != EqHighCut; }
        return c;
    }
    struct EqStore
    {
        juce::SpinLock lock;
        std::array<EqCurve, numSlots> curves;
        std::array<std::atomic<int>, numSlots> version {};
        EqStore() { for (auto& c : curves) c = defaultEq(); }
    };
    inline EqStore& eqStore() { static EqStore s; return s; }
    // réglages perso des effets, propres à chaque instance du plugin (2 Variant dans un projet ne se mélangent pas)
    struct Stores { CustomEnvelopes env; GlitchPatterns glitch; EqStore eq; LiquidCurves liquid; };
    // filtres d'une bande (jusqu'à 4 étages pour les coupes raides)
    inline int eqStages (const EqBand& b) { return (b.type == EqLowCut || b.type == EqHighCut) ? juce::jlimit (1, 4, b.slope) : 1; }
    inline void eqSetup (const EqBand& b, double fs, std::array<Biquad, 4>& st)
    {
        static const int bt[] = { 3, 0, 2, 1, 4, 5 };
        const int n = eqStages (b);
        for (int k = 0; k < n; ++k)
            st[(size_t) k].set (bt[juce::jlimit (0, NumEqTypes - 1, b.type)], fs, b.freq,
                                (b.type == EqLowCut || b.type == EqHighCut) ? (k == n - 1 ? juce::jmax (0.3, (double) b.q) : 0.707) : juce::jmax (0.1, (double) b.q),
                                b.type == EqNotch ? 0.0 : b.gain);
    }
    inline double eqResponseDb (const EqCurve& c, double f, double fs)
    {
        double db = 0.0;
        for (auto& b : c)
        {
            if (! b.on) continue;
            std::array<Biquad, 4> st;
            eqSetup (b, fs, st);
            for (int k = 0; k < eqStages (b); ++k) db += st[(size_t) k].magnitudeDb (f, fs);
        }
        return db;
    }

    // courbes des presets de l'égaliseur (même ordre que la liste des presets)
    inline EqCurve eqPresetCurve (int i)
    {
        auto c = defaultEq();
        auto band = [&c] (int b, int type, float f, float g, float q, bool on = true, int slope = 2) { c[(size_t) b] = { on, type, f, g, q, slope }; };
        switch (i)
        {
            case 1: band (1, EqLowShelf, 140, 3.5f, 0.7f); band (4, EqBell, 3500, -2.0f, 0.8f); band (5, EqHighShelf, 9000, -2.5f, 0.7f); break;
            case 2: band (4, EqBell, 4000, 2.5f, 0.7f); band (5, EqHighShelf, 9000, 4.0f, 0.7f); band (0, EqLowCut, 60, 0, 0.7f, true); break;
            case 3: band (5, EqHighShelf, 14000, 5.0f, 0.6f); band (4, EqBell, 6500, -1.5f, 1.2f); break;
            case 4: band (2, EqBell, 280, -5.0f, 1.0f); band (0, EqLowCut, 70, 0, 0.7f, true); break;
            case 5: band (4, EqBell, 2800, 4.0f, 0.9f); band (2, EqBell, 350, -2.5f, 1.0f); break;
            case 6: band (1, EqLowShelf, 100, 4.0f, 0.7f); band (3, EqBell, 900, -3.0f, 0.7f); band (5, EqHighShelf, 9000, 4.0f, 0.7f); break;
            case 7: band (0, EqLowCut, 400, 0, 0.9f, true, 4); band (6, EqHighCut, 3200, 0, 0.9f, true, 4); band (3, EqBell, 1500, 6.0f, 1.2f); break;
            case 8: band (1, EqLowShelf, 70, 6.0f, 0.8f); band (2, EqBell, 220, -3.0f, 1.0f); break;
            case 9: band (0, EqLowCut, 120, 0, 0.7f, true, 4); band (2, EqBell, 350, -3.0f, 1.2f); band (4, EqBell, 3000, 1.5f, 0.8f); band (6, EqHighCut, 17000, 0, 0.7f, true); break;
            default: break;
        }
        return c;
    }

    struct Line   // ligne de retard (lecture interpolée)
    {
        std::vector<float> d; int size = 1, w = 0;
        void prepare (int n) { size = juce::jmax (4, n); d.assign ((size_t) size, 0.0f); w = 0; }
        void clear() { std::fill (d.begin(), d.end(), 0.0f); }
        void push (float x) { d[(size_t) w] = x; w = (w + 1) % size; }
        float read (double delay) const
        {
            delay = juce::jlimit (1.0, (double) size - 2.0, delay);
            double p = (double) w - delay; if (p < 0) p += size;
            const int i0 = (int) p, i1 = (i0 + 1) % size; const float t = (float) (p - i0);
            return d[(size_t) i0] + (d[(size_t) i1] - d[(size_t) i0]) * t;
        }
    };

    struct OnePole { float z = 0; float lp (float x, float c) { z += c * (x - z); return z; } };
    inline float coef (double fs, double f) { return (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * f / fs)); }


    class Effect
    {
    public:
        virtual ~Effect() = default;
        virtual void prepare (double sampleRate, int maxBlock) = 0;
        virtual void reset() = 0;
        virtual void process (float* L, float* R, int n, const float* p, double bpm) = 0;
        virtual int latency (const float*) const { return 0; }   // retard ajouté (effets FFT), annoncé à FL
        float mix = 1.0f;          // pour les effets qui font eux-mêmes le mélange (selfMix)
        int slotIndex = 0;         // emplacement dans la chaîne (enveloppes perso du Gross)
        Stores* stores = nullptr;  // réglages perso de l'instance
        void clearTempoRefs() { for (auto& r : refs) r = {}; }

        // Réglages de temps qui suivent le BPM : un potard vaut ce qu'il vaut AU TEMPO OÙ IL A ÉTÉ RÉGLÉ
        // (ou au chargement du projet). Si le BPM change ensuite, la durée / vitesse suit pour garder le même son
        // musical (même valeur de note, même longueur en temps).
        double tempoRef (int k, float pk, double bpm)
        {
            auto& r = refs[(size_t) juce::jlimit (0, (int) refs.size() - 1, k)];
            if (r.bpm <= 0.0 || std::abs (pk - r.p) > 1.0e-6f) { r.p = pk; r.bpm = juce::jmax (20.0, bpm); }
            return r.bpm;
        }
        // durée libre (ms, s) -> multiplier par ce facteur
        double tempoScale (int k, float pk, double bpm) { return tempoRef (k, pk, bpm) / juce::jmax (20.0, bpm); }
        // vitesse (Hz) calée sur une division : la division reste la même quand le BPM change
        double lockedHz (int k, float pk, double hz, double bpm, double* divOut = nullptr)
        {
            const double d = nearestDivision (hz, tempoRef (k, pk, bpm));
            if (divOut != nullptr) *divOut = d;
            return juce::jmax (20.0, bpm) / 60.0 / d;
        }
        int lockedSamples (int k, float pk, double ms, double bpm, double rate)
        {
            const double ref = tempoRef (k, pk, bpm);
            return (int) (syncedSamples (ms, ref, rate) * ref / juce::jmax (20.0, bpm));
        }
    protected:
        double sr = 44100.0;
    private:
        struct Ref { float p = 0.0f; double bpm = 0.0; };
        std::array<Ref, 16> refs {};
    };

    //--------------------------------------------------------------------------
    class SaturationFx : public Effect
    {
    public:
        void prepare (double s, int maxBlock) override
        {
            sr = s;
            os = std::make_unique<juce::dsp::Oversampling<float>> (2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
            os->initProcessing ((size_t) juce::jmax (1, maxBlock));
            reset();
        }
        void reset() override { if (os) os->reset(); hp.reset(); rmsIn = rmsOut = 1.0e-4f; tl[0] = tl[1] = 0; auto_ = 1.0f; }

        void process (float* L, float* R, int n, const float* p, [[maybe_unused]] double bpm) override
        {
            const int model = step (p[0], 5);
            const float drive = juce::Decibels::decibelsToGain (lin (p[1], 0, 36));
            const float tone = lin (p[2], -1, 1);
            const float hard = p[4];
            const float outG = juce::Decibels::decibelsToGain (lin (p[5], -12, 12));
            hp.set (3, sr, expMap (p[3], 20, 600), 0.707, 0);

            for (int i = 0; i < n; ++i)
            {
                L[i] = hp.process (L[i], 0); R[i] = hp.process (R[i], 1);
                rmsIn += 0.0005f * (0.5f * (L[i] * L[i] + R[i] * R[i]) - rmsIn);
            }

            float* chans[2] = { L, R };
            juce::dsp::AudioBlock<float> block (chans, 2, (size_t) n);
            auto up = os->processSamplesUp (block);
            const float bias = 0.05f + 0.55f * hard;
            for (size_t ch = 0; ch < 2; ++ch)
            {
                float* x = up.getChannelPointer (ch);
                for (size_t i = 0; i < up.getNumSamples(); ++i)
                    x[i] = shape (model, x[i] * drive, bias, hard, (int) ch);
            }
            os->processSamplesDown (block);

            const float tc = coef (sr, 900.0);
            for (int i = 0; i < n; ++i)
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    float& x = chans[ch][i];
                    tl[ch] += tc * (x - tl[ch]);
                    const float lo = tl[ch], hi = x - lo;
                    x = lo * (1.0f - 0.6f * tone) + hi * (1.0f + 0.9f * tone);
                }
                rmsOut += 0.0005f * (0.5f * (L[i] * L[i] + R[i] * R[i]) - rmsOut);
                const float target = juce::jlimit (0.05f, 2.0f, std::sqrt ((rmsIn + 1.0e-9f) / (rmsOut + 1.0e-9f)));
                auto_ += 0.0002f * (target - auto_);   // compensation de niveau automatique (lente)
                L[i] *= auto_ * outG; R[i] *= auto_ * outG;
            }
        }

    private:
        float shape (int model, float x, float bias, float hard, int ch)
        {
            switch (model)
            {
                case 0: return std::tanh (x + bias) - std::tanh (bias);                                         // lampe : harmoniques paires
                case 1: { lf[ch] += 0.002f * (x - lf[ch]); const float y = x + 0.5f * lf[ch];                   // transfo : graves gras
                          return (2.0f / pi) * std::atan (y * (1.2f + hard)); }
                case 2: return std::sin (juce::jlimit (-pi * 0.5f, pi * 0.5f, x * 0.8f));                       // console : très doux
                case 3: { const float y = std::tanh (x * (1.0f + 0.3f * hard)); tp[ch] += 0.35f * (y - tp[ch]);   // bande : arrondi + aigus adoucis
                          return tp[ch] * 1.05f; }
                default: { const float y = juce::jlimit (-1.0f, 1.0f, (x + bias * 0.6f) * 1.6f);                // fuzz
                           return std::tanh (3.0f * y) * 0.8f - bias * 0.3f; }
            }
        }

        std::unique_ptr<juce::dsp::Oversampling<float>> os;
        Biquad hp;
        float rmsIn = 1e-4f, rmsOut = 1e-4f, auto_ = 1.0f, tl[2] {}, lf[2] {}, tp[2] {};
    };

    //--------------------------------------------------------------------------
    class FilterFx : public Effect
    {
    public:
        void prepare (double s, int) override { sr = s; reset(); }
        void reset() override { for (auto& c : st) c = {}; phase = 0; }

        void process (float* L, float* R, int n, const float* p, [[maybe_unused]] double bpm) override
        {
            const int type = step (p[0], 5);
            const float base = expMap (p[1], 20, 20000), res = p[2], drive = 1.0f + p[3] * 6.0f, depth = p[4];
            double div = 1.0;
            const float rate = (float) lockedHz (5, p[5], expMap (p[5], 0.05f, 10.0f), bpm, &div);
            lockPhase (div, phase);
            float* ch[2] = { L, R };
            for (int i = 0; i < n; ++i)
            {
                if ((i & 15) == 0)
                {
                    phase += twoPi * rate * 16.0f / (float) sr; if (phase > twoPi) phase -= twoPi;
                    fc = juce::jlimit (20.0f, (float) sr * 0.45f, base * std::pow (2.0f, std::sin (phase) * depth * 3.0f));
                    g = std::tan (pi * fc / (float) sr);
                }
                for (int c = 0; c < 2; ++c)
                {
                    float x = std::tanh (ch[c][i] * drive) / std::tanh (drive);
                    auto& s = st[(size_t) c];
                    if (type == 4) // ladder 24 dB (4 pôles + réinjection saturée)
                    {
                        const float G = g / (1.0f + g), k = res * 3.9f;
                        float u = std::tanh (x - k * s.y[3]);
                        for (int j = 0; j < 4; ++j) { const float v = G * (u - s.y[j]); const float y = v + s.y[j]; s.y[j] = y + v; u = y; }
                        ch[c][i] = u * (1.0f + res);
                    }
                    else
                    {
                        const float k = 2.0f - 1.95f * res;
                        const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
                        const float v3 = x - s.ic2, v1 = a1 * s.ic1 + a2 * v3, v2 = s.ic2 + a2 * s.ic1 + a3 * v3;
                        s.ic1 = 2 * v1 - s.ic1; s.ic2 = 2 * v2 - s.ic2;
                        const float lp = v2, bp = v1, hp = x - k * v1 - v2;
                        ch[c][i] = type == 0 ? lp : (type == 1 ? hp : (type == 2 ? bp * k : lp + hp));
                    }
                }
            }
        }

    private:
        struct State { float ic1 = 0, ic2 = 0; float y[4] {}; };
        std::array<State, 2> st {};
        float phase = 0, fc = 1000, g = 0.1f;
    };

    //--------------------------------------------------------------------------
    class CompressorFx : public Effect
    {
    public:
        // Compression multibande haut + bas façon OTT : 3 bandes (88 Hz / 2,5 kHz),
        // chaque bande est écrasée vers le haut (sons forts) ET remontée vers le bas (détails faibles)
        void prepare (double s, int) override { sr = s; reset(); }
        void reset() override { for (auto& c : ch) c = {}; for (auto& e : env) e = 0.0f; }

        void process (float* L, float* R, int n, const float* p, [[maybe_unused]] double bpm) override
        {
            const float depth = p[0];
            const float timeScale = expMap (p[1], 0.25f, 4.0f);
            const float down = p[2], up = p[3];
            const float bandGain[3] = { juce::Decibels::decibelsToGain (lin (p[4], -12, 12)), juce::Decibels::decibelsToGain (lin (p[5], -12, 12)),
                                        juce::Decibels::decibelsToGain (lin (p[6], -12, 12)) };
            const float outG = juce::Decibels::decibelsToGain (lin (p[7], -12, 12));
            const float gLo = std::tan (juce::MathConstants<float>::pi * 88.0f / (float) sr);
            const float gHi = std::tan (juce::MathConstants<float>::pi * 2500.0f / (float) sr);
            const float att[3] = { coefMs (13.5f * timeScale), coefMs (10.0f * timeScale), coefMs (5.0f * timeScale) };
            const float rel[3] = { coefMs (132.0f * timeScale), coefMs (282.0f * timeScale), coefMs (132.0f * timeScale) };
            static const float thrDown[3] = { -28.0f, -27.0f, -30.0f }, thrUp[3] = { -40.0f, -41.0f, -41.0f };
            float* io[2] = { L, R };
            for (int i = 0; i < n; ++i)
            {
                float band[2][3];
                for (int c = 0; c < 2; ++c)
                {
                    const float x = io[c][i];
                    const float lo = ch[c].lo.lp (x, gLo);
                    const float hi = x - ch[c].hi.lp (x, gHi);
                    band[c][0] = lo; band[c][2] = hi; band[c][1] = x - lo - hi;
                }
                float outL = 0.0f, outR = 0.0f;
                for (int b = 0; b < 3; ++b)
                {
                    const float lvl = juce::jmax (std::abs (band[0][b]), std::abs (band[1][b]));
                    env[b] += (lvl > env[b] ? att[b] : rel[b]) * (lvl - env[b]);
                    const float ldb = juce::Decibels::gainToDecibels (env[b] + 1.0e-6f, -100.0f);
                    float gdb = 0.0f;
                    if (ldb > thrDown[b]) gdb -= (ldb - thrDown[b]) * (1.0f - 1.0f / 66.7f) * down;            // vers le bas : 66:1
                    else if (ldb < thrUp[b] && ldb > -75.0f) gdb += juce::jmin (36.0f, (thrUp[b] - ldb) * (1.0f - 1.0f / 4.17f)) * up; // vers le haut : 4:1
                    const float g = juce::Decibels::decibelsToGain (gdb * depth) * bandGain[b];
                    outL += band[0][b] * g; outR += band[1][b] * g;
                }
                // compense le gain moyen pour que la profondeur ne change pas trop le volume
                const float makeup = juce::Decibels::decibelsToGain (depth * (down * 6.0f - up * 2.0f));
                L[i] = outL * makeup * outG;
                R[i] = outR * makeup * outG;
            }
        }

    private:
        float coefMs (float ms) const { return 1.0f - std::exp (-1.0f / (0.001f * ms * (float) sr)); }
        struct Lp { float ic1 = 0, ic2 = 0; float lp (float x, float g) { const float k = 1.41421f, a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
            const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3; ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2; return v2; } };
        struct Ch { Lp lo, hi; };
        Ch ch[2];
        float env[3] {};
    };

    class EqFx : public Effect
    {
    public:
        void prepare (double s, int) override { sr = s; reset(); seen = -1; }
        void reset() override { for (auto& b : st) for (auto& q : b) q.reset(); }

        void process (float* L, float* R, int n, const float* p, [[maybe_unused]] double bpm) override
        {
            auto& es = stores != nullptr ? stores->eq : eqStore();
            const int slotI = juce::jlimit (0, fxs::numSlots - 1, slotIndex);
            const int v = es.version[(size_t) slotI].load();
            if (v != seen && es.lock.tryEnter())
            {
                curve = es.curves[(size_t) slotI];
                es.lock.exit();
                seen = v;
                for (int b = 0; b < eqBandsCount; ++b) eqSetup (curve[(size_t) b], sr, st[(size_t) b]);
            }
            const float out = juce::Decibels::decibelsToGain (lin (p[0], -18.0f, 18.0f));
            for (int i = 0; i < n; ++i)
            {
                float l = L[i], r = R[i];
                for (int b = 0; b < eqBandsCount; ++b)
                {
                    const auto& bd = curve[(size_t) b];
                    if (! bd.on) continue;
                    for (int k = 0; k < eqStages (bd); ++k) { l = st[(size_t) b][(size_t) k].process (l, 0); r = st[(size_t) b][(size_t) k].process (r, 1); }
                }
                L[i] = l * out; R[i] = r * out;
            }
        }

    private:
        EqCurve curve = defaultEq();
        std::array<std::array<Biquad, 4>, eqBandsCount> st;
        int seen = -1;
    };

    //--------------------------------------------------------------------------
    class ChorusFx : public Effect
    {
    public:
        void prepare (double s, int) override { sr = s; for (auto& l : lines) l.prepare ((int) (s * 0.06)); reset(); }
        void reset() override { for (auto& l : lines) l.clear(); phase = 0; lp[0] = lp[1] = {}; }

        void process (float* L, float* R, int n, const float* p, [[maybe_unused]] double bpm) override
        {
            const int mode = step (p[0], 4);
            double div = 1.0;
            const float rate = (float) lockedHz (1, p[1], expMap (p[1], 0.05f, 6) * (mode == 1 ? 1.6f : 1.0f), bpm, &div);
            lockPhase (div, phase);
            const float depthMs = p[2] * (mode == 3 ? 6.0f : 3.5f);
            const float tc = coef (sr, expMap (p[3], 1500, 16000));
            const float width = p[4];
            const float baseMs = lin (p[5], 2, 25);
            auto tri = [] (float ph) { const float x = ph / twoPi; return 4.0f * std::abs (x - std::floor (x + 0.5f)) - 1.0f; };

            for (int i = 0; i < n; ++i)
            {
                phase += twoPi * rate / (float) sr; if (phase > twoPi) phase -= twoPi;
                const float mono = 0.5f * (L[i] + R[i]);
                lines[0].push (L[i]); lines[1].push (R[i]);
                float wl = 0, wr = 0;
                const int taps = mode == 3 ? 3 : (mode == 2 ? 2 : 1);
                for (int t = 0; t < taps; ++t)
                {
                    const float off = (float) t * twoPi / (float) taps;
                    const float m = mode == 3 ? std::sin (phase + off) : tri (phase + off);
                    const double dl = (baseMs + depthMs * (0.5f + 0.5f * m)) * 0.001 * sr;
                    const double dr = (baseMs + depthMs * (0.5f - 0.5f * m)) * 0.001 * sr;
                    wl += lines[0].read (dl); wr += lines[1].read (dr);
                }
                wl /= (float) taps; wr /= (float) taps;
                wl = lp[0].lp (wl, tc); wr = lp[1].lp (wr, tc);
                const float wm = 0.5f * (wl + wr);
                wl = wm + (wl - wm) * (0.4f + 1.2f * width);
                wr = wm + (wr - wm) * (0.4f + 1.2f * width);
                L[i] = 0.75f * (L[i] + wl);
                R[i] = 0.75f * (R[i] + wr);
                juce::ignoreUnused (mono);
            }
        }

    private:
        std::array<Line, 2> lines;
        std::array<OnePole, 2> lp {};
        float phase = 0;
    };

    //--------------------------------------------------------------------------
    class FlangerFx : public Effect
    {
    public:
        void prepare (double s, int) override { sr = s; for (auto& l : lines) l.prepare ((int) (s * 0.04)); for (auto& l : dry) l.prepare ((int) (s * 0.04)); reset(); }
        void reset() override { for (auto& l : lines) l.clear(); for (auto& l : dry) l.clear(); phase = 0; fbv[0] = fbv[1] = 0; }

        void process (float* L, float* R, int n, const float* p, [[maybe_unused]] double bpm) override
        {
            double div = 1.0;
            const float rate = (float) lockedHz (0, p[0], expMap (p[0], 0.02f, 8), bpm, &div), depth = p[1], fb = lin (p[2], -0.95f, 0.95f);
            lockPhase (div, phase);
            const float manualMs = lin (p[3], 0.2f, 8.0f), stereo = p[4] * pi;
            const bool tz = p[5] > 0.5f;
            float* ch[2] = { L, R };
            for (int i = 0; i < n; ++i)
            {
                phase += twoPi * rate / (float) sr; if (phase > twoPi) phase -= twoPi;
                for (int c = 0; c < 2; ++c)
                {
                    const float x = ch[c][i];
                    const float m = 0.5f + 0.5f * std::sin (phase + (c == 1 ? stereo : 0.0f));
                    const double d = (0.05 + depth * manualMs * m + (tz ? 0.0 : 0.15)) * 0.001 * sr + 1.0;
                    lines[(size_t) c].push (x + fbv[c] * fb);
                    const float wet = lines[(size_t) c].read (d);
                    fbv[c] = wet;
                    dry[(size_t) c].push (x);
                    const float d0 = tz ? dry[(size_t) c].read (depth * manualMs * 0.5 * 0.001 * sr + 1.0) : x;
                    ch[c][i] = 0.6f * (d0 + wet);
                }
            }
        }

    private:
        std::array<Line, 2> lines, dry;
        float phase = 0, fbv[2] {};
    };

    //--------------------------------------------------------------------------
    class PhaserFx : public Effect
    {
    public:
        void prepare (double s, int) override { sr = s; reset(); }
        void reset() override { for (auto& c : z) c.fill (0.0f); phase = 0; fbv[0] = fbv[1] = 0; }

        void process (float* L, float* R, int n, const float* p, [[maybe_unused]] double bpm) override
        {
            static const int stagesTable[] = { 2, 4, 6, 8, 12 };
            const int stages = stagesTable[step (p[0], 5)];
            double div = 1.0;
            const float rate = (float) lockedHz (1, p[1], expMap (p[1], 0.02f, 8), bpm, &div), depth = p[2], fb = lin (p[3], -0.9f, 0.9f), centre = expMap (p[4], 100, 4000);
            lockPhase (div, phase);
            const float stereo = p[5] * pi;
            float* ch[2] = { L, R };
            for (int i = 0; i < n; ++i)
            {
                phase += twoPi * rate / (float) sr; if (phase > twoPi) phase -= twoPi;
                for (int c = 0; c < 2; ++c)
                {
                    const float m = std::sin (phase + (c == 1 ? stereo : 0.0f));
                    const float f = juce::jlimit (30.0f, (float) sr * 0.45f, centre * std::pow (2.0f, m * depth * 2.5f));
                    const float t = std::tan (pi * f / (float) sr);
                    const float a = (t - 1.0f) / (t + 1.0f);
                    float y = ch[c][i] + fbv[c] * fb;
                    for (int s = 0; s < stages; ++s)
                    {
                        const float out = a * y + z[(size_t) c][(size_t) s];
                        z[(size_t) c][(size_t) s] = y - a * out;
                        y = out;
                    }
                    fbv[c] = y;
                    ch[c][i] = 0.6f * (ch[c][i] + y);
                }
            }
        }

    private:
        std::array<std::array<float, 12>, 2> z {};
        float phase = 0, fbv[2] {};
    };

    //--------------------------------------------------------------------------
    class TremoloFx : public Effect
    {
    public:
        void prepare (double s, int) override { sr = s; reset(); }
        void reset() override { phase = 0; }

        void process (float* L, float* R, int n, const float* p, double bpm) override
        {
            double div = 1.0;
            double rate = lockedHz (0, p[0], expMap (p[0], 0.1f, 20), bpm, &div);
            if (p[4] >= 0.04f) { div = divisionBeats (step (p[4], 12)); rate = juce::jmax (20.0, bpm) / 60.0 / div; }
            lockPhase (div, phase);
            const float depth = p[1], shapeAmt = p[2], pan = p[3];
            for (int i = 0; i < n; ++i)
            {
                phase += (float) (twoPi * rate / sr); if (phase > twoPi) phase -= twoPi;
                auto wave = [shapeAmt] (float ph)
                {
                    const float s = std::sin (ph);
                    const float sq = std::tanh (s * (1.0f + 20.0f * shapeAmt));
                    return 0.5f + 0.5f * (s * (1.0f - shapeAmt) + sq * shapeAmt);
                };
                const float gl = 1.0f - depth * wave (phase);
                const float gr = 1.0f - depth * wave (phase + pan * pi);
                L[i] *= gl; R[i] *= gr;
            }
        }

    private:
        float phase = 0;
    };

    //--------------------------------------------------------------------------
    class LofiFx : public Effect
    {
    public:
        void prepare (double s, int) override { sr = s; for (auto& l : lines) l.prepare ((int) (s * 0.05)); reset(); }
        void reset() override { for (auto& l : lines) l.clear(); acc = 0; held[0] = held[1] = 0; wowPh = flPh = 0; lp[0] = lp[1] = {}; }

        void process (float* L, float* R, int n, const float* p, [[maybe_unused]] double bpm) override
        {
            const float q = std::pow (2.0f, lin (p[0], 3, 16)) * 0.5f;
            const float rateDiv = expMap (p[1], 1, 40);
            const float wow = p[2], flutter = p[3];
            const float tc = coef (sr, expMap (p[4], 800, 20000));
            const float sat = p[5];
            float* ch[2] = { L, R };
            for (int i = 0; i < n; ++i)
            {
                wowPh += twoPi * 0.45f / (float) sr; flPh += twoPi * 7.3f / (float) sr;
                if (wowPh > twoPi) wowPh -= twoPi; if (flPh > twoPi) flPh -= twoPi;
                const double d = (4.0 + wow * 3.5 * (1.0 + std::sin (wowPh)) + flutter * 0.4 * (1.0 + std::sin (flPh))) * 0.001 * sr;
                acc += 1.0f;
                const bool take = acc >= rateDiv;
                if (take) acc -= rateDiv;
                for (int c = 0; c < 2; ++c)
                {
                    lines[(size_t) c].push (ch[c][i]);
                    float x = (wow > 0.001f || flutter > 0.001f) ? lines[(size_t) c].read (d) : ch[c][i];
                    if (take) held[c] = std::round (x * q) / q;
                    x = rateDiv > 1.01f ? held[c] : std::round (x * q) / q;
                    x = lp[(size_t) c].lp (x, tc);
                    if (sat > 0) x = x * (1 - sat) + std::tanh (x * (1 + 3 * sat)) * sat;
                    ch[c][i] = x;
                }
            }
        }

    private:
        std::array<Line, 2> lines;
        std::array<OnePole, 2> lp {};
        float acc = 0, held[2] {}, wowPh = 0, flPh = 0;
    };

    //--------------------------------------------------------------------------
    class PortalFx : public Effect
    {
    public:
        void prepare (double s, int) override { sr = s; for (auto& l : buf) l.prepare ((int) (s * 4.0)); reset(); }
        void reset() override { for (auto& l : buf) l.clear(); for (auto& g : grains) g.active = false; until = 0; fbL = fbR = 0; }

        void process (float* L, float* R, int n, const float* p, [[maybe_unused]] double bpm) override
        {
            const int len = juce::jmax (64, lockedSamples (0, p[0], expMap (p[0], 10, 500), bpm, sr));
            const float density = p[1], scatter = p[2], pitch = p[3], rev = p[4], fb = p[5] * 0.85f;
            for (int i = 0; i < n; ++i)
            {
                buf[0].push (L[i] + fbL * fb); buf[1].push (R[i] + fbR * fb);
                if (--until <= 0)
                {
                    until = juce::jmax (8, (int) (len / (1.0f + 7.0f * density)));
                    for (auto& g : grains)
                        if (! g.active)
                        {
                            const float x = rng.nextFloat();
                            double sp = 1.0;
                            if (x < pitch * 0.35f) sp = 2.0; else if (x < pitch * 0.55f) sp = 0.5; else if (x < pitch * 0.7f) sp = 1.4983; else if (x < pitch * 0.78f) sp = 4.0;
                            if (rng.nextFloat() < rev) sp = -sp;
                            g.speed = sp; g.len = len; g.age = 0;
                            g.delay = juce::jmin ((double) buf[0].size - len * 5.0 - 8, len * std::abs (sp) + 32 + rng.nextFloat() * scatter * 1.2 * sr);
                            const float pan = 0.5f + (rng.nextFloat() - 0.5f) * scatter;
                            g.gl = std::sqrt (1.0f - pan); g.gr = std::sqrt (pan);
                            g.active = true;
                            break;
                        }
                }
                float wl = 0, wr = 0; int active = 0;
                for (auto& g : grains)
                {
                    if (! g.active) continue;
                    ++active;
                    const float w = std::sin (pi * (float) g.age / (float) g.len);
                    const float win = w * w;
                    wl += buf[0].read (g.delay) * win * g.gl;
                    wr += buf[1].read (g.delay) * win * g.gr;
                    g.delay += 1.0 - g.speed;
                    if (++g.age >= g.len) g.active = false;
                }
                const float norm = 1.0f / std::sqrt ((float) juce::jmax (1, active));
                fbL = wl * norm; fbR = wr * norm;
                L[i] = fbL * 1.3f; R[i] = fbR * 1.3f;
            }
        }

    private:
        struct Grain { bool active = false; double delay = 0, speed = 1; int len = 0, age = 0; float gl = 1, gr = 1; };
        std::array<Line, 2> buf;
        std::array<Grain, 48> grains {};
        int until = 0; float fbL = 0, fbR = 0;
        juce::Random rng;
    };

    //--------------------------------------------------------------------------
    class CrystalFx : public Effect
    {
    public:
        void prepare (double s, int) override { sr = s; for (auto& l : buf) l.prepare ((int) (s * 6.0)); reset(); }
        void reset() override { for (auto& l : buf) l.clear(); phase = 0; fb[0] = fb[1] = 0; lp[0] = lp[1] = {}; }

        void process (float* L, float* R, int n, const float* p, [[maybe_unused]] double bpm) override
        {
            static const int semis[] = { -12, -7, -5, 0, 5, 7, 12, 19, 24 };
            const int len = juce::jmax (256, lockedSamples (0, p[0], expMap (p[0], 40, 1500), bpm, sr));
            const double speed = std::pow (2.0, semis[step (p[1], 9)] / 12.0);
            const float feedback = p[2] * 0.92f, reverse = p[3], width = p[4];
            const float tc = coef (sr, expMap (p[5], 1000, 18000));
            for (int i = 0; i < n; ++i)
            {
                buf[0].push (L[i] + fb[0] * feedback); buf[1].push (R[i] + fb[1] * feedback);
                float w[2] = { 0, 0 };
                for (int g = 0; g < 2; ++g)
                {
                    const int age = (phase + g * len / 2) % len;
                    const float win = std::sin (pi * (float) age / (float) len);
                    const double dirRev = (double) len * 0.5 + age * (1.0 + speed);           // lecture à l'envers
                    const double dirFwd = (double) len * (1.0 + speed) - age * (speed - 1.0);  // lecture à l'endroit
                    const double d = reverse * dirRev + (1.0 - reverse) * juce::jmax (2.0, dirFwd);
                    for (int c = 0; c < 2; ++c)
                    {
                        const int src = (g == 1 && width > 0.5f) ? 1 - c : c;   // croise les canaux pour élargir
                        w[c] += buf[(size_t) src].read (d) * win;
                    }
                }
                phase = (phase + 1) % len;
                for (int c = 0; c < 2; ++c) { w[c] = lp[(size_t) c].lp (w[c], tc); fb[c] = w[c] * 0.7f; }
                const float m = 0.5f * (w[0] + w[1]);
                L[i] = (m + (w[0] - m) * (0.5f + width)) * 0.9f;
                R[i] = (m + (w[1] - m) * (0.5f + width)) * 0.9f;
            }
        }

    private:
        std::array<Line, 2> buf;
        std::array<OnePole, 2> lp {};
        int phase = 0; float fb[2] {};
    };

    //--------------------------------------------------------------------------
    class DelayFx : public Effect
    {
    public:
        void prepare (double s, int) override
        {
            sr = s;
            for (auto& l : lines) l.prepare ((int) (s * 8.5));
            for (auto& c : diff) for (auto& a : c) a.prepare ((int) (s * 0.03));
            reset();
        }
        void reset() override { for (auto& l : lines) l.clear(); for (auto& c : diff) for (auto& a : c) a.clear(); fbv[0] = fbv[1] = 0; ph = 0;
                                for (auto& b : tone) b.reset(); for (auto& b : lowcut) b.reset(); smoothD = -1; }

        void process (float* L, float* R, int n, const float* p, double bpm) override
        {
            const double target = juce::jlimit (16.0, (double) lines[0].size - 2000.0, divisionBeats (step (p[0], 12)) * 60.0 / juce::jmax (20.0, bpm) * sr);
            if (smoothD < 0) smoothD = target;
            const float fb = p[1] * 0.98f, ping = p[2], wear = p[4], diffusion = p[5];
            const float duck = p[6];
            const float duckAtk = coef (sr, 1000.0f / 5.0f), duckRel = coef (sr, 1000.0f / 250.0f);
            tone[0].set (4, sr, expMap (p[3], 1800, 16000), 0.6, 0); tone[1].copyCoefs (tone[0]);
            lowcut[0].set (3, sr, 60 + (1.0f - p[3]) * 200, 0.6, 0); lowcut[1].copyCoefs (lowcut[0]);
            static const int dl[4] = { 113, 337, 541, 797 };
            for (int i = 0; i < n; ++i)
            {
                smoothD += 0.0005 * (target - smoothD);   // changement de tempo sans clic (effet bande)
                ph += twoPi * 0.6f / (float) sr; if (ph > twoPi) ph -= twoPi;
                ph2 += twoPi * 6.78f / (float) sr; if (ph2 > twoPi) ph2 -= twoPi;   // phase à part : plus de saut (clic) quand ph repart à 0
                const double wob = wear * (12.0 * std::sin (ph) + 3.0 * std::sin (ph2)) * sr / 44100.0;
                float y[2];
                for (int c = 0; c < 2; ++c) y[c] = lines[(size_t) c].read (smoothD + wob);
                float in[2] = { L[i], R[i] };
                const float mono = 0.5f * (in[0] + in[1]);
                float w[2];
                for (int c = 0; c < 2; ++c)
                {
                    float v = y[c];
                    v = tone[(size_t) c].process (v, 0);
                    v = lowcut[(size_t) c].process (v, 0);
                    if (wear > 0) v = std::tanh (v * (1.0f + wear)) / (1.0f + 0.3f * wear);
                    for (int k = 0; k < 4 && diffusion > 0.01f; ++k)   // diffusion : allpass en série
                    {
                        auto& a = diff[(size_t) c][(size_t) k];
                        const float dd = a.read (dl[k] * (sr / 44100.0));
                        const float g = 0.5f * diffusion;
                        const float o = -g * v + dd;
                        a.push (v + g * o);
                        v = o;
                    }
                    w[c] = v;
                }
                const float inL = (1.0f - ping) * in[0] + ping * mono;
                const float inR = (1.0f - ping) * in[1];
                lines[0].push (inL + fb * ((1.0f - ping) * w[0] + ping * w[1]));
                lines[1].push (inR + fb * ((1.0f - ping) * w[1] + ping * w[0]));
                // ducking : les échos se poussent quand le son joue, et reviennent dans les silences
                const float lvl = juce::jmax (std::abs (in[0]), std::abs (in[1]));
                duckEnv += (lvl > duckEnv ? duckAtk : duckRel) * (lvl - duckEnv);
                const float dg = 1.0f - duck * juce::jlimit (0.0f, 1.0f, duckEnv * 4.0f);
                L[i] = w[0] * dg; R[i] = w[1] * dg;
            }
        }

    private:
        std::array<Line, 2> lines;
        std::array<std::array<Line, 4>, 2> diff;
        std::array<Biquad, 2> tone, lowcut;
        float fbv[2] {}, ph = 0, ph2 = 0;
        double smoothD = -1;
        float duckEnv = 0.0f;
    };

    //--------------------------------------------------------------------------
    // Réverb à réseau de retards (FDN 8 lignes) modulé, avec shimmer : très différente d'une réverb "Freeverb".
    class ReverbFx : public Effect
    {
    public:
        void prepare (double s, int) override
        {
            sr = s;
            pre[0].prepare ((int) (s * 0.3)); pre[1].prepare ((int) (s * 0.3));
            for (auto& c : ap) for (auto& a : c) a.prepare ((int) (s * 0.05));
            for (auto& l : fdn) l.prepare ((int) (s * 0.35));
            shim.prepare ((int) (s * 0.2));
            reset();
        }
        void reset() override
        {
            for (auto& l : pre) l.clear(); for (auto& c : ap) for (auto& a : c) a.clear(); for (auto& l : fdn) l.clear(); shim.clear();
            for (auto& d : damp) d = {}; for (auto& b : inCut) b.reset(); lfo = 0; shPh = 0;
            inCut[0].set (3, sr, 90, 0.7, 0); inCut[1].copyCoefs (inCut[0]);
        }

        void process (float* L, float* R, int n, const float* p, [[maybe_unused]] double bpm) override
        {
            static const double baseMs[8] = { 29.7, 37.1, 41.1, 43.7, 53.3, 59.9, 67.1, 73.1 };
            static const int apLen[4] = { 142, 107, 379, 277 };
            // calage sur le tempo de FL (suit les changements de BPM en direct)
            const double spb = 60.0 / juce::jmax (20.0, host().bpm);
            const int preSync = step (p[6], 5), durSync = step (p[7], 5);
            static const double preBeats[] = { 0.0, 1.0 / 16.0, 1.0 / 8.0, 0.25, 0.5 };
            static const double durBars[] = { 0.0, 0.5, 1.0, 2.0, 4.0 };
            const float t60 = durSync > 0 ? (float) juce::jlimit (0.2, 30.0, durBars[durSync] * 4.0 * spb) : (float) juce::jlimit (0.2, 30.0, expMap (p[0], 0.2f, 20.0f) * tempoScale (0, p[0], host().bpm));   // durée libre : suit le BPM
            const double preD = juce::jmin (0.28 * sr, (preSync > 0 ? preBeats[preSync] * spb : lin (p[1], 0, 250) * 0.001 * tempoScale (1, p[1], host().bpm)) * sr) + 1.0;
            const float dc = coef (sr, expMap (1.0f - p[2], 1500, 18000));
            const float mod = p[3], shimmer = p[4];
            const double space = 0.45 + 1.4 * p[5];

            double len[8]; float gains[8];
            for (int k = 0; k < 8; ++k)
            {
                len[k] = baseMs[k] * space * 0.001 * sr;
                gains[k] = (float) std::pow (10.0, -3.0 * (len[k] / sr) / t60);
            }

            for (int i = 0; i < n; ++i)
            {
                lfo += (float) (twoPi * juce::jmax (20.0, host().bpm) / 60.0 / 8.0 / sr); if (lfo > twoPi) lfo -= twoPi;

                float in[2] = { inCut[0].process (L[i], 0), inCut[1].process (R[i], 1) };
                for (int c = 0; c < 2; ++c)
                {
                    pre[c].push (in[c]);
                    float v = pre[c].read (preD);
                    for (int k = 0; k < 4; ++k)   // diffusion d'entrée
                    {
                        auto& a = ap[(size_t) c][(size_t) k];
                        const float d = a.read (apLen[k] * (sr / 44100.0) * (c ? 1.07 : 1.0));
                        const float o = -0.7f * v + d;
                        a.push (v + 0.7f * o);
                        v = o;
                    }
                    in[c] = v;
                }

                float y[8], sum = 0;
                for (int k = 0; k < 8; ++k)
                {
                    const double m = mod * (8.0 + k) * std::sin (lfo * (1.0f + 0.31f * (float) k) + (float) k);
                    y[k] = damp[(size_t) k].lp (fdn[(size_t) k].read (len[k] + m), dc) * gains[k];
                    sum += y[k];
                }

                // shimmer : transposition +12 du retour (deux têtes de lecture en fondu)
                float sh = 0;
                if (shimmer > 0.001f)
                {
                    shim.push (sum * 0.125f);
                    const double win = 0.08 * sr;
                    shPh += 1.0 / win; if (shPh >= 1.0) shPh -= 1.0;
                    for (int h = 0; h < 2; ++h)
                    {
                        double ph = std::fmod (shPh + 0.5 * h, 1.0);
                        const double d = 2.0 + (1.0 - ph) * win;   // le retard diminue : lecture 2x plus vite
                        sh += shim.read (d) * std::sin ((float) ph * pi);
                    }
                }

                const float hh = -2.0f / 8.0f * sum;   // matrice de Householder
                for (int k = 0; k < 8; ++k)
                {
                    float x = y[k] + hh;
                    x += (k & 1) ? in[1] * 0.6f : in[0] * 0.6f;
                    x += sh * shimmer * 0.6f;
                    fdn[(size_t) k].push (x);
                }

                L[i] = (y[0] - y[2] + y[4] - y[6] + 0.5f * (y[1] + y[5])) * 0.45f;
                R[i] = (y[1] - y[3] + y[5] - y[7] + 0.5f * (y[2] + y[6])) * 0.45f;
            }
        }

    private:
        std::array<Line, 2> pre;
        std::array<std::array<Line, 4>, 2> ap;
        std::array<Line, 8> fdn;
        std::array<OnePole, 8> damp {};
        std::array<Biquad, 2> inCut;
        Line shim;
        float lfo = 0; double shPh = 0;
    };

    //--------------------------------------------------------------------------
    class StereoFx : public Effect
    {
    public:
        void prepare (double s, int) override { sr = s; haas.prepare ((int) (s * 0.04)); reset(); }
        void reset() override { haas.clear(); lo[0] = lo[1] = {}; }

        void process (float* L, float* R, int n, const float* p, [[maybe_unused]] double bpm) override
        {
            const float width = p[0] * 2.0f;
            const double hd = lin (p[1], 0, 30) * 0.001 * sr;
            const bool monoBass = p[2] >= 0.01f;
            const float bc = coef (sr, expMap (p[2], 40, 400));
            const float bal = lin (p[3], -1, 1);
            for (int i = 0; i < n; ++i)
            {
                float l = L[i], r = R[i];
                if (hd > 1.0) { haas.push (r); r = haas.read (hd); }
                const float mid = 0.5f * (l + r), side = 0.5f * (l - r) * width;
                l = mid + side; r = mid - side;
                if (monoBass)
                {
                    const float bl = lo[0].lp (l, bc), br = lo[1].lp (r, bc), bm = 0.5f * (bl + br);
                    l = l - bl + bm; r = r - br + bm;
                }
                L[i] = l * juce::jmin (1.0f, 1.0f - bal);
                R[i] = r * juce::jmin (1.0f, 1.0f + bal);
            }
        }

    private:
        Line haas;
        std::array<OnePole, 2> lo {};
    };

    //==========================================================================
    //--------------------------------------------------------------------------
    // Tampon stéréo circulaire partagé par les effets "temporels"
    struct Ring
    {
        std::vector<float> l, r;
        int size = 1, w = 0;
        void init (int n) { size = juce::jmax (16, n); l.assign ((size_t) size, 0.0f); r.assign ((size_t) size, 0.0f); w = 0; }
        void clear() { std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f); }
        void push (float a, float b) { l[(size_t) w] = a; r[(size_t) w] = b; w = (w + 1) % size; }
        // position absolue (compteur d'échantillons écrits) -> lecture interpolée
        void read (double absPos, juce::int64 written, float& a, float& b) const
        {
            double back = (double) written - absPos;
            if (back >= (double) size - 2.0) { a = b = 0.0f; return; }
            back = juce::jmax (back, 1.0);   // jamais dans le futur : on prend le dernier échantillon écrit
            double p = (double) w - back; while (p < 0.0) p += size;
            const int i0 = (int) p % size, i1 = (i0 + 1) % size, im = (i0 - 1 + size) % size, i2 = (i0 + 2) % size;
            const float f = (float) (p - std::floor (p));
            auto herm = [f] (float xm, float x0, float x1, float x2) { const float c1 = 0.5f * (x1 - xm), c2 = xm - 2.5f * x0 + 2.0f * x1 - 0.5f * x2,
                                                                      c3 = 0.5f * (x2 - xm) + 1.5f * (x0 - x1); return ((c3 * f + c2) * f + c1) * f + x0; };
            a = herm (l[(size_t) im], l[(size_t) i0], l[(size_t) i1], l[(size_t) i2]);
            b = herm (r[(size_t) im], r[(size_t) i0], r[(size_t) i1], r[(size_t) i2]);
        }
    };

    // compteur de "pas" calé sur FL (ou interne si la lecture est arrêtée)
    struct StepClock
    {
        double localBeats = 0.0;
        // renvoie la position en temps (noires) au début du bloc
        double beatsAtBlockStart (int n, double sr)
        {
            const auto& h = host();
            const double b = h.playing ? h.ppq : localBeats;
            localBeats = (h.playing ? h.ppq : localBeats) + n * juce::jmax (20.0, h.bpm) / 60.0 / sr;
            return b;
        }
    };

    //--------------------------------------------------------------------------
    class HalfTimeFx : public Effect
    {
    public:
        void prepare (double s, int) override { sr = s; ring.init ((int) (sr * 20.0)); reset(); }
        void reset() override { ring.clear(); written = 0; seg = -1; segStart = prevStart = 0; t = 0; lpL = lpR = 0; wow = 0; }

        void process (float* L, float* R, int n, const float* p, double bpm) override
        {
            const double rate0 = halftimeRate (step (p[0], 5));
            const double segBeats = halftimeBeats (step (p[1], 4));
            const bool keepPitch = p[2] >= 0.5f;
            const double fade = lin (p[3], 2, 60) * 0.001 * sr;
            const float tc = coef (sr, expMap (p[5], 800, 20000));
            const double spb = 60.0 / juce::jmax (20.0, bpm) * sr;
            double beat = clock.beatsAtBlockStart (n, sr);
            const double beatPerSample = 1.0 / spb;
            const double grain = 0.12 * sr;

            for (int i = 0; i < n; ++i, beat += beatPerSample)
            {
                ring.push (L[i], R[i]);
                ++written;
                const juce::int64 k = (juce::int64) std::floor (beat / segBeats);
                if (k != seg)
                {
                    prevStart = segStart; prevRev = rev; prevDry = dry; prevT = t;
                    seg = k; segStart = written - 1; t = 0;
                    juce::uint32 hsh = (juce::uint32) (k * 2654435761u); hsh ^= hsh >> 13;
                    rev = (float) (hsh % 1000) / 1000.0f < p[4];
                    dry = (float) ((hsh >> 10) % 1000) / 1000.0f < p[7];
                }
                wow += juce::MathConstants<double>::twoPi * 0.6 / sr;
                const double rate = rate0 * (1.0 + 0.03 * p[6] * std::sin (wow));
                float a, b;
                readSeg (segStart, rev, dry, (double) t, rate, keepPitch, grain, L[i], R[i], a, b);
                if (t < fade && seg > 0)
                {
                    float pa, pb;
                    readSeg (prevStart, prevRev, prevDry, (double) (prevT + t), rate, keepPitch, grain, L[i], R[i], pa, pb);
                    const float x = (float) (t / fade);
                    a = a * x + pa * (1.0f - x); b = b * x + pb * (1.0f - x);
                }
                lpL += tc * (a - lpL); lpR += tc * (b - lpR);
                L[i] = lpL; R[i] = lpR;
                ++t;
            }
        }

    private:
        void readSeg (juce::int64 start, bool reversed, bool normal, double tt, double rate, bool keepPitch, double grain,
                      float dryL, float dryR, float& a, float& b) const
        {
            if (normal) { a = dryL; b = dryR; return; }
            const double dir = reversed ? -1.0 : 1.0;
            if (! keepPitch)
            {
                ring.read ((double) start + dir * tt * rate, written, a, b);
                return;
            }
            // granulaire : 2 grains en Hann qui se chevauchent, la hauteur ne bouge pas
            a = b = 0.0f;
            for (int g = 0; g < 3; ++g)
            {
                const double local = std::fmod (tt + g * grain / 3.0, grain);
                const double t0 = tt - local;
                double pos = (double) start + dir * t0 * rate + local;
                pos = juce::jmin (pos, (double) written - 2.0);
                const float win = (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * local / grain));
                float x, y; ring.read (pos, written, x, y);
                a += x * win * 0.6667f; b += y * win * 0.6667f;
            }
        }

        Ring ring; StepClock clock;
        juce::int64 written = 0, seg = -1, segStart = 0, prevStart = 0, t = 0, prevT = 0;
        bool rev = false, prevRev = false, dry = false, prevDry = false;
        float lpL = 0, lpR = 0; double wow = 0;
    };

    //--------------------------------------------------------------------------
    class ShaperFx : public Effect
    {
    public:
        // "Gross" : tampon de 4 mesures, enveloppe de TEMPS (on relit le passé : halftime, reverse, stutter, tape stop…)
        // + enveloppe de VOLUME (gates, sidechain…), calées sur la position de FL
        void prepare (double s, int) override { sr = s; ring.init ((int) (sr * 16.0)); reset(); }
        void reset() override { ring.clear(); written = 0; g = 1.0f; f[0] = f[1] = {}; lastOffset = 0.0; fadeLeft = 0; }

        void process (float* L, float* R, int n, const float* p, double bpm) override
        {
            const int timeSlot = step (p[0], 13), volSlot = step (p[1], 13);
            const float timeMix = p[2], volMix = p[3];
            static const double cycles[] = { 1.0, 2.0, 4.0, 8.0 };
            const double cycleBeats = cycles[step (p[4], 4)];
            const float smooth = coef (sr, 1000.0 / lin (p[5], 0.5f, 40.0f));
            const float flt = p[6], pan = p[7];
            const double spb = 60.0 / juce::jmax (20.0, bpm) * sr;
            double beat = clock.beatsAtBlockStart (n, sr);

            // copie des enveloppes perso (verrou court, jamais bloquant)
            auto& ce = stores != nullptr ? stores->env : customEnvelopes();
            if (ce.lock.tryEnter()) { custom[0] = ce.env[(size_t) juce::jlimit (0, fxs::numSlots - 1, slotIndex)][0]; custom[1] = ce.env[(size_t) juce::jlimit (0, fxs::numSlots - 1, slotIndex)][1]; ce.lock.exit(); }

            const double fadeLen = 0.004 * sr;
            for (int i = 0; i < n; ++i, beat += 1.0 / spb)
            {
                ring.push (L[i], R[i]);
                ++written;
                const float x = (float) (beat / cycleBeats - std::floor (beat / cycleBeats));

                // --- temps
                float a = L[i], b = R[i];
                if (timeSlot != 1 && timeMix > 0.001f)
                {
                    const double offset = juce::jlimit (0.0, (double) ring.size - 8.0,
                                                        (double) slotValue (slotIndex, false, timeSlot, x, &custom[0]) * cycleBeats * spb);
                    if (std::abs (offset - (lastOffset + 0.0)) > 48.0 && written > 2) { fadeFrom = (double) written - 1.0 - lastOffset; fadeLeft = (int) fadeLen; }
                    lastOffset = offset;
                    float ta, tb;
                    ring.read ((double) written - 1.0 - offset, written, ta, tb);
                    if (fadeLeft > 0)
                    {
                        fadeFrom += 1.0;
                        float fa, fb; ring.read (fadeFrom, written, fa, fb);
                        const float w = (float) fadeLeft / (float) fadeLen;
                        ta = ta * (1.0f - w) + fa * w; tb = tb * (1.0f - w) + fb * w;
                        --fadeLeft;
                    }
                    a = a * (1.0f - timeMix) + ta * timeMix;
                    b = b * (1.0f - timeMix) + tb * timeMix;
                }

                // --- volume
                const float target = volSlot == 0 ? custom[1].at (x) : shaperShape (volSlot - 1, x);
                g += smooth * (juce::jlimit (0.0f, 1.0f, target) - g);
                if (flt > 0.01f)
                {
                    const float fc = 120.0f * std::pow (2.0f, 7.5f * (1.0f - flt + flt * g));
                    const float gg = std::tan (juce::MathConstants<float>::pi * juce::jmin (fc, (float) sr * 0.45f) / (float) sr);
                    a = f[0].lp (a, gg); b = f[1].lp (b, gg);
                }
                const float v = 1.0f - volMix + volMix * g;
                const float pn = (g - 0.5f) * 2.0f * pan;
                L[i] = a * v * (1.0f - juce::jmax (0.0f, pn));
                R[i] = b * v * (1.0f - juce::jmax (0.0f, -pn));
            }
        }

    private:
        struct Svf { float ic1 = 0, ic2 = 0; float lp (float x, float gg) { const float k = 1.2f, a1 = 1.0f / (1.0f + gg * (gg + k)), a2 = gg * a1, a3 = gg * a2;
            const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3; ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2; return v2; } };
        Ring ring; StepClock clock;
        Envelope custom[2];
        juce::int64 written = 0;
        double lastOffset = 0.0, fadeFrom = 0.0;
        int fadeLeft = 0;
        float g = 1.0f;
        Svf f[2];
    };


    //--------------------------------------------------------------------------
    class GlitchFx : public Effect
    {
    public:
        void prepare (double s, int) override { sr = s; ring.init ((int) (sr * 10.0)); reset(); }
        void reset() override { ring.clear(); written = 0; stepIdx = -1; t = 0; tapePos = 0; held[0] = held[1] = 0; gateG = 1.0f; lanes = {}; }

        void process (float* L, float* R, int n, const float* p, double bpm) override
        {
            const int patIdx = step (p[0], 13);
            const double div = divisionBeats (step (p[1], 12));
            static const double sizes[] = { 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
            const double repFrac = sizes[step (p[2], 5)];
            const double pitchRate = std::pow (2.0, ((double) step (p[3], 25) - 12.0) / 12.0);
            const float crush = p[4], gateDepth = p[5];
            const double fade = juce::jmax (16.0, lin (p[6], 2.0f, 25.0f) * 0.001 * sr);
            const float hasard = p[7];
            const double spb = 60.0 / juce::jmax (20.0, bpm) * sr;
            const double stepLen = div * spb;
            if (patIdx == 0) { auto& gp = stores != nullptr ? stores->glitch : glitchPatterns(); if (gp.lock.tryEnter()) { perso = gp.perso[(size_t) juce::jlimit (0, fxs::numSlots - 1, slotIndex)]; gp.lock.exit(); } }
            const GlitchPattern pat = patIdx == 0 ? perso : factoryGlitch (patIdx);
            const float gateCoef = coef (sr, 160.0 / lin (p[6], 2.0f, 25.0f));   // gate lissé comme le reste
            double beat = clock.beatsAtBlockStart (n, sr);

            for (int i = 0; i < n; ++i, beat += 1.0 / spb)
            {
                ring.push (L[i], R[i]);
                ++written;
                const juce::int64 k = (juce::int64) std::floor (beat / div);
                if (k != stepIdx)
                {
                    stepIdx = k; start = written - 1; t = 0; tapePos = 0.0;
                    lanes = glitchStep (pat, k, hasard);
                }
                const bool timeFx = lanes[LRepeat] || lanes[LReverse] || lanes[LTape] || lanes[LPitch];
                if (! timeFx && ! lanes[LCrush] && ! lanes[LGate]) { gateG += gateCoef * (1.0f - gateG); ++t; continue; }

                const double tt = (double) t;
                float a = L[i], b = R[i];
                float loopW = 1.0f;
                if (lanes[LReverse]) ring.read ((double) start - tt - 1.0, written, a, b);
                else if (lanes[LTape]) { ring.read ((double) start + tapePos, written, a, b); tapePos += juce::jmax (0.0, 1.0 - tt / stepLen); }
                else if (lanes[LPitch] || lanes[LRepeat])
                {
                    const double rl = juce::jmax (256.0, stepLen * (lanes[LRepeat] ? repFrac : 0.5));
                    const double rate = lanes[LPitch] ? pitchRate : 1.0;
                    const double lp = std::fmod (tt * rate, rl);
                    const double pos = rate > 1.0 ? (double) start - rl + lp : (double) start + lp;
                    ring.read (pos, written, a, b);
                    const double f2 = juce::jmin (fade, rl * 0.25);
                    loopW = (float) juce::jmin (1.0, lp / (f2 * rate), (rl - lp) / (f2 * rate));   // fondu à chaque répétition
                }
                a *= loopW; b *= loopW;
                if (lanes[LCrush] && crush > 0.0f)
                {
                    const int hold = 1 + (int) (crush * 24.0f);
                    if (t % hold == 0) { held[0] = a; held[1] = b; }
                    const float q = std::pow (2.0f, 12.0f - crush * 9.0f);
                    a = std::round (held[0] * q) / q; b = std::round (held[1] * q) / q;
                }
                if (lanes[LGate])
                {
                    const bool closed = std::fmod (tt / (stepLen * 0.25), 1.0) > 0.5;
                    gateG += gateCoef * ((closed ? 1.0f - gateDepth : 1.0f) - gateG);
                    a *= gateG; b *= gateG;
                }
                else gateG += gateCoef * (1.0f - gateG);

                // fondu d'entrée / sortie du pas : plus de clics
                const float e = (float) juce::jlimit (0.0, 1.0, juce::jmin (tt / fade, (stepLen - tt) / fade));
                L[i] = L[i] * (1.0f - e) + a * e;
                R[i] = R[i] * (1.0f - e) + b * e;
                ++t;
            }
        }

    private:
        Ring ring; StepClock clock;
        GlitchPattern perso;
        std::array<bool, NumLanes> lanes {};
        juce::int64 written = 0, stepIdx = -1, start = 0, t = 0;
        double tapePos = 0;
        float held[2] {}, gateG = 1.0f;
    };

    //--------------------------------------------------------------------------
    // Chroma : vue du spectre pour l'interface (une case par demi-ton, de C1 à B8) — écrite par l'effet, lue par l'écran
    inline std::array<std::array<float, 96>, numSlots>& chromaView() { static std::array<std::array<float, 96>, numSlots> v {}; return v; }
    inline std::array<int, numSlots>& chromaMaskView() { static std::array<int, numSlots> m {}; return m; }
    inline std::array<int, numSlots>& chromaKeyView() { static std::array<int, numSlots> m {}; return m; }

    class HarmonicFx : public Effect
    {
    public:
        // Façon Chroma (Xynth) : le son est séparé en fréquences DANS la gamme (gardées telles quelles, phase d'origine :
        // aucune coloration) et HORS gamme (déplacées vers la note de la gamme la plus proche, en gardant leur justesse fine).
        // Couleur 0-100 % = sec / traité ; au-delà de 100 % des résonances renforcent les notes de la gamme.
        // Modes : Zéro latence (filtres, pour jouer en direct), Normal (FFT 2048), Haute qualité (FFT 4096, plus de recouvrement).
        static constexpr int maxOrder = 12, maxN = 1 << maxOrder, maxBins = maxN / 2 + 1;
        static constexpr int numRes = 72;   // notes C2..B7 pour le mode zéro latence

        static int modeOf (const float* p) { return step (p[6], 3); }
        static int sizeFor (int mode) { return mode == 2 ? 4096 : 2048; }
        static int hopFor (int mode) { return mode == 2 ? 512 : 512; }
        int latency (const float* p) const override { return modeOf (p) == 0 ? 0 : sizeFor (modeOf (p)); }

        void prepare (double s, int) override
        {
            sr = s;
            fft11 = std::make_unique<juce::dsp::FFT> (11);
            fft12 = std::make_unique<juce::dsp::FFT> (12);
            for (auto& c : chans) { c.in.assign ((size_t) maxN, 0.0f); c.out.assign ((size_t) maxN * 2, 0.0f); c.dry.assign ((size_t) maxN, 0.0f); }
            buf.assign ((size_t) maxN * 2, 0.0f);
            win.assign ((size_t) maxN, 0.0f);
            curMode = -1;
            reset();
        }
        void reset() override
        {
            for (auto& c : chans)
            {
                std::fill (c.in.begin(), c.in.end(), 0.0f); std::fill (c.out.begin(), c.out.end(), 0.0f); std::fill (c.dry.begin(), c.dry.end(), 0.0f);
                c.lastPhase.fill (0.0f); c.sumPhase.fill (0.0f);
                for (auto& b : c.res) b.reset();
            }
            fill = 0; readOut = 0; sinceFrame = 0; sig = -1;
        }

        void process (float* L, float* R, int n, const float* p, [[maybe_unused]] double bpm) override
        {
            for (int k = 0; k < 8; ++k) prm[k] = p[k];
            const int mode = modeOf (p);
            if (mode != curMode) { setMode (mode); }
            const float color = prm[2] * 2.0f;                       // 0..200 %
            const float wet = juce::jmin (1.0f, color) * mix;
            const int keySel = step (prm[0], 13);
            key = keySel == 0 ? host().key : keySel - 1;
            mask = harmonicMask (step (prm[1], 14));
            if (mask == 0) mask = 0xFFF;
            chromaMaskView()[(size_t) juce::jlimit (0, numSlots - 1, slotIndex)] = mask;
            chromaKeyView()[(size_t) juce::jlimit (0, numSlots - 1, slotIndex)] = key;
            float* io[2] = { L, R };

            if (mode == 0)   // zéro latence : creuse les notes hors gamme, fait résonner celles de la gamme
            {
                updateFilters (color);
                for (int i = 0; i < n; ++i)
                    for (int c = 0; c < 2; ++c)
                    {
                        const float x = io[c][i];
                        float y = x;
                        for (int r = 0; r < numRes; ++r) if (active[(size_t) r]) y = chans[c].res[(size_t) r].process (y, 0);
                        y *= tiltGain;
                        io[c][i] = x + (y - x) * wet;
                    }
                for (int i = 0; i < n; ++i)   // analyse seulement pour l'écran (pas de latence ajoutée au son)
                {
                    chans[0].in[(size_t) fill] = 0.5f * (io[0][i] + io[1][i]);
                    fill = (fill + 1) % 2048;
                    if (++sinceFrame >= 1024) { sinceFrame = 0; viewOnly(); }
                }
                return;
            }

            const int N = sizeFor (mode);
            for (int i = 0; i < n; ++i)
            {
                for (int c = 0; c < 2; ++c)
                {
                    auto& ch = chans[c];
                    ch.in[(size_t) fill] = io[c][i];
                    const float delayedDry = ch.dry[(size_t) fill];
                    ch.dry[(size_t) fill] = io[c][i];
                    const float w = ch.out[(size_t) readOut];
                    ch.out[(size_t) readOut] = 0.0f;
                    io[c][i] = delayedDry + (w - delayedDry) * wet;
                }
                fill = (fill + 1) % N;
                readOut = (readOut + 1) % (N * 2);
                if (++sinceFrame >= hopFor (mode)) { sinceFrame = 0; for (int c = 0; c < 2; ++c) frame (chans[c], c, mode, color); }
            }
        }

    private:
        struct Chan
        {
            std::vector<float> in, out, dry;
            std::array<float, maxBins> lastPhase {}, sumPhase {};
            std::array<Biquad, numRes> res;
        };

        void setMode (int m)
        {
            curMode = m;
            const int N = sizeFor (m);
            for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N);
            reset();
        }

        // mode zéro latence : un filtre en cloche par note (C2..B7) : creux sur les notes hors gamme, bosse sur celles de la gamme
        void updateFilters (float color)
        {
            const float corr = prm[3], focus = prm[4];
            const int s = mask * 131 + key * 7 + (int) (color * 100) * 1009 + (int) (corr * 100) * 37 + (int) (focus * 100) * 3 + (int) (prm[7] * 50) * 7919;
            tiltGain = 1.0f;
            if (s == sig) return;
            sig = s;
            const float boost = juce::jmax (0.0f, color - 1.0f);
            const double q = 6.0 + focus * 30.0;
            for (int r = 0; r < numRes; ++r)
            {
                const int note = 36 + r;
                const bool in = ((mask >> (((note - key) % 12 + 12) % 12)) & 1) != 0;
                const double f = 440.0 * std::pow (2.0, (note - 69) / 12.0);
                const double gdb = in ? boost * 9.0 : -corr * 14.0;
                active[(size_t) r] = std::abs (gdb) > 0.05 && f < sr * 0.45;
                for (auto& ch : chans) ch.res[(size_t) r].set (2, sr, f, q, gdb);
            }
        }

        void viewOnly()
        {
            const int N = 2048;
            for (int i = 0; i < N; ++i) buf[(size_t) i] = chans[0].in[(size_t) ((fill + i) % N)] * win[(size_t) i];
            std::fill (buf.begin() + N, buf.begin() + 2 * N, 0.0f);
            fft11->performRealOnlyForwardTransform (buf.data(), true);
            auto& v = chromaView()[(size_t) juce::jlimit (0, numSlots - 1, slotIndex)];
            for (auto& x : v) x *= 0.75f;
            for (int k = 2; k < N / 2; ++k)
            {
                const double f = k * sr / N;
                const int nn = (int) std::round (69.0 + 12.0 * std::log2 (f / 440.0));
                if (nn < 24 || nn >= 120) continue;
                const float mg = std::sqrt (buf[(size_t) (2 * k)] * buf[(size_t) (2 * k)] + buf[(size_t) (2 * k + 1)] * buf[(size_t) (2 * k + 1)]);
                v[(size_t) (nn - 24)] = juce::jmax (v[(size_t) (nn - 24)], mg);
            }
        }

        void frame (Chan& ch, int channel, int mode, float color)
        {
            const int N = sizeFor (mode), hop = hopFor (mode), bins = N / 2 + 1;
            auto& fft = mode == 2 ? *fft12 : *fft11;
            const float corr = prm[3];                                               // 0 = rien, 1 = recalé d'un demi-ton entier
            const double preShift = std::pow (2.0, (step (prm[5], 25) - 12) / 12.0);
            const float tilt = lin (prm[7], -1.0f, 1.0f);
            const float boost = juce::jmax (0.0f, color - 1.0f);
            const double binHz = sr / N;
            const double expected = juce::MathConstants<double>::twoPi * hop / N;

            for (int i = 0; i < N; ++i) buf[(size_t) i] = ch.in[(size_t) ((fill + i) % N)] * win[(size_t) i];
            std::fill (buf.begin() + N, buf.begin() + 2 * N, 0.0f);
            fft.performRealOnlyForwardTransform (buf.data(), true);

            std::fill (outMag.begin(), outMag.begin() + bins, 0.0f);
            std::fill (outFreq.begin(), outFreq.begin() + bins, 0.0f);
            std::fill (keepPh.begin(), keepPh.begin() + bins, -100.0f);
            std::fill (domMag.begin(), domMag.begin() + bins, 0.0f);
            auto& view = chromaView()[(size_t) juce::jlimit (0, numSlots - 1, slotIndex)];
            if (channel == 0) for (auto& x : view) x *= 0.80f;

            for (int k = 0; k < bins; ++k)
            {
                const float re = buf[(size_t) (2 * k)], im = buf[(size_t) (2 * k + 1)];
                const float mag = std::sqrt (re * re + im * im);
                const float ph = std::atan2 (im, re);
                double d = ph - ch.lastPhase[(size_t) k] - expected * k;
                ch.lastPhase[(size_t) k] = ph;
                d -= juce::MathConstants<double>::twoPi * std::round (d / juce::MathConstants<double>::twoPi);
                const double f = (k + d / expected) * binHz;
                double ratio = preShift;
                bool shifted = std::abs (preShift - 1.0) > 1.0e-6;
                if (k >= 2 && f > 25.0 && mag > 1.0e-7f)
                {
                    const double m = 69.0 + 12.0 * std::log2 (f * preShift / 440.0);
                    const int n0 = (int) std::round (m);
                    const bool inKey = ((mask >> (((n0 - key) % 12 + 12) % 12)) & 1) != 0;
                    if (channel == 0 && n0 >= 24 && n0 < 120) view[(size_t) (n0 - 24)] = juce::jmax (view[(size_t) (n0 - 24)], mag);
                    if (! inKey && corr > 0.001f)
                    {
                        // note de la gamme la plus proche (en demi-tons entiers : le timbre et la justesse fine sont gardés)
                        int best = n0; double bd = 99.0;
                        for (int o = 1; o <= 6 && bd > 50.0; ++o)
                            for (int sgn : { -1, 1 })
                            {
                                const int cand = n0 + sgn * o;
                                if (((mask >> (((cand - key) % 12 + 12) % 12)) & 1) == 0) continue;
                                const double dist = std::abs (cand - m);
                                if (dist < bd) { bd = dist; best = cand; }
                            }
                        if (best != n0) { ratio *= std::pow (2.0, (best - n0) * corr / 12.0); shifted = true; }
                    }
                }
                // couleur > 100 % : les fréquences proches du centre d'une note de la gamme ressortent
                float g = 1.0f;
                const double fOut = f * ratio;
                if (boost > 0.001f && fOut > 25.0)
                {
                    const double m2 = 69.0 + 12.0 * std::log2 (fOut / 440.0);
                    const int nn = (int) std::round (m2);
                    const bool in2 = ((mask >> (((nn - key) % 12 + 12) % 12)) & 1) != 0;
                    const float close = (float) std::exp (-(m2 - nn) * (m2 - nn) / (0.006 + (1.0f - prm[4]) * 0.04));   // Focus : résonances plus ou moins serrées
                    g = in2 ? 1.0f + boost * 3.0f * close : 1.0f - boost * 0.5f;
                }
                if (std::abs (tilt) > 0.02f && fOut > 20.0) g *= std::pow ((float) (fOut / 1000.0), tilt * 0.3f);

                const int kk = shifted ? (int) std::round (k * ratio) : k;
                if (kk < 0 || kk >= bins) continue;
                const float mm = mag * g;
                outMag[(size_t) kk] += mm;
                if (mm > domMag[(size_t) kk])
                {
                    domMag[(size_t) kk] = mm;
                    if (shifted) { outFreq[(size_t) kk] = (float) fOut; keepPh[(size_t) kk] = -100.0f; }
                    else         { outFreq[(size_t) kk] = (float) f; keepPh[(size_t) kk] = ph; }   // dans la gamme : phase d'origine, son intact
                }
            }

            for (int k = 0; k < bins; ++k)
            {
                float phase;
                if (keepPh[(size_t) k] > -50.0f) phase = keepPh[(size_t) k];
                else
                {
                    const float fr = outFreq[(size_t) k] > 0.0f ? outFreq[(size_t) k] : (float) (k * binHz);
                    phase = ch.sumPhase[(size_t) k] + (float) (juce::MathConstants<double>::twoPi * fr * hop / sr);
                }
                phase = std::fmod (phase, juce::MathConstants<float>::twoPi);
                ch.sumPhase[(size_t) k] = phase;
                buf[(size_t) (2 * k)] = outMag[(size_t) k] * std::cos (phase);
                buf[(size_t) (2 * k + 1)] = outMag[(size_t) k] * std::sin (phase);
            }
            fft.performRealOnlyInverseTransform (buf.data());
            const float norm = 1.0f / (0.375f * (float) N / (float) hop);   // somme des fenêtres de Hann au carré
            for (int i = 0; i < N; ++i)
            {
                const int idx = (readOut + i) % (N * 2);   // latence fixe de N échantillons (le son sec est retardé d'autant)
                ch.out[(size_t) idx] += buf[(size_t) i] * win[(size_t) i] * norm;
            }
        }

        std::unique_ptr<juce::dsp::FFT> fft11, fft12;
        std::vector<float> win, buf;
        std::array<float, maxBins> outMag {}, outFreq {}, keepPh {}, domMag {};
        std::array<bool, numRes> active {};
        Chan chans[2];
        float prm[8] {}, tiltGain = 1.0f;
        int fill = 0, readOut = 0, sinceFrame = 0, curMode = -1, sig = -1, key = 0, mask = 0xAB5, viewTick = 0;
    };

    //--------------------------------------------------------------------------
    // Nappe : fabrique un pad à partir de n'importe quel son. Le son est adouci (montée lente),
    // accumulé dans un tampon qui peut "geler", relu par des grains (avec octaves), épaissi (chœur)
    // puis noyé dans une grande réverb. Les styles changent le timbre (cordes, chœur, glace, sombre…).
    class PadFx : public Effect
    {
    public:
        void prepare (double s, int maxBlock) override
        {
            sr = s;
            bufLen = (int) (s * 3.0);
            for (auto& b : buf) b.assign ((size_t) bufLen, 0.0f);
            for (auto& l : cho) l.prepare ((int) (s * 0.05));
            rev.prepare (s, maxBlock);
            reset();
        }
        void reset() override
        {
            for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
            for (auto& l : cho) l.clear();
            for (auto& f : filt) f.reset();
            for (auto& f : form) f.reset();
            rev.reset();
            w = 0; fastEnv = slowEnv = 0.0f; swell = 0.0f; lfo = 0.0f;
            for (int g = 0; g < numGrains; ++g) grains[(size_t) g] = { 0.0, 0.0, 1.0, (double) g / numGrains, 0.0f };
        }

        void process (float* L, float* R, int n, const float* p, [[maybe_unused]] double bpm) override
        {
            const int style = step (p[0], 6);
            const float size = p[1], riseMs = expMap (p[2], 30.0f, 3000.0f) * (float) tempoScale (2, p[2], bpm), gel = p[3], tilt = lin (p[4], -1.0f, 1.0f), motion = p[5], width = p[7];
            const int oct = step (p[6], 4);
            const double G = (style == 5 ? 0.07 : 0.20) * sr;   // longueur des grains
            const double jitter = (style == 5 ? 0.9 : 0.35) * sr;

            // couleur du style
            if (style != lastStyle || std::abs (tilt - lastTilt) > 0.01f)
            {
                lastStyle = style; lastTilt = tilt;
                const double lpHz[] = { 9000, 6500, 7000, 16000, 1400, 11000 };
                const double hpHz[] = { 60, 90, 120, 300, 30, 80 };
                for (int c = 0; c < 2; ++c)
                {
                    filt[(size_t) c * 4 + 0].set (4, sr, lpHz[style] * std::pow (2.0, tilt * 1.5), 0.7, 0);
                    filt[(size_t) c * 4 + 1].set (3, sr, hpHz[style], 0.7, 0);
                    filt[(size_t) c * 4 + 2].set (1, sr, 5000, 0.7, tilt * 6.0);
                    filt[(size_t) c * 4 + 3].set (0, sr, 200, 0.7, -tilt * 3.0);
                    const double fHz[] = { 700, 1150, 2600 };
                    for (int k = 0; k < 3; ++k) form[(size_t) c * 3 + k].set (2, sr, fHz[k], 6.0, 9.0 - k * 2.0);
                }
            }
            float rp[numP] = { 0.35f + size * 0.6f, 0.15f, style == 4 ? 0.75f : 0.35f, 0.35f + motion * 0.5f,
                               style == 3 ? 0.65f : 0.0f, 0.55f + size * 0.45f, 0.0f, 0.0f };

            const float rise = 1.0f - std::exp (-1.0f / (riseMs * 0.001f * (float) sr));
            const float fastA = coef (sr, 200.0), slowA = coef (sr, 4.0);
            const float keep = 0.97f * gel;    // gel : le tampon garde ce qu'il a (1 = figé)
            const float chRate = (0.15f + motion * 1.2f) / (float) sr * twoPi;

            for (int i = 0; i < n; ++i)
            {
                const float in[2] = { L[i], R[i] };
                // montée douce : chaque nouvelle attaque repart de presque zéro puis gonfle
                const float lvl = juce::jmax (std::abs (in[0]), std::abs (in[1]));
                fastEnv += fastA * (lvl - fastEnv);
                slowEnv += slowA * (lvl - slowEnv);
                if (fastEnv > slowEnv * 2.5f + 0.01f && swell > 0.5f) swell *= 0.15f;
                swell += rise * (1.0f - swell);

                for (int c = 0; c < 2; ++c)
                {
                    auto& b = buf[(size_t) c][(size_t) w];
                    b = b * keep + in[c] * swell * (1.0f - keep);
                }

                // grains
                float g2[2] = { 0, 0 };
                for (int k = 0; k < numGrains; ++k)
                {
                    auto& gr = grains[(size_t) k];
                    gr.phase += 1.0 / G;
                    if (gr.phase >= 1.0)
                    {
                        gr.phase -= 1.0;
                        rng = rng * 1664525u + 1013904223u;
                        const double r01 = (double) (rng >> 8) / 16777216.0;
                        double ratio = 1.0;
                        if (oct == 0 && (k % 2 == 1)) ratio = 0.5;
                        else if (oct == 2 && (k % 2 == 1)) ratio = 2.0;
                        else if (oct == 3) ratio = (k % 3 == 1) ? 0.5 : ((k % 3 == 2) ? 2.0 : 1.0);
                        if (style == 3 && k == 0) ratio = 2.0;
                        if (style == 4 && k == 1) ratio = 0.5;
                        gr.ratio = ratio;
                        gr.start = (double) w - (G * ratio + 0.02 * sr + r01 * jitter);
                        gr.pan = (float) (r01 * 2.0 - 1.0) * width;
                    }
                    const double pos = gr.start + gr.phase * G * gr.ratio;
                    const float win = 0.5f - 0.5f * std::cos (twoPi * (float) gr.phase);
                    for (int c = 0; c < 2; ++c)
                    {
                        const float v = readBuf (c, pos) * win;
                        const float pg = c == 0 ? 1.0f - juce::jmax (0.0f, gr.pan) : 1.0f + juce::jmin (0.0f, gr.pan);
                        g2[c] += v * pg;
                    }
                }
                w = (w + 1) % bufLen;

                // chœur / ensemble + couleur
                lfo += chRate; if (lfo > twoPi) lfo -= twoPi;
                float o[2];
                for (int c = 0; c < 2; ++c)
                {
                    float v = g2[c] * 0.45f;
                    cho[(size_t) c].push (v);
                    const double d1 = (8.0 + 5.0 * std::sin (lfo + c * 1.7f)) * 0.001 * sr;
                    const double d2 = (13.0 + 6.0 * std::sin (lfo * 0.7f + 2.0f + c)) * 0.001 * sr;
                    const float chAmt = style == 1 ? 0.8f : 0.45f;
                    v = v * (1.0f - chAmt * 0.5f) + (cho[(size_t) c].read (d1) + cho[(size_t) c].read (d2)) * chAmt * 0.5f;
                    if (style == 2) { float f = 0; for (int k = 0; k < 3; ++k) f += form[(size_t) c * 3 + k].process (v, 0); v = v * 0.35f + f * 0.35f; }
                    for (int k = 0; k < 4; ++k) v = filt[(size_t) c * 4 + k].process (v, 0);
                    o[c] = v;
                }
                // largeur (mid / side)
                const float mid = 0.5f * (o[0] + o[1]), side = 0.5f * (o[0] - o[1]) * (0.5f + width);
                L[i] = mid + side; R[i] = mid - side;
            }
            // grande réverb en parallèle
            int off = 0;
            while (off < n)
            {
                const int len = juce::jmin (512, n - off);
                for (int i = 0; i < len; ++i) { tmpL[(size_t) i] = L[off + i]; tmpR[(size_t) i] = R[off + i]; }
                rev.process (tmpL.data(), tmpR.data(), len, rp, bpm);
                const float wetR = 0.55f + size * 0.45f;
                for (int i = 0; i < len; ++i) { L[off + i] = L[off + i] * 0.75f + tmpL[(size_t) i] * wetR; R[off + i] = R[off + i] * 0.75f + tmpR[(size_t) i] * wetR; }
                off += len;
            }
        }

    private:
        float readBuf (int c, double pos) const
        {
            while (pos < 0) pos += bufLen;
            const int i0 = (int) pos % bufLen, i1 = (i0 + 1) % bufLen;
            const float t = (float) (pos - std::floor (pos));
            return buf[(size_t) c][(size_t) i0] * (1.0f - t) + buf[(size_t) c][(size_t) i1] * t;
        }
        struct Grain { double start, phase, ratio, dummy; float pan; };
        static constexpr int numGrains = 6;
        std::array<std::vector<float>, 2> buf;
        int bufLen = 1, w = 0;
        std::array<Grain, numGrains> grains {};
        std::array<Line, 2> cho;
        std::array<Biquad, 8> filt;
        std::array<Biquad, 6> form;
        std::array<float, 512> tmpL {}, tmpR {};
        ReverbFx rev;
        float fastEnv = 0, slowEnv = 0, swell = 0, lfo = 0, lastTilt = -9.0f;
        int lastStyle = -1;
        juce::uint32 rng = 12345;
    };

    //--------------------------------------------------------------------------
    class LiquidFx : public Effect
    {
    public:
        void prepare (double s, int) override
        {
            sr = s;
            for (auto& l : lines) l.prepare ((int) (s * 0.06));
            reset();
        }
        void reset() override
        {
            for (auto& l : lines) l.clear();
            for (auto& c : ap) for (auto& a : c) a = 0.0f;
            fb[0] = fb[1] = 0.0f; smoothV = -1.0f; freeBeat = 0.0;
        }

        void process (float* L, float* R, int n, const float* p, double bpm) override
        {
            const int type = step (p[0], 3);
            static const double lens[] = { 2.0, 4.0, 8.0, 16.0 };
            const double lenBeats = lens[step (p[1], 4)];
            const float centre = p[2], depth = p[3], stereo = p[4], feedback = p[5] * 0.92f, smooth = p[6];
            const int curveIdx = step (p[7], liquidCurveNames().size());
            Envelope custom;
            if (curveIdx == 0 && stores != nullptr && stores->liquid.lock.tryEnter())
            {
                custom = stores->liquid.env[(size_t) juce::jlimit (0, fxs::numSlots - 1, slotIndex)];
                stores->liquid.lock.exit();
                lastCustom = custom;
            }
            else custom = lastCustom;
            auto curve = [&] (float x) { x = x - std::floor (x); return curveIdx == 0 ? (custom.n > 0 ? custom.at (x) : 0.5f) : liquidShape (curveIdx, x); };

            const auto& h = host();
            const double bps = juce::jmax (20.0, bpm) / 60.0 / sr;
            double beat = h.playing ? h.ppq : freeBeat;
            const float sm = 1.0f - std::exp (-1.0f / (float) ((0.002 + smooth * 0.25) * sr));   // lissage de la courbe

            for (int i = 0; i < n; ++i)
            {
                const float x = (float) (beat / lenBeats);
                beat += bps;
                const float target = curve (x);
                if (smoothV < 0.0f) smoothV = target;
                smoothV += sm * (target - smoothV);
                const float vR = juce::jlimit (0.0f, 1.0f, smoothV + stereo * (curve (x + 0.25f) - smoothV));   // la droite est décalée
                float in[2] = { L[i], R[i] };
                float out[2];
                for (int c = 0; c < 2; ++c)
                {
                    const float v = c == 0 ? smoothV : vR;
                    if (type == 2)   // phaser : 6 passe-tout dont la fréquence suit la courbe
                    {
                        const float f = 200.0f * std::pow (2.0f, centre * 5.0f) * std::pow (2.0f, (v - 0.5f) * depth * 6.0f);
                        const float t = std::tan (juce::MathConstants<float>::pi * juce::jlimit (20.0f, 18000.0f, f) / (float) sr);
                        const float a = (t - 1.0f) / (t + 1.0f);
                        float y = in[c] + fb[c] * feedback;
                        for (int k = 0; k < 6; ++k)
                        {
                            const float z = ap[c][(size_t) k];
                            const float o = a * y + z;
                            ap[c][(size_t) k] = y - a * o;
                            y = o;
                        }
                        fb[c] = y;
                        out[c] = 0.5f * (in[c] + y);
                    }
                    else
                    {
                        // flanger : retard court qui balaie ; chorus : 2 retards plus longs
                        const double baseMs = type == 0 ? 0.3 + centre * 4.0 : 8.0 + centre * 14.0;
                        const double swingMs = (type == 0 ? 5.0 : 8.0) * depth;
                        const double d = (baseMs + swingMs * v) * 0.001 * sr;
                        lines[(size_t) c].push (in[c] + fb[c] * (type == 0 ? feedback : feedback * 0.4f));
                        float y = lines[(size_t) c].read (juce::jmax (2.0, d));
                        if (type == 1) y = 0.5f * (y + lines[(size_t) c].read (juce::jmax (2.0, d * 1.37 + 0.003 * sr)));
                        fb[c] = y;
                        out[c] = 0.5f * (in[c] + y);
                    }
                }
                L[i] = out[0]; R[i] = out[1];
            }
            if (! h.playing) freeBeat = beat;
            phaseOut.store ((float) std::fmod (beat / lenBeats, 1.0));
        }

        std::atomic<float> phaseOut { 0.0f };

    private:
        std::array<Line, 2> lines;
        std::array<std::array<float, 6>, 2> ap {};
        float fb[2] {}, smoothV = -1.0f;
        double freeBeat = 0.0;
        Envelope lastCustom;
    };

    //--------------------------------------------------------------------------
    // Bruit : ajoute du souffle (blanc, rose, vinyle, bande, radio) qui peut suivre le son ou pulser en rythme
    class NoiseFx : public Effect
    {
    public:
        void prepare (double s, int) override { sr = s; reset(); }
        void reset() override { for (auto& c : ch) c = Chan(); env = 0.0f; }

        void process (float* L, float* R, int n, const float* p, double bpm) override
        {
            const int type = step (p[0], 5);
            const float level = juce::Decibels::decibelsToGain (lin (p[1], -60.0f, -6.0f));
            const float tone = lin (p[2], -1.0f, 1.0f);
            const float follow = p[3], width = p[4], crackle = p[5];
            const int rhythm = step (p[6], 4);
            static const double rates[] = { 0.0, 1.0, 0.5, 0.25 };
            const float lpC = coef (sr, expMap (juce::jlimit (0.0f, 1.0f, 0.5f + tone * 0.5f), 400.0f, 18000.0f));
            const float hpC = coef (sr, expMap (juce::jlimit (0.0f, 1.0f, tone * 0.5f), 20.0f, 3000.0f));
            const float envA = coef (sr, 60.0), envR = coef (sr, 4.0);
            const auto& h = host();
            const double bps = juce::jmax (20.0, bpm) / 60.0 / sr;
            double beat = h.playing ? h.ppq : freeBeat;
            for (int i = 0; i < n; ++i)
            {
                const float lvl = juce::jmax (std::abs (L[i]), std::abs (R[i]));
                env += (lvl > env ? envA : envR) * (lvl - env);
                float gain = level * ((1.0f - follow) + follow * juce::jlimit (0.0f, 1.0f, env * 4.0f));
                if (rhythm > 0)   // le souffle pulse en rythme (calé sur FL)
                {
                    const double ph = std::fmod (beat / rates[rhythm], 1.0);
                    gain *= (float) std::pow (1.0 - ph, 3.0);
                }
                beat += bps;
                const float mono = rnd();
                float o[2];
                for (int c = 0; c < 2; ++c)
                {
                    auto& st = ch[c];
                    float x = width * rnd() + (1.0f - width) * mono;
                    if (type == 1 || type == 3)   // rose (filtre de Paul Kellet, version courte)
                    {
                        st.b0 = 0.99765f * st.b0 + x * 0.0990460f; st.b1 = 0.96300f * st.b1 + x * 0.2965164f; st.b2 = 0.57000f * st.b2 + x * 1.0526913f;
                        x = (st.b0 + st.b1 + st.b2 + x * 0.1848f) * 0.25f;
                    }
                    if (type == 2)   // vinyle : souffle doux + craquements
                    {
                        x *= 0.25f;
                        if (rnd01() < crackle * 0.0009f) st.pop = (rnd01() * 0.9f + 0.1f) * (rnd() > 0 ? 1.0f : -1.0f);
                        x += st.pop; st.pop *= 0.62f;
                    }
                    if (type == 4)   // radio : bande étroite qui ondule
                    {
                        st.lfo += 0.7f / (float) sr; if (st.lfo > 1.0f) st.lfo -= 1.0f;
                        x *= 0.6f + 0.4f * std::sin (st.lfo * twoPi * 3.0f);
                    }
                    st.lp += lpC * (x - st.lp);
                    st.hp += hpC * (st.lp - st.hp);
                    float y = st.lp - st.hp;
                    if (type == 4) y = std::tanh (y * 3.0f) * 0.5f;
                    o[c] = y * gain;
                }
                L[i] += o[0]; R[i] += o[1];
            }
            if (! h.playing) freeBeat = beat;
        }

    private:
        struct Chan { float b0 = 0, b1 = 0, b2 = 0, lp = 0, hp = 0, pop = 0, lfo = 0; };
        Chan ch[2];
        float env = 0.0f;
        double freeBeat = 0.0;
        juce::uint32 seed = 22222;
        float rnd() { seed = seed * 1664525u + 1013904223u; return (float) (seed >> 8) / 8388608.0f - 1.0f; }
        float rnd01() { return 0.5f + 0.5f * rnd(); }
    };

    inline std::unique_ptr<Effect> create (int t)
    {
        switch (t)
        {
            case Saturation: return std::make_unique<SaturationFx>();
            case Filter:     return std::make_unique<FilterFx>();
            case Compressor: return std::make_unique<CompressorFx>();
            case EQ:         return std::make_unique<EqFx>();
            case Chorus:     return std::make_unique<ChorusFx>();
            case Flanger:    return std::make_unique<FlangerFx>();
            case Phaser:     return std::make_unique<PhaserFx>();
            case Tremolo:    return std::make_unique<TremoloFx>();
            case Lofi:       return std::make_unique<LofiFx>();
            case Portal:     return std::make_unique<PortalFx>();
            case Crystal:    return std::make_unique<CrystalFx>();
            case Delay:      return std::make_unique<DelayFx>();
            case Reverb:     return std::make_unique<ReverbFx>();
            case Stereo:     return std::make_unique<StereoFx>();
            case HalfTime:   return std::make_unique<HalfTimeFx>();
            case Shaper:     return std::make_unique<ShaperFx>();
            case Glitch:     return std::make_unique<GlitchFx>();
            case Harmonic:   return std::make_unique<HarmonicFx>();
            case Pad:        return std::make_unique<PadFx>();
            case Liquid:     return std::make_unique<LiquidFx>();
            case Noise:      return std::make_unique<NoiseFx>();
            default:         return nullptr;
        }
    }

    struct SlotParams
    {
        std::atomic<float>* type = nullptr;
        std::atomic<float>* on = nullptr;
        std::atomic<float>* mix = nullptr;
        std::array<std::atomic<float>*, numP> p {};
    };

    // Un slot crée l'effet choisi la première fois qu'il sert (hors du thread audio : ensure()),
    // puis le garde : pas d'allocation en temps réel, et 16 places ne coûtent rien tant qu'elles sont vides
    class Slot
    {
    public:
        Slot() { for (auto& f : fx) f.store (nullptr); }
        int index = 0;
        Stores* stores = nullptr;

        void prepare (double sr, int maxBlock)
        {
            const juce::SpinLock::ScopedLockType l (createLock);
            prepSr = sr; prepBlock = maxBlock; prepared = true;
            for (auto& f : owned) if (f) f->prepare (sr, maxBlock);
            dryL.assign ((size_t) juce::jmax (1, maxBlock), 0.0f);
            dryR.assign ((size_t) juce::jmax (1, maxBlock), 0.0f);
            lastType = -1;
        }

        // hors du thread audio : crée (et prépare) l'effet de ce type s'il n'existe pas encore
        void ensure (int t)
        {
            if (t <= 0 || t >= NumTypes || fx[(size_t) t].load() != nullptr) return;
            auto e = create (t);
            if (e == nullptr) return;
            const juce::SpinLock::ScopedLockType l (createLock);
            if (fx[(size_t) t].load() != nullptr) return;
            if (prepared) e->prepare (prepSr, prepBlock);
            fx[(size_t) t].store (e.get());
            owned[(size_t) t] = std::move (e);
        }

        void process (float* L, float* R, int n, const SlotParams& sp, double bpm)
        {
            const int t = (int) sp.type->load();
            if (t <= 0 || t >= NumTypes || sp.on->load() < 0.5f) { lastType = t; return; }
            auto* e = fx[(size_t) t].load();
            if (e == nullptr) { needsCreate.store (true); return; }   // pas encore créé : le son passe tel quel un instant
            if (t != lastType) { e->reset(); e->clearTempoRefs(); lastType = t; }

            float p[numP];
            for (int i = 0; i < numP; ++i) p[i] = sp.p[(size_t) i]->load();
            const float mix = sp.mix->load();
            const bool wetOnly = def (t).wetOnly;

            for (int off = 0; off < n; )
            {
                const int len = juce::jmin (n - off, (int) dryL.size());
                std::copy (L + off, L + off + len, dryL.begin());
                std::copy (R + off, R + off + len, dryR.begin());
                e->mix = mix; e->slotIndex = index; e->stores = stores;
                e->process (L + off, R + off, len, p, bpm);
                if (def (t).selfMix) { off += len; continue; }

                const float dg = wetOnly ? juce::jmin (1.0f, 2.0f * (1.0f - mix)) : 1.0f - mix;
                const float wg = wetOnly ? juce::jmin (1.0f, 2.0f * mix) : mix;
                for (int i = 0; i < len; ++i)
                {
                    L[off + i] = dryL[(size_t) i] * dg + L[off + i] * wg;
                    R[off + i] = dryR[(size_t) i] * dg + R[off + i] * wg;
                }
                off += len;
            }
        }

        int latency (const SlotParams& sp) const
        {
            const int t = (int) sp.type->load();
            auto* e = (t <= 0 || t >= NumTypes) ? nullptr : fx[(size_t) t].load();
            if (e == nullptr || sp.on->load() < 0.5f) return 0;
            float p[numP];
            for (int i = 0; i < numP; ++i) p[i] = sp.p[(size_t) i]->load();
            return e->latency (p);
        }
        std::atomic<bool> needsCreate { false };

    private:
        std::array<std::atomic<Effect*>, NumTypes> fx;
        std::array<std::unique_ptr<Effect>, NumTypes> owned;
        juce::SpinLock createLock;
        double prepSr = 44100.0;
        int prepBlock = 512;
        bool prepared = false;
        std::vector<float> dryL, dryR;
        int lastType = -1;
    };

    class Chain
    {
    public:
        Chain() { for (int i = 0; i < numSlots; ++i) slots[(size_t) i].index = i; }
        void setStores (Stores* s) { for (auto& sl : slots) sl.stores = s; }
        void prepare (double sr, int maxBlock) { for (auto& s : slots) s.prepare (sr, maxBlock); }
        // hors du thread audio : crée les effets choisis dans la chaîne (appelé dès qu'un type change)
        void ensure (const std::array<SlotParams, numSlots>& sp)
        {
            for (int s = 0; s < numSlots; ++s)
            {
                slots[(size_t) s].needsCreate.store (false);
                if (sp[(size_t) s].type != nullptr) slots[(size_t) s].ensure ((int) sp[(size_t) s].type->load());
            }
        }
        bool anyNeedsCreate() const { for (auto& s : slots) if (s.needsCreate.load()) return true; return false; }

        void process (juce::AudioBuffer<float>& b, const std::array<SlotParams, numSlots>& sp, double bpm, double ppq, bool playing,
                      float width, float outGain)
        {
            if (b.getNumChannels() < 2) return;
            float* L = b.getWritePointer (0);
            float* R = b.getWritePointer (1);
            const int n = b.getNumSamples();
            host().bpm = bpm; host().ppq = ppq; host().playing = playing;
            for (int s = 0; s < numSlots; ++s) slots[(size_t) s].process (L, R, n, sp[(size_t) s], bpm);

            for (int i = 0; i < n; ++i)
            {
                const float mid = 0.5f * (L[i] + R[i]), side = 0.5f * (L[i] - R[i]) * width;
                L[i] = (mid + side) * outGain;
                R[i] = (mid - side) * outGain;
            }
        }

        int latency (const std::array<SlotParams, numSlots>& sp) const { int l = 0; for (int s = 0; s < numSlots; ++s) l += slots[(size_t) s].latency (sp[(size_t) s]); return l; }

    private:
        std::array<Slot, numSlots> slots;
    };
}
