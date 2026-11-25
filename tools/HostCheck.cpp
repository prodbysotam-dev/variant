// Teste les plugins comme le font les logiciels de musique (FL Studio, Ableton, Logic, Cubase, Studio One, REAPER, Bitwig…) :
//  - chargement, fenêtre ouverte / fermée plusieurs fois
//  - 44,1 / 48 / 96 kHz, blocs fixes (32 à 2048) et blocs de taille variable (FL, Ableton)
//  - transport arrêté / en lecture à plusieurs tempos, boucle
//  - sauvegarde puis rechargement du projet (état), automation rapide de tous les réglages
//  - bypass, rendu hors temps réel (export), piste mono, deux instances en même temps, réinitialisation
//  - pas de NaN / pas d'explosion du son, et coût processeur mesuré
// Utilisation : VariantHostCheck <plugin.vst3 | plugin.component> ...   (code de sortie 0 = tout va bien)
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <iostream>

namespace
{
    struct Head : juce::AudioPlayHead
    {
        double bpm = 120.0, sr = 44100.0; juce::int64 pos = 0; bool playing = false, looping = false;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo i;
            i.setBpm (bpm); i.setIsPlaying (playing); i.setTimeInSamples (pos); i.setTimeInSeconds (pos / sr);
            i.setPpqPosition (pos / sr * bpm / 60.0); i.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
            i.setBarCount ((juce::int64) (pos / sr * bpm / 240.0)); i.setPpqPositionOfLastBarStart (std::floor (pos / sr * bpm / 240.0) * 4.0);
            i.setIsLooping (looping);
            if (looping) i.setLoopPoints (juce::AudioPlayHead::LoopPoints { 0.0, 8.0 });
            return i;
        }
    };

    struct Result { bool ok = true; juce::String why; double peak = 0.0; };

    // joue « seconds » secondes ; instrument : accord tenu + notes ; effet : sinusoïde + bruit en entrée
    void run (juce::AudioPluginInstance& p, Head& h, double sr, int maxBlock, double seconds, bool variable, bool automate,
              bool bypass, Result& r, juce::Random& rng, int channels = -1)
    {
        const bool inst = p.getTotalNumInputChannels() == 0;
        const int ch = channels > 0 ? channels : juce::jmax (2, p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
        juce::AudioBuffer<float> b (ch, maxBlock);
        auto& params = p.getParameters();
        const juce::int64 total = (juce::int64) (seconds * sr);
        juce::int64 done = 0; int k = 0;
        h.sr = sr;
        while (done < total)
        {
            const int n = variable ? juce::jlimit (1, maxBlock, 1 + rng.nextInt (maxBlock)) : maxBlock;
            b.setSize (ch, n, false, false, true);
            b.clear();
            if (! inst)
                for (int c = 0; c < juce::jmin (2, ch); ++c)
                    for (int i = 0; i < n; ++i)
                        b.setSample (c, i, 0.3f * std::sin ((float) (done + i) * 6.2831853f * 220.0f / (float) sr) + 0.02f * (rng.nextFloat() - 0.5f));
            juce::MidiBuffer m;
            if (k == 1) for (int note : { 48, 60, 64, 67 }) m.addEvent (juce::MidiMessage::noteOn (1, note, 0.85f), 0);
            if (k > 1 && k % 37 == 0) { m.addEvent (juce::MidiMessage::noteOn (1, 72 + (k % 5), 0.7f), juce::jmin (n - 1, 3)); m.addEvent (juce::MidiMessage::noteOff (1, 72 + (k % 5)), n - 1); }
            if (k % 53 == 0) m.addEvent (juce::MidiMessage::pitchWheel (1, 8192 + rng.nextInt (4000) - 2000), 0);
            if (automate && ! params.isEmpty())   // automation comme un clip d'automation qui bouge vite
                for (int j = 0; j < 4; ++j)
                    if (auto* prm = params[rng.nextInt (params.size())])
                        prm->setValue (rng.nextFloat());
            if (bypass) p.processBlockBypassed (b, m); else p.processBlock (b, m);
            for (int c = 0; c < b.getNumChannels(); ++c)
                for (int i = 0; i < n; ++i)
                {
                    const float v = b.getSample (c, i);
                    if (! std::isfinite (v)) { r.ok = false; r.why = "NaN/infini"; return; }
                    r.peak = juce::jmax (r.peak, (double) std::abs (v));
                }
            done += n; h.pos += n; ++k;
            if (k % 40 == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil (5);   // laisse vivre les fils (chargement des sons…)
        }
        if (r.peak > 8.0) { r.ok = false; r.why = "son qui explose (crete " + juce::String (r.peak, 1) + ")"; }
    }

    void prepare (juce::AudioPluginInstance& p, double sr, int block, bool offline = false)
    {
        p.releaseResources();
        p.setNonRealtime (offline);
        p.setRateAndBufferSizeDetails (sr, block);
        p.prepareToPlay (sr, block);
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::AudioPluginFormatManager fm;
    fm.addDefaultFormats();
    juce::Random rng (1234);
    int failures = 0, tested = 0;
    for (int a = 1; a < argc; ++a)
    {
        const juce::String path = juce::String::fromUTF8 (argv[a]);
        bool found = false;
        for (auto* fmt : fm.getFormats())
        {
            if (! fmt->fileMightContainThisPluginType (path)) continue;
            juce::OwnedArray<juce::PluginDescription> types;
            fmt->findAllTypesForFile (types, path);
            for (auto* d : types)
            {
                found = true; ++tested;
                const auto label = d->name + " (" + fmt->getName() + ")";
                juce::StringArray problems;
                juce::String err;
                auto inst = fm.createPluginInstance (*d, 44100.0, 512, err);
                if (inst == nullptr) { std::cout << "ECHEC: " << label << " : chargement impossible " << err << std::endl; ++failures; continue; }
                Head head; inst->setPlayHead (&head);
                inst->enableAllBuses();
                prepare (*inst, 44100.0, 512);

                auto openEditor = [&] () -> bool
                {
                    if (! inst->hasEditor()) return true;
                    std::unique_ptr<juce::AudioProcessorEditor> ed (inst->createEditorIfNeeded());
                    if (ed == nullptr || ed->getWidth() < 50 || ed->getHeight() < 50) return false;
                    juce::Image img (juce::Image::ARGB, ed->getWidth(), ed->getHeight(), true);
                    juce::Graphics g (img);
                    ed->paintEntireComponent (g, false);
                    if (ed->isResizable()) ed->setSize (ed->getWidth() * 3 / 4, ed->getHeight() * 3 / 4);
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
                    inst->editorBeingDeleted (ed.get());
                    ed.reset();
                    return true;
                };
                // fenêtre ouverte à l'ajout (comme FL), on laisse 3 s pour charger les sons
                if (! openEditor()) problems.add ("fenetre");
                juce::MessageManager::getInstance()->runDispatchLoopUntil (3000);

                Result base; run (*inst, head, 44100.0, 512, 2.5, false, false, false, base, rng);
                if (! base.ok) problems.add ("44,1 kHz : " + base.why);
                const double soundPeak = base.peak;

                for (double sr : { 48000.0, 96000.0, 44100.0 })
                    for (int blk : { 32, 64, 256, 1024, 2048 })
                    {
                        prepare (*inst, sr, blk);
                        Result r; run (*inst, head, sr, blk, 0.6, false, false, false, r, rng);
                        if (! r.ok) problems.add (juce::String (sr / 1000.0, 1) + " kHz / " + juce::String (blk) + " : " + r.why);
                    }
                prepare (*inst, 48000.0, 1024);
                head.playing = true; head.bpm = 90.0; head.pos = 0;
                { Result r; run (*inst, head, 48000.0, 1024, 2.0, true, false, false, r, rng); if (! r.ok) problems.add ("blocs variables : " + r.why); }
                head.bpm = 140.0; head.looping = true;
                { Result r; run (*inst, head, 48000.0, 1024, 1.5, true, false, false, r, rng); if (! r.ok) problems.add ("tempo 140 + boucle : " + r.why); }
                { Result r; run (*inst, head, 48000.0, 1024, 2.0, true, true, false, r, rng); if (! r.ok) problems.add ("automation : " + r.why); }
                { Result r; run (*inst, head, 48000.0, 1024, 0.5, false, false, true, r, rng); if (! r.ok) problems.add ("bypass : " + r.why); }
                prepare (*inst, 44100.0, 4096, true);
                { Result r; run (*inst, head, 44100.0, 4096, 2.0, false, false, false, r, rng); if (! r.ok) problems.add ("export : " + r.why); }
                inst->reset();
                prepare (*inst, 44100.0, 512);
                head.playing = false; head.looping = false;

                // sauvegarde / rechargement du projet dans une nouvelle instance, puis les deux jouent en même temps
                juce::MemoryBlock state; inst->getStateInformation (state);
                auto inst2 = fm.createPluginInstance (*d, 44100.0, 512, err);
                if (inst2 == nullptr) problems.add ("deuxieme instance");
                else
                {
                    Head h2; inst2->setPlayHead (&h2);
                    inst2->enableAllBuses();
                    prepare (*inst2, 44100.0, 512);
                    inst2->setStateInformation (state.getData(), (int) state.getSize());
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (1500);
                    juce::MemoryBlock state2; inst2->getStateInformation (state2);
                    if (state.getSize() > 0 && state2.getSize() == 0) problems.add ("etat non relu");
                    Result r1, r2; run (*inst, head, 44100.0, 512, 0.5, false, false, false, r1, rng); run (*inst2, h2, 44100.0, 512, 0.5, false, false, false, r2, rng);
                    if (! r1.ok || ! r2.ok) problems.add ("deux instances");
                    inst2->releaseResources();
                    inst2.reset();
                }
                // piste mono (effets)
                if (inst->getTotalNumInputChannels() > 0)
                {
                    auto mono = inst->getBusesLayout();
                    for (auto& bus : mono.inputBuses) bus = juce::AudioChannelSet::mono();
                    for (auto& bus : mono.outputBuses) bus = juce::AudioChannelSet::mono();
                    if (inst->checkBusesLayoutSupported (mono) && inst->setBusesLayout (mono))
                    {
                        prepare (*inst, 44100.0, 512);
                        Result r; run (*inst, head, 44100.0, 512, 0.5, false, false, false, r, rng, 1);
                        if (! r.ok) problems.add ("mono : " + r.why);
                        auto st = inst->getBusesLayout();
                        for (auto& bus : st.inputBuses) bus = juce::AudioChannelSet::stereo();
                        for (auto& bus : st.outputBuses) bus = juce::AudioChannelSet::stereo();
                        inst->setBusesLayout (st);
                    }
                }
                // coût processeur : 10 s de son à 48 kHz / 256
                prepare (*inst, 48000.0, 256);
                const auto t0 = juce::Time::getMillisecondCounterHiRes();
                { Result r; run (*inst, head, 48000.0, 256, 10.0, false, false, false, r, rng); }
                const double cpu = (juce::Time::getMillisecondCounterHiRes() - t0) / 10000.0 * 100.0;
                if (cpu > 60.0) problems.add ("processeur " + juce::String (cpu, 0) + " %");
                for (int t = 0; t < 2; ++t) if (! openEditor()) { problems.add ("fenetre (reouverture)"); break; }

                inst->releaseResources();
                inst.reset();
                const bool ok = problems.isEmpty();
                std::cout << (ok ? "ok   : " : "ECHEC: ") << label << (d->isInstrument ? " instrument" : " effet")
                          << " | crete " << juce::String (soundPeak, 3) << " | cpu " << juce::String (cpu, 1) << " %"
                          << (ok ? juce::String() : " | " + problems.joinIntoString ("; ")) << std::endl;
                if (! ok) ++failures;
            }
        }
        if (! found) { std::cout << "ECHEC : rien trouve dans " << path << std::endl; ++failures; }
    }
    std::cout << tested << " plugins testes, " << failures << " echec(s)" << std::endl;
    return failures == 0 && tested > 0 ? 0 : 1;
}
