// Tests des moteurs (sans interface) : variations, générateur, arpège, accords, pré-écoute.
#include <juce_audio_basics/juce_audio_basics.h>
#include <iostream>
#include <set>
#include <map>
#include "../Source/VariationEngine.h"
#include "../Source/MidiEngines.h"
#include "../Source/MidiGenerator.h"
#include "../Source/FxSlots.h"
#include "../Source/Library.h"
#include "../Source/Analysis.h"
#include "../Source/Slicer.h"
#include "../Source/KickBrain.h"
#include "KickTests.h"
#include "../Source/SoundFont.h"
#include "../Source/Presets.h"
#include "../Source/Harmonizer.h"
#include "../Source/SynthPresets.h"
#include "../Source/Stretcher.h"
#include "../Source/Polish.h"
#include "../Source/Destroy.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstring>

static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { ++failures; std::cout << "ECHEC: " << msg << "\n"; } else std::cout << "ok   : " << msg << "\n"; } while (0)

int main()
{
    std::cout << std::unitbuf;   // sortie immédiate (un plantage montre le dernier test)
    if (juce::SystemStats::getEnvironmentVariable ("VARIANT_META_DEMO", {}).isNotEmpty())   // outil de dev : exemples de Métamorphose (« a.wav;b.wav|dossier »)
    {
        const auto spec = juce::SystemStats::getEnvironmentVariable ("VARIANT_META_DEMO", {});
        juce::StringArray ins; ins.addTokens (spec.upToFirstOccurrenceOf ("|", false, false), ";", ""); ins.removeEmptyStrings();
        const juce::File outDir (spec.fromFirstOccurrenceOf ("|", false, false)); outDir.createDirectory();
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        static const char* lv[] = { "Legere", "Moyenne", "Totale" };
        int seed = 11;
        for (auto& p : ins)
        {
            const juce::File f (p);
            std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
            if (rd == nullptr) continue;
            juce::AudioBuffer<float> in ((int) juce::jlimit (1u, 2u, rd->numChannels), (int) juce::jmin<juce::int64> (rd->lengthInSamples, (juce::int64) (rd->sampleRate * 10)));
            rd->read (&in, 0, in.getNumSamples(), 0, true, true);
            for (int k = 0; k < 6; ++k)
            {
                juce::Random r (seed++); juce::StringArray recipe;
                auto out = destroy::process (in, rd->sampleRate, k % 3, r, recipe);
                auto file = outDir.getChildFile (f.getFileNameWithoutExtension() + " - " + lv[k % 3] + " " + juce::String (k / 3 + 1) + " (" + recipe.joinIntoString (" + ") + ").wav");
                file.deleteFile();
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::OutputStream> os (new juce::FileOutputStream (file));
                if (auto* w = wav.createWriterFor (os.get(), rd->sampleRate, 2, 24, {}, 0)) { os.release(); std::unique_ptr<juce::AudioFormatWriter> ww (w); ww->writeFromAudioSampleBuffer (out, 0, out.getNumSamples()); }
            }
        }
        return 0;
    }
    if (juce::SystemStats::getEnvironmentVariable ("VARIANT_LIBTEST", {}).isNotEmpty())   // test : où Variant trouve sa bibliothèque (avec VARIANT_TEST_PROFILE)
    {
        std::cout << "BIBLIOTHEQUE=" << library::defaultFolder().getFullPathName() << std::endl;
        library::releaseSettings();
        return 0;
    }
    if (juce::SystemStats::getEnvironmentVariable ("VARIANT_FX_AUDIT", {}).isNotEmpty())   // outil de dev : chaque preset sur un accord tenu
    {
        const double sr = 44100.0; const int bs = 512, secs = 4, total = (int) sr * secs;
        for (int t = 1; t < fxs::NumTypes; ++t)
        {
            const auto& d = fxs::def (t);
            for (size_t pi = 0; pi < d.presets.size(); ++pi)
            {
                const auto& pr = d.presets[pi];
                auto e = fxs::create (t); e->prepare (sr, bs); e->reset();
                float p[fxs::numP]; for (int k = 0; k < fxs::numP; ++k) p[k] = pr.p[(size_t) k];
                std::vector<float> outL ((size_t) total), inS ((size_t) total);
                bool finite = true; double ph[3] = { 0, 0, 0 }; const double fr[3] = { 220.0, 261.63, 329.63 };
                for (int off = 0; off < total; off += bs)
                {
                    float L[bs], R[bs], dL[bs], dR[bs];
                    for (int i = 0; i < bs; ++i)
                    {
                        float s = 0.0f;
                        for (int k = 0; k < 3; ++k) { ph[k] += fr[k] / sr; ph[k] -= std::floor (ph[k]); s += (float) (2.0 * ph[k] - 1.0) * 0.12f; }
                        L[i] = R[i] = dL[i] = dR[i] = s; if (off + i < total) inS[(size_t) (off + i)] = s;
                    }
                    { static const bool stopped = juce::SystemStats::getEnvironmentVariable ("VARIANT_FX_AUDIT", {}) == "stop";
                      fxs::host() = { 120.0, stopped ? 0.0 : off / sr * 2.0, ! stopped }; }
                    e->mix = pr.mix; e->slotIndex = 0;
                    e->process (L, R, bs, p, 120.0);
                    const bool wetOnly = d.wetOnly;
                    if (! d.selfMix)
                    {
                        const float dg = wetOnly ? juce::jmin (1.0f, 2.0f * (1.0f - pr.mix)) : 1.0f - pr.mix;
                        const float wg = wetOnly ? juce::jmin (1.0f, 2.0f * pr.mix) : pr.mix;
                        for (int i = 0; i < bs; ++i) { L[i] = dL[i] * dg + L[i] * wg; R[i] = dR[i] * dg + R[i] * wg; }
                    }
                    for (int i = 0; i < bs && off + i < total; ++i) { if (! std::isfinite (L[i])) finite = false; outL[(size_t) (off + i)] = 0.5f * (L[i] + R[i]); }
                }
                // à partir de 1 s (les effets se sont installés)
                const int w = 441; int nw = 0, silent = 0; double inE = 0, outE = 0, peak = 0;
                for (int s0 = (int) sr; s0 + w <= total; s0 += w)
                {
                    double ie = 0, oe = 0;
                    for (int i = 0; i < w; ++i) { ie += inS[(size_t) (s0 + i)] * inS[(size_t) (s0 + i)]; oe += outL[(size_t) (s0 + i)] * outL[(size_t) (s0 + i)]; peak = juce::jmax (peak, (double) std::abs (outL[(size_t) (s0 + i)])); }
                    inE += ie; outE += oe; ++nw;
                    if (std::sqrt (oe / w) < 0.03 * std::sqrt (ie / w)) ++silent;
                }
                const double ratioDb = 10.0 * std::log10 ((outE + 1e-12) / (inE + 1e-12));
                const double rms = std::sqrt (outE / (nw * w)), crest = 20.0 * std::log10 ((peak + 1e-9) / (rms + 1e-9));
                const double sf = (double) silent / juce::jmax (1, nw);
                const bool bad = ! finite || ratioDb < -18.0 || sf > 0.45 || crest > 22.0;
                std::cout << (bad ? "!! " : "   ") << t << " " << d.name << " / " << pr.name << " : niveau " << juce::String (ratioDb, 1)
                          << " dB, silence " << juce::String (sf * 100.0, 0) << " %, crete " << juce::String (crest, 1) << " dB" << (finite ? "" : " NaN") << "\n";
            }
        }
        fxs::host() = {};
        return 0;
    }
    if (juce::SystemStats::getEnvironmentVariable ("VARIANT_DUMP_FX", {}).isNotEmpty())   // outil de dev : réglages et presets de chaque effet
    {
        for (int t = 1; t < fxs::NumTypes; ++t)
        {
            const auto& d = fxs::def (t);
            std::cout << t << " " << d.name << " (" << d.presets.size() << " presets) :";
            for (int k = 0; k < fxs::numP; ++k) if (d.params[(size_t) k].name != nullptr) { std::cout << " [" << k << "] " << d.params[(size_t) k].name; if (d.params[(size_t) k].text) std::cout << "=" << d.params[(size_t) k].text (0.0f) << ".." << d.params[(size_t) k].text (1.0f); }
            std::cout << "\n   presets:"; for (auto& p : d.presets) std::cout << " " << p.name << ";";
            std::cout << "\n";
        }
        return 0;
    }
    // --- 1. variations
    {
        const double sr = 44100.0;
        juce::AudioBuffer<float> src (1, (int) sr);
        for (int i = 0; i < src.getNumSamples(); ++i)
            src.setSample (0, i, std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * i / sr) * std::exp (-3.0 * i / sr));

        for (int style = 0; style < 3; ++style)
        {
            variation::Settings s; s.style = style; s.amount = 1.0f; s.count = 16; s.seed = 42;
            auto v = variation::generate (src, sr, s);
            bool finite = true, differs = false;
            for (size_t k = 0; k < v.size(); ++k)
                for (int ch = 0; ch < v[k].getNumChannels(); ++ch)
                    for (int i = 0; i < v[k].getNumSamples(); ++i)
                        if (! std::isfinite (v[k].getSample (ch, i))) finite = false;
            for (size_t k = 1; k < v.size(); ++k)
                if (std::abs (v[k].getSample (0, 2000) - src.getSample (0, 2000)) > 1.0e-4f) differs = true;
            CHECK (v.size() == 16, "variations style " << style << " : 16 variantes");
            CHECK (finite, "variations style " << style << " : pas de NaN");
            CHECK (differs, "variations style " << style << " : variantes differentes de l'original");
        }
    }

    // --- 2a. banques de progressions écrites : chaque accord est compris, et les générations successives changent vraiment
    {
        int bad = 0;
        juce::String badList;
        for (auto& m : moods::list())
            for (auto* t : m.templates)
            {
                juce::StringArray toks; toks.addTokens (juce::String::fromUTF8 (t), " +", ""); toks.removeEmptyStrings();
                for (auto& tok : toks)
                {
                    int r = 0; std::vector<int> iv; bool mi = false;
                    if (! gen::parseChordSymbol (tok, r, iv, mi)) { ++bad; badList << m.name << ":" << tok << " "; }
                }
            }
        CHECK (bad == 0, "progressions ecrites : tous les accords compris " << badList);
        CHECK (moods::indexOf ("Swagg") >= 0 && moods::indexOf ("Baile") >= 0 && moods::indexOf ("Gospel") < 0 && moods::indexOf ("Sombre") < 0
               && moods::indexOf ("R&B / Neo-soul") < 0, "ambiances : Swagg et Baile ajoutes, Gospel / Sombre / Neo-soul retires");

        float worst = 1.0f, total = 0.0f; int n = 0;
        for (int mood = 0; mood < (int) moods::list().size(); ++mood)
        {
            juce::StringArray avoid;
            gen::Settings s; s.mood = mood; s.key = 5; s.scaleIdx = moods::list()[(size_t) mood].scales.front(); s.bars = 4; s.avoid = &avoid;
            juce::Random rng (77 + mood);
            gen::Progression prev;
            for (int click = 0; click < 8; ++click)
            {
                // comme le plugin : 12 essais, on garde le plus différent
                gen::Progression best; float bestScore = -1.0f;
                for (int k = 0; k < 12; ++k)
                {
                    s.seed = (juce::uint32) rng.nextInt();
                    auto c = gen::generateProgression (s);
                    const float nov = prev.chords.empty() ? 1.0f : gen::difference (c.chords, prev.chords, 16.0);
                    const float sc = nov * 2.0f + (avoid.contains (c.signature) ? 0.0f : 1.0f);
                    if (sc > bestScore) { bestScore = sc; best = c; }
                }
                if (! prev.chords.empty()) { const float d = gen::difference (best.chords, prev.chords, 16.0); worst = juce::jmin (worst, d); total += d; ++n; }
                avoid.insert (0, best.signature);
                prev = best;
            }
        }
        CHECK (total / (float) juce::jmax (1, n) > 0.8f && worst >= 0.5f,
               "generateur : chaque clic donne une progression differente (moyenne " << total / juce::jmax (1, n) << ", pire " << worst << ")");
    }

    // --- 2. générateur pour chaque ambiance
    for (int mood = 0; mood < (int) moods::list().size(); ++mood)
    {
        gen::Settings s; s.mood = mood; s.key = 2; s.scaleIdx = moods::list()[(size_t) mood].scales.front();
        s.bars = 8; s.chords = s.bass = s.arp = true; s.seed = 1234;
        for (int i = 0; i < 16; ++i) s.arpSettings.steps[(size_t) i] = 1.0f;
        auto r = gen::generate (s);

        int ons = 0, unmatched = 0; bool inRange = true;
        for (auto* seq : { &r.chordSeq, &r.bassSeq, &r.arpSeq })
            for (int i = 0; i < seq->getNumEvents(); ++i)
            {
                auto* e = seq->getEventPointer (i);
                if (e->message.isNoteOn())
                {
                    ++ons;
                    if (e->noteOffObject == nullptr) ++unmatched;
                    if (e->message.getTimeStamp() < 0 || e->message.getTimeStamp() > r.lengthBeats * gen::ppq) inRange = false;
                }
            }
        juce::String names;
        for (auto& c : r.chords) names << c.roman << ":" << c.name << " ";
        std::cout << "       " << moods::names()[mood] << " -> " << names << "\n";
        CHECK (r.chords.size() >= 8 && ons > 0 && unmatched == 0 && inRange,
               "generateur " << moods::names()[mood] << " (" << ons << " notes)");

        auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("variant_test.mid");
        CHECK (gen::writeMidiFile (r.chordSeq, r.lengthBeats, "test", f), "ecriture .mid");
        juce::MidiFile mf; juce::FileInputStream in (f);
        CHECK (mf.readFrom (in) && mf.getNumTracks() == 1, "relecture .mid");
        f.deleteFile();
    }

    // --- 3. accords
    {
        ChordEngine::Settings cs; cs.mode = 1; cs.key = 0; cs.scaleIdx = theory::Minor; cs.size = 4;
        juce::Random rr (1);
        auto n = ChordEngine::chordFor (60, cs, rr, {});
        CHECK ((n == std::vector<int> { 60, 63, 67, 70 }), "accord C mineur 7e sur C5 = Cm7");
        CHECK (theory::chordName (n, true) == "Cm7", "nom Cm7");
        auto e = ChordEngine::chordFor (63, cs, rr, {});
        cs.reg = ChordEngine::Low;
        auto lowC = ChordEngine::chordFor (72, cs, rr, {});
        CHECK (lowC.front() >= 36 && lowC.front() < 48, "registre grave");
        cs.reg = ChordEngine::AsPlayed; cs.spice = 1.0f; cs.mode = 0; cs.type = 2; // accord 7 + couleur max
        bool altered = false;
        for (int i = 0; i < 20; ++i) { auto x = ChordEngine::chordFor (60, cs, rr, {}); if (x.size() > 4) altered = true; }
        CHECK (altered, "couleur jazzy : extensions / alterations ajoutees");
        CHECK (theory::chordName (e, theory::prefersFlats (0, theory::Minor)) == "Ebmaj7", "Eb (bemol) en Do mineur");
    }

    // --- 4. arpège (hôte arrêté, 120 BPM, 1/16)
    {
        const double sr = 48000.0; const int bs = 512;
        Arpeggiator a; a.prepare (sr);
        arp::Settings s; s.mode = arp::Up; s.rate = arp::R1_16; s.octaves = 1; s.gate = 0.5f; s.humanize = 0.0f; s.freedom = 0.0f;
        for (auto& x : s.steps) x = 1.0f;
        HostTime ht; ht.bpm = 120.0;

        int ons = 0, offs = 0; std::vector<int> order;
        const int blocks = (int) (2.0 * sr / bs);
        for (int b = 0; b < blocks + 20; ++b)
        {
            juce::MidiBuffer in, out;
            if (b == 0) { in.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0); in.addEvent (juce::MidiMessage::noteOn (1, 64, 0.8f), 0); in.addEvent (juce::MidiMessage::noteOn (1, 67, 0.8f), 0); }
            if (b == blocks) for (int nn : { 60, 64, 67 }) in.addEvent (juce::MidiMessage::noteOff (1, nn), 0);
            a.process (in, out, bs, (juce::int64) b * bs, s, ht);
            for (const auto m : out)
            {
                if (m.getMessage().isNoteOn()) { ++ons; if (order.size() < 6) order.push_back (m.getMessage().getNoteNumber()); }
                if (m.getMessage().isNoteOff()) ++offs;
            }
        }
        CHECK (ons >= 15 && ons <= 17, "arpege : ~16 notes en 2 s (" << ons << ")");
        CHECK (ons == offs, "arpege : autant de note-off que de note-on");
        CHECK ((order == std::vector<int> { 60, 64, 67, 60, 64, 67 }), "arpege : ordre montant");

        // options en plus : transpo, ratchet, probabilité, basse sur le 1er pas
        auto runArp = [&] (const arp::Settings& st, std::vector<int>* firstNotes)
        {
            Arpeggiator ax; ax.prepare (sr);
            int n = 0, nOff = 0;
            for (int b = 0; b < blocks + 20; ++b)
            {
                juce::MidiBuffer in, out;
                if (b == 0) for (int nn : { 60, 64, 67 }) in.addEvent (juce::MidiMessage::noteOn (1, nn, 0.8f), 0);
                if (b == blocks) for (int nn : { 60, 64, 67 }) in.addEvent (juce::MidiMessage::noteOff (1, nn), 0);
                ax.process (in, out, bs, (juce::int64) b * bs, st, ht);
                for (const auto m : out)
                {
                    if (m.getMessage().isNoteOn()) { ++n; if (firstNotes != nullptr && firstNotes->size() < 4) firstNotes->push_back (m.getMessage().getNoteNumber()); }
                    if (m.getMessage().isNoteOff()) ++nOff;
                }
            }
            return std::make_pair (n, nOff);
        };
        {
            auto t = s; t.transpose = 12; std::vector<int> fn;
            runArp (t, &fn);
            CHECK ((fn == std::vector<int> { 72, 76, 79, 72 }), "arpege : transpo +12");
            auto r = s; r.ratchet = 1.0f; r.ratchetN = 3;
            const auto rr = runArp (r, nullptr);
            CHECK (rr.first >= 45 && rr.first <= 51 && rr.first == rr.second, "arpege : ratchet x3 (" << rr.first << " notes)");
            auto p = s; p.prob = 0.0f;
            CHECK (runArp (p, nullptr).first == 0, "arpege : probabilite 0 = silence");
            auto bsx = s; bsx.bass = true; std::vector<int> fb;
            const auto br = runArp (bsx, &fb);
            CHECK (br.first >= 16 && br.first <= 19 && fb.size() >= 2 && (fb[0] == 48 || fb[1] == 48), "arpege : basse sur le 1er pas (" << br.first << ")");
        }

        // hôte en lecture (ppq)
        Arpeggiator a2; a2.prepare (sr);
        HostTime hp; hp.bpm = 120.0; hp.playing = true; hp.hasPpq = true;
        int ons2 = 0;
        for (int b = 0; b < blocks; ++b)
        {
            hp.ppq = (double) b * bs / sr * 2.0;
            juce::MidiBuffer in, out;
            if (b == 0) in.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
            a2.process (in, out, bs, (juce::int64) b * bs, s, hp);
            for (const auto m : out) if (m.getMessage().isNoteOn()) ++ons2;
        }
        CHECK (ons2 >= 15 && ons2 <= 17, "arpege synchro hote : ~16 notes (" << ons2 << ")");

        // bug v0.2 : stop puis replay depuis le debut -> l'arpege doit rejouer
        {
            int ons3 = 0;
            HostTime stop; stop.bpm = 120.0; stop.playing = false; stop.hasPpq = true; stop.ppq = 0.0;
            for (int b = 0; b < 10; ++b) { juce::MidiBuffer in, out; a2.process (in, out, bs, (juce::int64) (blocks + b) * bs, s, stop); }
            for (int b = 0; b < blocks; ++b)
            {
                hp.ppq = (double) b * bs / sr * 2.0;
                juce::MidiBuffer in, out;
                if (b == 0) in.addEvent (juce::MidiMessage::noteOn (1, 62, 0.8f), 0);
                a2.process (in, out, bs, (juce::int64) (blocks + 10 + b) * bs, s, hp);
                for (const auto m : out) if (m.getMessage().isNoteOn()) ++ons3;
            }
            CHECK (ons3 >= 15 && ons3 <= 17, "arpege apres stop / replay : rejoue sans desactiver (" << ons3 << ")");
        }

        // jeu humain + liberte : toujours equilibre on/off, et ca varie
        {
            Arpeggiator a3; a3.prepare (sr);
            arp::Settings h = s; h.humanize = 1.0f; h.freedom = 1.0f; h.octaves = 2;
            int on3 = 0, off3 = 0; std::set<int> distinct;
            for (int b = 0; b < blocks + 40; ++b)
            {
                juce::MidiBuffer in, out;
                if (b == 0) for (int nn : { 60, 64, 67 }) in.addEvent (juce::MidiMessage::noteOn (1, nn, 0.8f), 0);
                if (b == blocks) for (int nn : { 60, 64, 67 }) in.addEvent (juce::MidiMessage::noteOff (1, nn), 0);
                a3.process (in, out, bs, (juce::int64) b * bs, h, ht);
                for (const auto m : out) { if (m.getMessage().isNoteOn()) { ++on3; distinct.insert (m.getMessage().getNoteNumber()); } if (m.getMessage().isNoteOff()) ++off3; }
            }
            CHECK (on3 == off3 && on3 > 8, "arpege humain : on/off equilibres (" << on3 << "/" << off3 << ")");
            CHECK (distinct.size() >= 6, "arpege libre : notes variees (" << distinct.size() << " hauteurs)");
        }
    }

    // --- 6. effets : chaque type x chaque preset, pas de NaN, niveau raisonnable
    {
        for (int type = 1; type < fxs::NumTypes; ++type)
        {
            const auto& d = fxs::def (type);
            bool finite = true; float peak = 0.0f;
            for (size_t pr = 0; pr < d.presets.size(); ++pr)
            {
                fxs::Chain chain; chain.prepare (48000.0, 512);
                std::array<fxs::SlotParams, fxs::numSlots> sp;
                std::array<std::array<std::atomic<float>, 3 + fxs::numP>, fxs::numSlots> vals;
                for (int s = 0; s < fxs::numSlots; ++s)
                {
                    for (auto& v : vals[(size_t) s]) v = 0.0f;
                    sp[(size_t) s].type = &vals[(size_t) s][0]; sp[(size_t) s].on = &vals[(size_t) s][1]; sp[(size_t) s].mix = &vals[(size_t) s][2];
                    for (int k = 0; k < fxs::numP; ++k) sp[(size_t) s].p[(size_t) k] = &vals[(size_t) s][(size_t) (3 + k)];
                }
                vals[0][0] = (float) type; vals[0][1] = 1.0f; vals[0][2] = d.presets[pr].mix;
                for (int k = 0; k < fxs::numP; ++k) vals[0][(size_t) (3 + k)] = d.presets[pr].p[(size_t) k];
                chain.ensure (sp);   // l effet est créé hors du thread audio
                for (int b = 0; b < 200; ++b)
                {
                    juce::AudioBuffer<float> buf (2, 512);
                    for (int i = 0; i < 512; ++i) { const float x = b < 60 ? std::sin ((b * 512 + i) * 0.05f) * 0.5f : 0.0f; buf.setSample (0, i, x); buf.setSample (1, i, x * 0.8f); }
                    chain.process (buf, sp, 120.0, 0.0, false, 1.0f, 1.0f);
                    for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 512; ++i) { const float v = buf.getSample (ch, i); if (! std::isfinite (v)) finite = false; peak = juce::jmax (peak, std::abs (v)); }
                }
            }
            CHECK (finite && peak < 6.0f, "effet " << d.name << " : " << d.presets.size() << " presets OK (crete " << peak << ")");
        }
    }

    // --- 7. bibliotheque : chaque son est propre et normalise
    {
        int ok = 0;
        for (auto& it : library::items())
        {
            library::Buf b (2, (int) (it.seconds * library::sr)); b.clear();
            it.render (b); library::finish (b);
            bool finite = true;
            for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < b.getNumSamples(); ++i) if (! std::isfinite (b.getSample (ch, i))) finite = false;
            const float pk = b.getMagnitude (0, b.getNumSamples());
            if (finite && pk > 0.5f && pk < 0.95f) ++ok; else std::cout << "       son problematique : " << it.name << " (crete " << pk << ")\n";
        }
        CHECK (ok == (int) library::items().size(), "bibliotheque : " << ok << "/" << library::items().size() << " sons valides");
    }

    // --- 8. couches : une note = une couche dans un accord de 3 notes
    {
        LayerRouter r; r.prepare (48000.0);
        juce::MidiBuffer in, tmp;
        for (int nn : { 67, 60, 64 }) in.addEvent (juce::MidiMessage::noteOn (1, nn, 0.8f), 0);
        std::array<bool, maxLayers> act {}; act[0] = act[1] = act[2] = true;
        r.process (in, tmp, 0, LayerRouter::PerNote, false, act, nullptr);
        std::map<int, int> chOf;
        for (const auto m : in) chOf[m.getMessage().getNoteNumber()] = m.getMessage().getChannel();
        CHECK (chOf[60] == 1 && chOf[64] == 2 && chOf[67] == 3, "couches : C->1, E->2, G->3");
        juce::MidiBuffer off;
        off.addEvent (juce::MidiMessage::noteOff (1, 64), 0);
        r.process (off, tmp, 100, LayerRouter::PerNote, false, act, nullptr);
        int offCh = 0; for (const auto m : off) offCh = m.getMessage().getChannel();
        CHECK (offCh == 2, "couches : le note-off suit la meme couche");
        // avec l'arpège : le rang vient de l'accord tenu
        std::vector<int> ctx { 60, 64, 67 };
        juce::MidiBuffer a; a.addEvent (juce::MidiMessage::noteOn (1, 79, 0.8f), 0);   // G une octave au-dessus
        r.process (a, tmp, 200, LayerRouter::PerNote, false, act, &ctx);
        int ach = 0; for (const auto m : a) ach = m.getMessage().getChannel();
        CHECK (ach == 3, "couches + arpege : G (octave) -> couche 3");
    }

    // --- 9. progression editable : changer UN accord sans toucher aux autres
    {
        gen::Settings s; s.mood = moods::indexOf ("Jazz"); s.key = 0; s.scaleIdx = theory::Major; s.seed = 99;
        auto prog = gen::generateProgression (s);
        auto before = prog.chords;
        juce::Random rr (5);
        prog.chords[1] = gen::randomChord (s, prog.chords[1], prog.reg, rr);
        bool othersSame = true;
        for (size_t i = 0; i < prog.chords.size(); ++i) if (i != 1 && prog.chords[i].close != before[i].close) othersSame = false;
        CHECK (othersSame, "progression : changer l'accord 2 ne touche pas aux autres");
        // rendre un accord diminue
        auto& c = prog.chords[0];
        const int root = *std::min_element (c.close.begin(), c.close.end());
        c.close = { root, root + 3, root + 6 };
        gen::relabel (c, s.key, s.scaleIdx);
        CHECK (c.name.contains ("dim"), "progression : accord rendu diminue (" << c.name << ")");
        auto res = gen::render (prog, s);
        CHECK (res.chordSeq.getNumEvents() > 0, "progression : rendu MIDI apres modification");
        // registre grave
        s.reg = gen::RegLow; auto low = gen::generate (s); s.reg = gen::RegHigh; auto high = gen::generate (s);
        CHECK (low.chords[0].voiced.front() < high.chords[0].voiced.front(), "registre : grave plus bas que aigu");
    }
    // --- 10. analyse : tempo, tonalité, audio -> MIDI
    {
        const double sr = analysis::sr;
        auto tone = [] (double f, double t) { return std::sin (2 * juce::MathConstants<double>::pi * f * t) + 0.4 * std::sin (4 * juce::MathConstants<double>::pi * f * t) + 0.2 * std::sin (6 * juce::MathConstants<double>::pi * f * t); };
        auto hz = [] (int midi) { return 440.0 * std::pow (2.0, (midi - 69) / 12.0); };

        // 12 s à 120 BPM : accords de Do majeur (C, F, G, C) frappés sur chaque temps
        std::vector<float> x ((size_t) (sr * 12.0), 0.0f);
        const int chords[4][3] = { { 60, 64, 67 }, { 65, 69, 72 }, { 67, 71, 74 }, { 60, 64, 67 } };
        for (int beat = 0; beat < 24; ++beat)
        {
            const auto& c = chords[(beat / 2) % 4];
            const size_t s0 = (size_t) (beat * 0.5 * sr);
            for (size_t i = 0; i < (size_t) (0.45 * sr) && s0 + i < x.size(); ++i)
            {
                const double t = i / sr, env = std::exp (-t * 6.0);
                double v = 0; for (int k = 0; k < 3; ++k) v += tone (hz (c[k]), t);
                x[s0 + i] += (float) (0.15 * v * env);
            }
        }
        const auto tempo = analysis::detectTempo (x);
        CHECK (std::abs (tempo.bpm - 120.0) < 2.0, "tempo : 120 BPM detecte (" << tempo.bpm << ")");
        const auto keys = analysis::detectKey (x);
        CHECK (! keys.empty() && keys[0].key == 0 && ! keys[0].minor, "tonalite : Do majeur (" << keys[0].key << (keys[0].minor ? " mineur" : " majeur") << ")");

        // mélodie C D E G
        std::vector<float> m ((size_t) (sr * 2.4), 0.0f);
        const int mel[] = { 60, 62, 64, 67 };
        for (int n = 0; n < 4; ++n)
            for (size_t i = 0; i < (size_t) (0.5 * sr); ++i)
                m[(size_t) (n * 0.55 * sr) + i] = (float) (0.3 * tone (hz (mel[n]), i / sr) * std::min (1.0, i / 200.0));
        auto notes = analysis::melodyToMidi (m, 0.6f);
        juce::String got; for (auto& n : notes) got << n.pitch << " ";
        bool ok = notes.size() == 4; for (size_t i = 0; ok && i < 4; ++i) ok = notes[i].pitch == mel[i];
        CHECK (ok, "audio -> MIDI melodie : C D E G (" << got << ")");

        // accord C E G tenu
        std::vector<float> ch ((size_t) (sr * 1.5), 0.0f);
        for (size_t i = 0; i < ch.size(); ++i) { double v = 0; for (int p : { 60, 64, 67 }) v += tone (hz (p), i / sr); ch[i] = (float) (0.15 * v); }
        auto poly = analysis::polyToMidi (ch, 0.6f);
        std::set<int> ps; for (auto& n : poly) ps.insert (n.pitch);
        juce::String gp; for (int p : ps) gp << p << " ";
        CHECK (ps.count (60) && ps.count (64) && ps.count (67) && ps.size() <= 4, "audio -> MIDI accords : C E G (" << gp << ")");
    }

    // --- 11. notes editables -> MIDI
    {
        std::vector<gen::Note> n { { 0.0, 1.0, 60, 0.8f, 0 }, { 1.0, 1.0, 36, 0.8f, 1 }, { 2.0, 0.5, 72, 0.8f, 2 } };
        auto s = gen::seqFromNotes (n, { true, false, true });
        int ons = 0; for (int i = 0; i < s.getNumEvents(); ++i) if (s.getEventPointer (i)->message.isNoteOn()) ++ons;
        CHECK (ons == 2, "notes editables : filtrage par partie (" << ons << ")");
        CHECK (fr ("Son") == "Son", "traduction : francais par defaut");
        i18n::current().store (i18n::EN);
        CHECK (fr ("Son") == "Sound" && fr ("Piano électrique") == "Electric piano", "traduction : anglais");
        i18n::current().store (i18n::DE);
        CHECK (fr ("Effets") == "Effekte" && fr ("Piano électrique") == "Electric piano", "traduction : allemand + repli anglais");
        i18n::current().store (i18n::FR);
    }

    // --- 12. cut : un nouvel accord coupe le précédent
    {
        CutFilter cf; cf.prepare (48000.0);
        juce::MidiBuffer m, tmp;
        m.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
        m.addEvent (juce::MidiMessage::noteOn (1, 64, 0.8f), 10);
        cf.process (m, tmp, 0);
        int offs1 = 0; for (const auto e : m) if (e.getMessage().isNoteOff()) ++offs1;
        juce::MidiBuffer m2; m2.addEvent (juce::MidiMessage::noteOn (1, 67, 0.8f), 0);
        cf.process (m2, tmp, 48000);
        int offs2 = 0; for (const auto e : m2) if (e.getMessage().isNoteOff()) ++offs2;
        CHECK (offs1 == 0 && offs2 == 2, "cut : les notes d'un meme accord restent, l'accord suivant coupe le precedent (" << offs1 << "/" << offs2 << ")");
    }

    // --- 13. grooves : les styles se distinguent, pas d'accords identiques a la suite
    {
        auto hitsPerBar = [] (int mood)
        {
            gen::Settings s; s.mood = mood; s.bars = 8; s.bass = false; s.seed = 5; s.scaleIdx = moods::list()[(size_t) mood].scales.front();
            auto r = gen::generate (s);
            std::vector<double> starts;
            for (int i = 0; i < r.chordSeq.getNumEvents(); ++i)
                if (r.chordSeq.getEventPointer (i)->message.isNoteOn()) starts.push_back (r.chordSeq.getEventPointer (i)->message.getTimeStamp() / gen::ppq);
            std::sort (starts.begin(), starts.end());
            int hits = 0; double last = -10;
            for (double s0 : starts) if (s0 - last > 0.2) { ++hits; last = s0; }   // les notes d'un même accord (strum) = une frappe
            return (double) hits / 8.0;
        };
        const double house = hitsPerBar (moods::indexOf ("House")), ambient = hitsPerBar (moods::indexOf ("Ambient")), funk = hitsPerBar (moods::indexOf ("Funk")), jazz = hitsPerBar (moods::indexOf ("Jazz"));
        CHECK (house > ambient * 1.8 && funk > jazz, "grooves : House " << house << " / Ambient " << ambient << " / Funk " << funk << " / Jazz " << jazz << " frappes par mesure");

        int repeats = 0, total = 0;
        for (int mood = 0; mood < (int) moods::list().size(); ++mood)
            for (juce::uint32 seed = 1; seed <= 20; ++seed)
            {
                gen::Settings s; s.mood = mood; s.bars = 8; s.seed = seed; s.scaleIdx = moods::list()[(size_t) mood].scales.front();
                auto p = gen::generateProgression (s);
                std::vector<int> masks;
                for (auto& c : p.chords) if (std::fmod (c.start, 4.0) < 0.01) { int mk = 0; for (int x : c.close) mk |= 1 << theory::mod12 (x); masks.push_back (mk); }
                for (size_t i = 1; i < masks.size(); ++i) { ++total; if (masks[i] == masks[i - 1]) ++repeats; }
            }
        CHECK (repeats * 20 < total, "progressions : accords identiques a la suite rares (" << repeats << "/" << total << ")");
    }

    // --- 5. pré-écoute
    {
        PreviewPlayer p;
        auto seq = std::make_unique<PreviewPlayer::Sequence>();
        seq->lengthBeats = 4.0;
        seq->events.push_back ({ 0.0, juce::MidiMessage::noteOn (1, 60, 0.8f) });
        seq->events.push_back ({ 1.0, juce::MidiMessage::noteOff (1, 60) });
        p.setSequence (std::move (seq));
        int ons = 0;
        for (int b = 0; b < 400; ++b) { juce::MidiBuffer out; p.render (out, 480, 120.0, 48000.0); for (const auto m : out) if (m.getMessage().isNoteOn()) ++ons; }
        CHECK (ons == 2, "pre-ecoute : boucle de 2 s jouee 2 fois en 4 s (" << ons << ")");
    }

    // --- v2.8 : découpe d'une boucle de batterie (kick, snare, charleys) : chaque coup fort est coupé juste avant son attaque
    {
        const double sr = 44100.0, bpm = 100.0, beat = 60.0 / bpm;
        juce::AudioBuffer<float> b (2, (int) (sr * beat * 8));
        b.clear();
        juce::Random rr (11);
        std::vector<int> strong;
        for (int k = 0; k < 16; ++k)   // croches
        {
            const int s0 = (int) (k * beat * 0.5 * sr) + (int) (0.012 * sr);   // un peu de silence au tout début
            const bool kick = k % 4 == 0, snare = k % 4 == 2;
            if (kick || snare) strong.push_back (s0);
            for (int i = 0; i < (int) (0.25 * sr) && s0 + i < b.getNumSamples(); ++i)
            {
                const float t = (float) i / (float) sr;
                float x;
                if (kick) x = std::exp (-t * 18.0f) * std::sin (2.0f * 3.14159f * (55.0f + 90.0f * std::exp (-t * 40.0f)) * t) * 0.9f;
                else if (snare) x = std::exp (-t * 25.0f) * ((rr.nextFloat() * 2.0f - 1.0f) * 0.6f + 0.3f * std::sin (2.0f * 3.14159f * 190.0f * t));
                else x = std::exp (-t * 60.0f) * (rr.nextFloat() * 2.0f - 1.0f) * 0.15f;   // charley doux
                b.addSample (0, s0 + i, x); b.addSample (1, s0 + i, x);
            }
        }
        const auto st = slicer::detectTransients (b, sr, 0.5f, 16, bpm);
        int good = 0, late = 0;
        for (int a : strong)
        {
            bool found = false;
            for (int s : st) if (s <= a + (int) (0.001 * sr) && s >= a - (int) (0.012 * sr)) found = true;
            for (int s : st) if (s > a + (int) (0.001 * sr) && s < a + (int) (0.04 * sr)) ++late;   // coupe au milieu du coup
            if (found) ++good;
        }
        CHECK (good == (int) strong.size() && late == 0, "slicer : boucle de batterie, " << good << "/" << strong.size() << " coups forts coupes juste avant l'attaque (" << late << " coupes au milieu d'un coup, " << st.size() << " tranches)");
    }

    // --- v0.7 : slicer (transitoires + grille + lecture)
    {
        const double sr = 44100.0;
        juce::AudioBuffer<float> b (2, (int) (sr * 3.2));
        b.clear();
        juce::Random rr (7);
        const double hits[] = { 0.0, 0.5, 1.0, 1.37, 2.0, 2.5 };
        for (double h : hits)
        {
            const int s0 = (int) (h * sr);
            for (int i = 0; i < (int) (0.35 * sr) && s0 + i < b.getNumSamples(); ++i)
            {
                const float env = std::exp (-(float) i / (float) (0.06 * sr));
                const float x = env * (0.6f * std::sin (2.0f * 3.14159f * 180.0f * (float) i / (float) sr) + 0.3f * (rr.nextFloat() * 2.0f - 1.0f));
                b.addSample (0, s0 + i, x); b.addSample (1, s0 + i, x);
            }
        }
        auto st = slicer::detectTransients (b, sr, 0.55f, 16);
        int matched = 0;
        for (double h : hits)
            for (int s : st) if (std::abs (s - h * sr) < 0.015 * sr) { ++matched; break; }
        CHECK ((int) st.size() == 6 && matched == 6, "slicer : 6 attaques trouvees (" << st.size() << " tranches, " << matched << " justes)");
        auto st4 = slicer::detectTransients (b, sr, 0.55f, 4);
        CHECK ((int) st4.size() == 4 && st4[0] == 0, "slicer : limite a 4 tranches");
        auto gr = slicer::gridSlices (b, 8);
        CHECK ((int) gr.size() == 8 && gr[0] == 0 && std::is_sorted (gr.begin(), gr.end()), "slicer : grille 8 tranches");
        CHECK (slicer::sliceForNote (60, 6) == 0 && slicer::sliceForNote (65, 6) == 5 && slicer::sliceForNote (66, 6) == 0
               && slicer::sliceForNote (59, 6) == 5, "slicer : notes -> tranches");

        juce::Synthesiser syn;
        std::atomic<int> last { -1 }; std::atomic<float> pos { -1.0f };
        slicer::SliceParams sp;
        syn.addVoice (new slicer::SliceVoice (sp, last, pos));
        slicer::SliceSound::Ptr snd = new slicer::SliceSound();
        snd->data = std::make_shared<const juce::AudioBuffer<float>> (b);
        snd->rate = sr; snd->starts = st;
        syn.addSound (snd.get());
        syn.setCurrentPlaybackSampleRate (sr);
        juce::AudioBuffer<float> out (2, 512);
        juce::MidiBuffer mb; mb.addEvent (juce::MidiMessage::noteOn (1, 62, 0.9f), 0);
        float peak = 0.0f; bool finite = true;
        for (int k = 0; k < 40; ++k)
        {
            out.clear();
            syn.renderNextBlock (out, mb, 0, 512);
            mb.clear();
            for (int i = 0; i < 512; ++i) { const float v = out.getSample (0, i); peak = std::max (peak, std::abs (v)); if (! std::isfinite (v)) finite = false; }
        }
        CHECK (last.load() == 2 && peak > 0.05f && finite, "slicer : D5 joue la tranche 3 (pic " << peak << ")");

        // --- bibliothèque renommée : un ancien chemin retrouve le son sous son nouveau nom
        {
            auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("VariantRenameTest");
            root.deleteRecursively();
            auto dir = root.getChildFile ("Variant Basses");
            dir.createDirectory();
            dir.getChildFile ("17 Bass ( Hive ).wav").replaceWithText ("x");
            root.getChildFile (".renommages.txt").replaceWithText ("Variant Basses/Variant Bass hive 17.wav|Variant Basses/17 Bass ( Hive ).wav\n");
            const auto got = library::resolveRenamed (dir.getChildFile ("Variant Bass hive 17.wav"), root);
            const auto none = library::resolveRenamed (dir.getChildFile ("inconnu.wav"), root);
            CHECK (got.getFileName() == "17 Bass ( Hive ).wav" && none.getFileName() == "inconnu.wav", "bibliotheque renommee : ancien nom -> nouveau nom");
            CHECK (library::originalName (dir.getChildFile ("17 Bass ( Hive ).wav"), root) == "Variant Bass hive 17", "bibliotheque renommee : nom d'origine retrouve");

            // bibliothèque déplacée : l'ancien chemin (Documents\Variant Bibliothèque\…) retrouve le son au nouvel endroit
            {
                auto moved = root.getChildFile ("Variant Folder").getChildFile (juce::String::fromUTF8 ("Variant Bibliothèque @moi"));
                moved.getChildFile ("Variant Keys").createDirectory();
                moved.getChildFile ("Variant Keys").getChildFile ("2 Key.wav").replaceWithText ("x");
                moved.getChildFile (".variant-cree").create();
                root.getChildFile ("Variant Folder").getChildFile ("Drumkit").getChildFile ("Kicks").createDirectory();
                const juce::File oldPath (juce::String::fromUTF8 ("C:\\Nulle part\\Documents\\Variant Bibliothèque\\Variant Keys\\2 Key.wav"));
                const auto a = library::resolveRenamed (oldPath, moved);
                CHECK (a == moved.getChildFile ("Variant Keys").getChildFile ("2 Key.wav"), "bibliotheque deplacee : ancien chemin retrouve (" << a.getFullPathName() << ")");
                CHECK (library::looksLikeLibrary (moved) && ! library::looksLikeLibrary (root.getChildFile ("Variant Folder").getChildFile ("Drumkit")),
                       "bibliotheque deplacee : reconnue (et un drumkit n'est pas pris pour la bibliotheque)");
            }

            // note d'après le nom
            struct NN { const char* n; int pc; };
            const NN cases[] = {
                { "17 Bass ( Hive )", 0 }, { "3 Key ( SEM V3 )", 0 }, { "Variant Lead 12", 0 }, { "1 Bass ( @JefeJunior )", -1 },
                { "DS_SCB_bass_one_shot_sub_bass_one_shot_fluttery_noise_D#", 3 }, { "##18 BASS - BASIC BITCH C", 0 },
                { "MO_XP4_vocal_pad_mind_crescendo_phr_Eb", 3 }, { "KMRBI_BONJ_saxophone_long_Em7911", 4 }, { "Kautik LEad C3", 0 },
                { "SEQ - Bell Magic (Dmin 155) (okayypablo)", 2 }, { "C#m_-_Wah_Lick_02", 1 }, { "Rio Synth [C]", 0 },
                { "TSP_S2GE_132_fx_synth_scratch_Gmin", 7 }, { "acid - bass", -1 }, { "BASS - BIG LURK", -1 }, { "Ambient", -1 },
                { "#PORNSTAR [BITCRUSHFALL] [ONESHOT]", -1 }, { "1 Sample ( @DJ ENZN )", -1 }, { "Flywavs - One Shot  - 01 - Bass - C", 0 } };
            int bad = 0; juce::String badList;
            for (auto& c : cases) { const int got2 = library::noteFromName (c.n); if (got2 != c.pc) { ++bad; badList << c.n << "=" << got2 << " "; } }
            CHECK (bad == 0, "note d'apres le nom (" << badList << ")");
            root.deleteRecursively();
        }

        // --- son des chops : réglages par défaut identiques à avant, puis chaque option fait ce qu'elle dit
        {
            slicer::ChopSettings chops;
            std::atomic<float> atk { 1.5f }, rel { 15.0f }, vel { 0.75f }, tone { 0.0f }, lvl { 0.0f };
            auto render = [&] (const slicer::SliceParams& params, int slice, int blocks, float velocity)
            {
                juce::Synthesiser s2;
                std::atomic<int> l2 { -1 }; std::atomic<float> p2 { -1.0f };
                s2.addVoice (new slicer::SliceVoice (params, l2, p2));
                s2.addSound (snd.get());
                s2.setCurrentPlaybackSampleRate (sr);
                std::vector<float> mono;
                juce::AudioBuffer<float> o (2, 512);
                juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (1, slicer::base() + slice, velocity), 0);
                for (int k = 0; k < blocks; ++k)
                {
                    o.clear();
                    s2.renderNextBlock (o, m, 0, 512);
                    m.clear();
                    for (int i = 0; i < 512; ++i) mono.push_back (o.getSample (0, i));
                }
                return mono;
            };
            auto energy = [] (const std::vector<float>& v) { double e = 0; for (float x : v) e += (double) x * x; return e; };
            auto firstSound = [] (const std::vector<float>& v) { for (size_t i = 0; i < v.size(); ++i) if (std::abs (v[i]) > 0.01f) return (int) i; return -1; };

            slicer::SliceParams old;   // ancien comportement : aucun nouveau réglage branché
            slicer::SliceParams neu; neu.attack = &atk; neu.release = &rel; neu.velSens = &vel; neu.tone = &tone; neu.levelOn = &lvl; neu.chops = &chops;
            const auto a = render (old, 2, 30, 0.9f), b2 = render (neu, 2, 30, 0.9f);
            double diff = 0; for (size_t i = 0; i < a.size(); ++i) diff = std::max (diff, (double) std::abs (a[i] - b2[i]));
            CHECK (diff < 1.0e-6, "chops : reglages par defaut = son d'origine (ecart " << diff << ")");

            chops.vol[2] = -6.0f;
            const double eQuiet = energy (render (neu, 2, 30, 0.9f));
            chops.vol[2] = 0.0f;
            const double eBase = energy (b2);
            CHECK (eQuiet < eBase * 0.3 && eQuiet > eBase * 0.2, "chops : volume -6 dB (" << eQuiet / eBase << ")");

            chops.rev[2] = 1;
            const auto rv = render (neu, 2, 30, 0.9f);
            chops.rev[2] = 0;
            // la tranche 3 commence par une attaque forte : à l'envers, le début est calme et l'attaque arrive à la fin
            const int fwdHit = firstSound (b2), revHit = firstSound (rv);
            auto lastSound0 = [] (const std::vector<float>& v) { for (int i = (int) v.size() - 1; i >= 0; --i) if (std::abs (v[(size_t) i]) > 0.001f) return i; return -1; };
            CHECK (energy (rv) > eBase * 0.2 && fwdHit >= 0 && revHit > fwdHit + 1000, "chops : a l envers, le chop joue (" << fwdHit << " / " << revHit << " energie " << energy (rv) / eBase << " fin " << lastSound0 (rv) << ")");

            chops.pitch[2] = 12.0f;
            const auto up = render (neu, 2, 30, 0.9f);
            chops.pitch[2] = 0.0f;
            auto lastSound = [] (const std::vector<float>& v) { for (int i = (int) v.size() - 1; i >= 0; --i) if (std::abs (v[(size_t) i]) > 0.001f) return i; return -1; };
            const int lenBase = lastSound (b2), lenUp = lastSound (up);
            CHECK (lenUp > 0 && lenBase > 0 && std::abs ((double) lenUp / lenBase - 0.5) < 0.1, "chops : +12 st = deux fois plus court (" << lenUp << " / " << lenBase << ")");

            tone = -1.0f;
            const double eDark = energy (render (neu, 2, 30, 0.9f));
            tone = 0.0f;
            CHECK (eDark < eBase && eDark > 0.0, "chops : couleur sombre = moins d'aigus (" << eDark / eBase << ")");

            vel = 0.0f;
            const double eSoft = energy (render (neu, 2, 30, 0.2f)), eHard = energy (render (neu, 2, 30, 1.0f));
            vel = 0.75f;
            CHECK (std::abs (eSoft - eHard) < eHard * 1.0e-3, "chops : velocite 0 % = meme volume a chaque coup");

            snd->computeLevels();
            bool lvOk = snd->level.size() == snd->starts.size();
            for (float g : snd->level) if (! (g >= 0.25f && g <= 4.0f)) lvOk = false;
            CHECK (lvOk, "chops : volumes egalises calcules (" << snd->level.size() << " tranches)");
        }
    }

    // --- v0.7 : hasard d'arpège musical
    {
        juce::Random rng (99);
        bool ok = true;
        for (int t = 0; t < 300; ++t)
        {
            const auto a = arp::randomArp (rng);
            int active = 0;
            for (int i = 0; i < a.numSteps; ++i) if (a.steps[(size_t) i] > 0.05f) ++active;
            if (a.steps[0] < 0.99f || active < 2 || a.numSteps < 1 || a.numSteps > 16 || a.gate < 0.3f || a.gate > 1.0f) ok = false;
        }
        CHECK (ok, "arpege au hasard : 1er pas toujours joue, motif jouable");
    }

    // --- v0.7 : arpège du générateur calé sur la grille et les accords
    {
        int total = 0, onGrid = 0, outside = 0;
        for (int mood = 0; mood < (int) moods::list().size(); ++mood)
        {
            gen::Settings s; s.mood = mood; s.key = 0; s.scaleIdx = moods::list()[(size_t) mood].scales.front();
            s.bars = 4; s.chords = s.arp = true; s.bass = false; s.seed = 77 + (juce::uint32) mood;
            { const auto& ap = moods::list()[(size_t) mood].arp; s.arpSettings.mode = ap.mode; s.arpSettings.rate = ap.rate; s.arpSettings.octaves = ap.octaves;
              s.arpSettings.gate = ap.gate; s.arpSettings.swing = ap.swing; s.arpSettings.numSteps = ap.numSteps;
              for (int q = 0; q < 16; ++q) s.arpSettings.steps[(size_t) q] = ap.steps[(size_t) q]; }
            s.arpSettings.humanize = 0.8f; s.arpSettings.freedom = 0.0f; s.arpSettings.swing = 0.0f;
            auto r = gen::generate (s);
            const double step = arp::rateBeats (s.arpSettings.rate);
            for (int i = 0; i < r.arpSeq.getNumEvents(); ++i)
            {
                auto* e = r.arpSeq.getEventPointer (i);
                if (! e->message.isNoteOn()) continue;
                const double t = e->message.getTimeStamp() / gen::ppq;
                ++total;
                if (std::abs (t / step - std::round (t / step)) < 0.02) ++onGrid;
                bool inChord = false;
                for (int k = 0; k < r.chordSeq.getNumEvents(); ++k)
                {
                    auto* c = r.chordSeq.getEventPointer (k);
                    if (! c->message.isNoteOn() || c->noteOffObject == nullptr) continue;
                    const double cs = c->message.getTimeStamp() / gen::ppq - 1.05, ce = c->noteOffObject->message.getTimeStamp() / gen::ppq + 1.05;
                    if (t >= cs && t < ce) { inChord = true; break; }
                }
                if (! inChord) ++outside;
            }
        }
        CHECK (total > 50 && onGrid == total, "generateur : arpege sur la grille (" << onGrid << "/" << total << ")");
        CHECK (outside == 0, "generateur : arpege toujours pendant un accord (" << outside << " hors accord)");
    }

    // --- v1.0 : variété des accords, découpe au tempo, réponses de Kick
    {
        juce::Random rng (5);
        bool ok = true; int changed = 0;
        for (int i = 0; i < 400; ++i)
        {
            const std::vector<int> c { 48, 52, 55, 59 };
            const auto v = theory::varyChord (c, rng);
            if (v.size() < 3 || v.front() != 48 || ! std::is_sorted (v.begin(), v.end())) ok = false;
            if (v != c) ++changed;
        }
        CHECK (ok && changed > 200, "variete : accords valides, fondamentale gardee (" << changed << "/400 modifies)");

        int distinct = 0;
        std::set<juce::String> names;
        for (int seed = 0; seed < 30; ++seed)
        {
            gen::Settings s; s.mood = moods::indexOf ("Pop"); s.key = 0; s.scaleIdx = theory::Major; s.seed = (juce::uint32) seed; s.variety = 1.0f;
            const auto p = gen::generateProgression (s);
            for (auto& c : p.chords) names.insert (c.name);
        }
        distinct = (int) names.size();
        gen::Settings s0; s0.mood = moods::indexOf ("Pop"); s0.key = 0; s0.scaleIdx = theory::Major; s0.variety = 0.0f;
        std::set<juce::String> names0;
        for (int seed = 0; seed < 30; ++seed) { s0.seed = (juce::uint32) seed; for (auto& c : gen::generateProgression (s0).chords) names0.insert (c.name); }
        CHECK (distinct > (int) names0.size(), "variete : plus de types d'accords differents (" << distinct << " vs " << names0.size() << ")");

        // Stretch de qualité : +7 et -5 demi-tons gardent la même durée, la hauteur change, le tempo de FL est suivi
        {
            const double sr = 44100.0;
            juce::AudioBuffer<float> src (2, (int) (sr * 1.0));
            for (int i = 0; i < src.getNumSamples(); ++i) { const float v = 0.4f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * i / sr); src.setSample (0, i, v); src.setSample (1, i, v); }
            auto freqOf = [sr] (const juce::AudioBuffer<float>& b)
            {
                int cross = 0; float last = 0.0f; const int a = b.getNumSamples() / 4, z = b.getNumSamples() * 3 / 4;
                for (int i = a; i < z; ++i) { const float v = b.getSample (0, i); if ((last < 0.0f) != (v < 0.0f)) ++cross; last = v; }
                return cross / 2.0 / ((z - a) / sr);
            };
            const auto up = stretch::render (src, sr, 7, 1.0), down = stretch::render (src, sr, -5, 1.0), slow = stretch::render (src, sr, 0, 140.0 / 70.0);
            const double fu = freqOf (up), fd = freqOf (down);
            const bool lens = std::abs (up.getNumSamples() - src.getNumSamples()) < 300 && std::abs (down.getNumSamples() - src.getNumSamples()) < 300
                           && std::abs (slow.getNumSamples() - 2 * src.getNumSamples()) < 300;
            CHECK (lens && std::abs (fu - 220.0 * std::pow (2.0, 7.0 / 12.0)) < 4.0 && std::abs (fd - 220.0 * std::pow (2.0, -5.0 / 12.0)) < 4.0 && std::abs (freqOf (slow) - 220.0) < 3.0,
                   "stretch : meme duree pour toutes les notes (+7 : " << fu << " Hz, -5 : " << fd << " Hz), 140 -> 70 BPM = 2x plus long");
        }
        // one-shot d'arpège : les 4 notes (do4, do5, sol4, do5) sont trouvées avec leur hauteur (accords façon Hive)
        {
            const double sr = 44100.0;
            const int step = (int) (sr * 60.0 / 140.0 / 4.0);   // 1/16 à 140 BPM
            const float notes[] = { 60.0f, 72.0f, 67.0f, 72.0f };
            juce::AudioBuffer<float> arpS (2, step * 4);
            arpS.clear();
            for (int k = 0; k < 4; ++k)
            {
                const double f = 440.0 * std::pow (2.0, (notes[k] - 69.0) / 12.0);
                for (int i = 0; i < step; ++i)
                {
                    const float v = 0.4f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * f * i / sr) * std::exp (-3.0f * (float) i / (float) step) * juce::jmin (1.0f, (float) i / 40.0f);
                    arpS.setSample (0, k * step + i, v); arpS.setSample (1, k * step + i, v);
                }
            }
            const auto an = stretch::analyseArp (arpS, sr);
            bool okP = an.onsets.size() == 4;
            for (size_t k = 0; okP && k < 4; ++k) okP = std::abs (an.onsets[k] - (int) k * step) < 900 && std::abs (an.midi[k] - notes[k]) < 0.6f;
            { juce::String dd; for (size_t k = 0; k < an.onsets.size(); ++k) dd << an.onsets[k] << "/" << an.midi[k] << " "; CHECK (okP, "arpege : " << an.onsets.size() << " pas trouves avec leur hauteur (" << dd << ", pas " << step << ")"); }
            // stretch propre aux extrêmes : +24 et -24 demi-tons restent finis, à la bonne durée
            const auto hi = stretch::render (arpS, sr, 24, 1.0), lo = stretch::render (arpS, sr, -24, 1.0);
            bool fin = true; float pk = 0.0f;
            for (auto* b : { &hi, &lo }) for (int c = 0; c < b->getNumChannels(); ++c) for (int i = 0; i < b->getNumSamples(); ++i) { const float v = b->getSample (c, i); if (! std::isfinite (v)) fin = false; pk = juce::jmax (pk, std::abs (v)); }
            CHECK (fin && pk < 1.5f && std::abs (hi.getNumSamples() - arpS.getNumSamples()) < 300 && std::abs (lo.getNumSamples() - arpS.getNumSamples()) < 300,
                   "stretch +-24 demi-tons : propre (crete " << pk << ")");
        }
        // MODE ARPÉGIATEUR : calé tout de suite sur le tempo de FL (sans attendre le calcul en arrière-plan),
        // et en accord une seule voix porte le motif (pas plusieurs arpèges empilés)
        {
            const double sr = 44100.0;
            const int step = (int) (sr * 60.0 / 140.0 / 4.0);   // 1/16 à 140 BPM
            const float notes[] = { 60.0f, 64.0f, 67.0f, 72.0f };
            juce::AudioBuffer<float> arpS (2, step * 4 + 2000);
            arpS.clear();
            for (int k = 0; k < 4; ++k)
            {
                const double f = 440.0 * std::pow (2.0, (notes[k] - 69.0) / 12.0);
                for (int i = 0; i < step * 7 / 10; ++i)   // notes détachées (70 % du pas)
                {
                    const float v = 0.4f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * f * i / sr) * std::exp (-2.0f * (float) i / (float) step) * juce::jmin (1.0f, (float) i / 40.0f);
                    arpS.setSample (0, k * step + i, v); arpS.setSample (1, k * step + i, v);
                }
            }
            std::atomic<float> one { 1.0f }, zero { 0.0f }, root { 60.0f }, dec { 4000.0f }, rel { 30.0f }, att { 0.0f };
            std::atomic<double> hb { 70.0 }, ppq { 0.0 };
            std::atomic<bool> playingH { false };
            VoiceParams vp;
            vp.tune = &zero; vp.fine = &zero; vp.attack = &att; vp.decay = &dec; vp.sustain = &one; vp.release = &rel;
            vp.velocity = &zero; vp.gain = &zero; vp.oneshot = &zero; vp.varOn = &zero; vp.velTone = &zero; vp.human = &zero;
            vp.glideMode = &zero; vp.glideTime = &zero; vp.glideCurve = &zero; vp.slideIn = &zero; vp.loopChord = &zero;
            vp.hostPpq = &ppq; vp.hostBpm = &hb; vp.hostPlaying = &playingH; vp.stretchCache = nullptr;
            for (int l = 0; l < maxLayers; ++l)
            {
                auto& lp = vp.layers[(size_t) l];
                lp.on = l == 0 ? &one : &zero; lp.vol = &zero; lp.pan = &zero; lp.tune = &zero; lp.fine = &zero; lp.root = &root; lp.delay = &zero;
                lp.loop = &zero; lp.loopBpm = &zero; lp.autoBpm = &zero; lp.nameBpm = &zero; lp.pingPong = &zero; lp.stretch = &zero;
                lp.arpChord = &one; lp.susLoop = &zero; lp.slStart = &zero; lp.slEnd = &zero; lp.slXf = &zero;
            }
            auto run = [&] (std::vector<int> chord, double bpm, int total)
            {
                hb.store (bpm);
                LayerSynth syn; syn.hostBpm = &hb; syn.glideMode = &zero;
                std::array<RoundRobin, maxLayers> rr;
                std::array<std::array<std::atomic<float>, maxLayers>, 6> posA;
                for (int v = 0; v < 6; ++v) syn.addVoice (new LayerVoice (vp, rr, syn, posA[(size_t) v]));
                LayerSound::Ptr snd = new LayerSound();
                auto& ld = snd->layers[0];
                ld.source.makeCopyOf (arpS); ld.rate = sr; ld.valid = true; ld.uid = 77;
                ld.arp = std::make_shared<const stretch::ArpAnalysis> (stretch::analyseArp (arpS, sr));
                syn.addSound (snd.get());
                syn.setCurrentPlaybackSampleRate (sr);
                std::vector<float> outL;
                for (int pos = 0; pos < total; pos += 512)
                {
                    juce::AudioBuffer<float> b (2, 512); b.clear();
                    juce::MidiBuffer mb;
                    if (pos == 0) for (int nt : chord) mb.addEvent (juce::MidiMessage::noteOn (allLayersChannel, nt, 1.0f), 0);
                    syn.renderNextBlock (b, mb, 0, 512);
                    for (int i = 0; i < 512; ++i) outL.push_back (b.getSample (0, i));
                }
                return outL;
            };
            auto rms = [] (const std::vector<float>& x, int a, int b) { double e = 0; int n = 0; for (int i = juce::jmax (0, a); i < juce::jmin ((int) x.size(), b); ++i) { e += x[(size_t) i] * x[(size_t) i]; ++n; } return n > 0 ? std::sqrt (e / n) : 0.0; };
            auto freqAt = [sr] (const std::vector<float>& x, int a, int len) { int zc = 0; for (int i = a + 1; i < a + len && i < (int) x.size(); ++i) if ((x[(size_t) i - 1] < 0.0f) != (x[(size_t) i] < 0.0f)) ++zc; return zc * sr / 2.0 / len; };
            // 1 note, FL à 70 BPM : le motif dure 2x plus longtemps, chaque pas démarre pile à son temps, dès la 1re note
            const auto one70 = run ({ 60 }, 70.0, step * 9);
            const bool instant = rms (one70, 0, 1500) > 0.02;
            bool timed = true;
            for (int k = 1; k < 4; ++k)
            {
                const int t0 = k * step * 2;
                const double gapBefore = rms (one70, t0 - step / 3, t0 - 200), after = rms (one70, t0 + 300, t0 + step / 3);
                if (! (after > 0.02 && gapBefore < after * 0.25)) timed = false;
            }
            const double f0 = freqAt (one70, 400, 2000), f2 = freqAt (one70, 4 * step + 400, 2000);
            CHECK (instant && timed && std::abs (f0 - 261.6) < 12.0 && std::abs (f2 - 392.0) < 18.0,
                   "mode arpegiateur : son tout de suite, pas cales sur 70 BPM (" << f0 << " Hz, " << f2 << " Hz)");
            // accord do + mi : une seule voix joue (même niveau qu'une note seule), le 2e pas prend la 2e note de l'accord
            const auto solo = run ({ 60 }, 140.0, step * 5), chord = run ({ 60, 64 }, 140.0, step * 5);
            const double rSolo = rms (solo, 0, step * 4), rChord = rms (chord, 0, step * 4);
            // pas 3 : l'original joue sol (une quinte au-dessus) ; en accord il reprend la 1re note (do) à l'octave du motif -> do aigu
            const double fc2 = freqAt (chord, 2 * step + 400, 1800);
            CHECK (rChord < rSolo * 1.3 && rChord > rSolo * 0.7 && std::abs (fc2 - 523.3) < 22.0,
                   "mode arpegiateur en accord : une seule voix (" << rChord / juce::jmax (1.0e-9, rSolo) << "x), pas 3 = do de l'accord (" << fc2 << " Hz)");

            // son simple (une seule note tenue) avec le bouton Arpégiateur : l'accord joue normalement (avant : une seule note)
            {
                juce::AudioBuffer<float> pad (2, (int) sr * 2);
                for (int i = 0; i < pad.getNumSamples(); ++i) { const float v = 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 261.63 * i / sr); pad.setSample (0, i, v); pad.setSample (1, i, v); }
                const auto anP = stretch::analyseArp (pad, sr);
                CHECK (! anP.arpLike, "son simple : pas pris pour un arpege (" << (int) anP.onsets.size() << " attaques)");
            }

            // Glide Legato (notes slide de FL) : la 2e note qui chevauche fait glisser le son en cours, sans le relancer,
            // et le son continue jusqu'à la fin de la note slide
            {
                std::atomic<float> legato { 1.0f }, gt { 40.0f };
                vp.glideMode = &legato; vp.glideTime = &gt;
                for (auto& lp : vp.layers) lp.arpChord = &zero;
                juce::AudioBuffer<float> sus (2, (int) sr * 3);
                for (int i = 0; i < sus.getNumSamples(); ++i) { const float v = 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 261.63 * i / sr); sus.setSample (0, i, v); sus.setSample (1, i, v); }
                hb.store (120.0);
                LayerSynth syn; syn.hostBpm = &hb; syn.glideMode = &legato; syn.glideTime = &gt;
                std::array<RoundRobin, maxLayers> rr;
                std::array<std::array<std::atomic<float>, maxLayers>, 6> posA;
                for (int v = 0; v < 6; ++v) syn.addVoice (new LayerVoice (vp, rr, syn, posA[(size_t) v]));
                LayerSound::Ptr snd = new LayerSound();
                snd->layers[0].source.makeCopyOf (sus); snd->layers[0].rate = sr; snd->layers[0].valid = true; snd->layers[0].uid = 5;
                syn.addSound (snd.get());
                syn.setCurrentPlaybackSampleRate (sr);
                std::vector<float> out;
                int activeAfterSlide = 0;
                for (int pos = 0; pos < (int) sr * 2; pos += 512)
                {
                    juce::AudioBuffer<float> b (2, 512); b.clear();
                    juce::MidiBuffer mb;
                    if (pos == 0) mb.addEvent (juce::MidiMessage::noteOn (allLayersChannel, 60, 1.0f), 0);
                    if (pos == 512 * 40) mb.addEvent (juce::MidiMessage::noteOn (allLayersChannel, 67, 1.0f), 0);   // slide (chevauche)
                    if (pos == 512 * 60) mb.addEvent (juce::MidiMessage::noteOff (allLayersChannel, 60), 0);        // note d'origine relâchée
                    if (pos == 512 * 120) mb.addEvent (juce::MidiMessage::noteOff (allLayersChannel, 67), 0);       // fin du slide
                    syn.renderNextBlock (b, mb, 0, 512);
                    if (pos == 512 * 50) { activeAfterSlide = 0; for (int v = 0; v < syn.getNumVoices(); ++v) if (syn.getVoice (v)->isVoiceActive()) ++activeAfterSlide; }
                    for (int i = 0; i < 512; ++i) out.push_back (b.getSample (0, i));
                }
                const double fBefore = freqAt (out, 512 * 10, 4000), fSlid = freqAt (out, 512 * 80, 4000);
                const double stillOn = rms (out, 512 * 80, 512 * 110), afterEnd = rms (out, 512 * 140, 512 * 160);
                CHECK (activeAfterSlide == 1 && std::abs (fBefore - 261.6) < 10.0 && std::abs (fSlid - 392.0) < 14.0 && stillOn > 0.05 && afterEnd < 0.005,
                       "legato (slide FL) : 1 seule voix (" << activeAfterSlide << "), glisse do -> sol (" << fBefore << " -> " << fSlid << " Hz), continue puis s'arrete");
                vp.glideMode = &zero; vp.glideTime = &zero;
            }
        }
        // Delay : plus de craquement (l'état du filtre de droite n'est plus écrasé par celui de gauche à chaque bloc)
        {
            const double sr = 44100.0;
            auto d = fxs::create (fxs::Delay);
            d->prepare (sr, 512);
            const float p[8] = { 6.0f / 11.0f, 0.45f, 0.3f, 0.5f, 0.6f, 0.1f, 0.0f, 0.0f };
            std::vector<float> L (256), R (256);
            float worst = 0.0f, r1 = 0.0f, r2 = 0.0f; long t = 0;
            for (int b = 0; b < 1200; ++b)
            {
                for (int i = 0; i < 256; ++i, ++t) { L[(size_t) i] = 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * t / sr); R[(size_t) i] = 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 660.0 * t / sr + 1.0); }
                d->process (L.data(), R.data(), 256, p, 120.0);
                for (int i = 0; i < 256; ++i)
                {
                    if (b > 200) worst = juce::jmax (worst, std::abs (R[(size_t) i] - 2.0f * r1 + r2));
                    r2 = r1; r1 = R[(size_t) i];
                }
            }
            CHECK (worst < 0.05f, "delay : pas de craquement a droite (saut max " << worst << ")");
        }
        // Play de FL : la progression du plugin suit la position de FL (départ, saut au milieu d'une note, boucle)
        {
            PreviewPlayer pl;
            auto seq = std::make_unique<PreviewPlayer::Sequence>();
            seq->lengthBeats = 4.0;
            seq->events.push_back ({ 1.0, juce::MidiMessage::noteOn (1, 60, 0.8f) });
            seq->events.push_back ({ 3.0, juce::MidiMessage::noteOff (1, 60) });
            pl.setSequence (std::move (seq));
            const double sr = 48000.0, bpm = 120.0, bps = bpm / 60.0 / sr;
            double ppq = 0.0; int onAt = -1, offAt = -1; long t = 0;
            for (int b = 0; b < 200; ++b)
            {
                juce::MidiBuffer mb;
                pl.renderSynced (mb, 480, bpm, sr, ppq);
                for (const auto m : mb) { if (m.getMessage().isNoteOn() && onAt < 0) onAt = (int) (t + m.samplePosition); if (m.getMessage().isNoteOff() && offAt < 0) offAt = (int) (t + m.samplePosition); }
                ppq += 480 * bps; t += 480;
            }
            juce::MidiBuffer mid;
            pl.renderSynced (mid, 480, bpm, sr, 6.0);   // saut au milieu de la note (6 = temps 2 de la 2e boucle)
            bool retrig = false; for (const auto m : mid) if (m.getMessage().isNoteOn() && m.samplePosition == 0) retrig = true;
            CHECK (std::abs (onAt - 24000) < 2 && std::abs (offAt - 72000) < 2 && retrig, "play de FL : notes calees (on " << onAt << ", off " << offAt << "), relance au milieu");
        }
        // Chroma : une note hors gamme (C# en C majeur) est recalée sur une note de la gamme, une note juste (A) reste intacte
        {
            auto goertzel = [] (const std::vector<float>& x, double f, double sr)
            {
                const double w = 2.0 * juce::MathConstants<double>::pi * f / sr, c = 2.0 * std::cos (w);
                double s1 = 0, s2 = 0;
                for (float v : x) { const double s0 = v + c * s1 - s2; s2 = s1; s1 = s0; }
                return std::sqrt (s1 * s1 + s2 * s2 - c * s1 * s2);
            };
            auto run = [&] (double freq, std::vector<float>& out)
            {
                const double sr = 44100.0;
                auto e = fxs::create (fxs::Harmonic);
                e->prepare (sr, 512);
                fxs::host().key = 0; fxs::host().scaleMask = 0xAB5;
                const float p[8] = { 1.0f / 12.0f, 1.0f / 13.0f, 0.5f, 1.0f, 0.5f, 0.5f, 0.5f, 0.5f };   // C, Majeur, couleur 100 %, correction 100 %, normal
                e->mix = 1.0f;
                std::vector<float> L (512), R (512);
                out.clear();
                for (int b = 0; b < 200; ++b)
                {
                    for (int i = 0; i < 512; ++i) L[(size_t) i] = R[(size_t) i] = 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * freq * (b * 512 + i) / sr);
                    e->process (L.data(), R.data(), 512, p, 120.0);
                    if (b > 40) out.insert (out.end(), L.begin(), L.end());
                }
                return e->latency (p);
            };
            std::vector<float> o1, o2;
            const int lat = run (277.18, o1);
            const double sr = 44100.0;
            const double cs = goertzel (o1, 277.18, sr), c = goertzel (o1, 261.63, sr), d = goertzel (o1, 293.66, sr);
            run (440.0, o2);
            const double a = goertzel (o2, 440.0, sr), aExpected = 0.3 * o2.size() / 2.0;
            bool finite = true; for (float v : o1) if (! std::isfinite (v)) finite = false;
            CHECK (finite && juce::jmax (c, d) > 4.0 * cs && lat == 2048, "chroma : C# recale (C " << (int) c << ", D " << (int) d << ", C# " << (int) cs << "), latence " << lat);
            CHECK (a > 0.8 * aExpected && a < 1.2 * aExpected, "chroma : note juste intacte (" << (int) (100 * a / aExpected) << " %)");
        }
        // « Hors gamme » à 0 : toutes les humeurs, toutes les gammes -> accords entièrement dans la gamme
        {
            bool allIn = true; int checked = 0, fixedN = 0;
            for (int sc : { (int) theory::Major, (int) theory::Minor })
                for (int mood = 0; mood < 10; ++mood)
                    for (int seed = 0; seed < 12; ++seed)
                    {
                        gen::Settings s; s.mood = mood; s.key = (seed * 5) % 12; s.scaleIdx = sc; s.seed = (juce::uint32) (seed * 977 + mood); s.spice = 0.0f; s.variety = 1.0f;
                        for (auto c : gen::generateProgression (s).chords)
                        {
                            ++checked;
                            if (gen::conformToScale (c, s.key, sc)) ++fixedN;
                            if (c.close.size() < 3) allIn = false;
                            for (int n : c.close) if (! theory::inScale (n, s.key, theory::harmonyScale (sc))) allIn = false;
                        }
                    }
            CHECK (allIn && checked > 500, "hors gamme a 0 : " << checked << " accords tous dans la gamme (" << fixedN << " ramenes)");
        }
        const double sr = 44100.0;
        juce::AudioBuffer<float> loop (1, (int) (sr * 4.0));
        loop.clear();
        for (int b = 0; b < 8; ++b)
            for (int i = 0; i < 3000; ++i) loop.setSample (0, (int) (b * 0.5 * sr) + i, std::exp (-i / 600.0f) * std::sin (i * 0.3f));
        const auto ts = slicer::tempoSlices (loop, sr, 120.0, 8);
        bool onBeat = ts.size() == 8;
        for (size_t k = 1; k < ts.size(); ++k) if (std::abs (ts[k] - (int) (k * 0.5 * sr)) > 400) onBeat = false;
        CHECK (onBeat, "slicer au tempo : 8 tranches sur les temps a 120 BPM");

        {
            // outil de réglage : VARIANT_KICK_ASK="question 1;question 2" affiche le classement de Kick
            juce::StringArray asks;
            asks.addTokens (juce::SystemStats::getEnvironmentVariable ("VARIANT_KICK_ASK", {}), ";", "");
            for (auto& a : asks) if (a.trim().isNotEmpty()) std::cout << "   ? " << a << "\n" << kick::explain (a) << "\n";
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            const auto gen = kicktest::generated (false);
            const auto typo = kicktest::generated (true);
            const auto real = kicktest::real();
            const auto non = kicktest::nonsense();
            kicktest::print ("questions generees", gen, 120);
            kicktest::print ("fautes de frappe", typo, 40);
            kicktest::print ("vraies questions", real, 80);
            kicktest::print ("hors sujet", non, 20);
            std::cout << "   Kick : " << (int) kick::faq().size() << " fiches, " << (gen.total + typo.total + real.total + non.total)
                      << " questions en " << juce::roundToInt ((juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0) << " s\n";
            CHECK (gen.rate() >= 0.97f, "Kick : questions generees >= 97 %");
            CHECK (typo.rate() >= 0.90f, "Kick : fautes de frappe >= 90 %");
            CHECK (real.rate() >= 0.95f, "Kick : vraies questions >= 95 %");
            CHECK (non.rate() >= 0.80f, "Kick : hors sujet reconnu >= 80 %");
            {
                // la page affichée départage les potards du même nom
                struct Ctx { const char* q; int tab; const char* expect; };
                const Ctx ctx[] = { { "a quoi sert la vitesse", 3, "1/16" }, { "vitesse ?", 5, "LFO" }, { "c'est quoi la vitesse", 1, "plusieurs effets" },
                                    { "attaque", 4, "chop" }, { "attaque", 0, "montée" }, { "transpo", 3, "arpège" }, { "transpo", 4, "tranches" } };
                int okCtx = 0;
                for (auto& c : ctx)
                {
                    const auto m = kick::find (juce::String::fromUTF8 (c.q), -2, c.tab);
                    if (m.index >= 0 && kick::faq()[(size_t) m.index].answer.contains (juce::String::fromUTF8 (c.expect))) ++okCtx;
                    else std::cout << "      x page " << c.tab << " : " << c.q << " -> " << kick::subjectOf (m.index) << "\n";
                }
                CHECK (okCtx == (int) (sizeof (ctx) / sizeof (ctx[0])), "Kick : la page affichee departage les potards (" << okCtx << ")");
            }
        }
    }

    // --- v1.1 : effets calés sur le tempo
    {
        const double r1 = fxs::syncedHz (1.9, 120.0), r2 = fxs::syncedHz (1.9, 90.0);
        double d = 0;
        fxs::syncedHz (0.5, 120.0, &d);
        float ph = 0.0f;
        fxs::host() = { 120.0, 3.5, true };
        fxs::lockPhase (1.0, ph);
        CHECK (std::abs (r1 - 2.0) < 1e-9 && std::abs (r2 - 2.0) < 1e-9 && std::abs (d - 4.0) < 1e-9 && std::abs (ph - juce::MathConstants<float>::pi) < 1e-3f,
               "effets : vitesses calees sur le tempo (" << r1 << " Hz a 120, " << r2 << " Hz a 90)");
        fxs::host() = {};
    }

    // --- v1.2 : pas de micro-tranches
    {
        const auto st = slicer::enforceMinLength ({ 0, 2000, 30000, 31000, 60000, 88000 }, 90000, 44100.0);
        bool ok = st.front() == 0;
        for (size_t k = 1; k < st.size(); ++k) if (st[k] - st[k - 1] < 8820) ok = false;
        if (90000 - st.back() < 8820) ok = false;
        CHECK (ok && st.size() == 3, "slicer : aucune tranche de moins de 0,2 s (" << st.size() << " tranches)");
    }

    // --- v1.3 : effets créatifs
    {
        // shaper : la forme sidechain coupe bien le début du temps
        CHECK (fxs::shaperShape (0, 0.0f) < 0.05f && fxs::shaperShape (0, 0.9f) > 0.9f && fxs::shaperShape (2, 0.7f) == 0.0f, "shaper : formes correctes");
        // glitch : motif répétable (même pas = même événement) et densité 0 = rien
        float p[fxs::numP] = { 0.6f, 0.45f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.3f };
        bool rep = true; int events = 0;
        for (int k = 0; k < 16; ++k) { if (fxs::glitchEventFor (k, p) != fxs::glitchEventFor (k + 16, p)) rep = false; if (fxs::glitchEventFor (k, p) != 0) ++events; }
        p[0] = 0.0f; bool none = true; for (int k = 0; k < 16; ++k) if (fxs::glitchEventFor (k, p) != 0) none = false;
        CHECK (rep && events > 3 && none, "glitch : motif de 16 pas repetable (" << events << " evenements)");
        // halftime : à moitié vitesse, une sinusoïde de 800 Hz descend vers 400 Hz (mode bande)
        fxs::HalfTimeFx ht; ht.prepare (48000.0, 512);
        float hp[fxs::numP] = { 0.0f, 0.67f, 0.0f, 0.3f, 0.0f, 1.0f, 0.0f, 0.0f };
        int crossings = 0; float last = 0.0f;
        for (int b = 0; b < 160; ++b)
        {
            float L[512], R[512];
            for (int i = 0; i < 512; ++i) L[i] = R[i] = std::sin ((float) (b * 512 + i) * juce::MathConstants<float>::twoPi * 800.0f / 48000.0f) * 0.5f;
            ht.process (L, R, 512, hp, 120.0);
            if (b >= 100 && b < 140) for (int i = 0; i < 512; ++i) { if ((last < 0.0f) != (L[i] < 0.0f)) ++crossings; last = L[i]; }
        }
        const float freq = crossings / 2.0f / (40.0f * 512.0f / 48000.0f);
        CHECK (std::abs (freq - 400.0f) < 40.0f, "halftime : la hauteur descend d'une octave en mode bande (" << freq << " Hz)");
    }

    // --- Chroma (v2.4) : une note dans la gamme, même un peu fausse (450 Hz = La + 39 cents), n'est pas touchée
    {
        auto cPtr = std::make_unique<fxs::HarmonicFx>(); auto& c = *cPtr; c.prepare (48000.0, 512); c.mix = 1.0f;
        float p[fxs::numP] = { 1.0f / 12.0f, 1.0f / 13.0f, 0.5f, 1.0f, 0.5f, 0.5f, 0.5f, 0.5f };   // C, majeur, correction 100 %
        int crossings = 0; float last = 0.0f;
        for (int b = 0; b < 200; ++b)
        {
            float L[512], R[512];
            for (int i = 0; i < 512; ++i) L[i] = R[i] = std::sin ((float) (b * 512 + i) * juce::MathConstants<float>::twoPi * 450.0f / 48000.0f) * 0.3f;
            c.process (L, R, 512, p, 120.0);
            if (b >= 100) for (int i = 0; i < 512; ++i) { if ((last < 0.0f) != (L[i] < 0.0f)) ++crossings; last = L[i]; }
        }
        const float freq = crossings / 2.0f / (100.0f * 512.0f / 48000.0f);
        CHECK (std::abs (freq - 450.0f) < 4.0f, "chroma : 450 Hz (dans la gamme) garde sa justesse (" << freq << " Hz)");
    }

    // --- v1.5 : plus d'accord décalé d'une octave, Glitch sans clics
    {
        int bad = 0, total = 0;
        for (int mood = 0; mood < (int) moods::list().size(); ++mood)
            for (int seed = 0; seed < 12; ++seed)
            {
                gen::Settings s; s.mood = mood; s.key = seed % 12; s.scaleIdx = moods::list()[(size_t) mood].scales.front();
                s.seed = (juce::uint32) (seed * 977 + mood); s.variety = 0.8f;
                const auto r = gen::render (gen::generateProgression (s), s);
                std::vector<double> means;
                for (auto& c : r.chords) { double m = 0; for (int x : c.voiced) m += x; if (! c.voiced.empty()) means.push_back (m / c.voiced.size()); }
                auto sorted = means; std::sort (sorted.begin(), sorted.end());
                for (double m : means) { ++total; if (std::abs (m - sorted[sorted.size() / 2]) > 7.0) ++bad; }
            }
        CHECK (bad == 0, "accords : aucun accord ne saute d'une octave (" << bad << "/" << total << ")");

        // glitch : sinus 220 Hz, motif "Chaos", la plus grande marche entre 2 échantillons reste douce
        fxs::GlitchFx gl; gl.prepare (48000.0, 512);
        float p[fxs::numP] = { 1.0f, 0.45f, 0.5f, 0.75f, 0.0f, 0.6f, 0.35f, 0.3f };
        float maxJump = 0.0f, last = 0.0f; int jumpAt = 0;
        for (int b = 0; b < 400; ++b)
        {
            float L[512], R[512];
            for (int i = 0; i < 512; ++i) L[i] = R[i] = std::sin ((float) (b * 512 + i) * juce::MathConstants<float>::twoPi * 220.0f / 48000.0f) * 0.5f;
            gl.process (L, R, 512, p, 120.0);
            for (int i = 0; i < 512; ++i) { if (b > 2 && std::abs (L[i] - last) > maxJump) { maxJump = std::abs (L[i] - last); jumpAt = b * 512 + i; } last = L[i]; }
        }
        { const auto ln = fxs::glitchStep (fxs::factoryGlitch (12), jumpAt / 12000, 0.3f); std::cout << "   saut au pas " << jumpAt / 12000 << " t=" << jumpAt % 12000 << " pistes " << ln[0] << ln[1] << ln[2] << ln[3] << ln[4] << ln[5] << "\n"; }
        CHECK (maxJump < 0.12f, "glitch : pas de clic (plus grand saut " << maxJump << ")");
    }

    {
        // SoundFont de FL Studio : presets lus, échantillon extrait avec une note de base plausible
        const juce::File sfFile ("C:/Program Files/Image-Line/FL Studio 2024/Data/Patches/Soundfonts/STR_Ensemble.sf2");
        if (sfFile.existsAsFile())
        {
            sf2::File sf;
            const bool ok = sf.open (sfFile);
            CHECK (ok && ! sf.getPresets().empty(), "sf2 : presets lus (" << (ok ? (int) sf.getPresets().size() : 0) << ")");
            if (ok)
            {
                sf2::Extracted ex;
                const bool got = sf.extract (sf.getPresets().front().index, 60, ex);
                std::cout << "   sf2 : " << sf.getPresets().front().name << " root " << ex.rootNote << " ct " << ex.cents << " ch " << ex.audio.getNumChannels()
                          << " len " << ex.audio.getNumSamples() / ex.sampleRate << " s loop " << ex.looped << "\n";
                CHECK (got && ex.audio.getNumSamples() > 1000 && ex.rootNote > 20 && ex.rootNote < 110
                           && ex.audio.getMagnitude (0, ex.audio.getNumSamples()) <= 1.0f, "sf2 : échantillon extrait");
                const auto all = sf.extractAll (sf.getPresets().front().index);
                int loops = 0; for (auto& z : all) if (z.looped && z.loopEnd > z.loopStart) ++loops;
                std::cout << "   sf2 : " << all.size() << " zones, " << loops << " en boucle\n";
                CHECK (all.size() >= 1 && all.front().keyLo <= all.front().keyHi, "sf2 : zones multi-échantillons");
            }
        }
    }

    {
        // égaliseur paramétrique : une cloche +12 dB à 1 kHz monte bien à 1 kHz et pas à 100 Hz
        auto c = fxs::defaultEq();
        for (auto& b : c) b.on = false;
        c[3] = { true, fxs::EqBell, 1000.0f, 12.0f, 1.0f, 2 };
        const double at1k = fxs::eqResponseDb (c, 1000.0, 48000.0), at100 = fxs::eqResponseDb (c, 100.0, 48000.0);
        CHECK (std::abs (at1k - 12.0) < 0.5 && std::abs (at100) < 1.0, "eq : cloche +12 dB (" << at1k << " / " << at100 << ")");
        c[3].on = false; c[0] = { true, fxs::EqLowCut, 200.0f, 0.0f, 0.7f, 4 };
        CHECK (fxs::eqResponseDb (c, 50.0, 48000.0) < -40.0, "eq : coupe-bas 48 dB/oct raide");

        // nappe : un son court devient une traîne longue
        fxs::PadFx pad; pad.prepare (48000.0, 512);
        float pp[fxs::numP] = { 0.0f, 0.6f, 0.3f, 0.6f, 0.5f, 0.3f, 1.0f / 3.0f, 0.8f };
        double tail = 0.0;
        for (int b = 0; b < 200; ++b)
        {
            float L[512], R[512];
            for (int i = 0; i < 512; ++i) { const int k = b * 512 + i; L[i] = R[i] = k < 4800 ? std::sin ((float) k * 0.05f) * 0.5f : 0.0f; }
            pad.process (L, R, 512, pp, 120.0);
            if (b > 100) for (int i = 0; i < 512; ++i) tail += std::abs (L[i]);
        }
        CHECK (tail > 1.0, "nappe : le son tient longtemps (" << tail << ")");
    }

    {
        // registre des accords : jamais tout en bas ni tout en haut (moyenne autour de C5 dans FL)
        int bad = 0, total = 0; double lo = 999, hi = -999;
        for (int mood = 0; mood < (int) moods::list().size(); ++mood)
            for (int seed = 0; seed < 25; ++seed)
            {
                gen::Settings s; s.mood = mood; s.key = (seed * 5) % 12; s.scaleIdx = moods::list()[(size_t) mood].scales.front();
                s.seed = (juce::uint32) (seed * 7919 + mood * 31); s.variety = 0.9f; s.richness = seed % 3;
                const auto r = gen::render (gen::generateProgression (s), s);
                for (auto& c : r.chords)
                {
                    if (c.voiced.empty()) continue;
                    double m = 0; for (int x : c.voiced) m += x; m /= (double) c.voiced.size();
                    ++total; lo = juce::jmin (lo, m); hi = juce::jmax (hi, m);
                    if (m < 54.0 || m > 70.0) ++bad;
                }
            }
        CHECK (bad == 0, "accords : registre médium (" << bad << "/" << total << " hors zone, moyennes " << lo << " à " << hi << ")");
    }

    {
        // presets d'arpège : motifs de 16 pas, effets et réglages valides
        int bad = 0;
        for (auto& p : presets::factory())
        {
            if (std::strlen (p.steps) != 16 || p.keywords.empty() || p.rate < 0 || p.rate > arp::R1_32 || p.mode < 0 || p.mode > arp::Thumb) ++bad;
            for (auto& fx : p.fx) if (fx.type <= 0 || fx.type >= fxs::NumTypes || fx.preset >= (int) fxs::def (fx.type).presets.size()) ++bad;
        }
        CHECK (bad == 0 && presets::factory().size() >= 10, "presets d'arpège : " << presets::factory().size() << " presets valides (" << bad << " erreurs)");
    }

    {
        // « Accords autour de mes notes » : la mélodie reste, les accords sont sous la mélodie et contiennent ses notes fortes
        std::vector<gen::Note> mel;
        const int pitches[] = { 76, 74, 72, 74, 76, 76, 76, 74, 74, 74, 76, 79, 79 };
        for (int i = 0; i < 13; ++i) mel.push_back ({ i * 0.5, 0.5, pitches[i], 0.8f, 0 });
        int bad = 0;
        for (int variant = 0; variant < 4; ++variant)
        {
            const auto h = gen::harmonize (mel, 0, theory::Major, 1, true, variant);
            if (h.chords.empty()) ++bad;
            for (auto& m : mel)   // chaque note de la mélodie est toujours là
            {
                bool found = false;
                for (auto& n : h.notes) if (n.pitch == m.pitch && std::abs (n.start - m.start) < 1.0e-6) found = true;
                if (! found) ++bad;
            }
            for (auto& c : h.chords)
            {
                int melLow = 127;
                for (auto& m : mel) if (m.start < c.start + c.length && m.start + m.length > c.start) melLow = juce::jmin (melLow, (int) m.pitch);
                for (int x : c.voiced) if (x >= melLow) ++bad;   // accord sous la mélodie
            }
        }
        // fondamentales posées : un accord par note, construit sur elle
        std::vector<gen::Note> roots { { 0, 4, 48, 0.8f, 0 }, { 4, 4, 45, 0.8f, 0 }, { 8, 4, 41, 0.8f, 0 }, { 12, 4, 43, 0.8f, 0 } };
        const auto h2 = gen::harmonize (roots, 0, theory::Major, 1, false, 0);
        if (h2.chords.size() != 4) ++bad;
        else for (size_t i = 0; i < 4; ++i) if (theory::mod12 (h2.chords[i].root) != theory::mod12 (roots[i].pitch)) ++bad;
        CHECK (bad == 0, "accords autour de mes notes : mélodie gardée, accords dessous, fondamentales respectées (" << bad << " erreurs)");
    }

    {
        // synthé : chaque preset d'usine sonne, sans valeur folle (NaN / saturation énorme)
        int bad = 0, silent = 0;
        for (int i = 0; i < (int) synthpresets::factory().size(); ++i)
        {
            auto e0 = std::make_unique<wt::Engine>(); auto& e = *e0;
            e.prepare (44100.0);
            synthpresets::applyFactory (e, i);
            e.enabled.store (true);
            juce::AudioBuffer<float> buf (2, 512);
            float peak = 0.0f;
            for (int b = 0; b < 60; ++b)
            {
                buf.clear();
                juce::MidiBuffer m;
                if (b == 0) { m.addEvent (juce::MidiMessage::noteOn (1, 48, 0.9f), 0); m.addEvent (juce::MidiMessage::noteOn (1, 55, 0.9f), 0); }
                if (b == 40) { m.addEvent (juce::MidiMessage::noteOff (1, 48), 0); m.addEvent (juce::MidiMessage::noteOff (1, 55), 0); }
                e.render (buf, m, 512, 120.0, b * 512 / 44100.0 * 2.0, true);
                for (int ch = 0; ch < 2; ++ch)
                    for (int s = 0; s < 512; ++s)
                    {
                        const float v = buf.getSample (ch, s);
                        if (! std::isfinite (v)) ++bad;
                        peak = juce::jmax (peak, std::abs (v));
                    }
            }
            if (peak < 0.01f) { ++silent; std::cout << "  preset muet : " << synthpresets::factory()[(size_t) i].name << "\n"; }
            if (peak > 6.0f) { ++bad; std::cout << "  preset trop fort : " << synthpresets::factory()[(size_t) i].name << " " << peak << "\n"; }
        }
        CHECK (bad == 0 && silent == 0, "synthé : " << synthpresets::factory().size() << " presets d'usine jouent proprement (" << bad << " erreurs, " << silent << " muets)");

        // presets d'arpège synthé (style Hive / Diva / Zenology) : chaque son joue proprement
        {
            int bad2 = 0, silent2 = 0;
            for (auto& sp : presets::synthArps())
            {
                auto ePtr = std::make_unique<wt::Engine>(); auto& e = *ePtr;
                e.prepare (44100.0);
                e.patch.reset();
                juce::String ta = "builtin:0", tb = "builtin:0";
                sp.make (e.patch, ta, tb);
                if (auto t = wt::tableFromPath (synthpresets::resolveTable (ta))) e.setTable (0, t);
                if (auto t = wt::tableFromPath (synthpresets::resolveTable (tb))) e.setTable (1, t);
                e.enabled.store (true);
                juce::AudioBuffer<float> buf (2, 512);
                float peak = 0.0f;
                for (int b = 0; b < 40; ++b)
                {
                    buf.clear();
                    juce::MidiBuffer m;
                    if (b % 4 == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60 + (b / 4) % 3 * 4, 0.9f), 0);
                    if (b % 4 == 2) m.addEvent (juce::MidiMessage::noteOff (1, 60 + (b / 4) % 3 * 4), 0);
                    e.render (buf, m, 512, 120.0, b * 512 / 44100.0 * 2.0, true);
                    for (int ch = 0; ch < 2; ++ch) for (int s = 0; s < 512; ++s) { const float v = buf.getSample (ch, s); if (! std::isfinite (v)) ++bad2; peak = juce::jmax (peak, std::abs (v)); }
                }
                if (peak < 0.01f) { ++silent2; std::cout << "  arp synthé muet : " << sp.name << "\n"; }
                if (peak > 6.0f) { ++bad2; std::cout << "  arp synthé trop fort : " << sp.name << " " << peak << "\n"; }
            }
            CHECK (bad2 == 0 && silent2 == 0 && presets::synthArps().size() >= 15, "arpeges synthe : " << presets::synthArps().size() << " sons jouent proprement");
        }

        // table Serum lue depuis le disque (si Serum est installé)
        const auto f = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Xfer/Serum Presets/Tables/Analog/Basic Shapes.wav");
        if (f.existsAsFile())
        {
            auto t = wt::loadWav (f);
            CHECK (t != nullptr && t->frames >= 2, "synthé : table Serum chargée (" << (t != nullptr ? t->frames : 0) << " positions)");
        }
    }

    {
        // slicer « Mes chops » : chaque zone a sa propre fin
        slicer::SliceSound s;
        s.data = std::make_shared<const juce::AudioBuffer<float>> (1, 44100);
        s.starts = { 1000, 20000 }; s.ends = { 8000, 30000 };
        CHECK (s.endOf (0) == 8000 && s.endOf (1) == 30000, "slicer : zones choisies à la main (fin propre à chaque chop)");
    }

    // --- Qualité+ : types reconnus par le nom, polissage propre (pas de saturation, même niveau perçu), effets cohérents
    {
        juce::AudioBuffer<float> none;
        auto cat = [&none] (const char* path) { return polish::classify (juce::File (juce::String::fromUTF8 (path)), none, 44100.0); };
        const bool names = cat ("C:/x/Variant Basses/808 boom.wav") == polish::Bass && cat ("C:/x/Variant Leads/Supersaw lead.wav") == polish::Lead
                        && cat ("C:/x/Variant Pad/Warm Pad 3.wav") == polish::Pad && cat ("C:/x/Variant Keys/RIO - RHODES 1.wav") == polish::ElectricPiano
                        && cat ("C:/x/Variant Submundo/Lead 2.wav") == polish::Lead && cat ("C:/x/Variant Seq/thing.wav") == polish::Arp
                        && cat ("C:/x/Variant Bells/Bell 4.wav") == polish::Pluck && cat ("C:/x/zOther Fun Stuff/weird.wav") == polish::NoFx;
        CHECK (names, "qualite+ : types reconnus (808 = basse, Submundo n'est pas une basse, Rhodes = piano electrique...)");
        bool noFxOnBass = polish::effectsFor (polish::Bass).empty() && polish::effectsFor (polish::Percussive).empty() && polish::effectsFor (polish::NoFx).empty();
        bool leadHasSpace = false;
        for (auto& p : polish::effectsFor (polish::Lead)) if (p.type == fxs::Reverb && p.mix > 0.05f && p.mix < 0.3f) leadHasSpace = true;
        CHECK (noFxOnBass && leadHasSpace, "effets auto : rien sur basses / percussions, reverb dosee sur les leads");

        const double sr = 44100.0;
        bool clean = true; double worstLevel = 0.0;
        for (int c = 1; c <= 11; ++c)
        {
            juce::AudioBuffer<float> b (2, (int) sr);
            juce::Random rng (c);
            const double f = c == polish::Bass ? 55.0 : 330.0;
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float env = std::exp (-2.0f * (float) i / (float) sr);
                const float v = env * (0.6f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * f * i / sr) + 0.2f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * f * 3.01 * i / sr)) + 0.01f * (rng.nextFloat() - 0.5f);
                b.setSample (0, i, v); b.setSample (1, i, v * 0.9f);
            }
            const float r0 = b.getRMSLevel (0, 0, b.getNumSamples());
            polish::process (b, sr, (polish::Category) c);
            for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < b.getNumSamples(); ++i) { const float v = b.getSample (ch, i); if (! std::isfinite (v) || std::abs (v) > 0.95f) clean = false; }
            const double ratio = b.getRMSLevel (0, 0, b.getNumSamples()) / juce::jmax (1.0e-9f, r0);
            worstLevel = juce::jmax (worstLevel, std::abs (20.0 * std::log10 (ratio)));
        }
        CHECK (clean && worstLevel < 3.0, "qualite+ : sons polis propres (crete < -0,5 dB) et au meme niveau (ecart max " << worstLevel << " dB)");

        // vrais sons de ta banque : quel type pour chaque dossier (contrôle visuel)
        const auto bank = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (juce::String::fromUTF8 ("Variant Bibliothèque"));
        if (bank.isDirectory())
        {
            juce::AudioFormatManager fm; fm.registerBasicFormats();
            for (auto& d : bank.findChildFiles (juce::File::findDirectories, false))
            {
                std::map<juce::String, int> counts;
                int done = 0;
                for (auto& f : d.findChildFiles (juce::File::findFiles, false, "*.wav"))
                {
                    if (++done > 12) break;
                    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
                    juce::AudioBuffer<float> b;
                    if (rd != nullptr) { b.setSize ((int) juce::jlimit (1u, 2u, rd->numChannels), (int) juce::jmin<juce::int64> (rd->lengthInSamples, (juce::int64) (rd->sampleRate * 8))); rd->read (&b, 0, b.getNumSamples(), 0, true, true); }
                    ++counts[polish::categoryName (polish::classify (f, b, rd != nullptr ? rd->sampleRate : 44100.0))];
                }
                juce::String line;
                for (auto& [k, v] : counts) line << k << " x" << v << "  ";
                if (line.isNotEmpty()) std::cout << "info : " << d.getFileName() << " -> " << line << "\n";
            }
        }
    }

    // --- Destroy : le one-shot devient autre chose, propre, et différent à chaque clic
    {
        const double sr = 44100.0;
        juce::AudioBuffer<float> src (2, (int) (sr * 1.2));
        for (int i = 0; i < src.getNumSamples(); ++i) { const float v = std::sin (juce::MathConstants<float>::twoPi * 220.0f * (float) i / (float) sr) * std::exp (-(float) i / (float) sr * 3.0f) * 0.8f; src.setSample (0, i, v); src.setSample (1, i, v); }
        bool allOk = true; int different = 0; float lastSig = 0.0f, maxJump = 0.0f; juce::StringArray seen;
        for (int run = 0; run < 30; ++run)
        {
            juce::Random rng (1000 + run);
            juce::StringArray recipe;
            auto out = destroy::process (src, sr, run % 3, rng, recipe);
            float peak = 0.0f, diff = 0.0f, jump = 0.0f; bool finite = true;
            for (int i = 0; i < out.getNumSamples(); ++i) { const float v = out.getSample (0, i); if (! std::isfinite (v)) finite = false; peak = juce::jmax (peak, std::abs (v)); if (i < src.getNumSamples()) diff += std::abs (v - src.getSample (0, i)); if (i > 0) jump = juce::jmax (jump, std::abs (v - out.getSample (0, i - 1))); }
            if (! finite || peak < 0.1f || peak > 0.95f) allOk = false;
            maxJump = juce::jmax (maxJump, jump);
            if (diff / (float) src.getNumSamples() > 0.02f) ++different;
            seen.addIfNotAlreadyThere (recipe.joinIntoString ("+"));
            juce::ignoreUnused (lastSig);
        }
        CHECK (allOk && maxJump < 0.25f, "metamorphose : 30 sons propres (pas de NaN, ni silence, ni saturation, ni clic : saut max " << maxJump << ")");
        CHECK (different >= 27 && seen.size() >= 12, "metamorphose : le son change vraiment (" << different << "/30), recettes differentes (" << seen.size() << ")");
    }

    // --- effets et BPM : un réglage garde la même valeur musicale quand le tempo change
    {
        struct Probe : fxs::Effect
        {
            void prepare (double, int) override {}
            void reset() override {}
            void process (float*, float*, int, const float*, double) override {}
        } e;
        double d120 = 0, d140 = 0;
        const double hz = 4.0;   // ≈ croche à 120 BPM
        const double r120 = e.lockedHz (0, 0.5f, hz, 120.0, &d120);
        const double r140 = e.lockedHz (0, 0.5f, hz, 140.0, &d140);
        CHECK (std::abs (d120 - d140) < 1.0e-9 && std::abs (r140 / r120 - 140.0 / 120.0) < 1.0e-6,
               "effets : la vitesse garde sa division quand le BPM change (" << r120 << " Hz -> " << r140 << " Hz)");
        const double s1 = e.tempoScale (1, 0.3f, 100.0), s2 = e.tempoScale (1, 0.3f, 50.0);
        CHECK (std::abs (s1 - 1.0) < 1.0e-9 && std::abs (s2 - 2.0) < 1.0e-9, "effets : une duree libre (reverb) double quand le BPM est divise par 2");
        const double s3 = e.tempoScale (1, 0.4f, 50.0);
        CHECK (std::abs (s3 - 1.0) < 1.0e-9, "effets : potard tourne = la valeur vaut au tempo actuel");
        const int a = e.lockedSamples (2, 0.5f, 250.0, 120.0, 48000.0), b = e.lockedSamples (2, 0.5f, 250.0, 60.0, 48000.0);
        CHECK (std::abs (b - 2 * a) <= 1, "effets : longueur des grains (Portal / Crystal) suit le tempo");
    }

    std::cout << (failures == 0 ? "\nTOUS LES TESTS PASSENT\n" : "\nDES TESTS ECHOUENT\n");
    return failures == 0 ? 0 : 1;
}

