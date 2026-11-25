#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SoundFont.h"
#include "Presets.h"
#include "Destroy.h"

namespace
{
    juce::String msText (float v, int)
    {
        if (v < 1000.0f) return juce::String (juce::roundToInt (v)) + " ms";
        return juce::String (v / 1000.0f, 2) + " s";
    }

    juce::String signedText (float v, const char* unit, int decimals)
    {
        return (v > 0.0f ? "+" : "") + juce::String (v, decimals) + unit;
    }

    juce::String pctText (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }

    // nom lisible d'un réglage du synthé (liste d'automation de FL) : « Osc A position », « Filtre résonance »…
    juce::String synthParamName (int i)
    {
        const juce::String id (wt::info()[(size_t) i].id);
        const auto pre = id.upToFirstOccurrenceOf ("_", false, false);
        const auto suf = id.fromFirstOccurrenceOf ("_", false, false);
        juce::String a;
        if (pre == "A" || pre == "B") a = "Osc " + pre;
        else if (pre == "Sub") a = "Sub";
        else if (pre == "Noise") a = "Bruit";
        else if (pre == "F") a = "Filtre";
        else if (pre.startsWith ("E")) a = "Env " + pre.substring (1);
        else if (pre.startsWith ("L")) a = "LFO " + pre.substring (1);
        else if (pre.startsWith ("Mod")) a = "Mod " + juce::String (pre.substring (3).getIntValue() + 1);
        else if (pre == "G") a = "Global";
        else if (pre.startsWith ("M")) return "Macro " + pre.substring (1);
        else a = pre;
        static const std::pair<const char*, const char*> words[] = {
            { "On", "on" }, { "Pos", "position" }, { "Oct", "octave" }, { "Semi", "demi-tons" }, { "Fine", "finesse" }, { "Uni", "unisson" },
            { "Detune", "detune" }, { "Blend", "blend" }, { "Width", "largeur" }, { "Level", "volume" }, { "Pan", "pan" }, { "Phase", "phase" },
            { "Rand", "phase au hasard" }, { "Warp", "warp" }, { "Shape", "forme" }, { "Type", "type" }, { "Cutoff", "cutoff" }, { "Res", "résonance" },
            { "Drive", "drive" }, { "Key", "suivi du clavier" }, { "Mix", "mix" }, { "A", "attaque" }, { "D", "déclin" }, { "S", "maintien" }, { "R", "relâche" },
            { "Rate", "vitesse" }, { "Sync", "synchro" }, { "Retrig", "redémarrage" }, { "Src", "source" }, { "Dst", "destination" }, { "Amt", "quantité" },
            { "Volume", "volume" }, { "Voices", "voix" }, { "Mono", "mono" }, { "Glide", "glide" }, { "Bend", "plage du pitch bend" }, { "Vel", "vélocité" } };
        for (auto& w : words) if (suf == w.first) return a + " " + juce::String::fromUTF8 (w.second);
        return a + " " + suf;
    }

    juce::NormalisableRange<float> skewed (float lo, float hi, float centre)
    {
        juce::NormalisableRange<float> r (lo, hi, 0.1f);
        r.setSkewForCentre (centre);
        return r;
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout VariantProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    // réglages qui en changent d'autres (accordage lié des couches, bouton Arpégiateur d'une couche) :
    // déclarés « méta » (Logic / auval l'exigent, ça ne change rien au son)
    struct MetaFloat : AudioParameterFloat { using AudioParameterFloat::AudioParameterFloat; bool isMetaParameter() const override { return true; } };
    struct MetaBool : AudioParameterBool { using AudioParameterBool::AudioParameterBool; bool isMetaParameter() const override { return true; } };
    auto isMeta = [] (const String& id) { return id.startsWith ("L") && (id.endsWith ("Tune") || id.endsWith ("Fine") || id.endsWith ("ArpChord")); };
    auto F = [&p, isMeta] (const String& id, const String& name, NormalisableRange<float> r, float def, std::function<String (float, int)> txt)
    {
        auto attr = AudioParameterFloatAttributes();
        if (txt) attr = attr.withStringFromValueFunction (txt);
        if (isMeta (id)) p.push_back (std::make_unique<MetaFloat> (ParameterID { id, 1 }, name, r, def, attr));
        else p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def, attr));
    };
    auto I = [&p] (const String& id, const String& name, int lo, int hi, int def)
    {
        p.push_back (std::make_unique<AudioParameterInt> (ParameterID { id, 1 }, name, lo, hi, def));
    };
    auto C = [&p] (const String& id, const String& name, StringArray choices, int def)
    {
        p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, choices, def));
    };
    auto B = [&p, isMeta] (const String& id, const String& name, bool def)
    {
        if (isMeta (id)) p.push_back (std::make_unique<MetaBool> (ParameterID { id, 1 }, name, def));
        else p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def));
    };
    auto noteText = AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return MidiMessage::getMidiNoteName (v, true, true, 5); })
        .withValueFromStringFunction ([] (const String& s0) -> int
        {
            // on peut taper la note : "C5", "f#4", "Db3" ou un numéro MIDI
            const auto s = s0.trim().toUpperCase();
            if (s.containsOnly ("0123456789")) return jlimit (0, 127, s.getIntValue());
            static const int pcs[] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
            if (s.isEmpty() || s[0] < (juce_wchar) 'A' || s[0] > (juce_wchar) 'G') return 60;
            int pc = pcs[s[0] - 'A'], i = 1;
            if (s.length() > 1 && (s[1] == '#' || s[1] == (juce_wchar) 0x266F)) { ++pc; ++i; }
            else if (s.length() > 1 && s[1] == 'B') { --pc; ++i; }
            const auto octStr = s.substring (i).trim();
            const int oct = octStr.isEmpty() ? 5 : octStr.getIntValue();
            return jlimit (0, 127, oct * 12 + pc);
        });

    // --- Son (global)
    F ("tune",     "Transpo",  NormalisableRange<float> (-24.0f, 24.0f, 1.0f), 0.0f, [] (float v, int) { return signedText (v, " st", 0); });
    F ("fine",     "Finesse",  NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f, [] (float v, int) { return signedText (v, " ct", 0); });
    F ("attack",   "Attaque",  skewed (0.0f, 2000.0f, 150.0f), 0.0f, msText);   // 0 par défaut : chaque son part avec son attaque d origine
    F ("decay",    fr ("Déclin"),   skewed (1.0f, 4000.0f, 400.0f), 400.0f, msText);
    F ("sustain",  "Maintien", NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f, pctText);
    F ("release",  fr ("Relâche"),  skewed (1.0f, 5000.0f, 400.0f), 80.0f, msText);
    F ("velocity", fr ("Vélocité"), NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.8f, pctText);
    F ("gain",     "Volume",   NormalisableRange<float> (-36.0f, 12.0f, 0.1f), 0.0f, [] (float v, int) { return signedText (v, " dB", 1); });
    B ("oneshot",  "Jouer en entier", false);
    B ("metroOn", fr ("Métronome"), false);
    C ("metroSound", fr ("Son du métronome"), { "Clic", "Bois", "Cloche", "Charleston", fr ("Bip 8 bits"), "Rimshot" }, 0);
    F ("metroVol", fr ("Volume du métronome"), NormalisableRange<float> (-30.0f, 6.0f, 0.1f), -8.0f, [] (float v, int) { return signedText (v, " dB", 1); });
    // enveloppe façon FL : délai, maintien et courbes (tension) de chaque segment
    F ("envDelay", fr ("Délai"), skewed (0.0f, 2000.0f, 150.0f), 0.0f, msText);
    F ("envHold",  fr ("Tenue"), skewed (0.0f, 2000.0f, 150.0f), 0.0f, msText);
    F ("envAtkTen", fr ("Courbe attaque"), NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f, pctText);
    F ("envDecTen", fr ("Courbe déclin"), NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f, pctText);
    F ("envRelTen", fr ("Courbe relâche"), NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f, pctText);

    // --- Slide / glide
    C ("glideMode",  "Glide", { fr ("Désactivé"), fr ("Legato (notes liées)"), "Toujours" }, 0);
    F ("glideTime",  fr ("Durée du slide"), NormalisableRange<float> (5.0f, 2000.0f, 1.0f, 0.4f), 120.0f, msText);
    F ("glideCurve", fr ("Courbe du slide"), NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.4f, pctText);
    F ("slideIn",    fr ("Slide d'entrée"), NormalisableRange<float> (-24.0f, 24.0f, 1.0f), 0.0f, [] (float v, int) { return signedText (v, " st", 0); });

    // --- Couches
    for (int l = 0; l < maxLayers; ++l)
    {
        const auto n = " " + String (l + 1);
        p.push_back (std::make_unique<AudioParameterInt> (ParameterID { rootId (l), 1 }, "Note racine" + n, 0, 127, 60, noteText));
        B (layerId ("L", l) + "On",  "Couche" + n, l == 0);
        F (layerId ("L", l) + "Vol", "Volume couche" + n, NormalisableRange<float> (-36.0f, 12.0f, 0.1f), 0.0f, [] (float v, int) { return signedText (v, " dB", 1); });
        F (layerId ("L", l) + "Pan", "Pan couche" + n, NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f,
           [] (float v, int) { const int x = roundToInt (v * 100); return x == 0 ? String ("C") : (x < 0 ? "G " + String (-x) : "D " + String (x)); });
        F (layerId ("L", l) + "Tune", "Transpo couche" + n, NormalisableRange<float> (-24.0f, 24.0f, 1.0f), 0.0f, [] (float v, int) { return signedText (v, " st", 0); });
        F (layerId ("L", l) + "Fine", "Finesse couche" + n, NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f, [] (float v, int) { return signedText (v, " ct", 0); });
        F (layerId ("L", l) + "Delay", fr ("Retard couche") + n, NormalisableRange<float> (0.0f, 60.0f, 0.1f), 0.0f, msText);
        B (layerId ("L", l) + "Loop", fr ("Boucle calée au tempo") + n, false);
        F (layerId ("L", l) + "LoopBpm", fr ("BPM de la boucle") + n, NormalisableRange<float> (0.0f, 240.0f, 0.5f), 0.0f,
           [] (float v, int) { return v < 1.0f ? juce::String ("Auto") : juce::String (v, 1) + " BPM"; });
        // points de boucle (comme dans FL) : le son tourne en boucle tant que la note est tenue
        B (layerId ("L", l) + "SLoop", fr ("Points de boucle") + n, false);
        B (layerId ("L", l) + "Stretch", fr ("Stretch au tempo") + n, false);
        B (layerId ("L", l) + "ArpChord", fr ("Arpégiateur") + n, false);
        // réglages "Sampler" façon FL (traitement du son de la couche)
        F (layerId ("L", l) + "SmpStart", fr ("Départ du son") + n, NormalisableRange<float> (0.0f, 0.95f, 0.00001f), 0.0f, pctText);   // pas très fin : « Couper le blanc » se place à la milliseconde
        F (layerId ("L", l) + "SmpLen", fr ("Longueur du son") + n, NormalisableRange<float> (0.02f, 1.0f, 0.001f), 1.0f, pctText);
        F (layerId ("L", l) + "FadeIn", fr ("Fondu d'entrée") + n, NormalisableRange<float> (0.0f, 500.0f, 0.1f, 0.4f), 0.0f, msText);
        F (layerId ("L", l) + "FadeOut", fr ("Fondu de sortie") + n, NormalisableRange<float> (0.0f, 3000.0f, 0.1f, 0.4f), 0.0f, msText);
        B (layerId ("L", l) + "PingPong", fr ("Boucle ping-pong") + n, false);
        B (layerId ("L", l) + "Reverse", fr ("Inverser le son") + n, false);
        B (layerId ("L", l) + "Normalize", fr ("Normaliser") + n, false);
        B (layerId ("L", l) + "RemoveDC", fr ("Retirer le décalage DC") + n, false);
        B (layerId ("L", l) + "Polarity", fr ("Inverser la polarité") + n, false);
        B (layerId ("L", l) + "SwapSt", fr ("Échanger gauche / droite") + n, false);
        F (layerId ("L", l) + "SLStart", fr ("Début de boucle") + n, NormalisableRange<float> (0.0f, 1.0f, 0.0001f), 0.35f, pctText);
        F (layerId ("L", l) + "SLEnd", fr ("Fin de boucle") + n, NormalisableRange<float> (0.0f, 1.0f, 0.0001f), 0.75f, pctText);
        F (layerId ("L", l) + "SLXf", fr ("Fondu de boucle") + n, NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.3f, pctText);
    }
    B ("linkTune",   fr ("Accordage lié"), true);
    C ("loopChord",  fr ("Boucles en accord"), { fr ("Note la plus grave"), fr ("Toutes les notes") }, 0);
    B ("cutMode",    "Cut (un accord coupe le précédent)", false);
    C ("layerMode",  fr ("Répartition des couches"), LayerRouter::modeNames(), 0);
    C ("layerOrder", "Ordre des couches", { fr ("Grave → aigu"), fr ("Aigu → grave") }, 0);
    F ("layerHuman", "Naturel entre couches", NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.35f, pctText);

    // --- Variations
    B ("varOn",     "Variations", true);
    C ("varStyle",  "Style de variation", { fr ("Réaliste"), "Texture", fr ("Expérimental") }, 0);
    F ("varAmount", fr ("Intensité"), NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.65f, pctText);
    I ("varCount",  "Variantes", 2, 16, 12);
    F ("velTone",   fr ("Vélocité → timbre"), NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.35f, pctText);

    // --- Accords
    B ("chordOn",   "Accords", false);
    C ("chordMode", "Mode accord", { "Accord fixe", "Selon la gamme" }, 1);
    C ("chordType", "Type d'accord", theory::chordTypeNames(), 0);
    C ("key",       fr ("Tonalité"), theory::keyNames(), 0);
    C ("scale",     "Gamme", theory::scaleNames(), theory::Minor);
    C ("chordSize", "Taille", { "Triade", "7e", "9e", "11e", "13e" }, 1);
    C ("voicing",   "Voicing", theory::voicingNames(), 0);
    I ("inversion", "Renversement", 0, 3, 0);
    F ("strum",     "Strum", NormalisableRange<float> (0.0f, 250.0f, 1.0f), 0.0f, msText);
    F ("humanize",  "Humanisation", NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f, pctText);
    C ("chordRegister", "Registre", { fr ("Comme jouée"), fr ("Enchaînement fluide"), "Grave", "Aigu" }, 0);
    F ("chordSpice", "Couleur jazzy", NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f, pctText);

    // --- Arpège
    B ("arpOn",     fr ("Arpège"), false);
    C ("arpMode",   "Mode", arp::modeNames(), 0);
    C ("arpRate",   "Vitesse", arp::rateNames(), arp::R1_16);
    I ("arpOct",    "Octaves", 1, 4, 1);
    F ("arpGate",   fr ("Durée"), NormalisableRange<float> (0.05f, 1.5f, 0.01f), 0.6f, pctText);
    F ("arpSwing",  "Swing", NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f, pctText);
    I ("arpRepeat", fr ("Répétition"), 1, 4, 1);
    B ("arpLatch",  "Maintien (latch)", false);
    B ("arpSnap",   fr ("Contraindre à la gamme"), false);
    C ("arpSource", "Source", { fr ("Notes jouées"), "Gamme", fr ("Tranches du slicer") }, 0);
    C ("arpVel",    fr ("Vélocité arpège"), { fr ("Comme jouée"), "Fixe" }, 0);
    I ("arpSteps",  "Pas", 1, 16, 16);
    C ("arpOctDir", "Direction des octaves", arp::octDirNames(), arp::OctAuto);
    F ("arpHuman",  fr ("Humanisation arpège"), NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.35f, pctText);
    F ("arpFree",   fr ("Liberté"), NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f, pctText);   // 0 par défaut : pas de notes hors gamme
    // options en plus (menu « Plus d'options » sous l'arpège) : par défaut elles ne changent rien
    F ("arpProb",    fr ("Probabilité"), NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f, pctText);
    F ("arpRatchet", "Ratchet", NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f, pctText);
    I ("arpRatchetN", "Coups du ratchet", 2, 4, 2);
    F ("arpTranspose", "Transpo arpège", NormalisableRange<float> (-24.0f, 24.0f, 1.0f), 0.0f, [] (float v, int) { return signedText (v, " st", 0); });
    I ("arpRotate",  fr ("Décalage du motif"), 0, 15, 0);
    F ("arpAccent",  "Accent des temps", NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f, pctText);
    F ("arpVelRamp", fr ("Rampe de vélocité"), NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f, pctText);
    B ("arpRestart", fr ("Repartir du début à chaque note"), false);
    B ("arpBass",    fr ("Basse sur le 1er pas"), false);
    for (int i = 1; i <= 16; ++i)
        F ("step" + String (i), "Pas " + String (i), NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f, nullptr);

    // --- Slicer
    B ("slicerOn",  "Slicer", false);
    C ("sliceMode", "Découpe", { fr ("Transitoires"), fr ("Grille"), fr ("Au tempo") }, 0);
    I ("sliceCount", "Tranches", 2, 64, 16);
    B ("sliceFine", fr ("Découpe fine"), false);
    C ("sliceBase", fr ("Note du 1er chop"), { fr ("C0 (note la plus basse)"), "C3", "C5" }, 0);
    F ("sliceCents", fr ("Finesse des tranches"), NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f, [] (float v, int) { return signedText (v, " ct", 0); });
    B ("sliceStretch", fr ("Stretch des tranches"), false);
    F ("sliceSens", fr ("Sensibilité"), NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.55f, pctText);
    F ("sliceTune", "Transpo slicer", NormalisableRange<float> (-24.0f, 24.0f, 1.0f), 0.0f, [] (float v, int) { return signedText (v, " st", 0); });
    B ("sliceGate", fr ("Couper au relâchement"), true);
    B ("sliceChoke", fr ("Une tranche à la fois"), true);
    B ("sliceSync", fr ("Caler sur le tempo de FL"), false);
    // son des chops (valeurs par défaut = le son d'origine du Sampler)
    F ("sliceAtk", fr ("Attaque des chops"), skewed (0.5f, 500.0f, 30.0f), 1.5f, msText);
    F ("sliceRel", fr ("Relâche des chops"), skewed (5.0f, 3000.0f, 150.0f), 15.0f, msText);
    F ("sliceVel", fr ("Vélocité des chops"), NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.75f, pctText);
    F ("sliceTone", fr ("Couleur des chops"), NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f,
       [] (float v, int) { const int p = roundToInt (std::abs (v) * 100.0f); return p == 0 ? fr ("Neutre") : (v < 0.0f ? fr ("Sombre ") : fr ("Fin ")) + String (p) + " %"; });
    B ("sliceLevel", fr ("Volumes égalisés"), false);

    // --- Guide MIDI
    C ("genRegister", "Registre (guide)", gen::registerNames(), 0);
    F ("genSpice",    "Hors gamme (guide)", NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f, pctText);   // 0 = tout dans la gamme
    F ("genVariety",  fr ("Variété (guide)"), NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f, pctText);

    // --- Effets (slots)
    for (int s = 0; s < fxs::numSlots; ++s)
    {
        const auto n = " " + String (s + 1);
        C (slotId (s, "Type"), "Effet" + n, fxs::typeNames(), 0);
        B (slotId (s, "On"), "Effet actif" + n, true);
        for (int k = 0; k < fxs::numP; ++k)
        {
            String pname = "Effet" + n + " param " + String (k + 1);
            std::function<String (float, int)> ptxt;
           #ifdef VARIANT_SINGLE_FX
            // effet seul : les vrais noms et valeurs dans la liste d'automation de FL (« Durée », « 2.4 s »…)
            const auto& sd = fxs::def (VARIANT_SINGLE_FX);
            if (s == 0 && sd.params[(size_t) k].name != nullptr && String (sd.params[(size_t) k].name).isNotEmpty())
            {
                pname = fr (sd.params[(size_t) k].name);
                if (auto fn = sd.params[(size_t) k].text) ptxt = [fn] (float v, int) { return fn (v); };
            }
           #endif
            F (slotId (s, "P" + String (k + 1)), pname, NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f, ptxt);
        }
        F (slotId (s, "Mix"), "Effet" + n + " mix", NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f, pctText);
    }
    F ("spcWidth", fr ("Largeur stéréo"), NormalisableRange<float> (0.0f, 2.0f, 0.01f), 1.0f, pctText);
    F ("spcOut",   "Sortie", NormalisableRange<float> (-24.0f, 6.0f, 0.1f), 0.0f, [] (float v, int) { return signedText (v, " dB", 1); });

   #if ! VARIANT_FX_ONLY
    // --- Synthé (page Sound design) et VST ouverts dans Variant : tout est automatisable dans FL
    B ("synOn",  fr ("Synthé : allumé"), false);
    F ("synMix", fr ("Synthé : volume dans Variant"), NormalisableRange<float> (0.0f, 1.5f, 0.0f), 0.8f, pctText);
    for (int i = 0; i < wt::NumParams; ++i)
    {
        const auto& in = wt::info()[(size_t) i];
        F ("syn_" + String (in.id), fr ("Synthé : ") + synthParamName (i), NormalisableRange<float> (in.lo, in.hi, 0.0f), in.def, nullptr);
    }
    for (int s = 0; s < numHosted; ++s)
    {
        const auto n = "VST " + String (s + 1) + " : ";
        const auto id = "h" + String (s + 1);
        B (id + "On", n + fr ("allumé"), true);
        F (id + "Vol", n + "volume", NormalisableRange<float> (-36.0f, 12.0f, 0.0f), Decibels::gainToDecibels (0.8f), [] (float v, int) { return signedText (v, " dB", 1); });
        F (id + "Pan", n + "pan", NormalisableRange<float> (-1.0f, 1.0f, 0.0f), 0.0f, nullptr);
        F (id + "Tr",  n + "transpo", NormalisableRange<float> (-24.0f, 24.0f, 1.0f), 0.0f, [] (float v, int) { return signedText (v, " st", 0); });
        F (id + "Fine", n + "finesse", NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f, [] (float v, int) { return signedText (v, " ct", 0); });
        F (id + "Dly", n + "retard", NormalisableRange<float> (0.0f, 500.0f, 1.0f), 0.0f, msText);
    }
   #endif

    return { p.begin(), p.end() };
}

//==============================================================================
static std::atomic<int> liveInstances { 0 };   // Variant ouverts dans FL (le dernier qui se ferme range les réglages)
// tous les Variant ouverts (pour partager la tonalité et la gamme entre eux)
static juce::CriticalSection& instanceLock() { static juce::CriticalSection l; return l; }
static juce::Array<VariantProcessor*>& liveProcessors() { static juce::Array<VariantProcessor*> a; return a; }

VariantProcessor::VariantProcessor()
#if VARIANT_FX_ONLY
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true).withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
#else
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
#endif
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    formatManager.registerBasicFormats();

    auto raw = [this] (const juce::String& id) { return apvts.getRawParameterValue (id); };
    fxChain.setStores (&fxStores);
   #ifdef VARIANT_SINGLE_FX
    // plugin d'un seul effet (Variant Reverb, Variant Delay…) : l'effet est chargé d'office dans la 1re place
    {
        auto* t = apvts.getParameter (slotId (0, "Type"));
        if (t != nullptr) t->setValueNotifyingHost (t->convertTo0to1 ((float) VARIANT_SINGLE_FX));
        const auto& d = fxs::def (VARIANT_SINGLE_FX);
        if (! d.presets.empty())
            for (int k = 0; k < fxs::numP; ++k)
                if (auto* q = apvts.getParameter (slotId (0, "P" + juce::String (k + 1)))) q->setValueNotifyingHost (q->convertTo0to1 (d.presets.front().p[(size_t) k]));
    }
   #endif
    voiceParams.tune = raw ("tune");       voiceParams.fine = raw ("fine");
    voiceParams.attack = raw ("attack");   voiceParams.decay = raw ("decay");
    voiceParams.sustain = raw ("sustain"); voiceParams.release = raw ("release");
    voiceParams.envDelay = raw ("envDelay"); voiceParams.envHold = raw ("envHold");
    voiceParams.atkTen = raw ("envAtkTen"); voiceParams.decTen = raw ("envDecTen"); voiceParams.relTen = raw ("envRelTen");
    voiceParams.velocity = raw ("velocity"); voiceParams.gain = raw ("gain");
    voiceParams.oneshot = raw ("oneshot"); voiceParams.varOn = raw ("varOn");
    voiceParams.velTone = raw ("velTone"); voiceParams.human = raw ("layerHuman");
    voiceParams.glideMode = raw ("glideMode"); voiceParams.glideTime = raw ("glideTime");
    voiceParams.glideCurve = raw ("glideCurve"); voiceParams.slideIn = raw ("slideIn");
    synth.glideMode = voiceParams.glideMode;
    synth.glideTime = voiceParams.glideTime;
    synth.hostBpm = &hostBpmA;
    voiceParams.loopChord = raw ("loopChord");
    voiceParams.hostPpq = &hostPpqA; voiceParams.hostBpm = &hostBpmA; voiceParams.hostPlaying = &hostPlayingA;
    voiceParams.stretchCache = &stretchCache;
    stretchCache.getSource = [this] (int l, const juce::AudioBuffer<float>*& src, double& rate, juce::uint32& id) -> std::shared_ptr<void>
    {
        auto s = getSound();
        if (s != nullptr && juce::isPositiveAndBelow (l, maxLayers) && s->layers[(size_t) l].valid)
        { src = &s->layers[(size_t) l].source; rate = s->layers[(size_t) l].rate; id = s->layers[(size_t) l].uid; }
        return std::shared_ptr<void> (new LayerSound::Ptr (s), [] (void* p) { delete static_cast<LayerSound::Ptr*> (p); });
    };
    stretchCache.start();
    for (int l = 0; l < maxLayers; ++l)
    {
        auto& lp = voiceParams.layers[(size_t) l];
        lp.on = raw (layerId ("L", l) + "On");     lp.vol = raw (layerId ("L", l) + "Vol");
        lp.pan = raw (layerId ("L", l) + "Pan");   lp.tune = raw (layerId ("L", l) + "Tune");
        lp.fine = raw (layerId ("L", l) + "Fine"); lp.delay = raw (layerId ("L", l) + "Delay");
        lp.root = raw (rootId (l));
        lp.loop = raw (layerId ("L", l) + "Loop"); lp.loopBpm = raw (layerId ("L", l) + "LoopBpm");
        lp.autoBpm = &autoLoopBpm[(size_t) l];
        lp.nameBpm = &nameLoopBpm[(size_t) l];
        lp.stretch = raw (layerId ("L", l) + "Stretch");
        lp.arpChord = raw (layerId ("L", l) + "ArpChord");
        lp.pingPong = raw (layerId ("L", l) + "PingPong");
        lp.susLoop = raw (layerId ("L", l) + "SLoop"); lp.slStart = raw (layerId ("L", l) + "SLStart");
        lp.slEnd = raw (layerId ("L", l) + "SLEnd");   lp.slXf = raw (layerId ("L", l) + "SLXf");
    }

    for (int s = 0; s < fxs::numSlots; ++s)
    {
        auto& sp = slotParams[(size_t) s];
        sp.type = raw (slotId (s, "Type"));
        sp.on = raw (slotId (s, "On"));
        sp.mix = raw (slotId (s, "Mix"));
        for (int k = 0; k < fxs::numP; ++k) sp.p[(size_t) k] = raw (slotId (s, "P" + juce::String (k + 1)));
    }

    for (auto& v : voicePositions) for (auto& x : v) x.store (-1.0f);
    for (int i = 0; i < numVoices; ++i)
        synth.addVoice (new LayerVoice (voiceParams, roundRobins, synth, voicePositions[(size_t) i]));
    synth.setNoteStealingEnabled (true);

    sliceParams.tune = raw ("sliceTune");
    sliceParams.gain = raw ("gain");
    sliceParams.gate = raw ("sliceGate");
    sliceParams.speed = &sliceSpeed;
    sliceParams.cents = raw ("sliceCents");
    sliceParams.stretch = raw ("sliceStretch");
    sliceParams.timeRate = &sliceTimeRate;
    sliceParams.choke = raw ("sliceChoke");
    sliceParams.stretchVer = &sliceStretchVer;
    sliceParams.attack = raw ("sliceAtk");
    sliceParams.release = raw ("sliceRel");
    sliceParams.velSens = raw ("sliceVel");
    sliceParams.tone = raw ("sliceTone");
    sliceParams.levelOn = raw ("sliceLevel");
    sliceParams.chops = &chopSettings;
    sliceSynth.params = &sliceParams;
    for (int i = 0; i < 16; ++i) sliceSynth.addVoice (new slicer::SliceVoice (sliceParams, lastSlice, slicePlayPos));
    sliceSynth.setNoteStealingEnabled (true);

    for (auto* id : { "varStyle", "varAmount", "varCount", "sliceMode", "sliceCount", "sliceSens", "sliceFine" })
        apvts.addParameterListener (id, this);
    for (int l = 0; l < maxLayers; ++l)
    {
        apvts.addParameterListener (layerId ("L", l) + "Tune", this);
        apvts.addParameterListener (layerId ("L", l) + "Fine", this);
        apvts.addParameterListener (layerId ("L", l) + "ArpChord", this);
        for (auto* suf : { "SmpStart", "SmpLen", "FadeIn", "FadeOut", "Reverse", "Normalize", "RemoveDC", "Polarity", "SwapSt" })
            apvts.addParameterListener (layerId ("L", l) + suf, this);
    }
    refreshTuneMemory();
    for (int s = 0; s < fxs::numSlots; ++s) apvts.addParameterListener (slotId (s, "Type"), this);
    // tonalité et gamme partagées entre tous les Variant ouverts dans FL
    apvts.addParameterListener ("key", this);
    apvts.addParameterListener ("scale", this);
    { const juce::ScopedLock sl (instanceLock()); liveProcessors().add (this); }
    fxChain.ensure (slotParams);
   #if ! VARIANT_FX_ONLY
    // son par défaut : si rien n'a été chargé (pas de projet rouvert), on charge un son pour entendre quelque chose tout de suite
    {
        std::weak_ptr<int> alive = sliceAlive;
        juce::Timer::callAfterDelay (700, [this, alive] { if (! alive.expired()) loadDefaultSoundIfEmpty(); });
    }
   #endif

    variationSeed = (juce::uint32) juce::Random::getSystemRandom().nextInt();
    genSettings.seed = (juce::uint32) juce::Random::getSystemRandom().nextInt();
    startEmptyGuide();   // pas d'accords par défaut : la grille est vide jusqu'à « Générer »

    setupBridges();
    bridgesResync();
    bridgeTimer.startTimerHz (30);
    uiMidi.reset (44100.0);
    startTimer (250);
    if (++liveInstances == 1) telemetry::start();   // statistiques : un seul envoi pour tous les Variant ouverts
}

