#pragma once
// Interface du slicer : forme d'onde avec repères éditables + pads
#include "PluginProcessor.h"
#include "LookAndFeel.h"
#include "Components.h"

inline juce::Colour sliceColour (int i)
{
    const juce::Colour cols[] = { Palette::peach, Palette::mint, Palette::blue, Palette::lavender, Palette::pink };
    return cols[i % 5];
}

class SliceView : public juce::Component, public juce::FileDragAndDropTarget, public juce::SettableTooltipClient
{
public:
    explicit SliceView (VariantProcessor& p) : proc (p)
    {
        setTooltip (fr ("Bande du haut : glisse pour choisir la partie du sample à découper (double-clic = tout). "
                        "Clic : écouter · glisser un repère : il se cale sur les transitoires (traits orange) ou la grille, Alt = libre · double-clic : ajouter · clic droit : effacer · molette : zoom, Maj+molette : défiler"));
    }

    void tick()
    {
        const int v = proc.sliceVersion.load();
        if (v != seenVersion) { seenVersion = v; peaksKey = -1; repaint(); }
        const float pp = proc.slicePlayPos.load();
        if (pp != lastPos) { lastPos = pp; repaint(); }
    }

    // repère sélectionné : réglage fin au clavier ou avec les flèches de la carte
    int selected = -1;
    void nudgeSelected (double ms)
    {
        auto s = proc.getSliceSound();
        if (s == nullptr || selected <= 0 || selected >= s->count()) return;
        proc.sliceHistoryMark();
        proc.moveSliceMarker (selected, s->starts[(size_t) selected] + (int) (ms * 0.001 * s->rate));
        proc.sliceHistoryCommit();
    }
    void zoomToRegion()
    {
        const auto r = proc.getSliceRegion();
        viewA = r.first; viewB = r.second; peaksKey = -1; repaint();
    }
    void zoomAll() { viewA = 0; viewB = -1; peaksKey = -1; repaint(); }
    // zoom sur les tranches first..last (une banque de pads) pour bien voir ce que joue le clavier
    void zoomToSlices (int first, int last)
    {
        auto s = proc.getSliceSound();
        if (s == nullptr || s->count() == 0) return;
        first = juce::jlimit (0, s->count() - 1, first); last = juce::jlimit (first, s->count() - 1, last);
        const int a = s->starts[(size_t) first], b = s->endOf (last);
        const int pad = (b - a) / 40;
        viewA = juce::jmax (0, a - pad); viewB = juce::jmin (s->data->getNumSamples(), b + pad);
        peaksKey = -1; repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        g.setColour (over ? Palette::peach.withAlpha (0.12f) : Palette::panel2.withAlpha (0.45f));
        g.fillRoundedRectangle (area, 10.0f);

        auto s = proc.getSliceSound();
        if (s == nullptr || s->data == nullptr)
        {
            g.setColour (Palette::dim);
            g.setFont (uiFont (15.0f));
            g.drawFittedText (fr ("Glisse un sample ici (boucle, voix, break, mélodie…) : il sera découpé automatiquement"),
                              getLocalBounds().reduced (20), juce::Justification::centred, 2);
            return;
        }
        clampView (*s);
        const auto& d = *s->data;
        const float w = area.getWidth(), h = area.getHeight() - rulerH;
        const float top = rulerH;

        // bande de zone (en haut)
        const auto reg = showPending ? std::pair<int, int> { pendingA, pendingB } : proc.getSliceRegion();
        g.setColour (Palette::panel.withAlpha (0.9f));
        g.fillRect (0.0f, 0.0f, w, rulerH);
        const float ra = xOf (reg.first), rb = xOf (reg.second);
        g.setColour (Palette::peach.withAlpha (0.55f));
        g.fillRect (juce::jmax (0.0f, ra), 2.0f, juce::jmin (w, rb) - juce::jmax (0.0f, ra), rulerH - 4.0f);
        g.setColour (Palette::text);
        g.setFont (uiFont (10.5f, juce::Font::bold));
        g.drawText (fr ("ZONE À DÉCOUPER"), juce::Rectangle<float> (juce::jmax (0.0f, ra) + 4.0f, 0.0f, 140.0f, rulerH), juce::Justification::centredLeft, false);

        // régions colorées
        for (int i = 0; i < s->count(); ++i)
        {
            const float x0 = xOf (s->starts[(size_t) i]), x1 = xOf (s->endOf (i));
            if (x1 < 0.0f || x0 > w) continue;
            const bool hot = proc.lastSlice.load() == i && proc.slicePlayPos.load() >= 0.0f;
            g.setColour (sliceColour (i).withAlpha (hot ? 0.30f : 0.12f));
            g.fillRect (juce::Rectangle<float> (x0, top, x1 - x0, h));
        }

        // grille des temps (quand le tempo du sample est connu) : repère précis pour placer les coupes
        if (const double bpm = proc.sliceBpm.load(); bpm > 30.0)
        {
            const double spb = s->rate * 60.0 / bpm;
            const double pxPerBeat = spb / juce::jmax (1, viewB - viewA) * w;
            const int sub = pxPerBeat > 90.0 ? 4 : (pxPerBeat > 40.0 ? 2 : 1);   // 1/16 quand on zoome assez
            const double step = spb / sub;
            for (double t = std::ceil (viewA / step) * step; t < viewB; t += step)
            {
                const long long k = (long long) std::llround (t / step);
                const bool beat = k % sub == 0, bar = beat && (k / sub) % 4 == 0;
                const float x = xOf ((int) t);
                g.setColour (Palette::text.withAlpha (bar ? 0.16f : (beat ? 0.09f : 0.045f)));
                g.fillRect (juce::Rectangle<float> (x, top, 1.0f, h));
            }
        }

        // forme d'onde de la partie visible
        buildPeaks (d, (int) w);
        const float mid = top + h * 0.5f;
        for (int x = 0; x < (int) peaks.size(); ++x)
        {
            const int smp = smpOf ((float) x);
            const bool inside = smp >= reg.first && smp < reg.second;
            const int si = sliceIndexAt (smp, *s);
            g.setColour (inside ? sliceColour (si).interpolatedWith (Palette::text, 0.35f).withAlpha (0.9f) : Palette::dim.withAlpha (0.35f));
            g.drawVerticalLine (x, mid - peaks[(size_t) x] * h * 0.46f, mid + peaks[(size_t) x] * h * 0.46f + 1.0f);
        }

        // transitoires (débuts de coups) bien visibles : c'est là que les repères viennent se caler quand on les glisse
        ensureTransients (*s);
        for (size_t k = 0; k < transients.size(); ++k)
        {
            const float x = xOf (transients[k]);
            if (x < -2.0f || x > w + 2.0f) continue;
            const bool hot = (int) k == snapHint;
            const float strength = k < transStrength.size() ? transStrength[k] : 0.5f;
            g.setColour (Palette::peach.withAlpha (hot ? 0.95f : 0.22f + 0.33f * strength));
            g.fillRect (juce::Rectangle<float> (x - (hot ? 1.0f : 0.5f), top + h * 0.06f, hot ? 2.0f : 1.0f, h * 0.88f));
            juce::Path tick;   // petit triangle en bas de chaque transitoire
            tick.addTriangle (x - 4.0f, top + h, x + 4.0f, top + h, x, top + h - 6.0f);
            g.fillPath (tick);
        }
        // hors zone : grisé
        g.setColour (Palette::bg.withAlpha (0.45f));
        if (ra > 0.0f) g.fillRect (0.0f, top, juce::jmin (w, ra), h);
        if (rb < w) g.fillRect (juce::jmax (0.0f, rb), top, w - juce::jmax (0.0f, rb), h);

        // repères
        for (int i = 0; i < s->count(); ++i)
        {
            const float x = xOf (s->starts[(size_t) i]);
            if (x < -2.0f || x > w + 2.0f) continue;
            const bool sel = i == selected;
            g.setColour (sel ? Palette::peach : Palette::text.withAlpha (i == dragIndex ? 1.0f : 0.7f));
            g.fillRect (juce::Rectangle<float> (x, top, sel ? 2.5f : 1.5f, h));
            juce::Path tri;
            tri.addTriangle (x, top, x + 9.0f, top, x, top + 9.0f);
            g.fillPath (tri);
            g.setFont (uiFont (11.5f, juce::Font::bold));
            g.drawText (juce::String (i + 1) + "  " + slicer::noteLabel (i),
                        juce::Rectangle<float> (x + 4.0f, top + h - 18.0f, 60.0f, 16.0f), juce::Justification::centredLeft, false);
        }

        if (proc.sliceZoneMode)   // « Mes chops » : fin de chaque zone + zone en train d'être tracée
        {
            for (int i = 0; i < (int) proc.sliceZones.size(); ++i)
            {
                const float x = xOf (proc.sliceZones[(size_t) i].second);
                g.setColour (sliceColour (i).withAlpha (0.9f));
                g.fillRect (juce::Rectangle<float> (x - 1.0f, top, 2.0f, h));
                juce::Path tri; tri.addTriangle (x, top, x - 9.0f, top, x, top + 9.0f); g.fillPath (tri);
            }
            if (zoneCreating)
            {
                const float a = xOf (juce::jmin (zoneNewA, zoneNewB)), b = xOf (juce::jmax (zoneNewA, zoneNewB));
                g.setColour (Palette::peach.withAlpha (0.25f)); g.fillRect (juce::Rectangle<float> (a, top, b - a, h));
                g.setColour (Palette::peach); g.drawRect (juce::Rectangle<float> (a, top, b - a, h), 1.5f);
            }
            if (proc.sliceZones.empty())
            {
                g.setColour (Palette::text.withAlpha (0.85f));
                g.setFont (uiFont (14.0f, juce::Font::bold));
                g.drawFittedText (fr ("Mes chops : glisse sur la forme d'onde pour choisir chaque morceau à découper"),
                                  getLocalBounds().withTrimmedTop ((int) rulerH).reduced (20), juce::Justification::centred, 2);
            }
        }

        const float pp = proc.slicePlayPos.load();
        if (pp >= 0.0f)
        {
            const float x = xOf ((int) (pp * (float) d.getNumSamples()));
            g.setColour (Palette::text);
            g.fillRect (juce::Rectangle<float> (x - 1.0f, top, 2.0f, h));
        }

        g.setColour (Palette::dim);
        g.setFont (uiFont (12.0f));
        juce::String info = s->name + juce::String::fromUTF8 ("  ·  ") + juce::String (d.getNumSamples() / s->rate, 2) + " s  ·  " + juce::String (s->count()) + " " + fr ("tranches");
        if (viewB - viewA < d.getNumSamples()) info << "  ·  zoom x" << juce::String ((double) d.getNumSamples() / juce::jmax (1, viewB - viewA), 1);
        g.drawText (info, getLocalBounds().reduced (10, 4).withTrimmedTop ((int) rulerH).removeFromTop (16), juce::Justification::centredRight, true);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto s = proc.getSliceSound();
        if (s == nullptr) { browse(); return; }
        if (e.position.y < rulerH)   // choix de la zone
        {
            const auto reg = proc.getSliceRegion();
            const float ra = xOf (reg.first), rb = xOf (reg.second);
            regionDrag = std::abs (e.position.x - ra) < 7.0f ? 1 : (std::abs (e.position.x - rb) < 7.0f ? 2 : 3);
            regionAnchor = smpOf (e.position.x);
            pendingA = reg.first; pendingB = reg.second;
            return;
        }
        if (proc.sliceZoneMode)   // « Mes chops » : on trace soi-même chaque zone à découper
        {
            const int smp = smpOf (e.position.x);
            const int zi = zoneAt (smp);
            if (e.mods.isPopupMenu() && zi >= 0)
            {
                auto z = proc.sliceZones; z.erase (z.begin() + zi); proc.setSliceZones (z); return;
            }
            if (! e.mods.isPopupMenu())
            {
                bool endEdge = false;
                const int edge = zoneEdgeNear (e.position.x, endEdge);
                if (edge >= 0) { zoneEdit = edge; zoneEditEnd = endEdge; zoneBefore = proc.sliceZones; return; }
                if (zi >= 0) { playing = zi; proc.playSlice (zi, true); return; }
                zoneCreating = true; zoneNewA = zoneNewB = smp; repaint();
                return;
            }
        }
        const int m = proc.sliceZoneMode ? -1 : markerNear (e.position.x, *s);
        if (e.mods.isPopupMenu())
        {
            if (m > 0) { proc.removeSliceMarker (m); selected = -1; return; }
            const int at = smpOf (e.position.x);
            juce::PopupMenu menu;
            menu.addItem (1, fr ("Ajouter un repère ici"));
            menu.addItem (2, fr ("Découper tout le sample (effacer la zone)"));
            menu.addItem (3, fr ("Redécouper automatiquement (effacer mes repères)"));
            menu.addItem (4, fr ("Effacer le motif enregistré"), ! proc.getSlicePattern().empty());
            menu.addItem (5, fr ("Charger un autre sample…"));
            menu.addSeparator();
            menu.addItem (6, fr ("Annuler la dernière action  (Ctrl+Z)"));
            menu.addItem (7, fr ("Rétablir  (Ctrl+Y)"));
            if (proc.sliceZoneMode) menu.addItem (8, fr ("Mes chops : effacer toutes mes zones"), ! proc.sliceZones.empty());
            juce::Component::SafePointer<SliceView> self (this);
            menu.showMenuAsync (juce::PopupMenu::Options(), [safe = self, at] (int r)
            {
                if (safe == nullptr) return;
                auto& p = safe->proc;
                switch (r)
                {
                    case 1: p.addSliceMarker (at); break;
                    case 2: p.setSliceRegion (0, -1); safe->zoomAll(); break;
                    case 3: p.sliceHistoryMark(); p.recomputeSlices(); p.sliceHistoryCommit(); break;
                    case 4: p.clearSlicePattern(); break;
                    case 5: safe->browse(); break;
                    case 6: p.undoAny(); break;
                    case 7: p.redoAny(); break;
                    case 8: p.setSliceZones ({}); break;
                    default: break;
                }
                safe->repaint();
            });
            return;
        }
        if (m > 0) { dragIndex = m; selected = m; proc.sliceHistoryMark(); repaint(); grabKeyboardFocus(); return; }
        selected = -1;
        playing = sliceIndexAt (smpOf (e.position.x), *s);
        proc.playSlice (playing, true);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto s = proc.getSliceSound();
        if (s == nullptr) return;
        const int smp = juce::jlimit (0, s->data->getNumSamples(), smpOf (e.position.x));
        if (regionDrag > 0)
        {
            if (regionDrag == 1) pendingA = juce::jmin (smp, pendingB - 64);
            else if (regionDrag == 2) pendingB = juce::jmax (smp, pendingA + 64);
            else { pendingA = juce::jmin (regionAnchor, smp); pendingB = juce::jmax (regionAnchor, smp); }
            showPending = true;
            repaint();
            return;
        }
        if (zoneCreating) { zoneNewB = smp; repaint(); return; }
        if (zoneEdit >= 0 && zoneEdit < (int) proc.sliceZones.size())
        {
            auto& z = proc.sliceZones[(size_t) zoneEdit];
            const int minLen = (int) (0.04 * s->rate);
            if (zoneEditEnd) z.second = juce::jmax (z.first + minLen, smp);
            else z.first = juce::jmin (z.second - minLen, smp);
            proc.applySliceZones();
            return;
        }
        if (dragIndex <= 0) return;
        // aimant : le repère se cale sur la transitoire la plus proche (sinon sur la grille des temps), Alt = libre
        proc.moveSliceMarker (dragIndex, e.mods.isAltDown() ? smp : snapped (smp, *s));
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (regionDrag > 0)
        {
            regionDrag = 0;
            if (showPending && pendingB - pendingA > 256) proc.setSliceRegion (pendingA, pendingB);
            showPending = false;
            repaint();
            return;
        }
        if (zoneCreating)
        {
            zoneCreating = false;
            auto z = proc.sliceZones;
            z.push_back ({ juce::jmin (zoneNewA, zoneNewB), juce::jmax (zoneNewA, zoneNewB) });
            proc.setSliceZones (z);
            repaint();
            return;
        }
        if (zoneEdit >= 0)
        {
            auto after = proc.sliceZones;
            proc.sliceZones = zoneBefore;
            proc.setSliceZones (after);   // une seule étape d'annulation pour tout le geste
            zoneEdit = -1;
            return;
        }
        if (dragIndex > 0) proc.sliceHistoryCommit();
        if (playing >= 0) { proc.playSlice (playing, false); playing = -1; }
        dragIndex = -1;
        snapHint = -1;
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        auto s = proc.getSliceSound();
        if (s == nullptr) return;
        if (e.position.y < rulerH) { proc.setSliceRegion (0, -1); return; }   // zone = tout le sample
        if (playing >= 0) { proc.playSlice (playing, false); playing = -1; }
        if (proc.sliceZoneMode) return;
        const int at = smpOf (e.position.x);
        proc.addSliceMarker (e.mods.isAltDown() ? at : snapped (at, *s));   // nouveau repère calé sur la transitoire la plus proche
        snapHint = -1;
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wd) override
    {
        auto s = proc.getSliceSound();
        if (s == nullptr) return;
        const int len = s->data->getNumSamples();
        clampView (*s);
        const double span = viewB - viewA;
        if (e.mods.isShiftDown() || std::abs (wd.deltaX) > std::abs (wd.deltaY))
        {
            const double d = (wd.deltaX != 0.0f ? -wd.deltaX : -wd.deltaY) * span * 0.5;
            viewA = (int) juce::jlimit (0.0, (double) len - span, viewA + d); viewB = viewA + (int) span;
        }
        else
        {
            const int at = smpOf (e.position.x);
            const double f = wd.deltaY > 0 ? 0.75 : 1.0 / 0.75;
            const double ns = juce::jlimit (s->rate * 0.02, (double) len, span * f);
            const double rel = (at - viewA) / span;
            viewA = (int) juce::jlimit (0.0, (double) len - ns, at - rel * ns); viewB = viewA + (int) ns;
        }
        peaksKey = -1;
        repaint();
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (selected > 0 && (k.getKeyCode() == juce::KeyPress::leftKey || k.getKeyCode() == juce::KeyPress::rightKey))
        {
            nudgeSelected ((k.getKeyCode() == juce::KeyPress::leftKey ? -1.0 : 1.0) * (k.getModifiers().isShiftDown() ? 10.0 : 1.0));
            return true;
        }
        if (selected > 0 && (k.getKeyCode() == juce::KeyPress::deleteKey || k.getKeyCode() == juce::KeyPress::backspaceKey))
        {
            proc.removeSliceMarker (selected); selected = -1; return true;
        }
        return false;
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        auto s = proc.getSliceSound();
        juce::MouseCursor c = juce::MouseCursor::PointingHandCursor;
        if (s != nullptr)
        {
            if (e.position.y < rulerH) c = juce::MouseCursor::IBeamCursor;
            else if (markerNear (e.position.x, *s) > 0) c = juce::MouseCursor::LeftRightResizeCursor;
        }
        setMouseCursor (c);
    }

