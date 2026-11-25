#pragma once

#include "Theory.h"
#include "ArpCore.h"

// Ambiances : progressions, couleur des accords, rythme, basse et arpège typiques.
//
// Chaque ambiance a une grande banque de progressions écrites en chiffrage (« i9 bVImaj7 iv9 V7#9 »),
// avec les accords hors gamme qui font le style (emprunts, dominantes, demi-tons…), + des boucles de degrés
// pour le générateur « libre ». Le générateur pioche, varie, et évite ce qu'il a déjà proposé récemment.
//
// Chiffrage : [b|#] + chiffre romain (MAJUSCULE = majeur, minuscule = mineur) + couleur
//   couleurs : 7 maj7 9 maj9 6 69 11 13 sus2 sus4 7sus4 add9 ø (m7b5) ° (dim) °7 aug 7b9 7#9 7#11 maj7#11 (maj7)
//   « + » = deux accords dans la même mesure (ex. « ii7+V7 »).
namespace moods
{
    struct Hit     { float start, len, vel; };
    struct BassHit { float start, len, vel; int tone; int octave; }; // tone : 0 fondamentale, 1 tierce, 2 quinte

    struct ArpPreset
    {
        int mode, rate, octaves;
        float gate, swing;
        int numSteps;
        std::array<float, 16> steps;
    };

    // "Couleur" harmonique de chaque ambiance : à quel point on sort de la gamme.
    struct Colour
    {
        float spice;      // extensions / altérations (9, 13, b9, #9, #11…)
        float secDom;     // dominantes secondaires (V7 de l'accord suivant)
        float tritone;    // substitution tritonique (bII7)
        float chromatic;  // accord d'approche chromatique (demi-ton au-dessus / en dessous)
        float borrow;     // accords empruntés (iv mineur, bVI, bVII…)
        float split;      // 2 accords dans une mesure
        float subst;      // substitutions diatoniques (I↔vi, IV↔ii…)
    };

    // Grooves des accords : plusieurs motifs de 2 mesures (8 temps) par ambiance.
    // push = anticipation : la frappe joue déjà l'accord suivant (effet "groove").
    struct GHit { float start, len, vel; bool push = false; };

    struct Mood
    {
        const char* name;
        std::vector<int> scales;                 // gammes conseillées (la 1re est appliquée par défaut)
        std::vector<std::vector<int>> progs;     // degrés (0 = I) : boucles pour le générateur libre et les suggestions
        int ext;                                 // nb de notes par accord (3 triade, 4 = 7e, 5 = 9e…)
        int voicing;
        float strumMs;
        float humanize;                          // 0..1
        std::vector<Hit> rhythm;                 // dans une mesure de 4 temps
        std::vector<BassHit> bass;
        ArpPreset arp;
        Colour colour;
        std::vector<std::vector<GHit>> grooves;
        std::vector<const char*> templates;      // progressions écrites (chiffrage, une mesure par mot)
        float templateShare;                     // part des progressions tirées de la banque écrite (le reste : générateur libre)
    };

    using namespace theory;
    using namespace arp;

    inline const std::array<float, 16> allOn { 1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1 };