VariantProcessor::~VariantProcessor()
{
    bridgeTimer.stopTimer();
    { const juce::ScopedLock sl (instanceLock()); liveProcessors().removeFirstMatchingValue (this); }   // plus de partage de gamme avec lui
    stretchCache.stop();   // le thread de fond lit les sons du plugin (son calcul s'interrompt tout de suite)
    {
        // calculs en arrière-plan du Sampler : on les prévient (sliceAlive expire) et on attend qu'ils se terminent,
        // sinon leur code pouvait encore tourner quand FL décharge le plugin (FL figé ou plantage à la fermeture)
        sliceAlive.reset();
        const auto until = juce::Time::getMillisecondCounter() + 3000;
        while (bgRunning->load() > 0 && juce::Time::getMillisecondCounter() < until) juce::Thread::sleep (5);
    }
    pool.removeAllJobs (true, 3000);
    if (--liveInstances == 0)
    {
        // dernier Variant fermé : on referme proprement ce qui vivait en variable statique (sinon FL se fige quand il décharge le plugin)
        layermenu::chooser().reset();
        telemetry::stop();   // le fil des statistiques s'arrête (requête en cours annulée) avant que FL décharge Variant
        library::releaseSettings();
    }
    for (auto* id : { "varStyle", "varAmount", "varCount", "sliceMode", "sliceCount", "sliceSens", "sliceFine" })
        apvts.removeParameterListener (id, this);
    for (int l = 0; l < maxLayers; ++l)
    {
        apvts.removeParameterListener (layerId ("L", l) + "Tune", this);
        apvts.removeParameterListener (layerId ("L", l) + "Fine", this);
        apvts.removeParameterListener (layerId ("L", l) + "ArpChord", this);
        for (auto* suf : { "SmpStart", "SmpLen", "FadeIn", "FadeOut", "Reverse", "Normalize", "RemoveDC", "Polarity", "SwapSt" })
            apvts.removeParameterListener (layerId ("L", l) + suf, this);
    }
    for (int s = 0; s < fxs::numSlots; ++s) apvts.removeParameterListener (slotId (s, "Type"), this);
    apvts.removeParameterListener ("key", this);
    apvts.removeParameterListener ("scale", this);
    stopTimer();
    cancelPendingUpdate();
}

void VariantProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    hostFilterBuf.ensureSize (8192);
    synth.setCurrentPlaybackSampleRate (sampleRate);
    sliceSynth.setCurrentPlaybackSampleRate (sampleRate);
    wtSynth.prepare (sampleRate);
    wtMidi.ensureSize (4096);
    focusBuffer.ensureSize (4096);
    for (auto& h : hostedSlots) h.prepare (sampleRate, samplesPerBlock);
    renderExplosion (sfxBuffer, sampleRate);
    keyboardState.reset();
    chordEngine.prepare (sampleRate);
    arpeggiator.prepare (sampleRate);
    router.prepare (sampleRate);
    cutFilter.prepare (sampleRate);
    recEvents.reserve (20000);
    sliceRecEvents.reserve (20000);
    scheduler.prepare();
    uiMidi.reset (sampleRate);
    fxChain.prepare (sampleRate, juce::jmax (64, samplesPerBlock));
    fxChain.ensure (slotParams);

    const auto sz = (size_t) juce::jmax (4096, samplesPerBlock * 8);
    stageBuffer.ensureSize (sz);
    arpBuffer.ensureSize (sz);
    uiBuffer.ensureSize (sz);
    routeBuffer.ensureSize (sz);
    cutBuffer.ensureSize (sz);
    padBuffer.ensureSize (sz);
    restBuffer.ensureSize (sz);
}

bool VariantProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
   #if VARIANT_FX_ONLY
    if (layouts.getMainInputChannelSet() != out) return false;
   #endif
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

//==============================================================================
int VariantProcessor::getKey() const   { return (int) pv ("key"); }
int VariantProcessor::getScale() const { return (int) pv ("scale"); }

ChordEngine::Settings VariantProcessor::getChordSettings() const
{
    ChordEngine::Settings s;
    s.mode      = (int) pv ("chordMode");
    s.type      = (int) pv ("chordType");
    s.key       = getKey();
    s.scaleIdx  = getScale();
    s.size      = (int) pv ("chordSize") + 3;
    s.voicing   = (int) pv ("voicing");
    s.inversion = (int) pv ("inversion");
    s.strumMs   = pv ("strum");
    s.humanize  = pv ("humanize");
    s.reg       = (int) pv ("chordRegister");
    s.spice     = pv ("chordSpice");
    return s;
}

arp::Settings VariantProcessor::getArpSettings() const
{
    arp::Settings s;
    s.mode     = (int) pv ("arpMode");
    s.rate     = (int) pv ("arpRate");
    s.octaves  = (int) pv ("arpOct");
    s.repeat   = (int) pv ("arpRepeat");
    s.numSteps = (int) pv ("arpSteps");
    s.source   = (int) pv ("arpSource");
    s.velMode  = (int) pv ("arpVel");
    s.gate     = pv ("arpGate");
    s.swing    = pv ("arpSwing");
    s.latch    = pv ("arpLatch") > 0.5f;
    s.snap     = pv ("arpSnap") > 0.5f;
    s.octDir   = (int) pv ("arpOctDir");
    s.humanize = pv ("arpHuman");
    s.freedom  = pv ("arpFree");
    s.prob      = pv ("arpProb");
    s.ratchet   = pv ("arpRatchet");
    s.ratchetN  = (int) pv ("arpRatchetN");
    s.transpose = juce::roundToInt (pv ("arpTranspose"));
    s.rotate    = (int) pv ("arpRotate");
    s.accent    = pv ("arpAccent");
    s.velRamp   = pv ("arpVelRamp");
    s.restart   = pv ("arpRestart") > 0.5f;
    s.bass      = pv ("arpBass") > 0.5f;
    s.sliceCount = juce::jmax (1, sliceCountNow.load());
    if (s.source == 2) s.snap = false;   // les tranches ne sont pas des hauteurs
    s.key      = getKey();
    s.scaleIdx = getScale();
    static const char* ids[] = { "step1","step2","step3","step4","step5","step6","step7","step8",
                                 "step9","step10","step11","step12","step13","step14","step15","step16" };
    for (int i = 0; i < 16; ++i) s.steps[(size_t) i] = pv (ids[i]);
    return s;
}

void VariantProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
   #if ! VARIANT_FX_ONLY
    buffer.clear();
   #endif
    // pas encore de @ Instagram entré (1er lancement), ou Variant désactivé par le créateur pour ce @ : silence
    if (! telemetry::canPlay()) { buffer.clear(); midi.clear(); return; }
    bridgesFromHost();   // automation de FL -> synthé, VST ouverts dans Variant

    HostTime ht;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm()) ht.bpm = *bpm;
            ht.playing = pos->getIsPlaying();
            if (auto ppq = pos->getPpqPosition()) { ht.hasPpq = true; ht.ppq = *ppq; }
            if (auto ts = pos->getTimeSignature()) ht.num = juce::jlimit (1, 16, ts->numerator);
        }
    for (const auto meta : midi)   // notes tenues (pour Chroma en mode « Notes jouées »)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn()) heldIn[(size_t) m.getNoteNumber()] = true;
        else if (m.isNoteOff()) heldIn[(size_t) m.getNoteNumber()] = false;
        else if (m.isAllNotesOff() || m.isAllSoundOff()) heldIn.fill (false);
    }
    hostBpm.store (ht.bpm);
    hostPpqA.store (ht.hasPpq ? ht.ppq : 0.0); hostBpmA.store (ht.bpm); hostPlayingA.store (ht.playing && ht.hasPpq);
   #if VARIANT_FX_ONLY
    {
        // Variant FX (mixer de FL) : seulement la chaîne d'effets, sur le son qui entre
        for (int ch = getTotalNumInputChannels(); ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, n);
        if (buffer.getNumChannels() >= 2 && getTotalNumInputChannels() == 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);
        int m = 0;
        const auto& sc = theory::scaleAt (getScale());
        for (int k = 0; k < 12; ++k) if (theory::inScale (k, 0, sc)) m |= 1 << k;
        fxs::host().key = getKey(); fxs::host().scaleMask = m;
        int held = 0;
        for (int k = 0; k < 128; ++k) if (heldIn[(size_t) k]) held |= 1 << (((k - getKey()) % 12 + 12) % 12);
        if (held != 0) fxs::host().heldMask = held;
        fxChain.process (buffer, slotParams, ht.bpm, ht.ppq, ht.playing && ht.hasPpq, pv ("spcWidth"), juce::Decibels::decibelsToGain (pv ("spcOut")));
        float e = 0;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch) e = juce::jmax (e, buffer.getRMSLevel (ch, 0, n));
        outLevel.store (outLevel.load() * 0.7f + e * 0.3f);
        midi.clear();
        return;
    }
   #endif

    // « Jouer avec FL » : quand FL joue et que le plugin a une progression, c'est elle qu'on entend (pas les notes du piano roll de FL)
    const bool guideWithHost = hostPlayGuide.load() && ht.playing && ht.hasPpq && hostGuideHasNotes.load() && ! preview.isPlaying();
    if (guideWithHost)
    {
        hostFilterBuf.clear();
        for (const auto meta : midi) if (! meta.getMessage().isNoteOnOrOff()) hostFilterBuf.addEvent (meta.getMessage(), meta.samplePosition);
        midi.swapWith (hostFilterBuf);
    }

    keyboardState.processNextMidiBuffer (midi, 0, n, true);
    { const int ts = takeState.load(); if ((ts == 1 || ts == 2) && takeSource.load() == 0) takeCaptureMidi (midi, blockStart, ht.bpm); }   // « Prise » : notes jouées

    // 1. accords
    const bool chordOn = pv ("chordOn") > 0.5f;
    if (chordOn)
        chordEngine.process (midi, stageBuffer, blockStart, getChordSettings(), scheduler);
    else if (chordWasOn)
    {
        chordEngine.releaseAll (midi, blockStart, scheduler);
        scheduler.clearNoteOns();
    }
    chordWasOn = chordOn;
    scheduler.popInto (midi, blockStart, n);

    // 2. arpège
    const bool arpOn = pv ("arpOn") > 0.5f;
    const double viewBps = juce::jmax (20.0, ht.bpm) / 60.0 / currentSampleRate;   // visualiseur : temps en noires
    if (arpOn)
    {
        const auto as = getArpSettings();
        arpBuffer.clear();
        arpViewPush (midi, viewBps, true);
        arpeggiator.process (midi, arpBuffer, n, blockStart, as, ht);
        midi.swapWith (arpBuffer);
        arpViewPush (midi, viewBps, false);
    }
    else if (arpWasOn)
    {
        arpeggiator.flushAll (midi);
        arpeggiator.reset();
    }
    if (! arpOn) arpViewPush (midi, viewBps, false);   // arpège éteint : on voit les notes jouées
    { const int ts = takeState.load(); if ((ts == 1 || ts == 2) && takeSource.load() == 1) takeCaptureMidi (midi, blockStart, ht.bpm); }   // « Prise » de la page Arpège : les notes de l'arpège
    arpViewClock += n * viewBps;
    arpViewNow.store (arpViewClock);
    arpWasOn = arpOn;

    // 3. boutons de l'interface + pré-écoute du guide
    uiBuffer.clear();
    uiMidi.removeNextBlockOfMessages (uiBuffer, n);
    midi.addEvents (uiBuffer, 0, n, 0);

    // enregistrement (démarre à la première note jouée)
    if (recArmed.load() && recLock.tryEnter())
    {
        const double beatsPerSample = ht.bpm / 60.0 / currentSampleRate;
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (! m.isNoteOnOrOff()) continue;
            const auto t = blockStart + meta.samplePosition;
            if (! recStarted.load() && m.isNoteOn()) { recStarted.store (true); recStartSample = t; }
            if (recStarted.load() && recEvents.size() < recEvents.capacity())
                recEvents.push_back ({ (double) (t - recStartSample) * beatsPerSample, m.getNoteNumber(), m.getFloatVelocity(), m.isNoteOn() });
        }
        if (recStarted.load()) recBeats.store ((double) (blockStart + n - recStartSample) * beatsPerSample);
        recLock.exit();
    }

    preview.render (midi, n, ht.bpm, currentSampleRate);
    if (guideWithHost) hostGuide.renderSynced (midi, n, ht.bpm, currentSampleRate, ht.ppq);
    else if (hostGuideWasActive) hostGuide.stopSynced (midi);
    hostGuideWasActive = guideWithHost;

    // 4. slicer : toutes les notes jouent les tranches (les couches finissent leurs queues)
    const bool slicerOn = pv ("slicerOn") > 0.5f;
    if (slicerOn != slicerWasOn)
    {
        if (slicerOn) synth.allNotesOff (0, true);
        else          sliceSynth.allNotesOff (0, true);
        slicerWasOn = slicerOn;
    }
    {
        // pads de l'interface (canal 15) : jouent toujours les tranches
        padBuffer.clear(); restBuffer.clear();
        for (const auto meta : midi)
            (meta.getMessage().getChannel() == 15 ? padBuffer : restBuffer).addEvent (meta.getMessage(), meta.samplePosition);
        midi.swapWith (restBuffer);
        if (slicerOn) { padBuffer.addEvents (midi, 0, n, 0); midi.clear(); }

        // calage sur le tempo de FL : on accélère / ralentit la lecture (comme un sampler classique)
        const double sb = sliceBpm.load();
        { static const int bases[] = { 0, 36, 60 }; slicer::baseRef().store (bases[juce::jlimit (0, 2, (int) pv ("sliceBase"))]); }
        const bool stretchOn = pv ("sliceStretch") > 0.5f;
        sliceSpeed.store (! stretchOn && pv ("sliceSync") > 0.5f && sb > 0.0 ? (float) juce::jlimit (0.5, 2.0, ht.bpm / sb) : 1.0f);
        sliceTimeRate.store (sb > 0.0 ? (float) juce::jlimit (0.25, 4.0, ht.bpm / sb) : 1.0f);

        // enregistrement des chops
        if (sliceRecArmed.load() && sliceRecLock.tryEnter())
        {
            const double beatsPerSample = ht.bpm / 60.0 / currentSampleRate;
            const int count = juce::jmax (1, sliceCountNow.load());
            for (const auto meta : padBuffer)
            {
                const auto m = meta.getMessage();
                if (! m.isNoteOnOrOff()) continue;
                const auto t = blockStart + meta.samplePosition;
                if (! sliceRecStarted.load() && m.isNoteOn()) { sliceRecStarted.store (true); sliceRecStart = t; }
                if (sliceRecStarted.load() && sliceRecEvents.size() < sliceRecEvents.capacity())
                    sliceRecEvents.push_back ({ (double) (t - sliceRecStart) * beatsPerSample, slicer::sliceForNote (m.getNoteNumber(), count),
                                                m.getFloatVelocity(), m.isNoteOn() });
            }
            if (sliceRecStarted.load()) sliceRecLen.store ((double) (blockStart + n - sliceRecStart) * beatsPerSample);
            sliceRecLock.exit();
        }
        sliceSynth.renderNextBlock (buffer, padBuffer, 0, n);
    }
    if (tapPlaying.load() && sliceSource.getNumSamples() > 0)
    {
        // « Taper mes chops » : le sample défile (on entend où on coupe)
        const auto& d = sliceSource;
        const int len = d.getNumSamples(), end = juce::jmin (len, sliceRegB < 0 ? len : sliceRegB);
        const double inc = sliceRate / currentSampleRate;
        double p = tapPos.load();
        const float* L = d.getReadPointer (0);
        const float* R = d.getReadPointer (d.getNumChannels() > 1 ? 1 : 0);
        for (int i = 0; i < n; ++i)
        {
            if (p >= end - 2) { tapPlaying.store (false); break; }
            const int k = (int) p; const float t = (float) (p - k);
            buffer.addSample (0, i, (L[k] + t * (L[k + 1] - L[k])) * 0.9f);
            if (buffer.getNumChannels() > 1) buffer.addSample (1, i, (R[k] + t * (R[k + 1] - R[k])) * 0.9f);
            p += inc;
        }
        tapPos.store (p);
        slicePlayPos.store ((float) (p / juce::jmax (1, len)));
    }

    wtMidi.clear();
    wtMidi.addEvents (midi, 0, n, 0);   // le synthé joue les mêmes notes que les couches
    // 5. répartition entre les couches
    if (! slicerOn)
    {
        std::array<bool, maxLayers> active {};
        for (int l = 0; l < maxLayers; ++l)
            active[(size_t) l] = layerValid[(size_t) l].load() && voiceParams.layers[(size_t) l].on->load() > 0.5f;
        const std::vector<int>* ctx = (arpOn && arpeggiator.isRunning()) ? &arpeggiator.getBase() : nullptr;
        router.process (midi, routeBuffer, blockStart, (int) pv ("layerMode"), pv ("layerOrder") > 0.5f, active, ctx);
    }
    if (! slicerOn && pv ("cutMode") > 0.5f) cutFilter.process (midi, cutBuffer, blockStart);

    const bool focus = synthFocus.load();
    if (focus)
    {
        // page Sound design devant : les couches et les VST ne reçoivent plus de nouvelles notes (les notes en cours s'arrêtent normalement)
        focusBuffer.clear();
        for (const auto meta : midi) if (! meta.getMessage().isNoteOn()) focusBuffer.addEvent (meta.getMessage(), meta.samplePosition);
        midi.swapWith (focusBuffer);
    }
    synth.renderNextBlock (buffer, midi, 0, n);
    wtSynth.render (buffer, wtMidi, n, ht.bpm, ht.hasPpq ? ht.ppq : 0.0, ht.playing && ht.hasPpq);
    if (focus)
    {
        focusBuffer.clear();
        for (const auto meta : wtMidi) if (! meta.getMessage().isNoteOn()) focusBuffer.addEvent (meta.getMessage(), meta.samplePosition);
        wtMidi.swapWith (focusBuffer);
    }
    {
        // plugins ouverts dans Variant : mêmes notes, passent dans les effets, et suivent les réglages du son
        // (transpo, finesse, volume, vélocité, enveloppe, glide / slide, variations)
        HostedInstrument::Follow fw;
        fw.tuneSemis = pv ("tune"); fw.fineCents = pv ("fine"); fw.gainDb = pv ("gain"); fw.velSens = pv ("velocity");
        fw.delayMs = pv ("envDelay"); fw.attackMs = pv ("attack"); fw.holdMs = pv ("envHold");
        fw.decayMs = pv ("decay"); fw.sustain = pv ("sustain"); fw.releaseMs = pv ("release");
        fw.releaseOn = std::abs (fw.releaseMs - HostedInstrument::defaultRelease) > 0.5f;
        fw.glideMode = (int) pv ("glideMode"); fw.glideMs = pv ("glideTime"); fw.glideCurve = pv ("glideCurve"); fw.slideIn = pv ("slideIn");
        fw.varOn = pv ("varOn") > 0.5f; fw.varStyle = (int) pv ("varStyle"); fw.varAmount = pv ("varAmount");
        for (auto& h : hostedSlots) h.process (buffer, wtMidi, n, getPlayHead(), fw);
    }
    filePreview.render (buffer, currentSampleRate);
    {
        int m = 0;
        const auto& sc = theory::scaleAt (getScale());
        for (int k = 0; k < 12; ++k) if (theory::inScale (k, 0, sc)) m |= 1 << k;
        fxs::host().key = getKey(); fxs::host().scaleMask = m;
        int held = 0;
        for (int k = 0; k < 128; ++k) if (heldIn[(size_t) k]) held |= 1 << (((k - getKey()) % 12 + 12) % 12);
        if (held != 0) fxs::host().heldMask = held;   // on garde le dernier accord joué quand on relâche
    }
    fxChain.process (buffer, slotParams, ht.bpm, ht.ppq, ht.playing && ht.hasPpq, pv ("spcWidth"), juce::Decibels::decibelsToGain (pv ("spcOut")));
    {
        const int lat = fxChain.latency (slotParams);   // Chroma en mode FFT : FL compense le retard
        if (lat != reportedLatency) { reportedLatency = lat; setLatencySamples (lat); }
    }
    {
        // limiteur de sécurité en sortie (accords + plusieurs couches = plus de saturation)
        const float thr = 0.89f;
        const float rel = std::exp (-1.0f / (0.12f * (float) currentSampleRate));
        for (int i = 0; i < n; ++i)
        {
            float peak = 0.0f;
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch) peak = juce::jmax (peak, std::abs (buffer.getSample (ch, i)));
            limEnv = peak > limEnv ? peak : limEnv * rel + peak * (1.0f - rel);
            const float target = limEnv > thr ? thr / limEnv : 1.0f;
            limGain += (target < limGain ? 0.6f : 0.0008f) * (target - limGain);
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            {
                float x = buffer.getSample (ch, i) * limGain;
                if (std::abs (x) > 0.95f) x = std::copysign (0.95f + 0.05f * std::tanh ((std::abs (x) - 0.95f) * 20.0f), x);   // écrêtage doux de secours
                buffer.setSample (ch, i, x);
            }
        }
    }
    {
        // métronome : seulement quand on cherche dans le plugin (écoute des accords / du motif, enregistrement),
        // jamais pendant la lecture de FL (FL a déjà le sien)
        const bool on = pv ("metroOn") > 0.5f;
        const bool previewing = preview.isPlaying();
        if (previewing && ! metroWasPreviewing) metroBeat = -1.0e-9;   // calé sur le début de l'écoute
        metroWasPreviewing = previewing;
        const bool hostRun = false;
        const bool internalRun = previewing || sliceRecArmed.load() || recArmed.load();
        const int snd = (int) pv ("metroSound");
        if (snd != metroSoundBuilt || metroRateBuilt != currentSampleRate) buildMetroClicks (snd);
        const double bps = juce::jmax (20.0, ht.bpm) / 60.0 / currentSampleRate;
        if (on && (hostRun || internalRun))
        {
            double beat = hostRun ? ht.ppq : metroBeat;
            if (previewing && preview.lengthBeats() > 0.0)   // calé sur ce qui joue, même si on l'allume en cours de route
            {
                const double L = preview.lengthBeats();
                beat = std::fmod (preview.positionBeats() - n * bps + 4.0 * L, L) - 1.0e-9;
            }
            const int beatsPerBar = juce::jmax (1, ht.num);
            for (int i = 0; i < n; ++i)
            {
                const double next = beat + bps;
                if (std::floor (next) != std::floor (beat) || (i == 0 && std::abs (beat - std::round (beat)) < 1.0e-9))
                {
                    const long long b = (long long) std::floor (next + 1.0e-9);
                    metroAccent = (b % beatsPerBar) == 0;
                    metroPos = 0;
                }
                beat = next;
                if (metroPos >= 0)
                {
                    const auto& clk = metroClicks[metroAccent ? 1 : 0];
                    if (metroPos < clk.getNumSamples())
                    {
                        const float v = clk.getSample (0, metroPos) * juce::Decibels::decibelsToGain (pv ("metroVol"));
                        for (int ch = 0; ch < buffer.getNumChannels(); ++ch) buffer.addSample (ch, i, v);
                        ++metroPos;
                    }
                    else metroPos = -1;
                }
            }
            metroBeat = internalRun ? beat : 0.0;
        }
        else { metroBeat = -1.0e-9; metroPos = -1; }
    }
    if (int sp = sfxPos.load(); sp >= 0 && sfxBuffer.getNumSamples() > 0)
    {
        const int num = juce::jmin (n, sfxBuffer.getNumSamples() - sp);
        if (num > 0)
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.addFrom (ch, 0, sfxBuffer, juce::jmin (ch, sfxBuffer.getNumChannels() - 1), sp, num);
        sfxPos.store (sp + n >= sfxBuffer.getNumSamples() ? -1 : sp + n);
    }
    {
        float e = 0;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch) e = juce::jmax (e, buffer.getRMSLevel (ch, 0, n));
        outLevel.store (outLevel.load() * 0.7f + e * 0.3f);
    }
    if (takeState.load() == 2) takeCaptureAudio (buffer, n, blockStart);   // « Prise » : le son final (effets compris)
    midi.clear();
    {
        // niveau des basses (< 120 Hz) pour faire vibrer l'interface
        const float c = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 120.0f / (float) currentSampleRate);
        float pk = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            float m = buffer.getSample (0, i);
            if (buffer.getNumChannels() > 1) m = 0.5f * (m + buffer.getSample (1, i));
            bassLp += c * (m - bassLp);
            pk = juce::jmax (pk, std::abs (bassLp));
        }
        bassLevel.store (juce::jmax (pk, bassLevel.load() * 0.85f));
    }
    blockStart += n;
}

void VariantProcessor::playUiNotes (const std::vector<int>& notes, bool on, float vel)
{
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    for (int nt : notes)
    {
        if (nt < 0 || nt > 127) continue;
        auto m = on ? juce::MidiMessage::noteOn (1, nt, vel) : juce::MidiMessage::noteOff (1, nt);
        m.setTimeStamp (now);
        uiMidi.addMessageToQueue (m);
    }
}

//==============================================================================
bool VariantProcessor::isSupportedFile (const juce::File& file)
{
    return file.hasFileExtension ("wav;aif;aiff;flac;ogg;mp3;sf2");
}

//==============================================================================
// Couches annulables : chaque chargement / retrait garde le son et les réglages d'avant (Ctrl+Z, clic droit > Annuler)
VariantProcessor::LayerSnapshot VariantProcessor::layerSnapshot (int layer) const
{
    LayerSnapshot s;
    s.file = getLayerFile (layer);
    s.sf2 = sf2Preset[(size_t) layer];
    for (auto suffix : { "On", "Vol", "Pan", "Tune", "Fine", "Delay", "Loop", "LoopBpm", "SLoop", "SLStart", "SLEnd", "SLXf", "Stretch", "ArpChord" })
        s.params.push_back ({ layerId ("L", layer) + suffix, getParam (layerId ("L", layer) + suffix) });
    s.params.push_back ({ rootId (layer), getParam (rootId (layer)) });
    return s;
}

void VariantProcessor::applyLayerSnapshot (int layer, const LayerSnapshot& s)
{
    if (! s.file.existsAsFile()) clearLayerImpl (layer);
    else if (s.file.hasFileExtension ("sf2")) loadSoundFontPresetImpl (s.file, s.sf2, layer);
    else loadSampleImpl (s.file, layer, false);
    const NoLink nl (noLink);
    for (auto& [id, v] : s.params) setParam (id, v);
}

void VariantProcessor::pushLayerStep (int layer, const LayerSnapshot& before)
{
    if (restoringState) return;
    const auto after = layerSnapshot (layer);
    pushHistory ([this, layer, before] { applyLayerSnapshot (layer, before); }, [this, layer, after] { applyLayerSnapshot (layer, after); });
}

// un preset d'arpège a mis ses effets ; si l'utilisateur change ensuite de son lui-même, on repart sans effets
void VariantProcessor::dropPresetFx()
{
    if (! presetFxLoaded || inPresetLoad || restoringState) return;
    presetFxLoaded = false;
    fxAction ([this] { for (int s = 0; s < fxs::numSlots; ++s) setParam (slotId (s, "Type"), 0.0f); });
}

bool VariantProcessor::loadSample (const juce::File& file, int layer, bool autoSetup)
{
    layer = juce::jlimit (0, maxLayers - 1, layer);
    const auto before = layerSnapshot (layer);
    const bool fresh = ! restoringState && ! inPresetLoad && isMainSound (layer) && file.existsAsFile() && isSupportedFile (file);
    if (fresh) resetSoundOptions (layer);   // nouveau son principal : slide, enveloppe, arpège… repartent des réglages de base
    if (fresh) presetName.clear();
    const bool ok = loadSampleImpl (file, layer, autoSetup);
    if (ok) pushLayerStep (layer, before);
    if (ok) dropPresetFx();
    if (ok && fresh) applyAutoFx (layer);   // les effets d avant partent, remplacés par ceux prévus pour ce son
    if (ok && fresh && layerCat[(size_t) layer] == (int) polish::Lead)
    {
        // lead : jeu solo / legato avec un petit slide (30 ms) — deux notes liées glissent l'une vers l'autre
        const NoLink nl (noLink);
        setParam ("glideMode", 1.0f);
        setParam ("glideTime", 30.0f);
    }
    return ok;
}

bool VariantProcessor::loadSoundFontPreset (const juce::File& sf2File, int presetIndex, int layer)
{
    layer = juce::jlimit (0, maxLayers - 1, layer);
    const auto before = layerSnapshot (layer);
    const bool fresh = ! restoringState && ! inPresetLoad && isMainSound (layer) && sf2File.existsAsFile();
    if (fresh) resetSoundOptions (layer);
    const bool ok = loadSoundFontPresetImpl (sf2File, presetIndex, layer);
    if (ok) pushLayerStep (layer, before);
    if (ok) dropPresetFx();
    if (ok && fresh) applyAutoFx (layer);
    return ok;
}

