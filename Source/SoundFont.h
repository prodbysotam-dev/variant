#pragma once
// Lecteur de SoundFont 2 (.sf2, le format de "Fruity SoundFont Player" dans FL Studio)
// On lit la liste des presets, puis on extrait l'échantillon qui couvre la note visée
// (avec sa note de base, son accordage et sa boucle) pour le mettre dans une couche.
#include <juce_audio_formats/juce_audio_formats.h>
#include <vector>

namespace sf2
{
struct Preset { juce::String name; int bank = 0, number = 0, index = 0; };

struct Extracted
{
    juce::AudioBuffer<float> audio;
    double sampleRate = 44100.0;
    int rootNote = 60;
    float cents = 0.0f;      // correction fine (à mettre dans "Finesse")
    float gainDb = 0.0f;     // atténuation de la zone
    bool looped = false;
    int loopStart = -1, loopEnd = -1;   // points de boucle (mode brut, sans rallonge)
    int keyLo = 0, keyHi = 127;        // touches couvertes par cette zone
};

class File
{
public:
    bool open (const juce::File& f)
    {
        file = f;
        in = std::make_unique<juce::FileInputStream> (f);
        if (! in->openedOk()) return false;
        char id[4]; juce::uint32 size = 0;
        if (! readHeader (id, size) || std::memcmp (id, "RIFF", 4) != 0) return false;
        char form[4]; if (in->read (form, 4) != 4 || std::memcmp (form, "sfbk", 4) != 0) return false;
        const juce::int64 end = juce::jmin<juce::int64> (in->getTotalLength(), 8 + (juce::int64) size);
        while (in->getPosition() + 8 <= end)
        {
            if (! readHeader (id, size)) break;
            const auto next = in->getPosition() + size + (size & 1);
            if (std::memcmp (id, "LIST", 4) == 0)
            {
                char kind[4]; in->read (kind, 4);
                parseList (kind, in->getPosition(), in->getPosition() + size - 4);
            }
            in->setPosition (next);
        }
        if (phdr.size() < 2 || shdr.empty() || smplSize == 0) return false;
        buildPresets();
        return ! presets.empty();
    }

    const std::vector<Preset>& getPresets() const { return presets; }

    // Extrait l'échantillon du preset le plus proche de 'note' (vélocité 100)
    bool extract (int presetIndex, int note, Extracted& out)
    {
        if (presetIndex < 0 || presetIndex + 1 >= (int) phdr.size()) return false;
        Zone best; int bestDist = 1 << 30;
        const auto pz = zonesOf (phdr[(size_t) presetIndex].bag, phdr[(size_t) presetIndex + 1].bag, pbag, pgen, 41);
        for (auto& p : pz)
        {
            const int instId = p.get (41, -1);
            if (instId < 0 || instId + 1 >= (int) inst.size()) continue;
            const auto iz = zonesOf (inst[(size_t) instId].bag, inst[(size_t) instId + 1].bag, ibag, igen, 53);
            for (auto& z : iz)
            {
                const int sid = z.get (53, -1);
                if (sid < 0 || sid >= (int) shdr.size()) continue;
                const int kLo = juce::jmax (z.lo (43, 0), p.lo (43, 0)), kHi = juce::jmin (z.hi (43, 127), p.hi (43, 127));
                const int vLo = juce::jmax (z.lo (44, 0), p.lo (44, 0)), vHi = juce::jmin (z.hi (44, 127), p.hi (44, 127));
                int d = note < kLo ? kLo - note : (note > kHi ? note - kHi : 0);
                d = d * 4 + (100 < vLo ? (vLo - 100) / 8 : (100 > vHi ? (100 - vHi) / 8 : 0));
                if (d < bestDist)
                {
                    bestDist = d;
                    best = z;
                    best.presetGlobal.gens = p.gens;   // les décalages du preset s'ajoutent
                }
            }
        }
        if (bestDist == (1 << 30)) return false;
        return render (best, out);
    }

