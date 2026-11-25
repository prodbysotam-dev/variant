#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <memory>
#include <limits>
#include "Theory.h"
#include "ArpCore.h"
#include "SamplerVoice.h"

//==============================================================================
// Événements MIDI programmés dans le futur (strum, humanisation).
class MidiScheduler
{
public:
    void prepare() { events.clear(); events.reserve (2048); }

    void add (juce::int64 time, const juce::MidiMessage& m)
    {
        if (events.size() < events.capacity())
            events.push_back ({ time, m });
    }

    void popInto (juce::MidiBuffer& out, juce::int64 blockStart, int n)
    {
        const auto end = blockStart + n;
        for (auto& e : events)
            if (e.time < end)
                out.addEvent (e.msg, (int) juce::jlimit<juce::int64> (0, n - 1, e.time - blockStart));

        events.erase (std::remove_if (events.begin(), events.end(), [end] (const Ev& e) { return e.time < end; }),
                      events.end());
    }

    void clearNoteOns()
    {
        events.erase (std::remove_if (events.begin(), events.end(), [] (const Ev& e) { return e.msg.isNoteOn(); }),
                      events.end());
    }

private:
    struct Ev { juce::int64 time; juce::MidiMessage msg; };
    std::vector<Ev> events;
};

//==============================================================================
// Une note jouée -> un accord (fixe ou selon la gamme), avec couleurs jazzy et registre.
class ChordEngine
{
public:
    enum Register { AsPlayed = 0, Smooth, Low, High };

    struct Settings
    {
        int mode = 0;        // 0 = accord fixe, 1 = selon la gamme
        int type = 0, key = 0, scaleIdx = 0, size = 4, voicing = 0, inversion = 0, reg = AsPlayed;
        float strumMs = 0.0f, humanize = 0.0f, spice = 0.0f;
    };

    void prepare (double sr) { sampleRate = sr; for (auto& m : map) m.count = 0; prevChord.clear(); prevChord.reserve (16); }

    static std::vector<int> chordFor (int note, const Settings& s, juce::Random& rng, const std::vector<int>& prev)
    {
        std::vector<int> notes;
        if (s.mode == 0)
        {
            const auto& t = theory::chordTypes()[(size_t) juce::jlimit (0, (int) theory::chordTypes().size() - 1, s.type)];
            for (int iv : t.iv) notes.push_back (note + iv);
        }
        else
        {
            notes = theory::diatonicChord (note, s.key, s.scaleIdx, s.size);
        }

        notes = theory::colourChord (notes, s.spice, rng);

        if (s.reg == Smooth && ! prev.empty())
            notes = theory::voiceLead (notes, prev, 43, 84);
        notes = theory::applyVoicing (notes, s.voicing, s.reg == Smooth ? 0 : s.inversion);

        if (s.reg == Low || s.reg == High)
        {
            const int lo = s.reg == Low ? 36 : 60;
            while (notes.front() < lo)      for (auto& n : notes) n += 12;
            while (notes.front() >= lo + 12) for (auto& n : notes) n -= 12;
        }

        notes.erase (std::remove_if (notes.begin(), notes.end(), [] (int n) { return n < 0 || n > 127; }), notes.end());
        std::sort (notes.begin(), notes.end());
        notes.erase (std::unique (notes.begin(), notes.end()), notes.end());
        if (notes.size() > 8) notes.resize (8);
        return notes;
    }

