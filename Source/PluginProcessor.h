#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>
#include <array>
#include "SamplerVoice.h"
#include "VariationEngine.h"
#include "MidiEngines.h"
#include "MidiGenerator.h"
#include "FxSlots.h"
#include "Library.h"
#include "Analysis.h"
#include "Slicer.h"
#include "WtSynth.h"
#include "Harmonizer.h"
#include "PluginHost.h"
#include "Polish.h"

// Lecture simple d'un fichier pour écouter un son de la bibliothèque
class FilePreviewPlayer
{
public:
    void set (std::unique_ptr<juce::AudioBuffer<float>> b, double rate)
    {
        const juce::SpinLock::ScopedLockType l (lock);
        retired.reset();
        pending = std::move (b);
        pendingRate = rate;
        hasPending.store (true);
    }

    bool isPlaying() const { return playing.load(); }

    void render (juce::AudioBuffer<float>& out, double sr)
    {
        if (hasPending.load() && lock.tryEnter())
        {
            if (retired == nullptr) { retired = std::move (active); active = std::move (pending); rate = pendingRate; pos = 0; hasPending.store (false); }
            lock.exit();
        }
        if (active == nullptr) { playing.store (false); return; }
        const double inc = rate / sr;
        const int len = active->getNumSamples();
        playing.store (pos < len - 1);
        for (int i = 0; i < out.getNumSamples() && pos < len - 1; ++i, pos += inc)
            for (int ch = 0; ch < out.getNumChannels(); ++ch)
            {
                const float* s = active->getReadPointer (juce::jmin (ch, active->getNumChannels() - 1));
                const int p0 = (int) pos; const float t = (float) (pos - p0);
                out.addSample (ch, i, (s[p0] + (s[p0 + 1] - s[p0]) * t) * 0.8f);
            }
    }

private:
    juce::SpinLock lock;
    std::unique_ptr<juce::AudioBuffer<float>> pending, active, retired;
    std::atomic<bool> hasPending { false }, playing { false };
    double pendingRate = 44100, rate = 44100, pos = 0;
};

