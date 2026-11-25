#pragma once
// Synthé à tables d'ondes (façon Serum) : 2 oscillateurs wavetable avec unisson, sub, bruit, filtre,
// 3 enveloppes, 4 LFO, matrice de modulation, 4 macros. Les tables de Serum sont lues sur le disque.
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>
#include <cmath>

namespace wt
{
    constexpr int N = 2048;          // échantillons par cycle (format Serum)
    constexpr int maxFrames = 64;    // positions gardées (les tables plus longues sont rééchantillonnées)
    constexpr int numMips = 9;       // versions limitées en harmoniques (anti-repliement)
    constexpr int maxUnison = 8;
    constexpr int maxVoices = 16;

    //==========================================================================
    // Table : frames x mips x (N + 1) échantillons (le dernier recopie le premier pour l'interpolation)
    struct Table
    {
        juce::String name, path;
        int frames = 1;
        std::vector<float> data;
        const float* at (int frame, int mip) const { return data.data() + ((size_t) frame * numMips + (size_t) mip) * (N + 1); }
        const float* raw (int frame) const { return at (frame, 0); }
    };
    using TablePtr = std::shared_ptr<const Table>;

    // construit les mips à partir de cycles bruts (frames x N)
    inline TablePtr buildTable (const std::vector<std::vector<float>>& cycles, const juce::String& name, const juce::String& path)
    {
        auto t = std::make_shared<Table>();
        t->name = name; t->path = path;
        const int srcFrames = juce::jmax (1, (int) cycles.size());
        t->frames = juce::jmin (maxFrames, srcFrames);
        t->data.assign ((size_t) t->frames * numMips * (N + 1), 0.0f);
        juce::dsp::FFT fft (11);
        std::vector<float> spec ((size_t) N * 2), work ((size_t) N * 2);
        float peak = 1.0e-6f;
        for (int f = 0; f < t->frames; ++f)
        {
            const int sf = t->frames == srcFrames ? f : (int) std::round ((double) f * (srcFrames - 1) / juce::jmax (1, t->frames - 1));
            const auto& c = cycles[(size_t) sf];
            std::fill (spec.begin(), spec.end(), 0.0f);
            double mean = 0.0;
            for (int i = 0; i < N; ++i) mean += c[(size_t) i];
            mean /= N;
            for (int i = 0; i < N; ++i) spec[(size_t) i] = c[(size_t) i] - (float) mean;   // sans composante continue
            fft.performRealOnlyForwardTransform (spec.data(), true);
            for (int m = 0; m < numMips; ++m)
            {
                const int maxH = (N / 2) >> m;
                work = spec;
                for (int k = maxH + 1; k <= N / 2; ++k) { work[(size_t) k * 2] = 0.0f; work[(size_t) k * 2 + 1] = 0.0f; }
                for (int k = N / 2 + 1; k < N; ++k) { work[(size_t) k * 2] = 0.0f; work[(size_t) k * 2 + 1] = 0.0f; }
                fft.performRealOnlyInverseTransform (work.data());
                float* dst = const_cast<float*> (t->at (f, m));
                for (int i = 0; i < N; ++i) dst[i] = work[(size_t) i];
                dst[N] = dst[0];
                if (m == 0) for (int i = 0; i < N; ++i) peak = juce::jmax (peak, std::abs (dst[i]));
            }
        }
        const float g = 1.0f / peak;
        for (auto& x : t->data) x *= g;
        return t;
    }

    // Tables de base (sans fichier)
    inline juce::StringArray u8 (std::initializer_list<const char*> l) { juce::StringArray a; for (auto* s : l) a.add (juce::String::fromUTF8 (s)); return a; }
    inline juce::StringArray builtinNames() { return u8 ({ "Basique (sinus > triangle > scie > carré)", "Scie > carré PWM", "Harmoniques", "Sync", "Voix (formants)", "Sinus" }); }
    inline TablePtr builtin (int idx)
    {
        idx = juce::jlimit (0, builtinNames().size() - 1, idx);
        const int F = idx == 5 ? 1 : 32;
        std::vector<std::vector<float>> cy ((size_t) F, std::vector<float> ((size_t) N));
        const float pi2 = juce::MathConstants<float>::twoPi;
        for (int f = 0; f < F; ++f)
        {
            const float m = F > 1 ? (float) f / (F - 1) : 0.0f;
            for (int i = 0; i < N; ++i)
            {
                const float ph = (float) i / N;
                const float sine = std::sin (pi2 * ph);
                const float tri = 1.0f - 4.0f * std::abs (ph - 0.5f);
                const float saw = 2.0f * ph - 1.0f;
                const float sq = ph < 0.5f ? 1.0f : -1.0f;
                float v = 0.0f;
                switch (idx)
                {
                    case 0:   // morph sinus -> triangle -> scie -> carré
                    {
                        const float s = m * 3.0f; const int seg = juce::jmin (2, (int) s); const float u = s - seg;
                        const float a[] = { sine, tri, saw, sq };
                        v = a[seg] * (1.0f - u) + a[seg + 1] * u; break;
                    }
                    case 1: { const float pw = 0.5f - 0.45f * m; v = juce::jmap (m, saw, ph < pw ? 1.0f : -1.0f); if (m > 0.05f) v = ph < pw ? 1.0f : -1.0f; v = juce::jmap (juce::jmin (1.0f, m * 4.0f), saw, v); break; }
                    case 2: { const int H = 1 + (int) (m * 31); for (int h = 1; h <= H; ++h) v += std::sin (pi2 * ph * h) / (float) h; break; }
                    case 3: { const float r = 1.0f + m * 7.0f; v = 2.0f * std::fmod (ph * r, 1.0f) - 1.0f; v *= 1.0f - ph * 0.3f; break; }
                    case 4:
                    {
                        // deux formants qui glissent (a -> e -> i -> o -> u)
                        const float f1 = 3.0f + 6.0f * std::sin (m * 3.0f), f2 = 9.0f + 14.0f * m;
                        for (int h = 1; h <= 40; ++h)
                        {
                            const float g1 = std::exp (-0.5f * std::pow ((h - f1) / 2.0f, 2.0f)), g2 = 0.6f * std::exp (-0.5f * std::pow ((h - f2) / 3.0f, 2.0f));
                            v += (g1 + g2 + 0.15f / h) * std::sin (pi2 * ph * h);
                        }
                        break;
                    }
                    default: v = sine; break;
                }
                cy[(size_t) f][(size_t) i] = v;
            }
        }
        return buildTable (cy, builtinNames()[idx], "builtin:" + juce::String (idx));
    }