    bool isInterestedInFileDrag (const juce::StringArray& f) override { return f.size() > 0 && VariantProcessor::isSupportedFile (juce::File (f[0])); }
    void fileDragEnter (const juce::StringArray&, int, int) override { over = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { over = false; repaint(); }
    void filesDropped (const juce::StringArray& f, int, int) override { over = false; zoomAll(); proc.loadSliceFile (juce::File (f[0])); repaint(); }

    void browse()
    {
        chooser = std::make_unique<juce::FileChooser> (fr ("Choisir un sample à découper"), juce::File(), VariantProcessor::getSupportedWildcard());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe = juce::Component::SafePointer<SliceView> (this)] (const juce::FileChooser& fc)
            { if (safe != nullptr && fc.getResult().existsAsFile()) { safe->zoomAll(); safe->proc.loadSliceFile (fc.getResult()); } });
    }

private:
    static constexpr float rulerH = 16.0f;

    void clampView (const slicer::SliceSound& s)
    {
        const int len = s.data->getNumSamples();
        if (viewB <= 0 || viewB > len || viewA < 0 || viewA >= viewB) { viewA = 0; viewB = len; }
    }
    float xOf (int smp) const { return (float) ((double) (smp - viewA) / juce::jmax (1, viewB - viewA) * getWidth()); }
    int smpOf (float x) const { return viewA + (int) ((double) x / juce::jmax (1, getWidth()) * (viewB - viewA)); }

    void buildPeaks (const juce::AudioBuffer<float>& d, int width)
    {
        const juce::int64 key = (juce::int64) width * 1000003 + viewA * 31 + viewB;
        if (key == peaksKey || width <= 0) return;
        peaksKey = key;
        peaks.assign ((size_t) width, 0.0f);
        const int len = d.getNumSamples();
        float mx = 1.0e-6f;
        for (int c = 0; c < d.getNumChannels(); ++c)   // normalisé sur tout le fichier
        {
            const float* r = d.getReadPointer (c);
            for (int i = 0; i < len; i += 16) mx = juce::jmax (mx, std::abs (r[i]));
        }
        for (int x = 0; x < width; ++x)
        {
            const int a = juce::jlimit (0, len, smpOf ((float) x)), b = juce::jlimit (a + 1, juce::jmax (a + 1, len), smpOf ((float) x + 1.0f));
            const int step = juce::jmax (1, (b - a) / 64);
            float p = 0.0f;
            for (int c = 0; c < d.getNumChannels(); ++c)
            {
                const float* r = d.getReadPointer (c);
                for (int i = a; i < b && i < len; i += step) p = juce::jmax (p, std::abs (r[i]));
            }
            peaks[(size_t) x] = juce::jmin (1.0f, p / mx);
        }
    }

    static int sliceIndexAt (int smp, const slicer::SliceSound& s)
    {
        int idx = 0;
        for (int i = 0; i < s.count(); ++i) if (s.starts[(size_t) i] <= smp) idx = i;
        return idx;
    }

    int zoneAt (int smp) const
    {
        for (int i = 0; i < (int) proc.sliceZones.size(); ++i)
            if (smp >= proc.sliceZones[(size_t) i].first && smp < proc.sliceZones[(size_t) i].second) return i;
        return -1;
    }
    int zoneEdgeNear (float x, bool& isEnd) const
    {
        for (int i = 0; i < (int) proc.sliceZones.size(); ++i)
        {
            if (std::abs (xOf (proc.sliceZones[(size_t) i].second) - x) < 6.0f) { isEnd = true; return i; }
            if (std::abs (xOf (proc.sliceZones[(size_t) i].first) - x) < 6.0f) { isEnd = false; return i; }
        }
        return -1;
    }
    bool zoneCreating = false, zoneEditEnd = false;
    int zoneNewA = 0, zoneNewB = 0, zoneEdit = -1;
    std::vector<std::pair<int, int>> zoneBefore;

    int markerNear (float x, const slicer::SliceSound& s) const
    {
        for (int i = 1; i < s.count(); ++i)
            if (std::abs (xOf (s.starts[(size_t) i]) - x) < 6.0f) return i;
        return -1;
    }

    // transitoires de tout le sample (détection fine, plus nombreuses que les tranches) : calculées une fois par sample
    std::vector<int> transients;
    std::vector<float> transStrength;
    const void* transKey = nullptr;
    int transLen = -1;
    juce::String transName;
    int snapHint = -1;
    void ensureTransients (const slicer::SliceSound& s)
    {
        if (s.data == nullptr || (s.data.get() == transKey && s.data->getNumSamples() == transLen && s.name == transName)) return;
        transKey = s.data.get(); transLen = s.data->getNumSamples(); transName = s.name;
        transients = slicer::detectTransients (*s.data, s.rate, 0.8f, slicer::maxSlices * 4, 0.0, true);
        if (! transients.empty() && transients.front() == 0) transients.erase (transients.begin());   // le début du fichier n'est pas un coup
        // force de chaque attaque (montée de l'enveloppe) pour les afficher plus ou moins fort
        transStrength.assign (transients.size(), 0.5f);
        const auto& d = *s.data;
        const int len = d.getNumSamples(), w = juce::jmax (32, (int) (0.006 * s.rate));
        float mx = 1.0e-6f;
        std::vector<float> rise (transients.size(), 0.0f);
        for (size_t k = 0; k < transients.size(); ++k)
        {
            const int t = transients[k];
            float before = 0.0f, after = 0.0f;
            for (int c = 0; c < d.getNumChannels(); ++c)
            {
                const float* r = d.getReadPointer (c);
                for (int i = juce::jmax (0, t - w); i < t; ++i) before = juce::jmax (before, std::abs (r[i]));
                for (int i = t; i < juce::jmin (len, t + w); ++i) after = juce::jmax (after, std::abs (r[i]));
            }
            rise[k] = juce::jmax (0.0f, after - before);
            mx = juce::jmax (mx, rise[k]);
        }
        for (size_t k = 0; k < rise.size(); ++k) transStrength[k] = std::sqrt (rise[k] / mx);
    }
    // position aimantée : transitoire à moins de 10 pixels, sinon temps / 1/16 de la grille à moins de 6 pixels
    int snapped (int smp, const slicer::SliceSound& s)
    {
        ensureTransients (s);
        snapHint = -1;
        float best = 10.0f; int at = smp;
        for (size_t k = 0; k < transients.size(); ++k)
        {
            const float dx = std::abs (xOf (transients[k]) - xOf (smp));
            if (dx < best) { best = dx; at = transients[k]; snapHint = (int) k; }
        }
        if (snapHint >= 0) return at;
        if (const double bpm = proc.sliceBpm.load(); bpm > 30.0)
        {
            const double step = s.rate * 60.0 / bpm / 4.0;
            const int g = (int) std::llround (std::round (smp / step) * step);
            if (std::abs (xOf (g) - xOf (smp)) < 6.0f) return g;
        }
        return smp;
    }

    VariantProcessor& proc;
    std::vector<float> peaks;
    juce::int64 peaksKey = -1;
    int seenVersion = -1, dragIndex = -1, playing = -1;
    int viewA = 0, viewB = -1;
    int regionDrag = 0, regionAnchor = 0, pendingA = 0, pendingB = 0;
    bool showPending = false;
    float lastPos = -2.0f;
    bool over = false;
    std::unique_ptr<juce::FileChooser> chooser;
};

