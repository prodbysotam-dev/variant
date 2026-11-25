#pragma once
// Slicer : découpe automatique d'un sample en tranches (transitoires ou grille),
// chaque tranche est jouée par une note : C5 (60) = tranche 1, C#5 = tranche 2…
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <algorithm>
#include <cmath>
#include <memory>
#include <atomic>
#include <array>

namespace slicer
{
    // note qui joue le 1er chop (réglage « 1er chop » : C0 = la note la plus basse du piano roll, puis chaque note au-dessus = chop suivant)
    inline std::atomic<int>& baseRef() { static std::atomic<int> b { 0 }; return b; }
    inline int base() { return baseRef().load(); }
    constexpr int maxSlices = 64;
    constexpr int padsPerBank = 16;

    inline int sliceForNote (int note, int count)
    {
        if (count <= 0) return -1;
        const int d = note - base();
        return ((d % count) + count) % count;
    }

    inline juce::String noteLabel (int i)   // nom FL Studio de la note qui joue la tranche i
    {
        static const char* n[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int note = base() + i;
        return juce::String (n[note % 12]) + juce::String (note / 12 - 1 + 1);   // FL : 60 = C5
    }

    // point de passage par zéro le plus proche (en arrière en priorité) pour éviter les clics
    inline int snapToZero (const float* x, int len, int pos, int maxBack)
    {
        pos = juce::jlimit (0, juce::jmax (0, len - 1), pos);
        for (int k = 0; k < maxBack; ++k)
        {
            const int a = pos - k;
            if (a <= 0) return 0;
            if ((x[a - 1] <= 0.0f && x[a] >= 0.0f) || (x[a - 1] >= 0.0f && x[a] <= 0.0f)) return a;
        }
        return pos;
    }

    inline std::vector<float> toMono (const juce::AudioBuffer<float>& b)
    {
        const int len = b.getNumSamples();
        std::vector<float> m ((size_t) len, 0.0f);
        const int ch = juce::jmax (1, b.getNumChannels());
        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            const float* d = b.getReadPointer (c);
            for (int i = 0; i < len; ++i) m[(size_t) i] += d[i] / (float) ch;
        }
        return m;
    }

    // Pas de micro-tranches : chaque tranche dure au moins minSeconds (sinon elle est fusionnée avec la précédente)
    inline std::vector<int> enforceMinLength (std::vector<int> starts, int len, double sr, double minSeconds = 0.2)
    {
        std::sort (starts.begin(), starts.end());
        const int minS = (int) (minSeconds * sr);
        std::vector<int> out;
        for (int s : starts)
            if (out.empty() || s - out.back() >= minS) out.push_back (s);
        while (out.size() > 1 && len - out.back() < minS) out.pop_back();
        if (out.empty() || out.front() != 0) out.insert (out.begin(), 0);
        return out;
    }

    // Grille : tranches égales
    inline std::vector<int> gridSlices (const juce::AudioBuffer<float>& b, int count)
    {
        std::vector<int> out;
        const int len = b.getNumSamples();
        if (len <= 0) return out;
        count = juce::jlimit (1, maxSlices, count);
        const auto mono = toMono (b);
        for (int i = 0; i < count; ++i)
        {
            const int p = (int) ((juce::int64) len * i / count);
            out.push_back (i == 0 ? 0 : snapToZero (mono.data(), len, p, 400));
        }
        return out;
    }

    // Au tempo : une tranche par temps (ou demi-temps / quart de temps si le sample est court),
    // en partant de la première attaque
    inline std::vector<int> tempoSlices (const juce::AudioBuffer<float>& b, double sr, double bpm, int count)
    {
        std::vector<int> out;
        const int len = b.getNumSamples();
        if (len <= 0 || bpm <= 0.0) return gridSlices (b, count);
        count = juce::jlimit (1, maxSlices, count);
        const auto mono = toMono (b);
        float peak = 0.0f;
        for (float v : mono) peak = juce::jmax (peak, std::abs (v));
        int first = 0;
        while (first < len && std::abs (mono[(size_t) first]) < peak * 0.05f) ++first;
        if (first >= len) first = 0;
        double step = 60.0 / bpm * sr;
        while (step > sr * 0.4 && (double) (len - first) / step < (double) count * 0.9) step *= 0.5;
        out.push_back (0);
        for (int k = 1; k < count; ++k)
        {
            const int p = first + (int) std::round (k * step);
            if (p >= len - 256) break;
            out.push_back (snapToZero (mono.data(), len, p, 300));
        }
        return out;
    }

