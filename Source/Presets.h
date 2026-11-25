#pragma once
// Presets d'arpège : un son de la bibliothèque + tous les réglages (arpège, accords, enveloppe, effets).
// Les presets "usine" cherchent un son adapté dans la bibliothèque de l'utilisateur (par mots-clés),
// les presets perso sont des fichiers .vpreset dans Documents\Variant Presets.
#include <juce_core/juce_core.h>
#include <vector>
#include "ArpCore.h"
#include "FxSlots.h"
#include "Library.h"
#include "SynthPresets.h"

namespace presets
{
    struct Fx { int type = 0, preset = 0; float mix = -1.0f; };
    struct Factory
    {
        const char* name;
        std::vector<const char*> keywords;   // mots cherchés dans le nom du son ou de la catégorie
        int mode, rate, octaves, octDir;
        float gate, swing;
        const char* steps;                   // 16 chiffres : 0 = silence, 9 = plein
        bool chord; int chordSize;           // accords automatiques (une note = un accord arpégé)
        float attack, decay, sustain, release;
        std::vector<Fx> fx;
    };

    inline const std::vector<Factory>& factory()
    {
        using namespace arp;
        static const std::vector<Factory> f {
            { "Pluck trap 1/16",          { "pluck", "pizz", "harp", "harpe" },            Up,         R1_16,  2, OctUp,   0.45f, 0.10f, "9696969696969696", false, 0, 1, 260, 0.0f, 120, { { fxs::Delay, 0, 0.25f }, { fxs::Reverb, 2, 0.20f } } },
            { "Cloches qui tombent",      { "bell", "cloche", "glock", "celesta" },        Down,       R1_16T, 2, OctDown, 0.55f, 0.00f, "9575957595759575", false, 0, 1, 500, 0.0f, 300, { { fxs::Reverb, 5, 0.35f } } },
            { "Rhodes rêveur",            { "rhodes", "epiano", "e-piano", "keys" },       UpDown,     R1_8,   1, OctUp,   0.85f, 0.15f, "9797979797979797", false, 1, 4, 900, 0.6f, 400, { { fxs::Chorus, 0, 0.40f }, { fxs::Reverb, 3, 0.25f } } },
            { "Piano ballade",            { "piano" },                                     UpDownIncl, R1_8,   2, OctUp,   1.00f, 0.00f, "9777977797779777", false, 1, 2, 1500, 0.4f, 600, { { fxs::Reverb, 3, 0.25f } } },
            { "Lead synthé rapide",       { "lead", "saw", "synth", "supersaw" },          Up,         R1_16,  3, OctUp,   0.45f, 0.00f, "9999999999999999", false, 0, 1, 300, 0.0f, 130,  { { fxs::Delay, 1, 0.30f }, { fxs::Reverb, 2, 0.15f } } },
            { "Lo-fi aléatoire",          { "keys", "piano", "guitar", "guitare", "rhodes" }, Random,  R1_8,   2, OctBoth, 0.70f, 0.35f, "9070907090709070", false, 1, 3, 700, 0.3f, 300, { { fxs::Lofi, 0, 0.60f }, { fxs::Reverb, 1, 0.25f } } },
            { "Trance gate",              { "pad", "nappe", "supersaw", "synth" },         Up,         R1_16,  1, OctUp,   0.50f, 0.00f, "9090990990909909", false, 1, 2, 260, 0.0f, 60,  { { fxs::Delay, 1, 0.20f }, { fxs::Reverb, 2, 0.20f } } },
            { "Guitare picking",          { "guitar", "guitare", "nylon", "acoustic" },    Thumb,      R1_8,   1, OctUp,   0.90f, 0.20f, "9686968696869686", false, 1, 1, 1200, 0.5f, 400, { { fxs::Reverb, 1, 0.20f } } },
            { "Drill pinky 1/16T",        { "bell", "pluck", "piano", "cloche" },          PinkyUp,    R1_16T, 1, OctUp,   0.60f, 0.00f, "9696969696969696", false, 0, 1, 400, 0.0f, 200, { { fxs::Delay, 0, 0.20f }, { fxs::Reverb, 0, 0.20f } } },
            { "Cristal convergent",       { "crystal", "cristal", "glass", "kalimba", "mallet" }, Converge, R1_16, 2, OctBoth, 0.55f, 0.05f, "9797979797979797", false, 1, 1, 450, 0.0f, 250, { { fxs::Crystal, 0, 0.35f }, { fxs::Reverb, 5, 0.30f } } },
            { "Nappe ambient lente",      { "pad", "nappe", "string", "cordes", "choir" }, UpDown,     R1_4,   2, OctBoth, 1.40f, 0.00f, "9999999999999999", false, 2, 1, 1800, 0.9f, 1500, { { fxs::Pad, 0, 0.70f } } },
            { "Cuivres staccato",         { "sax", "trumpet", "trompette", "brass", "cuivre" }, Played, R1_8,   1, OctUp,   0.50f, 0.20f, "9090909099009090", false, 0, 15, 400, 0.2f, 150, { { fxs::Reverb, 1, 0.20f } } },
            { "Harpe céleste",            { "harp", "harpe", "pluck" },                    UpDownIncl, R1_16,  2, OctUp,   0.70f, 0.05f, "9676967696769676", false, 0, 1, 900, 0.2f, 500, { { fxs::Reverb, 5, 0.30f } } },
            { "Marimba tropical",         { "marimba", "mallet", "kalimba", "wood" },      Played,     R1_16,  1, OctUp,   0.55f, 0.30f, "9070970790709707", false, 0, 1, 400, 0.0f, 200, { { fxs::Delay, 0, 0.20f }, { fxs::Reverb, 1, 0.15f } } },
            { "Pluck house",              { "pluck", "synth", "stab" },                    UpDown,     R1_16,  1, OctUp,   0.40f, 0.20f, "9070907090709070", false, 0, 1, 250, 0.0f, 120, { { fxs::Delay, 1, 0.25f }, { fxs::Reverb, 2, 0.15f } } },
            { "Rhodes néo-soul",          { "rhodes", "epiano", "keys", "wurli" },         DownUp,     R1_8T,  1, OctUp,   0.80f, 0.30f, "9767976797679767", false, 0, 4, 1000, 0.5f, 400, { { fxs::Chorus, 0, 0.35f }, { fxs::Reverb, 1, 0.20f } } },
            { "Guitare drill",            { "guitar", "guitare", "nylon" },                Down,       R1_16T, 1, OctDown, 0.60f, 0.00f, "9696969696969696", false, 0, 1, 700, 0.2f, 250, { { fxs::Reverb, 2, 0.20f } } },
            { "Lead 8 bits",              { "chip", "8bit", "8-bit", "square", "lead" },   Up,         R1_16,  3, OctUp,   0.45f, 0.00f, "9999999999999999", false, 0, 1, 280, 0.0f, 120,  { { fxs::Lofi, 0, 0.40f }, { fxs::Delay, 1, 0.20f } } },
            { "Pad pulsé",                { "pad", "nappe", "string" },                    Up,         R1_8,   1, OctUp,   0.90f, 0.00f, "9999999999999999", false, 0, 30, 1500, 0.8f, 600, { { fxs::Reverb, 4, 0.30f } } },
            { "Cloches inversées",        { "bell", "cloche", "glock" },                   Converge,   R1_16,  2, OctBoth, 0.50f, 0.00f, "9575957595759575", false, 0, 1, 600, 0.0f, 400, { { fxs::Crystal, 0, 0.30f }, { fxs::Reverb, 5, 0.25f } } },
            { "Piano rapide 1/32",        { "piano" },                                     Up,         R1_32,  1, OctUp,   0.50f, 0.00f, "9777977797779777", false, 0, 1, 800, 0.3f, 300, { { fxs::Reverb, 3, 0.20f } } },
            { "Flûte mélancolique",       { "flute", "flûte", "wind", "pan" },             UpDown,     R1_8,   1, OctUp,   0.85f, 0.10f, "9787978797879787", false, 0, 40, 1200, 0.6f, 500, { { fxs::Delay, 0, 0.20f }, { fxs::Reverb, 3, 0.25f } } },
            { "Cordes spiccato",          { "string", "cordes", "violin", "violon", "pizz" }, Up,      R1_16,  1, OctUp,   0.30f, 0.00f, "9686968696869686", false, 0, 5, 300, 0.0f, 150, { { fxs::Reverb, 3, 0.25f } } },
            { "Basse arpégée",            { "bass", "basse", "sub", "808" },               Thumb,      R1_8,   1, OctDown, 0.60f, 0.10f, "9070907790709077", false, 0, 1, 500, 0.4f, 150, { { fxs::Saturation, 0, 0.30f } } },
            { "Vocal chop arp",           { "vox", "voice", "vocal", "choir", "chœur" },  Random,     R1_16,  1, OctUp,   0.40f, 0.10f, "9090990990909909", false, 0, 1, 400, 0.0f, 150, { { fxs::Delay, 1, 0.25f }, { fxs::Reverb, 5, 0.25f } } },
            { "Trap triolets",            { "pluck", "bell", "cloche", "keys" },           UpDown,     R1_8T,  2, OctUp,   0.50f, 0.00f, "9797979797979797", false, 0, 1, 450, 0.0f, 200, { { fxs::Delay, 0, 0.20f }, { fxs::Reverb, 0, 0.20f } } },
            // --- v2.2 : encore plus de styles
            { "Afro pluck syncopé",       { "pluck", "kalimba", "marimba", "guitar" },     Played,     R1_16,  1, OctUp,   0.45f, 0.25f, "9079070970790709", false, 0, 1, 350, 0.0f, 180, { { fxs::Delay, 0, 0.20f }, { fxs::Reverb, 1, 0.15f } } },
            { "Jersey bounce",            { "bell", "pluck", "synth", "stab" },            UpDown,     R1_16,  1, OctUp,   0.50f, 0.15f, "9009009090090090", false, 0, 1, 350, 0.0f, 160, { { fxs::Delay, 1, 0.20f } } },
            { "Dancehall staccato",       { "keys", "piano", "organ", "orgue", "stab" },   Played,     R1_8,   1, OctUp,   0.50f, 0.10f, "0909090909090909", false, 0, 1, 380, 0.0f, 160,  { { fxs::Reverb, 1, 0.15f } } },
            { "Synthwave 80s",            { "synth", "saw", "lead", "pad" },               Up,         R1_16,  2, OctUp,   0.55f, 0.00f, "9777977797779777", false, 0, 1, 300, 0.2f, 200, { { fxs::Chorus, 0, 0.35f }, { fxs::Delay, 1, 0.25f }, { fxs::Reverb, 2, 0.20f } } },
            { "Plugg nostalgique",        { "bell", "pluck", "keys", "cloche" },           UpDownIncl, R1_8,   2, OctUp,   0.70f, 0.10f, "9797979797979797", false, 0, 1, 600, 0.2f, 400, { { fxs::Lofi, 0, 0.30f }, { fxs::Reverb, 3, 0.30f } } },
            { "Rage lead",                { "lead", "synth", "saw", "supersaw" },          Up,         R1_16,  2, OctUp,   0.40f, 0.00f, "9999990999999909", false, 0, 1, 150, 0.1f, 80,  { { fxs::Saturation, 0, 0.35f }, { fxs::Delay, 1, 0.20f } } },
            { "Harpe en cascade",         { "harp", "harpe", "pluck" },                    Down,       R1_32,  3, OctDown, 0.80f, 0.00f, "9876987698769876", false, 0, 1, 700, 0.1f, 500, { { fxs::Reverb, 5, 0.30f } } },
            { "Piano jazz swing",         { "piano", "rhodes", "keys" },                   Random,     R1_8T,  1, OctBoth, 0.75f, 0.45f, "9707970797079707", false, 0, 1, 900, 0.3f, 300, { { fxs::Reverb, 1, 0.20f } } },
            { "Dub techno chord",         { "pad", "keys", "stab", "synth" },              Played,     R1_8D,  1, OctUp,   0.30f, 0.00f, "9000900090009000", false, 0, 1, 200, 0.0f, 150, { { fxs::Delay, 1, 0.45f }, { fxs::Reverb, 4, 0.30f } } },
            { "Lo-fi guitare douce",      { "guitar", "guitare", "nylon", "acoustic" },    UpDown,     R1_8,   1, OctUp,   0.95f, 0.30f, "9787978797879787", false, 0, 1, 1100, 0.4f, 500, { { fxs::Lofi, 0, 0.45f }, { fxs::Reverb, 1, 0.20f } } },
            { "Cloches drill sombres",    { "bell", "cloche", "glock", "celesta" },        DownUp,     R1_16T, 1, OctDown, 0.55f, 0.00f, "9696969696969696", false, 0, 1, 500, 0.0f, 300, { { fxs::Delay, 0, 0.20f }, { fxs::Reverb, 5, 0.25f } } },
            { "Kalimba berceuse",         { "kalimba", "mallet", "music box", "boite" },   Converge,   R1_8,   2, OctUp,   0.80f, 0.15f, "9797979797979797", false, 0, 1, 700, 0.1f, 500, { { fxs::Crystal, 0, 0.25f }, { fxs::Reverb, 5, 0.30f } } },
            { "Cordes épiques",           { "string", "cordes", "violin", "violon" },      Up,         R1_16,  2, OctUp,   0.65f, 0.00f, "9686968696869686", false, 0, 5, 400, 0.4f, 300, { { fxs::Reverb, 4, 0.30f } } },
            { "Pluck tropical house",     { "pluck", "marimba", "kalimba", "steel" },      Played,     R1_8,   1, OctUp,   0.40f, 0.20f, "9079907990799079", false, 0, 1, 300, 0.0f, 150, { { fxs::Delay, 0, 0.25f }, { fxs::Reverb, 2, 0.20f } } },
            { "Trance montante 1/16",     { "supersaw", "synth", "lead", "pad" },          Diverge,    R1_16,  2, OctBoth, 0.45f, 0.00f, "9999999999999999", false, 0, 1, 200, 0.0f, 100, { { fxs::Delay, 1, 0.25f }, { fxs::Reverb, 2, 0.20f } } },
            { "Basse pulsée 1/8",         { "bass", "basse", "sub", "808" },               Played,     R1_8,   1, OctUp,   0.55f, 0.00f, "9999999999999999", false, 0, 1, 300, 0.6f, 120, { { fxs::Saturation, 0, 0.25f } } },
            { "Synth bulles",             { "synth", "pluck", "bell", "chip" },            Random,     R1_32,  2, OctUp,   0.30f, 0.00f, "9090909090909090", false, 0, 1, 150, 0.0f, 80,  { { fxs::Delay, 1, 0.30f }, { fxs::Reverb, 5, 0.25f } } },
            { "Orgue gospel",             { "organ", "orgue", "keys" },                    UpDownIncl, R1_8T,  1, OctUp,   0.85f, 0.25f, "9777977797779777", false, 0, 1, 500, 0.7f, 300, { { fxs::Chorus, 0, 0.30f }, { fxs::Reverb, 1, 0.20f } } },
            { "Voix célestes",            { "choir", "chœur", "vox", "voice", "vocal" },   UpDown,     R1_4,   2, OctBoth, 1.20f, 0.00f, "9999999999999999", false, 0, 1, 1500, 0.8f, 1200, { { fxs::Pad, 0, 0.50f }, { fxs::Reverb, 5, 0.30f } } },
            { "Glitch hachuré",           { "synth", "pluck", "chip", "lead" },            Random,     R1_16,  1, OctUp,   0.40f, 0.00f, "9909099090990909", false, 0, 1, 220, 0.0f, 120,  { { fxs::Glitch, 0, 0.40f }, { fxs::Delay, 1, 0.15f } } },
        };
        return f;
    }

