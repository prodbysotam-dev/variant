#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <cmath>
#include <vector>
#include <array>
#include <memory>
#include <algorithm>
#include "Stretcher.h"

constexpr int maxLayers = 8;   // couches de samples (one-shots, SoundFonts)
static_assert (maxLayers == stretch::Cache::numLayers, "le cache du stretch doit couvrir toutes les couches");
constexpr int allLayersChannel = 16;   // canal MIDI = toutes les couches ; 1..4 = une seule couche

struct LayerParams
{
    std::atomic<float>* on = nullptr;
    std::atomic<float>* vol = nullptr;
    std::atomic<float>* pan = nullptr;
    std::atomic<float>* tune = nullptr;
    std::atomic<float>* fine = nullptr;
    std::atomic<float>* root = nullptr;
    std::atomic<float>* delay = nullptr;
    std::atomic<float>* loop = nullptr;      // boucle calée au tempo (arpèges, loops)
    std::atomic<float>* loopBpm = nullptr;   // 0 = auto
    std::atomic<float>* autoBpm = nullptr;   // tempo deviné au chargement
    std::atomic<float>* nameBpm = nullptr;   // tempo lu dans le nom du fichier (0 = aucun)
    std::atomic<float>* pingPong = nullptr;   // boucle aller-retour
    std::atomic<float>* stretch = nullptr;   // stretch : la durée suit le tempo de FL, la hauteur suit la note
    std::atomic<float>* arpChord = nullptr;  // one-shot d'arpège : en accord, les pas passent d'une note à l'autre (comme Hive)
    std::atomic<float>* susLoop = nullptr;   // points de boucle (le son tourne tant que la note est tenue)
    std::atomic<float>* slStart = nullptr;
    std::atomic<float>* slEnd = nullptr;
    std::atomic<float>* slXf = nullptr;
};

// Pointeurs vers les valeurs brutes des paramètres (lecture temps réel, sans verrou).
struct VoiceParams
{
    std::atomic<float>* tune     = nullptr;
    std::atomic<float>* fine     = nullptr;
    std::atomic<float>* attack   = nullptr;
    std::atomic<float>* decay    = nullptr;
    std::atomic<float>* sustain  = nullptr;
    std::atomic<float>* release  = nullptr;
    std::atomic<float>* envDelay = nullptr;
    std::atomic<float>* envHold  = nullptr;
    std::atomic<float>* atkTen = nullptr;
    std::atomic<float>* decTen = nullptr;
    std::atomic<float>* relTen = nullptr;
    std::atomic<float>* velocity = nullptr;
    std::atomic<float>* gain     = nullptr;
    std::atomic<float>* oneshot  = nullptr;
    std::atomic<float>* varOn    = nullptr;
    std::atomic<float>* velTone  = nullptr;
    std::atomic<float>* human    = nullptr;   // naturel entre les couches
    std::atomic<float>* glideMode  = nullptr;   // 0 off, 1 legato, 2 toujours
    std::atomic<float>* glideTime  = nullptr;   // ms
    std::atomic<float>* glideCurve = nullptr;   // 0 linéaire .. 1 rapide au début
    std::atomic<float>* slideIn    = nullptr;   // demi-tons : chaque note démarre décalée puis glisse
    std::array<LayerParams, maxLayers> layers;
    std::atomic<float>* loopChord = nullptr;   // 0 = seule la note la plus grave joue la boucle, 1 = toutes
    const std::atomic<double>* hostPpq = nullptr;   // position de FL au début du bloc
    const std::atomic<double>* hostBpm = nullptr;
    stretch::Cache* stretchCache = nullptr;   // notes pré-calculées du mode Stretch
    const std::atomic<bool>* hostPlaying = nullptr;
};

//==============================================================================
// Enveloppe façon FL Studio : Délai, Attaque, Tenue, Déclin, Sustain, Relâche,
// chaque segment avec sa courbe (tension -1..1 : négatif = rapide au début, positif = lent au début)
class FlEnvelope
{
public:
    struct Settings { float delay = 0, attack = 0.001f, hold = 0, decay = 0.4f, sustain = 1, release = 0.08f, tA = 0, tD = 0, tR = 0; };

    void setSampleRate (double s) { sr = s; }
    void setParameters (const Settings& s) { set = s; }
    void reset() { stage = Idle; level = 0.0f; }
    void noteOn() { stage = Delay; t = 0.0; level = 0.0f; }
    void noteOff() { if (stage != Idle && stage != Release) { relFrom = level; stage = Release; t = 0.0; } }
    bool isActive() const { return stage != Idle; }
    bool isReleasing() const { return stage == Release; }

    static float shape (float x, float ten)
    {
        x = juce::jlimit (0.0f, 1.0f, x);
        if (std::abs (ten) < 0.01f) return x;
        const float k = 1.0f + std::abs (ten) * 5.0f;
        return ten > 0.0f ? std::pow (x, k) : 1.0f - std::pow (1.0f - x, k);
    }

    float getNextSample()
    {
        auto advance = [this] (float secs) -> float
        {
            const double len = juce::jmax (1.0, (double) secs * sr);
            t += 1.0;
            return (float) juce::jmin (1.0, t / len);
        };
        switch (stage)
        {
            case Idle: return 0.0f;
            case Delay:
                if (set.delay <= 0.0005f || advance (set.delay) >= 1.0f) { stage = Attack; t = 0.0; }
                return level = 0.0f;
            case Attack:
            {
                const float x = set.attack <= 0.0005f ? 1.0f : advance (set.attack);
                level = shape (x, set.tA);
                if (x >= 1.0f) { stage = Hold; t = 0.0; level = 1.0f; }
                return level;
            }
            case Hold:
                if (set.hold <= 0.0005f || advance (set.hold) >= 1.0f) { stage = Decay; t = 0.0; }
                return level = 1.0f;
            case Decay:
            {
                const float x = advance (set.decay);
                level = 1.0f - (1.0f - set.sustain) * shape (x, -set.tD);
                if (x >= 1.0f) { stage = Sustain; level = set.sustain; }
                return level;
            }
            case Sustain:
                level = set.sustain;
                if (level <= 0.0001f) stage = Idle;
                return level;
            case Release:
            {
                const float x = advance (set.release);
                level = relFrom * (1.0f - shape (x, -set.tR));
                if (x >= 1.0f) { stage = Idle; level = 0.0f; }
                return level;
            }
        }
        return 0.0f;
    }

private:
    enum Stage { Idle, Delay, Attack, Hold, Decay, Sustain, Release } stage = Idle;
    Settings set;
    double sr = 44100.0, t = 0.0;
    float level = 0.0f, relFrom = 0.0f;
};

