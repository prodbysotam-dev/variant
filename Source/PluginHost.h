#pragma once
// Un plugin VST3 (Serum, Zenology…) ouvert DANS Variant : il reçoit les notes (clavier, accords, arpège, piano roll),
// son son passe dans les effets de Variant, et sa fenêtre s'ouvre pour modifier le son directement.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

class HostedInstrument
{
public:
    HostedInstrument()
    {
       #if JUCE_PLUGINHOST_VST3
        fm.addFormat (new juce::VST3PluginFormat());
       #endif
       #if JUCE_PLUGINHOST_AU && JUCE_MAC
        fm.addFormat (new juce::AudioUnitPluginFormat());   // Mac : les Audio Units (Logic) s'ouvrent aussi dans Variant
       #endif
    }
    ~HostedInstrument() { closeWindow(); const juce::SpinLock::ScopedLockType l (lock); inst.reset(); }

    // dossier proposé quand on choisit un plugin à la main
    static juce::File defaultPluginFolder()
    {
       #if JUCE_MAC
        return juce::File ("/Library/Audio/Plug-Ins/VST3");
       #else
        return juce::File ("C:\\Program Files\\Common Files\\VST3");
       #endif
    }
    static juce::String pluginWildcard()
    {
       #if JUCE_MAC
        return "*.vst3;*.component";
       #else
        return "*.vst3";
       #endif
    }

    // ---- liste des plugins installés (sans les charger : rapide)
    static juce::Array<juce::File> installedPlugins()
    {
        juce::Array<juce::File> out;
       #if JUCE_MAC
        const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
        juce::StringArray dirs { "/Library/Audio/Plug-Ins/VST3", home.getChildFile ("Library/Audio/Plug-Ins/VST3").getFullPathName(),
                                 "/Library/Audio/Plug-Ins/Components", home.getChildFile ("Library/Audio/Plug-Ins/Components").getFullPathName() };
       #else
        juce::StringArray dirs { "C:\\Program Files\\Common Files\\VST3",
                                 juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getParentDirectory().getChildFile ("Local\\Programs\\Common\\VST3").getFullPathName() };
       #endif
        for (auto& d : dirs)
        {
            const juce::File dir (d);
            if (! dir.isDirectory()) continue;
            for (auto& f : dir.findChildFiles (juce::File::findFilesAndDirectories, true, pluginWildcard()))
            {
                const auto full = f.getFullPathName().replaceCharacter ('\\', '/');
                if (f.getParentDirectory().getFileExtension().equalsIgnoreCase (".vst3") || full.containsIgnoreCase (".vst3/Contents") || full.containsIgnoreCase (".component/Contents")) continue;   // le binaire dans un bundle
                if (f.getFileNameWithoutExtension().startsWithIgnoreCase ("Variant")) continue;   // pas lui-même ni ses effets
                out.addIfNotAlreadyThere (f);
            }
        }
        struct Cmp { static int compareElements (const juce::File& a, const juce::File& b) { return a.getFileNameWithoutExtension().compareIgnoreCase (b.getFileNameWithoutExtension()); } };
        Cmp c; out.sort (c);
        return out;
    }