    void process (juce::MidiBuffer& midi, juce::MidiBuffer& out, juce::int64 blockStart, const Settings& s, MidiScheduler& sch)
    {
        out.clear();
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            const int pos = meta.samplePosition;
            const auto now = blockStart + pos;

            if (msg.isNoteOn() && msg.getChannel() != 2)   // canal 2 = accords joués au clavier d'ordinateur : déjà complets
            {
                const int in = msg.getNoteNumber();
                releaseMapping (in, now, pos, out, sch);

                const auto notes = chordFor (in, s, rng, prevChord);
                prevChord = notes;
                auto& m = map[(size_t) in];
                m.count = 0;

                for (size_t i = 0; i < notes.size(); ++i)
                {
                    double offsetMs = s.strumMs * (double) i;
                    if (s.humanize > 0.0f) offsetMs += rng.nextFloat() * s.humanize * 25.0;
                    const auto t = now + (juce::int64) (offsetMs * 0.001 * sampleRate);

                    float vel = msg.getFloatVelocity();
                    if (s.humanize > 0.0f) vel *= 1.0f + (rng.nextFloat() * 2.0f - 1.0f) * 0.18f * s.humanize;
                    const auto on = juce::MidiMessage::noteOn (msg.getChannel(), notes[i], juce::jlimit (0.05f, 1.0f, vel));

                    if (t == now) out.addEvent (on, pos);
                    else          sch.add (t, on);

                    m.notes[(size_t) m.count] = notes[i];
                    m.onTime[(size_t) m.count] = t;
                    ++m.count;
                }

                lastCount.store (0);
                for (size_t i = 0; i < notes.size(); ++i) lastNotes[i].store (notes[i]);
                lastCount.store ((int) notes.size());
                lastVersion.fetch_add (1);
            }
            else if (msg.isNoteOff() && msg.getChannel() != 2)
            {
                releaseMapping (msg.getNoteNumber(), now, pos, out, sch);
            }
            else
            {
                out.addEvent (msg, pos);
            }
        }
        midi.swapWith (out);
    }

    void releaseAll (juce::MidiBuffer& midi, juce::int64 blockStart, MidiScheduler& sch)
    {
        for (int n = 0; n < 128; ++n)
            releaseMapping (n, blockStart, 0, midi, sch);
        prevChord.clear();
    }

    std::array<std::atomic<int>, 8> lastNotes {};
    std::atomic<int> lastCount { 0 }, lastVersion { 0 };

private:
    void releaseMapping (int in, juce::int64 now, int pos, juce::MidiBuffer& out, MidiScheduler& sch)
    {
        auto& m = map[(size_t) in];
        for (int i = 0; i < m.count; ++i)
        {
            const auto off = juce::MidiMessage::noteOff (1, m.notes[(size_t) i]);
            const auto t = juce::jmax (now, m.onTime[(size_t) i] + 1);
            if (t == now) out.addEvent (off, pos);
            else          sch.add (t, off);
        }
        m.count = 0;
    }

    struct Mapping { int count = 0; std::array<int, 8> notes {}; std::array<juce::int64, 8> onTime {}; };
    std::array<Mapping, 128> map {};
    std::vector<int> prevChord;
    double sampleRate = 44100.0;
    juce::Random rng;
};

//==============================================================================
struct HostTime
{
    double bpm = 120.0;
    bool playing = false;
    bool hasPpq = false;
    int num = 4;   // temps par mesure (signature de FL)
    double ppq = 0.0;
};

// Arpégiateur temps réel, synchronisé au tempo de l'hôte, avec "feeling" humain.
class Arpeggiator
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        held.reserve (128); base.reserve (256); pattern.reserve (2048); scratch.reserve (2048);
        offs.reserve (512); ons.reserve (512);
        reset();
    }

    void reset()
    {
        held.clear(); offs.clear(); ons.clear(); pattern.clear();
        physicalDown = 0; running = false; patternPos = 0; stepCounter = 0; dirty = true;
        lastFiredK = std::numeric_limits<juce::int64>::min();
        wasPlaying = false;
        currentStep.store (-1);
    }

    // Envoie les note-off en attente (quand on désactive l'arpège)
    void flushAll (juce::MidiBuffer& out)
    {
        for (auto& o : offs) out.addEvent (juce::MidiMessage::noteOff (1, o.note), 0);
        offs.clear();
        ons.clear();
        held.clear();
        running = false;
        currentStep.store (-1);
    }

    void process (const juce::MidiBuffer& in, juce::MidiBuffer& out, int n, juce::int64 blockStart,
                  const arp::Settings& s, const HostTime& t)
    {
        settings = &s;
        blockAbs = blockStart;
        stepBeats = arp::rateBeats (s.rate);
        samplesPerBeat = 60.0 / juce::jmax (20.0, t.bpm) * sampleRate;
        usePpq = t.playing && t.hasPpq;
        ppqStart = t.ppq;
        ppqPerSample = 1.0 / samplesPerBeat;
        {
            // réglages changés (ex. bouton hasard) : le motif est reconstruit tout de suite
            const int sig = s.mode + 16 * (s.octaves + 8 * (s.octDir + 8 * (s.repeat + 8 * (s.source + 4 * (s.sliceCount + 32 * (s.key + 16 * s.scaleIdx))))));
            if (sig != lastSig) { lastSig = sig; dirty = true; }
        }

        // Correction : à chaque (re)démarrage du transport ou retour en arrière,
        // on repart de la position actuelle (sinon l'arpège restait muet après un stop/play).
        if (usePpq)
        {
            if (! wasPlaying || ppqStart + 1.0e-4 < lastPpqEnd)
                lastFiredK = (juce::int64) std::ceil (ppqStart / stepBeats - 1.0e-6) - 1;
        }
        else
        {
            lastFiredK = std::numeric_limits<juce::int64>::min();
        }
        wasPlaying = usePpq;

        if (! s.latch && physicalDown == 0 && ! held.empty())
        {
            held.clear();
            dirty = true;
        }

        int pos = 0;
        for (const auto meta : in)
        {
            const auto msg = meta.getMessage();
            const int es = meta.samplePosition;
            advance (pos, es, out);
            pos = es;

            if (msg.isNoteOn())
            {
                if (s.latch && physicalDown == 0) held.clear();
                ++physicalDown;
                const int note = msg.getNoteNumber();
                bool found = false;
                for (auto& h : held) if (h.note == note) found = true;
                if (! found) held.push_back ({ note, msg.getFloatVelocity() });
                dirty = true;
                if (! running) start (es);
                else if (s.restart) patternPos = 0;   // option : chaque nouvelle note relance le motif depuis le début
            }
            else if (msg.isNoteOff())
            {
                physicalDown = juce::jmax (0, physicalDown - 1);
                if (! s.latch)
                {
                    const int note = msg.getNoteNumber();
                    held.erase (std::remove_if (held.begin(), held.end(), [note] (const Held& h) { return h.note == note; }), held.end());
                    dirty = true;
                }
            }
            else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            {
                held.clear(); physicalDown = 0; dirty = true;
                out.addEvent (msg, es);
            }
            else
            {
                out.addEvent (msg, es);
            }

            if (held.empty()) { running = false; currentStep.store (-1); }
        }
        advance (pos, n, out);

        lastPpqEnd = ppqStart + n * ppqPerSample;
    }

    std::atomic<int> currentStep { -1 };

    const std::vector<int>& getBase() const { return base; }
    bool isRunning() const { return running && ! held.empty(); }