//==============================================================================
// zone d'un instrument multi-échantillons (SoundFont) : chaque plage de touches a son échantillon
struct KeyZone
{
    int lo = 0, hi = 127, root = 60;
    float cents = 0.0f, gain = 1.0f;
    double rate = 44100.0;
    int loopA = -1, loopB = -1;
    juce::AudioBuffer<float> audio;
};

struct LayerData
{
    juce::AudioBuffer<float> source;
    std::shared_ptr<const std::vector<KeyZone>> zones;   // SoundFont : un échantillon par plage de touches
    std::vector<juce::AudioBuffer<float>> variants;
    double rate = 44100.0;
    bool valid = false;
    juce::uint32 uid = 0;   // identifiant unique de la source (change seulement quand le son change)
    std::shared_ptr<const stretch::ArpAnalysis> arp;   // attaques + hauteurs (mode arpégiateur), calculées au chargement
};

// Toutes les couches chargées (+ leurs variantes pré-calculées)
class LayerSound : public juce::SynthesiserSound
{
public:
    using Ptr = juce::ReferenceCountedObjectPtr<LayerSound>;
    bool appliesToNote (int) override    { return true; }
    bool appliesToChannel (int) override { return true; }

    bool anyValid() const { for (auto& l : layers) if (l.valid) return true; return false; }

    std::array<LayerData, maxLayers> layers;
};

//==============================================================================
// Choix de la variante : aléatoire, jamais deux fois la même d'affilée.
struct RoundRobin
{
    int next (int count)
    {
        if (count <= 1) { last.store (0); return 0; }
        int v = rng.nextInt (count);
        if (v == last.load()) v = (v + 1 + rng.nextInt (count - 1)) % count;
        last.store (v);
        return v;
    }

    juce::Random rng;
    std::atomic<int> last { -1 };
};

//==============================================================================
// Synthé qui retient le canal de la note (le canal choisit la couche)
class LayerSynth : public juce::Synthesiser
{
public:
    void noteOn (int midiChannel, int midiNoteNumber, float velocity) override
    {
        currentChannel = midiChannel;
        const int note = juce::jlimit (0, 127, midiNoteNumber);
        notePart[(size_t) note] = curPart;
        if (slideNext && retargetSlides (note))
        {
            // note slide (comme FL) : elle ne se déclenche pas, ce sont les notes de sa couleur qui glissent jusqu'à elle
            slideNext = false;
            slideSwallow[(size_t) note] = true;
            lastNote = note;
            return;
        }
        const int mode = glideMode != nullptr ? (int) glideMode->load() : 0;
        glideFrom = -1;
        const bool sameInstant = samplesSinceNoteOn == 0;   // accord : pas de glide entre ses notes
        if (mode == 1 && held > 0 && ! sameInstant && ! slideNext && retargetSlides (note, legatoSamples()))
        {
            // Glide « Legato » (notes slide du piano roll de FL) : le son en cours n'est pas relancé,
            // il glisse jusqu'à la nouvelle note comme un one-shot (808…), et continue tant qu'elle est tenue
            slideSwallow[(size_t) note] = true;
            lastNote = note;
            return;
        }
        if (lastNote >= 0 && ! sameInstant && (slideNext || mode == 2 || (mode == 1 && held > 0)))
        {
            glideFrom = lastNote;
            if (slideNext || mode == 1)   // jeu lié : la note précédente laisse la place
                for (auto* v : voices) if (v->isVoiceActive()) stopVoice (v, 1.0f, true);
        }
        slideNext = false;
        if (held <= 0 && ! anyHeld()) arpStart = clock;   // mode arpégiateur : l'horloge du motif repart à la 1re note de l'accord
        heldNotes[(size_t) note] = true;
        updateChord();
        juce::Synthesiser::noteOn (midiChannel, midiNoteNumber, velocity);
        glideFrom = -1;
        lastNote = midiNoteNumber;
        samplesSinceNoteOn = 0;
        ++held;
    }

    void renderVoices (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) override
    {
        samplesSinceNoteOn += numSamples;
        juce::Synthesiser::renderVoices (buffer, startSample, numSamples);
        clock += numSamples;   // pendant le rendu, clock = position du début de ce morceau de bloc
    }

    void noteOff (int midiChannel, int midiNoteNumber, float velocity, bool allowTailOff) override;   // défini après LayerVoice
    double legatoSamples() const
    {
        const float ms = glideTime != nullptr ? glideTime->load() : 120.0f;
        return juce::jmax (1.0, (double) ms * 0.001 * getSampleRate());
    }
    std::atomic<float>* glideTime = nullptr;

    void allNotesOff (int midiChannel, bool allowTailOff) override
    {
        held = 0;
        heldNotes.fill (false);
        updateChord();
        slideSwallow.fill (false);
        juce::Synthesiser::allNotesOff (midiChannel, allowTailOff);
    }

    void handleController (int midiChannel, int controllerNumber, int controllerValue) override
    {
        if (controllerNumber == 65) slideNext = controllerValue >= 64;   // CC65 = la prochaine note est une note slide
        else if (controllerNumber == 3) curPart = controllerValue;        // partie (couleur) des notes qui suivent
        else if (controllerNumber == 5) slideLen16 = juce::jmax (1, controllerValue);   // durée du slide en 1/16 de temps
        juce::Synthesiser::handleController (midiChannel, controllerNumber, controllerValue);
    }