    // infos des plugins déjà ouverts une fois (évite de démarrer le plugin 2 fois : Zenology met ~1 min à démarrer)
    static juce::File descCacheFile() { return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Variant").getChildFile ("plugins_ouverts.xml"); }
    static bool cachedDescription (const juce::File& file, juce::PluginDescription& out)
    {
        auto x = juce::XmlDocument::parse (descCacheFile());
        if (x == nullptr) return false;
        for (auto* e : x->getChildIterator())
        {
            juce::PluginDescription d;
            if (d.loadFromXml (*e) && d.fileOrIdentifier == file.getFullPathName() && d.lastFileModTime == file.getLastModificationTime()) { out = d; return true; }
        }
        return false;
    }
    static void rememberDescription (const juce::PluginDescription& d)
    {
        auto f = descCacheFile();
        f.getParentDirectory().createDirectory();
        auto x = juce::XmlDocument::parse (f);
        if (x == nullptr) x = std::make_unique<juce::XmlElement> ("PLUGINS");
        for (int i = x->getNumChildElements(); --i >= 0;)
            if (x->getChildElement (i)->getStringAttribute ("file") == d.fileOrIdentifier) x->removeChildElement (x->getChildElement (i), true);
        if (auto e = d.createXml()) x->addChildElement (e.release());
        x->writeTo (f);
    }

    // Le binaire d'un plugin ouvert dans Variant reste chargé jusqu'à la fermeture de FL :
    // sinon JUCE le décharge quand le dernier Variant se ferme, alors que le plugin (Serum, Diva…) a encore
    // ses propres threads -> FL figé à la fermeture de Variant / de FL. (C'est ce que font les vrais hôtes.)
    static void pinModule (const juce::File& file)
    {
       #if ! JUCE_WINDOWS
        juce::ignoreUnused (file);   // Mac : les bundles restent chargés par le système
        return;
       #else
        static auto* pinned = new juce::OwnedArray<juce::DynamicLibrary>();   // jamais libéré exprès
        static auto* paths = new juce::StringArray();
        auto bin = file;
        if (file.isDirectory()) bin = file.getChildFile ("Contents").getChildFile ("x86_64-win").getChildFile (file.getFileName());
        if (! bin.existsAsFile() || paths->contains (bin.getFullPathName())) return;
        auto lib = std::make_unique<juce::DynamicLibrary>();
        if (lib->open (bin.getFullPathName())) { paths->add (bin.getFullPathName()); pinned->add (lib.release()); }
       #endif
    }

    // ---- thread message
    void prepare (double sampleRate, int block)
    {
        sr = sampleRate; maxBlock = juce::jmax (64, block);
        tmp.setSize (8, maxBlock * 2);
        delayLen = (int) (sampleRate * 0.6) + 8;
        dl.assign ((size_t) delayLen, 0.0f); dr.assign ((size_t) delayLen, 0.0f); dw = 0;
        midiCopy.ensureSize (8192);
        const juce::SpinLock::ScopedLockType l (lock);
        if (inst != nullptr) { inst->releaseResources(); inst->setRateAndBufferSizeDetails (sr, maxBlock); inst->prepareToPlay (sr, maxBlock); }
    }

    void load (const juce::File& file, std::function<void (bool ok, const juce::String& msg)> done, const juce::MemoryBlock& stateToRestore = {})
    {
        const auto t0 = juce::Time::getMillisecondCounter();
        juce::PluginDescription desc;
        if (! cachedDescription (file, desc))
        {
            juce::OwnedArray<juce::PluginDescription> types;
            for (auto* f : fm.getFormats()) f->findAllTypesForFile (types, file.getFullPathName());
            if (types.isEmpty()) { if (done) done (false, "Impossible de lire ce plugin : " + file.getFileNameWithoutExtension()); return; }
            desc = *types[0];
            for (auto* t : types) if (t->isInstrument) { desc = *t; break; }
            rememberDescription (desc);
        }
        lastScanMs = (int) (juce::Time::getMillisecondCounter() - t0);
        closeWindow();
        std::weak_ptr<int> alive = token;
        createStart = juce::Time::getMillisecondCounter();
        fm.createPluginInstanceAsync (desc, sr, maxBlock, [this, alive, desc, file, done, stateToRestore] (std::unique_ptr<juce::AudioPluginInstance> p, const juce::String& err)
        {
            if (alive.expired()) return;   // Variant a été fermé entre-temps
            lastCreateMs = (int) (juce::Time::getMillisecondCounter() - createStart);
            if (p == nullptr) { if (done) done (false, err.isNotEmpty() ? err : juce::String ("Le plugin n'a pas pu s'ouvrir")); return; }
            // sortie stéréo, pas d'entrée audio (instrument)
            auto layout = p->getBusesLayout();
            for (auto& b : layout.inputBuses) b = juce::AudioChannelSet::disabled();
            if (! layout.outputBuses.isEmpty()) layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
            if (! p->setBusesLayout (layout)) p->enableAllBuses();
            p->setRateAndBufferSizeDetails (sr, maxBlock);
            p->prepareToPlay (sr, maxBlock);
            if (stateToRestore.getSize() > 0) p->setStateInformation (stateToRestore.getData(), (int) stateToRestore.getSize());
            std::unique_ptr<juce::AudioPluginInstance> old;
            {
                const juce::SpinLock::ScopedLockType l (lock);
                old = std::move (inst);
                inst = std::move (p);
                lastBend = -1;
                name = desc.name; path = file.getFullPathName(); isInstrument = desc.isInstrument;
            }
            old.reset();
            ++version;
            if (done) done (true, desc.name);
        });
    }

    void unload()
    {
        closeWindow();
        std::unique_ptr<juce::AudioPluginInstance> old;
        { const juce::SpinLock::ScopedLockType l (lock); old = std::move (inst); name = {}; path = {}; }
        old.reset();
        ++version;
    }

    bool isLoaded() const { return inst != nullptr; }
    juce::AudioPluginInstance* get() const { return inst.get(); }
    juce::String name, path;
    bool isInstrument = true;
    std::atomic<float> volume { 0.8f };
    std::atomic<bool> enabled { true };
    std::atomic<float> panPos { 0.0f }, transpose { 0.0f }, fineCents { 0.0f }, delayMs { 0.0f };   // comme les réglages d'une couche
    std::atomic<int> version { 0 };
    int lastScanMs = 0, lastCreateMs = 0;
    juce::uint32 createStart = 0;

    // fenêtre du plugin (son interface d'origine, pour choisir et modifier le preset)
    void openWindow()
    {
        if (inst == nullptr) return;
        if (window != nullptr) { window->toFront (true); return; }
        juce::AudioProcessorEditor* ed = inst->createEditorIfNeeded();
        if (ed == nullptr) ed = new juce::GenericAudioProcessorEditor (*inst);
        window = std::make_unique<Window> (name + "  ·  dans Variant", [this] { juce::MessageManager::callAsync ([this] { closeWindow(); }); });
        window->setContentOwned (ed, true);
        window->centreWithSize (window->getWidth(), window->getHeight());
        window->setVisible (true);
        window->toFront (true);
    }
    void closeWindow() { window.reset(); }
    bool windowOpen() const { return window != nullptr; }
    bool windowActive() const { return window != nullptr && window->isActiveWindow(); }

    // preset VST3 (.vstpreset) glissé sur Variant
    bool loadPresetFile (const juce::File& f)
    {
        if (inst == nullptr) return false;
        juce::MemoryBlock mb;
        if (! f.loadFileAsData (mb)) return false;
       #if JUCE_PLUGINHOST_VST3
        return juce::VST3PluginFormat::setStateFromVSTPresetFile (inst.get(), mb);
       #else
        return false;
       #endif
    }

    juce::MemoryBlock getPluginState() const
    {
        juce::MemoryBlock mb;
        const juce::SpinLock::ScopedLockType l (lock);
        if (inst != nullptr) inst->getStateInformation (mb);
        return mb;
    }

    // Réglages globaux de Variant que le plugin suit aussi (onglet Son, Slide, Variations) :
    // leurs valeurs par défaut ne changent rien au son du plugin.
    struct Follow
    {
        float tuneSemis = 0.0f, fineCents = 0.0f, gainDb = 0.0f;
        float velSens = 0.8f;                                   // « Vélocité » (0,8 = le plugin reçoit la vélocité jouée)
        float delayMs = 0.0f, attackMs = 0.0f, holdMs = 0.0f, decayMs = 400.0f, sustain = 1.0f, releaseMs = 80.0f;
        bool releaseOn = false;                                 // Relâche bougée : la queue du plugin suit la relâche de Variant
        int glideMode = 0; float glideMs = 120.0f, glideCurve = 0.4f, slideIn = 0.0f;
        bool varOn = false; int varStyle = 0; float varAmount = 0.0f;
    };
    static constexpr float defaultVelSens = 0.8f, defaultRelease = 80.0f;

    // ---- thread audio : le plugin joue les notes et son son est ajouté au mélange
    void process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, int n, juce::AudioPlayHead* ph, const Follow& fw)   // (pas de valeur par défaut : le compilateur du Mac la refuse ici)
    {
        const juce::SpinLock::ScopedTryLockType l (lock);
        if (! l.isLocked() || inst == nullptr || n > tmp.getNumSamples()) return;
        const int ch = juce::jmax (2, juce::jmax (inst->getTotalNumInputChannels(), inst->getTotalNumOutputChannels()));
        if (ch > tmp.getNumChannels()) return;
        juce::AudioBuffer<float> view (tmp.getArrayOfWritePointers(), ch, n);
        view.clear();
        midiCopy.clear();
        // réglages comme pour une couche : transposition (notes décalées), finesse + glide / slide (pitch bend)
        const int tr = (int) std::round (transpose.load() + fw.tuneSemis);
        const float range = (float) juce::jlimit (1, 96, bendRange.load());
        const float cents = fineCents.load() + fw.fineCents;
        auto sendBend = [&] (int pos)
        {
            const float semis = cents / 100.0f + glideCur;
            const int b = juce::jlimit (0, 16383, 8192 + (int) std::round (semis / range * 8191.0f));
            if (b != lastBend) { midiCopy.addEvent (juce::MidiMessage::pitchWheel (1, b), pos); lastBend = b; }
        };
        const bool on = enabled.load();
        const bool envActive = fw.delayMs > 0.5f || fw.attackMs > 0.5f || fw.holdMs > 0.5f || fw.sustain < 0.999f || fw.releaseOn;
        if (! envActive) { envStage = EnvOff; envLevel = 1.0f; }
        int nEnv = 0;
        auto envEvent = [&] (int pos, int type) { if (nEnv < (int) envEvents.size()) envEvents[(size_t) nEnv++] = { pos, type }; };

        auto it = midi.begin();
        constexpr int chunk = 32;
        for (int c = 0; c < n; c += chunk)
        {
            // glide / slide en cours : la hauteur glisse (pitch bend) jusqu'à la note
            if (glideFrom != 0.0f)
            {
                const double p = juce::jmin (1.0, glidePos / juce::jmax (1.0, glideLen));
                const double shaped = 1.0 - std::pow (1.0 - p, 1.0 + 3.0 * glideShape);
                glideCur = (float) (glideFrom * (1.0 - shaped));
                if (p >= 1.0) { glideFrom = 0.0f; glideCur = 0.0f; }
            }
            sendBend (c);
            const int cEnd = juce::jmin (n, c + chunk);
            for (; it != midi.end() && (*it).samplePosition < cEnd; ++it)
            {
                const auto meta = *it;
                auto m = meta.getMessage();
                const int pos = meta.samplePosition;
                if (m.isNoteOn())
                {
                    if (! on) continue;   // coupé : plus de nouvelles notes (les notes en cours se terminent)
                    const int in = m.getNoteNumber();
                    const int nn = in + tr;
                    if (nn < 0 || nn > 127) continue;
                    // vélocité (sensibilité) + variations : chaque note est jouée un peu différemment
                    float v = m.getFloatVelocity();
                    if (std::abs (fw.velSens - defaultVelSens) > 0.005f) v = std::pow (juce::jmax (0.001f, v), juce::jmax (0.02f, fw.velSens / defaultVelSens));
                    if (fw.varOn && fw.varAmount > 0.0f)
                    {
                        static const float spread[] = { 0.06f, 0.12f, 0.22f };
                        v *= 1.0f + (rng.nextFloat() * 2.0f - 1.0f) * spread[juce::jlimit (0, 2, fw.varStyle)] * fw.varAmount;
                    }
                    v = juce::jlimit (0.01f, 1.0f, v);
                    // glide (Slide) : depuis la note précédente / slide d'entrée
                    const juce::int64 when = clock + pos;
                    const bool sameInstant = when == lastOnClock;   // accord : pas de glide entre ses notes
                    float from = 0.0f;
                    if (lastOut >= 0 && ! sameInstant && (fw.glideMode == 2 || (fw.glideMode == 1 && heldCount > 0)))
                    {
                        from = (float) (lastOut - nn) + glideCur;
                        if (fw.glideMode == 1 && lastOut != nn) midiCopy.addEvent (juce::MidiMessage::noteOff (1, lastOut), pos);   // jeu lié
                    }
                    else if (std::abs (fw.slideIn) > 0.01f && ! sameInstant) from = fw.slideIn;
                    if (from != 0.0f || (sameInstant && glideFrom != 0.0f))
                    {
                        if (! sameInstant) { glideFrom = from; glidePos = 0.0; glideLen = fw.glideMs * 0.001 * sr; glideShape = fw.glideCurve; glideCur = from; }
                        sendBend (pos);
                    }
                    if (envActive) envEvent (pos, heldCount > 0 ? 1 : 0);
                    noteMap[(size_t) in] = nn;
                    ++heldCount; lastOut = nn; lastOnClock = when;
                    midiCopy.addEvent (juce::MidiMessage::noteOn (m.getChannel(), nn, v), pos);
                }
                else if (m.isNoteOff())
                {
                    const int in = m.getNoteNumber();
                    int nn = noteMap[(size_t) in];
                    if (nn < 0) nn = in + tr; else heldCount = juce::jmax (0, heldCount - 1);
                    noteMap[(size_t) in] = -1;
                    if (nn < 0 || nn > 127) continue;
                    m.setNoteNumber (nn);
                    if (envActive && heldCount == 0) envEvent (pos, 2);
                    midiCopy.addEvent (m, pos);
                }
                else if (m.isAllNotesOff() || m.isAllSoundOff())
                {
                    noteMap.fill (-1); heldCount = 0;
                    midiCopy.addEvent (m, pos);
                }
                else if (m.isPitchWheel()) continue;
                else midiCopy.addEvent (m, pos);
            }
            glidePos += (double) (cEnd - c);
        }
        clock += n;
        inst->setPlayHead (ph);
        inst->processBlock (view, midiCopy);
        if (envActive) applyEnvelope (view, n, fw, nEnv);
        const float g = on ? volume.load() * juce::Decibels::decibelsToGain (fw.gainDb, -60.0f) : 0.0f;
        const float pan = juce::jlimit (-1.0f, 1.0f, panPos.load());
        const float a = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        const float gl = g * std::cos (a) * 1.41421f, gr = g * std::sin (a) * 1.41421f;
        const int outs = inst->getTotalNumOutputChannels();
        const int dly = juce::jlimit (0, delayLen - 1, (int) std::round (delayMs.load() * 0.001 * sr));
        for (int i = 0; i < n; ++i)
        {
            const float xl = view.getSample (0, i), xr = view.getSample (juce::jmin (1, juce::jmax (0, outs - 1)), i);
            float yl = xl, yr = xr;
            if (dly > 0)   // retard de la couche
            {
                dl[(size_t) dw] = xl; dr[(size_t) dw] = xr;
                const int rd = (dw - dly + delayLen) % delayLen;
                yl = dl[(size_t) rd]; yr = dr[(size_t) rd];
                dw = (dw + 1) % delayLen;
            }
            out.addSample (0, i, yl * gl);
            if (out.getNumChannels() > 1) out.addSample (1, i, yr * gr);
        }
    }