    // Toutes les zones du preset (une par plage de touches) pour jouer l'instrument comme dans FL :
    // chaque touche utilise son propre échantillon, avec les boucles de la SoundFont.
    std::vector<Extracted> extractAll (int presetIndex, int velocity = 100)
    {
        std::vector<Extracted> out;
        if (presetIndex < 0 || presetIndex + 1 >= (int) phdr.size()) return out;
        struct Cand { Zone z; int lo, hi, vlo, vhi; };
        std::vector<Cand> cands;
        const auto pz = zonesOf (phdr[(size_t) presetIndex].bag, phdr[(size_t) presetIndex + 1].bag, pbag, pgen, 41);
        for (auto& p : pz)
        {
            const int instId = p.get (41, -1);
            if (instId < 0 || instId + 1 >= (int) inst.size()) continue;
            const auto iz = zonesOf (inst[(size_t) instId].bag, inst[(size_t) instId + 1].bag, ibag, igen, 53);
            for (auto& z : iz)
            {
                const int sid = z.get (53, -1);
                if (sid < 0 || sid >= (int) shdr.size()) continue;
                Cand c { z, juce::jmax (z.lo (43, 0), p.lo (43, 0)), juce::jmin (z.hi (43, 127), p.hi (43, 127)),
                         juce::jmax (z.lo (44, 0), p.lo (44, 0)), juce::jmin (z.hi (44, 127), p.hi (44, 127)) };
                if (c.lo > c.hi) continue;
                c.z.presetGlobal.gens = p.gens;
                cands.push_back (c);
            }
        }
        // couche de vélocité la plus proche de "velocity", une seule zone par plage de touches
        auto velDist = [velocity] (const Cand& c) { return velocity < c.vlo ? c.vlo - velocity : (velocity > c.vhi ? velocity - c.vhi : 0); };
        std::stable_sort (cands.begin(), cands.end(), [&] (const Cand& a, const Cand& b) { return velDist (a) < velDist (b); });
        std::vector<std::pair<int, int>> taken;
        for (auto& c : cands)
        {
            bool dup = false;
            for (auto& t : taken) if (t.first == c.lo && t.second == c.hi) { dup = true; break; }
            if (dup) continue;
            Extracted ex;
            if (! render (c.z, ex, false)) continue;
            ex.keyLo = c.lo; ex.keyHi = c.hi;
            taken.push_back ({ c.lo, c.hi });
            out.push_back (std::move (ex));
            if (out.size() >= 128) break;
        }
        return out;
    }

    juce::File file;

private:
    struct Gen { int op = 0; juce::int16 amount = 0; };
    struct Zone
    {
        std::vector<Gen> gens;
        int get (int op, int def) const { for (auto& g : gens) if (g.op == op) return g.amount; return def; }
        int lo (int op, int def) const { for (auto& g : gens) if (g.op == op) return (juce::uint8) (g.amount & 0xff); return def; }
        int hi (int op, int def) const { for (auto& g : gens) if (g.op == op) return (juce::uint8) ((g.amount >> 8) & 0xff); return def; }
        struct P { std::vector<Gen> gens; int get (int op, int def) const { for (auto& g : gens) if (g.op == op) return g.amount; return def; } };
        P presetGlobal;
    };
    struct Hdr { juce::String name; int a = 0, b = 0, bag = 0; };
    struct Sample { juce::String name; juce::uint32 start = 0, end = 0, ls = 0, le = 0, rate = 44100; int pitch = 60, corr = 0, link = 0, type = 1; };

    bool readHeader (char* id, juce::uint32& size)
    {
        if (in->read (id, 4) != 4) return false;
        size = (juce::uint32) in->readInt();
        return true;
    }