    // Transitoires (v2.8) : flux spectral (FFT 1024, pas de 2,9 ms) pondéré par bande — les graves (kick) et les aigus (snare, hats)
    // comptent tous — seuil adaptatif (médiane locale), puis chaque coupe recule au DÉBUT de l'attaque (creux d'énergie juste avant)
    // et se cale sur un passage par zéro : plus de coupes au milieu d'un coup ni de pré-écho.
    // sens 0..1 : plus haut = plus de coupes. maxCount = nombre maximum de tranches.
    inline std::vector<int> detectTransients (const juce::AudioBuffer<float>& b, double sr, float sens, int maxCount, double bpm = 0.0, bool fine = false)
    {
        std::vector<int> out { 0 };
        const int len = b.getNumSamples();
        if (len < 4096 || sr <= 0.0) return out;
        maxCount = juce::jlimit (1, maxSlices, maxCount);
        const auto mono = toMono (b);
        constexpr int order = 10, N = 1 << order;
        const int hop = juce::jmax (64, (int) std::round (sr / 344.0));
        const int frames = (len - N) / hop;
        if (frames < 8) return out;
        juce::dsp::FFT fft (order);
        std::vector<float> win ((size_t) N), buf ((size_t) N * 2), prev ((size_t) N / 2 + 1, 0.0f), flux ((size_t) frames, 0.0f), energy ((size_t) frames, 0.0f);
        for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N);
        const double binHz = sr / N;
        for (int f = 0; f < frames; ++f)
        {
            const int s0 = f * hop;
            for (int i = 0; i < N; ++i) buf[(size_t) i] = mono[(size_t) (s0 + i)] * win[(size_t) i];
            std::fill (buf.begin() + N, buf.end(), 0.0f);
            fft.performFrequencyOnlyForwardTransform (buf.data(), true);
            float fl = 0.0f, e = 0.0f;
            for (int k = 1; k <= N / 2; ++k)
            {
                const float m = std::log1p (buf[(size_t) k] * 20.0f);
                const double hz = k * binHz;
                // poids par bande : coups de grave (40-150 Hz) et claquements (2-10 kHz) mis en avant
                const float w = hz < 150.0 ? 1.6f : (hz < 2000.0 ? 0.8f : (hz < 10000.0 ? 1.3f : 0.6f));
                fl += juce::jmax (0.0f, m - prev[(size_t) k]) * w;
                prev[(size_t) k] = m;
                e += buf[(size_t) k] * buf[(size_t) k];
            }
            flux[(size_t) f] = fl;
            energy[(size_t) f] = e;
        }
        float peakE = 0.0f; for (float e : energy) peakE = juce::jmax (peakE, e);
        if (peakE <= 1.0e-12f) return out;