class SlicePad : public juce::Component
{
public:
    SlicePad (VariantProcessor& p, int i) : proc (p), index (i) {}
    void setIndex (int i) { if (i != index) { index = i; repaint(); } }

    void paint (juce::Graphics& g) override
    {
        const bool valid = index < proc.getSliceCount();
        const bool hot = down || (proc.lastSlice.load() == index && proc.slicePlayPos.load() >= 0.0f);
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        const auto c = sliceColour (index);
        g.setColour (valid ? c.withAlpha (hot ? 0.75f : 0.22f) : Palette::panel2.withAlpha (0.35f));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (valid ? c.withAlpha (0.8f) : Palette::line);
        g.drawRoundedRectangle (r, 8.0f, 1.0f);
        g.setColour (valid ? Palette::text : Palette::dim.withAlpha (0.5f));
        g.setFont (uiFont (15.0f, juce::Font::bold));
        g.drawText (juce::String (index + 1), r.reduced (8.0f, 2.0f), juce::Justification::centredLeft);
        g.setFont (uiFont (11.5f));
        g.setColour (Palette::dim);
        g.drawText (slicer::noteLabel (index), r.reduced (8.0f, 2.0f), juce::Justification::centredRight);
        // réglages propres à ce chop (clic droit), en petit en bas du pad
        const auto sum = proc.chopSettings.summary (index);
        if (valid && sum.isNotEmpty())
        {
            g.setColour (c.brighter (0.4f));
            g.setFont (uiFont (10.0f, juce::Font::bold));
            g.drawText (sum, r.reduced (6.0f, 3.0f), juce::Justification::bottomLeft, true);
        }
    }