void VariantProcessor::clearLayer (int layer)
{
    layer = juce::jlimit (0, maxLayers - 1, layer);
    const auto before = layerSnapshot (layer);
    clearLayerImpl (layer);
    pushLayerStep (layer, before);
}

bool VariantProcessor::loadSoundFontPresetImpl (const juce::File& sf2File, int presetIndex, int layer)
{
    layer = juce::jlimit (0, maxLayers - 1, layer);
    sf2::File sf;
    if (! sf.open (sf2File)) return false;
    if (presetIndex < 0) presetIndex = sf.getPresets().front().index;
    auto all = sf.extractAll (presetIndex, 100);
    if (all.empty()) return false;

    // toutes les zones de l'instrument (comme le lecteur de SoundFont de FL)
    auto zones = std::make_shared<std::vector<KeyZone>>();
    int rep = 0, repDist = 1 << 20;
    float peak = 1.0e-6f;
    for (auto& ex : all) peak = juce::jmax (peak, ex.audio.getMagnitude (0, ex.audio.getNumSamples()));
    const float norm = juce::jmin (4.0f, 0.9f / peak);   // niveau homogène d'une SoundFont à l'autre
    for (size_t i = 0; i < all.size(); ++i)
    {
        auto& ex = all[i];
        KeyZone z;
        z.lo = ex.keyLo; z.hi = ex.keyHi; z.root = ex.rootNote; z.cents = -ex.cents;
        z.gain = juce::Decibels::decibelsToGain (juce::jlimit (-24.0f, 0.0f, ex.gainDb)) * norm;
        z.rate = ex.sampleRate;
        z.loopA = ex.looped ? ex.loopStart : -1; z.loopB = ex.looped ? ex.loopEnd : -1;
        z.audio = std::move (ex.audio);
        const int d = std::abs ((z.lo + z.hi) / 2 - 60);
        if (d < repDist) { repDist = d; rep = (int) i; }
        zones->push_back (std::move (z));
    }
    std::sort (zones->begin(), zones->end(), [] (const KeyZone& a, const KeyZone& b) { return a.lo < b.lo; });
    rep = 0; repDist = 1 << 20;
    for (int i = 0; i < (int) zones->size(); ++i) { const int d = std::abs (((*zones)[(size_t) i].lo + (*zones)[(size_t) i].hi) / 2 - 60); if (d < repDist) { repDist = d; rep = i; } }

    // la zone du milieu sert d'image (forme d'onde) et de son de base pour les variations
    const auto& rz = (*zones)[(size_t) rep];
    sourceBuffers[(size_t) layer].makeCopyOf (rz.audio);
    layerCat[(size_t) layer] = (int) polish::classify (sf2File, rz.audio, rz.rate);
    ++sourceGen[(size_t) layer];
    sourceBuffers[(size_t) layer].applyGain (rz.gain);
    sourceRates[(size_t) layer] = rz.rate;
    {
        const juce::SpinLock::ScopedLockType lock (soundLock);
        layerFiles[(size_t) layer] = sf2File;
    }
    layerZones[(size_t) layer] = zones;
    sf2Preset[(size_t) layer] = presetIndex;
    sf2Names[(size_t) layer].clear(); sf2Indices[(size_t) layer].clear();
    for (auto& p : sf.getPresets()) { sf2Names[(size_t) layer].add (juce::String (p.bank).paddedLeft ('0', 3) + ":" + juce::String (p.number).paddedLeft ('0', 3) + "  " + p.name); sf2Indices[(size_t) layer].push_back (p.index); }

    const NoLink nl (noLink);
    setParam (rootId (layer), (float) rz.root);
    setParam (layerId ("L", layer) + "Fine", 0.0f);
    setParam (layerId ("L", layer) + "Loop", 0.0f);
    setParam (layerId ("L", layer) + "SLoop", 0.0f);
    setParam (layerId ("L", layer) + "On", 1.0f);
    regenerateVariations (false);
    return true;
}

bool VariantProcessor::loadSampleImpl (const juce::File& file, int layer, bool autoSetup)
{
    loadingSample = true;
    struct Done { bool& f; ~Done() { f = false; } } done { loadingSample };
    return loadSampleImpl2 (library::resolveRenamed (file), layer, autoSetup);   // son renommé dans la bibliothèque : retrouvé
}

bool VariantProcessor::loadSampleImpl2 (const juce::File& file, int layer, bool autoSetup)
{
    layer = juce::jlimit (0, maxLayers - 1, layer);
    if (file.hasFileExtension ("sf2")) return loadSoundFontPresetImpl (file, -1, layer);   // chargé direct (le preset se change dans la couche)
    layerZones[(size_t) layer] = nullptr;
    sf2Preset[(size_t) layer] = -1;
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        return false;

    const double sr  = reader->sampleRate;
    const int    len = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) (sr * 60.0));
    const int    ch  = (int) juce::jlimit (1u, 2u, reader->numChannels);

    juce::AudioBuffer<float> data (ch, len);
    reader->read (&data, 0, len, 0, true, ch > 1);
    autoLevelDb[(size_t) layer] = 0.0f;
    if (library::settings().getBoolValue ("autoLevel", true))
    {
        // niveau auto : un son trop fort (crête au plafond ou très compressé) est baissé au chargement -> plus de saturation qui grésille
        const float peak = data.getMagnitude (0, len);
        float loud = 0.0f;   // RMS de la fenêtre de 50 ms la plus forte
        const int w = juce::jmax (64, (int) (sr * 0.05));
        for (int s = 0; s + w <= len; s += w / 2)
        {
            float sum = 0.0f;
            for (int c = 0; c < ch; ++c) { const float r = data.getRMSLevel (c, s, w); sum += r * r; }
            loud = juce::jmax (loud, std::sqrt (sum / (float) ch));
        }
        if (len < w) loud = data.getRMSLevel (0, 0, len);
        const float targetRms = juce::Decibels::decibelsToGain (-14.0f), targetPeak = juce::Decibels::decibelsToGain (-3.0f);
        float g = 1.0f;
        if (loud > targetRms) g = juce::jmin (g, targetRms / loud);
        if (peak > targetPeak) g = juce::jmin (g, targetPeak / peak);
        g = juce::jmax (g, juce::Decibels::decibelsToGain (-18.0f));
        if (g < 0.97f) { data.applyGain (g); autoLevelDb[(size_t) layer] = juce::Decibels::gainToDecibels (g); }
    }

    layerCat[(size_t) layer] = (int) polish::classify (file, data, sr);   // basse, lead, nappe… (pour Qualité+ et les effets auto)
    sourceBuffers[(size_t) layer] = std::move (data);
    ++sourceGen[(size_t) layer];
    sourceRates[(size_t) layer] = sr;
    autoLoopBpm[(size_t) layer].store (guessLoopBpm (file.getFileNameWithoutExtension(), (double) len / sr));
    nameLoopBpm[(size_t) layer].store (guessLoopBpm (file.getFileNameWithoutExtension(), 0.0));
    {
        // one-shot d'arpège (arp, seq, arpeggiator dans le nom) : calé sur le tempo de FL, même durée pour toutes les notes,
        // et en accord les pas passent d'une note à l'autre comme dans Hive. Une vraie boucle (loop, bpm) reste calée sur la mesure.
        const auto nm = file.getFileNameWithoutExtension().toLowerCase() + " " + file.getParentDirectory().getFileName().toLowerCase();
        juce::StringArray words; words.addTokens (nm.replaceCharacters ("_-.()[]", "       "), " ", "");
        bool arpLike = false;
        for (auto& w : words) if (w.startsWith ("arp") || w == "seq" || w.startsWith ("sequence") || w.startsWith ("seq0") || w.startsWith ("seq1")) arpLike = true;
        const bool loopLike = ! arpLike && (nm.contains ("loop") || nm.contains ("bpm"));
        detectedLoopLike = loopLike; detectedArpLike = arpLike;
        if (! restoringState)   // projet rouvert : on garde les réglages sauvés (ne pas les écraser avec ce que dit le nom)
        {
            setParam (layerId ("L", layer) + "Loop", loopLike ? 1.0f : 0.0f);
            setParam (layerId ("L", layer) + "Stretch", arpLike ? 1.0f : 0.0f);
            setParam (layerId ("L", layer) + "ArpChord", arpLike ? 1.0f : 0.0f);
        }
    }
    {
        const juce::SpinLock::ScopedLockType lock (soundLock);
        layerFiles[(size_t) layer] = file;
    }
    setParam (layerId ("L", layer) + "On", 1.0f);

    regenerateVariations (false);
    // (seuls les sons nommés « seq » / « arp… » ou rangés dans un dossier Seq passent en arpégiateur + stretch :
    //  plus de détection à l'oreille, un son normal reste toujours un son normal)
    if (autoSetup) autoSetupLayer (layer);
    if (pv ((layerId ("L", layer) + "Loop").toRawUTF8()) > 0.5f)
    {
        // boucle (arpège…) : on détecte sa tonalité et on la cale sur C, pour empiler les boucles sans fausses notes
        double secs = 0;
        const auto mono = analysis::fromBuffer (sourceBuffers[(size_t) layer], sourceRates[(size_t) layer], secs);
        const auto keys = analysis::detectKey (mono);
        if (! keys.empty())
        {
            int off = keys.front().key;            // 0 = C … 11 = B
            if (off > 6) off -= 12;                // transposition la plus courte
            const NoLink nl (noLink);
            setParam (rootId (layer), (float) (60 + off));
            setParam (layerId ("L", layer) + "Fine", 0.0f);
        }
    }
    return true;
}

int VariantProcessor::loadFiles (const juce::StringArray& files, int startLayer)
{
    int loaded = 0, layer = juce::jlimit (0, maxLayers - 1, startLayer);
    for (auto& path : files)
    {
        const juce::File f (path);
        if (! isSupportedFile (f)) continue;
        if (loaded > 0)   // les fichiers suivants vont dans les couches vides
        {
            int next = -1;
            for (int l = 0; l < maxLayers; ++l) if (getLayerFile (l) == juce::File() && l != layer) { next = l; break; }
            if (next < 0) break;
            layer = next;
        }
        if (loadSample (f, layer)) ++loaded;
    }
    if (loaded > 1) autoSetupAll();
    return loaded;
}

// « Couper le blanc » (bouton de la carte Sample) : le Départ saute le silence au début du fichier
// (mp3 qui ajoutent du vide, sons exportés avec un blanc), 1 ms avant l'attaque pour la garder entière.
bool VariantProcessor::trimLayerStart (int layer)
{
    layer = juce::jlimit (0, maxLayers - 1, layer);
    const auto& b = sourceBuffers[(size_t) layer];
    const int total = b.getNumSamples();
    if (total < 256) return false;
    float peak = 0.0f;
    for (int ch = 0; ch < b.getNumChannels(); ++ch) peak = juce::jmax (peak, b.getMagnitude (ch, 0, total));
    if (peak <= 1.0e-6f) return false;
    const float thr = juce::jmax (peak * 0.004f, 3.0e-5f);   // ≈ -48 dB sous le pic : le vrai début du son
    int first = -1;
    for (int i = 0; i < total && first < 0; ++i)
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            if (std::abs (b.getSample (ch, i)) > thr) { first = i; break; }
    const int preRoll = (int) (0.001 * sourceRates[(size_t) layer]);
    const int start = juce::jmax (0, first - preRoll);
    if (first < 0 || start < (int) (0.002 * sourceRates[(size_t) layer])) return false;   // moins de 2 ms de blanc : rien à couper
    setParam (layerId ("L", layer) + "SmpStart", juce::jmin (0.95f, (float) start / (float) total));
    return true;
}

// Réglages automatiques : note racine détectée, volumes équilibrés, légère ouverture stéréo
void VariantProcessor::autoSetupLayer (int layer)
{
    const NoLink nl (noLink);
    const auto& src = sourceBuffers[(size_t) layer];
    if (src.getNumSamples() < 512) return;
    double secs = 0;
    auto mono = analysis::fromBuffer (src, sourceRates[(size_t) layer], secs);

    // 1. hauteur (YIN sur le corps du son, fenêtres longues pour les basses / 808) + correction fine en cents
    std::vector<float> pitches;
    int frames = 0;
    const size_t startPos = (size_t) (0.03 * analysis::sr), endPos = juce::jmin (mono.size(), (size_t) (1.2 * analysis::sr));
    float peakAbs = 0.0f;
    for (size_t i = 0; i < endPos; ++i) peakAbs = juce::jmax (peakAbs, std::abs (mono[i]));
    for (size_t pos = startPos; pos + 2048 <= endPos; pos += 512)
    {
        float e = 0.0f;
        for (size_t i = pos; i < pos + 2048; ++i) e = juce::jmax (e, std::abs (mono[i]));
        if (e < peakAbs * 0.08f) continue;   // fin du son : trop faible pour être fiable
        ++frames;
        const float f = analysis::yin (mono.data() + pos, 2048, 0.18f, 27.0);
        if (f > 27.0f && f < 2000.0f) pitches.push_back (69.0f + 12.0f * std::log2 (f / 440.0f));
    }
    bool found = false;
    if (pitches.size() >= 3 && pitches.size() * 10 >= (size_t) frames * 3)
    {
        std::sort (pitches.begin(), pitches.end());
        // on garde la note la plus présente (les octaves parasites sont écartées)
        const float med = pitches[pitches.size() / 2];
        std::vector<float> near;
        for (float p : pitches) if (std::abs (p - med) < 0.6f) near.push_back (p);
        float avg = 0.0f; for (float p : near) avg += p; avg /= (float) juce::jmax ((size_t) 1, near.size());
        int root = juce::roundToInt (avg);
        if (root >= 12 && root <= 108 && near.size() * 2 >= pitches.size())
        {
            // basse, 808, son grave : la racine reste dans l octave de C5 (comme dans FL), donc jouer autour de C5
            // donne le son tel qu il est enregistré, bien grave — pas besoin de descendre tout en bas du clavier
            if (layerCat[(size_t) layer] == (int) polish::Bass || root < 48)
                while (root < 54) { root += 12; avg += 12.0f; }
            setParam (rootId (layer), (float) root);
            setParam (layerId ("L", layer) + "Fine", juce::jlimit (-50.0f, 50.0f, ((float) root - avg) * 100.0f));
            found = true;
        }
    }
    if (! found)
    {
        // son sans hauteur claire (bruit, percussion) : on le laisse tel quel, joué sur C5
        setParam (rootId (layer), 60.0f);
        setParam (layerId ("L", layer) + "Fine", 0.0f);
    }

    // 1b. la note écrite dans le nom passe avant l'oreille : un son exporté d'un plugin (« … ( Hive ) ») est en C,
    //     un son dont le nom (ou l'ancien nom, avant renommage) dit « _D# », « - C »… est sur cette note.
    //     L'oreille ne sert plus qu'à choisir l'octave (elle se trompait parfois de note : quinte, harmonique…)
    {
        const auto f = getLayerFile (layer);
        int pc = library::noteFromName (f.getFileNameWithoutExtension());
        if (pc < 0) { const auto orig = library::originalName (f); if (orig.isNotEmpty()) pc = library::noteFromName (orig); }
        if (pc >= 0)
        {
            const int heard = juce::roundToInt (getParam (rootId (layer)));
            int root;
            if (found)
            {
                int diff = ((heard - pc) % 12 + 12) % 12;      // 0..11 : demi-tons au-dessus de la bonne note
                if (diff > 0 && diff < 5) root = heard - diff;  // un peu au-dessus : on redescend
                else if (diff > 7) root = heard + (12 - diff);  // un peu en dessous : on remonte
                else root = heard - diff;                       // quinte / quarte (harmonique entendue) : la note du dessous
            }
            else root = 60 + (pc > 6 ? pc - 12 : pc);           // pas de hauteur claire : autour de C5
            if (layerCat[(size_t) layer] == (int) polish::Bass || root < 48)
                while (root < 54) root += 12;
            root = juce::jlimit (0, 127, root);
            const bool sameNote = found && ((heard - pc) % 12 + 12) % 12 == 0;
            setParam (rootId (layer), (float) root);
            if (! sameNote) setParam (layerId ("L", layer) + "Fine", 0.0f);   // même note qu'à l'oreille : on garde la finesse mesurée
        }
    }

    // 2. volume : on aligne la couche sur la couche de référence (la première chargée)
    auto rms = [this] (int l)
    {
        const auto& b = sourceBuffers[(size_t) l];
        const int n = juce::jmin (b.getNumSamples(), (int) (sourceRates[(size_t) l] * 1.0));
        double e = 0;
        for (int ch = 0; ch < b.getNumChannels(); ++ch) { const float r = b.getRMSLevel (ch, 0, n); e += r * r; }
        return std::sqrt (e / juce::jmax (1, b.getNumChannels()));
    };
    int ref = -1;
    for (int l = 0; l < maxLayers; ++l) if (sourceBuffers[(size_t) l].getNumSamples() > 0) { ref = l; break; }
    const int loadedCount = [this] { int c = 0; for (auto& b : sourceBuffers) if (b.getNumSamples() > 0) ++c; return c; }();
    if (ref >= 0 && ref != layer)
    {
        const double r0 = rms (ref), r1 = rms (layer);
        if (r0 > 1e-6 && r1 > 1e-6)
            setParam (layerId ("L", layer) + "Vol", (float) juce::jlimit (-12.0, 12.0, 20.0 * std::log10 (r0 / r1)) - 2.0f);
    }
    else setParam (layerId ("L", layer) + "Vol", loadedCount > 1 ? -2.0f : 0.0f);

    // 3. empilement naturel : un peu d'espace stéréo et de décalage
    static const float pans[] = { 0.0f, -0.25f, 0.25f, 0.0f };
    static const float delays[] = { 0.0f, 3.0f, 6.0f, 9.0f };
    setParam (layerId ("L", layer) + "Pan", loadedCount > 1 ? pans[layer % 4] * (layer >= 4 ? -1.0f : 1.0f) : 0.0f);
    setParam (layerId ("L", layer) + "Delay", loadedCount > 1 ? delays[layer % 4] + (layer >= 4 ? 1.5f : 0.0f) : 0.0f);
    setParam (layerId ("L", layer) + "Tune", 0.0f);
    setParam (layerId ("L", layer) + "Fine", 0.0f);
    setParam (layerId ("L", layer) + "On", 1.0f);
}

// Layers automatiques : on garde le son de la couche 1 et on ajoute des sons complémentaires
// pris dans d'autres catégories de la bibliothèque (rôles : corps, grave, brillance), accordés et équilibrés.
int VariantProcessor::autoLayers (int count)
{
    count = juce::jlimit (1, maxLayers, count);
    ensureLibrary();
    std::vector<library::Category> cats;
    for (auto& c : library::categories())
    {
        const auto nm = c.name.toLowerCase();
        if (nm.contains ("arp") || nm.contains ("boucle") || nm.contains ("loop")) continue;   // pas de boucles en layer
        if (! library::soundsIn (c.folder).isEmpty()) cats.push_back (c);
    }
    if (cats.empty()) return 0;
    juce::Random rng;
    std::vector<int> order ((size_t) cats.size());
    for (int i = 0; i < (int) cats.size(); ++i) order[(size_t) i] = i;
    for (int i = (int) order.size() - 1; i > 0; --i) std::swap (order[(size_t) i], order[(size_t) rng.nextInt (i + 1)]);
    // évite de reprendre la catégorie du son déjà chargé
    const auto first = getLayerFile (0);
    int pick = 0, loaded = 0;
    auto nextFile = [&] () -> juce::File
    {
        for (int tries = 0; tries < (int) order.size() * 2; ++tries)
        {
            const auto& c = cats[(size_t) order[(size_t) (pick++ % (int) order.size())]];
            if (first.existsAsFile() && first.isAChildOf (c.folder) && order.size() > 1) continue;
            const auto files = library::soundsIn (c.folder);
            if (! files.isEmpty()) return files[rng.nextInt (files.size())];
        }
        return {};
    };
    if (! first.existsAsFile()) { const auto f = nextFile(); if (f.existsAsFile() && loadSample (f, 0, true)) ++loaded; }
    static const float roleTune[] = { 0.0f, 0.0f, -12.0f, 12.0f };
    static const float roleVol[]  = { 0.0f, -4.0f, -6.0f, -9.0f };
    for (int l = 1; l < count; ++l)
    {
        const auto f = nextFile();
        if (! f.existsAsFile() || ! loadSample (f, l, true)) continue;
        ++loaded;
        const NoLink nl (noLink);
        setParam (layerId ("L", l) + "Tune", roleTune[l % 4]);
        setParam (layerId ("L", l) + "Vol", getParam (layerId ("L", l) + "Vol") + roleVol[l % 4]);
        setParam (layerId ("L", l) + "On", 1.0f);
    }
    for (int l = count; l < maxLayers; ++l) setParam (layerId ("L", l) + "On", 0.0f);
    setParam ("layerMode", 0.0f);   // toutes les couches ensemble
    return loaded;
}

void VariantProcessor::autoSetupAll()
{
    for (int l = 0; l < maxLayers; ++l)
        if (sourceBuffers[(size_t) l].getNumSamples() > 0) autoSetupLayer (l);
}

void VariantProcessor::clearLayerImpl (int layer)
{
    const NoLink nl (noLink);
    layer = juce::jlimit (0, maxLayers - 1, layer);
    sourceBuffers[(size_t) layer].setSize (0, 0);
    layerCat[(size_t) layer] = 0;
    ++sourceGen[(size_t) layer];
    {
        const juce::SpinLock::ScopedLockType lock (soundLock);
        layerFiles[(size_t) layer] = juce::File();
    }
    layerZones[(size_t) layer] = nullptr;
    sf2Preset[(size_t) layer] = -1;
    regenerateVariations (false);
}

void VariantProcessor::regenerateVariations (bool newSeed)
{
    if (newSeed)
        variationSeed = (juce::uint32) juce::Random::getSystemRandom().nextInt();

    variation::Settings vs;
    vs.style  = (int) pv ("varStyle");
    vs.amount = pv ("varAmount");
    vs.count  = (int) pv ("varCount");

    LayerSound::Ptr s = new LayerSound();
    for (int l = 0; l < maxLayers; ++l)
    {
        auto& ld = s->layers[(size_t) l];
        const auto& src0 = sourceBuffers[(size_t) l];
        if (src0.getNumSamples() == 0) continue;
        // traitement façon Sampler de FL : départ, longueur, fondus, inverser, normaliser, DC, polarité, stéréo
        auto P = [this, l] (const char* suf) { return pv ((layerId ("L", l) + suf).toRawUTF8()); };
        const double rate = sourceRates[(size_t) l];
        const int total = src0.getNumSamples();
        const int a = juce::jlimit (0, total - 64, (int) (P ("SmpStart") * total));
        const int len = juce::jlimit (64, total - a, (int) (P ("SmpLen") * total));
        juce::AudioBuffer<float> src (src0.getNumChannels(), len);
        for (int c = 0; c < src.getNumChannels(); ++c) src.copyFrom (c, 0, src0, c, a, len);
        if (P ("RemoveDC") > 0.5f)
            for (int c = 0; c < src.getNumChannels(); ++c)
            {
                float* d = src.getWritePointer (c); float x1 = 0, y1 = 0;
                for (int i = 0; i < len; ++i) { const float y = d[i] - x1 + 0.995f * y1; x1 = d[i]; y1 = y; d[i] = y; }
            }
        if (P ("Polarity") > 0.5f) src.applyGain (-1.0f);
        if (P ("SwapSt") > 0.5f && src.getNumChannels() > 1)
        {
            juce::AudioBuffer<float> tmp; tmp.makeCopyOf (src);
            src.copyFrom (0, 0, tmp, 1, 0, len); src.copyFrom (1, 0, tmp, 0, 0, len);
        }
        if (P ("Reverse") > 0.5f) for (int c = 0; c < src.getNumChannels(); ++c) src.reverse (c, 0, len);
        if (P ("Normalize") > 0.5f) { const float pk = src.getMagnitude (0, len); if (pk > 1.0e-5f) src.applyGain (0.98f / pk); }
        const int fi = juce::jmin (len / 2, (int) (P ("FadeIn") * 0.001 * rate));
        const int fo = juce::jmin (len / 2, (int) (P ("FadeOut") * 0.001 * rate));
        for (int c = 0; c < src.getNumChannels(); ++c)
        {
            if (fi > 0) src.applyGainRamp (c, 0, fi, 0.0f, 1.0f);
            if (fo > 0) src.applyGainRamp (c, len - fo, fo, 1.0f, 0.0f);
        }
        // Qualité+ : son poli (graves propres et mono, air, harmoniques, dynamique tenue) selon son type
        const bool polishOn = library::settings().getBoolValue ("polish", true);
        if (polishOn) polish::process (src, rate, (polish::Category) layerCat[(size_t) l]);
        vs.seed = variationSeed + (juce::uint32) l * 7717u;
        ld.source = src;
        ld.variants = variation::generate (src, rate, vs);
        ld.rate = rate;
        ld.valid = true;
        ld.zones = layerZones[(size_t) l];
        {
            // identifiant de la source : le même tant que le son et son traitement ne changent pas
            // (le stretch déjà calculé est gardé), nouveau sinon (jamais les notes d'un ancien son)
            juce::uint64 sig = (juce::uint64) sourceGen[(size_t) l] * 1000003ull + (juce::uint64) len * 31ull + (juce::uint64) a + (polishOn ? 7777ull : 0ull) + (juce::uint64) layerCat[(size_t) l] * 13ull;
            for (auto* suf : { "Reverse", "Normalize", "RemoveDC", "Polarity", "SwapSt", "FadeIn", "FadeOut" })
                sig = sig * 131ull + (juce::uint64) juce::roundToInt (P (suf) * 1000.0f);
            if (sig != sourceSig[(size_t) l] || sourceUid[(size_t) l] == 0)
            {
                static std::atomic<juce::uint32> counter { 1 };
                sourceSig[(size_t) l] = sig;
                sourceUid[(size_t) l] = counter.fetch_add (1);
                // attaques + hauteurs du son (mode arpégiateur) : calculées une fois, tout de suite
                sourceArp[(size_t) l] = len < (int) (rate * 40.0) ? std::make_shared<const stretch::ArpAnalysis> (stretch::analyseArp (src, rate)) : nullptr;
            }
            ld.uid = sourceUid[(size_t) l];
            ld.arp = sourceArp[(size_t) l];
        }
    }
    installSound (s);
    prepareStretch();
}

void VariantProcessor::installSound (LayerSound::Ptr s)
{
    synth.clearSounds();
    synth.addSound (s.get());
    for (int l = 0; l < maxLayers; ++l) layerValid[(size_t) l].store (s->layers[(size_t) l].valid);
    {
        const juce::SpinLock::ScopedLockType lock (soundLock);
        if (currentSound != nullptr) retiredSounds.add (currentSound);
        currentSound = s;
    }
    sendChangeMessage();
}

void VariantProcessor::refreshTuneMemory()
{
    for (int l = 0; l < maxLayers; ++l)
    {
        lastTune[(size_t) l] = pv ((layerId ("L", l) + "Tune").toRawUTF8());
        lastFine[(size_t) l] = pv ((layerId ("L", l) + "Fine").toRawUTF8());
    }
}

void VariantProcessor::parameterChanged (const juce::String& id, float value)
{
    if (id == "key" || id == "scale")
    {
        // gamme changée dans un Variant : les autres Variant du projet prennent la même (pas pendant le chargement d'un projet ou d'un preset)
        if (restoringState || inPresetLoad || syncingScale) return;
        juce::MessageManager::callAsync ([src = this, id, value]
        {
            const juce::ScopedLock sl (instanceLock());
            if (! liveProcessors().contains (src)) return;
            for (auto* p : liveProcessors())
                if (p != src && std::abs (p->getParam (id) - value) > 0.01f)
                {
                    p->syncingScale = true;
                    p->setParam (id, value);
                    p->syncingScale = false;
                }
        });
        return;
    }
    if (id.endsWith ("ArpChord"))
    {
        // bouton Arpégiateur d'une couche : si son son n'est pas un one-shot d'arpège (piano, pad…),
        // c'est l'arpégiateur de Variant qui arpège tes accords (avant : l'accord ne jouait plus qu'une note)
        if (! juce::MessageManager::existsAndIsCurrentThread() || restoringState || inPresetLoad || loadingSample) return;
        const int l = id.substring (1, 2).getIntValue() - 1;
        if (! juce::isPositiveAndBelow (l, maxLayers)) return;
        const bool isArpSound = sourceArp[(size_t) l] != nullptr && sourceArp[(size_t) l]->arpLike;
        if (value > 0.5f && ! isArpSound && pv ("arpOn") < 0.5f)
        {
            arpTurnedOnByLayer = true;
            juce::MessageManager::callAsync ([this, alive = std::weak_ptr<int> (sliceAlive)] { if (! alive.expired()) setParam ("arpOn", 1.0f); });
        }
        else if (value < 0.5f && arpTurnedOnByLayer)
        {
            bool anyOn = false;
            for (int o = 0; o < maxLayers; ++o) if (pv ((layerId ("L", o) + "ArpChord").toRawUTF8()) > 0.5f) anyOn = true;
            if (! anyOn)
            {
                arpTurnedOnByLayer = false;
                juce::MessageManager::callAsync ([this, alive = std::weak_ptr<int> (sliceAlive)] { if (! alive.expired()) setParam ("arpOn", 0.0f); });
            }
        }
        return;
    }
    // accordage lié : changer la transpo / finesse d'une couche décale les autres d'autant
    if (id.startsWith ("L") && (id.endsWith ("Tune") || id.endsWith ("Fine")))
    {
        const bool isTune = id.endsWith ("Tune");
        const int l = id.substring (1, 2).getIntValue() - 1;
        if (! juce::isPositiveAndBelow (l, maxLayers)) return;
        auto& mem = isTune ? lastTune : lastFine;
        const float delta = value - mem[(size_t) l];
        mem[(size_t) l] = value;
        if (noLink.load() > 0 || linking || pv ("linkTune") < 0.5f || std::abs (delta) < 1.0e-4f) return;
        linking = true;
        for (int o = 0; o < maxLayers; ++o)
        {
            if (o == l) continue;
            const auto oid = layerId ("L", o) + (isTune ? "Tune" : "Fine");
            const float lim = isTune ? 24.0f : 100.0f;
            const float nv = juce::jlimit (-lim, lim, pv (oid.toRawUTF8()) + delta);
            mem[(size_t) o] = nv;
            setParam (oid, nv);
        }
        linking = false;
        return;
    }
    if (id.startsWith ("fx") && id.endsWith ("Type"))
    {
        // un effet arrive dans la chaîne : on le crée tout de suite (hors du thread audio)
        if (juce::MessageManager::existsAndIsCurrentThread()) fxChain.ensure (slotParams);
        else { fxDirty = true; triggerAsyncUpdate(); }
        return;
    }
    if (id.startsWith ("slice")) slicesDirty = true;
    else variationsDirty = true;
    triggerAsyncUpdate();
}