private:
    struct Held { int note; float vel; };
    struct Off  { juce::int64 time; int note; };
    struct On   { juce::int64 time; int note; float vel; juce::int64 length; };

    void start (int es)
    {
        running = true;
        patternPos = 0;
        stepCounter = 0;
        nextStepAbs = (double) (blockAbs + es);
        if (usePpq)
        {
            // si la note tombe pile sur la grille, le 1er pas part tout de suite
            const double p = ppqStart + es * ppqPerSample;
            lastFiredK = (juce::int64) std::ceil (p / stepBeats - 0.02) - 1;
        }
    }

    void rebuild()
    {
        const auto& s = *settings;
        base.clear();
        if (s.source == 2 && ! held.empty())   // tranches du slicer : C5, C#5, D5… = tranche 1, 2, 3…
        {
            for (int i = 0; i < juce::jlimit (1, 16, s.sliceCount); ++i) base.push_back (60 + i);
        }
        else if (s.source == 1 && ! held.empty())
        {
            int low = 127;
            for (auto& h : held) low = juce::jmin (low, h.note);
            const auto& sc = theory::scaleAt (s.scaleIdx);
            const int root = theory::snapToScale (low, s.key, sc);
            for (int i = 0, note = root; i < (int) sc.steps.size() && note < 128; ++note)
                if (theory::inScale (note, s.key, sc)) { base.push_back (note); ++i; }
        }
        else
        {
            for (auto& h : held) base.push_back (h.note);
        }

        centre = 60;
        if (! base.empty())
        {
            int sum = 0;
            for (int b : base) sum += b;
            centre = sum / (int) base.size();
        }

        arp::buildPattern (base, s.mode, s.octaves, s.octDir, s.repeat, pattern, scratch);
        dirty = false;
    }

    void flush (juce::int64 upTo, int maxPos, juce::MidiBuffer& out)
    {
        for (auto& o : offs)
            if (o.time <= upTo)
            {
                out.addEvent (juce::MidiMessage::noteOff (1, o.note), (int) juce::jlimit<juce::int64> (0, maxPos, o.time - blockAbs));
                o.time = -1;
            }
        offs.erase (std::remove_if (offs.begin(), offs.end(), [] (const Off& o) { return o.time < 0; }), offs.end());

        for (auto& on : ons)
        {
            if (on.time > upTo) continue;
            const int smp = (int) juce::jlimit<juce::int64> (0, maxPos, on.time - blockAbs);

            for (auto& o : offs)
                if (o.note == on.note) { out.addEvent (juce::MidiMessage::noteOff (1, o.note), smp); o.time = -1; }
            offs.erase (std::remove_if (offs.begin(), offs.end(), [] (const Off& o) { return o.time < 0; }), offs.end());

            out.addEvent (juce::MidiMessage::noteOn (1, on.note, on.vel), smp);
            if (offs.size() < offs.capacity()) offs.push_back ({ on.time + on.length, on.note });
            on.time = -1;
        }
        ons.erase (std::remove_if (ons.begin(), ons.end(), [] (const On& o) { return o.time < 0; }), ons.end());
    }

    void advance (int from, int to, juce::MidiBuffer& out)
    {
        if (to > from && running && ! held.empty())
        {
            const auto& s = *settings;
            const double sw = juce::jlimit (0.0f, 1.0f, s.swing) * 0.33;

            if (usePpq)
            {
                const double pFrom = ppqStart + from * ppqPerSample;
                const double pTo   = ppqStart + to * ppqPerSample;
                const auto kLo = (juce::int64) std::floor (pFrom / stepBeats) - 1;
                const auto kHi = (juce::int64) std::ceil (pTo / stepBeats) + 1;

                for (auto k = kLo; k <= kHi; ++k)
                {
                    if (k <= lastFiredK) continue;
                    const double tk = (double) k * stepBeats + ((k & 1) ? sw * stepBeats : 0.0);
                    const int smp = (int) std::floor ((tk - ppqStart) / ppqPerSample + 0.5);
                    if (smp >= to) continue;
                    fireStep (juce::jmax (from, smp), k, out);
                    lastFiredK = k;
                }
            }
            else
            {
                const double spStep = stepBeats * samplesPerBeat;
                while (nextStepAbs < (double) (blockAbs + to))
                {
                    const int smp = juce::jmax (from, (int) (nextStepAbs - (double) blockAbs));
                    fireStep (smp, stepCounter, out);
                    nextStepAbs += spStep * ((stepCounter & 1) ? (1.0 - sw) : (1.0 + sw));
                    ++stepCounter;
                }
            }
        }
        else if (! usePpq && to > from)
        {
            nextStepAbs = (double) (blockAbs + to);
        }

        flush (blockAbs + juce::jmax (from, to - 1), juce::jmax (0, to - 1), out);
    }

    void fireStep (int smp, juce::int64 k, juce::MidiBuffer&)
    {
        const auto& s = *settings;
        const int numSteps = juce::jlimit (1, 16, s.numSteps);
        const int rot = juce::jlimit (0, 15, s.rotate);   // décalage du motif : il démarre plus loin
        const int idx = (int) ((((k + rot) % numSteps) + numSteps) % numSteps);
        currentStep.store (idx);

        const float level = s.steps[(size_t) idx];
        if (level <= 0.001f) return;

        if (dirty) rebuild();
        if (pattern.empty()) return;

        // probabilité : le pas peut sauter (la suite des notes avance quand même)
        if (s.prob < 0.999f && rng.nextFloat() >= juce::jlimit (0.0f, 1.0f, s.prob)) { ++patternPos; return; }

        const auto feel = arp::decideStep (rng, s, k);

        float vel = 0.0f;
        for (auto& h : held) vel += h.vel;
        vel = (s.velMode == 1 || held.empty()) ? 0.8f : vel / (float) held.size();
        {
            // accent des temps : le pas qui tombe sur un temps est appuyé, les autres un peu plus doux
            const double beatPos = (double) k * stepBeats;
            const bool onBeat = std::abs (beatPos - std::round (beatPos)) < 1.0e-6;
            const float a = juce::jlimit (0.0f, 1.0f, s.accent);
            const float accentMul = onBeat ? 1.0f + 0.35f * a : 1.0f - 0.3f * a;
            // rampe de vélocité : crescendo (+) ou decrescendo (−) sur la longueur du motif
            const float phase = numSteps > 1 ? (float) idx / (float) (numSteps - 1) : 0.0f;
            const float rampMul = 1.0f + juce::jlimit (-1.0f, 1.0f, s.velRamp) * (phase - 0.5f) * 1.2f;
            vel *= accentMul * rampMul;
        }
        vel = juce::jlimit (0.05f, 1.0f, vel * (0.3f + 0.7f * level) * feel.vel);

        const double stepSamples = stepBeats * samplesPerBeat;
        const auto t0 = blockAbs + smp + (juce::int64) (feel.delayBeats * samplesPerBeat);
        const auto len = juce::jmax ((juce::int64) 1, (juce::int64) (stepSamples * s.gate * feel.gate));

        int entry;
        if (s.mode == arp::Random)
        {
            int r = rng.nextInt ((int) pattern.size());
            if (pattern.size() > 1 && r == lastRandom) r = (r + 1) % (int) pattern.size();
            lastRandom = r;
            entry = pattern[(size_t) r];
        }
        else
        {
            entry = pattern[(size_t) (patternPos % (int) pattern.size())];
        }
        ++patternPos;

        if (feel.rest) return;

        auto play = [&] (int note, juce::int64 t, float v, juce::int64 l)
        {
            note += juce::jlimit (-24, 24, s.transpose);   // transpo de l'arpège seul
            if (s.snap) note = theory::snapToScale (note, s.key, theory::scaleAt (s.scaleIdx));
            if (note < 0 || note > 127) return;
            if (ons.size() < ons.capacity()) ons.push_back ({ t, note, v, l });
        };

        // basse sur le 1er pas : la note la plus grave, une octave dessous, à chaque début de motif
        if (s.bass && idx == 0 && ! base.empty())
        {
            int low = 127;
            for (int b : base) low = juce::jmin (low, b);
            play (low - 12, t0, juce::jlimit (0.05f, 1.0f, vel * 1.05f), juce::jmax (len, (juce::int64) (stepSamples * 0.9)));
        }

        if (entry == arp::chordMarker)
        {
            for (int b : base) play (b, t0, vel, len);
            return;
        }

        const int note = arp::applyFeel (entry, feel, s, centre);
        // ratchet : le pas est rejoué 2 à 4 fois très vite (roulement)
        if (s.ratchet > 0.0f && rng.nextFloat() < juce::jlimit (0.0f, 1.0f, s.ratchet))
        {
            const int hits = juce::jlimit (2, 4, s.ratchetN);
            const auto sub = (juce::int64) (stepSamples / hits);
            for (int i = 0; i < hits; ++i)
                play (note, t0 + sub * i, vel * (1.0f - 0.12f * (float) i), juce::jmax ((juce::int64) 1, juce::jmin (len, (juce::int64) (sub * 0.8))));
        }
        else if (feel.ratchet)
        {
            const auto half = (juce::int64) (stepSamples * 0.5);
            play (note, t0, vel, juce::jmin (len, (juce::int64) (half * 0.8)));
            play (note, t0 + half, vel * 0.75f, juce::jmin (len, (juce::int64) (half * 0.8)));
        }
        else
        {
            play (note, t0, vel, len);
        }
    }

    const arp::Settings* settings = nullptr;
    std::vector<Held> held;
    std::vector<int> base, pattern, scratch;
    std::vector<Off> offs;
    std::vector<On> ons;
    double sampleRate = 44100.0, stepBeats = 0.25, samplesPerBeat = 22050.0, ppqStart = 0.0, ppqPerSample = 0.0,
           nextStepAbs = 0.0, lastPpqEnd = 0.0;
    juce::int64 blockAbs = 0, stepCounter = 0, lastFiredK = 0;
    int physicalDown = 0, patternPos = 0, lastRandom = -1, centre = 60;
    bool running = false, usePpq = false, dirty = true, wasPlaying = false;
    int lastSig = -1;
    juce::Random rng;
};
//==============================================================================
// "Cut" façon FL Studio : quand un nouvel accord (ou une note) commence, ce qui sonnait avant s'arrête.
class CutFilter
{
public:
    void prepare (double sr) { sampleRate = sr; for (auto& a : on) a = { false, 0, 1, 0 }; }