    bool retargetSlides (int note, double samplesOverride = -1.0);   // défini après LayerVoice

    int lowestHeld() const { for (int i = 0; i < 128; ++i) if (heldNotes[(size_t) i]) return i; return -1; }
    bool anyHeld() const { return lowestHeld() >= 0; }
    // mode arpégiateur : l'accord tenu (gardé après le relâchement pour les sons joués en entier)
    // et la note qui "porte" le motif (la plus grave), partagés par toutes les voix
    void updateChord()
    {
        int k = 0;
        for (int i = 0; i < 128 && k < (int) chord.size(); ++i) if (heldNotes[(size_t) i]) chord[(size_t) k++] = i;
        if (k > 0) { chordN = k; arpLead = chord[0]; }
    }
    std::array<int, 16> chord {};
    int chordN = 0, arpLead = -1;
    juce::int64 clock = 0, arpStart = 0;
    std::array<bool, 128> heldNotes {};
    std::array<int, 128> notePart {};
    std::array<bool, 128> slideSwallow {};
    int currentChannel = allLayersChannel, glideFrom = -1, lastNote = -1, held = 0, curPart = 0, slideLen16 = 4;
    juce::int64 samplesSinceNoteOn = 1 << 20;
    bool slideNext = false;
    std::atomic<float>* glideMode = nullptr;
    const std::atomic<double>* hostBpm = nullptr;
};

//==============================================================================
class LayerVoice : public juce::SynthesiserVoice
{
public:
    LayerVoice (const VoiceParams& p, std::array<RoundRobin, maxLayers>& rr, const LayerSynth& s,
                std::array<std::atomic<float>, maxLayers>& positionsOut)
        : params (p), roundRobin (rr), synth (s), positions (positionsOut)
    {
        for (auto& x : positions) x.store (-1.0f);
    }

    bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<LayerSound*> (s) != nullptr; }

    void setCurrentPlaybackSampleRate (double sr) override
    {
        SynthesiserVoice::setCurrentPlaybackSampleRate (sr);
        if (sr > 0.0) env.setSampleRate (sr);
    }

    void startNote (int note, float vel, juce::SynthesiserSound* s, int wheel) override
    {
        sound = dynamic_cast<LayerSound*> (s);
        if (sound == nullptr) return;

        midiNote  = note;
        slideNote = -1;
        bendSemis = wheelToSemis (wheel);

        const int ch = synth.currentChannel;
        const bool useVariants = params.varOn->load() > 0.5f;
        const float human = params.human != nullptr ? params.human->load() : 0.0f;
        const double sr = getSampleRate();

        int firstValid = -1;
        for (int i = 0; i < maxLayers; ++i)
            if (sound->layers[(size_t) i].valid && params.layers[(size_t) i].on->load() > 0.5f) { firstValid = i; break; }

        for (int i = 0; i < maxLayers; ++i)
        {
            auto& st = state[(size_t) i];
            st.active = false;
            const auto& ld = sound->layers[(size_t) i];
            const auto& lp = params.layers[(size_t) i];
            const bool enabled = ld.valid && lp.on->load() > 0.5f;

            bool wanted = (ch == allLayersChannel || ch < 1 || ch > maxLayers) ? enabled : (i == ch - 1 && enabled);
            if (ch >= 1 && ch <= maxLayers && ! (sound->layers[(size_t) ch - 1].valid && params.layers[(size_t) ch - 1].on->load() > 0.5f))
                wanted = (i == firstValid);   // couche demandée vide : on retombe sur la première
            if (! wanted) continue;

            const int variant = useVariants ? roundRobin[(size_t) i].next ((int) ld.variants.size()) : -1;
            st.data = variant >= 0 ? &ld.variants[(size_t) variant] : &ld.source;
            st.rate = ld.rate;
            st.zone = nullptr;
            if (ld.zones != nullptr && ! ld.zones->empty())
            {
                // SoundFont : la zone qui couvre la note (sinon la plus proche)
                const KeyZone* best = nullptr; int bestD = 1 << 20;
                for (auto& z : *ld.zones)
                {
                    const int d = note < z.lo ? z.lo - note : (note > z.hi ? note - z.hi : 0);
                    if (d < bestD) { bestD = d; best = &z; }
                }
                if (best != nullptr) { st.zone = best; st.data = &best->audio; st.rate = best->rate; }
            }
            st.active = st.data->getNumSamples() > 0;

            // petites différences entre couches pour un empilement naturel
            const float jitterMs = human * rng.nextFloat() * 12.0f;
            st.pos = -((double) lp.delay->load() + jitterMs) * 0.001 * sr;
            st.detune = (rng.nextFloat() * 2.0f - 1.0f) * 4.0f * human;
            st.gainJitter = juce::Decibels::decibelsToGain ((rng.nextFloat() * 2.0f - 1.0f) * 1.5f * human);
            st.zL = st.zR = 0.0f;
            st.dir = 1;
            st.cached = false; st.cacheSemis = 0; st.arpChord = false; st.loopGain = 1.0f;
            st.arpStep = false; st.arpAn = nullptr; st.fadeIn = 0;

            // boucle calée au tempo (arpèges .wav) : toujours le son d'origine, et en accord seule la note la plus grave joue
            st.loop = lp.loop != nullptr && lp.loop->load() > 0.5f;

            // mode arpégiateur (one-shot d'arpège) : le motif suit le tempo de FL tout de suite (pas d'attente de calcul),
            // et en accord chaque pas prend la note suivante de l'accord, comme l'arpégiateur de Hive
            if (! st.loop && st.zone == nullptr && lp.arpChord != nullptr && lp.arpChord->load() > 0.5f
                && ld.arp != nullptr && ld.arp->arpLike && ld.uid != 0)
            {
                const float manual = lp.loopBpm != nullptr ? lp.loopBpm->load() : 0.0f;
                const float fromName = lp.nameBpm != nullptr ? lp.nameBpm->load() : 0.0f;
                st.loopBpm = manual > 1.0f ? manual : (fromName > 1.0f ? fromName : 140.0f);
                st.arpStep = true; st.stretch = false; st.cached = false;
                st.data = &ld.source; st.rate = ld.rate; st.arpAn = ld.arp.get(); st.srcKey = ld.uid;
                st.rootNote = (int) lp.root->load();
                st.tuneInt = (int) std::round (params.tune->load()) + (int) std::round (lp.tune->load());
                st.lastStep = -1; st.xf = 0; st.prevStep = {}; st.curStep = {};
                st.pos = 0.0; st.gainJitter = 1.0f;
                const bool lead = synth.arpLead < 0 || synth.arpLead == note;
                st.loopGain = lead ? 1.0f : 0.0f;
                st.fadeIn = synth.clock > synth.arpStart ? 96 : 0;   // la voix arrive en cours de motif : petit fondu (pas de clic)
                continue;
            }
            st.stretch = ! st.loop && lp.stretch != nullptr && lp.stretch->load() > 0.5f;
            if (st.stretch)
            {
                const double delayPos = st.pos;
                st.data = &ld.source; st.zone = nullptr;
                const float manual = lp.loopBpm != nullptr ? lp.loopBpm->load() : 0.0f;
                const float fromName = lp.nameBpm != nullptr ? lp.nameBpm->load() : 0.0f;
                st.loopBpm = manual > 1.0f ? manual : (fromName > 1.0f ? fromName : 140.0f);   // tes one-shots sont enregistrés à 140 BPM
                st.loopGain = 1.0f; st.localT = 0.0; st.pos = 0.0;
                if (params.stretchCache != nullptr)
                {
                    // version de qualité pré-calculée : même durée pour toutes les notes, calée sur le tempo de FL
                    const double hb = params.hostBpm != nullptr ? juce::jmax (20.0, params.hostBpm->load()) : 120.0;
                    const int tempoKey = (int) std::round ((double) st.loopBpm / hb * 1000.0);
                    const int cs = note - (int) lp.root->load() + (int) std::round (params.tune->load()) + (int) std::round (lp.tune->load());
                    if (auto* b = params.stretchCache->get (i, cs, ld.uid, tempoKey))
                    {
                        st.data = b; st.stretch = false; st.cached = true; st.cacheSemis = cs; st.rate = ld.rate; st.pos = delayPos;
                    }
                    else
                    {
                        // pas encore calculée : le son d'origine transposé normalement (pas de grain qui grésille), le temps que ça arrive
                        st.stretch = false; st.data = &ld.source; st.rate = ld.rate; st.pos = delayPos;
                    }
                    st.cacheBase = cs; st.tempoKey = tempoKey; st.srcKey = ld.uid;
                    st.rootNote = (int) lp.root->load(); st.tuneInt = (int) std::round (params.tune->load()) + (int) std::round (lp.tune->load());
                    st.arpChord = false;   // le mode arpégiateur a son propre moteur (au-dessus)
                    st.lastStep = -1; st.xf = 0; st.prevData = nullptr; st.loopGain = 1.0f;
                    st.cacheSemis = st.cached ? cs : 0;
                }
            }
            if (st.loop)
            {
                st.data = &ld.source;
                const bool mono = params.loopChord == nullptr || params.loopChord->load() < 0.5f;
                const int low = synth.lowestHeld();
                if (mono && low >= 0 && note != low) { st.active = false; continue; }
                const float manual = lp.loopBpm != nullptr ? lp.loopBpm->load() : 0.0f;
                const float autoB = lp.autoBpm != nullptr ? lp.autoBpm->load() : 0.0f;
                st.loopBpm = manual > 1.0f ? manual : (autoB > 1.0f ? autoB : 120.0f);
                st.loopGain = 1.0f;
                st.localT = 0.0;
                st.pos = 0.0;
                st.gainJitter = 1.0f;
            }
        }

        // glide : départ décalé puis glissement vers la note
        glideSemis = synth.glideFrom >= 0 ? (float) (synth.glideFrom - note)
                                          : (params.slideIn != nullptr ? params.slideIn->load() : 0.0f);
        glideTotal = juce::jmax (1.0, (params.glideTime != nullptr ? params.glideTime->load() : 100.0f) * 0.001 * sr);
        glidePos = 0.0;
        slideCur = 0.0; slideLen = 0.0; slideFrom = 0.0; slideTarget = 0.0;

        const float sens = params.velocity->load();
        velGain = (1.0f - sens) + sens * std::pow (vel, 1.5f);

        const float vt = params.velTone->load();
        softness = vt * (1.0f - vel);
        const float cutoff = 20000.0f * std::pow (0.04f, softness);
        toneCoef = cutoff >= 18000.0f ? 1.0f : 1.0f - std::exp (-juce::MathConstants<float>::twoPi * cutoff / (float) sr);

        released = false;
        updateEnvelope();
        env.reset();
        env.noteOn();
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            released = true;
            // points de boucle actifs : la note doit pouvoir s'arrêter même en mode "jouer en entier"
            bool susLoop = false;
            for (int i = 0; i < maxLayers; ++i)
                if (state[(size_t) i].active && params.layers[(size_t) i].susLoop != nullptr && params.layers[(size_t) i].susLoop->load() > 0.5f) susLoop = true;
            for (int i = 0; i < maxLayers; ++i)
                if (state[(size_t) i].active && state[(size_t) i].zone != nullptr) susLoop = true;   // SoundFont : la note s'arrête au relâchement
            if (params.oneshot->load() < 0.5f || susLoop) env.noteOff();
        }
        else finish();
    }

    // note slide (comme FL) : la hauteur glisse vers "note" pendant "samples", sans relancer le son
    void slideTo (int note, double samples)
    {
        slideFrom = slideCur;
        slideTarget = (double) (note - getCurrentlyPlayingNote());
        slideLen = juce::jmax (1.0, samples);
        slidePos = 0.0;
        slideNote = note;   // le son « appartient » maintenant à la note slide (il continue tant qu elle est tenue)
    }

    void pitchWheelMoved (int value) override { bendSemis = wheelToSemis (value); }
    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& out, int start, int num) override
    {
        if (sound == nullptr || ! isVoiceActive()) return;

        const double sr = getSampleRate();
        const double globalSemis = params.tune->load() + params.fine->load() / 100.0 + bendSemis + slideCur;
        const float g = juce::Decibels::decibelsToGain (params.gain->load(), -60.0f) * velGain;
        updateEnvelope();

        float* outL = out.getWritePointer (0);
        float* outR = out.getNumChannels() > 1 ? out.getWritePointer (1) : nullptr;

        struct Prep { double inc, semis, ratio; float gl, gr; };
        std::array<Prep, maxLayers> prep {};
        for (int i = 0; i < maxLayers; ++i)
        {
            auto& st = state[(size_t) i];
            if (! st.active) continue;
            const auto& lp = params.layers[(size_t) i];
            const double semis = (double) (midiNote - (st.zone != nullptr ? st.zone->root : (int) lp.root->load())) + globalSemis
                               + lp.tune->load() + lp.fine->load() / 100.0 + st.detune / 100.0
                               + (st.zone != nullptr ? st.zone->cents / 100.0 : 0.0)
                               - (st.cached ? (double) st.cacheBase : 0.0);
            const float lg = juce::Decibels::decibelsToGain (lp.vol->load(), -60.0f) * st.gainJitter * (st.zone != nullptr ? st.zone->gain : 1.0f);
            const float pan = juce::jlimit (-1.0f, 1.0f, lp.pan->load());
            const float a = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            prep[(size_t) i] = { std::pow (2.0, semis / 12.0) * st.rate / sr, semis, st.rate / sr, lg * std::cos (a) * 1.41421f, lg * std::sin (a) * 1.41421f };
        }
        {
            // plusieurs couches empilées : on baisse chacune pour que la somme ne sature pas
            int nActive = 0;
            for (int i = 0; i < maxLayers; ++i) if (state[(size_t) i].active) ++nActive;
            const float norm = nActive > 1 ? std::pow ((float) nActive, -0.7f) : 1.0f;
            for (auto& pp : prep) { pp.gl *= norm; pp.gr *= norm; }
        }

        const float curve = params.glideCurve != nullptr ? params.glideCurve->load() : 0.0f;
        // infos de tempo pour les couches en boucle
        const bool playing = params.hostPlaying != nullptr && params.hostPlaying->load();
        const double hostBpm = params.hostBpm != nullptr ? juce::jmax (20.0, params.hostBpm->load()) : 120.0;
        const double hostSpb = 60.0 / hostBpm * sr;
        const double ppq0 = params.hostPpq != nullptr ? params.hostPpq->load() : 0.0;
        const bool monoLoop = params.loopChord == nullptr || params.loopChord->load() < 0.5f;
        const int lowNote = synth.lowestHeld();
        const float loopFade = (float) (1.0 / (0.012 * sr));
        for (int n = 0; n < num; ++n)
        {
            if (slideLen > 0.0 && (n & 15) == 0)   // note slide en cours
            {
                const double p = juce::jmin (1.0, slidePos / slideLen);
                const double before = slideCur;
                slideCur = slideFrom + (slideTarget - slideFrom) * (1.0 - std::pow (1.0 - p, 1.5));
                for (auto& pp : prep) { pp.semis += slideCur - before; if (pp.ratio > 0.0) pp.inc = std::pow (2.0, pp.semis / 12.0) * pp.ratio; }
                if (p >= 1.0) slideLen = 0.0;
            }
            if (slideLen > 0.0) slidePos += 1.0;
            if (glideSemis != 0.0f && (n & 15) == 0)
            {
                const double p = juce::jmin (1.0, glidePos / glideTotal);
                const double shaped = 1.0 - std::pow (1.0 - p, 1.0 + 3.0 * curve);
                const double off = glideSemis * (1.0 - shaped);
                for (auto& pp : prep) if (pp.ratio > 0.0) pp.inc = std::pow (2.0, (pp.semis + off) / 12.0) * pp.ratio;
                if (p >= 1.0) glideSemis = 0.0f;
            }
            glidePos += 1.0;
            const float e = env.getNextSample() * g;
            float l = 0.0f, r = 0.0f;
            bool anyActive = false;

            for (int i = 0; i < maxLayers; ++i)
            {
                auto& st = state[(size_t) i];
                if (! st.active) continue;
                anyActive = true;

                if (st.loop || st.stretch)
                {
                    // la note n'est plus la plus grave de l'accord : la boucle s'efface (pas d'empilement)
                    if (st.loop && monoLoop && lowNote >= 0 && lowNote != midiNote)
                    {
                        st.loopGain -= loopFade;
                        if (st.loopGain <= 0.0f) { st.active = false; continue; }
                    }
                    const auto& d = *st.data;
                    const int len = d.getNumSamples();
                    const double srcSpb = st.rate * 60.0 / juce::jmax (20.0, (double) st.loopBpm);
                    const double beat = (playing && st.loop) ? ppq0 + (double) (start + n) / hostSpb : st.localT / hostSpb;
                    if (st.stretch && beat * srcSpb >= (double) len - 4.0) { st.active = false; continue; }   // fin du son étiré
                    auto srcAt = [&] (double b) { double p = std::fmod (b * srcSpb, (double) len); return p < 0.0 ? (st.stretch ? 0.0 : p + len) : p; };
                    const auto& pp = prep[(size_t) i];
                    const double ratio = std::pow (2.0, pp.semis / 12.0);
                    const float* dl = d.getReadPointer (0);
                    const float* dr = d.getReadPointer (d.getNumChannels() > 1 ? 1 : 0);
                    float sl = 0.0f, srr = 0.0f;
                    // petit fondu à la jonction de la boucle (sinon clic à chaque tour)
                    const double edge = 0.003 * st.rate;
                    auto edgeGain = [len, edge] (double p) { return (float) juce::jmin (1.0, p / edge, ((double) len - p) / edge); };
                    if (std::abs (ratio - 1.0) < 1.0e-3)
                    {
                        const double p = srcAt (beat);
                        const float eg = st.stretch ? 1.0f : edgeGain (p);
                        sl = hermiteWrap (dl, len, p) * eg; srr = hermiteWrap (dr, len, p) * eg;
                    }
                    else
                    {
                        // transposition sans changer le tempo : 2 grains qui se chevauchent
                        const double G = 0.06 * sr;
                        for (int gi = 0; gi < 2; ++gi)
                        {
                            const double local = std::fmod (st.localT + gi * G * 0.5, G);
                            double p = srcAt (beat - local / hostSpb) + local * ratio * st.rate / sr;
                            p = std::fmod (p, (double) len);
                            const float w = (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * local / G));
                            const float eg = st.stretch ? 1.0f : edgeGain (p);
                            sl += hermiteWrap (dl, len, p) * w * eg; srr += hermiteWrap (dr, len, p) * w * eg;
                        }
                    }
                    st.localT += 1.0;
                    l += sl * pp.gl * st.loopGain;
                    r += srr * pp.gr * st.loopGain;
                    continue;
                }

                if (st.arpStep)
                {
                    // MODE ARPÉGIATEUR : le motif du one-shot est rejoué pas par pas, calé tout de suite sur le tempo de FL.
                    // Chaque pas : la version haute qualité (stretch pré-calculé) si elle est prête, sinon le pas d'origine
                    // transposé à la volée -> plus d'attente quand on change de son, de tempo ou de réglage.
                    // En accord, une seule voix (la note la plus grave) porte le motif et chaque pas prend la note suivante.
                    const auto* an = st.arpAn;
                    const auto& src = *st.data;
                    const int len = src.getNumSamples();
                    const float target = (synth.arpLead < 0 || synth.arpLead == midiNote) ? 1.0f : 0.0f;
                    if (st.loopGain < target) st.loopGain = juce::jmin (target, st.loopGain + loopFade);
                    else if (st.loopGain > target) st.loopGain = juce::jmax (target, st.loopGain - loopFade);
                    const double factor = (double) st.loopBpm / hostBpm;                 // durée du motif x factor
                    const double tOut = (double) (synth.clock - synth.arpStart + n);     // horloge partagée par l'accord
                    const double tSrc = tOut * st.rate / sr / factor;                    // position équivalente dans le son d'origine
                    if (tSrc >= (double) len - 2.0 || len < 4) { st.active = false; continue; }
                    const auto& on = an->onsets;
                    const int step = juce::jmax (0, (int) (std::upper_bound (on.begin(), on.end(), (int) tSrc) - on.begin()) - 1);
                    const auto& pp = prep[(size_t) i];
                    if (step != st.lastStep)
                    {
                        st.lastStep = step;
                        const int K = synth.chordN;
                        int tgt = midiNote;
                        if (K >= 2) tgt = synth.chord[(size_t) (step % K)];
                        // la note de ce pas garde son octave dans le motif (ex. pas joué une octave au-dessus)
                        const float m = (size_t) step < an->midi.size() ? an->midi[(size_t) step] : -1.0f;
                        const int d = m >= 0.0f ? (int) std::round (m - (float) st.rootNote) : 0;
                        const int oct = (int) std::round (d / 12.0);
                        const int T = K >= 2 ? tgt - st.rootNote + 12 * oct - d + st.tuneInt
                                             : midiNote - st.rootNote + st.tuneInt;
                        const double frac = pp.semis - (double) (midiNote - st.rootNote + st.tuneInt);   // finesse, pitch bend, slide
                        const int tempoKey = (int) std::round (factor * 1000.0);
                        ArpStepState ns;
                        ns.on = true;
                        ns.startOut = (double) on[(size_t) step] * factor * sr / st.rate;
                        ns.onset = on[(size_t) step];
                        ns.end = (size_t) step + 1 < on.size() ? on[(size_t) step + 1] : len;
                        ns.ratio = std::pow (2.0, ((double) T + frac) / 12.0);
                        ns.cached = std::abs (frac) < 0.02 && params.stretchCache != nullptr ? params.stretchCache->get (i, T, st.srcKey, tempoKey) : nullptr;
                        if (std::abs (frac) >= 0.02 && params.stretchCache != nullptr) params.stretchCache->get (i, T, st.srcKey, tempoKey);   // on prépare quand même
                        st.prevStep = st.curStep; st.curStep = ns;
                        st.xf = st.prevStep.on ? 96 : 0;
                    }
                    auto readStep = [&] (const ArpStepState& S, float& oL, float& oR)
                    {
                        if (! S.on) { oL = oR = 0.0f; return; }
                        if (S.cached != nullptr)
                        {
                            const auto& cb = *S.cached;
                            const int cl = cb.getNumSamples();
                            const double p = tOut * st.rate / sr;
                            oL = hermite (cb.getReadPointer (0), cl, p);
                            oR = hermite (cb.getReadPointer (cb.getNumChannels() > 1 ? 1 : 0), cl, p);
                            return;
                        }
                        // pas d'origine transposé à la volée, coupé en douceur à la fin de sa note
                        const double p = (double) S.onset + (tOut - S.startOut) * S.ratio * st.rate / sr;
                        const double left = (double) S.end - p;
                        if (left <= 0.0) { oL = oR = 0.0f; return; }
                        const float eg = left < 96.0 ? (float) (left / 96.0) : 1.0f;
                        oL = hermite (src.getReadPointer (0), len, p) * eg;
                        oR = hermite (src.getReadPointer (src.getNumChannels() > 1 ? 1 : 0), len, p) * eg;
                    };
                    float sl, srr;
                    readStep (st.curStep, sl, srr);
                    if (st.xf > 0)   // fondu enchaîné avec le pas précédent : pas de clic au changement de note
                    {
                        float pl, pr;
                        readStep (st.prevStep, pl, pr);
                        const float w = (float) st.xf / 96.0f;
                        sl = sl * (1.0f - w) + pl * w; srr = srr * (1.0f - w) + pr * w;
                        --st.xf;
                    }
                    if (st.fadeIn > 0) { const float w = 1.0f - (float) st.fadeIn / 96.0f; sl *= w; srr *= w; --st.fadeIn; }
                    {
                        const double left = (double) len - tSrc;
                        if (left < 96.0) { const float tg = (float) juce::jmax (0.0, left / 96.0); sl *= tg; srr *= tg; }
                    }
                    if (toneCoef < 1.0f)
                    {
                        st.zL += toneCoef * (sl - st.zL);  sl = st.zL;
                        st.zR += toneCoef * (srr - st.zR); srr = st.zR;
                    }
                    l += sl * pp.gl * st.loopGain;
                    r += srr * pp.gr * st.loopGain;
                    st.pos = tSrc;   // pour la tête de lecture affichée
                    continue;
                }

                if (st.pos < 0.0) { st.pos += 1.0; continue; }   // retard de la couche

                const auto& d = *st.data;
                const int len = d.getNumSamples();
                if (st.pos >= (double) len) { st.active = false; continue; }

                float sl = hermite (d.getReadPointer (0), len, st.pos);
                float srr = hermite (d.getReadPointer (d.getNumChannels() > 1 ? 1 : 0), len, st.pos);
                if (st.xf > 0 && st.prevData != nullptr)   // fondu depuis la note précédente de l'accord
                {
                    const auto& pd = *st.prevData;
                    const int pl = pd.getNumSamples();
                    const float w = (float) st.xf / 96.0f;
                    sl = sl * (1.0f - w) + hermite (pd.getReadPointer (0), pl, st.pos) * w;
                    srr = srr * (1.0f - w) + hermite (pd.getReadPointer (pd.getNumChannels() > 1 ? 1 : 0), pl, st.pos) * w;
                    --st.xf;
                }
                if (st.arpChord) { sl *= st.loopGain; srr *= st.loopGain; }
                {
                    // fin du son : 2 ms de fondu (un son coupé sec claque)
                    const double left = (double) len - st.pos;
                    if (left < 96.0) { const float tg = (float) juce::jmax (0.0, left / 96.0); sl *= tg; srr *= tg; }
                }
                // points de boucle : tant que la note est tenue, on reboucle entre début et fin avec un fondu enchaîné
                const auto& lpp = params.layers[(size_t) i];
                double loopA = 0.0, loopLen = 0.0;
                const bool pingpong = lpp.pingPong != nullptr && lpp.pingPong->load() > 0.5f;
                if (released) st.dir = 1;
                const bool zoneLoop = st.zone != nullptr && st.zone->loopB > st.zone->loopA + 32 && (lpp.susLoop == nullptr || lpp.susLoop->load() < 0.5f);
                if (! released && (zoneLoop || (lpp.susLoop != nullptr && lpp.susLoop->load() > 0.5f)))
                {
                    const double a = zoneLoop ? (double) st.zone->loopA : juce::jlimit (0.0, 1.0, (double) lpp.slStart->load()) * len;
                    const double b = zoneLoop ? (double) st.zone->loopB : juce::jlimit (0.0, 1.0, (double) lpp.slEnd->load()) * len;
                    if (b - a > 256.0)
                    {
                        loopA = a; loopLen = b - a;
                        const double xf = zoneLoop ? juce::jmin (64.0, loopLen * 0.1) : juce::jmax (32.0, loopLen * 0.5 * (double) juce::jlimit (0.0f, 1.0f, lpp.slXf->load()));
                        if (! pingpong && st.pos > b - xf)
                        {
                            const float w = (float) juce::jlimit (0.0, 1.0, (st.pos - (b - xf)) / xf);
                            const double p2 = st.pos - loopLen;
                            const float g1 = std::cos (w * 1.5707963f), g2 = std::sin (w * 1.5707963f);
                            sl = sl * g1 + hermite (d.getReadPointer (0), len, p2) * g2;
                            srr = srr * g1 + hermite (d.getReadPointer (d.getNumChannels() > 1 ? 1 : 0), len, p2) * g2;
                        }
                    }
                }
                if (toneCoef < 1.0f)
                {
                    st.zL += toneCoef * (sl - st.zL);  sl = st.zL;
                    st.zR += toneCoef * (srr - st.zR); srr = st.zR;
                }
                const auto& pp = prep[(size_t) i];
                l += sl * pp.gl;
                r += srr * pp.gr;
                st.pos += pp.inc * (double) st.dir;
                if (loopLen > 0.0)
                {
                    if (pingpong)
                    {
                        if (st.dir > 0 && st.pos >= loopA + loopLen) { st.pos = 2.0 * (loopA + loopLen) - st.pos; st.dir = -1; }
                        else if (st.dir < 0 && st.pos <= loopA) { st.pos = 2.0 * loopA - st.pos; st.dir = 1; }
                    }
                    else if (st.pos >= loopA + loopLen) st.pos -= loopLen;
                }
            }

            if (! anyActive || ! env.isActive()) { finish(); return; }

            l *= e; r *= e;
            if (outR != nullptr) { outL[start + n] += l; outR[start + n] += r; }
            else                 { outL[start + n] += 0.5f * (l + r); }
        }

        for (int i = 0; i < maxLayers; ++i)
        {
            auto& st = state[(size_t) i];
            positions[(size_t) i].store (st.active && st.pos >= 0.0 ? (float) (st.pos / (double) st.data->getNumSamples()) : -1.0f,
                                         std::memory_order_relaxed);
        }
    }

