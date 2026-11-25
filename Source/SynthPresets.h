#pragma once
// Presets du synthé : rangés dans la banque de sons (sous-dossier « Presets de synthé »), fichiers .vsynth
#include "WtSynth.h"
#include "Library.h"

namespace synthpresets
{
    inline juce::String folderName() { return juce::String::fromUTF8 ("Presets de synthé"); }
    inline juce::File folder()
    {
        auto f = library::defaultFolder().getChildFile (folderName());
        // pas créé d avance : seulement quand l utilisateur sauve son premier preset de synthé
        return f;
    }
    inline juce::File wavetableFolder()
    {
        auto f = library::defaultFolder().getChildFile ("Wavetables");
        f.createDirectory();
        return f;
    }

    struct Factory { const char* name; const char* category; std::function<void (wt::Patch&, juce::String& tableA, juce::String& tableB)> make; };

    // raccourcis
    inline void mod (wt::Patch& p, int slot, int src, int dst, float amt) { p.set (wt::Mod0_Src + slot * 3, (float) src); p.set (wt::Mod0_Dst + slot * 3, (float) dst); p.set (wt::Mod0_Amt + slot * 3, amt); }
    inline void env (wt::Patch& p, int e, float a, float d, float s, float r) { p.set (wt::E1_A + e * 4, a); p.set (wt::E1_D + e * 4, d); p.set (wt::E1_S + e * 4, s); p.set (wt::E1_R + e * 4, r); }
    inline void lfo (wt::Patch& p, int l, int shape, float rate, bool sync, bool retrig) { p.set (wt::L1_Shape + l * 4, (float) shape); p.set (wt::L1_Rate + l * 4, rate); p.set (wt::L1_Sync + l * 4, sync ? 1.0f : 0.0f); p.set (wt::L1_Retrig + l * 4, retrig ? 1.0f : 0.0f); }
    inline float syncRate (int idx) { return (float) idx / 11.0f; }   // index de wt::lfoSyncNames()