    void process (juce::MidiBuffer& midi, juce::MidiBuffer& out, juce::int64 blockStart)
    {
        out.clear();
        const auto together = (juce::int64) (0.035 * sampleRate);   // notes d'un même accord (strum compris)
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            const int pos = meta.samplePosition;
            const auto now = blockStart + pos;
            if (msg.isNoteOn())
            {
                // seul un nouvel ACCORD coupe le précédent : on attend sa 2e note (les notes jouées une par une ne se coupent plus)
                bool startsGroup = false;
                if (now - groupStart > together) { prevGroupSize = groupSize; groupStart = now; groupSize = 0; ++groupId; startsGroup = true; }
                ++groupSize;
                // coupe si le nouveau groupe est un accord, ou si ce qui sonnait était un accord (pas entre notes seules)
                if (groupSize == 2 || (startsGroup && prevGroupSize >= 2))
                    for (int n = 0; n < 128; ++n)
                    {
                        auto& a = on[(size_t) n];
                        if (a.active && a.group != groupId && n != msg.getNoteNumber())
                        {
                            out.addEvent (juce::MidiMessage::noteOff (a.channel, n), pos);
                            a.active = false;
                        }
                    }
                on[(size_t) msg.getNoteNumber()] = { true, now, msg.getChannel(), groupId };
                out.addEvent (msg, pos);
            }
            else if (msg.isNoteOff())
            {
                auto& a = on[(size_t) msg.getNoteNumber()];
                if (a.active) { a.active = false; out.addEvent (msg, pos); }   // déjà coupée sinon
            }
            else out.addEvent (msg, pos);
        }
        midi.swapWith (out);
    }