    // clic droit : réglages du chop (volume, hauteur, pan, inversé), comme sur une MPC
    void showChopMenu()
    {
        auto& cs = proc.chopSettings;
        const int i = index;
        const float vol = cs.vol[(size_t) i].load(), pit = cs.pitch[(size_t) i].load(), pan = cs.pan[(size_t) i].load();
        juce::PopupMenu m, vm, pm, panm;
        for (int d : { 6, 3, 0, -3, -6, -12 })
            vm.addItem (100 + d + 50, (d > 0 ? "+" : "") + juce::String (d) + " dB", true, juce::roundToInt (vol) == d);
        for (int s : { 12, 7, 5, 3, 2, 1, 0, -1, -2, -3, -5, -7, -12 })
            pm.addItem (300 + s + 50, (s > 0 ? "+" : "") + juce::String (s) + (std::abs (s) == 12 ? fr (" (octave)") : juce::String (" st")), true, juce::roundToInt (pit) == s);
        static const float pans[] = { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f };
        static const char* panNames[] = { "Gauche", "Un peu à gauche", "Centre", "Un peu à droite", "Droite" };
        for (int k = 0; k < 5; ++k) panm.addItem (500 + k, fr (panNames[k]), true, std::abs (pan - pans[k]) < 0.01f);
        m.addSectionHeader (fr ("Chop ") + juce::String (i + 1) + "  (" + slicer::noteLabel (i) + ")");
        m.addSubMenu (fr ("Volume"), vm);
        m.addSubMenu (fr ("Hauteur"), pm);
        m.addSubMenu (fr ("Pan"), panm);
        m.addItem (600, fr ("Jouer à l'envers"), true, cs.rev[(size_t) i].load() != 0);
        m.addSeparator();
        m.addItem (700, fr ("Remettre ce chop d'origine"), ! cs.isDefault (i));
        m.addItem (701, fr ("Remettre tous les chops d'origine"));
        juce::Component::SafePointer<SlicePad> self (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [self, i] (int r)
        {
            if (self == nullptr || r == 0) return;
            auto& c = self->proc.chopSettings;
            if (r >= 100 && r < 300) c.vol[(size_t) i] = (float) (r - 150);
            else if (r >= 300 && r < 500) c.pitch[(size_t) i] = (float) (r - 350);
            else if (r >= 500 && r < 505) { static const float pv[] = { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f }; c.pan[(size_t) i] = pv[r - 500]; }
            else if (r == 600) c.rev[(size_t) i] = c.rev[(size_t) i].load() != 0 ? 0 : 1;
            else if (r == 700) c.resetOne (i);
            else if (r == 701) c.resetAll();
            ++c.version;
            if (self->getParentComponent() != nullptr) self->getParentComponent()->repaint();
            self->repaint();
            self->proc.playSlice (i, true);   // on entend tout de suite le résultat
            juce::Timer::callAfterDelay (350, [self, i] { if (self != nullptr) self->proc.playSlice (i, false); });
        });
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (index < proc.getSliceCount()) showChopMenu(); return; }
        if (index < proc.getSliceCount()) { down = true; proc.playSlice (index, true); repaint(); }
    }
    void mouseUp (const juce::MouseEvent&) override   { if (down) { down = false; proc.playSlice (index, false); repaint(); } }
    void checkStuck() { if (down && ! juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown()) { down = false; proc.playSlice (index, false); repaint(); } }