    inline const std::vector<Factory>& factory()
    {
        using namespace wt;
        static const std::vector<Factory> f {
            { "Init", "Base", [] (Patch& p, juce::String& a, juce::String&) { a = "builtin:0"; p.set (A_Pos, 0.66f); } },
            { "Supersaw", "Lead", [] (Patch& p, juce::String& a, juce::String&) {
                a = "builtin:0"; p.set (A_Pos, 0.66f); p.set (A_Uni, 7); p.set (A_Detune, 0.38f); p.set (A_Blend, 0.8f); p.set (A_Width, 1.0f);
                p.set (F_Type, 1); p.set (F_Cutoff, 0.82f); p.set (F_Res, 0.1f); env (p, 0, 0.005f, 0.6f, 0.9f, 0.35f); } },
            { "Reese", "Basse", [] (Patch& p, juce::String& a, juce::String& b) {
                a = "builtin:0"; b = "builtin:0"; p.set (A_Pos, 0.66f); p.set (B_On, 1); p.set (B_Pos, 0.66f); p.set (B_Fine, 18); p.set (A_Uni, 2); p.set (A_Detune, 0.25f);
                p.set (A_Oct, -1); p.set (B_Oct, -1); p.set (F_Type, 1); p.set (F_Cutoff, 0.5f); p.set (F_Res, 0.2f); p.set (F_Drive, 0.3f);
                p.set (G_Mono, 2); p.set (G_Glide, 0.12f); p.set (Sub_On, 1); p.set (Sub_Level, 0.5f);
                lfo (p, 0, 0, 0.18f, false, false); mod (p, 0, S_Lfo1, D_Cutoff, 0.12f); env (p, 0, 0.003f, 1.0f, 1.0f, 0.2f); } },
            { "Sub 808", "Basse", [] (Patch& p, juce::String& a, juce::String&) {
                a = "builtin:5"; p.set (A_Oct, -1); p.set (A_Level, 0.9f); p.set (F_On, 0); p.set (G_Mono, 2); p.set (G_Glide, 0.18f);
                p.set (A_Warp, 0.15f); env (p, 0, 0.002f, 1.6f, 0.0f, 0.6f); env (p, 1, 0.001f, 0.08f, 0.0f, 0.1f); mod (p, 0, S_Env2, D_APitch, 0.25f); } },
            { "Pluck", "Pluck", [] (Patch& p, juce::String& a, juce::String&) {
                a = "builtin:0"; p.set (A_Pos, 0.6f); p.set (A_Uni, 3); p.set (A_Detune, 0.15f); p.set (F_Type, 1); p.set (F_Cutoff, 0.35f); p.set (F_Res, 0.25f);
                env (p, 0, 0.002f, 0.9f, 0.0f, 0.45f); env (p, 1, 0.001f, 0.45f, 0.0f, 0.3f); mod (p, 0, S_Env2, D_Cutoff, 0.8f); } },
            { "Pad qui bouge", "Pad", [] (Patch& p, juce::String& a, juce::String& b) {
                a = "builtin:4"; b = "builtin:2"; p.set (B_On, 1); p.set (B_Oct, 1); p.set (B_Level, 0.4f); p.set (A_Uni, 4); p.set (A_Detune, 0.3f); p.set (A_Width, 1.0f);
                p.set (F_Type, 0); p.set (F_Cutoff, 0.62f); env (p, 0, 0.9f, 1.0f, 0.85f, 2.5f);
                lfo (p, 0, 0, 0.2f, false, false); mod (p, 0, S_Lfo1, D_APos, 0.4f); lfo (p, 1, 1, 0.12f, false, false); mod (p, 1, S_Lfo2, D_BPos, 0.5f); mod (p, 2, S_Lfo2, D_Cutoff, 0.15f); } },
            { "Keys douces", "Clavier", [] (Patch& p, juce::String& a, juce::String&) {
                a = "builtin:2"; p.set (A_Pos, 0.15f); p.set (F_Type, 0); p.set (F_Cutoff, 0.7f); env (p, 0, 0.003f, 1.4f, 0.25f, 0.6f);
                env (p, 1, 0.002f, 0.8f, 0.1f, 0.5f); mod (p, 0, S_Env2, D_Cutoff, 0.3f); mod (p, 1, S_Vel, D_Cutoff, 0.25f); } },
            { "Cloche", "Clavier", [] (Patch& p, juce::String& a, juce::String& b) {
                a = "builtin:2"; b = "builtin:5"; p.set (A_Pos, 0.85f); p.set (B_On, 1); p.set (B_Semi, 7); p.set (B_Oct, 1); p.set (B_Level, 0.35f);
                p.set (F_On, 0); env (p, 0, 0.001f, 2.2f, 0.0f, 1.5f); p.set (A_Warp, 0.4f); } },
            { "Wobble", "Basse", [] (Patch& p, juce::String& a, juce::String& b) {
                a = "builtin:0"; b = "builtin:1"; p.set (A_Pos, 0.66f); p.set (B_On, 1); p.set (B_Pos, 0.4f); p.set (A_Oct, -1); p.set (B_Oct, -1);
                p.set (F_Type, 1); p.set (F_Cutoff, 0.3f); p.set (F_Res, 0.45f); p.set (F_Drive, 0.5f); p.set (G_Mono, 1);
                lfo (p, 0, 0, syncRate (6), true, true); mod (p, 0, S_Lfo1, D_Cutoff, 0.6f); mod (p, 1, S_Lfo1, D_BPos, 0.3f); } },
            { "Voix", "Lead", [] (Patch& p, juce::String& a, juce::String&) {
                a = "builtin:4"; p.set (A_Uni, 3); p.set (A_Detune, 0.18f); p.set (F_Type, 0); p.set (F_Cutoff, 0.75f); p.set (G_Glide, 0.08f);
                lfo (p, 0, 1, 0.3f, false, false); mod (p, 0, S_Lfo1, D_APos, 0.6f); lfo (p, 1, 0, 0.45f, false, true); mod (p, 1, S_Lfo2, D_APitch, 0.008f); } },
            { "Arp pluck", "Pluck", [] (Patch& p, juce::String& a, juce::String&) {
                a = "builtin:1"; p.set (A_Pos, 0.5f); p.set (F_Type, 1); p.set (F_Cutoff, 0.28f); p.set (F_Res, 0.4f);
                env (p, 0, 0.001f, 0.6f, 0.0f, 0.3f); env (p, 1, 0.001f, 0.3f, 0.0f, 0.2f); mod (p, 0, S_Env2, D_Cutoff, 0.7f); mod (p, 1, S_Random, D_APos, 0.25f); } },
            { "Cuivres", "Lead", [] (Patch& p, juce::String& a, juce::String&) {
                a = "builtin:0"; p.set (A_Pos, 0.66f); p.set (A_Uni, 4); p.set (A_Detune, 0.2f); p.set (F_Type, 1); p.set (F_Cutoff, 0.4f);
                env (p, 0, 0.04f, 0.6f, 0.8f, 0.3f); env (p, 1, 0.12f, 0.5f, 0.4f, 0.3f); mod (p, 0, S_Env2, D_Cutoff, 0.45f); } },
            { "Atmosphère", "Pad", [] (Patch& p, juce::String& a, juce::String&) {
                a = "builtin:3"; p.set (A_Pos, 0.3f); p.set (A_Level, 0.5f); p.set (A_Uni, 5); p.set (A_Detune, 0.45f); p.set (Noise_On, 1); p.set (Noise_Level, 0.35f);
                p.set (F_Type, 6); p.set (F_Cutoff, 0.45f); p.set (F_Res, 0.7f); env (p, 0, 1.6f, 1.0f, 1.0f, 3.0f);
                lfo (p, 0, 6, 0.15f, false, false); mod (p, 0, S_Lfo1, D_Cutoff, 0.25f); mod (p, 1, S_Lfo1, D_APos, 0.4f); } },
            { "Hoover", "Lead", [] (Patch& p, juce::String& a, juce::String&) {
                a = "builtin:1"; p.set (A_Pos, 0.7f); p.set (A_Uni, 6); p.set (A_Detune, 0.6f); p.set (Sub_On, 1); p.set (F_Type, 1); p.set (F_Cutoff, 0.7f);
                p.set (G_Mono, 2); p.set (G_Glide, 0.2f); env (p, 1, 0.001f, 0.25f, 0.0f, 0.2f); mod (p, 0, S_Env2, D_Pitch, -0.08f); } },
            { "Keys lo-fi", "Clavier", [] (Patch& p, juce::String& a, juce::String&) {
                a = "builtin:0"; p.set (A_Pos, 0.25f); p.set (Noise_On, 1); p.set (Noise_Type, 2); p.set (Noise_Level, 0.3f); p.set (F_Type, 1); p.set (F_Cutoff, 0.5f);
                env (p, 0, 0.004f, 1.1f, 0.3f, 0.5f); lfo (p, 0, 0, 0.3f, false, false); mod (p, 0, S_Lfo1, D_Pitch, 0.003f); } },
            // tables de Serum (lues sur ton PC ; si Serum n'est pas là, une table de base les remplace)
            { "Acid 303", "Basse", [] (Patch& p, juce::String& a, juce::String&) {
                a = "serum:Analog/Acid.wav"; p.set (A_Pos, 0.2f); p.set (G_Mono, 2); p.set (G_Glide, 0.1f); p.set (F_Type, 1); p.set (F_Cutoff, 0.3f); p.set (F_Res, 0.7f); p.set (F_Drive, 0.4f);
                env (p, 1, 0.001f, 0.3f, 0.0f, 0.2f); mod (p, 0, S_Env2, D_Cutoff, 0.6f); mod (p, 1, S_Vel, D_Cutoff, 0.3f); } },
            { "Choeur", "Pad", [] (Patch& p, juce::String& a, juce::String&) {
                a = "serum:Vowel/DudaChoir.wav"; p.set (A_Uni, 4); p.set (A_Detune, 0.22f); p.set (A_Width, 1.0f); env (p, 0, 0.5f, 1.0f, 0.9f, 1.8f);
                lfo (p, 0, 0, 0.12f, false, false); mod (p, 0, S_Lfo1, D_APos, 0.5f); } },
            { "Évolution", "Pad", [] (Patch& p, juce::String& a, juce::String&) {
                a = "serum:Digital/Evolution.wav"; p.set (A_Uni, 3); p.set (A_Detune, 0.2f); env (p, 0, 0.6f, 1.0f, 0.85f, 2.0f);
                env (p, 2, 2.5f, 1.0f, 1.0f, 2.0f); mod (p, 0, S_Env3, D_APos, 0.9f); } },
            { "Monstre", "Basse", [] (Patch& p, juce::String& a, juce::String&) {
                a = "serum:Spectral/Monster 3 [SL].wav"; p.set (A_Oct, -1); p.set (G_Mono, 1); p.set (F_Type, 1); p.set (F_Cutoff, 0.65f); p.set (F_Drive, 0.6f);
                lfo (p, 0, 3, syncRate (5), true, true); mod (p, 0, S_Lfo1, D_APos, 0.7f); } },
            { "Wah", "Lead", [] (Patch& p, juce::String& a, juce::String&) {
                a = "serum:Vowel/WahWah.wav"; p.set (A_Uni, 2); p.set (A_Detune, 0.12f); lfo (p, 0, 1, syncRate (6), true, false); mod (p, 0, S_Lfo1, D_APos, 0.8f); } },
            { "Mario", "Lead", [] (Patch& p, juce::String& a, juce::String&) {
                a = "serum:Digital/Mario.wav"; p.set (A_Pos, 0.3f); p.set (F_On, 0); env (p, 0, 0.001f, 0.3f, 0.6f, 0.1f); } },
        };
        return f;
    }