    std::atomic<int> bendRange { 2 };   // plage du pitch bend réglée dans le plugin (±demi-tons) : finesse, glide et slide l'utilisent

private:
    // enveloppe de Variant appliquée au son du plugin (délai, attaque, tenue, déclin, maintien, relâche)
    enum EnvStage { EnvOff, EnvDuck, EnvDelay, EnvAttack, EnvHold, EnvDecay, EnvSustain, EnvRelease, EnvDone, EnvRest };
    struct EnvEv { int pos = 0, type = 0; };   // 0 = 1re note, 1 = note ajoutée, 2 = plus aucune note tenue
    std::array<EnvEv, 128> envEvents {};
    EnvStage envStage = EnvOff;
    float envLevel = 1.0f; double envT = 0.0;

    void applyEnvelope (juce::AudioBuffer<float>& b, int n, const Follow& fw, int nEv)
    {
        const double s = sr * 0.001;
        const float atkInc = fw.attackMs > 0.5f ? (float) (1.0 / (fw.attackMs * s)) : 1.0f;
        const float duckDec = (float) (1.0 / (2.0 * s));
        const float decK = (float) std::exp (-1.0 / juce::jmax (1.0, fw.decayMs * 0.2 * s));
        const float relK = (float) std::exp (-1.0 / juce::jmax (1.0, fw.releaseMs * 0.2 * s));
        const float sus = juce::jlimit (0.0f, 1.0f, fw.sustain);
        const int chans = juce::jmin (2, b.getNumChannels());
        int e = 0;
        for (int i = 0; i < n; ++i)
        {
            while (e < nEv && envEvents[(size_t) e].pos <= i)
            {
                const int t = envEvents[(size_t) e++].type;
                if (t == 0) { envStage = envLevel > 0.001f ? EnvDuck : (fw.delayMs > 0.5f ? EnvDelay : EnvAttack); envT = 0.0; }
                else if (t == 1) { envStage = EnvAttack; }
                else envStage = fw.releaseOn ? EnvRelease : EnvRest;
            }
            switch (envStage)
            {
                case EnvDuck:   envLevel -= duckDec; if (envLevel <= 0.0f) { envLevel = 0.0f; envStage = fw.delayMs > 0.5f ? EnvDelay : EnvAttack; envT = 0.0; } break;
                case EnvDelay:  envLevel = 0.0f; envT += 1.0; if (envT >= fw.delayMs * s) envStage = EnvAttack; break;
                case EnvAttack: envLevel += atkInc; if (envLevel >= 1.0f) { envLevel = 1.0f; envStage = fw.holdMs > 0.5f ? EnvHold : EnvDecay; envT = 0.0; } break;
                case EnvHold:   envT += 1.0; if (envT >= fw.holdMs * s) envStage = EnvDecay; break;
                case EnvDecay:  envLevel = sus + (envLevel - sus) * decK; if (std::abs (envLevel - sus) < 1.0e-4f) { envLevel = sus; envStage = EnvSustain; } break;
                case EnvSustain: envLevel = sus; break;
                case EnvRelease: envLevel *= relK; if (envLevel < 1.0e-4f) { envLevel = 0.0f; envStage = EnvDone; } break;
                case EnvDone:   envLevel = 0.0f; break;
                case EnvOff:    envLevel = 1.0f; break;
                case EnvRest:   break;
            }
            for (int c = 0; c < chans; ++c) b.setSample (c, i, b.getSample (c, i) * envLevel);
        }
    }