// Stretch / mode arpégiateur : on prépare d'avance les notes autour de la racine (thread de fond)
void VariantProcessor::prepareStretch()
{
    LayerSound::Ptr s;
    for (int l = 0; l < maxLayers; ++l)
    {
        const bool stretchOn = pv ((layerId ("L", l) + "Stretch").toRawUTF8()) > 0.5f || pv ((layerId ("L", l) + "ArpChord").toRawUTF8()) > 0.5f;
        if (! stretchOn || pv ((layerId ("L", l) + "Loop").toRawUTF8()) > 0.5f) continue;
        if (s == nullptr) s = getSound();
        if (s == nullptr || ! s->layers[(size_t) l].valid) continue;
        const float manual = pv ((layerId ("L", l) + "LoopBpm").toRawUTF8());
        const float nb = nameLoopBpm[(size_t) l].load();
        const double bpm = manual > 1.0f ? manual : (nb > 1.0f ? nb : 140.0);
        stretchCache.prepareLayer (l, s->layers[(size_t) l].uid, (int) std::round (bpm / juce::jmax (20.0, hostBpmA.load()) * 1000.0));
    }
}

void VariantProcessor::timerCallback()
{
    updateSliceStretch();
    prepareStretch();
    if (fxChain.anyNeedsCreate()) fxChain.ensure (slotParams);
    if (++timerTicks % 4 != 0) return;   // le ménage ci-dessous une fois par seconde
    const juce::SpinLock::ScopedLockType lock (soundLock);
    for (int i = retiredSounds.size(); --i >= 0;)
        if (retiredSounds.getReference (i)->getReferenceCount() <= 1)
            retiredSounds.remove (i);
    const juce::SpinLock::ScopedLockType l2 (sliceLock);
    for (int i = retiredSlices.size(); --i >= 0;)
        if (retiredSlices.getReference (i)->getReferenceCount() <= 1)
            retiredSlices.remove (i);
}

LayerSound::Ptr VariantProcessor::getSound() const
{
    const juce::SpinLock::ScopedLockType lock (soundLock);
    return currentSound;
}

juce::File VariantProcessor::getLayerFile (int layer) const
{
    const juce::SpinLock::ScopedLockType lock (soundLock);
    return layerFiles[(size_t) juce::jlimit (0, maxLayers - 1, layer)];
}

//==============================================================================
void VariantProcessor::ensureLibrary()
{
    if (libraryEnsured.load() || libraryBuilding.exchange (true)) return;   // une seule fois : sinon chaque appel relançait un rafraîchissement de la bibliothèque
    pool.addJob ([this]
    {
        library::buildDefaultLibrary();
        libraryEnsured.store (true);
        libraryBuilding.store (false);
        libraryVersion.fetch_add (1);
    });
}

void VariantProcessor::previewFile (const juce::File& f)
{
    if (f == juce::File()) { stopFilePreview(); return; }
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (f));
    if (reader == nullptr) return;
    const int len = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 8.0));
    auto b = std::make_unique<juce::AudioBuffer<float>> ((int) juce::jlimit (1u, 2u, reader->numChannels), len);
    reader->read (b.get(), 0, len, 0, true, b->getNumChannels() > 1);
    filePreview.set (std::move (b), reader->sampleRate);
}

void VariantProcessor::stopFilePreview() { filePreview.set (nullptr, 44100.0); }

//==============================================================================
void VariantProcessor::setParam (const juce::String& id, float realValue)
{
    if (auto* p = apvts.getParameter (id))
    {
        // geste complet : FL voit le réglage comme « dernier touché » (Link to controller / automation)
        const bool gesture = ! restoringState && juce::MessageManager::existsAndIsCurrentThread();
        if (gesture) p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (realValue));
        if (gesture) p->endChangeGesture();
    }
}

float VariantProcessor::getParam (const juce::String& id) const
{
    if (auto* v = apvts.getRawParameterValue (id)) return v->load();
    return 0.0f;
}

void VariantProcessor::applyChordStyle (int mood)
{
    const auto& m = moods::list()[(size_t) juce::jlimit (0, (int) moods::list().size() - 1, mood)];
    setParam ("chordMode", 1.0f);
    setParam ("scale", (float) m.scales.front());
    setParam ("chordSize", (float) juce::jlimit (0, 4, m.ext - 3));
    setParam ("voicing", (float) m.voicing);
    setParam ("inversion", 0.0f);
    setParam ("strum", m.strumMs);
    setParam ("humanize", m.humanize);
}

void VariantProcessor::applyArpStyle (int mood)
{
    const auto& m = moods::list()[(size_t) juce::jlimit (0, (int) moods::list().size() - 1, mood)];
    setParam ("scale", (float) m.scales.front());
    setParam ("arpMode", (float) m.arp.mode);
    setParam ("arpRate", (float) m.arp.rate);
    setParam ("arpOct", (float) m.arp.octaves);
    setParam ("arpGate", m.arp.gate);
    setParam ("arpSwing", m.arp.swing);
    setParam ("arpSteps", (float) m.arp.numSteps);
    setParam ("arpRepeat", 1.0f);
    for (int i = 0; i < 16; ++i)
        setParam ("step" + juce::String (i + 1), m.arp.steps[(size_t) i]);
}

void VariantProcessor::randomizeArp (bool patternOnly)
{
    juce::Random rng (juce::Random::getSystemRandom().nextInt64());
    if (patternOnly)
    {
        std::array<float, 16> st {};
        arp::randomPattern (rng, (int) getParam ("arpSteps"), st);
        for (int i = 0; i < 16; ++i) setParam ("step" + juce::String (i + 1), st[(size_t) i]);
        return;
    }
    const auto a = arp::randomArp (rng);
    setParam ("arpMode", (float) a.mode);
    setParam ("arpRate", (float) a.rate);
    setParam ("arpOct", (float) a.octaves);
    setParam ("arpOctDir", (float) a.octDir);
    setParam ("arpRepeat", (float) a.repeat);
    setParam ("arpSteps", (float) a.numSteps);
    setParam ("arpGate", a.gate);
    setParam ("arpSwing", a.swing);
    setParam ("arpHuman", a.human);
    setParam ("arpFree", a.freedom);
    setParam ("arpSnap", 1.0f);
    for (int i = 0; i < 16; ++i) setParam ("step" + juce::String (i + 1), a.steps[(size_t) i]);
}

void VariantProcessor::surpriseGuide()
{
    auto& rng = juce::Random::getSystemRandom();
    const int nm = (int) moods::list().size();
    int m = rng.nextInt (nm);
    if (m == genSettings.mood) m = (m + 1 + rng.nextInt (nm - 1)) % nm;   // toujours une autre ambiance
    genSettings.mood = m;
    genSettings.richness = arp::weightedPick (rng, { { 0, 1.0f }, { 1, 3.0f }, { 2, 2.0f } });
    genSettings.arp = rng.nextFloat() < 0.6f;
    setParam ("scale", (float) moods::list()[(size_t) m].scales[(size_t) rng.nextInt ((int) moods::list()[(size_t) m].scales.size())]);
    setParam ("genSpice", 0.15f + rng.nextFloat() * 0.6f);
    setParam ("genVariety", 0.25f + rng.nextFloat() * 0.65f);
    applyArpStyle (m);
    randomizeArp (true);
    regenerateGuide (true);
}

void VariantProcessor::randomizeEffect (int slot)
{
    const auto& d = fxs::def (getSlotType (slot));
    if (d.presets.empty()) return;
    auto& rng = juce::Random::getSystemRandom();
    // un preset au hasard comme base, puis de légères variations autour : reste toujours musical
    const auto& pr = d.presets[(size_t) rng.nextInt ((int) d.presets.size())];
    for (int k = 0; k < fxs::numP; ++k)
        setParam (slotId (slot, "P" + juce::String (k + 1)), juce::jlimit (0.0f, 1.0f, pr.p[(size_t) k] + (rng.nextFloat() - 0.5f) * 0.24f));
    setParam (slotId (slot, "Mix"), juce::jlimit (0.1f, 1.0f, pr.mix + (rng.nextFloat() - 0.5f) * 0.2f));
}

//==============================================================================
// Effets
int VariantProcessor::addEffect (int type)
{
    for (int s = 0; s < fxs::numSlots; ++s)
        if (getSlotType (s) == 0)
        {
            setParam (slotId (s, "Type"), (float) type);
            setParam (slotId (s, "On"), 1.0f);
            applyEffectPreset (s, 0);
            return s;
        }
    return -1;
}

void VariantProcessor::applyEffectPreset (int slot, int preset)
{
    const auto& d = fxs::def (getSlotType (slot));
    if (d.presets.empty()) return;
    const auto& pr = d.presets[(size_t) juce::jlimit (0, (int) d.presets.size() - 1, preset)];
    for (int k = 0; k < fxs::numP; ++k) setParam (slotId (slot, "P" + juce::String (k + 1)), pr.p[(size_t) k]);
    setParam (slotId (slot, "Mix"), pr.mix);
    if (getSlotType (slot) == fxs::EQ)
    {
        auto& es = fxStores.eq;
        const juce::SpinLock::ScopedLockType l (es.lock);
        es.curves[(size_t) juce::jlimit (0, fxs::numSlots - 1, slot)] = fxs::eqPresetCurve (preset);
        ++es.version[(size_t) juce::jlimit (0, fxs::numSlots - 1, slot)];
    }
}

// Chaînes d'effets au hasard mais cohérentes : chaque style a ses effets dans un ordre qui a du sens,
// puis presets et dosages tirés au hasard dans des plages raisonnables.
juce::StringArray VariantProcessor::fxChainStyleNames()
{
    return { fr ("Lo-fi chaleureux"), fr ("Spacieux"), fr ("Rêveur"), fr ("Agressif"), fr ("Glitch"),
             fr ("Ambient / pad"), fr ("Halftime sombre"), fr ("Vintage"), fr ("French touch"), fr ("Liquide") };
}

void VariantProcessor::randomFxChain (int style)
{
    juce::Random rng;
    const int nStyles = fxChainStyleNames().size();
    if (style < 0 || style >= nStyles) style = rng.nextInt (nStyles);
    struct Step { int type; float mixLo, mixHi; float chance; };
    using namespace fxs;
    static const std::vector<std::vector<Step>> chains {
        { { Saturation, 0.5f, 0.9f, 1.0f }, { Lofi, 0.4f, 0.8f, 1.0f }, { Noise, 1.0f, 1.0f, 0.6f }, { Filter, 0.6f, 1.0f, 0.5f }, { Reverb, 0.12f, 0.25f, 1.0f } },
        { { Chorus, 0.3f, 0.6f, 0.7f }, { Delay, 0.15f, 0.3f, 1.0f }, { Reverb, 0.2f, 0.35f, 1.0f } },
        { { Harmonic, 0.4f, 0.8f, 1.0f }, { Chorus, 0.3f, 0.5f, 0.6f }, { Reverb, 0.25f, 0.4f, 1.0f } },
        { { Saturation, 0.7f, 1.0f, 1.0f }, { Compressor, 0.6f, 1.0f, 1.0f }, { EQ, 1.0f, 1.0f, 0.6f } },
        { { Glitch, 0.7f, 1.0f, 1.0f }, { Delay, 0.15f, 0.3f, 0.7f }, { Reverb, 0.1f, 0.25f, 0.8f } },
        { { Pad, 0.6f, 0.9f, 1.0f }, { EQ, 1.0f, 1.0f, 0.5f } },
        { { HalfTime, 0.8f, 1.0f, 1.0f }, { Filter, 0.6f, 1.0f, 0.8f }, { Reverb, 0.15f, 0.3f, 1.0f } },
        { { Phaser, 0.3f, 0.6f, 0.5f }, { Flanger, 0.3f, 0.5f, 0.4f }, { Lofi, 0.3f, 0.6f, 1.0f }, { Delay, 0.15f, 0.3f, 0.8f } },
        { { Filter, 0.7f, 1.0f, 1.0f }, { Compressor, 0.5f, 0.8f, 0.8f }, { Harmonic, 0.4f, 0.7f, 0.7f }, { Reverb, 0.12f, 0.25f, 0.8f } },
        { { Liquid, 0.5f, 0.9f, 1.0f }, { Delay, 0.12f, 0.25f, 0.6f }, { Reverb, 0.15f, 0.3f, 0.9f } },
    };
    fxAction ([this, &rng, style]
    {
        for (int s = 0; s < fxs::numSlots; ++s) setParam (slotId (s, "Type"), 0.0f);
        int s = 0;
        for (auto& st : chains[(size_t) style])
        {
            if (s >= fxs::numSlots || rng.nextFloat() > st.chance) continue;
            setParam (slotId (s, "Type"), (float) st.type);
            setParam (slotId (s, "On"), 1.0f);
            const auto& d = fxs::def (st.type);
            if (! d.presets.empty()) applyEffectPreset (s, rng.nextInt ((int) d.presets.size()));
            // petite variation autour du preset, toujours musicale
            for (int k = 0; k < fxs::numP; ++k)
            {
                const auto pid = slotId (s, "P" + juce::String (k + 1));
                if (d.params[(size_t) k].name == nullptr || juce::String (d.params[(size_t) k].name).isEmpty()) continue;
                const juce::String nm (d.params[(size_t) k].name);
                if (nm == "Temps" || nm == "Grille" || nm == "Motif" || nm == "Gamme" || nm == "Tonalité" || nm == "Style") continue;   // choix discrets : on garde ceux du preset
                setParam (pid, juce::jlimit (0.0f, 1.0f, getParam (pid) + (rng.nextFloat() - 0.5f) * 0.16f));
            }
            setParam (slotId (s, "Mix"), st.mixLo + rng.nextFloat() * (st.mixHi - st.mixLo));
            ++s;
        }
    });
}

// action sur la chaîne d'effets annulable d'un bloc (retirer, déplacer, réinitialiser…)
void VariantProcessor::fxAction (std::function<void()> action)
{
    auto snap = [this]
    {
        std::vector<std::pair<juce::String, float>> v;
        for (int s = 0; s < fxs::numSlots; ++s)
            for (auto what : { "Type", "On", "Mix", "P1", "P2", "P3", "P4", "P5", "P6", "P7", "P8" })
                v.push_back ({ slotId (s, what), getParam (slotId (s, what)) });
        return v;
    };
    auto curves = [this] { const juce::SpinLock::ScopedLockType l (fxStores.eq.lock); return fxStores.eq.curves; };
    const auto before = snap(); const auto eqBefore = curves();
    action();
    const auto after = snap(); const auto eqAfter = curves();
    auto apply = [this] (const std::vector<std::pair<juce::String, float>>& v, const std::array<fxs::EqCurve, fxs::numSlots>& eq)
    {
        for (auto& [id, val] : v) setParam (id, val);
        { const juce::SpinLock::ScopedLockType l (fxStores.eq.lock); fxStores.eq.curves = eq; }
        for (auto& ver : fxStores.eq.version) ++ver;
    };
    pushHistory ([apply, before, eqBefore] { apply (before, eqBefore); }, [apply, after, eqAfter] { apply (after, eqAfter); });
}

void VariantProcessor::removeEffect (int slot)
{
    // on décale les slots suivants vers le haut
    for (int s = slot; s < fxs::numSlots; ++s)
    {
        const int src = s + 1;
        for (auto what : { juce::String ("Type"), juce::String ("On"), juce::String ("Mix"), juce::String ("P1"), juce::String ("P2"),
                           juce::String ("P3"), juce::String ("P4"), juce::String ("P5"), juce::String ("P6"), juce::String ("P7"), juce::String ("P8") })
        {
            const float v = src < fxs::numSlots ? getParam (slotId (src, what)) : (what == "Type" ? 0.0f : (what == "On" || what == "Mix" ? 1.0f : 0.5f));
            setParam (slotId (s, what), v);
        }
        {
            auto& es = fxStores.eq;
            const juce::SpinLock::ScopedLockType l (es.lock);
            es.curves[(size_t) s] = src < fxs::numSlots ? es.curves[(size_t) src] : fxs::defaultEq();
            ++es.version[(size_t) s];
        }
        fxNames[(size_t) s] = src < fxs::numSlots ? fxNames[(size_t) src] : juce::String();
    }
}

void VariantProcessor::moveEffect (int slot, int dir)
{
    const int other = slot + dir;
    if (other < 0 || other >= fxs::numSlots || getSlotType (other) == 0) return;
    for (auto what : { juce::String ("Type"), juce::String ("On"), juce::String ("Mix"), juce::String ("P1"), juce::String ("P2"),
                       juce::String ("P3"), juce::String ("P4"), juce::String ("P5"), juce::String ("P6"), juce::String ("P7"), juce::String ("P8") })
    {
        const float a = getParam (slotId (slot, what)), b = getParam (slotId (other, what));
        setParam (slotId (slot, what), b);
        setParam (slotId (other, what), a);
    }
    std::swap (fxNames[(size_t) slot], fxNames[(size_t) other]);
    auto& es = fxStores.eq;
    const juce::SpinLock::ScopedLockType l (es.lock);
    std::swap (es.curves[(size_t) slot], es.curves[(size_t) other]);
    ++es.version[(size_t) slot]; ++es.version[(size_t) other];
}

//==============================================================================
// Guide MIDI
void VariantProcessor::regenerateGuide (bool newSeed)
{
    if (newSeed) pushUndo();
    notesEdited = false;
    if (newSeed)
        genSettings.seed = (juce::uint32) juce::Random::getSystemRandom().nextInt();

    genSettings.key = getKey();
    genSettings.scaleIdx = getScale();
    genSettings.arpSettings = getArpSettings();
    genSettings.reg = (int) pv ("genRegister");
    genSettings.spice = pv ("genSpice");
    genSettings.variety = pv ("genVariety");
    genSettings.liked = &likedSeqs; genSettings.disliked = &dislikedSeqs;
    genSettings.tasteAmount = tasteOn ? 0.65f : 0.0f;
    if (tasteOn && ! likedSeqs.empty())   // les réglages aimés tirent un peu les curseurs
    {
        genSettings.variety = 0.5f * genSettings.variety + 0.5f * tasteVariety;
        if (genSettings.spice > 0.01f) genSettings.spice = 0.5f * genSettings.spice + 0.5f * tasteSpice;   // « Hors gamme » à 0 = strictement dans la gamme
    }
    if (newSeed)
    {
        // Nouvelle progression VRAIMENT différente : 12 essais, on garde celui qui ressemble le moins à celle qui est là
        // et aux dernières proposées (même entre deux sessions de FL)
        genAvoid.clear();
        genAvoid.addTokens (library::settings().getValue ("genRecent"), "|", "");
        genAvoid.removeEmptyStrings();
        genSettings.avoid = &genAvoid;
        const auto before = prog;
        const bool hasBefore = ! before.chords.empty();
        auto& sysRng = juce::Random::getSystemRandom();
        gen::Progression best; juce::uint32 bestSeed = genSettings.seed; float bestScore = -1.0f;
        for (int k = 0; k < 12; ++k)
        {
            genSettings.seed = (juce::uint32) sysRng.nextInt();
            auto cand = gen::generateProgression (genSettings);
            for (auto& c : cand.chords) conformChordToScale (c);
            const float novelty = hasBefore ? gen::difference (cand.chords, before.chords, juce::jmax (cand.lengthBeats, before.lengthBeats)) : 1.0f;
            const float fresh = genAvoid.contains (cand.signature) ? 0.0f : 1.0f;
            const float score = novelty * 2.0f + fresh + sysRng.nextFloat() * 0.35f;
            if (score > bestScore) { bestScore = score; best = cand; bestSeed = genSettings.seed; }
        }
        genSettings.seed = bestSeed;
        prog = best;
        if (prog.signature.isNotEmpty())
        {
            genAvoid.removeString (prog.signature);
            genAvoid.insert (0, prog.signature);
            while (genAvoid.size() > 48) genAvoid.remove (genAvoid.size() - 1);
            library::settings().setValue ("genRecent", genAvoid.joinIntoString ("|"));
            library::settings().saveIfNeeded();
            genAvoid.removeString (prog.signature);   // (la progression en place reste reproductible si on change un réglage)
        }
    }
    else
    {
        genSettings.avoid = &genAvoid;
        prog = gen::generateProgression (genSettings);
        for (auto& c : prog.chords) conformChordToScale (c);
    }
    renderGuide();
}

// « Hors gamme » à 0 : chaque accord créé reste dans la gamme choisie (sinon on remplace par l'accord de la gamme le plus proche)
void VariantProcessor::conformChordToScale (gen::Chord& c) const
{
    if (pv ("genSpice") > 0.01f) return;
    gen::conformToScale (c, genSettings.key, genSettings.scaleIdx);
}

// nouvelle gamme choisie : la progression générée y est ramenée (si « Hors gamme » = 0 et que les notes n'ont pas été retouchées)
void VariantProcessor::conformGuideToScale()
{
    if (pv ("genSpice") > 0.01f || notesEdited || prog.chords.empty()) return;
    auto chords = prog.chords;
    bool any = false;
    for (auto& c : chords) { const auto before = c.close; conformChordToScale (c); any = any || c.close != before; }
    if (! any) return;
    pushUndo();
    prog.chords = chords;
    renderGuide();
}

void VariantProcessor::startEmptyGuide()
{
    regenerateGuide (false);   // garde la longueur et les réglages
    prog.chords.clear();
    notesEdited = false;
    renderGuide();
}

void VariantProcessor::renderGuide()
{
    genSettings.arpSettings = getArpSettings();
    std::sort (prog.chords.begin(), prog.chords.end(), [] (const gen::Chord& a, const gen::Chord& b) { return a.start < b.start; });
    genResult = gen::render (prog, genSettings);
    // on récupère les notes jouées pour l'affichage
    for (size_t i = 0; i < prog.chords.size() && i < genResult.chords.size(); ++i)
        prog.chords[i].voiced = genResult.chords[i].voiced;
    if (! notesEdited) notes = gen::notesFromResult (genResult);
    guideVersion.fetch_add (1);
    if (preview.isPlaying()) previewGuide (true);
    syncHostGuide();
}

void VariantProcessor::guideEdited()
{
    notesEdited = false;
    for (auto& c : prog.chords) if (c.roman.isEmpty()) gen::relabel (c, genSettings.key, genSettings.scaleIdx);
    renderGuide();
}

void VariantProcessor::transposeGuide (int newKey)
{
    const int d = theory::mod12 (newKey - genSettings.key);
    const int shift = d > 6 ? d - 12 : d;
    genSettings.key = newKey;
    for (auto& c : prog.chords)
    {
        for (auto& x : c.close) x += shift;
        const auto roman = c.roman;
        gen::relabel (c, genSettings.key, genSettings.scaleIdx);
        c.roman = roman;   // le chiffrage ne change pas quand on transpose
    }
    renderGuide();
}

void VariantProcessor::setGuideLength (int bars)
{
    pushUndo(); notesEdited = false;
    genSettings.bars = bars;
    prog.lengthBeats = bars * 4.0;
    for (int i = (int) prog.chords.size(); --i >= 0;)
    {
        auto& c = prog.chords[(size_t) i];
        if (c.start >= prog.lengthBeats - 0.01) prog.chords.erase (prog.chords.begin() + i);
        else c.length = juce::jmin (c.length, prog.lengthBeats - c.start);
    }
    renderGuide();
}

void VariantProcessor::setGuideChordIntervals (int i, const std::vector<int>& iv)
{
    if (! juce::isPositiveAndBelow (i, (int) prog.chords.size()) || iv.empty()) return;
    pushUndo(); notesEdited = false;
    auto& c = prog.chords[(size_t) i];
    std::vector<int> sorted = c.close; std::sort (sorted.begin(), sorted.end());
    const int root = sorted.front();
    c.close.clear();
    for (int x : iv) c.close.push_back (root + x);
    gen::relabel (c, genSettings.key, genSettings.scaleIdx);
    renderGuide();
}

void VariantProcessor::setGuideChordRoot (int i, int pc)
{
    if (! juce::isPositiveAndBelow (i, (int) prog.chords.size())) return;
    pushUndo(); notesEdited = false;
    auto& c = prog.chords[(size_t) i];
    std::vector<int> sorted = c.close; std::sort (sorted.begin(), sorted.end());
    const int root = sorted.front();
    int d = theory::mod12 (pc - root);
    if (d > 6) d -= 12;
    for (auto& x : c.close) x += d;
    gen::relabel (c, genSettings.key, genSettings.scaleIdx);
    renderGuide();
}

void VariantProcessor::setGuideChordInversion (int i, int inversion)
{
    if (! juce::isPositiveAndBelow (i, (int) prog.chords.size())) return;
    pushUndo(); notesEdited = false;
    prog.chords[(size_t) i].inversion = inversion;
    renderGuide();
}

void VariantProcessor::shiftGuideChordOctave (int i, int delta)
{
    if (! juce::isPositiveAndBelow (i, (int) prog.chords.size())) return;
    pushUndo(); notesEdited = false;
    auto& c = prog.chords[(size_t) i];
    c.octave = juce::jlimit (-2, 2, c.octave + delta);
    renderGuide();
}

void VariantProcessor::moveGuideChord (int i, double start)
{
    if (! juce::isPositiveAndBelow (i, (int) prog.chords.size())) return;
    auto& c = prog.chords[(size_t) i];
    c.start = juce::jlimit (0.0, prog.lengthBeats - c.length, start);
    genResult = gen::render (prog, genSettings);   // pendant le glissement : pas de tri (les index restent stables)
    notesEdited = false;
    notes = gen::notesFromResult (genResult);
    guideVersion.fetch_add (1);
}

void VariantProcessor::resizeGuideChord (int i, double length)
{
    if (! juce::isPositiveAndBelow (i, (int) prog.chords.size())) return;
    auto& c = prog.chords[(size_t) i];
    c.length = juce::jlimit (0.5, prog.lengthBeats - c.start, length);
    genResult = gen::render (prog, genSettings);
    notesEdited = false;
    notes = gen::notesFromResult (genResult);
    guideVersion.fetch_add (1);
}

void VariantProcessor::deleteGuideChord (int i)
{
    if (! juce::isPositiveAndBelow (i, (int) prog.chords.size()) || prog.chords.size() <= 1) return;
    pushUndo(); notesEdited = false;
    prog.chords.erase (prog.chords.begin() + i);
    renderGuide();
}

int VariantProcessor::duplicateGuideChord (int i)
{
    if (! juce::isPositiveAndBelow (i, (int) prog.chords.size())) return -1;
    auto c = prog.chords[(size_t) i];
    const double end = c.start + c.length;
    if (end + 0.5 > prog.lengthBeats) return -1;
    pushUndo(); notesEdited = false;
    c.start = end;
    c.length = juce::jmin (c.length, prog.lengthBeats - end);
    prog.chords.push_back (c);
    renderGuide();
    for (int k = 0; k < (int) prog.chords.size(); ++k) if (std::abs (prog.chords[(size_t) k].start - end) < 1.0e-6) return k;
    return -1;
}

int VariantProcessor::addGuideChord (double start)
{
    pushUndo(); notesEdited = false;
    gen::Chord c;
    c.start = juce::jlimit (0.0, prog.lengthBeats - 1.0, std::floor (start));
    c.length = 2.0;
    c.close = theory::degreeChord (0, genSettings.key, genSettings.scaleIdx, 4, 48);
    gen::relabel (c, genSettings.key, genSettings.scaleIdx);
    c.roman = theory::roman (0, c.close);
    prog.chords.push_back (c);
    renderGuide();
    for (int k = 0; k < (int) prog.chords.size(); ++k) if (std::abs (prog.chords[(size_t) k].start - c.start) < 1.0e-6) return k;
    return -1;
}

void VariantProcessor::regenerateGuideChord (int i)
{
    if (! juce::isPositiveAndBelow (i, (int) prog.chords.size())) return;
    pushUndo(); notesEdited = false;
    juce::Random rng;
    auto& c = prog.chords[(size_t) i];
    c = gen::randomChord (genSettings, c, prog.reg, rng);
    conformChordToScale (c);
    renderGuide();
}