    void parseList (const char* kind, juce::int64 from, juce::int64 to)
    {
        in->setPosition (from);
        char id[4]; juce::uint32 size = 0;
        while (in->getPosition() + 8 <= to && readHeader (id, size))
        {
            const auto pos = in->getPosition();
            if (std::memcmp (kind, "sdta", 4) == 0 && std::memcmp (id, "smpl", 4) == 0) { smplPos = pos; smplSize = size; }
            else if (std::memcmp (kind, "pdta", 4) == 0 && size < 64 * 1024 * 1024)
            {
                juce::MemoryBlock mb; in->readIntoMemoryBlock (mb, (size_t) size);
                const auto* d = static_cast<const juce::uint8*> (mb.getData());
                auto u16 = [d] (size_t o) { return (int) (d[o] | (d[o + 1] << 8)); };
                auto u32 = [d] (size_t o) { return (juce::uint32) (d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | ((juce::uint32) d[o + 3] << 24)); };
                auto name = [d] (size_t o) { char s[21] = {}; std::memcpy (s, d + o, 20); return juce::String (juce::CharPointer_UTF8 (s)).trim(); };
                const juce::String k (id, 4);
                if (k == "phdr") for (size_t o = 0; o + 38 <= size; o += 38) phdr.push_back ({ name (o), u16 (o + 20), u16 (o + 22), u16 (o + 24) });
                else if (k == "inst") for (size_t o = 0; o + 22 <= size; o += 22) inst.push_back ({ name (o), 0, 0, u16 (o + 20) });
                else if (k == "pbag") for (size_t o = 0; o + 4 <= size; o += 4) pbag.push_back (u16 (o));
                else if (k == "ibag") for (size_t o = 0; o + 4 <= size; o += 4) ibag.push_back (u16 (o));
                else if (k == "pgen") for (size_t o = 0; o + 4 <= size; o += 4) pgen.push_back ({ u16 (o), (juce::int16) u16 (o + 2) });
                else if (k == "igen") for (size_t o = 0; o + 4 <= size; o += 4) igen.push_back ({ u16 (o), (juce::int16) u16 (o + 2) });
                else if (k == "shdr") for (size_t o = 0; o + 46 <= size; o += 46)
                    shdr.push_back ({ name (o), u32 (o + 20), u32 (o + 24), u32 (o + 28), u32 (o + 32), u32 (o + 36),
                                      (int) d[o + 40], (int) (juce::int8) d[o + 41], u16 (o + 42), u16 (o + 44) });
            }
            in->setPosition (pos + size + (size & 1));
        }
    }

    void buildPresets()
    {
        for (int i = 0; i + 1 < (int) phdr.size(); ++i)
            presets.push_back ({ phdr[(size_t) i].name, phdr[(size_t) i].b, phdr[(size_t) i].a, i });
        std::stable_sort (presets.begin(), presets.end(), [] (const Preset& x, const Preset& y)
                          { return x.bank != y.bank ? x.bank < y.bank : x.number < y.number; });
    }

    // Zones d'un preset / instrument ; la première zone sans "terminal" est globale et sert de base
    static std::vector<Zone> zonesOf (int bagFrom, int bagTo, const std::vector<int>& bags, const std::vector<Gen>& gens, int terminal)
    {
        std::vector<Zone> zones; std::vector<Gen> global;
        for (int b = bagFrom; b < bagTo && b + 1 < (int) bags.size(); ++b)
        {
            Zone z;
            for (int g = bags[(size_t) b]; g < bags[(size_t) b + 1] && g < (int) gens.size(); ++g) z.gens.push_back (gens[(size_t) g]);
            const bool hasTerm = z.get (terminal, -1) >= 0;
            if (! hasTerm) { if (b == bagFrom) global = z.gens; continue; }
            for (auto& g : global) if (z.get (g.op, -99999) == -99999) z.gens.push_back (g);
            zones.push_back (z);
        }
        return zones;
    }

    void readFrames (const Sample& s, juce::uint32 from, int n, float* dest)
    {
        const juce::uint64 maxFrames = smplSize / 2;
        std::vector<juce::int16> tmp ((size_t) n, 0);
        if ((juce::uint64) from < maxFrames)
        {
            const int avail = (int) juce::jmin<juce::uint64> ((juce::uint64) n, maxFrames - from);
            in->setPosition (smplPos + (juce::int64) from * 2);
            in->read (tmp.data(), avail * 2);
        }
        for (int i = 0; i < n; ++i) dest[i] = (float) (juce::int16) juce::ByteOrder::swapIfBigEndian ((juce::uint16) tmp[(size_t) i]) / 32768.0f;
        juce::ignoreUnused (s);
    }