    inline const std::vector<Mood>& list()
    {
        static const std::vector<Mood> m {
            //==================================================================
            { "Disco", { Minor, Dorian },
              { { 0, 3, 4, 3 }, { 0, 3, 0, 3 }, { 1, 4, 0, 0 }, { 0, 6, 5, 4 }, { 0, 2, 5, 4 }, { 3, 6, 2, 5 }, { 0, 4, 3, 6 } },
              4, Close, 0.0f, 0.15f,
              { { 0.5f, 0.3f, 0.9f }, { 1.5f, 0.3f, 0.8f }, { 2.5f, 0.3f, 0.9f }, { 3.5f, 0.3f, 0.8f } },
              { { 0.0f, 0.45f, 0.9f, 0, 0 }, { 0.5f, 0.45f, 0.8f, 0, 1 }, { 1.0f, 0.45f, 0.9f, 0, 0 }, { 1.5f, 0.45f, 0.8f, 0, 1 },
                { 2.0f, 0.45f, 0.9f, 0, 0 }, { 2.5f, 0.45f, 0.8f, 0, 1 }, { 3.0f, 0.45f, 0.9f, 0, 0 }, { 3.5f, 0.45f, 0.8f, 0, 1 } },
              { UpDown, R1_16, 2, 0.45f, 0.1f, 16, { 1, .6f, .8f, .6f, 1, .6f, .8f, .6f, 1, .6f, .8f, .6f, 1, .6f, .8f, .6f } },
              { 0.60f, 0.45f, 0.10f, 0.25f, 0.20f, 0.50f, 0.25f },
              { { { .5f, .3f, .9f }, { 1.5f, .3f, .75f }, { 2.5f, .3f, .9f }, { 3.5f, .3f, .75f }, { 4.5f, .3f, .9f }, { 5.5f, .3f, .75f }, { 6.5f, .3f, .9f }, { 7.5f, .4f, .85f, true } },
                { { 0, .5f, .85f }, { .75f, .25f, .6f }, { 1.5f, .5f, .8f }, { 2.5f, .25f, .7f }, { 3, .5f, .85f }, { 4.5f, .25f, .8f }, { 5.5f, .25f, .7f }, { 6, .5f, .85f }, { 7.25f, .5f, .8f, true } },
                { { .5f, .75f, .85f }, { 2.5f, .75f, .85f }, { 4.5f, .75f, .85f }, { 6.5f, .5f, .85f }, { 7.5f, .25f, .7f, true } },
                { { 0, .25f, .9f }, { .5f, .25f, .7f }, { 1, .25f, .85f }, { 1.75f, .5f, .75f }, { 2.5f, .25f, .8f }, { 3.5f, .5f, .8f, true }, { 4, .25f, .9f }, { 5, .25f, .8f }, { 5.75f, .5f, .75f }, { 7, .75f, .8f } } },
              { "i7 IV9 i7 IV9", "i7 bVII7 bVImaj7 V7", "i9 iv9 bVII9 bIIImaj7", "i7 bIIImaj7 bVImaj7 V7sus4+V7",
                "iv7 bVII7 bIIImaj7 bVImaj7", "i9 bVI7 bVII9 i9", "i7 iv7 v7 bVImaj7", "i9 IV13 bVIImaj7 V7#9",
                "ii7 V7 Imaj7 vi7", "Imaj7 iii7 vi7 ii7+V7", "IVmaj7 iii7 ii7 Imaj7", "Imaj9 bVII9 IVmaj7 iv7",
                "vi9 ii9 V13 Imaj9", "i7 bIII7 bVI7 bII7", "i9 bVImaj7 iv9 bVII13", "iv9 v7 bVImaj7 bVII9",
                "i7+bVII7 bVI7+bVII7 i7+bVII7 bVImaj7+V7", "Imaj7 II7 iv7 Imaj7" },
              0.75f },

            //==================================================================
            { "Bossa nova", { Major, Dorian, Lydian },
              { { 1, 4, 0, 5 }, { 0, 5, 1, 4 }, { 1, 4, 2, 5 }, { 0, 3, 1, 4 }, { 2, 5, 1, 4 }, { 0, 1, 2, 5 }, { 3, 6, 2, 5 } },
              5, Drop2, 12.0f, 0.3f,
              { { 0.0f, 0.75f, 0.8f }, { 1.5f, 0.5f, 0.7f }, { 2.5f, 0.75f, 0.75f }, { 3.5f, 0.4f, 0.65f } },
              { { 0.0f, 1.4f, 0.85f, 0, 0 }, { 1.5f, 0.45f, 0.7f, 2, 0 }, { 2.0f, 1.4f, 0.8f, 2, -1 }, { 3.5f, 0.45f, 0.7f, 0, 0 } },
              { Played, R1_8, 1, 0.8f, 0.25f, 8, allOn },
              { 0.75f, 0.50f, 0.35f, 0.20f, 0.20f, 0.60f, 0.30f },
              { { { 0, .75f, .8f }, { 1.5f, .5f, .65f }, { 2.5f, .75f, .75f }, { 4, .75f, .7f }, { 5.5f, .5f, .65f }, { 6.5f, .9f, .75f }, { 7.5f, .4f, .6f, true } },
                { { 0, 1, .8f }, { 1.5f, .5f, .6f }, { 3, .75f, .7f }, { 4.5f, .75f, .7f }, { 6, .5f, .6f }, { 7, .9f, .7f, true } },
                { { .5f, .75f, .7f }, { 2, .5f, .65f }, { 3.5f, .45f, .7f, true }, { 5, 1, .75f }, { 6.5f, .75f, .65f } },
                { { 0, .45f, .8f }, { 1, .45f, .6f }, { 1.5f, .9f, .7f }, { 3, .45f, .65f }, { 4, .45f, .75f }, { 4.75f, .45f, .6f }, { 5.5f, .9f, .7f }, { 7, .9f, .65f, true } } },
              { "Imaj9 ii9 iii7 bIII°7", "ii9 V13 Imaj9 vi9", "Imaj7 II7#11 ii9 bII7", "vi9 II13 ii9 V7b9",
                "Imaj9 bVII9 vi7 II7", "ii7 bII7 Imaj7 VI7b9", "IVmaj7 iv6 iii7 VI7", "Imaj7 #iv°7 ii9 V7#9",
                "i6 ii° V7b9 i6", "i9 iv9 bVII13 bIIImaj7", "iii7 VI7 ii7 V7", "Imaj9 iv9 bVII13 Imaj9",
                "ii9+V13 Imaj9+VI7 ii9+V13 Imaj9", "Imaj7 vi9 ii11 bII7#11", "IVmaj9 bVII13 Imaj9 VI7#9",
                "i9 bII7 i9 V7b9", "Imaj7 II7 ii7 V7sus4+V7", "vi9 bVImaj7 V7 Imaj9" },
              0.8f },

            //==================================================================
            { "Jazz", { Major, Dorian, MelodicMinor },
              { { 1, 4, 0, 0 }, { 1, 4, 0, 5 }, { 2, 5, 1, 4 }, { 0, 5, 1, 4 }, { 3, 6, 2, 5 }, { 0, 3, 2, 5 }, { 2, 3, 1, 4 } },
              5, Drop2, 8.0f, 0.35f,
              { { 0.0f, 0.75f, 0.85f }, { 1.5f, 1.5f, 0.75f } },
              { { 0.0f, 0.95f, 0.85f, 0, 0 }, { 1.0f, 0.95f, 0.75f, 1, 0 }, { 2.0f, 0.95f, 0.8f, 2, 0 }, { 3.0f, 0.95f, 0.75f, 1, 0 } },
              { UpDown, R1_8T, 2, 0.75f, 0.0f, 12, allOn },
              { 0.90f, 0.60f, 0.50f, 0.30f, 0.25f, 0.70f, 0.35f },
              { { { 0, .75f, .85f }, { 1.5f, 1.5f, .7f }, { 4, .75f, .8f }, { 5.5f, 2, .7f } },
                { { 0, .3f, .85f }, { 1.5f, .3f, .6f }, { 3.5f, 1, .75f, true }, { 5, .3f, .7f }, { 7.5f, .45f, .7f, true } },
                { { 0, 1.75f, .75f }, { 2.5f, .3f, .6f }, { 3.5f, .5f, .7f, true }, { 6, .3f, .7f }, { 6.5f, 1.3f, .65f } },
                { { .5f, .4f, .75f }, { 1.5f, .4f, .65f }, { 2.5f, 1.4f, .7f }, { 4.5f, .4f, .75f }, { 6, .4f, .65f }, { 7, .9f, .7f, true } } },
              { "ii9 V13 Imaj9 VI7b9", "iii7 VI7 ii7 V7", "Imaj7 vi7 ii7 V7", "ii7 bII7 Imaj7 Imaj7",
                "Imaj7 #i°7 ii7 #ii°7", "ii7+V7 iii7+VI7 ii7+V7 Imaj7", "IVmaj7 bVII7 iii7 VI7#9", "i(maj7) i7 viø bII7",
                "iiø V7b9 i9 i6", "Imaj9 bIIImaj7 bVImaj7 bIImaj7", "iii7 bIII7 ii7 bII7", "Imaj7 IV7 iii7 VI7b9",
                "vi9 II7 ii9 V7", "IVmaj9 iv9 iii7 bIII°7", "Imaj7 bVII9 bVImaj7 V7#9", "i9 IV13 i9 bVI7",
                "ii7 V7 iii7 VI7", "Imaj9 II7#11 ii9 V7b9", "bVImaj7 V7#9 i9 i9" },
              0.8f },

            //==================================================================
            { "Lo-fi", { Major, Dorian },
              { { 3, 2, 1, 0 }, { 1, 4, 0, 5 }, { 0, 2, 3, 3 }, { 3, 4, 2, 5 }, { 0, 5, 3, 4 }, { 5, 3, 1, 4 }, { 3, 0, 5, 4 } },
              5, Open, 18.0f, 0.6f,
              { { 0.0f, 1.8f, 0.7f }, { 2.0f, 1.9f, 0.6f } },
              { { 0.0f, 1.8f, 0.8f, 0, 0 }, { 2.5f, 1.4f, 0.7f, 0, 0 } },
              { Up, R1_8, 1, 0.9f, 0.3f, 8, allOn },
              { 0.55f, 0.30f, 0.15f, 0.15f, 0.35f, 0.30f, 0.30f },
              { { { .06f, 1.8f, .7f }, { 2.56f, 1.4f, .6f }, { 4.06f, 3.8f, .65f } },
                { { .06f, 2.9f, .7f }, { 3.06f, .9f, .55f }, { 4.56f, 1.5f, .6f }, { 6.56f, 1.4f, .55f } },
                { { .08f, .9f, .65f }, { 1.58f, 2.3f, .6f }, { 4.08f, 1.4f, .65f }, { 5.58f, 2.3f, .6f } },
                { { .07f, 3.8f, .65f }, { 4.07f, 1.4f, .6f }, { 5.82f, 2.1f, .55f } } },
              { "IVmaj9 iii7 ii9 Imaj9", "ii9 V13 Imaj9 vi9", "Imaj7 iii7 IVmaj7 iv7", "vi9 IVmaj9 Imaj9 V7sus4",
                "IVmaj7 iv6 Imaj7 VI7", "Imaj9 vi9 IVmaj9 bVII9", "i9 iv9 bVIImaj7 bIIImaj7", "ii9 iii7 IVmaj9 V6",
                "Imaj7 II7 IVmaj7 iv6", "iii7 vi9 ii9 V13", "IVmaj9 V7sus4 iii7 vi9", "i9 bVImaj7 bIIImaj7 bVII6",
                "Imaj9 bIIImaj7 IVmaj9 iv9", "vi9 V6 IVmaj9 iii7", "ii9 bII7 Imaj9 Imaj9", "IVmaj7#11 Imaj7 IVmaj7#11 iv(maj7)",
                "i9 v9 bVImaj7 bVII13", "Imaj9 III7 vi9 bVII9" },
              0.75f },

            //==================================================================
            { "Trap", { Minor, HarmonicMinor, Phrygian },
              { { 0, 5, 6, 0 }, { 0, 3, 5, 4 }, { 0, 6, 5, 6 }, { 0, 1, 5, 4 }, { 0, 5, 2, 6 }, { 5, 3, 0, 4 }, { 0, 3, 6, 5 } },
              3, Open, 0.0f, 0.1f,
              { { 0.0f, 4.0f, 0.8f } },
              { { 0.0f, 1.4f, 0.95f, 0, -1 }, { 1.5f, 0.45f, 0.8f, 0, -1 }, { 2.0f, 2.0f, 0.9f, 0, -1 } },
              { Up, R1_16T, 2, 0.5f, 0.0f, 12, { 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 1, 0, 1,1,1,1 } },
              { 0.15f, 0.05f, 0.00f, 0.00f, 0.25f, 0.10f, 0.20f },
              { { { 0, 7.8f, .75f } },
                { { 0, 1.33f, .75f }, { 1.33f, 1.33f, .6f }, { 2.66f, 1.34f, .65f }, { 4, 3.9f, .7f } },
                { { 0, 3, .75f }, { 3, .33f, .55f }, { 3.33f, .33f, .5f }, { 3.66f, .34f, .55f }, { 4, 3.8f, .7f } },
                { { 0, 1.5f, .75f }, { 1.5f, 2.5f, .65f }, { 4, 1.5f, .7f }, { 5.5f, 2.4f, .65f } } },
              { "i bVI bVII i", "i bII i bII", "i iv bVI V", "i bVI iv V7", "i v bVI bVII", "i bIII bVI V",
                "i bII i bVI", "i iv i V", "i bVI bIII bVII", "iv bVI V i", "i bII bVII i", "i7 bVImaj7 iv7 V7b9",
                "i bVI iv bII", "i iv bVI V", "bVI bVII i i", "i v iv bVI", "i bIII iv bII", "i i(maj7) i7 bVI" },
              0.7f },

            //==================================================================
            { "Pop", { Major },
              { { 0, 4, 5, 3 }, { 5, 3, 0, 4 }, { 0, 5, 3, 4 }, { 3, 0, 4, 5 }, { 0, 3, 5, 4 }, { 3, 4, 2, 5 }, { 0, 2, 3, 4 } },
              3, Close, 0.0f, 0.15f,
              { { 0.0f, 0.95f, 0.85f }, { 1.0f, 0.95f, 0.7f }, { 2.0f, 0.95f, 0.8f }, { 3.0f, 0.95f, 0.7f } },
              { { 0.0f, 0.95f, 0.9f, 0, 0 }, { 1.0f, 0.95f, 0.75f, 0, 0 }, { 2.0f, 0.95f, 0.85f, 0, 0 }, { 3.0f, 0.95f, 0.75f, 2, 0 } },
              { UpDown, R1_8, 2, 0.7f, 0.0f, 16, allOn },
              { 0.20f, 0.15f, 0.00f, 0.05f, 0.25f, 0.20f, 0.25f },
              { { { 0, 1, .85f }, { 1, 1, .7f }, { 2, 1, .8f }, { 3, 1, .7f }, { 4, 1, .85f }, { 5, 1, .7f }, { 6, 1, .8f }, { 7, 1, .7f } },
                { { 0, 1.5f, .85f }, { 1.5f, 1.5f, .7f }, { 3, 1, .8f }, { 4, 1.5f, .85f }, { 5.5f, 1.5f, .7f }, { 7, 1, .8f } },
                { { 0, .5f, .85f }, { 1, .5f, .7f }, { 1.5f, .5f, .6f }, { 2, .5f, .8f }, { 3, .5f, .7f }, { 3.5f, .5f, .65f }, { 4, 2, .85f }, { 6, 2, .75f } },
                { { 0, 3.5f, .8f }, { 3.5f, .5f, .7f, true }, { 4, 1.5f, .8f }, { 5.5f, 2.4f, .7f } } },
              { "I V vi IV", "vi IV I V", "I IV vi V", "IV I V vi", "I iii IV iv", "vi V IV V", "I Vsus4 vi IV",
                "IV V iii vi", "I bVII IV I", "ii IV I V", "I vi ii V", "IVadd9 Iadd9 V vi", "I III7 vi IV",
                "vi IV ii V", "I Vsus4+V vi IV", "IV iv I I", "I bVI bVII I", "vi iii IV I", "I ii iii IV" },
              0.8f },

            //==================================================================
            { "Épique / Cinéma", { Minor, HarmonicMinor },
              { { 0, 5, 2, 6 }, { 0, 3, 5, 6 }, { 5, 3, 0, 4 }, { 0, 6, 5, 4 }, { 0, 5, 3, 4 }, { 5, 6, 0, 0 }, { 0, 2, 6, 3 } },
              3, BassPlus, 0.0f, 0.1f,
              { { 0.0f, 4.0f, 0.9f } },
              { { 0.0f, 4.0f, 0.9f, 0, -1 } },
              { Up, R1_16, 3, 0.5f, 0.0f, 16, allOn },
              { 0.20f, 0.10f, 0.00f, 0.05f, 0.45f, 0.15f, 0.25f },
              { { { 0, 4, .85f }, { 4, 4, .9f } },
                { { 0, 2, .8f }, { 2, 2, .85f }, { 4, 1, .8f }, { 5, 1, .85f }, { 6, 2, .95f } },
                { { 0, 8, .9f } },
                { { 0, 3, .85f }, { 3, 1, .8f }, { 4, 3, .9f }, { 7, 1, .85f } } },
              { "i bVI bIII bVII", "i bVI iv V", "bVI bVII i i", "i bIII bVII iv", "i iv bVI bVII", "i bVII bVI V",
                "i III bVI V", "bVI iv i V", "i bII bVI V", "i v bVI bIII", "iv i bVII bIII", "i bVIImaj7 bVI bVII",
                "i bVI bVII bIII", "i IV bVI V", "bVI bIII bVII i", "i bIII iv V", "i i° bVI V" },
              0.75f },

            //==================================================================
            { "House", { Minor, Dorian },
              { { 0, 5, 3, 4 }, { 0, 3, 5, 4 }, { 0, 6, 5, 6 }, { 0, 0, 3, 3 }, { 0, 4, 3, 6 }, { 5, 6, 0, 3 }, { 0, 2, 3, 4 } },
              4, Close, 0.0f, 0.1f,
              { { 0.5f, 0.4f, 0.9f }, { 1.5f, 0.4f, 0.8f }, { 2.5f, 0.4f, 0.9f }, { 3.5f, 0.4f, 0.8f } },
              { { 0.5f, 0.4f, 0.9f, 0, 0 }, { 1.5f, 0.4f, 0.85f, 0, 0 }, { 2.5f, 0.4f, 0.9f, 0, 0 }, { 3.5f, 0.4f, 0.85f, 0, 0 } },
              { Up, R1_16, 1, 0.4f, 0.05f, 16, { 1, .5f, .8f, .5f, 1, .5f, .8f, .5f, 1, .5f, .8f, .5f, 1, .5f, .8f, .5f } },
              { 0.45f, 0.20f, 0.05f, 0.10f, 0.25f, 0.25f, 0.25f },
              { { { 0, .4f, .9f }, { .75f, .4f, .8f }, { 1.5f, .4f, .85f }, { 2.5f, .4f, .8f }, { 3.25f, .4f, .75f }, { 4, .4f, .9f }, { 4.75f, .4f, .8f }, { 5.5f, .4f, .85f }, { 6.5f, .4f, .8f }, { 7.25f, .4f, .75f } },
                { { .5f, .4f, .9f }, { 1.5f, .4f, .8f }, { 2.5f, .4f, .9f }, { 3.5f, .4f, .75f }, { 4.5f, .4f, .9f }, { 5.5f, .4f, .8f }, { 6.25f, .4f, .85f }, { 7, .4f, .8f }, { 7.75f, .25f, .6f, true } },
                { { 0, 1.5f, .85f }, { 1.75f, .5f, .7f }, { 3, .75f, .8f }, { 4, 1.5f, .85f }, { 5.75f, .5f, .7f }, { 7, .75f, .8f } },
                { { .75f, .25f, .85f }, { 1.75f, .25f, .8f }, { 2.25f, .5f, .75f }, { 3.5f, .25f, .8f }, { 4.75f, .25f, .85f }, { 5.75f, .25f, .8f }, { 6.5f, .75f, .75f } } },
              { "i9 iv9 i9 iv9", "i7 bVII7 bVImaj7 bVII7", "i9 bIIImaj7 bVII9 iv9", "iv9 v7 i9 i9", "i7 bVImaj7 bIIImaj7 bVII9",
                "ii9 V13 Imaj9 Imaj9", "vi9 IVmaj9 Imaj9 V9", "i11 bVII11 bVI11 v11", "i9 bII9 i9 bVII9", "i7 iv7 bVII9 bIIImaj7",
                "Imaj9 iii7 IVmaj9 iv9", "i9 bVImaj9 bVII13 i9", "i7+bVII7 bVImaj7 i7+bVII7 v7", "iv9 bIIImaj7 bVImaj7 V7sus4",
                "i11 i11 bVImaj7 bVII13", "vi9 iii7 IVmaj9 IVmaj9" },
              0.75f },

            //==================================================================
            { "Ambient", { Lydian, Major },
              { { 0, 4, 3, 0 }, { 0, 1, 0, 3 }, { 3, 0, 4, 5 }, { 5, 3, 0, 0 }, { 0, 2, 3, 1 }, { 1, 0, 3, 5 }, { 0, 5, 1, 3 } },
              5, Open, 40.0f, 0.4f,
              { { 0.0f, 4.0f, 0.6f } },
              { { 0.0f, 4.0f, 0.7f, 0, 0 } },
              { Random, R1_8, 3, 1.4f, 0.0f, 16, allOn },
              { 0.40f, 0.05f, 0.00f, 0.05f, 0.35f, 0.10f, 0.30f },
              { { { 0, 8, .6f } },
                { { 0, 6, .6f }, { 6, 2, .5f } },
                { { 0, 3, .55f }, { 3, 5, .6f } },
                { { 0, 4, .55f }, { 4, 4, .6f } } },
              { "Imaj7#11 IImaj7 Imaj7#11 IImaj7", "Imaj9 iii7 vi9 IVmaj9", "Isus2 IVmaj7#11 vi9 Isus2", "IVmaj9 Imaj9 bVIImaj7 IVmaj9",
                "vi9 IVmaj7#11 Imaj9 V6", "Imaj7 bVImaj7 Imaj7 bVImaj7", "Isus2 bIIImaj7 IVmaj7#11 iv(maj7)", "Imaj9 vii7 vi9 IVmaj7#11",
                "iii7 IVmaj9 vi9 Imaj9", "Imaj7#11 V6 vi9 IVmaj9", "IImaj7 Imaj7 IImaj7 vii7", "i9 bVImaj7#11 bIIImaj7 bVIImaj9",
                "IVmaj7#11 IVmaj7#11 Imaj9 Imaj9", "Imaj9 IIImaj7 IVmaj9 iv(maj7)" },
              0.7f },

            //==================================================================
            { "Funk", { Dorian, Mixolydian },
              { { 0, 3, 0, 3 }, { 0, 0, 3, 4 }, { 0, 6, 0, 3 }, { 0, 3, 6, 3 }, { 0, 4, 3, 0 }, { 1, 4, 0, 3 } },
              5, Close, 0.0f, 0.2f,
              { { 0.0f, 0.2f, 0.9f }, { 0.75f, 0.2f, 0.7f }, { 1.5f, 0.2f, 0.85f }, { 2.0f, 0.2f, 0.7f }, { 2.75f, 0.2f, 0.8f }, { 3.5f, 0.2f, 0.75f } },
              { { 0.0f, 0.45f, 0.95f, 0, 0 }, { 0.75f, 0.2f, 0.7f, 0, 0 }, { 1.5f, 0.45f, 0.85f, 0, 1 }, { 2.5f, 0.2f, 0.7f, 2, 0 }, { 3.0f, 0.45f, 0.85f, 0, 0 } },
              { UpDown, R1_16, 1, 0.35f, 0.2f, 16, { 1, .5f, .8f, 0, 1, .5f, 0, .8f, 1, .5f, .8f, 0, 1, 0, .8f, .5f } },
              { 0.75f, 0.45f, 0.25f, 0.30f, 0.15f, 0.45f, 0.20f },
              { { { 0, .2f, .9f }, { .75f, .2f, .65f }, { 1.5f, .2f, .8f }, { 2.25f, .2f, .6f }, { 2.75f, .2f, .75f }, { 3.5f, .2f, .7f }, { 4, .2f, .9f }, { 4.5f, .2f, .6f }, { 5.25f, .2f, .75f }, { 6, .2f, .8f }, { 6.75f, .2f, .65f }, { 7.5f, .2f, .8f, true } },
                { { 0, .3f, .9f }, { 1, .2f, .6f }, { 1.75f, .2f, .8f }, { 2.5f, .3f, .75f }, { 3.25f, .2f, .6f }, { 4, .3f, .9f }, { 4.75f, .2f, .7f }, { 5.5f, .2f, .8f }, { 6.5f, .3f, .75f }, { 7.25f, .2f, .6f } },
                { { .5f, .2f, .85f }, { 1, .2f, .6f }, { 1.5f, .2f, .85f }, { 2.75f, .2f, .75f }, { 3.5f, .2f, .7f }, { 4.5f, .2f, .85f }, { 5, .2f, .6f }, { 5.5f, .2f, .85f }, { 6.75f, .2f, .75f }, { 7.5f, .2f, .7f } } },
              { "i9 IV9 i9 IV9", "I7 IV9 I7 bVII9", "i7 IV7 bVII9 bIII7", "I9 bVII9 IV9 I9", "i11 bIII7 IV9 i11",
                "I7#9 IV9 I7#9 V7#9", "ii9 V9 ii9 V9", "i7 bVI7 bVII7 i7", "I13 bIII7 IV9 bVI7", "i9 i9 IV13 bVII13",
                "I9 I9 IV9 #iv°7", "i7+IV9 i7+IV9 bVII9+bIII7 IV9", "I7 bVII7 bVI7 V7#9", "i9 v7 bVI7 bVII9",
                "IV9 I9 bVII9 I9", "i7 IV9 bVI13 V7#9" },
              0.75f },

            //==================================================================
            // SWAGG : façon Neptunes / Pharrell, Clipse, et les « swag beats » 2000s (met2night, Hi-Tech…) :
            // 90-105 BPM, guitare / keys acoustiques, accords maj7 / m9 / #11 suspendus, glissements d'un demi-ton,
            // emprunts (bIII, bVII, bVI) qui créent de la tension sans jamais trop résoudre, peu de notes, beaucoup d'espace.
            { "Swagg", { Dorian, Major, Lydian, Minor },
              { { 0, 3, 0, 3 }, { 0, 6, 5, 6 }, { 3, 2, 1, 0 }, { 1, 0, 1, 0 }, { 0, 5, 3, 6 }, { 3, 4, 0, 0 }, { 5, 3, 6, 0 } },
              5, Drop2, 9.0f, 0.45f,
              { { 0.0f, 0.75f, 0.8f }, { 1.5f, 0.45f, 0.65f }, { 2.75f, 0.9f, 0.75f } },
              { { 0.0f, 1.2f, 0.9f, 0, 0 }, { 1.75f, 0.3f, 0.7f, 2, 0 }, { 2.5f, 0.9f, 0.85f, 0, 0 }, { 3.5f, 0.3f, 0.65f, 0, 0 } },
              { Played, R1_16, 1, 0.5f, 0.35f, 16, { 1, 0, .6f, .8f, 0, .7f, 0, .6f, 1, 0, .6f, 0, .8f, .6f, 0, .7f } },
              { 0.80f, 0.20f, 0.25f, 0.35f, 0.40f, 0.35f, 0.25f },
              { // guitare / keys « posés » : un peu en retard, frappes courtes et syncopées, beaucoup d'air
                { { .04f, .7f, .8f }, { 1.54f, .35f, .6f }, { 2.79f, .9f, .75f }, { 4.04f, .7f, .78f }, { 5.54f, .35f, .58f }, { 6.29f, .4f, .62f }, { 7.29f, .6f, .7f, true } },
                { { 0, .25f, .85f }, { .75f, .25f, .6f }, { 1.5f, .5f, .75f }, { 2.5f, .25f, .65f }, { 3.25f, .5f, .7f, true }, { 4, .25f, .85f }, { 4.75f, .25f, .6f }, { 5.5f, 1.2f, .75f }, { 7.5f, .45f, .65f } },
                { { .03f, 1.9f, .75f }, { 2.28f, .3f, .55f }, { 2.78f, 1.1f, .7f }, { 4.03f, 1.4f, .72f }, { 5.78f, .3f, .55f }, { 6.53f, 1.3f, .68f } },
                { { 0, .4f, .82f }, { .5f, .2f, .55f }, { 1.75f, .4f, .7f }, { 3, .4f, .72f }, { 3.75f, .2f, .55f, true }, { 4.5f, .4f, .78f }, { 5.75f, .4f, .65f }, { 6.5f, .2f, .55f }, { 7, .9f, .7f } } },
              { "Imaj7 bIIImaj7 IVmaj7 bVII9", "i9 IV9 i9 IV9", "Imaj9 bVIImaj7 Imaj9 bVIImaj7", "IVmaj7 iii7 ii7 Imaj7",
                "i9 bIImaj7 i9 bIImaj7", "Imaj7#11 bIIImaj7 bVImaj7 bIImaj7", "iv9 bVII13 bIIImaj7 bVImaj7", "i7 v7 bVImaj7 bVII7sus4",
                "Imaj9 vi9 ii9 bIImaj7", "IVmaj9 V7sus4 iii7 vi9", "bVImaj7 bVIImaj7 Imaj7 Imaj7", "i11 bIIImaj7 iv9 V7#9",
                "Imaj7 IVmaj7#11 iii7 bIIImaj7", "i9 bVImaj7#11 bIIImaj7 V7b9", "ii9 bII7#11 Imaj9 Imaj9", "Imaj7 bVImaj7 bVIImaj7 Imaj7",
                "i9 bVII6 bVImaj7 V7sus4+V7", "IVmaj7 bVII9 Imaj7 vi9", "i(maj7) i7 bVImaj7 bIImaj7", "Isus2 bIIImaj7 IVmaj7#11 iv9",
                "vi9 IVmaj7 bVImaj7 V7sus4", "i9 iv9 bVII13 bIIImaj7+bVImaj7", "Imaj9 iii7 bIIImaj7 ii9" },
              0.8f },

            //==================================================================
            // BAILE : funk carioca / mandelão / funk BH (DJ Vini da ZO, DJ Caio Prince…) : tamborzão 130 BPM ou 150,
            // harmonie courte (2-4 accords qui tournent), souvent mineure ou phrygienne (bruxaria), parfois un « funk melody » pop ;
            // les accords frappent sur les accents du tamborzão (3-3-2 en doubles-croches), basse 808 qui suit le même rythme.
            { "Baile", { Minor, Phrygian, HarmonicMinor, Major },
              { { 0, 5, 0, 5 }, { 0, 1, 0, 1 }, { 0, 3, 0, 3 }, { 0, 6, 5, 6 }, { 0, 5, 2, 6 }, { 0, 4, 5, 3 }, { 5, 3, 0, 4 } },
              3, Close, 0.0f, 0.15f,
              { { 0.0f, 0.2f, 0.9f }, { 0.75f, 0.2f, 0.75f }, { 1.5f, 0.2f, 0.85f }, { 2.5f, 0.2f, 0.8f }, { 3.25f, 0.2f, 0.75f } },
              { { 0.0f, 0.5f, 0.95f, 0, -1 }, { 0.75f, 0.5f, 0.85f, 0, -1 }, { 1.5f, 0.75f, 0.9f, 0, -1 }, { 2.5f, 0.5f, 0.85f, 0, -1 }, { 3.25f, 0.6f, 0.85f, 0, -1 } },
              { Played, R1_16, 1, 0.35f, 0.08f, 16, { 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0 } },
              { 0.20f, 0.10f, 0.05f, 0.10f, 0.30f, 0.25f, 0.20f },
              { // tamborzão (3-3-2-3-3 en doubles-croches) et ses variantes
                { { 0, .2f, .92f }, { .75f, .2f, .75f }, { 1.5f, .2f, .85f }, { 2.5f, .2f, .8f }, { 3.25f, .2f, .78f },
                  { 4, .2f, .92f }, { 4.75f, .2f, .75f }, { 5.5f, .2f, .85f }, { 6.5f, .2f, .8f }, { 7.25f, .2f, .78f } },
                { { 0, .2f, .9f }, { .75f, .2f, .7f }, { 1.5f, .2f, .85f }, { 2, .2f, .65f }, { 2.75f, .2f, .78f }, { 3.5f, .2f, .8f },
                  { 4, .2f, .9f }, { 4.75f, .2f, .7f }, { 5.5f, .2f, .85f }, { 6.5f, .2f, .78f }, { 7.25f, .5f, .8f, true } },
                { { 0, 1.4f, .85f }, { 1.5f, .2f, .75f }, { 2.5f, .2f, .8f }, { 3.25f, .2f, .78f }, { 4, 1.4f, .85f }, { 5.5f, .2f, .75f }, { 6.5f, .2f, .8f }, { 7.25f, .2f, .75f } },
                { { 0, .15f, .92f }, { .5f, .15f, .6f }, { .75f, .15f, .78f }, { 1.5f, .15f, .88f }, { 2.25f, .15f, .6f }, { 2.5f, .15f, .8f }, { 3.25f, .15f, .8f }, { 3.75f, .15f, .6f },
                  { 4, .15f, .92f }, { 4.75f, .15f, .78f }, { 5.5f, .15f, .88f }, { 6.5f, .15f, .8f }, { 7, .15f, .6f }, { 7.25f, .15f, .8f } } },
              { "i bVI i bVI", "i bII i bII", "i iv i iv", "i bVII bVI bVII", "i bVI bIII bVII", "i v bVI iv",
                "i+bVI i+bVII i+bVI V", "i bII bVII i", "I bII I bII", "i i iv V", "i7 iv7 bVImaj7 V7", "vi IV I V",
                "I V vi IV", "i bIII bVII iv", "i v i bII", "iv i bVI V", "i bVI iv V", "i° bII i bVI", "I iii vi IV" },
              0.85f },
        };
        return m;
    }