private:
    struct Active { bool active; juce::int64 time; int channel; int group; };
    std::array<Active, 128> on {};
    juce::int64 groupStart = -(1 << 30);
    int groupSize = 0, groupId = 0, prevGroupSize = 0;
    double sampleRate = 44100.0;
};

//==============================================================================
// Répartit les notes entre les couches (canal MIDI 1..4 = une couche, 16 = toutes)
class LayerRouter
{
public:
    enum Mode { Stack = 0, PerNote, Alternate, Register, RandomLayer };

    static juce::StringArray modeNames()
    {
        return { fr ("Superposées (toutes ensemble)"), fr ("Une note = une couche (accords / arpèges)"),
                 fr ("En alternance"), fr ("Par registre (grave → aigu)"), fr ("Aléatoire") };
    }

    void prepare (double sr) { sampleRate = sr; chanOf.fill (allLayersChannel); recent.clear(); recent.reserve (64); }

    void process (juce::MidiBuffer& midi, juce::MidiBuffer& out, juce::int64 blockStart, int mode, bool highFirst,
                  const std::array<bool, maxLayers>& active, const std::vector<int>* context)
    {
        out.clear();
        std::array<int, maxLayers> act {};
        int nAct = 0;
        for (int i = 0; i < maxLayers; ++i) if (active[(size_t) i]) act[(size_t) nAct++] = i;
        if (highFirst) std::reverse (act.begin(), act.begin() + nAct);

        // notes de la même "frappe" (accord) pour le mode une note = une couche
        int groupNotes[128]; int groupPos[128]; int groupCount = 0;
        for (const auto meta : midi)
            if (meta.getMessage().isNoteOn() && groupCount < 128)
            {
                groupNotes[groupCount] = meta.getMessage().getNoteNumber();
                groupPos[groupCount++] = meta.samplePosition;
            }

        const auto window = (juce::int64) (0.06 * sampleRate);
        recent.erase (std::remove_if (recent.begin(), recent.end(), [&] (const Recent& r) { return blockStart - r.time > window; }), recent.end());

        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            const int pos = meta.samplePosition;

            if (msg.isNoteOn())
            {
                const int note = msg.getNoteNumber();
                int ch = allLayersChannel;

                if (mode != Stack && nAct > 0)
                {
                    int idx = 0;
                    if (mode == PerNote)
                    {
                        int rank = 0;
                        if (context != nullptr && ! context->empty())
                        {
                            std::vector<int>& sorted = scratch;
                            sorted.assign (context->begin(), context->end());
                            std::sort (sorted.begin(), sorted.end());
                            int best = 0, bestDist = 1000;
                            for (int i = 0; i < (int) sorted.size(); ++i)
                            {
                                const int d = sorted[(size_t) i] == note ? 0
                                            : (theory::mod12 (sorted[(size_t) i] - note) == 0 ? 1 : 100 + std::abs (sorted[(size_t) i] - note));
                                if (d < bestDist) { bestDist = d; best = i; }
                            }
                            rank = best;
                        }
                        else
                        {
                            for (int i = 0; i < groupCount; ++i)
                                if (std::abs (groupPos[i] - pos) <= 64 && groupNotes[i] < note) ++rank;
                            for (auto& r : recent)
                                if (r.note < note && blockStart + pos - r.time <= window) ++rank;
                        }
                        idx = rank % nAct;
                    }
                    else if (mode == Alternate)   idx = (counter++) % nAct;
                    else if (mode == Register)    idx = juce::jlimit (0, nAct - 1, (note - 40) * nAct / 45);
                    else
                    {
                        idx = rng.nextInt (nAct);
                        if (nAct > 1 && idx == lastRandom) idx = (idx + 1) % nAct;
                        lastRandom = idx;
                    }
                    ch = act[(size_t) idx] + 1;
                }

                if (pendingRecent < 128) { pendingNotes[pendingRecent] = note; pendingTimes[pendingRecent++] = blockStart + pos; }
                chanOf[(size_t) note] = ch;
                out.addEvent (juce::MidiMessage::noteOn (ch, note, msg.getFloatVelocity()), pos);
            }
            else if (msg.isNoteOff())
            {
                const int note = msg.getNoteNumber();
                out.addEvent (juce::MidiMessage::noteOff (chanOf[(size_t) note], note), pos);
            }
            else if (msg.isPitchWheel() || msg.isController())
            {
                for (int ch = 1; ch <= allLayersChannel; ch = (ch == maxLayers ? allLayersChannel : ch + 1))
                {
                    auto m = msg;
                    m.setChannel (ch);
                    out.addEvent (m, pos);
                }
            }
            else
            {
                out.addEvent (msg, pos);
            }
        }
        for (int i = 0; i < pendingRecent; ++i) if (recent.size() < recent.capacity()) recent.push_back ({ pendingTimes[i], pendingNotes[i] });
        pendingRecent = 0;
        midi.swapWith (out);
    }