// La progression du plugin (page Accords & MIDI) joue quand on fait Play dans FL : séquence reconstruite si les notes changent
void VariantProcessor::syncHostGuide()
{
    juce::uint64 h = 1469598103934665603ull;
    auto mixIn = [&h] (double v) { h = (h ^ (juce::uint64) (juce::int64) std::llround (v * 1000.0)) * 1099511628211ull; };
    const auto parts = enabledParts();
    for (auto& nt : notes) { mixIn (nt.start); mixIn (nt.length); mixIn (nt.pitch); mixIn (nt.vel); mixIn (nt.part); }
    for (bool b : parts) mixIn (b ? 1.0 : 0.0);
    mixIn (genResult.lengthBeats); mixIn (prog.lengthBeats);
    if (h == hostGuideHash) return;
    hostGuideHash = h;
    auto seq = std::make_unique<PreviewPlayer::Sequence>();
    seq->lengthBeats = juce::jmax (1.0, genResult.lengthBeats > 0.0 ? genResult.lengthBeats : prog.lengthBeats);
    const auto s = gen::seqFromNotes (notes, parts, true);
    for (int i = 0; i < s.getNumEvents(); ++i)
    {
        const auto& m = s.getEventPointer (i)->message;
        if (m.isNoteOnOrOff() || m.isController()) seq->events.push_back ({ m.getTimeStamp() / gen::ppq, m });
    }
    std::stable_sort (seq->events.begin(), seq->events.end(), [] (const PreviewPlayer::Event& a, const PreviewPlayer::Event& b) { return a.beat < b.beat; });
    hostGuideHasNotes.store (! seq->events.empty());
    hostGuide.setSequence (std::move (seq));
}

void VariantProcessor::previewGuide (bool play)
{
    if (! play) { preview.stop(); return; }

    auto seq = std::make_unique<PreviewPlayer::Sequence>();
    seq->lengthBeats = genResult.lengthBeats;

    const auto s = gen::seqFromNotes (notes, enabledParts(), true);   // avec les slides
    for (int i = 0; i < s.getNumEvents(); ++i)
    {
        const auto& m = s.getEventPointer (i)->message;
        if (m.isNoteOnOrOff() || m.isController())
            seq->events.push_back ({ m.getTimeStamp() / gen::ppq, m });
    }

    std::stable_sort (seq->events.begin(), seq->events.end(),
                      [] (const PreviewPlayer::Event& a, const PreviewPlayer::Event& b) { return a.beat < b.beat; });
    const bool wasPlaying = preview.isPlaying();
    preview.setSequence (std::move (seq), wasPlaying);   // déjà en lecture : on continue au même endroit
    if (! wasPlaying && guideStart > 0.0) preview.seek (guideStart);   // départ choisi sur la règle du piano roll
}

juce::File VariantProcessor::getExportFolder() const
{
    auto f = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Variant MIDI");
    f.createDirectory();
    return f;
}

juce::String VariantProcessor::layerName (int layer)
{
    static const char* n[] = { "Accords", "Basse", "Arpege", "Tout" };
    return n[juce::jlimit (0, 3, layer)];
}

juce::File VariantProcessor::exportGuide (int layer)
{
    const auto moodName = moods::names()[genSettings.mood].replace ("/", "-").removeCharacters (" ");
    auto scaleName = theory::scaleNames()[genSettings.scaleIdx];
    scaleName = scaleName.upToFirstOccurrenceOf (" (", false, false).removeCharacters (" ");

    const auto name = juce::File::createLegalFileName (moodName + "_" + theory::pcNames[genSettings.key] + "-" + scaleName
                                                       + "_" + layerName (layer) + "_"
                                                       + juce::String::toHexString ((int) ((genSettings.seed + (juce::uint32) guideVersion.load()) & 0xffff)) + ".mid");
    const auto file = getExportFolder().getChildFile (name);

    std::array<bool, 3> parts { layer == 0, layer == 1, layer == 2 };
    if (layer == 3) parts = enabledParts();
    const auto seq = gen::seqFromNotes (notes, parts);

    if (seq.getNumEvents() == 0) return {};
    return gen::writeMidiFile (seq, genResult.lengthBeats, moodName + " " + layerName (layer), file) ? file : juce::File();
}

//==============================================================================
void VariantProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    for (int l = 0; l < maxLayers; ++l)
        state.setProperty ("layerPath" + juce::String (l + 1), getLayerFile (l).getFullPathName(), nullptr);
    for (int l = 0; l < maxLayers; ++l) state.setProperty ("sf2Preset" + juce::String (l + 1), sf2Preset[(size_t) l], nullptr);
    state.setProperty ("varSeed", (juce::int64) variationSeed, nullptr);
    state.setProperty ("genMood", genSettings.mood, nullptr);
    state.setProperty ("genMoodName", juce::String::fromUTF8 (moods::list()[(size_t) juce::jlimit (0, (int) moods::list().size() - 1, genSettings.mood)].name), nullptr);
    for (int s = 0; s < fxs::numSlots; ++s) if (fxNames[(size_t) s].isNotEmpty()) state.setProperty ("fxName" + juce::String (s + 1), fxNames[(size_t) s], nullptr);
    state.setProperty ("genBars", genSettings.bars, nullptr);
    state.setProperty ("genRich", genSettings.richness, nullptr);
    state.setProperty ("genChords", genSettings.chords, nullptr);
    state.setProperty ("genBass", genSettings.bass, nullptr);
    state.setProperty ("genArp", genSettings.arp, nullptr);
    state.setProperty ("genSeed", (juce::int64) genSettings.seed, nullptr);
    state.setProperty ("genKey", genSettings.key, nullptr);
    state.setProperty ("genScale", genSettings.scaleIdx, nullptr);
    state.setProperty ("theme", themeIndex, nullptr);
    state.setProperty ("genNoOverlap", genSettings.noOverlap, nullptr);
    state.setProperty ("hostPlayGuide2", hostPlayGuide.load(), nullptr);
    state.setProperty ("autoFxSig", autoFxSig, nullptr);
    state.setProperty ("presetName", presetName, nullptr);
    state.setProperty ("libView", libView, nullptr);
    writeFxStores (state);
    state.setProperty ("slicePath", sliceFile.getFullPathName(), nullptr);
    state.setProperty ("sliceLayer", sliceLayer, nullptr);
    state.setProperty ("sliceRegA", sliceRegA, nullptr);
    state.setProperty ("sliceRegB", sliceRegB, nullptr);
    state.setProperty ("sliceZoneMode", sliceZoneMode, nullptr);
    state.setProperty ("sliceBpmUser", sliceBpmManual.load() ? sliceBpm.load() : 0.0, nullptr);   // BPM tapé à la main dans le Sampler
    {
        juce::StringArray zs;
        for (auto& z : sliceZones) zs.add (juce::String (z.first) + "-" + juce::String (z.second));
        state.setProperty ("sliceZones", zs.joinIntoString (","), nullptr);
    }
    {
        // réglages des chops (seulement ceux qui ont été changés) : « index:volume:hauteur:pan:inversé »
        juce::StringArray cs;
        for (int i = 0; i < slicer::ChopSettings::n; ++i)
            if (! chopSettings.isDefault (i))
                cs.add (juce::String (i) + ":" + juce::String (chopSettings.vol[(size_t) i].load(), 2) + ":" + juce::String (chopSettings.pitch[(size_t) i].load(), 0)
                        + ":" + juce::String (chopSettings.pan[(size_t) i].load(), 2) + ":" + juce::String (chopSettings.rev[(size_t) i].load()));
        state.setProperty ("chopSettings", cs.joinIntoString (";"), nullptr);
    }
    if (sliceManual && sliceZones.empty())
        if (auto ss = getSliceSound())
        {
            juce::StringArray m;
            for (int s : ss->starts) m.add (juce::String (s));
            state.setProperty ("sliceMarks", m.joinIntoString (","), nullptr);
        }

    // progression éditée
    juce::ValueTree pt ("Prog");
    pt.setProperty ("len", prog.lengthBeats, nullptr);
    pt.setProperty ("reg", prog.reg, nullptr);
    for (auto& c : prog.chords)
    {
        juce::ValueTree ct ("C");
        juce::StringArray notes;
        for (int x : c.close) notes.add (juce::String (x));
        ct.setProperty ("s", c.start, nullptr);
        ct.setProperty ("l", c.length, nullptr);
        ct.setProperty ("n", notes.joinIntoString (","), nullptr);
        ct.setProperty ("inv", c.inversion, nullptr);
        ct.setProperty ("oct", c.octave, nullptr);
        ct.setProperty ("deg", c.degree, nullptr);
        ct.setProperty ("rom", c.roman, nullptr);
        pt.appendChild (ct, nullptr);
    }
    state.appendChild (pt, nullptr);
    state.appendChild (wt::toTree (wtSynth), nullptr);   // synthé (page Sound design)
    writeHosted (state);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void VariantProcessor::setStateInformation (const void* data, int size)
{
    const NoLink nl (noLink);
    restoringState = true;
    stateWasRestored = true;
    bridgeHold.store (true);
    struct Resync { VariantProcessor& p; ~Resync() { p.bridgesResync(); } } resync { *this };   // réglages chargés -> paramètres de FL
    struct Done { bool& f; ~Done() { f = false; } } done { restoringState };
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    auto pt = tree.getChildWithName ("Prog");
    if (pt.isValid()) tree.removeChild (pt, nullptr);
    {
        auto st = tree.getChildWithName ("Synth");
        if (st.isValid()) { wt::fromTree (wtSynth, st, true); tree.removeChild (st, nullptr); }
        else wtSynth.enabled.store (false);
    }
    readHosted (tree, true);
    apvts.replaceState (tree);

    variationSeed        = (juce::uint32) (juce::int64) tree.getProperty ("varSeed", (juce::int64) variationSeed);
    {
        // ambiance : par son nom (la liste a changé en v3.20), sinon ancien numéro converti
        const int byName = moods::indexOf (tree.getProperty ("genMoodName").toString());
        if (byName >= 0) genSettings.mood = byName;
        else if (tree.hasProperty ("genMood")) genSettings.mood = moods::fromLegacyIndex ((int) tree.getProperty ("genMood"));
        genSettings.mood = juce::jlimit (0, (int) moods::list().size() - 1, genSettings.mood);
    }
    genSettings.bars     = tree.getProperty ("genBars", genSettings.bars);
    genSettings.richness = tree.getProperty ("genRich", genSettings.richness);
    genSettings.chords   = tree.getProperty ("genChords", genSettings.chords);
    genSettings.bass     = tree.getProperty ("genBass", genSettings.bass);
    genSettings.arp      = tree.getProperty ("genArp", genSettings.arp);
    genSettings.seed     = (juce::uint32) (juce::int64) tree.getProperty ("genSeed", (juce::int64) genSettings.seed);
    themeIndex           = tree.getProperty ("theme", themeIndex);
    genSettings.noOverlap = tree.getProperty ("genNoOverlap", false);
    hostPlayGuide.store ((bool) tree.getProperty ("hostPlayGuide2", false));   // (anciens projets : éteint, FL joue son piano roll)
    autoFxSig = tree.getProperty ("autoFxSig").toString();
    presetName = tree.getProperty ("presetName").toString();
    libView = tree.getProperty ("libView").toString();
    for (int s = 0; s < fxs::numSlots; ++s) fxNames[(size_t) s] = tree.getProperty ("fxName" + juce::String (s + 1)).toString();

    if (pt.isValid() && pt.getNumChildren() > 0)
    {
        genSettings.key = tree.getProperty ("genKey", getKey());
        genSettings.scaleIdx = tree.getProperty ("genScale", getScale());
        genSettings.reg = (int) pv ("genRegister");
        genSettings.spice = pv ("genSpice");
        genSettings.variety = pv ("genVariety");
        prog.chords.clear();
        prog.lengthBeats = pt.getProperty ("len", 16.0);
        prog.reg = pt.getProperty ("reg", (int) gen::RegMid);
        for (int i = 0; i < pt.getNumChildren(); ++i)
        {
            auto ct = pt.getChild (i);
            gen::Chord c;
            c.start = ct.getProperty ("s"); c.length = ct.getProperty ("l");
            juce::StringArray notes; notes.addTokens (ct.getProperty ("n").toString(), ",", "");
            for (auto& s : notes) if (s.isNotEmpty()) c.close.push_back (s.getIntValue());
            if (c.close.empty()) continue;
            c.inversion = ct.getProperty ("inv", -1); c.octave = ct.getProperty ("oct", 0); c.degree = ct.getProperty ("deg", 0);
            gen::relabel (c, genSettings.key, genSettings.scaleIdx);
            const auto rom = ct.getProperty ("rom").toString();
            if (rom.isNotEmpty()) c.roman = rom;
            prog.chords.push_back (c);
        }
        renderGuide();
    }
    else
    {
        startEmptyGuide();
    }

    for (int l = 0; l < maxLayers; ++l)
    {
        auto path = tree.getProperty ("layerPath" + juce::String (l + 1)).toString();
        if (l == 0 && path.isEmpty()) path = tree.getProperty ("samplePath").toString();   // projets v0.1 - v0.3
        if (path.isNotEmpty() && juce::File::isAbsolutePath (path))
        {
            const juce::File f (path);
            if (f.existsAsFile())
            {
                const bool wasOn = pv ((layerId ("L", l) + "On").toRawUTF8()) > 0.5f;
                if (f.hasFileExtension ("sf2"))
                {
                    const float fine = pv ((layerId ("L", l) + "Fine").toRawUTF8());
                    loadSoundFontPreset (f, (int) tree.getProperty ("sf2Preset" + juce::String (l + 1), -1), l);
                    const NoLink nl (noLink);
                    setParam (layerId ("L", l) + "Fine", fine);
                }
                else loadSample (f, l, false);
                setParam (layerId ("L", l) + "On", wasOn || l == 0 ? 1.0f : 0.0f);
            }
        }
        else if (getLayerFile (l) != juce::File())
        {
            // couche vide dans le projet (ex. le son de départ chargé avant que FL rende le projet) : on la vide
            const bool wasOn = pv ((layerId ("L", l) + "On").toRawUTF8()) > 0.5f;
            clearLayerImpl (l);
            setParam (layerId ("L", l) + "On", wasOn ? 1.0f : 0.0f);
        }
    }

    readFxStores (tree);
    // slicer
    {
        const auto sp = tree.getProperty ("slicePath").toString();
        const int sl = tree.getProperty ("sliceLayer", -1);
        bool ok = false;
        if (sp.isNotEmpty() && juce::File::isAbsolutePath (sp) && juce::File (sp).existsAsFile()) ok = loadSliceFile (juce::File (sp));
        else if (sl >= 0) ok = useLayerForSlices (sl);
        if (ok && ((int) tree.getProperty ("sliceRegA", 0) > 0 || (int) tree.getProperty ("sliceRegB", -1) >= 0))
            setSliceRegion ((int) tree.getProperty ("sliceRegA", 0), (int) tree.getProperty ("sliceRegB", -1));
        const auto marks = tree.getProperty ("sliceMarks").toString();
        if (ok && marks.isNotEmpty())
        {
            juce::StringArray t; t.addTokens (marks, ",", "");
            std::vector<int> st;
            for (auto& s : t) if (s.isNotEmpty()) st.push_back (s.getIntValue());
            installSlices (st, true);
        }
        const auto zs = tree.getProperty ("sliceZones").toString();
        if (ok && zs.isNotEmpty())
        {
            juce::StringArray t; t.addTokens (zs, ",", "");
            std::vector<std::pair<int, int>> z;
            for (auto& s : t) if (s.contains ("-")) z.push_back ({ s.upToFirstOccurrenceOf ("-", false, false).getIntValue(), s.fromFirstOccurrenceOf ("-", false, false).getIntValue() });
            sliceZones = z;
        }
        sliceZoneMode = (bool) tree.getProperty ("sliceZoneMode", false);
        if (ok && sliceZoneMode && ! sliceZones.empty()) applySliceZones();
        if (const double ub = (double) tree.getProperty ("sliceBpmUser", 0.0); ok && ub > 0.0) setSliceBpm (ub);
        // réglages des chops : après le chargement du sample (qui les remet à zéro)
        chopSettings.resetAll();
        juce::StringArray cs; cs.addTokens (tree.getProperty ("chopSettings").toString(), ";", "");
        for (auto& c : cs)
        {
            juce::StringArray f; f.addTokens (c, ":", "");
            if (f.size() < 5) continue;
            const int i = f[0].getIntValue();
            if (i < 0 || i >= slicer::ChopSettings::n) continue;
            chopSettings.vol[(size_t) i] = juce::jlimit (-24.0f, 12.0f, f[1].getFloatValue());
            chopSettings.pitch[(size_t) i] = (float) juce::jlimit (-24, 24, f[2].getIntValue());
            chopSettings.pan[(size_t) i] = juce::jlimit (-1.0f, 1.0f, f[3].getFloatValue());
            chopSettings.rev[(size_t) i] = f[4].getIntValue() != 0 ? 1 : 0;
        }
        ++chopSettings.version;
    }
}

//==============================================================================
// Notes éditables
void VariantProcessor::pushUndo()
{
    undoStack.push_back ({ prog, notes, notesEdited });
    if (undoStack.size() > 60) undoStack.erase (undoStack.begin());
    redoStack.clear();
    pushHistory ([this] { undo(); }, [this] { redo(); });
}

void VariantProcessor::beginEdit() { pushUndo(); }

bool VariantProcessor::undo()
{
    if (undoStack.empty()) return false;
    redoStack.push_back ({ prog, notes, notesEdited });
    auto s = undoStack.back(); undoStack.pop_back();
    prog = s.prog; notes = s.notes; notesEdited = s.notesEdited;
    const bool keep = notesEdited; renderGuide(); if (keep) notes = s.notes;
    guideVersion.fetch_add (1);
    return true;
}

bool VariantProcessor::redo()
{
    if (redoStack.empty()) return false;
    undoStack.push_back ({ prog, notes, notesEdited });
    auto s = redoStack.back(); redoStack.pop_back();
    prog = s.prog; notes = s.notes; notesEdited = s.notesEdited;
    const bool keep = notesEdited; renderGuide(); if (keep) notes = s.notes;
    guideVersion.fetch_add (1);
    return true;
}

//==============================================================================
// Sons du métronome (synthétisés) : 0 clic, 1 bois, 2 cloche, 3 charleston, 4 bip 8 bits, 5 rimshot
void VariantProcessor::buildMetroClicks (int sound)
{
    const double sr = currentSampleRate > 0.0 ? currentSampleRate : 44100.0;
    juce::Random rng (1234);
    for (int accent = 0; accent < 2; ++accent)
    {
        const int len = (int) (sr * 0.09);
        auto& b = metroClicks[(size_t) accent];
        b.setSize (1, len);
        float* d = b.getWritePointer (0);
        const double up = accent ? 1.5 : 1.0;   // le premier temps de la mesure est plus aigu
        float lp = 0.0f, bp1 = 0.0f, bp2 = 0.0f;
        for (int i = 0; i < len; ++i)
        {
            const double t = i / sr;
            const double tw = juce::MathConstants<double>::twoPi * t;
            float v = 0.0f;
            switch (sound)
            {
                case 1:   // bois : bruit filtré résonant + ton
                {
                    const float nz = rng.nextFloat() * 2.0f - 1.0f;
                    const float f = (float) (2.0 * std::sin (juce::MathConstants<double>::pi * 1100.0 * up / sr));
                    bp1 += f * (nz - bp1 - 0.08f * bp2); bp2 += f * bp1;
                    v = (float) (bp2 * 0.6 + 0.5 * std::sin (tw * 1100.0 * up)) * (float) std::exp (-t * 70.0);
                    break;
                }
                case 2:   // cloche (cowbell)
                    v = (float) ((std::sin (tw * 540.0 * up) > 0 ? 0.5 : -0.5) + (std::sin (tw * 800.0 * up) > 0 ? 0.5 : -0.5)) * 0.5f * (float) std::exp (-t * 35.0);
                    break;
                case 3:   // charleston : bruit aigu
                {
                    const float nz = rng.nextFloat() * 2.0f - 1.0f;
                    lp += 0.5f * (nz - lp);
                    v = (nz - lp) * (float) std::exp (-t * (accent ? 45.0 : 90.0));
                    break;
                }
                case 4:   // bip 8 bits
                    v = (std::sin (tw * 1000.0 * up) > 0 ? 0.45f : -0.45f) * (t < 0.04 ? 1.0f : 0.0f);
                    break;
                case 5:   // rimshot
                {
                    const float nz = rng.nextFloat() * 2.0f - 1.0f;
                    v = (float) (0.6 * std::sin (tw * 1700.0 * up) + 0.5 * nz) * (float) std::exp (-t * 120.0);
                    break;
                }
                default:  // clic
                    v = (float) std::sin (tw * 1500.0 * up) * (float) std::exp (-t * 90.0);
                    break;
            }
            const float att = (float) juce::jmin (1.0, t / 0.0008);
            d[i] = v * att * 0.7f;
        }
    }
    metroSoundBuilt = sound;
    metroRateBuilt = currentSampleRate;
}

//==============================================================================
// Presets d'arpège
juce::File VariantProcessor::findLibrarySound (const std::vector<const char*>& keywords, juce::Random& rng)
{
    ensureLibrary();
    juce::Array<juce::File> all, hits;
    for (auto& c : library::categories())
    {
        const auto cat = c.name.toLowerCase();
        if (cat.contains ("arp") || cat.contains ("boucle") || cat.contains ("loop")) continue;
        for (auto& f : library::soundsIn (c.folder))
        {
            all.add (f);
            const auto nm = f.getFileNameWithoutExtension().toLowerCase();
            for (auto* k : keywords) if (nm.contains (k) || cat.contains (k)) { hits.add (f); break; }
        }
    }
    if (! hits.isEmpty()) return hits[rng.nextInt (hits.size())];
    return all.isEmpty() ? juce::File() : all[rng.nextInt (all.size())];
}

// Les presets "usine" deviennent de vrais fichiers dans la banque, chacun lié à un son précis de la banque.
// regenerate = refait les presets usine avec d'autres sons (les presets perso ne sont jamais touchés).
static constexpr bool writeFactoryPresetFiles = false;

int VariantProcessor::ensureFactoryPresets (bool regenerate)
{
    // Les presets d usine ne sont plus écrits dans la bibliothèque des gens (« Presets d arpège ») : demandé par le créateur.
    juce::ignoreUnused (regenerate);
    if constexpr (! writeFactoryPresetFiles) return 0;
    constexpr int presetVersion = 4;   // v4 : notes trop courtes allongées (certains presets ne faisaient presque que des clics)
    ensureLibrary();
    if (! library::defaultFolder().isDirectory()) return 0;
    const auto folder = presets::userFolder();
    int written = 0;
    const auto& list = presets::factory();
    juce::Random rng (regenerate ? juce::Random::getSystemRandom().nextInt64() : 4242);
    for (int i = 0; i < (int) list.size(); ++i)
    {
        const auto& p = list[(size_t) i];
        const auto file = folder.getChildFile (juce::File::createLegalFileName (juce::String::fromUTF8 (p.name)) + ".vpreset");
        juce::File keepSound;
        if (file.existsAsFile())
        {
            auto old = juce::XmlDocument::parse (file);
            if (old == nullptr || ! old->getBoolAttribute ("factory")) continue;   // preset perso : on garde
            const bool outdated = old->getIntAttribute ("version", 1) < presetVersion;
            if (! regenerate && ! outdated) continue;
            if (! regenerate)   // mise à jour des réglages : on garde le son choisi
                if (auto* e = old->getChildByName ("LAYER")) keepSound = juce::File (e->getStringAttribute ("path"));
        }
        const auto snd = keepSound.existsAsFile() ? keepSound : findLibrarySound (p.keywords, rng);
        juce::XmlElement x ("VARIANTPRESET");
        x.setAttribute ("name", juce::String::fromUTF8 (p.name));
        x.setAttribute ("factory", true);
        x.setAttribute ("version", presetVersion);
        auto* prm = x.createNewChildElement ("PARAMS");
        auto put = [prm] (const juce::String& id, double v) { prm->setAttribute (id, v); };
        put ("arpOn", 1); put ("arpMode", p.mode); put ("arpRate", p.rate); put ("arpOct", p.octaves); put ("arpOctDir", p.octDir);
        put ("arpGate", p.gate); put ("arpSwing", p.swing); put ("arpRepeat", 1); put ("arpSource", 0); put ("arpLatch", 0);
        put ("arpHuman", 0.25); put ("arpSteps", 16);
        for (int k = 0; k < 16; ++k) put ("step" + juce::String (k + 1), juce::jlimit (0.0, 1.0, (p.steps[k] - '0') / 9.0));
        put ("chordOn", p.chord ? 1 : 0); put ("chordMode", 1); put ("chordSize", p.chordSize); put ("chordRegister", 1);
        put ("attack", p.attack); put ("decay", p.decay); put ("sustain", p.sustain); put ("release", p.release);
        put ("layerMode", 0);
        for (int l = 1; l < maxLayers; ++l) put (layerId ("L", l) + "On", 0);
        put ("L1On", 1);
        for (int s = 0; s < fxs::numSlots; ++s)
        {
            if (s < (int) p.fx.size())
            {
                const auto& fx = p.fx[(size_t) s];
                const auto& d = fxs::def (fx.type);
                put (slotId (s, "Type"), fx.type); put (slotId (s, "On"), 1);
                if (! d.presets.empty())
                {
                    const auto& pr = d.presets[(size_t) juce::jlimit (0, (int) d.presets.size() - 1, fx.preset)];
                    for (int k = 0; k < fxs::numP; ++k) put (slotId (s, "P" + juce::String (k + 1)), pr.p[(size_t) k]);
                    put (slotId (s, "Mix"), fx.mix >= 0.0f ? fx.mix : pr.mix);
                }
            }
            else put (slotId (s, "Type"), 0);
        }
        auto* e = x.createNewChildElement ("LAYER");
        e->setAttribute ("index", 0);
        e->setAttribute ("path", snd.getFullPathName());
        e->setAttribute ("file", snd.getFileName());
        e->setAttribute ("sf2", -1);
        e->setAttribute ("autoSetup", true);   // la note du son est détectée au chargement
        x.createNewChildElement ("SYNTH")->setAttribute ("on", false);   // preset de sample : le synthé se tait
        if (x.writeTo (file)) ++written;
    }
    // presets d'arpège avec un son de synthé fait pour eux (pas de sample)
    const auto& sl = presets::synthArps();
    for (int i = 0; i < (int) sl.size(); ++i)
    {
        const auto& p = sl[(size_t) i];
        const auto file = folder.getChildFile (juce::File::createLegalFileName (juce::String::fromUTF8 (p.name)) + ".vpreset");
        if (file.existsAsFile() && ! regenerate)
        {
            auto old = juce::XmlDocument::parse (file);
            if (old == nullptr || ! old->getBoolAttribute ("factory")) continue;
            if (old->getIntAttribute ("version", 1) >= presetVersion) continue;
        }
        juce::XmlElement x ("VARIANTPRESET");
        x.setAttribute ("name", juce::String::fromUTF8 (p.name));
        x.setAttribute ("factory", true);
        x.setAttribute ("version", presetVersion);
        auto* prm = x.createNewChildElement ("PARAMS");
        auto put = [prm] (const juce::String& id, double v) { prm->setAttribute (id, v); };
        put ("arpOn", 1); put ("arpMode", p.mode); put ("arpRate", p.rate); put ("arpOct", p.octaves); put ("arpOctDir", p.octDir);
        put ("arpGate", p.gate); put ("arpSwing", p.swing); put ("arpRepeat", 1); put ("arpSource", 0); put ("arpLatch", 0);
        put ("arpHuman", 0.15); put ("arpSteps", 16);
        for (int k = 0; k < 16; ++k) put ("step" + juce::String (k + 1), juce::jlimit (0.0, 1.0, (p.steps[k] - '0') / 9.0));
        put ("chordOn", p.chord ? 1 : 0); put ("chordMode", 1); put ("chordSize", p.chordSize); put ("chordRegister", 1);
        for (int l = 0; l < maxLayers; ++l) put (layerId ("L", l) + "On", 0);   // le son vient du synthé seul
        for (int s = 0; s < fxs::numSlots; ++s)
        {
            if (s < (int) p.fx.size())
            {
                const auto& fx = p.fx[(size_t) s];
                const auto& d = fxs::def (fx.type);
                put (slotId (s, "Type"), fx.type); put (slotId (s, "On"), 1);
                if (! d.presets.empty())
                {
                    const auto& pr = d.presets[(size_t) juce::jlimit (0, (int) d.presets.size() - 1, fx.preset)];
                    for (int k = 0; k < fxs::numP; ++k) put (slotId (s, "P" + juce::String (k + 1)), pr.p[(size_t) k]);
                    put (slotId (s, "Mix"), fx.mix >= 0.0f ? fx.mix : pr.mix);
                }
            }
            else put (slotId (s, "Type"), 0);
        }
        wt::Engine tmp;
        tmp.patch.reset();
        juce::String ta = "builtin:0", tb = "builtin:0";
        p.make (tmp.patch, ta, tb);
        if (auto t = wt::tableFromPath (synthpresets::resolveTable (ta))) tmp.setTable (0, t);
        if (auto t = wt::tableFromPath (synthpresets::resolveTable (tb))) tmp.setTable (1, t);
        tmp.enabled.store (true);
        tmp.mix.store (p.volume);
        if (auto sx = wt::toTree (tmp).createXml()) { sx->setTagName ("SYNTH"); x.addChildElement (sx.release()); }
        if (x.writeTo (file)) ++written;
    }
    return written;
}

bool VariantProcessor::applyFactoryPreset (int index)
{
    const auto& list = presets::factory();
    if (! juce::isPositiveAndBelow (index, (int) list.size())) return false;
    ensureFactoryPresets (false);
    const auto file = presets::userFolder().getChildFile (juce::File::createLegalFileName (juce::String::fromUTF8 (list[(size_t) index].name)) + ".vpreset");
    return loadUserPreset (file);
}