private:
    VariantProcessor& proc;
    int index;
    bool down = false;
};

// Motif enregistré : une ligne par tranche
class SlicePatternView : public juce::Component
{
public:
    explicit SlicePatternView (VariantProcessor& p) : proc (p) {}
    void paint (juce::Graphics& g) override
    {
        auto a = getLocalBounds().toFloat();
        g.setColour (Palette::panel2.withAlpha (0.5f));
        g.fillRoundedRectangle (a, 8.0f);
        const auto pat = proc.getSlicePattern();
        if (pat.empty())
        {
            g.setColour (Palette::dim);
            g.setFont (uiFont (12.5f));
            g.drawText (proc.isSliceRecArmed() ? (proc.isSliceRecStarted() ? fr ("● Enregistrement…") : fr ("Prêt : joue une tranche pour démarrer"))
                                               : fr ("Ton motif enregistré apparaîtra ici"), a, juce::Justification::centred);
            return;
        }
        const double len = proc.getSlicePatternLength();
        int maxS = 1;
        for (auto& n : pat) maxS = juce::jmax (maxS, n.pitch - slicer::base() + 1);
        const float rh = a.getHeight() / (float) maxS;
        for (int b = 0; b <= (int) len; b += 4)
        {
            g.setColour (Palette::line);
            g.drawVerticalLine ((int) (b / len * a.getWidth()), 0.0f, a.getHeight());
        }
        for (auto& n : pat)
        {
            const int s = n.pitch - slicer::base();
            g.setColour (sliceColour (s).withAlpha (0.5f + 0.5f * n.vel));
            g.fillRoundedRectangle ((float) (n.start / len) * a.getWidth(), a.getHeight() - (float) (s + 1) * rh + 1.0f,
                                    juce::jmax (3.0f, (float) (n.length / len) * a.getWidth() - 1.0f), juce::jmax (2.0f, rh - 2.0f), 2.0f);
        }
        if (proc.isPreviewing())
        {
            g.setColour (Palette::text);
            g.fillRect (a.getWidth() * proc.getPreviewProgress() - 1.0f, 0.0f, 2.0f, a.getHeight());
        }
    }
private:
    VariantProcessor& proc;
};