private:
    int pendingNotes[128] {}; juce::int64 pendingTimes[128] {}; int pendingRecent = 0;
    struct Recent { juce::int64 time; int note; };
    std::array<int, 128> chanOf {};
    std::vector<Recent> recent;
    std::vector<int> scratch;
    double sampleRate = 44100.0;
    int counter = 0, lastRandom = -1;
    juce::Random rng;
};
//==============================================================================
// Pré-écoute d'une séquence (guide MIDI) dans le plugin, en boucle.
class PreviewPlayer
{
public:
    struct Event { double beat; juce::MidiMessage msg; };
    struct Sequence { std::vector<Event> events; double lengthBeats = 4.0; };

    // Thread message
    // keepPosition : la lecture continue au même endroit (modifier un accord ne relance pas depuis le début)
    void setSequence (std::unique_ptr<Sequence> s, bool keepPosition = false)
    {
        const juce::SpinLock::ScopedLockType l (lock);
        retired.reset();
        pending = std::move (s);
        keepPending = keepPosition;
        hasPending.store (true);
    }

    void stop() { setSequence (nullptr); }
    void seek (double beat) { seekTo.store (juce::jmax (0.0, beat)); }   // thread message : la lecture saute à cet endroit
    bool isPlaying() const { return playing.load(); }
    double positionBeats() const { return pos; }   // thread audio
    double lengthBeats() const { return active != nullptr ? active->lengthBeats : 0.0; }   // thread audio
    float getProgress() const { return progress.load(); }