static bool presetParam (const juce::String& id)
{
    // tout sauf le slicer, le générateur, le métronome et l'affichage
    return ! (id.startsWith ("slice") || id.startsWith ("gen") || id.startsWith ("metro"));
}

bool VariantProcessor::saveUserPreset (const juce::String& name)
{
    return saveFullPreset (name).existsAsFile();
}

// Dossier des presets de l'utilisateur : dans la banque, un sous-dossier par personne (nom de session Windows)
juce::File VariantProcessor::userPresetFolder()
{
    auto who = juce::File::createLegalFileName (juce::SystemStats::getLogonName().trim());
    if (who.isEmpty()) who = "Moi";
    auto f = library::defaultFolder().getChildFile (library::userPresetFolderName()).getChildFile (who);
    f.createDirectory();
    return f;
}

// Preset complet : toutes les couches (sons + réglages), les VST ouverts dans Variant (avec leur son),
// le synthé, la chaîne d'effets et ses courbes -> en le rechargeant on retrouve exactement la même chose
juce::File VariantProcessor::saveFullPreset (const juce::String& name0)
{
    const auto name = name0.trim().isEmpty() ? juce::String ("Mon preset") : name0.trim();
    juce::XmlElement x ("VARIANTPRESET");
    x.setAttribute ("name", name);
    x.setAttribute ("full", true);
    x.setAttribute ("user", juce::SystemStats::getLogonName());
    auto* prm = x.createNewChildElement ("PARAMS");
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (presetParam (rp->getParameterID()))
                prm->setAttribute (rp->getParameterID(), (double) rp->convertFrom0to1 (rp->getValue()));
    for (int l = 0; l < maxLayers; ++l)
    {
        auto* e = x.createNewChildElement ("LAYER");
        e->setAttribute ("index", l);
        e->setAttribute ("path", getLayerFile (l).getFullPathName());
        e->setAttribute ("file", getLayerFile (l).getFileName());
        e->setAttribute ("sf2", sf2Preset[(size_t) l]);
    }
    if (auto sx = wt::toTree (wtSynth).createXml()) { sx->setTagName ("SYNTH"); x.addChildElement (sx.release()); }   // le son du synthé est gardé avec le preset
    {
        juce::ValueTree fx ("FX");
        writeFxStores (fx);   // courbes EQ, Liquide, Glitch, Temps & Volume
        if (auto fxml = fx.createXml()) { fxml->setTagName ("FXSTORES"); x.addChildElement (fxml.release()); }
        juce::ValueTree hosted ("HOSTED");
        writeHosted (hosted);   // VST ouverts dans Variant + leur état complet
        if (auto hxml = hosted.createXml()) x.addChildElement (hxml.release());
    }
    const auto file = userPresetFolder().getChildFile (juce::File::createLegalFileName (name) + ".vpreset");
    return x.writeTo (file) ? file : juce::File();
}

bool VariantProcessor::loadUserPreset (const juce::File& file)
{
    auto x = juce::XmlDocument::parse (file);
    if (x == nullptr || ! x->hasTagName ("VARIANTPRESET")) return false;
    inPresetLoad = true;
    bridgeHold.store (true);
    struct Resync { VariantProcessor& p; ~Resync() { p.bridgesResync(); } } resync { *this };   // synthé et VST du preset -> paramètres de FL
    resetToLaunchState();   // on repart du son de lancement : rien ne reste du son précédent
    presetName = file.getFileNameWithoutExtension();
    struct Done { bool& f; ~Done() { f = false; } } done { inPresetLoad };
    // sons d'abord (le chargement règle la note), puis tous les réglages du preset
    for (auto* e : x->getChildWithTagNameIterator ("LAYER"))
    {
        const int l = e->getIntAttribute ("index");
        if (! juce::isPositiveAndBelow (l, maxLayers)) continue;
        juce::File f (e->getStringAttribute ("path"));
        if (! f.existsAsFile()) f = library::resolveRenamed (f);   // bibliothèque ailleurs (autre PC, autre dossier)
        if (! f.existsAsFile() && e->getStringAttribute ("file").isNotEmpty())
        {
            // son déplacé : on le cherche par son nom dans la bibliothèque
            const auto wanted = e->getStringAttribute ("file");
            for (auto& c : library::categories())
                for (auto& s : library::soundsIn (c.folder)) if (s.getFileName() == wanted) f = s;
        }
        if (f.existsAsFile())
        {
            if (f.hasFileExtension ("sf2")) loadSoundFontPreset (f, e->getIntAttribute ("sf2", -1), l);
            else loadSample (f, l, e->getBoolAttribute ("autoSetup", false));
        }
        else if (getLayerFile (l) != juce::File()) clearLayer (l);
    }
    if (auto* sx = x->getChildByName ("SYNTH"))   // son de synthé du preset (ou synthé éteint pour un preset de sample)
    {
        if (sx->hasAttribute ("A_Pos")) wt::fromTree (wtSynth, juce::ValueTree::fromXml (*sx), false);
        wtSynth.enabled.store (sx->getBoolAttribute ("on", false));
        if (sx->hasAttribute ("mix")) wtSynth.mix.store ((float) sx->getDoubleAttribute ("mix", 0.8));
    }
    const bool full = x->getBoolAttribute ("full", false);
    if (full)
    {
        // preset complet : les VST (avec leur son) et les courbes des effets
        if (auto* hx = x->getChildByName ("HOSTED")) { auto ht = juce::ValueTree::fromXml (*hx); readHosted (ht, true); }
        else { juce::ValueTree none ("HOSTED"); readHosted (none, true); }
        if (auto* fx = x->getChildByName ("FXSTORES")) readFxStores (juce::ValueTree::fromXml (*fx));
    }
    if (auto* prm = x->getChildByName ("PARAMS"))
    {
        const NoLink nl (noLink);
        fxAction ([this, prm, full]
        {
            if (full) for (int s = 0; s < fxs::numSlots; ++s) setParam (slotId (s, "Type"), 0.0f);   // places vides du preset = vides
            for (int i = 0; i < prm->getNumAttributes(); ++i)
                setParam (prm->getAttributeName (i), (float) prm->getAttributeValue (i).getDoubleValue());
        });
    }
    refreshTuneMemory();
    fxChain.ensure (slotParams);
    // effets d'un preset d'arpège "usine" : retirés si on change de son ensuite (un preset à toi garde toujours ses effets)
    presetFxLoaded = x->getBoolAttribute ("factory", false);
    return true;
}

//==============================================================================
// Son par défaut au lancement
bool VariantProcessor::isEmptyInstrument() const
{
    for (int l = 0; l < maxLayers; ++l) if (getLayerFile (l) != juce::File()) return false;
    for (auto& h : hostedSlots) if (h.isLoaded()) return false;
    return ! wtSynth.enabled.load();
}

juce::File VariantProcessor::defaultSoundFile() const
{
    // son choisi par l'utilisateur (clic droit dans la bibliothèque > « Son au lancement »), sinon un piano / des keys de la banque
    const auto chosen = library::resolveRenamed (juce::File (library::settings().getValue ("defaultSound")));
    if (chosen.existsAsFile()) return chosen;
    const auto cats = library::categories();
    for (auto* key : { "piano", "key", "rhodes", "pluck", "synth" })
        for (auto& c : cats)
            if (c.name.toLowerCase().contains (key))
            {
                auto files = library::soundsIn (c.folder);
                juce::Array<juce::File> plain;
                for (auto& f : files) if (! f.hasFileExtension ("sf2")) plain.add (f);
                if (! plain.isEmpty())
                {
                    // de préférence un son "Variant ..." (fait pour), sinon le premier
                    for (auto& f : plain) if (f.getFileName().startsWithIgnoreCase ("variant")) return f;
                    return plain.getFirst();
                }
            }
    for (auto& c : cats) { auto files = library::soundsIn (c.folder); if (! files.isEmpty()) return files.getFirst(); }
    return {};
}

void VariantProcessor::loadDefaultSoundIfEmpty()
{
    if (stateWasRestored || restoringState || ! isEmptyInstrument()) return;
    if (! library::settings().getBoolValue ("defaultSoundOn", true)) return;
    const auto f = defaultSoundFile();
    if (! f.existsAsFile()) return;
    restoringState = true;   // pas d'étape d'annulation pour le son de départ
    loadSampleImpl (f, 0, true);
    restoringState = false;
    applyAutoFx (0);   // le son de départ a aussi ses effets (s il en a besoin)
    history.clear(); future.clear();   // (rien à annuler au lancement)
    selectedLayer = 0;
}

//==============================================================================
// Historique global
void VariantProcessor::pushHistory (std::function<void()> undoFn, std::function<void()> redoFn)
{
    if (inHistory) return;
    history.push_back ({ std::move (undoFn), std::move (redoFn) });
    if (history.size() > 200) history.erase (history.begin());
    future.clear();
}

bool VariantProcessor::undoAny()
{
    if (history.empty()) return false;
    auto s = std::move (history.back()); history.pop_back();
    inHistory = true; if (s.undo) s.undo(); inHistory = false;
    future.push_back (std::move (s));
    return true;
}

bool VariantProcessor::redoAny()
{
    if (future.empty()) return false;
    auto s = std::move (future.back()); future.pop_back();
    inHistory = true; if (s.redo) s.redo(); inHistory = false;
    history.push_back (std::move (s));
    return true;
}

void VariantProcessor::sliceHistoryMark()
{
    auto s = getSliceSound();
    sliceMarkBefore = s != nullptr ? s->starts : std::vector<int>();
}

void VariantProcessor::sliceHistoryCommit()
{
    auto s = getSliceSound();
    if (s == nullptr || s->starts == sliceMarkBefore || sliceMarkBefore.empty()) return;
    auto before = sliceMarkBefore, after = s->starts;
    pushHistory ([this, before] { installSlices (before, true); }, [this, after] { installSlices (after, true); });
    sliceMarkBefore.clear();
}

void VariantProcessor::setSlicePattern (std::vector<gen::Note> p, double len)
{
    {
        const juce::ScopedLock l (slicePatLock);
        slicePattern = std::move (p);
        slicePatternLen = len;
    }
    ++slicePatternVersion;
}

void VariantProcessor::clearSlicePattern()
{
    preview.stop();
    const auto old = getSlicePattern(); const double oldLen = getSlicePatternLength();
    if (old.empty()) return;
    pushHistory ([this, old, oldLen] { setSlicePattern (old, oldLen); }, [this] { setSlicePattern ({}, 4.0); });
    setSlicePattern ({}, 4.0);
}

static void notesTouched (VariantProcessor& p)
{
    p.notesEdited = true;
    p.guideVersion.fetch_add (1);
    if (p.isPreviewing()) p.previewGuide (true);
}

int VariantProcessor::addGuideNote (double start, int pitch, double length, int part)
{
    pushUndo();
    notes.push_back ({ juce::jlimit (0.0, prog.lengthBeats - 0.25, start), length, juce::jlimit (0, 127, pitch), 0.8f, part });
    notesTouched (*this);
    return (int) notes.size() - 1;
}

void VariantProcessor::moveGuideNote (int i, double start, int pitch)
{
    if (! juce::isPositiveAndBelow (i, (int) notes.size())) return;
    auto& n = notes[(size_t) i];
    n.start = juce::jlimit (0.0, prog.lengthBeats - 0.125, start);
    n.pitch = juce::jlimit (0, 127, pitch);
    notesTouched (*this);
}

void VariantProcessor::resizeGuideNote (int i, double length)
{
    if (! juce::isPositiveAndBelow (i, (int) notes.size())) return;
    auto& n = notes[(size_t) i];
    n.length = juce::jlimit (0.125, prog.lengthBeats - n.start, length);
    notesTouched (*this);
}

void VariantProcessor::toggleGuideSlide (int i)
{
    if (! juce::isPositiveAndBelow (i, (int) notes.size())) return;
    pushUndo();
    notes[(size_t) i].slide = ! notes[(size_t) i].slide;
    notesTouched (*this);
}

void VariantProcessor::deleteGuideNote (int i)
{
    if (! juce::isPositiveAndBelow (i, (int) notes.size())) return;
    pushUndo();
    notes.erase (notes.begin() + i);
    notesTouched (*this);
}

void VariantProcessor::setGuideNoteVelocity (int i, float vel)
{
    if (! juce::isPositiveAndBelow (i, (int) notes.size())) return;
    notes[(size_t) i].vel = juce::jlimit (0.05f, 1.0f, vel);
    notesTouched (*this);
}

//==============================================================================
// Outils d'analyse (en arrière-plan)
void VariantProcessor::analyseFile (const juce::File& f)
{
    if (analysisBusy.exchange (true)) return;
    analysisSource = f;
    pool.addJob ([this, f]
    {
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        double secs = 0;
        auto x = analysis::loadMono (fm, f, secs);
        AnalysisResult r;
        r.name = f.getFileNameWithoutExtension();
        r.seconds = secs;
        if (! x.empty())
        {
            const auto t = analysis::detectTempo (x);
            r.bpm = t.bpm; r.bpmConfidence = t.confidence;
            r.keys = analysis::detectKey (x);
        }
        { const juce::ScopedLock l (analysisLock); analysisRes = r; analysisAudio = std::move (x); }
        if (f == sliceFile) { sliceBpm.store (pickSliceBpm (r.bpm)); if ((int) pv ("sliceMode") == 2 && ! sliceManual) { slicesDirty = true; triggerAsyncUpdate(); } }
        analysisBusy.store (false);
        analysisVersion.fetch_add (1);
    });
}

void VariantProcessor::analyseLayer (int layer)
{
    const auto f = getLayerFile (layer);
    if (f.existsAsFile()) analyseFile (f);
}

void VariantProcessor::convertToMidi (int mode, float sensitivity, int quantize)
{
    if (analysisBusy.exchange (true)) return;
    pool.addJob ([this, mode, sensitivity, quantize]
    {
        std::vector<float> x;
        double bpm = 120;
        { const juce::ScopedLock l (analysisLock); x = analysisAudio; if (analysisRes.bpm > 0) bpm = analysisRes.bpm; }
        auto raw = mode == 0 ? analysis::melodyToMidi (x, sensitivity) : analysis::polyToMidi (x, sensitivity);
        static const double grids[] = { 0.0, 0.25, 0.5, 1.0 / 3.0 };
        analysis::toBeats (raw, bpm, grids[juce::jlimit (0, 3, quantize)]);
        std::vector<gen::Note> out;
        double end = 0;
        for (auto& n : raw) { out.push_back ({ n.start, n.length, n.pitch, n.vel, 0 }); end = juce::jmax (end, n.start + n.length); }
        {
            const juce::ScopedLock l (analysisLock);
            analysisRes.midi = std::move (out);
            analysisRes.midiLength = std::ceil (juce::jmax (4.0, end) / 4.0) * 4.0;
        }
        analysisBusy.store (false);
        analysisVersion.fetch_add (1);
    });
}

void VariantProcessor::previewAnalysisAudio()
{
    if (isFilePreviewing()) stopFilePreview();
    else if (analysisSource.existsAsFile()) previewFile (analysisSource);
}

void VariantProcessor::previewAnalysisMidi (bool play)
{
    if (! play) { preview.stop(); return; }
    const auto r = getAnalysis();
    auto seq = std::make_unique<PreviewPlayer::Sequence>();
    seq->lengthBeats = juce::jmax (4.0, r.midiLength);
    const auto s = gen::seqFromNotes (r.midi, { true, true, true });
    for (int i = 0; i < s.getNumEvents(); ++i)
    {
        const auto& m = s.getEventPointer (i)->message;
        if (m.isNoteOnOrOff()) seq->events.push_back ({ m.getTimeStamp() / gen::ppq, m });
    }
    std::stable_sort (seq->events.begin(), seq->events.end(), [] (const PreviewPlayer::Event& a, const PreviewPlayer::Event& b) { return a.beat < b.beat; });
    preview.setSequence (std::move (seq));
}

juce::File VariantProcessor::exportAnalysisMidi()
{
    const auto r = getAnalysis();
    if (r.midi.empty()) return {};
    const auto file = getExportFolder().getChildFile (juce::File::createLegalFileName (r.name + "_audio-midi.mid"));
    return gen::writeMidiFile (gen::seqFromNotes (r.midi, { true, true, true }), r.midiLength, r.name, file) ? file : juce::File();
}

void VariantProcessor::sendAnalysisToGuide()
{
    const auto r = getAnalysis();
    if (r.midi.empty()) return;
    pushUndo();
    prog.lengthBeats = juce::jmin (64.0, r.midiLength);
    notes.clear();
    for (auto& n : r.midi) if (n.start < prog.lengthBeats) notes.push_back (n);
    genSettings.chords = true;
    notesEdited = true;
    guideVersion.fetch_add (1);
}

//==============================================================================
// Clavier d'ordinateur : accords par touche
std::vector<int> VariantProcessor::keyboardChord (int index) const
{
    const int key = getKey(), scale = getScale();
    const int base = kbOctave * 12 + key;
    if (index < 10)
    {
        // touches A..P : tous les degrés de la gamme choisie, chacun avec une couleur différente
        // (septièmes, neuvièmes, sus, renversements) mais toujours dans la gamme
        struct Recipe { int degree, size, inversion, sus; };   // sus : 0 normal, 2 = sus2, 4 = sus4
        static const Recipe r[] = { { 0, 4, 0, 0 }, { 1, 4, 0, 0 }, { 2, 3, 1, 0 }, { 3, 5, 0, 0 }, { 4, 4, 1, 0 },
                                    { 5, 5, 0, 0 }, { 6, 3, 2, 0 }, { 0, 5, 2, 0 }, { 3, 3, 1, 2 }, { 4, 3, 0, 4 } };
        const auto& rc = r[index];
        const auto& sc = theory::scaleAt (scale);
        const int nDeg = theory::degreeCount (scale);
        auto c = theory::degreeChord (rc.degree % juce::jmax (1, nDeg), key, scale, rc.size, base - key);
        std::sort (c.begin(), c.end());
        if (rc.sus != 0 && c.size() >= 3)
        {
            // la tierce devient la seconde ou la quarte de la gamme
            const int root = c[0];
            int target = root + (rc.sus == 2 ? 2 : 5);
            target = theory::snapToScale (target, key, sc);
            if (target != root && target != c[2]) c[1] = target;
            std::sort (c.begin(), c.end());
        }
        for (int k = 0; k < rc.inversion % (int) c.size(); ++k) { c.front() += 12; std::sort (c.begin(), c.end()); }
        // registre : centré autour de l'octave choisie
        double m = 0; for (int x : c) m += x; m /= (double) juce::jmax ((size_t) 1, c.size());
        while (m > base + 12) { for (auto& x : c) x -= 12; m -= 12; }
        while (m < base - 2)  { for (auto& x : c) x += 12; m += 12; }
        return theory::applyVoicing (c, (int) pv ("voicing"), 0);
    }
    // touches noires : couleurs hors gamme
    static const int offsets[] = { 2, 4, 9, 1, 5, 8, 10 };
    static const std::vector<int> ivs[] = { { 0, 4, 7, 10, 14 }, { 0, 4, 7, 10, 13 }, { 0, 4, 7, 10, 15 },
                                            { 0, 4, 7, 10, 14 }, { 0, 3, 7, 9 }, { 0, 4, 7, 11, 14 }, { 0, 4, 7, 10, 14 } };
    const int k = juce::jlimit (0, 6, index - 10);
    std::vector<int> out;
    const int root = base + offsets[k] - (offsets[k] > 6 ? 12 : 0);
    for (int iv : ivs[k]) out.push_back (root + iv);
    return out;
}

//==============================================================================
// Enregistrement
void VariantProcessor::armRecording (bool on)
{
    if (on)
    {
        const juce::SpinLock::ScopedLockType l (recLock);
        recEvents.clear();
        recStarted.store (false);
        recBeats.store (0.0);
        recArmed.store (true);
        return;
    }

    recArmed.store (false);
    std::vector<RecEvent> ev;
    {
        const juce::SpinLock::ScopedLockType l (recLock);
        ev = recEvents;
        recEvents.clear();
        recStarted.store (false);
    }
    if (ev.empty()) return;

    static const double grids[] = { 0.0, 0.25, 0.5 };
    const double grid = grids[juce::jlimit (0, 2, recQuantize)];
    std::vector<gen::Note> rec;
    std::array<int, 128> open;
    open.fill (-1);
    double last = 0;
    for (auto& e : ev)
    {
        last = juce::jmax (last, e.beat);
        if (e.on)
        {
            if (open[(size_t) e.note] >= 0) rec[(size_t) open[(size_t) e.note]].length = e.beat - rec[(size_t) open[(size_t) e.note]].start;
            open[(size_t) e.note] = (int) rec.size();
            rec.push_back ({ e.beat, 0.25, e.note, e.vel, 0 });
        }
        else if (open[(size_t) e.note] >= 0)
        {
            auto& n = rec[(size_t) open[(size_t) e.note]];
            n.length = juce::jmax (0.05, e.beat - n.start);
            open[(size_t) e.note] = -1;
        }
    }
    for (int i = 0; i < 128; ++i)
        if (open[(size_t) i] >= 0)
            rec[(size_t) open[(size_t) i]].length = juce::jmax (0.25, last - rec[(size_t) open[(size_t) i]].start + 0.25);

    double end = 0;
    for (auto& n : rec)
    {
        if (grid > 0)
        {
            const double s = std::round (n.start / grid) * grid;
            const double e = juce::jmax (s + grid, std::round ((n.start + n.length) / grid) * grid);
            n.start = s;
            n.length = e - s;
        }
        end = juce::jmax (end, n.start + n.length);
    }

    pushUndo();
    prog.lengthBeats = juce::jlimit (4.0, 64.0, std::ceil (end / 4.0) * 4.0);
    notes.clear();
    for (auto& n : rec) if (n.start < prog.lengthBeats) notes.push_back (n);
    genSettings.chords = true;
    notesEdited = true;
    guideVersion.fetch_add (1);
}

//==============================================================================
// Accords construits autour des notes gardées (ou des notes fondamentales posées)
void VariantProcessor::buildChordsFromNotes (int variant)
{
    // tes notes restent : des accords sont posés en dessous (voir Harmonizer.h)
    std::vector<gen::Note> src;
    if (! harmonySrc.empty() && notes.size() == harmonyOut.size()
        && std::equal (notes.begin(), notes.end(), harmonyOut.begin(), [] (const gen::Note& a, const gen::Note& b)
                       { return a.pitch == b.pitch && std::abs (a.start - b.start) < 1.0e-6 && std::abs (a.length - b.length) < 1.0e-6 && a.part == b.part; }))
        src = harmonySrc;   // on relance sur les notes de départ, pas sur le résultat
    else
        src = notes;
    if (src.empty()) return;
    pushUndo();
    auto h = gen::harmonize (src, genSettings.key, genSettings.scaleIdx, genSettings.richness, genSettings.bass, variant);
    prog.chords = h.chords;
    prog.lengthBeats = juce::jmax (prog.lengthBeats, h.lengthBeats);
    notes = h.notes;
    notesEdited = true;
    std::sort (src.begin(), src.end(), [] (const gen::Note& a, const gen::Note& b) { return a.start < b.start; });
    harmonySrc = src;
    harmonyOut = h.notes;
    renderGuide();
}

//==============================================================================
juce::AudioProcessorEditor* VariantProcessor::createEditor()
{
   #ifdef VARIANT_SINGLE_FX
    return createSingleFxEditor (*this);   // effet seul : juste sa carte
   #else
    return new VariantEditor (*this);
   #endif
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VariantProcessor();
}

//==============================================================================
// Slicer
bool VariantProcessor::loadSliceFile (const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        return false;
    const double sr = reader->sampleRate;
    const int len = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) (sr * 120.0));
    const int ch = (int) juce::jlimit (1u, 2u, reader->numChannels);
    juce::AudioBuffer<float> data (ch, len);
    reader->read (&data, 0, len, 0, true, ch > 1);
    sliceSource = std::move (data);
    sliceShared = nullptr;
    sliceRate = sr;
    sliceName = file.getFileNameWithoutExtension();
    sliceFile = file;
    chopSettings.resetAll();   // nouveau sample : réglages des chops remis à zéro
    sliceLayer = -1;
    sliceManual = false;
    sliceZones.clear();
    sliceRegA = 0; sliceRegB = -1;
    sliceBpmManual.store (false);
    sliceNamedBpm.store (nameBpmOf (file));   // BPM dans le nom : c'est le bon
    sliceBpm.store (sliceNamedBpm.load());
    recomputeSlices();
    analyseFile (file);
    return true;
}

bool VariantProcessor::useLayerForSlices (int layer)
{
    layer = juce::jlimit (0, maxLayers - 1, layer);
    if (sourceBuffers[(size_t) layer].getNumSamples() <= 0) return false;
    sliceSource.makeCopyOf (sourceBuffers[(size_t) layer]);
    sliceShared = nullptr;
    sliceRate = sourceRates[(size_t) layer];
    sliceName = getLayerFile (layer).getFileNameWithoutExtension();
    sliceFile = juce::File();
    chopSettings.resetAll();
    sliceLayer = layer;
    sliceManual = false;
    sliceZones.clear();
    sliceRegA = 0; sliceRegB = -1;
    sliceBpmManual.store (false);
    sliceNamedBpm.store (nameBpmOf (getLayerFile (layer)));
    sliceBpm.store (sliceNamedBpm.load());
    recomputeSlices();
    analyseBuffer (sliceSource, sliceRate, sliceName);
    return true;
}

bool VariantProcessor::sliceAnalysedSound()
{
    juce::File f;
    { const juce::ScopedLock l (analysisLock); f = analysisSource; }
    return f.existsAsFile() && loadSliceFile (f);
}

void VariantProcessor::setSliceRegion (int a, int b)
{
    const int len = sliceSource.getNumSamples();
    if (len <= 0) return;
    a = juce::jlimit (0, len - 1, a);
    b = b < 0 ? len : juce::jlimit (a + 1, len, b);
    if (b - a < (int) (0.05 * sliceRate)) return;
    sliceRegA = a; sliceRegB = b >= len ? -1 : b;
    sliceManual = false;
    recomputeSlices();
}

void VariantProcessor::recomputeSlices()
{
    if (sliceSource.getNumSamples() <= 0) return;
    if (sliceZoneMode && ! sliceZones.empty()) { applySliceZones(); return; }
    const int count = (int) pv ("sliceCount");
    const int mode = (int) pv ("sliceMode");
    const bool fine = pv ("sliceFine") > 0.5f;
    const auto [a, b] = getSliceRegion();
    // on ne découpe que la zone choisie (les stems / passages en trop restent hors des chops)
    juce::AudioBuffer<float> sub (sliceSource.getNumChannels(), juce::jmax (1, b - a));
    for (int c = 0; c < sub.getNumChannels(); ++c) sub.copyFrom (c, 0, sliceSource, c, a, sub.getNumSamples());
    auto starts = mode == 1 ? slicer::gridSlices (sub, count)
                : mode == 2 ? slicer::tempoSlices (sub, sliceRate, sliceBpm.load(), count)
                            : slicer::detectTransients (sub, sliceRate, pv ("sliceSens"), count, sliceBpm.load(), fine);
    const double regionSec = (double) sub.getNumSamples() / sliceRate;
    // pas de tranches minuscules (touches quasi inutilisables) : au moins 0,18 s (0,1 s en mode fin)
    const double minLen = juce::jlimit (fine ? 0.1 : 0.18, 0.5, regionSec / juce::jmax (1, count) * 0.7);
    starts = slicer::enforceMinLength (std::move (starts), sub.getNumSamples(), sliceRate, minLen);
    for (auto& s : starts) s += a;
    installSlices (std::move (starts), false);
}

void VariantProcessor::installSlices (std::vector<int> starts, bool manual)
{
    const int len = sliceSource.getNumSamples();
    if (len <= 0) return;
    std::sort (starts.begin(), starts.end());
    starts.erase (std::unique (starts.begin(), starts.end()), starts.end());
    const auto [ra, rb] = getSliceRegion();
    starts.erase (std::remove_if (starts.begin(), starts.end(), [ra = ra, rb = rb] (int s) { return s < ra || s >= rb - 64; }), starts.end());
    if (starts.empty() || starts.front() != ra) starts.insert (starts.begin(), ra);
    if ((int) starts.size() > slicer::maxSlices) starts.resize ((size_t) slicer::maxSlices);

    slicer::SliceSound::Ptr s = new slicer::SliceSound();
    if (sliceShared == nullptr) sliceShared = std::make_shared<const juce::AudioBuffer<float>> (sliceSource);
    s->data = sliceShared;
    s->rate = sliceRate;
    s->starts = std::move (starts);
    s->endSample = rb;
    s->name = sliceName;
    s->computeLevels();   // « Volumes égalisés » (calculé ici, pas pendant le son)
    sliceManual = manual;

    sliceSynth.clearSounds();
    sliceSynth.addSound (s.get());
    {
        const juce::SpinLock::ScopedLockType l (sliceLock);
        if (sliceSound != nullptr) retiredSlices.add (sliceSound);
        sliceSound = s;
    }
    sliceCountNow.store (s->count());
    ++sliceVersion;
}

void VariantProcessor::setSliceZoneMode (bool on)
{
    sliceZoneMode = on;
    if (on && ! sliceZones.empty()) applySliceZones();
    else recomputeSlices();
}