private:
    // un pas du mode arpégiateur : d'où il part dans le son, sa transposition, et sa version haute qualité si elle est prête
    struct ArpStepState
    {
        bool on = false;
        double startOut = 0.0, ratio = 1.0;
        int onset = 0, end = 0;
        const juce::AudioBuffer<float>* cached = nullptr;
    };
    struct LayerState
    {
        bool active = false;
        const juce::AudioBuffer<float>* data = nullptr;
        double pos = 0.0, rate = 44100.0;
        float detune = 0.0f, gainJitter = 1.0f, zL = 0.0f, zR = 0.0f;
        bool loop = false;
        float loopBpm = 120.0f, loopGain = 1.0f;
        const KeyZone* zone = nullptr;
        bool stretch = false;
        bool cached = false;   // joue une version pré-calculée du stretch
        int cacheSemis = 0;
        int cacheBase = 0, tempoKey = 1000, rootNote = 60, tuneInt = 0, lastStep = -1, xf = 0;
        bool arpChord = false;
        bool arpStep = false;   // mode arpégiateur : le motif est rejoué pas par pas au tempo de FL
        const stretch::ArpAnalysis* arpAn = nullptr;
        ArpStepState curStep, prevStep;
        int fadeIn = 0;
        juce::uint32 srcKey = 0;
        const juce::AudioBuffer<float>* prevData = nullptr;
        int dir = 1;   // sens de lecture (boucle ping-pong)
        double localT = 0.0;
    };

    static double wheelToSemis (int wheel) { return ((double) wheel - 8192.0) / 8192.0 * 2.0; }

    static float hermiteWrap (const float* d, int len, double pos)
    {
        const int i = (int) pos;
        const float t = (float) (pos - (double) i);
        auto at = [d, len] (int k) { k %= len; if (k < 0) k += len; return d[k]; };
        const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }

    static float hermite (const float* d, int len, double pos)
    {
        const int   i = (int) pos;
        const float t = (float) (pos - (double) i);
        auto at = [d, len] (int k) { return (k >= 0 && k < len) ? d[k] : 0.0f; };
        const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }

    void updateEnvelope()
    {
        auto v = [] (std::atomic<float>* p, float def) { return p != nullptr ? p->load() : def; };
        FlEnvelope::Settings s;
        s.delay = v (params.envDelay, 0.0f) / 1000.0f;
        s.attack = params.attack->load() / 1000.0f + softness * 0.008f;
        s.hold = v (params.envHold, 0.0f) / 1000.0f;
        s.decay = params.decay->load() / 1000.0f;
        s.sustain = params.sustain->load();
        s.release = params.release->load() / 1000.0f;
        s.tA = v (params.atkTen, 0.0f); s.tD = v (params.decTen, 0.0f); s.tR = v (params.relTen, 0.0f);
        env.setParameters (s);
    }

    void finish()
    {
        env.reset();
        sound = nullptr;
        for (auto& st : state) st.active = false;
        for (auto& p : positions) p.store (-1.0f, std::memory_order_relaxed);
        clearCurrentNote();
    }

    const VoiceParams& params;
    std::array<RoundRobin, maxLayers>& roundRobin;
    const LayerSynth& synth;
    std::array<std::atomic<float>, maxLayers>& positions;

    LayerSound* sound = nullptr;
    std::array<LayerState, maxLayers> state {};
    FlEnvelope env;
    juce::Random rng;
    int midiNote = 60;
    double bendSemis = 0.0;
    float velGain = 1.0f, softness = 0.0f, toneCoef = 1.0f, glideSemis = 0.0f;
    double slideCur = 0.0, slideFrom = 0.0, slideTarget = 0.0, slideLen = 0.0, slidePos = 0.0;   // note slide façon FL