        // seuil adaptatif : médiane locale (±0,15 s) x facteur + marge réglée par la sensibilité
        const int halfW = juce::jmax (4, (int) (0.15 * sr / hop));
        const float k = 1.3f + (1.0f - juce::jlimit (0.0f, 1.0f, sens)) * 1.6f;
        float meanFlux = 0.0f; for (float v : flux) meanFlux += v; meanFlux /= (float) frames;
        const float floorV = meanFlux * (0.25f + (1.0f - sens) * 0.6f);
        struct Cand { int frame; float strength; };
        std::vector<Cand> cands;
        std::vector<float> win2;
        const int minGap = juce::jmax (1, (int) ((fine ? 0.05 : 0.10) * sr / hop));
        for (int f = 2; f < frames - 2; ++f)
        {
            const float v = flux[(size_t) f];
            if (v < flux[(size_t) f - 1] || v < flux[(size_t) f + 1] || v < flux[(size_t) f - 2] || v < flux[(size_t) f + 2]) continue;
            if (energy[(size_t) f + 2] < peakE * 1.0e-5f) continue;   // bruit de fond
            win2.clear();
            for (int j = juce::jmax (0, f - halfW); j < juce::jmin (frames, f + halfW); ++j) win2.push_back (flux[(size_t) j]);
            std::nth_element (win2.begin(), win2.begin() + (long) win2.size() / 2, win2.end());
            const float med = win2[win2.size() / 2];
            if (v <= med * k + floorV) continue;
            if (! cands.empty() && f - cands.back().frame < minGap) { if (v > cands.back().strength) cands.back() = { f, v }; continue; }
            cands.push_back ({ f, v });
        }

        // tempo connu : les coups qui tombent sur la grille (croches) passent devant -> des tranches cohérentes entre elles
        int firstHit = -1;
        for (auto& c : cands) if (firstHit < 0 || c.frame < firstHit) firstHit = c.frame;
        if (bpm > 30.0 && firstHit >= 0)
        {
            const double eighth = 30.0 / bpm * sr / hop;
            for (auto& c : cands)
            {
                const double ph = std::fmod ((double) (c.frame - firstHit) / eighth, 1.0);
                const double d = juce::jmin (ph, 1.0 - ph);
                c.strength *= (float) (1.0 + 0.8 * juce::jmax (0.0, 1.0 - d * 4.0));
            }
        }
        std::sort (cands.begin(), cands.end(), [] (const Cand& a, const Cand& c) { return a.strength > c.strength; });