    // Lit une table d'ondes .wav (format Serum : cycles de 2048, taille lue dans le bloc « clm » si présent)
    inline TablePtr loadWav (const juce::File& file)
    {
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (file));
        if (r == nullptr || r->lengthInSamples < 16) return nullptr;
        const int len = (int) juce::jmin<juce::int64> (r->lengthInSamples, 2048 * 512);
        juce::AudioBuffer<float> b (1, len);
        r->read (&b, 0, len, 0, true, false);
        int cyc = 2048;
        {
            juce::MemoryBlock mb; file.loadFileAsData (mb);
            const auto s = juce::String::createStringFromData (mb.getData(), juce::jmin ((int) mb.getSize(), 400));
            const int at = s.indexOf ("<!>");
            if (at >= 0) { const int v = s.substring (at + 3, at + 8).getIntValue(); if (v >= 64 && v <= 8192) cyc = v; }
        }
        if (len < cyc) cyc = len;   // un seul cycle plus court
        const int frames = juce::jmax (1, len / cyc);
        std::vector<std::vector<float>> cy ((size_t) frames, std::vector<float> ((size_t) N));
        for (int f = 0; f < frames; ++f)
            for (int i = 0; i < N; ++i)
            {
                const double pos = (double) i * cyc / N;
                const int a = (int) pos; const float u = (float) (pos - a);
                const float* d = b.getReadPointer (0) + f * cyc;
                cy[(size_t) f][(size_t) i] = d[a % cyc] * (1.0f - u) + d[(a + 1) % cyc] * u;
            }
        return buildTable (cy, file.getFileNameWithoutExtension(), file.getFullPathName());
    }

    // Dossiers où chercher des tables : Serum, Serum 2, et le dossier « Wavetables » de la bibliothèque Variant
    inline juce::Array<juce::File> tableFolders (const juce::File& libraryRoot)
    {
        const auto docs = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
        juce::Array<juce::File> out;
        for (auto f : { docs.getChildFile ("Xfer/Serum Presets/Tables"), docs.getChildFile ("Xfer/Serum 2 Presets/Tables"), libraryRoot.getChildFile ("Wavetables") })
            if (f.isDirectory()) out.add (f);
        return out;
    }

    //==========================================================================
    // Paramètres (tous flottants, partagés UI <-> audio)
    enum P
    {
        // oscillateur A puis B (même ordre)
        A_On, A_Pos, A_Oct, A_Semi, A_Fine, A_Uni, A_Detune, A_Blend, A_Width, A_Level, A_Pan, A_Phase, A_Rand, A_Warp,
        B_On, B_Pos, B_Oct, B_Semi, B_Fine, B_Uni, B_Detune, B_Blend, B_Width, B_Level, B_Pan, B_Phase, B_Rand, B_Warp,
        Sub_On, Sub_Shape, Sub_Oct, Sub_Level, Sub_Pan,
        Noise_On, Noise_Type, Noise_Level, Noise_Pan,
        F_On, F_Type, F_Cutoff, F_Res, F_Drive, F_Key, F_Mix,
        E1_A, E1_D, E1_S, E1_R, E2_A, E2_D, E2_S, E2_R, E3_A, E3_D, E3_S, E3_R,
        L1_Shape, L1_Rate, L1_Sync, L1_Retrig, L2_Shape, L2_Rate, L2_Sync, L2_Retrig,
        L3_Shape, L3_Rate, L3_Sync, L3_Retrig, L4_Shape, L4_Rate, L4_Sync, L4_Retrig,
        M1, M2, M3, M4,
        Mod0_Src, Mod0_Dst, Mod0_Amt, Mod1_Src, Mod1_Dst, Mod1_Amt, Mod2_Src, Mod2_Dst, Mod2_Amt, Mod3_Src, Mod3_Dst, Mod3_Amt,
        Mod4_Src, Mod4_Dst, Mod4_Amt, Mod5_Src, Mod5_Dst, Mod5_Amt, Mod6_Src, Mod6_Dst, Mod6_Amt, Mod7_Src, Mod7_Dst, Mod7_Amt,
        Mod8_Src, Mod8_Dst, Mod8_Amt, Mod9_Src, Mod9_Dst, Mod9_Amt,
        G_Volume, G_Voices, G_Mono, G_Glide, G_Bend, G_Vel,
        NumParams
    };
    constexpr int oscStride = B_On - A_On;
    constexpr int numMods = 10;

    struct ParamInfo { const char* id; float def, lo, hi; };
    inline const std::array<ParamInfo, NumParams>& info()
    {
        static const std::array<ParamInfo, NumParams> t = []
        {
            std::array<ParamInfo, NumParams> a {};
            auto osc = [&] (int base, int which, float on)
            {
                static const char* names[2][14] = { { "A_On", "A_Pos", "A_Oct", "A_Semi", "A_Fine", "A_Uni", "A_Detune", "A_Blend", "A_Width", "A_Level", "A_Pan", "A_Phase", "A_Rand", "A_Warp" },
                                                    { "B_On", "B_Pos", "B_Oct", "B_Semi", "B_Fine", "B_Uni", "B_Detune", "B_Blend", "B_Width", "B_Level", "B_Pan", "B_Phase", "B_Rand", "B_Warp" } };
                const float d[][3] = { { on, 0, 1 }, { 0, 0, 1 }, { 0, -3, 3 }, { 0, -12, 12 }, { 0, -100, 100 }, { 1, 1, 8 }, { 0.2f, 0, 1 }, { 0.75f, 0, 1 },
                                       { 0.8f, 0, 1 }, { 0.75f, 0, 1 }, { 0, -1, 1 }, { 0, 0, 1 }, { 1, 0, 1 }, { 0, -1, 1 } };
                for (int i = 0; i < 14; ++i) a[(size_t) (base + i)] = { names[which][i], d[i][0], d[i][1], d[i][2] };
            };
            osc (A_On, 0, 1.0f);
            osc (B_On, 1, 0.0f);
            auto set = [&] (int i, const char* id, float d, float lo, float hi) { a[(size_t) i] = { id, d, lo, hi }; };
            set (Sub_On, "Sub_On", 0, 0, 1); set (Sub_Shape, "Sub_Shape", 0, 0, 2); set (Sub_Oct, "Sub_Oct", -1, -2, 0); set (Sub_Level, "Sub_Level", 0.6f, 0, 1); set (Sub_Pan, "Sub_Pan", 0, -1, 1);
            set (Noise_On, "Noise_On", 0, 0, 1); set (Noise_Type, "Noise_Type", 0, 0, 2); set (Noise_Level, "Noise_Level", 0.25f, 0, 1); set (Noise_Pan, "Noise_Pan", 0, -1, 1);
            set (F_On, "F_On", 1, 0, 1); set (F_Type, "F_Type", 1, 0, 6); set (F_Cutoff, "F_Cutoff", 0.85f, 0, 1); set (F_Res, "F_Res", 0.15f, 0, 1);
            set (F_Drive, "F_Drive", 0, 0, 1); set (F_Key, "F_Key", 0, 0, 1); set (F_Mix, "F_Mix", 1, 0, 1);
            const char* en[] = { "E1_A", "E1_D", "E1_S", "E1_R", "E2_A", "E2_D", "E2_S", "E2_R", "E3_A", "E3_D", "E3_S", "E3_R" };
            const float ed[][4] = { { 0.0005f, 0.4f, 1.0f, 0.25f },   /* attaque au minimum par défaut */ { 0.01f, 0.5f, 0.0f, 0.4f }, { 0.3f, 1.0f, 0.5f, 0.8f } };
            for (int e = 0; e < 3; ++e)
            {
                set (E1_A + e * 4, en[e * 4], ed[e][0], 0.0005f, 10.0f); set (E1_D + e * 4, en[e * 4 + 1], ed[e][1], 0.001f, 10.0f);
                set (E1_S + e * 4, en[e * 4 + 2], ed[e][2], 0, 1);       set (E1_R + e * 4, en[e * 4 + 3], ed[e][3], 0.001f, 12.0f);
            }
            const char* ln[] = { "L1_Shape", "L1_Rate", "L1_Sync", "L1_Retrig", "L2_Shape", "L2_Rate", "L2_Sync", "L2_Retrig",
                                 "L3_Shape", "L3_Rate", "L3_Sync", "L3_Retrig", "L4_Shape", "L4_Rate", "L4_Sync", "L4_Retrig" };
            for (int l = 0; l < 4; ++l)
            {
                set (L1_Shape + l * 4, ln[l * 4], 0, 0, 6); set (L1_Rate + l * 4, ln[l * 4 + 1], 0.35f, 0, 1);
                set (L1_Sync + l * 4, ln[l * 4 + 2], 0, 0, 1); set (L1_Retrig + l * 4, ln[l * 4 + 3], 1, 0, 1);
            }
            set (M1, "M1", 0, 0, 1); set (M2, "M2", 0, 0, 1); set (M3, "M3", 0, 0, 1); set (M4, "M4", 0, 0, 1);
            static const char* mn[] = { "Mod0_Src", "Mod0_Dst", "Mod0_Amt", "Mod1_Src", "Mod1_Dst", "Mod1_Amt", "Mod2_Src", "Mod2_Dst", "Mod2_Amt",
                                        "Mod3_Src", "Mod3_Dst", "Mod3_Amt", "Mod4_Src", "Mod4_Dst", "Mod4_Amt", "Mod5_Src", "Mod5_Dst", "Mod5_Amt",
                                        "Mod6_Src", "Mod6_Dst", "Mod6_Amt", "Mod7_Src", "Mod7_Dst", "Mod7_Amt", "Mod8_Src", "Mod8_Dst", "Mod8_Amt",
                                        "Mod9_Src", "Mod9_Dst", "Mod9_Amt" };
            for (int m = 0; m < numMods; ++m)
            {
                set (Mod0_Src + m * 3, mn[m * 3], 0, 0, 30); set (Mod0_Dst + m * 3, mn[m * 3 + 1], 0, 0, 40); set (Mod0_Amt + m * 3, mn[m * 3 + 2], 0, -1, 1);
            }
            set (G_Volume, "G_Volume", 0.7f, 0, 1); set (G_Voices, "G_Voices", 8, 1, 16); set (G_Mono, "G_Mono", 0, 0, 2);
            set (G_Glide, "G_Glide", 0, 0, 1); set (G_Bend, "G_Bend", 2, 0, 24); set (G_Vel, "G_Vel", 0.6f, 0, 1);
            return a;
        }();
        return t;
    }

    // sources et destinations de modulation
    enum Src { S_None, S_Env2, S_Env3, S_Lfo1, S_Lfo2, S_Lfo3, S_Lfo4, S_Vel, S_Note, S_ModWheel, S_Macro1, S_Macro2, S_Macro3, S_Macro4, S_Random, S_Env1, NumSrc };
    enum Dst { D_None, D_APos, D_ALevel, D_APitch, D_ADetune, D_APan, D_AWarp, D_BPos, D_BLevel, D_BPitch, D_BDetune, D_BPan, D_BWarp,
               D_SubLevel, D_NoiseLevel, D_Cutoff, D_Res, D_Drive, D_FMix, D_Volume, D_Pitch, D_L1Rate, D_L2Rate, NumDst };
    inline juce::StringArray srcNames() { return u8 ({ "—", "Env 2", "Env 3", "LFO 1", "LFO 2", "LFO 3", "LFO 4", "Vélocité", "Note (clavier)", "Mod wheel", "Macro 1", "Macro 2", "Macro 3", "Macro 4", "Hasard (par note)", "Env 1" }); }
    inline juce::StringArray dstNames()
    {
        return u8 ({ "—", "A : position", "A : volume", "A : hauteur", "A : detune", "A : pan", "A : warp", "B : position", "B : volume", "B : hauteur", "B : detune", "B : pan", "B : warp",
                     "Sub : volume", "Bruit : volume", "Filtre : cutoff", "Filtre : résonance", "Filtre : drive", "Filtre : mix", "Volume", "Hauteur (tout)", "LFO 1 : vitesse", "LFO 2 : vitesse" });
    }
    inline juce::StringArray filterNames() { return { "Passe-bas 12", "Passe-bas 24", "Passe-haut 12", "Passe-haut 24", "Passe-bande", "Coupe-bande", "Peigne" }; }
    inline juce::StringArray lfoShapeNames() { return u8 ({ "Sinus", "Triangle", "Scie descendante", "Scie montante", "Carré", "Hasard (S&H)", "Hasard doux" }); }
    inline juce::StringArray lfoSyncNames() { return { "8 mesures", "4 mesures", "2 mesures", "1 mesure", "1/2", "1/4", "1/8", "1/16", "1/32", "1/4 T", "1/8 T", "1/16 T" }; }
    inline double lfoSyncBeats (int i) { static const double b[] = { 32, 16, 8, 4, 2, 1, 0.5, 0.25, 0.125, 2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0 }; return b[juce::jlimit (0, 11, i)]; }
    inline float lfoHz (float rate01) { return 0.05f * std::pow (400.0f, rate01); }   // 0,05 .. 20 Hz
    inline float cutoffHz (float c01) { return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, c01)); }   // 20 Hz .. 20 kHz

    //==========================================================================
    struct Patch
    {
        std::array<std::atomic<float>, NumParams> v;
        Patch() { reset(); }
        void reset() { for (int i = 0; i < NumParams; ++i) v[(size_t) i].store (info()[(size_t) i].def); }
        float get (int i) const { return v[(size_t) i].load (std::memory_order_relaxed); }
        void set (int i, float x) { const auto& in = info()[(size_t) i]; v[(size_t) i].store (juce::jlimit (in.lo, in.hi, x), std::memory_order_relaxed); }
    };

    //==========================================================================
    struct Adsr
    {
        enum Stage { Idle, Attack, Decay, Sustain, Release } stage = Idle;
        float level = 0.0f, relFrom = 0.0f;
        void noteOn (bool legato) { stage = Attack; if (! legato) level = juce::jmin (level, 1.0f); }
        void noteOff() { if (stage != Idle) { stage = Release; relFrom = level; } }
        // avance de n échantillons
        float next (float a, float d, float s, float r, float sr, int n)
        {
            const float dt = (float) n / sr;
            switch (stage)
            {
                case Attack:  level += dt / juce::jmax (0.0005f, a); if (level >= 1.0f) { level = 1.0f; stage = Decay; } break;
                case Decay:   level = s + (level - s) * std::exp (-dt / juce::jmax (0.001f, d * 0.3f)); if (std::abs (level - s) < 1.0e-4f) { level = s; stage = Sustain; } break;
                case Sustain: level = s; break;
                case Release: level *= std::exp (-dt / juce::jmax (0.001f, r * 0.3f)); if (level < 1.0e-4f) { level = 0.0f; stage = Idle; } break;
                default: level = 0.0f; break;
            }
            return level;
        }
    };

    struct Svf   // filtre à variable d'état (TPT), stéréo
    {
        float ic1[2] {}, ic2[2] {};
        void reset() { for (int c = 0; c < 2; ++c) ic1[c] = ic2[c] = 0.0f; }
        // mode 0 LP, 1 HP, 2 BP, 3 notch
        float process (int ch, float x, float g, float k, int mode)
        {
            const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
            const float v3 = x - ic2[ch];
            const float v1 = a1 * ic1[ch] + a2 * v3;
            const float v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
            ic1[ch] = 2.0f * v1 - ic1[ch];
            ic2[ch] = 2.0f * v2 - ic2[ch];
            switch (mode)
            {
                case 1: return x - k * v1 - v2;
                case 2: return v1 * k;
                case 3: return x - k * v1;
                default: return v2;
            }
        }
    };

    inline float lfoShape (int shape, float ph, float& hold, float& prevHold, juce::Random& rng, bool wrapped)
    {
        switch (shape)
        {
            case 1: return 1.0f - 4.0f * std::abs (ph - 0.5f);
            case 2: return 1.0f - 2.0f * ph;
            case 3: return 2.0f * ph - 1.0f;
            case 4: return ph < 0.5f ? 1.0f : -1.0f;
            case 5: if (wrapped) hold = rng.nextFloat() * 2.0f - 1.0f; return hold;
            case 6: if (wrapped) { prevHold = hold; hold = rng.nextFloat() * 2.0f - 1.0f; } return prevHold + (hold - prevHold) * (0.5f - 0.5f * std::cos (ph * juce::MathConstants<float>::pi));
            default: return std::sin (ph * juce::MathConstants<float>::twoPi);
        }
    }

    //==========================================================================
    class Engine
    {
    public:
        Patch patch;
        std::atomic<bool> enabled { false };
        std::atomic<float> mix { 0.8f };       // volume du synthé dans la sortie du plugin
        std::atomic<float> uiPos[2] { { 0.0f }, { 0.0f } };   // position modulée affichée (oscillateur A / B)
        std::atomic<float> uiLfo[4] {};
        std::atomic<int> activeVoices { 0 };

        Engine() { setTable (0, builtin (0)); setTable (1, builtin (0)); }

        void setTable (int osc, TablePtr t)
        {
            if (t == nullptr) return;
            const juce::SpinLock::ScopedLockType l (tableLock);
            retired.push_back (tables[(size_t) osc]);
            if (retired.size() > 6) retired.erase (retired.begin());
            tables[(size_t) osc] = std::move (t);
        }
        TablePtr getTable (int osc) const { const juce::SpinLock::ScopedLockType l (tableLock); return tables[(size_t) osc]; }

        void prepare (double sr) { sampleRate = sr; for (auto& v : voices) v.reset(); }
        void allOff() { for (auto& v : voices) { v.env[0].stage = Adsr::Idle; v.active = false; } held.clear(); }

        // rend le synthé (ajouté dans buffer) pour les notes de midi
        void render (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, int n, double bpm, double ppq, bool playing)
        {
            if (! enabled.load())
            {
                if (wasEnabled) { allOff(); wasEnabled = false; }
                return;
            }
            wasEnabled = true;
            TablePtr tab[2];
            {
                const juce::SpinLock::ScopedLockType l (tableLock);
                tab[0] = tables[0]; tab[1] = tables[1];
            }
            int pos = 0;
            for (const auto meta : midi)
            {
                const int at = juce::jlimit (0, n, meta.samplePosition);
                if (at > pos) { renderRange (buffer, pos, at - pos, tab, bpm, ppq + (double) pos / sampleRate * bpm / 60.0, playing); pos = at; }
                handle (meta.getMessage());
            }
            if (pos < n) renderRange (buffer, pos, n - pos, tab, bpm, ppq + (double) pos / sampleRate * bpm / 60.0, playing);
            int act = 0; for (auto& v : voices) act += v.active ? 1 : 0;
            activeVoices.store (act);
        }

    private:
        struct OscState { double phase[maxUnison] {}; };
        struct Voice
        {
            bool active = false, released = false;
            int note = 60; float vel = 0.8f, random = 0.0f;
            double freqNow = 261.6, freqTarget = 261.6;
            OscState osc[2];
            double subPhase = 0.0;
            Adsr env[3];
            float lfoPh[4] {}, lfoHold[4] {}, lfoPrev[4] {};
            Svf f1, f2;
            float noiseState[2] {};
            std::array<std::array<float, 4096>, 2> comb {};
            int combPos[2] {};
            juce::uint32 age = 0;
            void reset() { active = false; for (auto& e : env) e = Adsr(); f1.reset(); f2.reset(); }
        };

        double sampleRate = 44100.0;
        std::array<Voice, maxVoices> voices;
        std::array<TablePtr, 2> tables;
        std::vector<TablePtr> retired;
        mutable juce::SpinLock tableLock;
        std::vector<int> held;   // notes tenues (mode mono)
        float bend = 0.0f, modWheel = 0.0f;
        juce::uint32 ageCounter = 0;
        juce::Random rng;
        float globalLfoPh[4] {};
        bool wasEnabled = false;

        float p (int i) const { return patch.get (i); }

        void handle (const juce::MidiMessage& m)
        {
            if (m.isNoteOn()) noteOn (m.getNoteNumber(), m.getFloatVelocity());
            else if (m.isNoteOff()) noteOff (m.getNoteNumber());
            else if (m.isPitchWheel()) bend = (m.getPitchWheelValue() - 8192) / 8192.0f;
            else if (m.isController() && m.getControllerNumber() == 1) modWheel = m.getControllerValue() / 127.0f;
            else if (m.isAllNotesOff() || m.isAllSoundOff()) { for (auto& v : voices) if (v.active) { for (auto& e : v.env) e.noteOff(); v.released = true; } held.clear(); }
        }

        static double noteHz (double note) { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); }

        void startVoice (Voice& v, int note, float vel, bool legato)
        {
            const bool glide = p (G_Glide) > 0.001f && v.active;
            v.note = note; v.vel = vel; v.released = false;
            v.freqTarget = noteHz (note);
            if (! glide) v.freqNow = v.freqTarget;
            if (! legato)
            {
                v.random = rng.nextFloat() * 2.0f - 1.0f;
                for (int o = 0; o < 2; ++o)
                    for (int u = 0; u < maxUnison; ++u)
                    {
                        const float base = p (A_Phase + o * oscStride);
                        v.osc[o].phase[u] = std::fmod (base + p (A_Rand + o * oscStride) * rng.nextFloat(), 1.0f);
                    }
                for (int l = 0; l < 4; ++l)
                    if (p (L1_Retrig + l * 4) > 0.5f) { v.lfoPh[l] = 0.0f; v.lfoHold[l] = v.lfoPrev[l] = rng.nextFloat() * 2.0f - 1.0f; }
                    else v.lfoPh[l] = globalLfoPh[l];
                if (! v.active) { v.f1.reset(); v.f2.reset(); for (auto& e : v.env) e.level = 0.0f; }
                for (auto& e : v.env) e.noteOn (false);
            }
            v.active = true;
            v.age = ++ageCounter;
        }

        void noteOn (int note, float vel)
        {
            const int mono = (int) p (G_Mono);
            if (mono > 0)
            {
                held.erase (std::remove (held.begin(), held.end(), note), held.end());
                held.push_back (note);
                auto& v = voices[0];
                const bool legato = mono == 2 && v.active && ! v.released;
                startVoice (v, note, vel, legato);
                return;
            }
            const int maxV = juce::jlimit (1, maxVoices, (int) p (G_Voices));
            Voice* pick = nullptr;
            for (int i = 0; i < maxV; ++i) if (voices[(size_t) i].active && voices[(size_t) i].note == note) { pick = &voices[(size_t) i]; break; }   // même note : on la rejoue
            if (pick == nullptr) for (int i = 0; i < maxV; ++i) if (! voices[(size_t) i].active) { pick = &voices[(size_t) i]; break; }
            if (pick == nullptr)
            {
                // vole la voix relâchée la plus ancienne, sinon la plus ancienne
                for (int i = 0; i < maxV; ++i) if (voices[(size_t) i].released && (pick == nullptr || voices[(size_t) i].age < pick->age)) pick = &voices[(size_t) i];
                if (pick == nullptr) for (int i = 0; i < maxV; ++i) if (pick == nullptr || voices[(size_t) i].age < pick->age) pick = &voices[(size_t) i];
            }
            if (pick != nullptr) startVoice (*pick, note, vel, false);
        }

        void noteOff (int note)
        {
            if ((int) p (G_Mono) > 0)
            {
                held.erase (std::remove (held.begin(), held.end(), note), held.end());
                auto& v = voices[0];
                if (! v.active || v.note != note) return;
                if (! held.empty()) { startVoice (v, held.back(), v.vel, (int) p (G_Mono) == 2); return; }
                for (auto& e : v.env) e.noteOff();
                v.released = true;
                return;
            }
            for (auto& v : voices)
                if (v.active && ! v.released && v.note == note) { for (auto& e : v.env) e.noteOff(); v.released = true; }
        }

        static float readTable (const Table& t, float pos, int mip, double phase)
        {
            const float fp = juce::jlimit (0.0f, 1.0f, pos) * (float) (t.frames - 1);
            const int f0 = (int) fp; const int f1 = juce::jmin (t.frames - 1, f0 + 1); const float fu = fp - (float) f0;
            const double x = phase * N; const int i = (int) x; const float u = (float) (x - i);
            const float* a = t.at (f0, mip); const float* b = t.at (f1, mip);
            const float va = a[i] + (a[i + 1] - a[i]) * u;
            const float vb = b[i] + (b[i + 1] - b[i]) * u;
            return va + (vb - va) * fu;
        }
        int mipFor (double hz) const
        {
            const double nyq = sampleRate * 0.5;
            for (int m = 0; m < numMips; ++m) if ((double) ((N / 2) >> m) * hz < nyq) return m;
            return numMips - 1;
        }
        static double warp (double ph, float w)
        {
            if (std::abs (w) < 0.001f) return ph;
            const double e = std::exp (-2.2 * w);   // bend : la phase accélère ou ralentit
            return std::pow (ph, e);
        }

        void renderRange (juce::AudioBuffer<float>& buffer, int start, int num, TablePtr* tab, double bpm, double ppq, bool playing)
        {
            constexpr int ctrl = 16;   // les modulations sont recalculées tous les 16 échantillons
            const float sr = (float) sampleRate;
            float* outL = buffer.getWritePointer (0);
            float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
            const float master = mix.load();
            // LFO globaux (pour les LFO non redéclenchés et l'affichage)
            for (int l = 0; l < 4; ++l)
            {
                const double hz = p (L1_Sync + l * 4) > 0.5f ? bpm / 60.0 / lfoSyncBeats ((int) std::round (p (L1_Rate + l * 4) * 11.0f)) : lfoHz (p (L1_Rate + l * 4));
                if (p (L1_Sync + l * 4) > 0.5f && playing) globalLfoPh[l] = (float) std::fmod (ppq / lfoSyncBeats ((int) std::round (p (L1_Rate + l * 4) * 11.0f)), 1.0);
                else globalLfoPh[l] = (float) std::fmod (globalLfoPh[l] + hz * num / sampleRate, 1.0);
            }
            for (auto& v : voices)
            {
                if (! v.active) continue;
                for (int s0 = 0; s0 < num; s0 += ctrl)
                {
                    const int len = juce::jmin (ctrl, num - s0);
                    // ---- enveloppes et LFO
                    const float env0 = v.env[0].level;
                    float envv[3];
                    for (int e = 0; e < 3; ++e) envv[e] = v.env[e].next (p (E1_A + e * 4), p (E1_D + e * 4), p (E1_S + e * 4), p (E1_R + e * 4), sr, len);
                    if (v.env[0].stage == Adsr::Idle) { v.active = false; break; }
                    float lfo[4];
                    for (int l = 0; l < 4; ++l)
                    {
                        const bool sync = p (L1_Sync + l * 4) > 0.5f;
                        const int si = (int) std::round (p (L1_Rate + l * 4) * 11.0f);
                        double hz = sync ? bpm / 60.0 / lfoSyncBeats (si) : lfoHz (p (L1_Rate + l * 4));
                        hz *= std::pow (2.0, (double) modSum (l == 0 ? D_L1Rate : (l == 1 ? D_L2Rate : -1), v, envv, nullptr) * 3.0);
                        if (p (L1_Retrig + l * 4) < 0.5f) v.lfoPh[l] = globalLfoPh[l];
                        const float before = v.lfoPh[l];
                        if (p (L1_Retrig + l * 4) > 0.5f) v.lfoPh[l] = (float) std::fmod (v.lfoPh[l] + hz * len / sampleRate, 1.0);
                        const bool wrapped = v.lfoPh[l] < before;
                        lfo[l] = lfoShape ((int) p (L1_Shape + l * 4), v.lfoPh[l], v.lfoHold[l], v.lfoPrev[l], rng, wrapped);
                    }
                    curLfo = lfo;
                    auto mod = [&] (int d) { return modSum (d, v, envv, lfo); };

                    // ---- glide
                    if (p (G_Glide) > 0.001f && std::abs (v.freqNow - v.freqTarget) > 0.01)
                    {
                        const double k = std::exp (-(double) len / (sampleRate * (0.01 + p (G_Glide) * 0.8)));
                        v.freqNow = v.freqTarget * std::pow (v.freqNow / v.freqTarget, k);
                    }
                    else v.freqNow = v.freqTarget;
                    const double pitchAll = (double) bend * p (G_Bend) + mod (D_Pitch) * 24.0;

                    // ---- oscillateurs : réglages du bloc
                    struct OscBlock { bool on; const Table* t; float pos, level, panC, width, blend, warpAmt; int uni, mip; double inc[maxUnison]; float pan[maxUnison], gain[maxUnison]; };
                    OscBlock ob[2];
                    for (int o = 0; o < 2; ++o)
                    {
                        const int b = o * oscStride;
                        auto& B = ob[o];
                        B.on = p (A_On + b) > 0.5f && tab[o] != nullptr;
                        if (! B.on) continue;
                        B.t = tab[o].get();
                        B.pos = juce::jlimit (0.0f, 1.0f, p (A_Pos + b) + mod (o == 0 ? D_APos : D_BPos));
                        B.level = juce::jlimit (0.0f, 1.0f, p (A_Level + b) + mod (o == 0 ? D_ALevel : D_BLevel));
                        B.panC = juce::jlimit (-1.0f, 1.0f, p (A_Pan + b) + mod (o == 0 ? D_APan : D_BPan));
                        B.warpAmt = juce::jlimit (-1.0f, 1.0f, p (A_Warp + b) + mod (o == 0 ? D_AWarp : D_BWarp));
                        B.uni = juce::jlimit (1, maxUnison, (int) std::round (p (A_Uni + b)));
                        const float det = juce::jlimit (0.0f, 1.0f, p (A_Detune + b) + mod (o == 0 ? D_ADetune : D_BDetune));
                        B.width = p (A_Width + b); B.blend = p (A_Blend + b);
                        const double semis = p (A_Oct + b) * 12.0 + p (A_Semi + b) + p (A_Fine + b) * 0.01 + pitchAll + mod (o == 0 ? D_APitch : D_BPitch) * 24.0;
                        const double hz = v.freqNow * std::pow (2.0, semis / 12.0);
                        B.mip = mipFor (hz * (1.0 + det * 0.03));
                        float gsum = 0.0f;
                        for (int u = 0; u < B.uni; ++u)
                        {
                            const float spread = B.uni == 1 ? 0.0f : (float) u / (B.uni - 1) * 2.0f - 1.0f;   // -1 .. 1
                            const double cents = spread * det * 50.0;   // jusqu'à ±50 cents
                            B.inc[u] = hz * std::pow (2.0, cents / 1200.0) / sampleRate;
                            B.pan[u] = juce::jlimit (-1.0f, 1.0f, B.panC + spread * B.width);
                            const bool centre = B.uni == 1 || std::abs (spread) < 0.34f;
                            B.gain[u] = centre ? 1.0f : B.blend;
                            gsum += B.gain[u];
                        }
                        const float norm = B.level / std::sqrt (juce::jmax (1.0f, gsum));
                        for (int u = 0; u < B.uni; ++u) B.gain[u] *= norm;
                    }
                    if (curVoiceIsFirst (v)) { uiPos[0].store (ob[0].on ? ob[0].pos : p (A_Pos)); uiPos[1].store (ob[1].on ? ob[1].pos : p (B_Pos)); for (int l = 0; l < 4; ++l) uiLfo[l].store (lfo[l]); }

                    const bool subOn = p (Sub_On) > 0.5f, noiseOn = p (Noise_On) > 0.5f;
                    const float subLvl = juce::jlimit (0.0f, 1.0f, p (Sub_Level) + mod (D_SubLevel)) * 0.8f;
                    const float noiseLvl = juce::jlimit (0.0f, 1.0f, p (Noise_Level) + mod (D_NoiseLevel)) * 0.5f;
                    const double subInc = v.freqNow * std::pow (2.0, p (Sub_Oct) + pitchAll / 12.0) / sampleRate;
                    const int subShape = (int) p (Sub_Shape), noiseType = (int) p (Noise_Type);
                    const float subPan = p (Sub_Pan), noisePan = p (Noise_Pan);

                    // ---- filtre
                    const bool fOn = p (F_On) > 0.5f;
                    const int fType = (int) p (F_Type);
                    float cut01 = p (F_Cutoff) + mod (D_Cutoff) * 0.5f + p (F_Key) * (float) (v.note - 60) / 120.0f;
                    const float fc = juce::jlimit (20.0f, sr * 0.45f, cutoffHz (cut01));
                    const float res = juce::jlimit (0.0f, 1.0f, p (F_Res) + mod (D_Res));
                    const float drive = juce::jlimit (0.0f, 1.0f, p (F_Drive) + mod (D_Drive));
                    const float fmix = juce::jlimit (0.0f, 1.0f, p (F_Mix) + mod (D_FMix));
                    const float g = std::tan (juce::MathConstants<float>::pi * fc / sr);
                    const float k = 2.0f - 1.96f * res;
                    const float preGain = 1.0f + drive * 6.0f;
                    const int combLen = juce::jlimit (1, 4095, (int) (sr / fc));

                    const float velAmt = p (G_Vel);
                    const float amp = juce::jlimit (0.0f, 2.0f, (1.0f - velAmt + velAmt * v.vel) * juce::jlimit (0.0f, 1.0f, p (G_Volume) + mod (D_Volume)) * master);

                    for (int i = 0; i < len; ++i)
                    {
                        const float env = env0 + (envv[0] - env0) * (float) (i + 1) / (float) len;   // enveloppe lissée dans le bloc
                        float l = 0.0f, r = 0.0f;
                        for (int o = 0; o < 2; ++o)
                        {
                            auto& B = ob[o];
                            if (! B.on) continue;
                            for (int u = 0; u < B.uni; ++u)
                            {
                                double& ph = v.osc[o].phase[u];
                                const float s = readTable (*B.t, B.pos, B.mip, warp (ph, B.warpAmt)) * B.gain[u];
                                ph += B.inc[u]; if (ph >= 1.0) ph -= 1.0;
                                l += s * (0.5f - 0.5f * B.pan[u]); r += s * (0.5f + 0.5f * B.pan[u]);
                            }
                        }
                        if (subOn)
                        {
                            const float ph = (float) v.subPhase;
                            const float s = (subShape == 0 ? std::sin (ph * juce::MathConstants<float>::twoPi) : (subShape == 1 ? 1.0f - 4.0f * std::abs (ph - 0.5f) : (ph < 0.5f ? 0.7f : -0.7f))) * subLvl;
                            v.subPhase += subInc; if (v.subPhase >= 1.0) v.subPhase -= 1.0;
                            l += s * (0.5f - 0.5f * subPan); r += s * (0.5f + 0.5f * subPan);
                        }
                        if (noiseOn)
                        {
                            float w = rng.nextFloat() * 2.0f - 1.0f;
                            if (noiseType == 1) { v.noiseState[0] = 0.97f * v.noiseState[0] + 0.03f * w; w = v.noiseState[0] * 5.0f; }   // rose / grave
                            else if (noiseType == 2) w = rng.nextFloat() < 0.002f ? w * 4.0f : w * 0.05f;   // crépitements vinyle
                            l += w * noiseLvl * (0.5f - 0.5f * noisePan); r += w * noiseLvl * (0.5f + 0.5f * noisePan);
                        }
                        if (fOn)
                        {
                            for (int ch = 0; ch < 2; ++ch)
                            {
                                float x = ch == 0 ? l : r;
                                float in = drive > 0.001f ? std::tanh (x * preGain) / std::sqrt (preGain) : x;
                                float y;
                                switch (fType)
                                {
                                    case 0: y = v.f1.process (ch, in, g, k, 0); break;
                                    case 1: y = v.f2.process (ch, v.f1.process (ch, in, g, k, 0), g, juce::jmax (0.7f, k), 0); break;
                                    case 2: y = v.f1.process (ch, in, g, k, 1); break;
                                    case 3: y = v.f2.process (ch, v.f1.process (ch, in, g, k, 1), g, juce::jmax (0.7f, k), 1); break;
                                    case 4: y = v.f1.process (ch, in, g, k, 2); break;
                                    case 5: y = v.f1.process (ch, in, g, k, 3); break;
                                    default:
                                    {
                                        auto& buf = v.comb[(size_t) ch];
                                        auto& wp = v.combPos[ch];
                                        const float d = buf[(size_t) ((wp - combLen + 4096) & 4095)];
                                        y = in + d * (0.5f + 0.45f * res);
                                        buf[(size_t) wp] = y; wp = (wp + 1) & 4095;
                                        y *= 0.5f;
                                        break;
                                    }
                                }
                                const float out = x + (y - x) * fmix;
                                if (ch == 0) l = out; else r = out;
                            }
                        }
                        // niveau de sortie : au même niveau que les samples (avant, le synthé sortait ~10 dB trop bas)
                        const float gOut = env * amp * 1.6f;
                        outL[start + s0 + i] += l * gOut;
                        if (outR != nullptr) outR[start + s0 + i] += r * gOut;
                    }
                }
            }
        }

        bool curVoiceIsFirst (const Voice& v) const
        {
            for (auto& o : voices) if (o.active) return &o == &v;
            return false;
        }

        const float* curLfo = nullptr;

        float srcValue (int s, const Voice& v, const float* env, const float* lfo) const
        {
            switch (s)
            {
                case S_Env1: return env[0];
                case S_Env2: return env[1];
                case S_Env3: return env[2];
                case S_Lfo1: case S_Lfo2: case S_Lfo3: case S_Lfo4: return lfo != nullptr ? lfo[s - S_Lfo1] : 0.0f;
                case S_Vel: return v.vel;
                case S_Note: return (float) (v.note - 60) / 24.0f;
                case S_ModWheel: return modWheel;
                case S_Macro1: case S_Macro2: case S_Macro3: case S_Macro4: return p (M1 + (s - S_Macro1));
                case S_Random: return v.random;
                default: return 0.0f;
            }
        }
        float modSum (int dst, const Voice& v, const float* env, const float* lfo) const
        {
            if (dst <= 0) return 0.0f;
            float sum = 0.0f;
            for (int m = 0; m < numMods; ++m)
                if ((int) p (Mod0_Dst + m * 3) == dst)
                {
                    const int s = (int) p (Mod0_Src + m * 3);
                    if (s <= 0) continue;
                    sum += srcValue (s, v, env, lfo) * p (Mod0_Amt + m * 3);
                }
            return sum;
        }
    };

    //==========================================================================
    // État : paramètres + tables (pour sauvegarde du projet et presets .vsynth)
    inline juce::ValueTree toTree (const Engine& e)
    {
        juce::ValueTree t ("Synth");
        for (int i = 0; i < NumParams; ++i) t.setProperty (info()[(size_t) i].id, e.patch.get (i), nullptr);
        for (int o = 0; o < 2; ++o) if (auto tb = e.getTable (o)) t.setProperty (o == 0 ? "tableA" : "tableB", tb->path, nullptr);
        t.setProperty ("on", e.enabled.load(), nullptr);
        t.setProperty ("mix", e.mix.load(), nullptr);
        return t;
    }

    inline TablePtr tableFromPath (const juce::String& path)
    {
        if (path.startsWith ("builtin:")) return builtin (path.fromFirstOccurrenceOf (":", false, false).getIntValue());
        const juce::File f (path);
        if (juce::File::isAbsolutePath (path) && f.existsAsFile()) return loadWav (f);
        // table de Serum enregistrée sur un autre ordinateur : on la cherche dans le dossier Serum de ce PC / Mac
        const auto norm = path.replaceCharacter ('\\', '/');
        if (norm.containsIgnoreCase ("/Tables/"))
        {
            const auto rel = norm.fromLastOccurrenceOf ("/Tables/", false, true);
            const auto docs = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
            for (auto base : { docs.getChildFile ("Xfer/Serum Presets/Tables"), docs.getChildFile ("Xfer/Serum 2 Presets/Tables") })
                if (base.getChildFile (rel).existsAsFile()) return loadWav (base.getChildFile (rel));
        }
        return nullptr;
    }

    inline void fromTree (Engine& e, const juce::ValueTree& t, bool includeOnMix)
    {
        if (! t.isValid()) return;
        for (int i = 0; i < NumParams; ++i)
        {
            const auto& in = info()[(size_t) i];
            e.patch.set (i, t.hasProperty (in.id) ? (float) t.getProperty (in.id) : in.def);
        }
        for (int o = 0; o < 2; ++o)
        {
            const auto path = t.getProperty (o == 0 ? "tableA" : "tableB").toString();
            auto tb = path.isNotEmpty() ? tableFromPath (path) : nullptr;
            e.setTable (o, tb != nullptr ? tb : builtin (0));
        }
        if (includeOnMix)
        {
            e.enabled.store ((bool) t.getProperty ("on", false));
            e.mix.store ((float) t.getProperty ("mix", 0.8f));
        }
    }
}