public:
    int slideNote = -1;   // note slide vers laquelle ce son a glissé (-1 = aucune)
private:
    bool released = false;
    double glideTotal = 1.0, glidePos = 0.0;
};

// note slide (comme FL) : les notes tenues de la même couleur glissent jusqu'à la note slide, pendant sa durée
inline bool LayerSynth::retargetSlides (int note, double samplesOverride)
{
    const double bpm = hostBpm != nullptr ? juce::jmax (20.0, hostBpm->load()) : 120.0;
    const double samples = samplesOverride > 0.0 ? samplesOverride : slideLen16 / 16.0 * 60.0 / bpm * getSampleRate();
    bool any = false;
    for (auto* v : voices)
        if (auto* lv = dynamic_cast<LayerVoice*> (v))
            if (lv->isVoiceActive() && (lv->isKeyDown() || (lv->slideNote >= 0 && slideSwallow[(size_t) lv->slideNote]))
                && notePart[(size_t) juce::jlimit (0, 127, lv->getCurrentlyPlayingNote())] == curPart)
            {
                lv->slideTo (note, samples);
                any = true;
            }
    return any;
}

// relâchement d'une note : gère les sons qui ont glissé vers une note slide (ils continuent tant qu'elle est tenue)
inline void LayerSynth::noteOff (int midiChannel, int midiNoteNumber, float velocity, bool allowTailOff)
{
    const int note = juce::jlimit (0, 127, midiNoteNumber);
    if (slideSwallow[(size_t) note])
    {
        // fin d'une note slide : les sons qui ont glissé jusqu'à elle s'arrêtent si leur touche d'origine est déjà relâchée
        slideSwallow[(size_t) note] = false;
        for (auto* v : voices)
            if (auto* lv = dynamic_cast<LayerVoice*> (v))
                if (lv->isVoiceActive() && lv->slideNote == note)
                {
                    lv->slideNote = -1;
                    if (! lv->isKeyDown()) stopVoice (lv, velocity, allowTailOff);
                }
        return;
    }
    held = juce::jmax (0, held - 1);
    heldNotes[(size_t) note] = false;
    updateChord();
    bool kept = false;
    for (auto* v : voices)
        if (auto* lv = dynamic_cast<LayerVoice*> (v))
            if (lv->isVoiceActive() && lv->getCurrentlyPlayingNote() == midiNoteNumber
                && lv->slideNote >= 0 && slideSwallow[(size_t) lv->slideNote])
            {
                lv->setKeyDown (false);   // la touche d'origine est relâchée, mais le son continue sur la note slide encore tenue
                kept = true;
            }
    if (! kept) { juce::Synthesiser::noteOff (midiChannel, midiNoteNumber, velocity, allowTailOff); return; }
    for (auto* v : voices)
        if (auto* lv = dynamic_cast<LayerVoice*> (v))
            if (lv->isVoiceActive() && lv->getCurrentlyPlayingNote() == midiNoteNumber && lv->isKeyDown())
            {
                lv->setKeyDown (false);
                stopVoice (lv, velocity, allowTailOff);
            }
}