class SlicerCard : public juce::Component
{
public:
    explicit SlicerCard (VariantProcessor& p) : proc (p)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &on, &load, &fromLayer, &layerBox, &toArp, &view,
                                                                &mode, &gate, &sync, &count, &sens, &tune, &shuffle, &cents, &stretchT,
                                                                &kbButton, &recButton, &playPat, &clearPat, &chipMidi, &chipWav, &toLayer, &toGuide, &patView, &detachPads,
                                                                &fine, &zoneAll, &zoneZoom, &zoomAllBtn, &nudgeL, &nudgeR, &baseBox, &myChops, &tapBtn, &choke,
                                                                &chopAtk, &chopRel, &chopVel, &chopTone, &chopLevel })
            addAndMakeVisible (c);
        for (int i = 0; i < slicer::padsPerBank; ++i) addAndMakeVisible (pads.add (new SlicePad (proc, i)));
        for (int b = 0; b < 4; ++b)
        {
            auto* bb = bankBtns.add (new juce::TextButton (juce::String::charToString ((juce::juce_wchar) ('A' + b))));
            bb->setClickingTogglesState (true);
            bb->setRadioGroupId (7400);
            bb->setColour (juce::TextButton::buttonOnColourId, Palette::peach);
            bb->setTooltip (fr ("Banque de pads (comme Serato) : A = tranches 1-16, B = 17-32, C = 33-48, D = 49-64"));
            bb->onClick = [this, b] { bank = b; proc.sliceBank = b; view.zoomToSlices (b * slicer::padsPerBank, b * slicer::padsPerBank + slicer::padsPerBank - 1); for (int i = 0; i < pads.size(); ++i) pads[i]->setIndex (bank * slicer::padsPerBank + i); };
            addChildComponent (bb);
        }
        bankBtns[0]->setToggleState (true, juce::dontSendNotification);
        myChops.setClickingTogglesState (true);
        myChops.setColour (juce::TextButton::buttonOnColourId, Palette::peach);
        myChops.setTooltip (fr ("Mes chops : c'est toi qui choisis. Glisse sur la forme d'onde pour tracer chaque morceau à découper (bords déplaçables, clic droit = enlever). Éteint = découpe automatique."));
        myChops.setToggleState (proc.sliceZoneMode, juce::dontSendNotification);
        myChops.onClick = [this] { proc.setSliceZoneMode (myChops.getToggleState()); view.repaint(); };
        tapBtn.setColour (juce::TextButton::buttonColourId, Palette::mint.withAlpha (0.25f));
        addChildComponent (tapStop);
        tapStop.setColour (juce::TextButton::buttonColourId, Palette::panel2);
        tapStop.setTooltip (fr ("Arrêter le défilement (les coupes déjà posées sont gardées) · Échap marche aussi"));
        tapStop.onClick = [this] { proc.stopTapChop(); refreshTap(); };
        tapBtn.setTooltip (fr ("Taper mes chops : le sample défile, et à chaque appui sur Espace (ou sur ce bouton) une coupe est posée à cet endroit, calée sur le début du coup. Le plus simple pour faire tes propres chops."));
        tapBtn.onClick = [this]
        {
            if (proc.isTapChopping()) proc.tapChop();
            else { if (proc.sliceZoneMode) { proc.setSliceZoneMode (false); myChops.setToggleState (false, juce::dontSendNotification); } proc.startTapChop(); }
            refreshTap();
        };
        zoneAll.setTooltip (fr ("Découper tout le sample (efface la zone choisie)"));
        zoneAll.onClick = [this] { proc.setSliceRegion (0, -1); view.zoomAll(); };
        zoneZoom.setTooltip (fr ("Zoomer sur la zone choisie (molette sur la forme d'onde = zoom)"));
        zoneZoom.onClick = [this] { view.zoomToRegion(); };
        zoomAllBtn.setTooltip (fr ("Voir tout le sample"));
        zoomAllBtn.onClick = [this] { view.zoomAll(); };
        nudgeL.setTooltip (fr ("Décale le repère sélectionné de 1 ms vers la gauche (Maj = 10 ms). Aussi avec les flèches du clavier."));
        nudgeR.setTooltip (fr ("Décale le repère sélectionné de 1 ms vers la droite (Maj = 10 ms). Aussi avec les flèches du clavier."));
        baseBox.addItemList ({ fr ("1er chop : C0"), fr ("1er chop : C3"), fr ("1er chop : C5") }, 1);
        baseBox.setTooltip (fr ("Note qui joue la 1re tranche dans le piano roll de FL (C0 = la note la plus basse), puis une note par tranche vers le haut"));
        baseAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "sliceBase", baseBox);
        nudgeL.onClick = [this] { view.nudgeSelected (juce::ModifierKeys::currentModifiers.isShiftDown() ? -10.0 : -1.0); };
        nudgeR.onClick = [this] { view.nudgeSelected (juce::ModifierKeys::currentModifiers.isShiftDown() ? 10.0 : 1.0); };

        load.onClick = [this] { view.browse(); };
        detachPads.setTooltip (fr ("Ouvre les pads du slicer dans une petite fenêtre à placer où tu veux"));
        detachPads.onClick = [this] { if (onDetachPads) onDetachPads(); };
        for (int i = 0; i < maxLayers; ++i) layerBox.addItem (fr ("Couche ") + juce::String (i + 1), i + 1);
        layerBox.setSelectedItemIndex (0, juce::dontSendNotification);
        layerBox.setTooltip (fr ("Couche de l'onglet Son à utiliser"));
        fromLayer.setTooltip (fr ("Découpe le son chargé dans cette couche (onglet Son)"));
        fromLayer.onClick = [this] { proc.useLayerForSlices (layerBox.getSelectedItemIndex()); };

        toArp.setColour (juce::TextButton::buttonColourId, Palette::peach.withAlpha (0.3f));
        toArp.setTooltip (fr ("Active le slicer et l'arpège sur les tranches avec un motif au hasard : tiens une seule note et ça joue"));
        toArp.onClick = [this]
        {
            proc.setParam ("slicerOn", 1.0f);
            proc.randomizeArp (false);
            proc.setParam ("arpSource", 2.0f);
            proc.setParam ("arpOn", 1.0f);
            proc.setParam ("arpOct", 1.0f);
            if (onArp) onArp();
        };
        shuffle.setTooltip (fr ("Nouveau motif d'arpège au hasard sur les tranches"));
        shuffle.onClick = [this] { proc.randomizeArp (false); proc.setParam ("arpSource", 2.0f); proc.setParam ("arpOct", 1.0f); };

        // clavier d'ordinateur -> chops
        kbButton.setClickingTogglesState (false);
        kbButton.setColour (juce::TextButton::buttonOnColourId, Palette::peach);
        kbButton.setTooltip (fr ("Les lettres du clavier jouent les tranches : A Z E R T Y U I O P = tranches 1 à 10, W X C V B N = 11 à 16, Q S D F G H J K L M = 17 à 26."));
        kbButton.onClick = [this] { proc.kbMode = proc.kbMode == 3 ? 0 : 3; library::settings().setValue ("samplerKbOff", proc.kbMode != 3); focusEditor(); refreshButtons(); };

        recButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffff4d6d));
        recButton.setTooltip (fr ("Enregistre ce que tu joues sur les tranches (clavier, pads, MIDI). Reclique pour arrêter."));
        recButton.onClick = [this] { proc.armSliceRec (! proc.isSliceRecArmed()); focusEditor(); refreshButtons(); };
        clearPat.setTooltip (fr ("Efface le motif enregistré pour recommencer (Ctrl+Z pour le récupérer)"));
        clearPat.onClick = [this] { proc.clearSlicePattern(); patView.repaint(); focusEditor(); };
        playPat.setTooltip (fr ("Écouter le motif enregistré"));
        playPat.onClick = [this] { proc.previewSlicePattern (! proc.isPreviewing()); };
        chipMidi.setTooltip (fr ("Glisse le motif en MIDI dans FL Studio (1re tranche = la note choisie dans « 1er chop », puis une note par tranche)"));
        chipWav.setTooltip (fr ("Glisse le motif rendu en audio (.wav) dans FL Studio"));
        toLayer.setTooltip (fr ("Rend le motif en audio et le charge dans la couche choisie de l'onglet Son"));
        toLayer.onClick = [this] { proc.slicePatternToLayer (layerBox.getSelectedItemIndex()); };
        toGuide.setTooltip (fr ("Met le motif dans le piano roll de l'onglet Accords & MIDI pour le modifier"));
        toGuide.onClick = [this] { proc.slicePatternToGuide(); };
        juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<SlicerCard> (this)] { if (sp != nullptr) sp->refreshBpm(); });

        // BPM du sample : modifiable (clic, tape le tempo, Entrée). Vide ou « auto » = tempo du nom du fichier ou de l'analyse
        addAndMakeVisible (bpmLabel);
        bpmLabel.setEditable (true, true, false);
        bpmLabel.setJustificationType (juce::Justification::centred);
        bpmLabel.setColour (juce::Label::backgroundColourId, Palette::panel2.withAlpha (0.8f));
        bpmLabel.setColour (juce::Label::outlineColourId, Palette::peach.withAlpha (0.6f));
        bpmLabel.setColour (juce::Label::textColourId, Palette::text);
        bpmLabel.setFont (uiFont (12.5f, juce::Font::bold));
        bpmLabel.setTooltip (fr ("Tempo du sample : clique et tape le bon BPM (ex. 140), puis Entrée. Efface et Entrée = tempo automatique (celui écrit dans le nom du fichier, sinon l'analyse)."));
        bpmLabel.onTextChange = [this]
        {
            const auto t = bpmLabel.getText().retainCharacters ("0123456789.,").replaceCharacter (',', '.');
            proc.setSliceBpm (t.isEmpty() ? 0.0 : t.getDoubleValue());
            refreshBpm();
        };
        refreshButtons();
    }

    std::function<void()> onArp;
    std::function<void()> onDetachPads;
    juce::TextButton detachPads { juce::String::fromUTF8 ("⧉") };
    static constexpr int preferredHeight = 872;   // + rangée « Son des chops »

    // les touches n'arrivent à Variant que s'il a le focus clavier : on le reprend dès qu'on active le clavier ou le rec
    bool hasFocusSomewhere() const
    {
        auto* f = juce::Component::getCurrentlyFocusedComponent();
        return f != nullptr && f->getTopLevelComponent() == getTopLevelComponent() && juce::Process::isForegroundProcess();
    }
    void focusEditor()
    {
        if (auto* e = findParentComponentOfClass<juce::AudioProcessorEditor>()) { e->toFront (true); e->grabKeyboardFocus(); }
        else if (auto* top = getTopLevelComponent()) top->grabKeyboardFocus();
    }

    void refreshButtons()
    {
        kbButton.setToggleState (proc.kbMode == 3, juce::dontSendNotification);
        kbButton.setButtonText (proc.kbMode == 3 ? fr ("Clavier : ON") : fr ("Jouer au clavier"));
        if (proc.kbMode == 3 && ! hasFocusSomewhere())
            kbButton.setButtonText (fr ("Clique ici : clavier inactif"));
        recButton.setToggleState (proc.isSliceRecArmed(), juce::dontSendNotification);
        recButton.setButtonText (proc.isSliceRecArmed() ? fr ("■ Stop") : fr ("● Rec"));
    }

    void tick()
    {
        view.tick();
        if (proc.isTapChopping() != tapWas) refreshTap();
        {
            const int sm = (int) proc.getParam ("sliceMode");
            if (lastSliceMode >= 0 && sm != lastSliceMode && proc.sliceZoneMode) proc.setSliceZoneMode (false);   // on revient à la découpe auto
            lastSliceMode = sm;
            myChops.setToggleState (proc.sliceZoneMode, juce::dontSendNotification);
        }
        {
            const bool showBanks = proc.getSliceCount() > slicer::padsPerBank;
            if (showBanks != bankBtns[0]->isVisible())
            {
                for (auto* bb : bankBtns) bb->setVisible (showBanks);
                if (! showBanks && bank != 0) bankBtns[0]->triggerClick();
            }
        }
        const int v = proc.sliceVersion.load() * 7 + proc.lastSlice.load() + (proc.slicePlayPos.load() >= 0.0f ? 1000 : 0) + proc.chopSettings.version.load() * 13;
        if (v != lastV) { lastV = v; for (auto* p : pads) p->repaint(); }
        for (auto* p : pads) p->checkStuck();
        if (proc.isSliceRecArmed() || proc.isPreviewing() || proc.slicePatternVersion.load() != lastPat) { lastPat = proc.slicePatternVersion.load(); patView.repaint(); }
        refreshButtons();
        playPat.setButtonText (proc.isPreviewing() ? fr ("Stop") : fr ("Écouter"));
        if (! bpmLabel.isBeingEdited()) refreshBpm();
        const auto info = infoText();
        if (info != lastInfo) { lastInfo = info; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        drawCard (g, getLocalBounds(), fr ("SLICER  ·  découpe un sample en tranches jouables"), Palette::peach);
        g.setColour (Palette::text);
        g.setFont (uiFont (13.0f, juce::Font::bold));
        g.drawText (lastInfo, infoArea, juce::Justification::centredLeft, true);

        // titre de la rangée « Son des chops »
        g.setColour (Palette::peach);
        g.setFont (uiFont (12.5f, juce::Font::bold));
        auto sl = soundLabel;
        g.drawText (fr ("SON DES CHOPS"), sl.removeFromTop (20), juce::Justification::centredLeft);
        g.setColour (Palette::dim);
        g.setFont (uiFont (11.5f));
        g.drawFittedText (fr ("Clic droit sur un pad : volume, hauteur, pan, à l'envers pour ce chop seulement."), sl, juce::Justification::topLeft, 2, 1.0f);

        drawCard (g, recCard, fr ("JOUER & ENREGISTRER LES CHOPS"), Palette::pink);
        g.setColour (Palette::dim);
        g.setFont (uiFont (12.0f));
        g.drawFittedText (fr ("Slicer activé : dans le piano roll de FL, la note la plus basse (C0, réglable dans « 1er chop ») joue la tranche 1, la suivante la tranche 2… aussi au clavier "
                              "ou avec l'arpège (source « Tranches du slicer »). Repères modifiés à la main : gardés jusqu'à ce que tu changes le réglage de découpe."),
                          hint, juce::Justification::topLeft, 3, 1.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16, 10);
        auto title = r.removeFromTop (22);
        title.removeFromRight (60);   // place des flèches ▲ ▼ de la page
        on.setBounds (title.removeFromRight (190).withSizeKeepingCentre (190, 22).translated (0, 2));
        detachPads.setBounds (title.removeFromRight (40).withSizeKeepingCentre (32, 24).translated (0, 2));
        r.removeFromTop (8);

        auto row1 = r.removeFromTop (34);
        load.setBounds (row1.removeFromLeft (140)); row1.removeFromLeft (8);
        layerBox.setBounds (row1.removeFromLeft (100)); row1.removeFromLeft (4);
        fromLayer.setBounds (row1.removeFromLeft (96));
        row1.removeFromLeft (8);
        myChops.setBounds (row1.removeFromLeft (104)); row1.removeFromLeft (6);
        {
            auto tb = row1.removeFromLeft (160);
            if (proc.isTapChopping()) { tapStop.setBounds (tb.removeFromRight (58)); tb.removeFromRight (4); }
            tapBtn.setBounds (tb);
        }
        toArp.setBounds (row1.removeFromRight (170));
        r.removeFromTop (6);
        infoArea = r.removeFromTop (20);
        bpmLabel.setBounds (infoArea.removeFromRight (130).withTrimmedBottom (-2));
        infoArea.removeFromRight (8);
        r.removeFromTop (4);

        view.setBounds (r.removeFromTop (160));
        r.removeFromTop (6);
        {
            auto tb = r.removeFromTop (26);
            for (int b = 3; b >= 0; --b) { bankBtns[b]->setBounds (tb.removeFromRight (30)); tb.removeFromRight (3); }   // A B C D d'abord (toujours visibles)
            tb.removeFromRight (6);
            zoneAll.setBounds (tb.removeFromLeft (96)); tb.removeFromLeft (3);
            zoneZoom.setBounds (tb.removeFromLeft (92)); tb.removeFromLeft (3);
            zoomAllBtn.setBounds (tb.removeFromLeft (80)); tb.removeFromLeft (8);
            nudgeL.setBounds (tb.removeFromLeft (30)); tb.removeFromLeft (2);
            nudgeR.setBounds (tb.removeFromLeft (30)); tb.removeFromLeft (8);
            fine.setBounds (tb.removeFromLeft (124));
            tb.removeFromLeft (6);
            baseBox.setBounds (tb);
        }
        r.removeFromTop (8);

        auto row3 = r.removeFromTop (112);
        auto left = row3.removeFromLeft (370);
        mode.setBounds (left.removeFromTop (32));
        left.removeFromTop (10);
        auto tg = left.removeFromTop (26);
        gate.setBounds (tg.removeFromLeft (172));
        sync.setBounds (tg.removeFromLeft (110));
        stretchT.setBounds (tg);
        left.removeFromTop (4);
        choke.setBounds (left.removeFromTop (24).removeFromLeft (220));
        row3.removeFromLeft (12);
        shuffle.setBounds (row3.removeFromRight (130).withSizeKeepingCentre (130, 32));
        row3.removeFromRight (8);
        const int kw = juce::jmin (90, row3.getWidth() / 4);
        for (auto* k : { &count, &sens, &tune, &cents }) k->setBounds (row3.removeFromLeft (kw));
        r.removeFromTop (8);

        // son des chops : attaque, relâche, vélocité, couleur, volumes égalisés
        soundRow = r.removeFromTop (102);
        {
            auto sr = soundRow;
            auto lab = sr.removeFromLeft (370);
            chopLevel.setBounds (lab.withTrimmedTop (50).removeFromTop (26).removeFromLeft (220));
            soundLabel = lab.removeFromTop (46);
            sr.removeFromLeft (12);
            const int kw2 = juce::jmin (90, sr.getWidth() / 4);
            for (auto* k : { &chopAtk, &chopRel, &chopVel, &chopTone }) k->setBounds (sr.removeFromLeft (kw2));
        }
        r.removeFromTop (8);

        auto padArea = r.removeFromTop (92);
        for (auto* bb : bankBtns) bb->setVisible (proc.getSliceCount() > slicer::padsPerBank);
        const int pw = padArea.getWidth() / 8, ph = padArea.getHeight() / 2;
        for (int i = 0; i < pads.size(); ++i)
            pads[i]->setBounds (padArea.getX() + (i % 8) * pw, padArea.getY() + (i / 8) * ph, pw, ph);
        r.removeFromTop (10);

        recCard = r.removeFromTop (170);
        auto rc = recCard.reduced (14, 10).withTrimmedTop (24);
        auto b1 = rc.removeFromTop (32);
        kbButton.setBounds (b1.removeFromLeft (150)); b1.removeFromLeft (8);
        recButton.setBounds (b1.removeFromLeft (100)); b1.removeFromLeft (8);
        playPat.setBounds (b1.removeFromLeft (90)); b1.removeFromLeft (8);
        clearPat.setBounds (b1.removeFromLeft (80)); b1.removeFromLeft (8);
        toGuide.setBounds (b1.removeFromRight (150)); b1.removeFromRight (6);
        toLayer.setBounds (b1.removeFromRight (130));
        rc.removeFromTop (8);
        auto b2 = rc.removeFromTop (70);
        auto chips = b2.removeFromRight (150);
        chipMidi.setBounds (chips.removeFromTop (32));
        chips.removeFromTop (6);
        chipWav.setBounds (chips.removeFromTop (32));
        b2.removeFromRight (10);
        patView.setBounds (b2);
        r.removeFromTop (8);
        hint = r;
    }

private:
    void refreshBpm()
    {
        const double b = proc.sliceBpm.load();
        const auto src = proc.sliceBpmManual.load() ? fr (" (toi)") : (proc.sliceNamedBpm.load() > 0.0 ? fr (" (nom)") : fr (" (auto)"));
        const auto t = b > 0.0 ? juce::String (b, b == std::round (b) ? 0 : 1) + " BPM" + src : fr ("BPM ?");
        if (bpmLabel.getText() != t) bpmLabel.setText (t, juce::dontSendNotification);
    }
    juce::Label bpmLabel;

    juce::String infoText() const
    {
        auto s = proc.getSliceSound();
        if (s == nullptr) return fr ("Glisse un sample : tempo, tonalité et découpe d'un coup.");
        const auto an = proc.getAnalysis();
        juce::String t = s->name + juce::String::fromUTF8 ("  ·  ") + juce::String (s->count()) + " " + fr ("tranches");
        if (proc.analysisBusy.load()) t << juce::String::fromUTF8 ("  ·  ") << fr ("Analyse en cours…");
        else
        {
            const double bpm = proc.sliceBpm.load();
            if (bpm > 0.0) t << juce::String::fromUTF8 ("  ·  ") << juce::String (bpm, bpm == std::round (bpm) ? 0 : 1) << " BPM";
            if (! an.keys.empty())
                t << juce::String::fromUTF8 ("  ·  ") << theory::pcName (an.keys[0].key, theory::prefersFlats (an.keys[0].key, an.keys[0].minor ? theory::Minor : theory::Major))
                  << " " << (an.keys[0].minor ? fr ("mineur") : fr ("majeur"));
        }
        return t;
    }

    VariantProcessor& proc;

public:
    SliceView view { proc };

private:
    ParamToggle on { proc.apvts, "slicerOn", fr ("Jouer les tranches"), Palette::peach,
                     fr ("Activé : toutes les notes (clavier, FL, arpège, guide) jouent les tranches au lieu des couches") };
    juce::TextButton load { fr ("Charger un sample") }, fromLayer { fr ("Découper") },
                     toArp { fr ("Arpéger les tranches") }, shuffle { fr ("Autre motif") },
                     kbButton { fr ("Jouer au clavier") }, recButton { fr ("● Rec") }, playPat { fr ("Écouter") },
                     toLayer { fr ("Dans une couche") }, toGuide { fr ("Vers le piano roll") }, clearPat { fr ("Effacer") };
    juce::ComboBox layerBox;
    SegmentedChoice mode { proc.apvts, "sliceMode", { fr ("Transitoires"), fr ("Grille"), fr ("Au tempo") }, Palette::peach };
    ParamToggle gate { proc.apvts, "sliceGate", fr ("Couper au relâchement"), Palette::peach,
                       fr ("Activé : la tranche s'arrête quand la note s'arrête (rythmes nets). Désactivé : la tranche joue jusqu'au bout") };
    ParamToggle sync { proc.apvts, "sliceSync", fr ("Caler sur FL"), Palette::peach,
                       fr ("Accélère ou ralentit le sample pour qu'il suive le tempo de FL Studio (le ton change aussi, comme un sampler classique)") };
    LabeledKnob count { proc.apvts, "sliceCount", "Tranches", Palette::peach, fr ("Nombre de tranches (maximum en mode transitoires)") };
    LabeledKnob sens  { proc.apvts, "sliceSens", fr ("Sensibilité"), Palette::peach, fr ("Plus haut = coupe aussi sur les attaques faibles") };
    LabeledKnob tune  { proc.apvts, "sliceTune", "Transpo", Palette::peach, fr ("Transpose toutes les tranches") };
    LabeledKnob cents { proc.apvts, "sliceCents", fr ("Finesse"), Palette::peach, fr ("Accorde le sample au cent près (+ ou - quelques cents)") };
    ParamToggle stretchT { proc.apvts, "sliceStretch", "Stretch", Palette::peach,
                           fr ("Stretch : les tranches suivent le tempo de FL sans changer de hauteur (la transpo change la note, pas la vitesse)") };
    DragChip chipMidi { "MIDI .mid", Palette::lavender, [this] { return proc.exportSlicePatternMidi(); } };
    DragChip chipWav  { "Audio .wav", Palette::pink, [this] { return proc.renderSlicePatternWav(); } };
    SlicePatternView patView { proc };
    juce::OwnedArray<SlicePad> pads;
    juce::OwnedArray<juce::TextButton> bankBtns;
    int bank = 0;
    juce::ComboBox baseBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> baseAtt;
    ParamToggle fine { proc.apvts, "sliceFine", fr ("Découpe fine"), Palette::peach,
                       fr ("Coupes plus serrées et plus précises (tranches très courtes autorisées, détection des petites attaques)") };
    juce::TextButton myChops { fr ("Mes chops") };
    juce::TextButton tapBtn { fr ("✂ Taper mes chops") };
    juce::TextButton tapStop { fr ("■ Stop") };
    ParamToggle choke { proc.apvts, "sliceChoke", fr ("Une tranche à la fois"), Palette::peach,
                        fr ("Comme une MPC : une nouvelle tranche coupe la précédente (fondu très court). Plus propre pour jouer au clavier.") };
    LabeledKnob chopAtk  { proc.apvts, "sliceAtk", "Attaque", Palette::peach, fr ("Début de chaque chop : court = punchy (par défaut), long = entrée douce, enlève les clics") };
    LabeledKnob chopRel  { proc.apvts, "sliceRel", fr ("Relâche"), Palette::peach, fr ("Fin du chop quand tu relâches : court = sec (par défaut), long = le son s'éteint doucement") };
    LabeledKnob chopVel  { proc.apvts, "sliceVel", fr ("Vélocité"), Palette::peach, fr ("À 0 %, tous les coups ont le même volume ; vers 100 %, plus tu tapes fort, plus c'est fort") };
    LabeledKnob chopTone { proc.apvts, "sliceTone", "Couleur", Palette::peach, fr ("Vers la gauche : plus sombre (passe-bas, style lo-fi). Vers la droite : plus fin (enlève les basses). Au centre : son d'origine") };
    ParamToggle chopLevel { proc.apvts, "sliceLevel", fr ("Volumes égalisés"), Palette::peach,
                            fr ("Tous les chops au même volume : les faibles montent, les trop forts baissent (±12 dB au plus)") };
    bool tapWas = false;
    void refreshTap()
    {
        const bool on = proc.isTapChopping();
        tapBtn.setButtonText (on ? fr ("✂ Couper (Espace)") : fr ("✂ Taper mes chops"));
        tapBtn.setColour (juce::TextButton::buttonColourId, on ? juce::Colour (0xffff4d6d).withAlpha (0.6f) : Palette::mint.withAlpha (0.25f));
        if (tapWas && ! on) proc.stopTapChop();   // arrivé au bout (ou Stop) : on valide
        tapStop.setVisible (on);
        if (on != tapWas) resized();
        tapWas = on;
    }
    int lastSliceMode = -1;
    juce::TextButton zoneAll { fr ("Zone : tout") }, zoneZoom { fr ("Zoom zone") }, zoomAllBtn { fr ("Dézoomer") },
                     nudgeL { juce::String::fromUTF8 ("◀") }, nudgeR { juce::String::fromUTF8 ("▶") };
    juce::Rectangle<int> hint, infoArea, recCard, soundRow, soundLabel;
    juce::String lastInfo;
    int lastV = -1, lastPat = -1;
};