    inline const Colour& colourFor (int mood) { return list()[(size_t) juce::jlimit (0, (int) list().size() - 1, mood)].colour; }
    inline const std::vector<std::vector<GHit>>& grooves (int mood) { return list()[(size_t) juce::jlimit (0, (int) list().size() - 1, mood)].grooves; }

    inline juce::StringArray names()
    {
        juce::StringArray a;
        for (auto& x : list()) a.add (fr (x.name));
        return a;
    }

    inline int indexOf (const juce::String& rawName)
    {
        for (int i = 0; i < (int) list().size(); ++i) if (juce::String::fromUTF8 (list()[(size_t) i].name) == rawName) return i;
        return -1;
    }

    // anciens projets (avant v3.20) : numéro de l'ancienne liste -> ambiance actuelle
    inline int fromLegacyIndex (int old)
    {
        // 0 Sombre, 1 Disco, 2 Bossa, 3 Jazz, 4 Lo-fi, 5 Trap, 6 R&B / Neo-soul, 7 Pop, 8 Épique, 9 Gospel, 10 House, 11 Ambient, 12 Funk
        static const char* map[] = { "Trap", "Disco", "Bossa nova", "Jazz", "Lo-fi", "Trap", "Swagg", "Pop", "\xc3\x89pique / Cin\xc3\xa9ma", "Jazz", "House", "Ambient", "Funk" };
        const int i = indexOf (juce::String::fromUTF8 (map[juce::jlimit (0, 12, old)]));
        return i < 0 ? 0 : i;
    }
}