    // notes, glide
    std::array<int, 128> noteMap = [] { std::array<int, 128> a {}; a.fill (-1); return a; }();
    int heldCount = 0, lastOut = -1;
    juce::int64 clock = 0, lastOnClock = -1;
    float glideFrom = 0.0f, glideCur = 0.0f, glideShape = 0.4f;
    double glidePos = 0.0, glideLen = 1.0;
    juce::Random rng;

    struct Window : public juce::DocumentWindow
    {
        Window (const juce::String& t, std::function<void()> onClose)
            : juce::DocumentWindow (t, juce::Colour (0xff1c1c20), juce::DocumentWindow::closeButton), closed (std::move (onClose))
        { setUsingNativeTitleBar (true); setResizable (false, false); }
        void closeButtonPressed() override { if (closed) closed(); }
        std::function<void()> closed;
    };

    juce::AudioPluginFormatManager fm;
    mutable juce::SpinLock lock;
    std::unique_ptr<juce::AudioPluginInstance> inst;
    std::unique_ptr<Window> window;
    juce::AudioBuffer<float> tmp;
    juce::MidiBuffer midiCopy;
    std::shared_ptr<int> token = std::make_shared<int> (0);
    std::vector<float> dl, dr; int dw = 0, delayLen = 1; int lastBend = -1;
    double sr = 44100.0;
    int maxBlock = 512;
};