    // dossier des presets DANS la banque de sons (ils suivent la banque si tu la copies / partages)

    //==========================================================================
    // Presets d'arpège avec un son de SYNTHÉ fait pour eux (style Hive / Diva / Zenology) : pas besoin de sample.
    struct SynthArp
    {
        const char* name;
        int mode, rate, octaves, octDir;
        float gate, swing;
        const char* steps;
        bool chord; int chordSize;
        std::vector<Fx> fx;
        std::function<void (wt::Patch&, juce::String&, juce::String&)> make;
        float volume = 0.8f;
    };

    inline const std::vector<SynthArp>& synthArps()
    {
        using namespace arp;
        using namespace wt;
        using synthpresets::env; using synthpresets::mod; using synthpresets::lfo; using synthpresets::syncRate;
        static const std::vector<SynthArp> s {
            { "Synth - Pluck rebondissant", Up, R1_16, 2, OctUp, 0.50f, 0.00f, "9696969696969696", false, 0,
              { { fxs::Delay, 1, 0.25f }, { fxs::Reverb, 2, 0.20f } },
              [] (Patch& p, juce::String& a, juce::String& b) {
                  a = "builtin:0"; b = "builtin:1"; p.set (A_Pos, 0.66f); p.set (A_Uni, 5); p.set (A_Detune, 0.25f); p.set (A_Width, 1.0f);
                  p.set (B_On, 1); p.set (B_Pos, 0.85f); p.set (B_Oct, 1); p.set (B_Level, 0.28f);
                  p.set (F_Type, 1); p.set (F_Cutoff, 0.30f); p.set (F_Res, 0.30f);
                  env (p, 0, 0.001f, 0.35f, 0.0f, 0.25f); env (p, 1, 0.001f, 0.18f, 0.0f, 0.15f);
                  mod (p, 0, S_Env2, D_Cutoff, 0.65f); mod (p, 1, S_Vel, D_Cutoff, 0.2f); } },
            { "Synth - Arp analogique chaud", UpDown, R1_16, 2, OctUp, 0.70f, 0.10f, "9797979797979797", false, 0,
              { { fxs::Chorus, 0, 0.30f }, { fxs::Delay, 2, 0.20f }, { fxs::Reverb, 3, 0.20f } },
              [] (Patch& p, juce::String& a, juce::String&) {
                  a = "builtin:0"; p.set (A_Pos, 0.66f); p.set (A_Uni, 2); p.set (A_Detune, 0.08f); p.set (A_Rand, 1.0f);
                  p.set (F_Type, 1); p.set (F_Cutoff, 0.42f); p.set (F_Res, 0.35f); p.set (F_Drive, 0.25f); p.set (F_Key, 0.4f);
                  env (p, 0, 0.002f, 0.5f, 0.3f, 0.3f); env (p, 1, 0.002f, 0.30f, 0.1f, 0.25f);
                  mod (p, 0, S_Env2, D_Cutoff, 0.45f); lfo (p, 0, 0, 0.08f, false, false); mod (p, 1, S_Lfo1, D_Cutoff, 0.06f);
                  lfo (p, 1, 6, 0.25f, false, false); mod (p, 2, S_Lfo2, D_Pitch, 0.002f); } },
            { "Synth - Séquence Berlin", Up, R1_16, 3, OctUp, 0.40f, 0.00f, "9999999999999999", false, 0,
              { { fxs::Delay, 0, 0.30f }, { fxs::Reverb, 5, 0.25f } },
              [] (Patch& p, juce::String& a, juce::String&) {
                  a = "builtin:0"; p.set (A_Pos, 0.85f); p.set (F_Type, 1); p.set (F_Cutoff, 0.25f); p.set (F_Res, 0.55f);
                  env (p, 0, 0.001f, 0.3f, 0.2f, 0.2f); env (p, 1, 0.001f, 0.20f, 0.0f, 0.15f); mod (p, 0, S_Env2, D_Cutoff, 0.55f);
                  lfo (p, 0, 1, syncRate (1), true, false); mod (p, 1, S_Lfo1, D_Cutoff, 0.25f); } },
            { "Synth - Acid qui couine", Played, R1_16, 2, OctUp, 0.55f, 0.05f, "9590959995909599", false, 0,
              { { fxs::Saturation, 1, 0.50f }, { fxs::Delay, 0, 0.20f } },
              [] (Patch& p, juce::String& a, juce::String&) {
                  a = "serum:Analog/Acid.wav"; p.set (A_Pos, 0.2f); p.set (G_Mono, 2); p.set (G_Glide, 0.08f);
                  p.set (F_Type, 1); p.set (F_Cutoff, 0.18f); p.set (F_Res, 0.82f); p.set (F_Drive, 0.5f);
                  env (p, 0, 0.001f, 0.4f, 0.6f, 0.08f); env (p, 1, 0.001f, 0.22f, 0.0f, 0.1f);
                  mod (p, 0, S_Env2, D_Cutoff, 0.55f); mod (p, 1, S_Vel, D_Cutoff, 0.35f); } },
            { "Synth - Cloches glacées", UpDown, R1_16T, 2, OctUp, 0.80f, 0.00f, "9686968696869686", false, 0,
              { { fxs::Crystal, 0, 0.30f }, { fxs::Reverb, 5, 0.35f } },
              [] (Patch& p, juce::String& a, juce::String& b) {
                  a = "builtin:2"; b = "builtin:5"; p.set (A_Pos, 0.85f); p.set (B_On, 1); p.set (B_Semi, 7); p.set (B_Oct, 1); p.set (B_Level, 0.35f);
                  p.set (F_On, 0); p.set (A_Warp, 0.3f); env (p, 0, 0.001f, 1.6f, 0.0f, 1.2f);
                  env (p, 1, 0.001f, 0.4f, 0.0f, 0.3f); mod (p, 0, S_Env2, D_APos, -0.4f); } },
            { "Synth - Supersaw trance gate", Up, R1_16, 1, OctUp, 0.45f, 0.00f, "9090990990909909", false, 0,
              { { fxs::Delay, 1, 0.20f }, { fxs::Reverb, 2, 0.25f } },
              [] (Patch& p, juce::String& a, juce::String&) {
                  a = "builtin:0"; p.set (A_Pos, 0.66f); p.set (A_Uni, 7); p.set (A_Detune, 0.40f); p.set (A_Blend, 0.8f); p.set (A_Width, 1.0f);
                  p.set (F_Type, 1); p.set (F_Cutoff, 0.70f); env (p, 0, 0.003f, 0.3f, 0.6f, 0.2f); } },
            { "Synth - Synthwave de nuit", UpDown, R1_16, 2, OctUp, 0.60f, 0.00f, "9797979797979797", false, 0,
              { { fxs::Chorus, 0, 0.45f }, { fxs::Delay, 0, 0.30f }, { fxs::Reverb, 3, 0.30f } },
              [] (Patch& p, juce::String& a, juce::String&) {
                  a = "builtin:1"; p.set (A_Pos, 0.5f); p.set (A_Uni, 3); p.set (A_Detune, 0.12f); p.set (A_Width, 0.8f);
                  p.set (F_Type, 1); p.set (F_Cutoff, 0.5f); p.set (F_Res, 0.2f); env (p, 0, 0.004f, 0.6f, 0.4f, 0.4f);
                  lfo (p, 1, 1, 0.25f, false, false); mod (p, 0, S_Lfo2, D_APos, 0.3f);
                  env (p, 1, 0.002f, 0.4f, 0.2f, 0.3f); mod (p, 1, S_Env2, D_Cutoff, 0.3f); } },
            { "Synth - Stabs d'accords", Chord, R1_8, 1, OctUp, 0.35f, 0.10f, "9090900990909009", true, 3,
              { { fxs::Chorus, 3, 0.50f }, { fxs::Reverb, 2, 0.20f } },
              [] (Patch& p, juce::String& a, juce::String&) {
                  a = "builtin:0"; p.set (A_Pos, 0.66f); p.set (A_Uni, 4); p.set (A_Detune, 0.15f); p.set (A_Width, 0.9f);
                  p.set (F_Type, 1); p.set (F_Cutoff, 0.55f); env (p, 0, 0.002f, 0.25f, 0.0f, 0.2f);
                  env (p, 1, 0.001f, 0.15f, 0.0f, 0.1f); mod (p, 0, S_Env2, D_Cutoff, 0.3f); }, 0.7f },
            { "Synth - Pluck de verre", PinkyUp, R1_16, 1, OctUp, 0.55f, 0.00f, "9696969696969696", false, 0,
              { { fxs::Compressor, 0, 0.50f }, { fxs::Reverb, 2, 0.25f } },
              [] (Patch& p, juce::String& a, juce::String&) {
                  a = "builtin:3"; p.set (A_Pos, 0.4f); p.set (A_Uni, 4); p.set (A_Detune, 0.18f); p.set (A_Width, 1.0f);
                  p.set (F_Type, 1); p.set (F_Cutoff, 0.55f); env (p, 0, 0.001f, 0.3f, 0.0f, 0.25f);
                  env (p, 1, 0.001f, 0.15f, 0.0f, 0.1f); mod (p, 0, S_Env2, D_APos, 0.6f); } },
            { "Synth - Basse séquencée", Up, R1_16, 1, OctUp, 0.60f, 0.00f, "9909909909909099", false, 0,
              { { fxs::Saturation, 0, 0.40f } },
              [] (Patch& p, juce::String& a, juce::String&) {
                  a = "builtin:0"; p.set (A_Pos, 0.66f); p.set (A_Oct, -1); p.set (Sub_On, 1); p.set (Sub_Level, 0.5f);
                  p.set (F_Type, 1); p.set (F_Cutoff, 0.22f); p.set (F_Res, 0.45f); p.set (F_Drive, 0.35f); p.set (G_Mono, 1);
                  env (p, 0, 0.001f, 0.3f, 0.5f, 0.15f); env (p, 1, 0.001f, 0.25f, 0.0f, 0.15f); mod (p, 0, S_Env2, D_Cutoff, 0.5f); } },
            { "Synth - Kalimba numérique", Thumb, R1_16T, 2, OctUp, 0.70f, 0.00f, "9575957595759575", false, 0,
              { { fxs::Reverb, 1, 0.25f }, { fxs::Delay, 0, 0.15f } },
              [] (Patch& p, juce::String& a, juce::String& b) {
                  a = "builtin:5"; b = "builtin:2"; p.set (B_On, 1); p.set (B_Pos, 0.3f); p.set (B_Oct, 2); p.set (B_Level, 0.25f);
                  p.set (A_Warp, 0.2f); p.set (F_On, 0); env (p, 0, 0.001f, 0.45f, 0.0f, 0.3f);
                  env (p, 1, 0.001f, 0.06f, 0.0f, 0.05f); mod (p, 0, S_Env2, D_BLevel, 0.6f); } },
            { "Synth - Pad pulsé", UpDown, R1_8, 2, OctBoth, 1.00f, 0.00f, "9999999999999999", false, 0,
              { { fxs::Chorus, 0, 0.30f }, { fxs::Reverb, 3, 0.35f } },
              [] (Patch& p, juce::String& a, juce::String&) {
                  a = "builtin:4"; p.set (A_Pos, 0.3f); p.set (A_Uni, 4); p.set (A_Detune, 0.3f); p.set (A_Width, 1.0f);
                  p.set (Noise_On, 1); p.set (Noise_Level, 0.1f); p.set (F_Type, 0); p.set (F_Cutoff, 0.5f);
                  env (p, 0, 0.03f, 0.8f, 0.6f, 1.2f); lfo (p, 0, 0, syncRate (7), true, true); mod (p, 0, S_Lfo1, D_Volume, -0.3f);
                  lfo (p, 1, 0, 0.1f, false, false); mod (p, 1, S_Lfo2, D_APos, 0.3f); } },
            { "Synth - Table qui évolue", Random, R1_16, 2, OctBoth, 0.60f, 0.00f, "9797979797979797", false, 0,
              { { fxs::Delay, 1, 0.25f }, { fxs::Reverb, 2, 0.20f } },
              [] (Patch& p, juce::String& a, juce::String&) {
                  a = "serum:Digital/Evolution.wav"; p.set (A_Uni, 3); p.set (A_Detune, 0.15f); p.set (F_Type, 1); p.set (F_Cutoff, 0.65f);
                  env (p, 0, 0.002f, 0.4f, 0.5f, 0.3f); lfo (p, 0, 1, syncRate (3), true, false); mod (p, 0, S_Lfo1, D_APos, 0.7f); } },
            { "Synth - Chiptune 8 bits", Up, R1_32, 2, OctUp, 0.50f, 0.00f, "9999999999999999", false, 0,
              { { fxs::Lofi, 0, 0.50f } },
              [] (Patch& p, juce::String& a, juce::String&) {
                  a = "builtin:1"; p.set (A_Pos, 0.9f); p.set (F_On, 0); env (p, 0, 0.001f, 0.1f, 0.7f, 0.05f); }, 0.6f },
            { "Synth - Rêve lo-fi", UpDown, R1_8, 2, OctUp, 0.90f, 0.30f, "9777977797779777", false, 0,
              { { fxs::Lofi, 0, 0.50f }, { fxs::Reverb, 1, 0.30f } },
              [] (Patch& p, juce::String& a, juce::String&) {
                  a = "builtin:2"; p.set (A_Pos, 0.2f); p.set (Noise_On, 1); p.set (Noise_Type, 2); p.set (Noise_Level, 0.2f);
                  p.set (F_Type, 1); p.set (F_Cutoff, 0.45f); env (p, 0, 0.004f, 1.1f, 0.2f, 0.5f);
                  lfo (p, 0, 0, 0.3f, false, false); mod (p, 0, S_Lfo1, D_Pitch, 0.003f); } },
            { "Synth - Ping-pong stéréo", Diverge, R1_16, 2, OctBoth, 0.50f, 0.00f, "9797979797979797", false, 0,
              { { fxs::Delay, 1, 0.35f }, { fxs::Reverb, 2, 0.20f } },
              [] (Patch& p, juce::String& a, juce::String&) {
                  a = "builtin:0"; p.set (A_Pos, 0.45f); p.set (A_Uni, 2); p.set (A_Detune, 0.1f); p.set (A_Width, 1.0f);
                  p.set (F_Type, 1); p.set (F_Cutoff, 0.48f); p.set (F_Res, 0.3f); env (p, 0, 0.001f, 0.35f, 0.1f, 0.2f);
                  env (p, 1, 0.001f, 0.2f, 0.0f, 0.15f); mod (p, 0, S_Env2, D_Cutoff, 0.4f); lfo (p, 0, 5, syncRate (7), true, false); mod (p, 1, S_Lfo1, D_APan, 0.6f); } },
            { "Synth - Rave hoover", Up, R1_8, 1, OctUp, 0.80f, 0.00f, "9999999999999999", false, 0,
              { { fxs::Saturation, 1, 0.40f }, { fxs::Reverb, 2, 0.20f } },
              [] (Patch& p, juce::String& a, juce::String&) {
                  a = "builtin:1"; p.set (A_Pos, 0.7f); p.set (A_Uni, 6); p.set (A_Detune, 0.6f); p.set (Sub_On, 1);
                  p.set (F_Type, 1); p.set (F_Cutoff, 0.7f); env (p, 1, 0.001f, 0.25f, 0.0f, 0.2f); mod (p, 0, S_Env2, D_Pitch, -0.05f); }, 0.6f },
            { "Synth - Chœur céleste", UpDown, R1_4, 2, OctBoth, 1.40f, 0.00f, "9999999999999999", false, 0,
              { { fxs::Reverb, 5, 0.40f } },
              [] (Patch& p, juce::String& a, juce::String&) {
                  a = "serum:Vowel/DudaChoir.wav"; p.set (A_Uni, 4); p.set (A_Detune, 0.22f); p.set (A_Width, 1.0f);
                  env (p, 0, 0.3f, 1.0f, 0.9f, 1.5f); lfo (p, 0, 0, 0.12f, false, false); mod (p, 0, S_Lfo1, D_APos, 0.5f); } },
        };
        return s;
    }

    inline juce::File userFolder()
    {
        auto f = library::defaultFolder().getChildFile (library::presetFolderName());
        const auto oldPresets = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Variant Presets");
        if (f.getParentDirectory().isDirectory() && oldPresets.isDirectory()) f.createDirectory();   // seulement pour ranger d anciens presets
        // anciens presets (v1.9, DocumentsVariant Presets) : on les range avec la banque
        const auto old = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Variant Presets");
        if (old.isDirectory() && f.isDirectory())
        {
            for (auto& p : old.findChildFiles (juce::File::findFiles, false, "*.vpreset"))
                if (! f.getChildFile (p.getFileName()).exists()) p.moveFileTo (f.getChildFile (p.getFileName()));
            if (old.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 0) old.deleteRecursively();
        }
        return f;
    }

    inline juce::Array<juce::File> userPresets()
    {
        auto a = userFolder().findChildFiles (juce::File::findFiles, false, "*.vpreset");
        a.sort();
        return a;
    }
}