    inline juce::String resolveTable (const juce::String& t)
    {
        if (! t.startsWith ("serum:")) return t;
        const auto rel = t.fromFirstOccurrenceOf ("serum:", false, false);
        const auto docs = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
        for (auto base : { docs.getChildFile ("Xfer/Serum Presets/Tables"), docs.getChildFile ("Xfer/Serum 2 Presets/Tables") })
            if (base.getChildFile (rel).existsAsFile()) return base.getChildFile (rel).getFullPathName();
        return "builtin:0";
    }

    // applique un preset d'usine au moteur
    inline void applyFactory (wt::Engine& e, int idx)
    {
        const auto& f = factory()[(size_t) juce::jlimit (0, (int) factory().size() - 1, idx)];
        e.patch.reset();
        juce::String a = "builtin:0", b = "builtin:0";
        f.make (e.patch, a, b);
        if (auto t = wt::tableFromPath (resolveTable (a))) e.setTable (0, t);
        if (auto t = wt::tableFromPath (resolveTable (b))) e.setTable (1, t);
    }

    inline bool save (const wt::Engine& e, const juce::File& file, const juce::String& category)
    {
        auto t = wt::toTree (e);
        t.removeProperty ("on", nullptr); t.removeProperty ("mix", nullptr);
        t.setProperty ("category", category, nullptr);
        file.getParentDirectory().createDirectory();
        if (auto x = t.createXml()) return x->writeTo (file);
        return false;
    }
    inline bool load (wt::Engine& e, const juce::File& file)
    {
        if (auto x = juce::XmlDocument::parse (file)) { wt::fromTree (e, juce::ValueTree::fromXml (*x), false); return true; }
        return false;
    }

    inline juce::Array<juce::File> list()
    {
        auto a = folder().findChildFiles (juce::File::findFiles, false, "*.vsynth");
        a.sort();
        return a;
    }

    // écrit les presets d'usine dans la banque (une seule fois par version)
    inline void ensureFactory()
    {
        if (true) return;   // presets de synthé plus écrits dans la bibliothèque des gens (« Presets de synthé ») : demandé par le créateur
        constexpr int version = 2;   // v2 : plucks plus longs (avant : presque des clics)
        auto dir = folder();
        if (library::settings().getIntValue ("synthPresetVersion", 0) >= version && ! list().isEmpty()) return;
        for (int i = 0; i < (int) factory().size(); ++i)
        {
            wt::Engine tmp;
            applyFactory (tmp, i);
            const auto file = dir.getChildFile (juce::File::createLegalFileName (juce::String::fromUTF8 (factory()[(size_t) i].category) + " - " + juce::String::fromUTF8 (factory()[(size_t) i].name)) + ".vsynth");
            save (tmp, file, juce::String::fromUTF8 (factory()[(size_t) i].category));
        }
        library::settings().setValue ("synthPresetVersion", version);
        library::settings().saveIfNeeded();
    }
}