//==============================================================================
class VariantProcessor : public juce::AudioProcessor,
                         public juce::ChangeBroadcaster,
                         private juce::AudioProcessorValueTreeState::Listener,
                         private juce::AsyncUpdater,
                         private juce::Timer
{
public:
    static constexpr int numVoices = 48;   // assez de voix : pas de vol de note (clics) avec arpèges + couches

    VariantProcessor();
    ~VariantProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                     { return true; }

    const juce::String getName() const override         { return JucePlugin_Name; }
    bool acceptsMidi() const override                   { return true; }
    bool producesMidi() const override                  { return false; }
    bool isMidiEffect() const override                  { return false; }
    double getTailLengthSeconds() const override        { return 2.0; }

    int getNumPrograms() override                       { return 1; }
    int getCurrentProgram() override                    { return 0; }
    void setCurrentProgram (int) override               {}
    const juce::String getProgramName (int) override    { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==========================================================================
    // Couches de sons
    bool loadSample (const juce::File& file, int layer, bool autoSetup = true);
    // « Destroy » : le son de la couche est transformé en autre chose (nouveau fichier dans la banque, dossier Destroy).
    // Renvoie ce qui a été fait (vide = rien). Toujours à partir du son d'origine : on peut détruire encore et encore.
    juce::String destroyLayer (int layer, int intensity);
    bool restoreLayer (int layer);
    bool canRestore (int layer) const { return morphStill (layer) && destroyOrigin[(size_t) layer].existsAsFile() && getLayerFile (layer) != destroyOrigin[(size_t) layer]; }
    std::array<juce::File, maxLayers> destroyOrigin, destroyLast;
    // retour en arrière / refaire des métamorphoses (sons précédents de la couche)
    bool undoMorph (int layer);
    bool redoMorph (int layer);
    bool canUndoMorph (int layer) const { return morphStill (layer) && ! morphUndo[(size_t) layer].empty(); }
    bool canRedoMorph (int layer) const { return morphStill (layer) && ! morphRedo[(size_t) layer].empty(); }
    bool morphStill (int layer) const { return juce::isPositiveAndBelow (layer, maxLayers) && destroyLast[(size_t) layer] != juce::File() && getLayerFile (layer) == destroyLast[(size_t) layer]; }
    std::array<std::vector<juce::File>, maxLayers> morphUndo, morphRedo;
    bool loadSoundFontPreset (const juce::File& sf2File, int presetIndex, int layer);
    struct LayerSnapshot { juce::File file; int sf2 = -1; std::vector<std::pair<juce::String, float>> params; };
    LayerSnapshot layerSnapshot (int layer) const;
    void applyLayerSnapshot (int layer, const LayerSnapshot& s);
    bool restoringState = false, presetFxLoaded = false, inPresetLoad = false;
    juce::String presetName;   // preset chargé (affiché sur le bouton de la banque ; vidé dès qu'on change de son)
    juce::String libView;      // catégorie ouverte dans la banque (retrouvée quand on rouvre le plugin)
    bool syncingScale = false;   // gamme reçue d'un autre Variant (ne pas la renvoyer)
    void dropPresetFx();
    void resetToLaunchState();
    // Qualité+ et effets auto : type de chaque couche (basse, lead…) et chaîne d effets posée automatiquement
    std::array<int, maxLayers> layerCat {};
    void applyAutoFx (int layer);
    bool trimLayerStart (int layer);   // « Couper le blanc » : Départ placé juste avant l attaque (false = pas de blanc)
    void resetSoundOptions (int layer);   // nouveau son principal : options de jeu remises à leur réglage de base
    void resetFxStores();                 // courbes / motifs perso des effets remis à zéro
    bool isMainSound (int layer) const;   // couche 1, ou le seul son chargé
    juce::String fxSignature() const;
    juce::String autoFxSig;
    bool stepLayerSound (int layer, int dir);   // flèches de la banque : son précédent / suivant dans le même dossier   // tout le son remis comme à l ouverture de Variant (avant de charger un preset)
private:
    bool loadSampleImpl (const juce::File& file, int layer, bool autoSetup);
    bool loadSampleImpl2 (const juce::File& file, int layer, bool autoSetup);
    bool loadingSample = false;
    bool loadSoundFontPresetImpl (const juce::File& sf2File, int presetIndex, int layer);
    void clearLayerImpl (int layer);
    void pushLayerStep (int layer, const LayerSnapshot& before);
public:
    // SoundFont chargée dans une couche : zones, preset choisi et liste des presets
    std::array<std::shared_ptr<const std::vector<KeyZone>>, maxLayers> layerZones;
    std::array<int, maxLayers> sf2Preset = [] { std::array<int, maxLayers> a {}; a.fill (-1); return a; }();
    std::array<juce::StringArray, maxLayers> sf2Names;
    std::array<std::vector<int>, maxLayers> sf2Indices;
    std::function<void (const juce::File&, int)> onSoundFontDropped;   // l éditeur montre la liste des presets
    int  loadFiles (const juce::StringArray& files, int startLayer);
    void autoSetupLayer (int layer);
    void autoSetupAll();
    void clearLayer (int layer);
    void regenerateVariations (bool newSeed);
    static bool isSupportedFile (const juce::File& file);
    static juce::String getSupportedWildcard() { return "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3;*.sf2"; }
    static juce::String layerId (const char* base, int layer) { return juce::String (base) + juce::String (layer + 1); }
    static juce::String rootId (int layer) { return layer == 0 ? juce::String ("root") : "root" + juce::String (layer + 1); }

    LayerSound::Ptr getSound() const;
    juce::File getLayerFile (int layer) const;
    float getVoicePosition (int voice, int layer) const { return voicePositions[(size_t) voice][(size_t) layer].load (std::memory_order_relaxed); }
    int getLastVariant (int layer) const { return roundRobins[(size_t) layer].last.load(); }
    int selectedLayer = 0;   // couche affichée dans l'interface

    // Bibliothèque
    void ensureLibrary();
    void previewFile (const juce::File& f);
    std::atomic<int> libraryVersion { 0 };
    std::atomic<bool> libraryBuilding { false }, libraryEnsured { false };
    // son par défaut au lancement (pour entendre quelque chose tout de suite)
    juce::File defaultSoundFile() const;
    void loadDefaultSoundIfEmpty();
    bool stateWasRestored = false;
    bool isEmptyInstrument() const;
    // page Sound design au premier plan : le clavier joue le synthé seul (les couches et les VST se taisent)
    std::atomic<bool> synthFocus { false };

    // MIDI depuis l'interface (boutons d'accords) : ignore accords/arpège
    void playUiNotes (const std::vector<int>& notes, bool on, float vel = 0.8f);

    ChordEngine::Settings getChordSettings() const;
    arp::Settings getArpSettings() const;
    int getKey() const;
    bool isHostPlaying() const { return hostPlayingA.load(); }   // FL en lecture (Play)
    int getScale() const;
    double getHostBpm() const { return hostBpm.load(); }

    void applyChordStyle (int mood);
    void applyArpStyle (int mood);
    void randomizeArp (bool patternOnly);
    void surpriseGuide();
    // goûts : j'aime / pas mon style -> le générateur s'en inspire
    void rateProgression (bool like);
    int likedCount() const { return (int) likedSeqs.size(); }
    bool tasteOn = true;
    std::vector<std::vector<int>> likedSeqs, dislikedSeqs;
    float tasteVariety = 0.5f, tasteSpice = 0.5f;
    void loadTaste();
    bool tasteLoaded = false;
    void saveTaste() const;
    void randomizeEffect (int slot);
    void setParam (const juce::String& id, float realValue);
    float getParam (const juce::String& id) const;
    std::array<float, maxLayers> autoLevelDb {};   // baisse appliquée au chargement par le niveau auto (0 = rien)

    // Effets (slots)
    int  addEffect (int type);
    void removeEffect (int slot);
    void moveEffect (int slot, int dir);
    fxs::Stores fxStores;   // courbes EQ, motifs glitch et enveloppes perso de cette instance
    std::array<juce::String, fxs::numSlots> fxNames;   // noms donnés aux effets par l'utilisateur (vide = nom d'origine)
    void applyEffectPreset (int slot, int preset);
    int  getSlotType (int slot) const { return (int) pv (("fx" + juce::String (slot + 1) + "Type").toRawUTF8()); }
    static juce::String slotId (int slot, const juce::String& what) { return "fx" + juce::String (slot + 1) + what; }

    // Guide MIDI / progression éditable
    gen::Settings genSettings;
    gen::Progression prog;
    juce::StringArray genAvoid;   // progressions proposées récemment (gardées entre les sessions) : le générateur en prend d'autres
    gen::Result   genResult;
    std::atomic<int> guideVersion { 0 };
    void regenerateGuide (bool newSeed);
    void conformChordToScale (gen::Chord& c) const;   // accord ramené dans la gamme si « Hors gamme » = 0
    void conformGuideToScale();
    void syncHostGuide();   // séquence jouée avec le Play de FL
    double guideStart = 0.0;   // départ de la lecture (clic sur la règle du piano roll)
    void setGuideStart (double beat) { guideStart = juce::jmax (0.0, beat); if (preview.isPlaying()) preview.seek (guideStart); }
    PreviewPlayer hostGuide;
    std::atomic<bool> hostPlayGuide { false }, hostGuideHasNotes { false };   // éteint par défaut : le Play de FL joue SON piano roll, pas les accords du plugin
    juce::uint64 hostGuideHash = 0;
    bool hostGuideWasActive = false;
    juce::MidiBuffer hostFilterBuf;
    void startEmptyGuide();
    // presets (arpège + son + réglages)
    juce::File findLibrarySound (const std::vector<const char*>& keywords, juce::Random& rng);
    bool applyFactoryPreset (int index);
    int ensureFactoryPresets (bool regenerate);   // presets usine écrits dans la banque de sons
    bool saveUserPreset (const juce::String& name);
    bool loadUserPreset (const juce::File& file);
    // presets complets (couches, VST, synthé, effets) rangés dans le dossier de l utilisateur
    static juce::File userPresetFolder();
    juce::File saveFullPreset (const juce::String& name);
    void writeFxStores (juce::ValueTree& t);
    void readFxStores (const juce::ValueTree& t);
    void writeHosted (juce::ValueTree& t);
    void readHosted (juce::ValueTree& t, bool unloadMissing);
    void fxAction (std::function<void()> action);   // modification de la chaîne d effets, annulable d un bloc
    static juce::StringArray fxChainStyleNames();
    void randomFxChain (int style);   // -1 = style au hasard
    int autoLayers (int count);   // couches complémentaires piochées dans la bibliothèque
    void renderGuide();
    void transposeGuide (int newKey);
    void setGuideLength (int bars);
    // édition d'un accord
    void setGuideChordIntervals (int i, const std::vector<int>& iv);
    void setGuideChordRoot (int i, int pitchClass);
    void setGuideChordInversion (int i, int inversion);
    void shiftGuideChordOctave (int i, int delta);
    void moveGuideChord (int i, double start);
    void resizeGuideChord (int i, double length);
    void deleteGuideChord (int i);
    int  duplicateGuideChord (int i);
    int  addGuideChord (double start);
    void regenerateGuideChord (int i);

    // notes éditables (piano roll du plugin)
    std::vector<gen::Note> notes;
    bool notesEdited = false;
    int  addGuideNote (double start, int pitch, double length, int part);
    void moveGuideNote (int i, double start, int pitch);
    void resizeGuideNote (int i, double length);
    void deleteGuideNote (int i);
    void toggleGuideSlide (int i);
    // édition façon FL (plusieurs notes)
    void deleteGuideNotes (const std::vector<int>& idx);
    void transposeGuideNotes (const std::vector<int>& idx, int semis);
    void shiftGuideNotes (const std::vector<int>& idx, double beats, int semis);   // sans annulation (pendant un glisser)
    void quantizeGuideNotes (const std::vector<int>& idx, double grid);
    void legatoGuideNotes (const std::vector<int>& idx);
    std::vector<int> pasteGuideNotes (const std::vector<gen::Note>& clip, double offset);
    void splitGuideNote (int i, double beat);
    void setGuideNotesVelocity (const std::vector<int>& idx, float vel);
    bool importMidiFile (const juce::File& f);
    void setGuideNoteVelocity (int i, float vel);
    std::array<bool, 3> enabledParts() const { return { genSettings.chords, genSettings.bass, genSettings.arp }; }

    // clavier d'ordinateur : 0 = désactivé, 1 = notes, 2 = accords
    int kbMode = 0, kbLayout = 0, kbOctave = 5;
    int sliceBank = 0;   // banque de pads affichée (le clavier joue cette banque)
    slicer::ChopSettings chopSettings;   // réglages de chaque chop (clic droit sur un pad) : volume, hauteur, pan, inversé
    std::vector<int> keyboardChord (int index) const;

    // « Prise » (page Son) : enregistre ce que tu joues en MIDI + le son de Variant en WAV, à glisser dans FL
    void takeArm();     // prêt : démarre à la première note
    void takeStop();
    void takeClear();
    int takeStatus() const { return takeState.load(); }   // 0 rien, 1 attend la 1re note, 2 en cours, 3 fini
    void setTakeSource (int s) { takeSource.store (s); }   // 0 = notes jouées, 1 = notes de l'arpège
    double takeSeconds() const { return takeRate > 0.0 ? (double) takeLen.load() / takeRate : 0.0; }
    juce::File exportTakeWav();
    juce::File exportTakeMidi();

    // enregistrement MIDI dans le plugin
    void armRecording (bool on);
    bool isRecordArmed() const { return recArmed.load(); }
    bool isRecordingStarted() const { return recStarted.load(); }
    double recordedBeats() const { return recBeats.load(); }
    int recQuantize = 1;   // 0 aucune, 1 = 1/16, 2 = 1/8

    // construire des accords à partir des notes du piano roll (notes gardées / notes fondamentales)
    wt::Engine wtSynth;   // synthé à tables d'ondes (page Sound design)
    void buildChordsFromNotes (int variant);
    // « Mes chops » : zones choisies à la main sur le sample (début, fin), chacune devient une tranche
    std::vector<std::pair<int, int>> sliceZones;
    bool sliceZoneMode = false;
    void setSliceZones (std::vector<std::pair<int, int>> z, bool undoable = true);
    void setSliceZoneMode (bool on);
    void applySliceZones();
    std::vector<gen::Note> harmonySrc, harmonyOut;   // notes de départ / résultat de « Accords autour de mes notes »

    std::atomic<float> outLevel { 0.0f }, bassLevel { 0.0f };
    float bassLp = 0.0f;

    // annuler / rétablir
    void beginEdit();
    bool undo();
    bool redo();

    // historique global (Ctrl+Z / Ctrl+Y) : réglages, notes, repères et motifs du slicer
    void pushHistory (std::function<void()> undoFn, std::function<void()> redoFn);
    bool undoAny();
    bool redoAny();
    void sliceHistoryMark();     // à appeler avant de modifier les repères
    void sliceHistoryCommit();   // après : ajoute une étape si quelque chose a changé
    void clearSlicePattern();
    void setSlicePattern (std::vector<gen::Note> p, double len);

    // Outils d'analyse
    struct AnalysisResult
    {
        juce::String name;
        double seconds = 0, bpm = 0;
        float bpmConfidence = 0;
        std::vector<analysis::KeyCandidate> keys;
        std::vector<gen::Note> midi;
        double midiLength = 0;
    };
    void analyseFile (const juce::File& f);
    void analyseLayer (int layer);
    void convertToMidi (int mode, float sensitivity, int quantize);
    AnalysisResult getAnalysis() const { const juce::ScopedLock l (analysisLock); return analysisRes; }
    std::atomic<int> analysisVersion { 0 };
    std::atomic<bool> analysisBusy { false };
    void previewAnalysisAudio();
    void previewAnalysisMidi (bool play);
    juce::File exportAnalysisMidi();
    void sendAnalysisToGuide();
    void stopFilePreview();
    bool isFilePreviewing() const { return filePreview.isPlaying(); }

    void previewGuide (bool play);
    bool isPreviewing() const { return preview.isPlaying(); }
    float getPreviewProgress() const { return preview.getProgress(); }
    juce::File getExportFolder() const;
    juce::File exportGuide (int layer); // 0 accords, 1 basse, 2 arpège, 3 tout
    static juce::String layerName (int layer);

    int themeIndex = 0;

    // Obliterate : tout remettre à zéro (comme un plugin fraîchement ouvert) + explosion 8 bits
    void obliterate();
    void playExplosion() { sfxPos.store (0); }
    static void renderExplosion (juce::AudioBuffer<float>& out, double sr);

    // Slicer : découpe un sample en tranches jouables (C5 = tranche 1…)
    bool loadSliceFile (const juce::File& f);
    // zone du sample utilisée pour les chops (en échantillons, fin = -1 : jusqu'au bout)
    void setSliceRegion (int startSample, int endSample);
    std::pair<int, int> getSliceRegion() const { return { sliceRegA, sliceRegB < 0 ? sliceSource.getNumSamples() : sliceRegB }; }
    int getSliceSourceLength() const { return sliceSource.getNumSamples(); }
    bool useLayerForSlices (int layer);
    bool sliceAnalysedSound();
    void recomputeSlices();
    void addSliceMarker (int sample);
    void removeSliceMarker (int index);
    void moveSliceMarker (int index, int sample);
    void playSlice (int index, bool on);
    slicer::SliceSound::Ptr getSliceSound() const { const juce::SpinLock::ScopedLockType l (sliceLock); return sliceSound; }
    int getSliceCount() const { return sliceCountNow.load(); }
    std::atomic<int> sliceVersion { 0 }, lastSlice { -1 };
    std::atomic<double> sliceBpm { 0.0 };     // tempo détecté du sample découpé
    std::atomic<double> sliceNamedBpm { 0.0 };   // tempo écrit dans le nom du fichier (« 140bpm », « _130_ »…) : pris comme le bon
    std::atomic<bool> sliceBpmManual { false };  // tempo tapé à la main dans le Sampler : gardé
    void setSliceBpm (double bpm);                // BPM modifié dans le Sampler (0 = revenir à l'automatique)
    double nameBpmOf (const juce::File& f) const;
    double pickSliceBpm (double detected) const { return sliceBpmManual.load() ? sliceBpm.load() : (sliceNamedBpm.load() > 0.0 ? sliceNamedBpm.load() : detected); }
    void analyseBuffer (const juce::AudioBuffer<float>& b, double rate, const juce::String& name);

    // enregistrement des chops (clavier, pads, MIDI) -> motif réutilisable
    void armSliceRec (bool on);
    bool isSliceRecArmed() const { return sliceRecArmed.load(); }
    void startTapChop(); void tapChop(); void stopTapChop();   // « Taper mes chops »
    bool isTapChopping() const { return tapPlaying.load(); }
    bool isSliceRecStarted() const { return sliceRecStarted.load(); }
    double sliceRecBeats() const { return sliceRecLen.load(); }
    std::vector<gen::Note> getSlicePattern() const { const juce::ScopedLock l (slicePatLock); return slicePattern; }
    double getSlicePatternLength() const { return slicePatternLen; }
    std::atomic<int> slicePatternVersion { 0 };
    void previewSlicePattern (bool play);
    juce::File exportSlicePatternMidi();
    juce::File renderSlicePatternWav();
    bool slicePatternToLayer (int layer);
    void slicePatternToGuide();
    std::atomic<float> slicePlayPos { -1.0f };

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;
    ChordEngine chordEngine;
    Arpeggiator arpeggiator;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void parameterChanged (const juce::String& id, float) override;
    void handleAsyncUpdate() override
    {
        if (variationsDirty.exchange (false)) regenerateVariations (false);
        if (slicesDirty.exchange (false)) recomputeSlices();
        if (fxDirty.exchange (false)) fxChain.ensure (slotParams);
    }
    std::atomic<bool> variationsDirty { false }, slicesDirty { false }, fxDirty { false };
    // accordage lié entre couches
    std::array<float, maxLayers> lastTune {}, lastFine {};
    std::atomic<int> noLink { 0 };
    bool linking = false;
    void refreshTuneMemory();
    struct NoLink { std::atomic<int>& c; explicit NoLink (std::atomic<int>& x) : c (x) { ++c; } ~NoLink() { --c; } };
    void installSlices (std::vector<int> starts, bool manual);

    // enregistrement des chops
    struct SliceRecEvent { double beat; int slice; float vel; bool on; };
    std::vector<SliceRecEvent> sliceRecEvents;
    juce::SpinLock sliceRecLock;
    std::atomic<bool> sliceRecArmed { false }, sliceRecStarted { false };
    std::atomic<double> sliceRecLen { 0.0 };
    juce::int64 sliceRecStart = 0;
    mutable juce::CriticalSection slicePatLock;
    std::vector<gen::Note> slicePattern;
    double slicePatternLen = 4.0;
    std::atomic<float> sliceSpeed { 1.0f }, sliceTimeRate { 1.0f };

    // son d'explosion (joué par-dessus tout, après les effets)
    juce::AudioBuffer<float> sfxBuffer;
    std::atomic<int> sfxPos { -1 };

    // slicer
    slicer::SliceSynth sliceSynth;
    std::atomic<const slicer::StretchVer*> sliceStretchVer { nullptr };   // sample étiré pré-calculé (Stretch du Sampler)
    std::vector<std::unique_ptr<slicer::StretchVer>> sliceVers;   // versions gardées un moment (une voix peut encore les lire)
    std::atomic<bool> sliceStretchBusy { false };
    std::shared_ptr<int> sliceAlive = std::make_shared<int> (0);
    std::shared_ptr<std::atomic<int>> bgRunning = std::make_shared<std::atomic<int>> (0);   // calculs du Sampler en cours
    void updateSliceStretch();
    std::atomic<bool> tapPlaying { false };
    std::atomic<double> tapPos { 0.0 };
    bool tapSession = false;
    slicer::SliceParams sliceParams;
    mutable juce::SpinLock sliceLock;
    slicer::SliceSound::Ptr sliceSound;
    juce::Array<slicer::SliceSound::Ptr> retiredSlices;
    juce::AudioBuffer<float> sliceSource;
    std::shared_ptr<const juce::AudioBuffer<float>> sliceShared;
    double sliceRate = 44100.0;
    juce::String sliceName;
    juce::File sliceFile;
    int sliceLayer = -1;          // couche d'origine (-1 = fichier)
    bool sliceManual = false;     // repères modifiés à la main
    int sliceRegA = 0, sliceRegB = -1;
    std::atomic<int> sliceCountNow { 0 };
    bool slicerWasOn = false;
    juce::MidiBuffer emptyMidi;
    void timerCallback() override;
    void prepareStretch();
    int timerTicks = 0;
    bool detectedLoopLike = false, detectedArpLike = false;
    bool arpTurnedOnByLayer = false;   // l arpégiateur de Variant a été allumé par le bouton Arpégiateur d une couche
    void installSound (LayerSound::Ptr s);
    struct Snapshot { gen::Progression prog; std::vector<gen::Note> notes; bool notesEdited; };
    std::vector<Snapshot> undoStack, redoStack;
    struct HistoryStep { std::function<void()> undo, redo; };
    std::vector<HistoryStep> history, future;
    std::vector<int> sliceMarkBefore;
    bool inHistory = false;
    void pushUndo();
    juce::ThreadPool pool { 1 };
    mutable juce::CriticalSection analysisLock;
    AnalysisResult analysisRes;
    std::vector<float> analysisAudio;
    juce::File analysisSource;
    void guideEdited();
    float pv (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    juce::AudioFormatManager formatManager;
    LayerSynth synth;
    VoiceParams voiceParams;
    std::array<RoundRobin, maxLayers> roundRobins;
    std::array<std::array<std::atomic<float>, maxLayers>, numVoices> voicePositions;

    mutable juce::SpinLock soundLock;
    LayerSound::Ptr currentSound;
    std::array<std::atomic<bool>, maxLayers> layerValid {};
    juce::Array<LayerSound::Ptr> retiredSounds;
    std::array<juce::File, maxLayers> layerFiles;
    std::array<juce::AudioBuffer<float>, maxLayers> sourceBuffers;
    std::array<double, maxLayers> sourceRates = [] { std::array<double, maxLayers> a {}; a.fill (44100.0); return a; }();
    // identifiant unique de chaque source (le cache du stretch et le mode arpégiateur le reconnaissent par lui)
    std::array<juce::uint32, maxLayers> sourceUid {};
    std::array<juce::uint64, maxLayers> sourceSig {};
    std::array<std::shared_ptr<const stretch::ArpAnalysis>, maxLayers> sourceArp;
    std::array<juce::uint32, maxLayers> sourceGen {};   // +1 à chaque nouveau son chargé dans la couche
    juce::uint32 variationSeed = 1;

    // MIDI temps réel
    MidiScheduler scheduler;
    PreviewPlayer preview;
    FilePreviewPlayer filePreview;
    LayerRouter router;
    CutFilter cutFilter;
    juce::MidiBuffer cutBuffer;
    struct TakeEvent { juce::int64 sample; int note; float vel; bool on; };
    std::atomic<int> takeState { 0 }, takeLen { 0 };
    std::atomic<int> takeSource { 0 };   // 0 = notes jouées, 1 = notes qui sortent de l'arpège (page Arpège)
    juce::SpinLock takeLock;
    juce::AudioBuffer<float> takeAudio;
    std::vector<TakeEvent> takeEvents;
    juce::int64 takeStart = 0;
    double takeBpm = 120.0, takeRate = 0.0;
    void takeCaptureMidi (const juce::MidiBuffer& m, juce::int64 blockStartSample, double bpm);
    void takeCaptureAudio (const juce::AudioBuffer<float>& b, int n, juce::int64 blockStartSample);
    struct RecEvent { double beat; int note; float vel; bool on; };
    std::vector<RecEvent> recEvents;
    juce::SpinLock recLock;
    std::atomic<bool> recArmed { false }, recStarted { false };
    std::atomic<double> recBeats { 0.0 };
    juce::int64 recStartSample = 0;
    juce::MidiMessageCollector uiMidi;
    juce::MidiBuffer stageBuffer, arpBuffer, uiBuffer, routeBuffer, padBuffer, restBuffer;
    juce::MidiBuffer wtMidi, focusBuffer;
    juce::int64 blockStart = 0;
    bool chordWasOn = false, arpWasOn = false;
    std::atomic<double> hostBpm { 120.0 };
    std::atomic<double> hostPpqA { 0.0 }, hostBpmA { 120.0 };
    std::atomic<bool> hostPlayingA { false };
public:
    std::array<std::atomic<float>, maxLayers> autoLoopBpm {};   // tempo deviné de chaque couche (boucles)
    std::array<std::atomic<float>, maxLayers> nameLoopBpm {};   // tempo écrit dans le nom du fichier (0 = aucun)
    stretch::Cache stretchCache;   // notes pré-calculées du mode Stretch (thread de fond)
    static constexpr int numHosted = 8;
    std::array<HostedInstrument, numHosted> hostedSlots;   // VST (Serum, Zenology…) ouverts dans Variant (jusqu'à 4, en couches)
    juce::MemoryBlock hostedPendingState;

    // Visualiseur de l'arpège : dernières notes jouées (entrée = notes tenues, sortie = notes de l'arpège)
    struct ArpViewEvent { double beat = 0.0; int note = 0; float vel = 0.0f; bool on = false, input = false; };
    static constexpr int arpViewSize = 1024;
    std::array<ArpViewEvent, arpViewSize> arpView {};
    std::atomic<int> arpViewWrite { 0 };
    std::atomic<double> arpViewNow { 0.0 };   // temps actuel (en temps / noires)
    double arpViewClock = 0.0;
    void arpViewPush (const juce::MidiBuffer& b, double bps, bool input)
    {
        for (const auto meta : b)
        {
            const auto m = meta.getMessage();
            if (! m.isNoteOnOrOff()) continue;
            const int w = arpViewWrite.load (std::memory_order_relaxed);
            arpView[(size_t) (w % arpViewSize)] = { arpViewClock + meta.samplePosition * bps, m.getNoteNumber(), m.getFloatVelocity(), m.isNoteOn(), input };
            arpViewWrite.store (w + 1, std::memory_order_release);
        }
    }

    // Réglages qui n'ont pas de paramètre à eux (synthé, VST ouverts dans Variant) : reliés à des paramètres
    // automatisables dans FL. FL -> réglage dans le thread audio, réglage (interface, preset) -> FL dans un minuteur.
    struct Bridge
    {
        juce::RangedAudioParameter* rp = nullptr;
        std::atomic<float>* raw = nullptr;
        std::function<float()> get;
        std::function<void (float)> set;
        std::atomic<float> lastParam { 0.0f }, lastModel { 0.0f };
    };
    std::vector<std::unique_ptr<Bridge>> bridges;
    std::atomic<bool> bridgeHold { true };
    void setupBridges();
    void bridgesFromHost();   // thread audio
    void bridgesToHost();     // thread message
    void bridgesResync();     // après un chargement : les réglages chargés passent dans les paramètres
    struct BridgeTimer : public juce::Timer
    {
        explicit BridgeTimer (VariantProcessor& q) : p (q) {}
        void timerCallback() override { p.bridgesToHost(); }
        VariantProcessor& p;
    };
    BridgeTimer bridgeTimer { *this };
    static float guessLoopBpm (const juce::String& fileName, double seconds);
private:
    double currentSampleRate = 44100.0;
    float limEnv = 0.0f, limGain = 1.0f;
    std::array<bool, 128> heldIn {};
    // métronome
    std::array<juce::AudioBuffer<float>, 2> metroClicks;
    int metroSoundBuilt = -1, metroPos = -1;
    double metroRateBuilt = 0.0, metroBeat = 0.0;
    bool metroAccent = false, metroWasPreviewing = false;
    void buildMetroClicks (int sound);

    // Effets
    fxs::Chain fxChain;
    int reportedLatency = 0;
    std::array<fxs::SlotParams, fxs::numSlots> slotParams;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VariantProcessor)
};