void VariantProcessor::setSliceZones (std::vector<std::pair<int, int>> z, bool undoable)
{
    const auto before = sliceZones;
    const int len = sliceSource.getNumSamples();
    const auto mono = slicer::toMono (sliceSource);
    std::vector<std::pair<int, int>> clean;
    for (auto p : z)
    {
        int a = juce::jlimit (0, juce::jmax (0, len - 1), juce::jmin (p.first, p.second));
        int b = juce::jlimit (0, len, juce::jmax (p.first, p.second));
        a = slicer::snapToZero (mono.data(), len, a, 200);
        if (b - a < (int) (0.04 * sliceRate)) continue;
        clean.push_back ({ a, b });
    }
    std::sort (clean.begin(), clean.end());
    if ((int) clean.size() > slicer::maxSlices) clean.resize ((size_t) slicer::maxSlices);
    sliceZones = clean;
    sliceZoneMode = true;
    applySliceZones();
    if (undoable && before != clean)
        pushHistory ([this, before] { setSliceZones (before, false); }, [this, clean] { setSliceZones (clean, false); });
}

void VariantProcessor::applySliceZones()
{
    const int len = sliceSource.getNumSamples();
    if (len <= 0) return;
    if (sliceZones.empty()) { recomputeSlices(); return; }
    slicer::SliceSound::Ptr s = new slicer::SliceSound();
    if (sliceShared == nullptr) sliceShared = std::make_shared<const juce::AudioBuffer<float>> (sliceSource);
    s->data = sliceShared;
    s->rate = sliceRate;
    for (auto& z : sliceZones) { s->starts.push_back (z.first); s->ends.push_back (z.second); }
    s->name = sliceName;
    s->computeLevels();   // « Volumes égalisés » (calculé ici, pas pendant le son)
    sliceSynth.clearSounds();
    sliceSynth.addSound (s.get());
    {
        const juce::SpinLock::ScopedLockType l (sliceLock);
        if (sliceSound != nullptr) retiredSlices.add (sliceSound);
        sliceSound = s;
    }
    sliceCountNow.store (s->count());
    ++sliceVersion;
}

void VariantProcessor::addSliceMarker (int sample)
{
    auto s = getSliceSound();
    if (s == nullptr || s->count() >= slicer::maxSlices) return;
    sliceHistoryMark();
    auto st = s->starts;
    const auto mono = slicer::toMono (sliceSource);
    st.push_back (slicer::snapToZero (mono.data(), (int) mono.size(), sample, 200));
    installSlices (st, true);
    sliceHistoryCommit();
}

void VariantProcessor::removeSliceMarker (int index)
{
    auto s = getSliceSound();
    if (s == nullptr || index <= 0 || index >= s->count()) return;   // la tranche 1 commence toujours au début
    auto st = s->starts;
    sliceHistoryMark();
    st.erase (st.begin() + index);
    installSlices (st, true);
    sliceHistoryCommit();
}

void VariantProcessor::moveSliceMarker (int index, int sample)
{
    auto s = getSliceSound();
    if (s == nullptr || index <= 0 || index >= s->count()) return;
    auto st = s->starts;
    const int minGap = juce::jmax (256, (int) (0.08 * sliceRate));   // pas de tranche minuscule
    const int lo = st[(size_t) index - 1] + minGap;
    const int hi = index + 1 < (int) st.size() ? st[(size_t) index + 1] - minGap : getSliceRegion().second - minGap;
    if (hi <= lo) return;
    st[(size_t) index] = juce::jlimit (lo, hi, sample);
    installSlices (st, true);
}

// Stretch du Sampler : tout le sample est étiré en arrière-plan (phase vocoder) au tempo de FL et à la transpo choisie
void VariantProcessor::updateSliceStretch()
{
    if (pv ("sliceStretch") < 0.5f || sliceShared == nullptr || sliceStretchBusy.load()) return;
    const double factor = 1.0 / juce::jmax (0.05, (double) sliceTimeRate.load());
    const int semis = (int) std::round (pv ("sliceTune"));
    if (auto* v = sliceStretchVer.load(); v != nullptr && v->src == sliceShared.get() && std::abs (v->factor - factor) < 0.01 && v->semis == semis) return;
    sliceStretchBusy.store (true);
    auto src = sliceShared;   // gardé vivant pendant le calcul
    const double rate = sliceRate;
    std::weak_ptr<int> alive = sliceAlive;
    auto running = bgRunning;
    running->fetch_add (1);
    juce::Thread::launch ([this, alive, src, rate, factor, semis, running]
    {
        auto ver = std::make_shared<std::unique_ptr<slicer::StretchVer>> (std::make_unique<slicer::StretchVer>());
        (*ver)->buf = std::make_unique<juce::AudioBuffer<float>> (stretch::render (*src, rate, semis, factor, [alive] { return alive.expired(); }));
        if (alive.expired()) { running->fetch_sub (1); return; }   // plugin fermé : on s arrête tout de suite
        (*ver)->src = src.get(); (*ver)->factor = factor; (*ver)->semis = semis;
        juce::MessageManager::callAsync ([this, alive, ver, src]
        {
            if (alive.expired()) return;
            sliceStretchBusy.store (false);
            if (src != sliceShared) return;   // autre sample entre-temps
            sliceStretchVer.store ((*ver).get());
            sliceVers.push_back (std::move (*ver));
            while (sliceVers.size() > 4) sliceVers.erase (sliceVers.begin());   // les plus anciennes (plus lues depuis longtemps)
        });
        running->fetch_sub (1);
    });
}

// « Taper mes chops » : le sample défile, chaque appui (Espace ou bouton) pose une coupe à cet endroit
void VariantProcessor::startTapChop()
{
    if (sliceSource.getNumSamples() <= 0) return;
    sliceHistoryMark();
    const auto [a, b] = getSliceRegion();
    juce::ignoreUnused (b);
    installSlices ({ a }, true);   // on repart d'une page blanche : seulement le début de la zone
    tapPos.store ((double) a);
    tapSession = true;
    tapPlaying.store (true);
}

void VariantProcessor::tapChop()
{
    if (! tapPlaying.load() || sliceSource.getNumSamples() <= 0) return;
    // on tape toujours un peu en retard : on recule de 25 ms, puis on cale la coupe sur le début de l'attaque la plus proche
    int p = (int) tapPos.load() - (int) (0.025 * sliceRate);
    const auto mono = slicer::toMono (sliceSource);
    const int len = (int) mono.size();
    const int from = juce::jmax (0, p - (int) (0.06 * sliceRate)), to = juce::jmin (len - 1, p + (int) (0.06 * sliceRate));
    const int w = juce::jmax (16, (int) (0.002 * sliceRate));
    float bestRise = 0.0f; int bestAt = -1;
    for (int i = from + w; i + w < to; i += w / 2)
    {
        float e0 = 0.0f, e1 = 0.0f;
        for (int j = 0; j < w; ++j) { e0 = juce::jmax (e0, std::abs (mono[(size_t) (i - w + j)])); e1 = juce::jmax (e1, std::abs (mono[(size_t) (i + j)])); }
        const float rise = e1 - e0 * 1.5f;
        if (rise > bestRise) { bestRise = rise; bestAt = i - w / 2; }
    }
    if (bestAt >= 0) p = bestAt;
    p = slicer::snapToZero (mono.data(), len, juce::jlimit (0, len - 1, p), (int) (0.003 * sliceRate));
    addSliceMarker (p);
}

void VariantProcessor::stopTapChop()
{
    const bool was = tapPlaying.exchange (false);
    if (was) slicePlayPos.store (-1.0f);   // la tête de lecture du défilement disparaît
    if (! tapSession) return;
    tapSession = false;
    sliceHistoryCommit();
}

void VariantProcessor::playSlice (int index, bool on)
{
    if (index < 0 || index >= slicer::maxSlices) return;
    auto m = on ? juce::MidiMessage::noteOn (15, slicer::base() + index, 0.85f) : juce::MidiMessage::noteOff (15, slicer::base() + index);
    m.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
    uiMidi.addMessageToQueue (m);
}

//==============================================================================
// Obliterate
void VariantProcessor::renderExplosion (juce::AudioBuffer<float>& out, double sr)
{
    // explosion façon console 8 bits : bruit LFSR dont l'horloge descend + "boum" carré grave,
    // le tout quantifié sur 4 bits et échantillonné-bloqué pour le grain rétro
    const int len = (int) (sr * 1.5);
    out.setSize (2, len);
    out.clear();
    juce::uint32 lfsr = 1;
    double clock = 0.0, square = 0.0;
    float noiseBit = 1.0f, held = 0.0f;
    const int hold = juce::jmax (1, (int) (sr / 11025.0));
    for (int i = 0; i < len; ++i)
    {
        const double t = i / sr;
        const double rate = 9000.0 * std::exp (-t * 3.2) + 180.0;          // l'horloge du bruit chute
        clock += rate / sr;
        while (clock >= 1.0)
        {
            clock -= 1.0;
            const juce::uint32 bit = ((lfsr >> 0) ^ (lfsr >> 1)) & 1u;    // LFSR 15 bits (comme la NES)
            lfsr = (lfsr >> 1) | (bit << 14);
            noiseBit = (lfsr & 1u) ? 1.0f : -1.0f;
        }
        const float envNoise = (float) (t < 0.008 ? t / 0.008 : std::exp (-(t - 0.008) * 2.6));
        const double f = 120.0 * std::exp (-t * 6.0) + 38.0;               // boum qui descend
        square += f / sr;
        const float sq = (std::fmod (square, 1.0) < 0.5 ? 1.0f : -1.0f) * (float) std::exp (-t * 7.0);
        const float crackle = (t > 0.15 && t < 1.1 && (i / (int) (sr * 0.045)) % 3 == 0) ? noiseBit * 0.25f * (float) std::exp (-t * 2.0) : 0.0f;
        float x = 0.62f * noiseBit * envNoise + 0.5f * sq + crackle;
        if (i % hold == 0) held = std::round (juce::jlimit (-1.0f, 1.0f, x) * 7.0f) / 7.0f;   // 4 bits
        out.setSample (0, i, held * 0.42f);
        out.setSample (1, i, held * 0.42f);
    }
    // fondu final anti-clic
    const int fade = (int) (sr * 0.05);
    for (int ch = 0; ch < 2; ++ch) out.applyGainRamp (ch, len - fade, fade, 1.0f, 0.0f);
}

void VariantProcessor::obliterate()
{
    const NoLink nl (noLink);
    previewGuide (false);
    stopFilePreview();
    armRecording (false);
    keyboardState.allNotesOff (0);

    // tous les réglages à leur valeur d'origine
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            rp->setValueNotifyingHost (rp->getDefaultValue());

    // sons, slicer, analyse
    for (int l = 0; l < maxLayers; ++l)
    {
        sourceBuffers[(size_t) l].setSize (0, 0);
        const juce::SpinLock::ScopedLockType lock (soundLock);
        layerFiles[(size_t) l] = juce::File();
    }
    selectedLayer = 0;
    regenerateVariations (true);

    sliceSynth.clearSounds();
    {
        const juce::SpinLock::ScopedLockType l (sliceLock);
        if (sliceSound != nullptr) retiredSlices.add (sliceSound);
        sliceSound = nullptr;
    }
    sliceSource.setSize (0, 0);
    sliceShared = nullptr;
    sliceFile = juce::File();
    chopSettings.resetAll();
    sliceLayer = -1;
    sliceManual = false;
    sliceZones.clear();
    sliceRegA = 0; sliceRegB = -1;
    sliceCountNow.store (0);
    lastSlice.store (-1);
    ++sliceVersion;
    {
        const juce::ScopedLock l (analysisLock);
        analysisRes = AnalysisResult();
        analysisAudio.clear();
        analysisSource = juce::File();
    }
    ++analysisVersion;

    // guide MIDI tout neuf
    kbMode = 0; kbOctave = 5;
    undoStack.clear(); redoStack.clear();
    notesEdited = false;
    genSettings = gen::Settings();
    genSettings.seed = (juce::uint32) juce::Random::getSystemRandom().nextInt();
    startEmptyGuide();
    refreshTuneMemory();
    sendChangeMessage();
}

//==============================================================================
// Analyse d'un son déjà en mémoire (couche envoyée au slicer)
void VariantProcessor::analyseBuffer (const juce::AudioBuffer<float>& b, double rate, const juce::String& name)
{
    if (b.getNumSamples() <= 0 || analysisBusy.exchange (true)) return;
    auto copy = std::make_shared<juce::AudioBuffer<float>> (b);
    {
        const juce::ScopedLock l (analysisLock);
        analysisSource = juce::File();
    }
    pool.addJob ([this, copy, rate, name]
    {
        double secs = 0;
        auto x = analysis::fromBuffer (*copy, rate, secs);
        AnalysisResult r;
        r.name = name;
        r.seconds = secs;
        if (! x.empty())
        {
            const auto t = analysis::detectTempo (x);
            r.bpm = t.bpm; r.bpmConfidence = t.confidence;
            r.keys = analysis::detectKey (x);
        }
        { const juce::ScopedLock l (analysisLock); analysisRes = r; analysisAudio = std::move (x); }
        sliceBpm.store (pickSliceBpm (r.bpm));
        if ((int) pv ("sliceMode") == 2 && ! sliceManual) { slicesDirty = true; triggerAsyncUpdate(); }
        analysisBusy.store (false);
        analysisVersion.fetch_add (1);
    });
}

//==============================================================================
// Enregistrement des chops
void VariantProcessor::armSliceRec (bool on)
{
    if (on)
    {
        const juce::SpinLock::ScopedLockType l (sliceRecLock);
        sliceRecEvents.clear();
        sliceRecStarted.store (false);
        sliceRecLen.store (0.0);
        sliceRecArmed.store (true);
        return;
    }
    sliceRecArmed.store (false);
    std::vector<SliceRecEvent> ev;
    {
        const juce::SpinLock::ScopedLockType l (sliceRecLock);
        ev = sliceRecEvents;
        sliceRecEvents.clear();
        sliceRecStarted.store (false);
    }
    if (ev.empty()) return;

    // quantification au 1/16 + durées
    std::vector<gen::Note> pat;
    std::array<int, slicer::maxSlices> open;
    open.fill (-1);
    double last = 0.0;
    for (auto& e : ev)
    {
        if (e.slice < 0 || e.slice >= slicer::maxSlices) continue;
        const double q = std::round (e.beat * 4.0) / 4.0;
        last = juce::jmax (last, q);
        if (e.on)
        {
            open[(size_t) e.slice] = (int) pat.size();
            pat.push_back ({ q, 0.25, slicer::base() + e.slice, juce::jmax (0.3f, e.vel), 0 });
        }
        else if (open[(size_t) e.slice] >= 0)
        {
            auto& nn = pat[(size_t) open[(size_t) e.slice]];
            nn.length = juce::jmax (0.25, q - nn.start);
            open[(size_t) e.slice] = -1;
        }
    }
    double end = 0.0;
    for (auto& nn : pat) end = juce::jmax (end, nn.start + nn.length);
    const double newLen = juce::jlimit (4.0, 64.0, std::ceil (juce::jmax (end, last + 0.25) / 4.0) * 4.0);
    const auto old = getSlicePattern(); const double oldLen = getSlicePatternLength();
    pushHistory ([this, old, oldLen] { setSlicePattern (old, oldLen); }, [this, pat, newLen] { setSlicePattern (pat, newLen); });
    setSlicePattern (std::move (pat), newLen);
}

void VariantProcessor::previewSlicePattern (bool play)
{
    if (! play) { preview.stop(); return; }
    const auto pat = getSlicePattern();
    if (pat.empty()) return;
    auto seq = std::make_unique<PreviewPlayer::Sequence>();
    seq->lengthBeats = getSlicePatternLength();
    for (auto& nn : pat)
    {
        seq->events.push_back ({ nn.start, juce::MidiMessage::noteOn (15, nn.pitch, nn.vel) });
        seq->events.push_back ({ nn.start + nn.length, juce::MidiMessage::noteOff (15, nn.pitch) });
    }
    std::stable_sort (seq->events.begin(), seq->events.end(), [] (const PreviewPlayer::Event& a, const PreviewPlayer::Event& b) { return a.beat < b.beat; });
    preview.setSequence (std::move (seq));
}

juce::File VariantProcessor::exportSlicePatternMidi()
{
    const auto pat = getSlicePattern();
    if (pat.empty()) return {};
    auto s = getSliceSound();
    const auto name = (s != nullptr && s->name.isNotEmpty() ? s->name : juce::String ("chops")) + "_chops";
    const auto file = getExportFolder().getChildFile (juce::File::createLegalFileName (name + ".mid"));
    return gen::writeMidiFile (gen::seqFromNotes (pat, { true, true, true }), getSlicePatternLength(), name, file) ? file : juce::File();
}

juce::File VariantProcessor::renderSlicePatternWav()
{
    const auto pat = getSlicePattern();
    auto snd = getSliceSound();
    if (pat.empty() || snd == nullptr || snd->data == nullptr) return {};

    // rendu hors temps réel des chops enregistrés, au tempo actuel de FL
    const double sr = 44100.0, bpm = juce::jmax (40.0, hostBpm.load());
    const double spb = 60.0 / bpm * sr;
    const int len = (int) (getSlicePatternLength() * spb + sr * 0.5);
    juce::Synthesiser syn;
    std::atomic<int> last { -1 };
    std::atomic<float> pos { -1.0f };
    slicer::SliceParams sp = sliceParams;
    std::atomic<float> speed { sliceSpeed.load() };
    sp.speed = &speed;
    for (int i = 0; i < 16; ++i) syn.addVoice (new slicer::SliceVoice (sp, last, pos));
    syn.addSound (snd.get());
    syn.setCurrentPlaybackSampleRate (sr);
    juce::MidiBuffer mb;
    for (auto& nn : pat)
    {
        mb.addEvent (juce::MidiMessage::noteOn (1, nn.pitch, nn.vel), (int) (nn.start * spb));
        mb.addEvent (juce::MidiMessage::noteOff (1, nn.pitch), juce::jmin (len - 1, (int) ((nn.start + nn.length) * spb)));
    }
    juce::AudioBuffer<float> out (2, len);
    out.clear();
    syn.renderNextBlock (out, mb, 0, len);
    const float peak = out.getMagnitude (0, len);
    if (peak > 0.98f) out.applyGain (0.98f / peak);

    const auto name = (snd->name.isNotEmpty() ? snd->name : juce::String ("chops")) + "_chops_" + juce::String ((int) std::round (bpm)) + "bpm";
    auto file = getExportFolder().getChildFile (juce::File::createLegalFileName (name + ".wav"));
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (file.createOutputStream());
    if (os == nullptr) return {};
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, 2, 24, {}, 0));
    if (w == nullptr) return {};
    os.release();
    w->writeFromAudioSampleBuffer (out, 0, len);
    w.reset();
    return file;
}

bool VariantProcessor::slicePatternToLayer (int layer)
{
    const auto f = renderSlicePatternWav();
    return f.existsAsFile() && loadSample (f, layer, false);
}

void VariantProcessor::slicePatternToGuide()
{
    const auto pat = getSlicePattern();
    if (pat.empty()) return;
    pushUndo();
    prog.lengthBeats = juce::jmin (64.0, getSlicePatternLength());
    notes.clear();
    for (auto& nn : pat) if (nn.start < prog.lengthBeats) notes.push_back (nn);
    genSettings.chords = true;
    notesEdited = true;
    setParam ("slicerOn", 1.0f);
    guideVersion.fetch_add (1);
}

//==============================================================================
// Tempo d'une boucle : écrit dans le nom ("Arp_Cm_124bpm.wav"), sinon déduit de la durée (mesures entières)
float VariantProcessor::guessLoopBpm (const juce::String& fileName, double seconds)
{
    const auto lower = fileName.toLowerCase();
    for (int i = 0; i < lower.length(); ++i)
    {
        if (! juce::CharacterFunctions::isDigit (lower[i]) || (i > 0 && juce::CharacterFunctions::isDigit (lower[i - 1]))) continue;
        int j = i;
        while (j < lower.length() && (juce::CharacterFunctions::isDigit (lower[j]) || lower[j] == '.')) ++j;
        const auto num = lower.substring (i, j);
        const auto rest = lower.substring (j).trimStart();
        const double v = num.getDoubleValue();
        if (rest.startsWith ("bpm") && v >= 50.0 && v <= 220.0) return (float) v;
    }
    if (seconds <= 0.5) return 0.0f;
    double best = 0.0, bestErr = 1.0e9;
    for (double beats : { 2.0, 4.0, 8.0, 16.0, 32.0, 64.0 })
    {
        const double bpm = beats * 60.0 / seconds;
        if (bpm < 70.0 || bpm > 180.0) continue;
        const double err = std::abs (bpm - 120.0);
        if (err < bestErr) { bestErr = err; best = bpm; }
    }
    return (float) best;
}

//==============================================================================
// Goûts de l'utilisateur (mémorisés pour tous les projets)
void VariantProcessor::rateProgression (bool like)
{
    std::vector<int> degs;
    for (auto& c : prog.chords)
        if (std::fmod (c.start, 4.0) < 0.01) degs.push_back (((c.degree % 7) + 7) % 7);
    if (degs.size() < 2) return;
    auto& list = like ? likedSeqs : dislikedSeqs;
    list.push_back (degs);
    if (list.size() > 60) list.erase (list.begin());
    if (like)
    {
        const float n = (float) likedSeqs.size();
        tasteVariety += (pv ("genVariety") - tasteVariety) / n;
        tasteSpice += (pv ("genSpice") - tasteSpice) / n;
    }
    saveTaste();
}

void VariantProcessor::saveTaste() const
{
    auto enc = [] (const std::vector<std::vector<int>>& l)
    {
        juce::StringArray parts;
        for (auto& s : l) { juce::StringArray d; for (int x : s) d.add (juce::String (x)); parts.add (d.joinIntoString (",")); }
        return parts.joinIntoString ("|");
    };
    auto& st = library::settings();
    st.setValue ("tasteLiked", enc (likedSeqs));
    st.setValue ("tasteDisliked", enc (dislikedSeqs));
    st.setValue ("tasteVar", (double) tasteVariety);
    st.setValue ("tasteSpice", (double) tasteSpice);
    st.setValue ("tasteOn", tasteOn);
    st.saveIfNeeded();
}

void VariantProcessor::loadTaste()
{
    auto dec = [] (const juce::String& txt)
    {
        std::vector<std::vector<int>> out;
        juce::StringArray parts; parts.addTokens (txt, "|", "");
        for (auto& p : parts)
        {
            juce::StringArray d; d.addTokens (p, ",", "");
            std::vector<int> s;
            for (auto& x : d) if (x.isNotEmpty()) s.push_back (x.getIntValue());
            if (s.size() >= 2) out.push_back (s);
        }
        return out;
    };
    auto& st = library::settings();
    likedSeqs = dec (st.getValue ("tasteLiked"));
    dislikedSeqs = dec (st.getValue ("tasteDisliked"));
    tasteVariety = (float) st.getDoubleValue ("tasteVar", 0.5);
    tasteSpice = (float) st.getDoubleValue ("tasteSpice", 0.5);
    tasteOn = st.getBoolValue ("tasteOn", true);
}

//==============================================================================
// Édition de notes façon FL Studio (plusieurs notes à la fois)
static std::vector<int> sortedDesc (std::vector<int> idx) { std::sort (idx.begin(), idx.end(), std::greater<int>()); idx.erase (std::unique (idx.begin(), idx.end()), idx.end()); return idx; }

void VariantProcessor::deleteGuideNotes (const std::vector<int>& idx)
{
    if (idx.empty()) return;
    pushUndo();
    for (int i : sortedDesc (idx)) if (juce::isPositiveAndBelow (i, (int) notes.size())) notes.erase (notes.begin() + i);
    notesTouched (*this);
}

void VariantProcessor::transposeGuideNotes (const std::vector<int>& idx, int semis)
{
    if (idx.empty()) return;
    pushUndo();
    for (int i : idx) if (juce::isPositiveAndBelow (i, (int) notes.size())) notes[(size_t) i].pitch = juce::jlimit (0, 127, notes[(size_t) i].pitch + semis);
    notesTouched (*this);
}

void VariantProcessor::shiftGuideNotes (const std::vector<int>& idx, double beats, int semis)
{
    for (int i : idx)
        if (juce::isPositiveAndBelow (i, (int) notes.size()))
        {
            auto& n = notes[(size_t) i];
            n.start = juce::jlimit (0.0, juce::jmax (0.0, prog.lengthBeats - n.length), n.start + beats);
            n.pitch = juce::jlimit (0, 127, n.pitch + semis);
        }
    notesTouched (*this);
}

void VariantProcessor::quantizeGuideNotes (const std::vector<int>& idx, double grid)
{
    if (idx.empty() || grid <= 0.0) return;
    pushUndo();
    for (int i : idx)
        if (juce::isPositiveAndBelow (i, (int) notes.size()))
        {
            auto& n = notes[(size_t) i];
            const double end = n.start + n.length;
            n.start = std::round (n.start / grid) * grid;
            n.length = juce::jmax (grid, std::round ((end - n.start) / grid) * grid);
        }
    notesTouched (*this);
}

void VariantProcessor::legatoGuideNotes (const std::vector<int>& idx)
{
    if (idx.empty()) return;
    pushUndo();
    for (int i : idx)
    {
        if (! juce::isPositiveAndBelow (i, (int) notes.size())) continue;
        auto& n = notes[(size_t) i];
        double next = prog.lengthBeats;
        for (int j : idx)
            if (juce::isPositiveAndBelow (j, (int) notes.size()) && notes[(size_t) j].part == n.part && notes[(size_t) j].start > n.start + 1.0e-6)
                next = juce::jmin (next, notes[(size_t) j].start);
        n.length = juce::jmax (0.125, next - n.start);
    }
    notesTouched (*this);
}

std::vector<int> VariantProcessor::pasteGuideNotes (const std::vector<gen::Note>& clip, double offset)
{
    std::vector<int> added;
    if (clip.empty()) return added;
    pushUndo();
    for (auto n : clip)
    {
        n.start += offset;
        if (n.start >= prog.lengthBeats) continue;
        n.length = juce::jmin (n.length, prog.lengthBeats - n.start);
        notes.push_back (n);
        added.push_back ((int) notes.size() - 1);
    }
    notesTouched (*this);
    return added;
}

void VariantProcessor::splitGuideNote (int i, double beat)
{
    if (! juce::isPositiveAndBelow (i, (int) notes.size())) return;
    auto n = notes[(size_t) i];
    if (beat <= n.start + 0.06 || beat >= n.start + n.length - 0.06) return;
    pushUndo();
    notes[(size_t) i].length = beat - n.start;
    n.length = n.start + n.length - beat;
    n.start = beat;
    notes.push_back (n);
    notesTouched (*this);
}

void VariantProcessor::setGuideNotesVelocity (const std::vector<int>& idx, float vel)
{
    for (int i : idx) if (juce::isPositiveAndBelow (i, (int) notes.size())) notes[(size_t) i].vel = juce::jlimit (0.05f, 1.0f, vel);
    notesTouched (*this);
}

// Importer un .mid (accords, arpège, mélodie exportés d'un autre plugin ou de FL) dans le piano roll
bool VariantProcessor::importMidiFile (const juce::File& f)
{
    juce::FileInputStream in (f);
    if (! in.openedOk()) return false;
    juce::MidiFile mf;
    if (! mf.readFrom (in)) return false;
    const int tpq = mf.getTimeFormat() > 0 ? mf.getTimeFormat() : 960;
    std::vector<gen::Note> out;
    double end = 0.0;
    for (int t = 0; t < mf.getNumTracks(); ++t)
    {
        juce::MidiMessageSequence seq (*mf.getTrack (t));
        seq.updateMatchedPairs();
        for (int i = 0; i < seq.getNumEvents(); ++i)
        {
            auto* e = seq.getEventPointer (i);
            if (! e->message.isNoteOn()) continue;
            const double s = e->message.getTimeStamp() / tpq;
            const double off = e->noteOffObject != nullptr ? e->noteOffObject->message.getTimeStamp() / tpq : s + 0.25;
            out.push_back ({ s, juce::jmax (0.0625, off - s), e->message.getNoteNumber(), e->message.getFloatVelocity(), 0 });
            end = juce::jmax (end, off);
        }
    }
    if (out.empty()) return false;
    pushUndo();
    prog.lengthBeats = juce::jlimit (4.0, 64.0, std::ceil (end / 4.0) * 4.0);
    notes.clear();
    for (auto& n : out) if (n.start < prog.lengthBeats) notes.push_back (n);
    genSettings.chords = true;
    notesEdited = true;
    guideVersion.fetch_add (1);
    return true;
}

//==============================================================================
// Courbes des effets (Liquide, EQ, Glitch, Temps & Volume) : projet et presets
void VariantProcessor::writeFxStores (juce::ValueTree& state)
{
    {
        auto& lc = fxStores.liquid;   // courbes de l'effet Liquide
        const juce::SpinLock::ScopedLockType ll (lc.lock);
        for (int s = 0; s < fxs::numSlots; ++s)
        {
            juce::StringArray pts;
            const auto& e = lc.env[(size_t) s];
            for (int i = 0; i < e.n; ++i) pts.add (juce::String (e.pts[(size_t) i].x, 4) + ":" + juce::String (e.pts[(size_t) i].y, 4));
            state.setProperty ("liq" + juce::String (s), pts.joinIntoString (","), nullptr);
        }
    }
    {
        // courbes de l'égaliseur paramétrique
        auto& es = fxStores.eq;
        const juce::SpinLock::ScopedLockType el (es.lock);
        for (int s = 0; s < fxs::numSlots; ++s)
        {
            juce::StringArray v;
            for (auto& b : es.curves[(size_t) s])
                v.add (juce::String (b.on ? 1 : 0) + ":" + juce::String (b.type) + ":" + juce::String (b.freq, 1) + ":" + juce::String (b.gain, 2) + ":" + juce::String (b.q, 3) + ":" + juce::String (b.slope));
            state.setProperty ("peq" + juce::String (s), v.joinIntoString (","), nullptr);
        }
    }
    {
        {
            auto& gp = fxStores.glitch;
            const juce::SpinLock::ScopedLockType gl (gp.lock);
            for (int s = 0; s < fxs::numSlots; ++s)
            {
                juce::StringArray v;
                for (auto x : gp.perso[(size_t) s].lanes) v.add (juce::String ((int) x));
                state.setProperty ("gpat" + juce::String (s), v.joinIntoString (","), nullptr);
            }
        }
        // enveloppes "Perso" de l'effet Temps & Volume
        auto& ce = fxStores.env;
        const juce::SpinLock::ScopedLockType l (ce.lock);
        for (int s = 0; s < fxs::numSlots; ++s)
            for (int w = 0; w < 2; ++w)
            {
                juce::StringArray pts;
                const auto& e = ce.env[(size_t) s][(size_t) w];
                for (int i = 0; i < e.n; ++i) pts.add (juce::String (e.pts[(size_t) i].x, 4) + ":" + juce::String (e.pts[(size_t) i].y, 4));
                state.setProperty ("genv" + juce::String (s) + "_" + juce::String (w), pts.joinIntoString (","), nullptr);
            }
    }
}