    bool render (const Zone& z, Extracted& out, bool extendLoop = true)
    {
        const auto& s = shdr[(size_t) z.get (53, 0)];
        auto off = [&z] (int fine, int coarse) { return (juce::int64) z.get (fine, 0) + (juce::int64) z.get (coarse, 0) * 32768; };
        const juce::int64 start = (juce::int64) s.start + off (0, 4), end = (juce::int64) s.end + off (1, 12);
        const juce::int64 ls = (juce::int64) s.ls + off (2, 45), le = (juce::int64) s.le + off (3, 50);
        const int len = (int) juce::jlimit<juce::int64> (0, (juce::int64) s.rate * 30, end - start);
        if (len < 32) return false;

        const int mode = z.get (54, 0);
        out.looped = (mode == 1 || mode == 3) && le - ls > 16 && ls >= start && le <= end;
        out.sampleRate = (double) juce::jlimit<juce::uint32> (4000, 192000, s.rate);
        const int root = z.get (58, -1) >= 0 ? z.get (58, -1) : (s.pitch <= 127 ? s.pitch : 60);
        const float tuneSemis = (float) (z.get (51, 0) + z.presetGlobal.get (51, 0))
                              + (float) (z.get (52, 0) + z.presetGlobal.get (52, 0) + s.corr) / 100.0f;
        // jouer la note "root" donne root + tune : la note de base apparente est root - tune
        const float apparent = (float) root - tuneSemis;
        out.rootNote = juce::jlimit (0, 127, juce::roundToInt (apparent));
        out.cents = ((float) out.rootNote - apparent) * 100.0f;
        out.gainDb = -(float) (z.get (48, 0) + z.presetGlobal.get (48, 0)) / 25.0f;   // centibels (atténuation adoucie)

        // un échantillon en boucle est rallongé (la boucle est faite pour se répéter sans couture)
        const int total = (out.looped && extendLoop) ? juce::jmax (len, (int) (out.sampleRate * 3.0)) : len;
        if (out.looped) { out.loopStart = (int) (ls - start); out.loopEnd = (int) juce::jmin<juce::int64> (le - start, len); }
        const bool stereo = (s.type == 2 || s.type == 4) && s.link < (int) shdr.size() && s.link != z.get (53, 0);
        out.audio.setSize (stereo ? 2 : 1, total);
        out.audio.clear();
        auto fill = [&] (const Sample& smp, juce::int64 st, juce::int64 lps, juce::int64 lpe, float* dst)
        {
            std::vector<float> body ((size_t) len);
            readFrames (smp, (juce::uint32) juce::jmax<juce::int64> (0, st), len, body.data());
            if (! out.looped || ! extendLoop) { std::copy (body.begin(), body.end(), dst); return; }
            const int a = (int) (lps - start), b = (int) (lpe - start);
            const int head = juce::jmin (b, len);
            std::copy (body.begin(), body.begin() + head, dst);
            for (int i = head; i < total; ++i) dst[i] = body[(size_t) (a + (i - head) % juce::jmax (1, b - a))];
        };
        const Sample* other = stereo ? &shdr[(size_t) s.link] : nullptr;
        const juce::int64 delta = other != nullptr ? (juce::int64) other->start - (juce::int64) s.start : 0;
        if (stereo)
        {
            const int me = s.type == 4 ? 0 : 1;   // 4 = gauche, 2 = droite
            fill (s, start, ls, le, out.audio.getWritePointer (me));
            fill (*other, start + delta, ls + delta, le + delta, out.audio.getWritePointer (1 - me));
        }
        else fill (s, start, ls, le, out.audio.getWritePointer (0));

        // petite descente à la fin d'une boucle pour ne pas couper sec
        if (out.looped && extendLoop)
        {
            const int fade = juce::jmin (total / 3, (int) (out.sampleRate * 0.6));
            for (int ch = 0; ch < out.audio.getNumChannels(); ++ch)
                out.audio.applyGainRamp (ch, total - fade, fade, 1.0f, 0.0f);
        }
        return out.audio.getMagnitude (0, total) > 1.0e-5f;
    }

    std::unique_ptr<juce::FileInputStream> in;
    juce::int64 smplPos = 0; juce::uint32 smplSize = 0;
    std::vector<Hdr> phdr, inst;
    std::vector<int> pbag, ibag;
    std::vector<Gen> pgen, igen;
    std::vector<Sample> shdr;
    std::vector<Preset> presets;
};

}