    // Thread audio
    void render (juce::MidiBuffer& out, int n, double bpm, double sr)
    {
        if (hasPending.load() && lock.tryEnter())
        {
            if (retired == nullptr)
            {
                for (int i = 0; i < 128; ++i)
                    if (on[(size_t) i]) { out.addEvent (juce::MidiMessage::noteOff (1, i), 0); on[(size_t) i] = false; }
                retired = std::move (active);
                active  = std::move (pending);
                hasPending.store (false);
                if (keepPending && active != nullptr && active->lengthBeats > 0.0)
                {
                    pos = std::fmod (pos, active->lengthBeats);
                    std::array<bool, 128> should {};   // notes qui devraient sonner à cet endroit
                    for (auto& e : active->events)
                    {
                        if (e.beat >= pos) break;
                        if (e.msg.isNoteOn()) should[(size_t) e.msg.getNoteNumber()] = true;
                        else if (e.msg.isNoteOff()) should[(size_t) e.msg.getNoteNumber()] = false;
                    }
                    for (int k = 0; k < 128; ++k)
                        if (should[(size_t) k]) { out.addEvent (juce::MidiMessage::noteOn (1, k, 0.75f), 0); on[(size_t) k] = true; }
                }
                else pos = 0.0;
                keepPending = false;
            }
            lock.exit();
        }

        if (active == nullptr || active->events.empty() || active->lengthBeats <= 0.0)
        {
            playing.store (false);
            return;
        }
        playing.store (true);
        if (const double sk = seekTo.exchange (-1.0); sk >= 0.0)
        {
            // saut demandé (clic sur la règle) : on coupe et on relance ce qui doit sonner à cet endroit
            for (int i = 0; i < 128; ++i) if (on[(size_t) i]) { out.addEvent (juce::MidiMessage::noteOff (1, i), 0); on[(size_t) i] = false; }
            pos = std::fmod (sk, active->lengthBeats);
            std::array<bool, 128> should {};
            for (auto& e : active->events)
            {
                if (e.beat >= pos) break;
                if (e.msg.isNoteOn()) should[(size_t) e.msg.getNoteNumber()] = true;
                else if (e.msg.isNoteOff()) should[(size_t) e.msg.getNoteNumber()] = false;
            }
            for (int k = 0; k < 128; ++k) if (should[(size_t) k]) { out.addEvent (juce::MidiMessage::noteOn (1, k, 0.75f), 0); on[(size_t) k] = true; }
        }
        renderLoop (out, n, bpm, sr);
    }