void VariantProcessor::readFxStores (const juce::ValueTree& tree)
{
    {
        auto& lc = fxStores.liquid;
        const juce::SpinLock::ScopedLockType ll (lc.lock);
        for (int s = 0; s < fxs::numSlots; ++s)
        {
            juce::StringArray v; v.addTokens (tree.getProperty ("liq" + juce::String (s)).toString(), ",", "");
            if (v.size() < 2) continue;
            fxs::Envelope e;
            for (auto& t : v) if (e.n < 32) { juce::StringArray xy; xy.addTokens (t, ":", ""); if (xy.size() == 2) e.pts[(size_t) e.n++] = { xy[0].getFloatValue(), xy[1].getFloatValue() }; }
            lc.env[(size_t) s] = e;
        }
        ++lc.version;
    }
    {
        auto& es = fxStores.eq;
        const juce::SpinLock::ScopedLockType el (es.lock);
        for (int s = 0; s < fxs::numSlots; ++s)
        {
            juce::StringArray v; v.addTokens (tree.getProperty ("peq" + juce::String (s)).toString(), ",", "");
            if (v.size() != fxs::eqBandsCount) { es.curves[(size_t) s] = fxs::defaultEq(); ++es.version[(size_t) s]; continue; }
            for (int b = 0; b < fxs::eqBandsCount; ++b)
            {
                juce::StringArray f; f.addTokens (v[b], ":", "");
                if (f.size() < 6) continue;
                auto& bd = es.curves[(size_t) s][(size_t) b];
                bd.on = f[0].getIntValue() != 0; bd.type = f[1].getIntValue(); bd.freq = f[2].getFloatValue();
                bd.gain = f[3].getFloatValue(); bd.q = f[4].getFloatValue(); bd.slope = f[5].getIntValue();
            }
            ++es.version[(size_t) s];
        }
    }
    {
        auto& gp = fxStores.glitch;
        const juce::SpinLock::ScopedLockType gl (gp.lock);
        for (int s = 0; s < fxs::numSlots; ++s)
        {
            juce::StringArray v; v.addTokens (tree.getProperty ("gpat" + juce::String (s)).toString(), ",", "");
            if (v.size() == fxs::NumLanes) for (int l = 0; l < fxs::NumLanes; ++l) gp.perso[(size_t) s].lanes[(size_t) l] = (juce::uint16) v[l].getIntValue();
        }
    }
    {
        auto& ce = fxStores.env;
        const juce::SpinLock::ScopedLockType l (ce.lock);
        for (int s = 0; s < fxs::numSlots; ++s)
            for (int w = 0; w < 2; ++w)
            {
                const auto txt = tree.getProperty ("genv" + juce::String (s) + "_" + juce::String (w)).toString();
                if (txt.isEmpty()) continue;
                juce::StringArray pts; pts.addTokens (txt, ",", "");
                fxs::Envelope e;
                for (auto& p : pts) if (e.n < 32 && p.contains (":")) e.pts[(size_t) e.n++] = { p.upToFirstOccurrenceOf (":", false, false).getFloatValue(), p.fromFirstOccurrenceOf (":", false, false).getFloatValue() };
                if (e.n >= 2) ce.env[(size_t) s][(size_t) w] = e;
            }
    }
}

// VST ouverts dans Variant (chemin, réglages de la couche et état complet du plugin)
void VariantProcessor::writeHosted (juce::ValueTree& state)
{
    for (int hsI = 0; hsI < numHosted; ++hsI)
    {
        auto& hh = hostedSlots[(size_t) hsI];
        if (! hh.isLoaded()) continue;
        juce::ValueTree hs ("Hosted");
        hs.setProperty ("slot", hsI, nullptr);
        hs.setProperty ("path", hh.path, nullptr);
        hs.setProperty ("name", hh.name, nullptr);
        hs.setProperty ("vol", hh.volume.load(), nullptr);
        hs.setProperty ("on", hh.enabled.load(), nullptr);
        hs.setProperty ("pan", hh.panPos.load(), nullptr);
        hs.setProperty ("tr", hh.transpose.load(), nullptr);
        hs.setProperty ("fine", hh.fineCents.load(), nullptr);
        hs.setProperty ("dly", hh.delayMs.load(), nullptr);
        hs.setProperty ("bend", hh.bendRange.load(), nullptr);
        hs.setProperty ("state", hh.getPluginState().toBase64Encoding(), nullptr);
        state.appendChild (hs, nullptr);
    }
}

void VariantProcessor::readHosted (juce::ValueTree& tree, bool unloadMissing)
{
    {
        // plugins ouverts dans Variant : relancés avec leur son (état de chaque plugin)
        std::array<bool, numHosted> seen {};
        for (int ci = tree.getNumChildren(); --ci >= 0;)
        {
            auto hs = tree.getChild (ci);
            if (! hs.hasType ("Hosted")) continue;
            const int si = juce::jlimit (0, numHosted - 1, (int) hs.getProperty ("slot", 0));
            seen[(size_t) si] = true;
            auto& hh = hostedSlots[(size_t) si];
            const juce::File f (hs.getProperty ("path").toString());
            juce::MemoryBlock mb; mb.fromBase64Encoding (hs.getProperty ("state").toString());
            hh.volume.store ((float) hs.getProperty ("vol", 0.8)); hh.enabled.store ((bool) hs.getProperty ("on", true));
            hh.panPos.store ((float) hs.getProperty ("pan", 0.0)); hh.transpose.store ((float) hs.getProperty ("tr", 0.0));
            hh.fineCents.store ((float) hs.getProperty ("fine", 0.0)); hh.delayMs.store ((float) hs.getProperty ("dly", 0.0));
            hh.bendRange.store ((int) hs.getProperty ("bend", 2));
            tree.removeChild (ci, nullptr);
            if (f.exists() && f.getFullPathName() != hh.path)
                juce::MessageManager::callAsync ([&hh, f, mb] { hh.load (f, nullptr, mb); });
            else if (f.exists())
                juce::MessageManager::callAsync ([&hh, mb] { if (auto* p = hh.get()) p->setStateInformation (mb.getData(), (int) mb.getSize()); });
        }
        for (int si = 0; si < numHosted; ++si)
            if (unloadMissing && ! seen[(size_t) si] && hostedSlots[(size_t) si].isLoaded()) { auto* hp = &hostedSlots[(size_t) si]; juce::MessageManager::callAsync ([hp] { hp->unload(); }); }
    }
}

//==============================================================================
// Avant de charger un preset : le son repart exactement de l'état d'ouverture de Variant
// (réglages du son, couches vides, plus de VST, synthé éteint, chaîne d'effets et courbes vides).
// La tonalité / la gamme, le générateur, le slicer et le métronome ne bougent pas.
void VariantProcessor::resetToLaunchState()
{
    const NoLink nl (noLink);
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto id = rp->getParameterID();
            if (! presetParam (id) || id == "key" || id == "scale") continue;
            rp->setValueNotifyingHost (rp->getDefaultValue());
        }
    for (int l = 0; l < maxLayers; ++l) if (getLayerFile (l) != juce::File()) clearLayerImpl (l);
    for (auto& h : hostedSlots) if (h.isLoaded()) h.unload();
    wtSynth.patch.reset();
    wtSynth.setTable (0, wt::builtin (0)); wtSynth.setTable (1, wt::builtin (0));
    wtSynth.enabled.store (false);
    wtSynth.mix.store (0.8f);
    resetFxStores();
    refreshTuneMemory();
    presetFxLoaded = false;
    autoFxSig = {};
}

// courbes et motifs perso des effets (Liquide, enveloppes, Glitch, EQ) remis à zéro
void VariantProcessor::resetFxStores()
{
    {
        const fxs::LiquidCurves fresh;
        const juce::SpinLock::ScopedLockType l (fxStores.liquid.lock);
        fxStores.liquid.env = fresh.env; ++fxStores.liquid.version;
    }
    {
        const fxs::CustomEnvelopes fresh;
        const juce::SpinLock::ScopedLockType l (fxStores.env.lock);
        fxStores.env.env = fresh.env;
    }
    {
        const juce::SpinLock::ScopedLockType l (fxStores.glitch.lock);
        for (auto& g : fxStores.glitch.perso) g = fxs::GlitchPattern();
    }
    {
        const juce::SpinLock::ScopedLockType l (fxStores.eq.lock);
        for (auto& c : fxStores.eq.curves) c = fxs::defaultEq();
        for (auto& v : fxStores.eq.version) ++v;
    }
}

// le son principal = la couche 1, ou le seul son chargé
bool VariantProcessor::isMainSound (int layer) const
{
    if (layer == 0) return true;
    for (int l = 0; l < maxLayers; ++l) if (l != layer && getLayerFile (l) != juce::File()) return false;
    return true;
}

// Nouveau son principal : toutes les options de jeu reviennent à leur réglage de base
// (slide, enveloppe, transpo, accords, arpège, variations, slicer, réglages sampler de la couche).
// On garde : volume général, tonalité et gamme, métronome, guide MIDI, répartition des couches,
// volume / pan de la couche et tout ce qui concerne les autres couches.
void VariantProcessor::resetSoundOptions (int layer)
{
    const NoLink nl (noLink);
    const auto lp = layerId ("L", layer);
    static const char* keep[] = { "gain", "key", "scale", "linkTune", "loopChord", "layerMode", "layerOrder", "layerHuman" };
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto id = rp->getParameterID();
            bool reset = true;
            if (id.length() > 1 && id[0] == 'L' && juce::CharacterFunctions::isDigit (id[1]))
            {
                const auto suffix = id.substring (lp.length());
                reset = id.startsWith (lp) && suffix.isNotEmpty() && ! juce::CharacterFunctions::isDigit (suffix[0])
                        && suffix != "On" && suffix != "Vol" && suffix != "Pan";
            }
            else if (id.startsWith ("fx") || id.startsWith ("root") || id.startsWith ("metro") || id.startsWith ("gen")) reset = false;
            else for (auto* k : keep) if (id == k) reset = false;
            if (reset && rp->getValue() != rp->getDefaultValue()) rp->setValueNotifyingHost (rp->getDefaultValue());
        }
}

//==============================================================================
// Effets auto : quand tu choisis un nouveau son principal, les effets d avant partent tous et la chaîne
// prévue pour ce son est posée tout de suite (petite plate + écho sur un lead, chorus + salle sur une nappe,
// rien sur une basse…). Une couche ajoutée sous un autre son garde les effets du son principal.
juce::String VariantProcessor::fxSignature() const
{
    juce::String s;
    for (int sl = 0; sl < fxs::numSlots; ++sl)
    {
        const int t = getSlotType (sl);
        if (t == 0) continue;
        s << sl << ":" << t << ":" << juce::roundToInt (getParam (slotId (sl, "On"))) << ":" << juce::roundToInt (getParam (slotId (sl, "Mix")) * 1000.0f);
        for (int k = 1; k <= fxs::numP; ++k) s << "," << juce::roundToInt (getParam (slotId (sl, "P" + juce::String (k))) * 1000.0f);
        s << ";";
    }
    return s;
}

void VariantProcessor::applyAutoFx (int layer)
{
    if (! juce::isPositiveAndBelow (layer, maxLayers) || ! isMainSound (layer)) return;
    // option "effets auto" coupée : on repart quand même sans les effets du son d avant
    const auto picks = library::settings().getBoolValue ("autoFx", true)
                         ? polish::effectsFor ((polish::Category) layerCat[(size_t) layer]) : std::vector<polish::FxPick>();
    resetFxStores();
    if (fxSignature().isEmpty() && picks.empty()) { autoFxSig = {}; return; }
    fxAction ([this, picks]
    {
        for (int s = 0; s < fxs::numSlots; ++s) setParam (slotId (s, "Type"), 0.0f);
        int s = 0;
        for (auto& p : picks)
        {
            setParam (slotId (s, "Type"), (float) p.type);
            setParam (slotId (s, "On"), 1.0f);
            applyEffectPreset (s, p.preset);
            setParam (slotId (s, "Mix"), p.mix);
            ++s;
        }
    });
    fxChain.ensure (slotParams);
    autoFxSig = fxSignature();
}

// flèches de la banque (en haut) : son précédent / suivant du même dossier, chargé dans la couche choisie
bool VariantProcessor::stepLayerSound (int layer, int dir)
{
    layer = juce::jlimit (0, maxLayers - 1, layer);
    ensureLibrary();
    auto cur = getLayerFile (layer);
    juce::Array<juce::File> list;
    if (cur.existsAsFile() && ! cur.hasFileExtension ("sf2"))
        for (auto& f : library::soundsIn (cur.getParentDirectory())) if (! f.hasFileExtension ("sf2")) list.add (f);
    if (list.isEmpty())   // couche vide : on part de la 1re catégorie de la banque
        for (auto& c : library::categories()) { for (auto& f : library::soundsIn (c.folder)) if (! f.hasFileExtension ("sf2")) list.add (f); if (! list.isEmpty()) break; }
    if (list.isEmpty()) return false;
    int at = list.indexOf (cur);
    at = at < 0 ? (dir > 0 ? 0 : list.size() - 1) : (at + dir + list.size()) % list.size();
    return loadSample (list[at], layer, true);
}

//==============================================================================
// « Prise » : enregistre ce que tu joues (MIDI) et le son de Variant (WAV, effets compris), démarre à la première note.
// Tout est préparé à l'avance (mémoire réservée) : le son n'est jamais bloqué pendant l'enregistrement.
void VariantProcessor::takeArm()
{
    const juce::SpinLock::ScopedLockType l (takeLock);
    takeRate = currentSampleRate > 0.0 ? currentSampleRate : 44100.0;
    takeAudio.setSize (2, (int) (takeRate * 240.0), false, false, true);   // 4 minutes au plus
    takeEvents.clear();
    takeEvents.reserve (40000);
    takeLen.store (0);
    takeState.store (1);
}

void VariantProcessor::takeStop()
{
    const int s = takeState.load();
    if (s == 1) { takeState.store (0); return; }   // pas encore commencé : on annule
    if (s == 2) takeState.store (takeLen.load() > 0 ? 3 : 0);
}

void VariantProcessor::takeClear()
{
    const juce::SpinLock::ScopedLockType l (takeLock);
    takeState.store (0);
    takeLen.store (0);
    takeEvents.clear();
    takeAudio.setSize (0, 0);
}

void VariantProcessor::takeCaptureMidi (const juce::MidiBuffer& m, juce::int64 blockStartSample, double bpm)
{
    if (! takeLock.tryEnter()) return;
    for (const auto meta : m)
    {
        const auto msg = meta.getMessage();
        if (! msg.isNoteOnOrOff()) continue;
        const auto t = blockStartSample + meta.samplePosition;
        if (takeState.load() == 1)
        {
            if (! msg.isNoteOn()) continue;
            takeStart = t; takeBpm = bpm; takeState.store (2);   // la prise commence à la première note
        }
        if (takeEvents.size() < takeEvents.capacity())
            takeEvents.push_back ({ t - takeStart, msg.getNoteNumber(), msg.getFloatVelocity(), msg.isNoteOn() });
    }
    takeLock.exit();
}

void VariantProcessor::takeCaptureAudio (const juce::AudioBuffer<float>& b, int n, juce::int64 blockStartSample)
{
    if (! takeLock.tryEnter()) return;
    const int from = (int) juce::jlimit<juce::int64> (0, n, takeStart - blockStartSample);   // la note peut tomber au milieu du bloc
    const int pos = takeLen.load(), room = takeAudio.getNumSamples() - pos, cnt = juce::jmin (n - from, room);
    if (cnt > 0)
        for (int ch = 0; ch < 2; ++ch)
            takeAudio.copyFrom (ch, pos, b, juce::jmin (ch, b.getNumChannels() - 1), from, cnt);
    takeLen.store (pos + juce::jmax (0, cnt));
    if (room <= n - from) takeState.store (3);   // 4 minutes atteintes : on s'arrête
    takeLock.exit();
}

juce::File VariantProcessor::exportTakeWav()
{
    if (takeState.load() != 3) return {};
    juce::AudioBuffer<float> copy;
    {
        const juce::SpinLock::ScopedLockType l (takeLock);
        copy.makeCopyOf (takeAudio);
    }
    const int len = juce::jmin (takeLen.load(), copy.getNumSamples());
    if (len <= 0) return {};
    const auto name = (takeSource.load() == 1 ? juce::String ("Variant arpege ") : juce::String ("Variant prise ")) + juce::Time::getCurrentTime().formatted ("%H-%M-%S");
    auto file = getExportFolder().getChildFile (juce::File::createLegalFileName (name + ".wav"));
    file.deleteFile();
    juce::WavAudioFormat wav;
    if (auto os = std::unique_ptr<juce::OutputStream> (file.createOutputStream()))
        if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (os.get(), takeRate, 2, 24, {}, 0)))
        {
            os.release();
            w->writeFromAudioSampleBuffer (copy, 0, len);
        }
    return file;
}

juce::File VariantProcessor::exportTakeMidi()
{
    if (takeState.load() != 3) return {};
    std::vector<TakeEvent> ev;
    double bpm, rate;
    {
        const juce::SpinLock::ScopedLockType l (takeLock);
        ev = takeEvents; bpm = takeBpm; rate = takeRate;
    }
    if (ev.empty() || rate <= 0.0) return {};
    constexpr int ppq = 960;
    auto ticks = [bpm, rate] (juce::int64 s) { return (double) s / rate * (juce::jmax (20.0, bpm) / 60.0) * 960.0; };
    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) (60000000.0 / juce::jmax (20.0, bpm))), 0.0);
    std::array<bool, 128> held {};
    double last = 0.0;
    for (auto& e : ev)
    {
        const double t = ticks (e.sample);
        last = juce::jmax (last, t);
        if (e.on) { seq.addEvent (juce::MidiMessage::noteOn (1, e.note, juce::jlimit (0.01f, 1.0f, e.vel)), t); held[(size_t) e.note] = true; }
        else if (held[(size_t) e.note]) { seq.addEvent (juce::MidiMessage::noteOff (1, e.note), t); held[(size_t) e.note] = false; }
    }
    const double endT = juce::jmax (last, ticks (takeLen.load()));
    for (int k = 0; k < 128; ++k) if (held[(size_t) k]) seq.addEvent (juce::MidiMessage::noteOff (1, k), endT);   // notes encore tenues à l'arrêt
    seq.updateMatchedPairs();
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (ppq);
    mf.addTrack (seq);
    const auto name = (takeSource.load() == 1 ? juce::String ("Variant arpege ") : juce::String ("Variant prise ")) + juce::Time::getCurrentTime().formatted ("%H-%M-%S");
    auto file = getExportFolder().getChildFile (juce::File::createLegalFileName (name + ".mid"));
    file.deleteFile();
    if (auto os = file.createOutputStream()) mf.writeTo (*os);
    return file;
}

//==============================================================================
// BPM d'un fichier d'après son nom (ou son nom d'avant renommage) : « 140bpm », « 140 BPM », « _130_ » (code Splice)
double VariantProcessor::nameBpmOf (const juce::File& f) const
{
    if (f == juce::File()) return 0.0;
    auto fromName = [] (const juce::String& n) -> double
    {
        if (n.isEmpty()) return 0.0;
        if (const float b = guessLoopBpm (n, 0.0); b > 0.0f) return (double) b;
        // nombre seul entre deux « _ » (ex. « T_TSDRX_130_fx… ») : le tempo des packs Splice
        juce::StringArray parts; parts.addTokens (n, "_", "");
        for (int i = 1; i + 1 < parts.size(); ++i)
            if (parts[i].containsOnly ("0123456789") && parts[i].length() >= 2 && parts[i].length() <= 3)
                if (const int v = parts[i].getIntValue(); v >= 60 && v <= 200) return (double) v;
        return 0.0;
    };
    if (const double b = fromName (f.getFileNameWithoutExtension()); b > 0.0) return b;
    return fromName (library::originalName (f));
}

void VariantProcessor::setSliceBpm (double bpm)
{
    if (bpm <= 0.0)
    {
        // retour à l'automatique : nom du fichier, sinon l'analyse
        sliceBpmManual.store (false);
        const auto an = getAnalysis();
        sliceBpm.store (pickSliceBpm (an.bpm));
    }
    else
    {
        sliceBpmManual.store (true);
        sliceBpm.store (juce::jlimit (40.0, 300.0, bpm));
    }
    if ((int) pv ("sliceMode") == 2 && ! sliceManual) { slicesDirty = true; triggerAsyncUpdate(); }   // découpe « au tempo » recalculée
    ++sliceVersion;
}

//==============================================================================
// Automation dans FL des réglages qui n'avaient pas de paramètre (synthé, VST ouverts dans Variant)
void VariantProcessor::setupBridges()
{
    auto add = [this] (const juce::String& id, std::function<float()> get, std::function<void (float)> set)
    {
        auto* rp = apvts.getParameter (id);
        auto* raw = apvts.getRawParameterValue (id);
        if (rp == nullptr || raw == nullptr) return;
        auto b = std::make_unique<Bridge>();
        b->rp = rp; b->raw = raw; b->get = std::move (get); b->set = std::move (set);
        bridges.push_back (std::move (b));
    };
    add ("synOn", [this] { return wtSynth.enabled.load() ? 1.0f : 0.0f; }, [this] (float v) { wtSynth.enabled.store (v > 0.5f); });
    add ("synMix", [this] { return wtSynth.mix.load(); }, [this] (float v) { wtSynth.mix.store (v); });
    for (int i = 0; i < wt::NumParams; ++i)
        add ("syn_" + juce::String (wt::info()[(size_t) i].id), [this, i] { return wtSynth.patch.get (i); }, [this, i] (float v) { wtSynth.patch.set (i, v); });
    for (int s = 0; s < numHosted; ++s)
    {
        auto* h = &hostedSlots[(size_t) s];
        const auto id = "h" + juce::String (s + 1);
        add (id + "On", [h] { return h->enabled.load() ? 1.0f : 0.0f; }, [h] (float v) { h->enabled.store (v > 0.5f); });
        add (id + "Vol", [h] { return juce::jlimit (-36.0f, 12.0f, juce::Decibels::gainToDecibels (h->volume.load(), -36.0f)); },
                         [h] (float v) { h->volume.store (juce::Decibels::decibelsToGain (v, -60.0f)); });
        add (id + "Pan", [h] { return h->panPos.load(); }, [h] (float v) { h->panPos.store (v); });
        add (id + "Tr", [h] { return h->transpose.load(); }, [h] (float v) { h->transpose.store (std::round (v)); });
        add (id + "Fine", [h] { return h->fineCents.load(); }, [h] (float v) { h->fineCents.store (std::round (v)); });
        add (id + "Dly", [h] { return h->delayMs.load(); }, [h] (float v) { h->delayMs.store (std::round (v)); });
    }
}

void VariantProcessor::bridgesFromHost()
{
    if (bridgeHold.load()) return;
    for (auto& b : bridges)
    {
        const float v = b->raw->load();
        if (v == b->lastParam.load (std::memory_order_relaxed)) continue;
        b->lastParam.store (v, std::memory_order_relaxed);
        b->set (v);
        b->lastModel.store (b->get(), std::memory_order_relaxed);
    }
}

void VariantProcessor::bridgesToHost()
{
    if (bridgeHold.load()) return;
    for (auto& b : bridges)
    {
        const float m = b->get();
        const float lm = b->lastModel.load (std::memory_order_relaxed);
        if (m == lm || std::abs (m - lm) < 1.0e-6f) continue;
        b->lastModel.store (m, std::memory_order_relaxed);
        b->rp->beginChangeGesture();   // FL voit le réglage bouger (« dernier réglage touché » -> clip d'automation)
        b->rp->setValueNotifyingHost (b->rp->convertTo0to1 (m));
        b->rp->endChangeGesture();
        b->lastParam.store (b->raw->load(), std::memory_order_relaxed);
    }
}

void VariantProcessor::bridgesResync()
{
    for (auto& b : bridges)
    {
        b->lastParam.store (b->raw->load(), std::memory_order_relaxed);
        const float m = b->get();
        // réglage chargé différent du paramètre (anciens projets, presets) : il sera envoyé à FL
        b->lastModel.store (std::abs (m - b->raw->load()) < 1.0e-4f ? m : std::numeric_limits<float>::quiet_NaN(), std::memory_order_relaxed);
    }
    bridgeHold.store (false);
}

//==============================================================================
// « Destroy » : le one-shot de la couche devient autre chose (toujours à partir du son d'origine)
juce::String VariantProcessor::destroyLayer (int layer, int intensity)
{
    if (! juce::isPositiveAndBelow (layer, maxLayers)) return {};
    const auto current = getLayerFile (layer);
    if (! current.existsAsFile() || current.hasFileExtension ("sf2;sfz")) return {};
    if (current != destroyLast[(size_t) layer])
    {
        destroyOrigin[(size_t) layer] = current;   // nouveau son : c est lui l original
        morphUndo[(size_t) layer].clear();
        morphRedo[(size_t) layer].clear();
    }
    const auto source = destroyOrigin[(size_t) layer];
    std::unique_ptr<juce::AudioFormatReader> rd (formatManager.createReaderFor (source));
    if (rd == nullptr) return {};
    const int len = (int) juce::jmin<juce::int64> (rd->lengthInSamples, (juce::int64) (rd->sampleRate * 20.0));
    juce::AudioBuffer<float> in ((int) juce::jlimit (1u, 2u, rd->numChannels), juce::jmax (1, len));
    rd->read (&in, 0, len, 0, true, true);

    juce::Random rng (juce::Random::getSystemRandom().nextInt64());
    if (intensity < 0) intensity = rng.nextInt (3);
    juce::StringArray recipe;
    auto out = destroy::process (in, rd->sampleRate, intensity, rng, recipe);

    auto dir = library::morphFolder();
    dir.createDirectory();
    static const char* tags[] = { "Legere", "Moyenne", "Totale" };
    const auto base = source.getFileNameWithoutExtension().upToFirstOccurrenceOf (" Morph", false, true);
    auto file = dir.getNonexistentChildFile (base + " Morph " + tags[juce::jlimit (0, 2, intensity)], ".wav", true);
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (new juce::FileOutputStream (file));
        if (! static_cast<juce::FileOutputStream*> (os.get())->openedOk()) return {};
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), rd->sampleRate, 2, 24, {}, 0));
        if (w == nullptr) return {};
        os.release();
        w->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
    }
    const auto origin = destroyOrigin[(size_t) layer];
    if (! loadSample (file, layer)) return {};
    destroyOrigin[(size_t) layer] = origin;
    morphUndo[(size_t) layer].push_back (current);   // on pourra revenir au son d avant
    morphRedo[(size_t) layer].clear();
    destroyLast[(size_t) layer] = file;
    libraryVersion.fetch_add (1);   // la banque montre le nouveau son (dossier Destroy)
    return recipe.joinIntoString (" + ");
}

bool VariantProcessor::restoreLayer (int layer)
{
    if (! canRestore (layer)) return false;
    const auto origin = destroyOrigin[(size_t) layer];
    const auto cur = getLayerFile (layer);
    if (! loadSample (origin, layer)) return false;
    morphUndo[(size_t) layer].push_back (cur);   // « Annuler » ramène la dernière métamorphose
    morphRedo[(size_t) layer].clear();
    destroyOrigin[(size_t) layer] = origin;
    destroyLast[(size_t) layer] = origin;
    return true;
}

bool VariantProcessor::undoMorph (int layer)
{
    if (! canUndoMorph (layer)) return false;
    auto& u = morphUndo[(size_t) layer];
    while (! u.empty() && ! u.back().existsAsFile()) u.pop_back();   // son effacé entre-temps : on le saute
    if (u.empty()) return false;
    const auto prev = u.back();
    const auto cur = getLayerFile (layer);
    const auto origin = destroyOrigin[(size_t) layer];
    if (! loadSample (prev, layer)) return false;
    u.pop_back();
    morphRedo[(size_t) layer].push_back (cur);
    destroyOrigin[(size_t) layer] = origin;
    destroyLast[(size_t) layer] = prev;   // reste « dans » la même série de métamorphoses
    return true;
}

bool VariantProcessor::redoMorph (int layer)
{
    if (! canRedoMorph (layer)) return false;
    auto& rd = morphRedo[(size_t) layer];
    while (! rd.empty() && ! rd.back().existsAsFile()) rd.pop_back();
    if (rd.empty()) return false;
    const auto next = rd.back();
    const auto cur = getLayerFile (layer);
    const auto origin = destroyOrigin[(size_t) layer];
    if (! loadSample (next, layer)) return false;
    rd.pop_back();
    morphUndo[(size_t) layer].push_back (cur);
    destroyOrigin[(size_t) layer] = origin;
    destroyLast[(size_t) layer] = next;
    return true;
}