        // début réel de l'attaque : on recule jusqu'au creux d'énergie (enveloppe 1 ms) dans les 40 ms avant, puis passage par zéro
        auto attackStart = [&] (int frame)
        {
            const int centre = frame * hop + N / 2;
            const int from = juce::jmax (0, centre - N / 2 - (int) (0.010 * sr)), to = juce::jmin (len - 1, centre + N / 2);
            const int w = juce::jmax (16, (int) (0.001 * sr));
            int best = from; float bestE = 1.0e30f;
            float pk = 0.0f;
            for (int i = from; i < to; ++i) pk = juce::jmax (pk, std::abs (mono[(size_t) i]));
            // premier endroit où ça monte franchement, puis le creux juste avant
            int rise = to;
            for (int i = from; i < to; i += w / 2)
            {
                float e = 0.0f; for (int j = 0; j < w && i + j < len; ++j) e = juce::jmax (e, std::abs (mono[(size_t) (i + j)]));
                if (e > pk * 0.3f) { rise = i; break; }
            }
            for (int i = juce::jmax (from, rise - (int) (0.030 * sr)); i < rise; i += w / 4)
            {
                float e = 0.0f; for (int j = 0; j < w && i + j < len; ++j) e = juce::jmax (e, std::abs (mono[(size_t) (i + j)]));
                if (e <= bestE * 1.05f + 1.0e-6f) { bestE = juce::jmin (bestE, e); best = i; }   // le creux le plus proche de l'attaque
            }
            if (rise < to && best >= rise) best = rise;
            return snapToZero (mono.data(), len, best, (int) (0.003 * sr));
        };
        const double spread = fine ? 0.045 : 0.085;
        std::vector<int> pos;
        for (auto& c : cands)
        {
            const int p = attackStart (c.frame);
            if (p < (int) ((fine ? 0.01 : 0.03) * sr)) continue;   // trop près du début : c'est la tranche 1
            bool close = false;
            for (int q : pos) if (std::abs (q - p) < (int) (spread * sr)) { close = true; break; }
            if (close) continue;
            pos.push_back (p);
            if ((int) pos.size() >= maxCount - 1) break;
        }
        std::sort (pos.begin(), pos.end());
        for (int p : pos) out.push_back (p);
        return out;
    }

    //==========================================================================
    class SliceSound : public juce::SynthesiserSound
    {
    public:
        using Ptr = juce::ReferenceCountedObjectPtr<SliceSound>;
        bool appliesToNote (int) override    { return true; }
        bool appliesToChannel (int) override { return true; }

        std::shared_ptr<const juce::AudioBuffer<float>> data;   // partagé entre versions (déplacer un repère ne recopie pas l'audio)
        double rate = 44100.0;
        std::vector<int> starts;
        juce::String name;

        int count() const { return (int) starts.size(); }
        int endSample = -1;   // fin de la zone choisie (-1 = fin du fichier)
        std::vector<int> ends;   // fins choisies à la main (mode « Mes chops », vide = chaque tranche va jusqu'à la suivante)
        int endOf (int i) const { if (i >= 0 && i < (int) ends.size()) return juce::jmin (ends[(size_t) i], data->getNumSamples()); return i + 1 < count() ? starts[(size_t) i + 1] : (endSample > 0 ? juce::jmin (endSample, data->getNumSamples()) : data->getNumSamples()); }

        // « Volumes égalisés » : gain de chaque tranche pour qu'elles sonnent toutes aussi fort (calculé une fois, hors temps réel)
        std::vector<float> level;
        void computeLevels()
        {
            level.assign ((size_t) count(), 1.0f);
            if (data == nullptr || count() == 0) return;
            std::vector<float> rms ((size_t) count(), 0.0f);
            const int nch = data->getNumChannels();
            for (int i = 0; i < count(); ++i)
            {
                const int a = starts[(size_t) i], b = juce::jmin (endOf (i), a + (int) (rate * 0.5));   // le corps de la tranche (0,5 s max)
                if (b - a < 16) continue;
                double e = 0.0;
                for (int ch = 0; ch < nch; ++ch) { const float r = data->getRMSLevel (ch, a, b - a); e += (double) r * r; }
                rms[(size_t) i] = (float) std::sqrt (e / juce::jmax (1, nch));
            }
            std::vector<float> sorted;
            for (float r : rms) if (r > 1.0e-5f) sorted.push_back (r);
            if (sorted.empty()) return;
            std::sort (sorted.begin(), sorted.end());
            const float target = sorted[sorted.size() / 2];   // niveau médian : les tranches faibles montent, les fortes baissent
            for (int i = 0; i < count(); ++i)
                if (rms[(size_t) i] > 1.0e-5f) level[(size_t) i] = juce::jlimit (0.25f, 4.0f, target / rms[(size_t) i]);   // ±12 dB au plus
        }
    };

    // Réglages de chaque chop (clic droit sur un pad), comme sur une MPC : volume, hauteur, pan, inversé
    struct ChopSettings
    {
        static constexpr int n = maxSlices;
        std::array<std::atomic<float>, n> vol {}, pitch {}, pan {};   // dB, demi-tons, -1 (gauche) … +1 (droite)
        std::array<std::atomic<int>, n> rev {};                        // 1 = joué à l'envers
        std::atomic<int> version { 0 };

        bool isDefault (int i) const
        {
            if (i < 0 || i >= n) return true;
            return vol[(size_t) i].load() == 0.0f && pitch[(size_t) i].load() == 0.0f && pan[(size_t) i].load() == 0.0f && rev[(size_t) i].load() == 0;
        }
        void resetOne (int i)
        {
            if (i < 0 || i >= n) return;
            vol[(size_t) i] = 0.0f; pitch[(size_t) i] = 0.0f; pan[(size_t) i] = 0.0f; rev[(size_t) i] = 0;
            ++version;
        }
        void resetAll() { for (int i = 0; i < n; ++i) { vol[(size_t) i] = 0.0f; pitch[(size_t) i] = 0.0f; pan[(size_t) i] = 0.0f; rev[(size_t) i] = 0; } ++version; }

        // texte court affiché sur le pad (ex. « +2 st  -3 dB  G50  ◀ »)
        juce::String summary (int i) const
        {
            if (isDefault (i)) return {};
            juce::StringArray s;
            const float p = pitch[(size_t) i].load(), v = vol[(size_t) i].load(), pn = pan[(size_t) i].load();
            if (p != 0.0f) s.add ((p > 0 ? "+" : "") + juce::String ((int) p) + " st");
            if (v != 0.0f) s.add ((v > 0 ? "+" : "") + juce::String ((int) v) + " dB");
            if (pn != 0.0f) s.add ((pn < 0 ? "G" : "D") + juce::String (juce::roundToInt (std::abs (pn) * 100.0f)));
            if (rev[(size_t) i].load() != 0) s.add ("REV");
            return s.joinIntoString ("  ");
        }
    };

    // version étirée pré-calculée du sample entier (Stretch : tempo de FL sans changer la hauteur, sans grains qui grésillent)
    struct StretchVer { std::unique_ptr<juce::AudioBuffer<float>> buf; const juce::AudioBuffer<float>* src = nullptr; double factor = 1.0; int semis = 0; };

    struct SliceParams
    {
        std::atomic<float>* tune = nullptr;   // demi-tons
        std::atomic<float>* gain = nullptr;   // dB (volume général du plugin)
        std::atomic<float>* gate = nullptr;   // 1 = la tranche s'arrête quand on relâche la note
        std::atomic<float>* speed = nullptr;  // vitesse de lecture (calage sur le tempo de FL)
        std::atomic<float>* cents = nullptr;  // finesse (cents)
        std::atomic<float>* stretch = nullptr;   // 1 = stretch : le tempo suit FL sans changer la hauteur
        std::atomic<float>* timeRate = nullptr;  // vitesse du temps en stretch (BPM de FL / BPM du sample)
        std::atomic<float>* choke = nullptr;     // 1 = une tranche à la fois (la nouvelle coupe la précédente, comme une MPC)
        std::atomic<const StretchVer*>* stretchVer = nullptr;
        // son des chops (valeurs par défaut = comportement d'origine)
        std::atomic<float>* attack = nullptr;    // ms (1,5 ms par défaut : juste de quoi éviter le clic)
        std::atomic<float>* release = nullptr;   // ms (15 ms par défaut)
        std::atomic<float>* velSens = nullptr;   // 0 = vélocité ignorée … 1 = très sensible (0,75 par défaut)
        std::atomic<float>* tone = nullptr;      // filtre : -1 = très sombre (passe-bas), 0 = neutre, +1 = très fin (passe-haut)
        std::atomic<float>* levelOn = nullptr;   // 1 = volumes égalisés
        const ChopSettings* chops = nullptr;     // réglages de chaque chop
    };

    class SliceVoice : public juce::SynthesiserVoice
    {
    public:
        SliceVoice (const SliceParams& p, std::atomic<int>& lastSliceOut, std::atomic<float>& posOut)
            : params (p), lastSlice (lastSliceOut), playPos (posOut) {}

        bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<SliceSound*> (s) != nullptr; }

        void startNote (int note, float vel, juce::SynthesiserSound* s, int) override
        {
            sound = dynamic_cast<SliceSound*> (s);
            if (sound == nullptr || sound->count() == 0 || sound->data == nullptr) { clearCurrentNote(); return; }
            slice = sliceForNote (note, sound->count());
            uiGate = isPlayingChannel (15);   // pads, clics et clavier d'ordi : s'arrêtent toujours au relâchement
            data = sound->data.get();
            scale = 1.0;
            usingVer = false;
            const bool stretch = params.stretch != nullptr && params.stretch->load() > 0.5f;
            if (stretch && params.stretchVer != nullptr)
                if (auto* v = params.stretchVer->load(); v != nullptr && v->buf != nullptr && v->src == sound->data.get())
                {
                    const double want = 1.0 / juce::jmax (0.05, (double) (params.timeRate != nullptr ? params.timeRate->load() : 1.0f));
                    const int semis = (int) std::round (params.tune != nullptr ? params.tune->load() : 0.0f);
                    if (std::abs (v->factor - want) < 0.01 && v->semis == semis) { data = v->buf.get(); scale = v->factor; usingVer = true; }
                }
            // réglages propres à ce chop (clic droit sur le pad) + volumes égalisés
            chopPitch = 0.0; chopGain = 1.0f; panL = panR = 1.0f; reverse = false;
            if (params.chops != nullptr && slice < ChopSettings::n)
            {
                const auto& c = *params.chops;
                chopPitch = (double) c.pitch[(size_t) slice].load();
                chopGain = juce::Decibels::decibelsToGain (c.vol[(size_t) slice].load(), -60.0f);
                const float pn = juce::jlimit (-1.0f, 1.0f, c.pan[(size_t) slice].load());
                panL = pn > 0.0f ? 1.0f - pn : 1.0f;
                panR = pn < 0.0f ? 1.0f + pn : 1.0f;
                reverse = c.rev[(size_t) slice].load() != 0;
            }
            if (params.levelOn != nullptr && params.levelOn->load() > 0.5f && slice < (int) sound->level.size())
                chopGain *= sound->level[(size_t) slice];
            startPos = (double) sound->starts[(size_t) slice] * scale;
            end = juce::jmin (data->getNumSamples(), (int) std::round (sound->endOf (slice) * scale));
            pos = reverse ? (double) end - 1.0 : startPos;
            const float vs = params.velSens != nullptr ? juce::jlimit (0.0f, 1.0f, params.velSens->load()) : 0.75f;
            gain = (1.0f - vs) + vs * vel;   // 0,75 par défaut = 0,25 + 0,75 × vélocité (comme avant)
            fadeIn = 0.0f;
            releasing = false; choked = false;
            relGain = 1.0f;
            f1L = f2L = f1R = f2R = 0.0f;
            lastSlice.store (slice);
        }

        void stopNote (float, bool allowTailOff) override
        {
            if (allowTailOff) { if (uiGate || params.gate == nullptr || params.gate->load() > 0.5f) releasing = true; }
            else { sound = nullptr; clearCurrentNote(); }
        }
        void choke() { releasing = true; choked = true; }   // coupé par une nouvelle tranche (fondu très court)

        void pitchWheelMoved (int) override {}
        void controllerMoved (int, int) override {}

        void renderNextBlock (juce::AudioBuffer<float>& out, int start, int num) override
        {
            if (sound == nullptr || ! isVoiceActive() || data == nullptr) return;
            const double sr = getSampleRate();
            const double tune = params.tune != nullptr ? params.tune->load() : 0.0;
            const double cents = params.cents != nullptr ? params.cents->load() / 100.0 : 0.0;
            const bool stretch = params.stretch != nullptr && params.stretch->load() > 0.5f;
            double inc;
            if (usingVer) inc = std::pow (2.0, (tune - std::round (tune) + cents + chopPitch) / 12.0) * sound->rate / sr;   // version étirée : seulement la finesse
            else if (stretch) inc = (params.timeRate != nullptr ? (double) params.timeRate->load() : 1.0) * std::pow (2.0, (cents + chopPitch) / 12.0) * sound->rate / sr;   // en attendant la version étirée
            else inc = std::pow (2.0, (tune + cents + chopPitch) / 12.0) * sound->rate / sr * (params.speed != nullptr ? (double) params.speed->load() : 1.0);
            const float g = gain * chopGain * juce::Decibels::decibelsToGain (params.gain != nullptr ? params.gain->load() : 0.0f, -60.0f);
            const double atkMs = params.attack != nullptr ? (double) params.attack->load() : 1.5;
            const double relMs = params.release != nullptr ? (double) params.release->load() : 15.0;
            const float fadeStep = (float) (1.0 / (juce::jmax (0.0002, atkMs * 0.001) * sr));
            const float relStep  = (float) (1.0 / ((choked ? 0.006 : juce::jmax (0.001, relMs * 0.001)) * sr));
            const double tailLen = 0.004 * sound->rate * scale;
            const double dir = reverse ? -1.0 : 1.0;

            // filtre « Couleur » : à gauche passe-bas (plus sombre), à droite passe-haut (plus fin), au centre rien
            const float toneV = params.tone != nullptr ? juce::jlimit (-1.0f, 1.0f, params.tone->load()) : 0.0f;
            const bool filt = std::abs (toneV) > 0.01f;
            float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
            const float kq = 1.41421356f;   // Q = 0,707 (sans bosse)
            if (filt)
            {
                const double fc = toneV < 0.0f ? 20000.0 * std::pow (2.0, (double) toneV * 6.6)   // -1 ≈ 200 Hz
                                               : 20.0 * std::pow (2.0, (double) toneV * 6.6);      // +1 ≈ 2 kHz
                const float gg = (float) std::tan (juce::MathConstants<double>::pi * juce::jmin (fc, 0.45 * sr) / sr);
                a1 = 1.0f / (1.0f + gg * (gg + kq)); a2 = gg * a1; a3 = gg * a2;
            }
            auto svf = [&] (float x, float& ic1, float& ic2)
            {
                const float v3 = x - ic2;
                const float v1 = a1 * ic1 + a2 * v3;
                const float v2 = ic2 + a2 * ic1 + a3 * v3;
                ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
                return toneV < 0.0f ? v2 : x - kq * v1 - v2;
            };
            const auto& d = *data;
            const float* L = d.getReadPointer (0);
            const float* R = d.getReadPointer (d.getNumChannels() > 1 ? 1 : 0);
            const int len = d.getNumSamples();
            auto herm = [len] (const float* x, double p)
            {
                const int i = (int) p; const float t = (float) (p - (double) i);
                auto at = [x, len] (int k) { return (k >= 0 && k < len) ? x[k] : 0.0f; };
                const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
                const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
                return ((c3 * t + c2) * t + c1) * t + x0;
            };
            for (int n = 0; n < num; ++n)
            {
                const bool done = reverse ? pos <= startPos : pos >= (double) end;
                if (done || relGain <= 0.0f) { sound = nullptr; clearCurrentNote(); playPos.store (-1.0f); return; }
                float l = herm (L, pos), r = herm (R, pos);
                pos += inc * dir;
                fadeIn = juce::jmin (1.0f, fadeIn + fadeStep);
                if (releasing) relGain -= relStep;
                const double left = reverse ? pos - startPos : (double) end - pos;
                const float tail = left < tailLen ? (float) juce::jmax (0.0, left / tailLen) : 1.0f;
                const float e = g * fadeIn * juce::jmax (0.0f, relGain) * tail;
                if (filt) { l = svf (l, f1L, f2L); r = svf (r, f1R, f2R); }
                out.addSample (0, start + n, l * e * panL);
                if (out.getNumChannels() > 1) out.addSample (1, start + n, r * e * panR);
            }
            playPos.store ((float) (pos / scale / juce::jmax (1, sound->data->getNumSamples())));
        }

    private:
        const SliceParams& params;
        std::atomic<int>& lastSlice;
        std::atomic<float>& playPos;
        SliceSound* sound = nullptr;
        const juce::AudioBuffer<float>* data = nullptr;
        int slice = 0, end = 0;
        double pos = 0.0, startPos = 0.0, scale = 1.0, chopPitch = 0.0;
        float gain = 1.0f, fadeIn = 0.0f, relGain = 1.0f, chopGain = 1.0f, panL = 1.0f, panR = 1.0f;
        float f1L = 0.0f, f2L = 0.0f, f1R = 0.0f, f2R = 0.0f;   // mémoire du filtre
        bool releasing = false, uiGate = false, choked = false, usingVer = false, reverse = false;
    };

    // une tranche à la fois (réglable) : la nouvelle coupe la précédente, comme sur une MPC
    class SliceSynth : public juce::Synthesiser
    {
    public:
        const SliceParams* params = nullptr;
        void noteOn (int ch, int note, float vel) override
        {
            if (params != nullptr && params->choke != nullptr && params->choke->load() > 0.5f)
                for (auto* v : voices) if (v->isVoiceActive()) if (auto* sv = dynamic_cast<SliceVoice*> (v)) sv->choke();
            juce::Synthesiser::noteOn (ch, note, vel);
        }
    };
}