    // Lecture calée sur FL (ppq de l'hôte) : la progression du plugin joue quand on fait Play dans FL
    void renderSynced (juce::MidiBuffer& out, int n, double bpm, double sr, double hostPpq)
    {
        if (hasPending.load() && lock.tryEnter())
        {
            for (int i = 0; i < 128; ++i) if (on[(size_t) i]) { out.addEvent (juce::MidiMessage::noteOff (1, i), 0); on[(size_t) i] = false; }
            retired = std::move (active);
            active = std::move (pending);
            hasPending.store (false);
            pos = -1.0;   // on se recale ci-dessous
            lock.exit();
        }
        if (active == nullptr || active->events.empty() || active->lengthBeats <= 0.0) { playing.store (false); return; }
        const double len = active->lengthBeats;
        const double expected = std::fmod (juce::jmax (0.0, hostPpq), len);
        if (pos < 0.0 || (std::abs (expected - pos) > 1.0e-3 && std::abs (expected - pos) < len - 1.0e-3))
        {
            // saut (départ, retour en arrière, boucle de FL) : on coupe et on rejoue ce qui doit sonner ici
            for (int i = 0; i < 128; ++i) if (on[(size_t) i]) { out.addEvent (juce::MidiMessage::noteOff (1, i), 0); on[(size_t) i] = false; }
            pos = expected;
            std::array<bool, 128> should {};
            for (auto& e : active->events)
            {
                if (e.beat >= pos) break;
                if (e.msg.isNoteOn()) should[(size_t) e.msg.getNoteNumber()] = true;
                else if (e.msg.isNoteOff()) should[(size_t) e.msg.getNoteNumber()] = false;
            }
            for (int k = 0; k < 128; ++k) if (should[(size_t) k]) { out.addEvent (juce::MidiMessage::noteOn (1, k, 0.75f), 0); on[(size_t) k] = true; }
        }
        renderLoop (out, n, bpm, sr);
    }
    void stopSynced (juce::MidiBuffer& out)
    {
        for (int i = 0; i < 128; ++i) if (on[(size_t) i]) { out.addEvent (juce::MidiMessage::noteOff (1, i), 0); on[(size_t) i] = false; }
        pos = -1.0;
        playing.store (false);
    }

private:
    void renderLoop (juce::MidiBuffer& out, int n, double bpm, double sr)
    {
        playing.store (true);

        const double bps = juce::jmax (20.0, bpm) / 60.0 / sr; // beats par échantillon
        const double len = active->lengthBeats;

        for (int i = 0; i < n; )
        {
            const double start = pos;
            const int chunk = juce::jmin (n - i, (int) std::ceil ((len - pos) / bps));
            const double end = pos + chunk * bps;

            for (auto& e : active->events)
            {
                if (e.beat >= start && e.beat < end)
                {
                    const int smp = i + juce::jlimit (0, chunk - 1, (int) ((e.beat - start) / bps));
                    out.addEvent (e.msg, smp);
                    if (e.msg.isNoteOn())  on[(size_t) e.msg.getNoteNumber()] = true;
                    if (e.msg.isNoteOff()) on[(size_t) e.msg.getNoteNumber()] = false;
                }
            }

            pos = end;
            i += juce::jmax (1, chunk);
            if (pos >= len - 1.0e-9) pos = 0.0; // boucle
        }

        progress.store ((float) (pos / len));
    }

private:
    juce::SpinLock lock;
    std::unique_ptr<Sequence> pending, active, retired;
    std::atomic<bool> hasPending { false }, playing { false };
    std::atomic<double> seekTo { -1.0 };
    bool keepPending = false;
    std::atomic<float> progress { 0.0f };
    std::array<bool, 128> on {};
    double pos = 0.0;
};
