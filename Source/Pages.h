#pragma once

#include "Components.h"
#include "Presets.h"
#include <set>
#include <map>
#include "Moods.h"
#include "SlicerUI.h"

// Presets : demande avant de remplacer le son en cours (case « Ne plus afficher ce message »),
// et fenêtre pour nommer un preset à sauver
namespace presetui
{
    inline void confirmLoad (std::function<void()> go)
    {
        if (library::settings().getBoolValue ("presetNoConfirm", false)) { go(); return; }
        auto* w = new juce::AlertWindow (fr ("Charger ce preset ?"),
                                         fr ("Le son actuel est remplacé et Variant repart de ses réglages de lancement : couches, VST, synthé, effets et réglages que tu n'as pas sauvés seront perdus."),
                                         juce::AlertWindow::NoIcon);
        auto tb = std::make_shared<juce::ToggleButton> (fr ("Ne plus afficher ce message"));
        tb->setSize (320, 26);
        w->addCustomComponent (tb.get());
        w->addButton (fr ("Continuer"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton (fr ("Annuler"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([go, tb] (int r)
        {
            if (r != 1) return;
            if (tb->getToggleState()) { library::settings().setValue ("presetNoConfirm", true); library::settings().saveIfNeeded(); }
            go();
        }), true);
    }

    inline void askSave (VariantProcessor& proc, std::function<void (const juce::File&)> saved = {})
    {
        auto* w = new juce::AlertWindow (fr ("Sauver mon preset"), fr ("Nom du preset (sons des couches + VST + synthé + effets) :"), juce::AlertWindow::NoIcon);
        w->addTextEditor ("n", fr ("Mon preset"));
        w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton (fr ("Annuler"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        auto* pp = &proc;
        w->enterModalState (true, juce::ModalCallbackFunction::create ([pp, w, saved] (int res)
        {
            if (res != 1) return;
            const auto f = pp->saveFullPreset (w->getTextEditorContents ("n"));
            if (saved && f.existsAsFile()) saved (f);
        }), true);
    }
}

// Page qui défile verticalement (contenu plus haut que la fenêtre)
class ScrollPage : public juce::Component
{
public:
    explicit ScrollPage (juce::Component& content, int contentHeight) : inner (content), height (contentHeight)
    {
        view.setViewedComponent (&inner, false);
        view.setScrollBarsShown (true, false);
        view.setScrollBarThickness (8);
        view.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, Palette::line.brighter (0.3f));
        addAndMakeVisible (view);
    }
    void resized() override
    {
        view.setBounds (getLocalBounds());
        inner.setSize (getWidth() - (height > getHeight() ? 12 : 0), juce::jmax (height, getHeight()));
    }
    void setContentHeight (int h) { height = h; resized(); }

private:
    juce::Viewport view;
    juce::Component& inner;
    int height;
};

//==============================================================================
// Cartes déplaçables : ordre mémorisé + petites flèches ▲ ▼ en haut à droite de chaque carte
struct CardMover
{
    static std::vector<int>& order (const juce::String& key, int n)
    {
        static std::map<juce::String, std::vector<int>> all;
        auto it = all.find (key);
        if (it != all.end() && (int) it->second.size() == n) return it->second;
        std::vector<int> a; for (int i = 0; i < n; ++i) a.push_back (i);
        juce::StringArray t; t.addTokens (library::settings().getValue (key), ",", "");
        if (t.size() == n)
        {
            std::vector<int> b; std::vector<bool> seen ((size_t) n, false); bool ok = true;
            for (auto& s : t) { const int v = s.getIntValue(); if (v < 0 || v >= n || seen[(size_t) v]) ok = false; else { seen[(size_t) v] = true; b.push_back (v); } }
            if (ok) a = b;
        }
        return all[key] = a;
    }
    static void move (const juce::String& key, int n, int block, int dir)
    {
        auto& o = order (key, n);
        int at = 0; for (int i = 0; i < n; ++i) if (o[(size_t) i] == block) at = i;
        const int to = juce::jlimit (0, n - 1, at + dir);
        if (to == at) return;
        std::swap (o[(size_t) at], o[(size_t) to]);
        juce::StringArray t; for (int v : o) t.add (juce::String (v));
        library::settings().setValue (key, t.joinIntoString (","));
    }
    static juce::Rectangle<int> arrow (juce::Rectangle<int> card, bool up)
    {
        const int right = card.getRight() - 14;
        return { right - (up ? 52 : 26), card.getY() + 8, 24, 20 };
    }
    // -1 = monter, +1 = descendre, 0 = rien
    static int hit (juce::Rectangle<int> card, juce::Point<int> p)
    {
        if (card.isEmpty()) return 0;
        if (arrow (card, true).contains (p)) return -1;
        if (arrow (card, false).contains (p)) return 1;
        return 0;
    }
    static void paint (juce::Graphics& g, juce::Rectangle<int> card)
    {
        if (card.isEmpty()) return;
        for (int up = 0; up < 2; ++up)
        {
            const auto a = arrow (card, up == 1).toFloat();
            g.setColour (Palette::panel2.withAlpha (0.9f)); g.fillRoundedRectangle (a, 5.0f);
            g.setColour (Palette::dim);
            juce::Path p;
            const auto c = a.getCentre();
            if (up == 1) p.addTriangle (c.x - 5, c.y + 3, c.x + 5, c.y + 3, c.x, c.y - 4);
            else p.addTriangle (c.x - 5, c.y - 3, c.x + 5, c.y - 3, c.x, c.y + 4);
            g.fillPath (p);
        }
    }
};

// Les deux flèches sous forme de composant (au-dessus des cartes qui ont leurs propres boutons)
class CardArrows : public juce::Component
{
public:
    std::function<void (int)> onMove;
    void place (juce::Rectangle<int> card)
    {
        const auto a = CardMover::arrow (card, true), b = CardMover::arrow (card, false);
        setBounds (a.getUnion (b));
        toFront (false);
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        for (int up = 0; up < 2; ++up)
        {
            const auto a = (up == 1 ? r.removeFromLeft (24) : r.removeFromRight (24)).toFloat();
            g.setColour (Palette::panel2.withAlpha (0.9f)); g.fillRoundedRectangle (a, 5.0f);
            g.setColour (Palette::dim);
            juce::Path p;
            const auto c = a.getCentre();
            if (up == 1) p.addTriangle (c.x - 5, c.y + 3, c.x + 5, c.y + 3, c.x, c.y - 4);
            else p.addTriangle (c.x - 5, c.y - 3, c.x + 5, c.y - 3, c.x, c.y + 4);
            g.fillPath (p);
        }
    }
    void mouseUp (const juce::MouseEvent& e) override { if (onMove) onMove (e.x < getWidth() / 2 ? -1 : 1); }
};

//==============================================================================
// Carte d'une couche : gros bouton on/off, nom, note racine, mini forme d'onde, dépôt de fichiers
class LayerChip : public juce::Component, public juce::FileDragAndDropTarget, public juce::SettableTooltipClient
{
public:
    LayerChip (VariantProcessor& p, int index) : proc (p), layer (index)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip (fr ("Clique pour régler cette couche · glisse un ou plusieurs sons dessus"));
    }
    std::function<void (int)> onSelect;

    void paint (juce::Graphics& g) override
    {
        const bool sel = proc.selectedLayer == layer;
        const auto file = proc.getLayerFile (layer);
        const bool loaded = file != juce::File();
        const bool on = proc.getParam (VariantProcessor::layerId ("L", layer) + "On") > 0.5f;
        const juce::Colour cols[] = { Palette::blue, Palette::mint, Palette::pink, Palette::lavender };
        const auto accent = cols[layer % 4];

        auto r = getLocalBounds().toFloat().reduced (1.0f);
        if (sel || over || dropping)
        {
            g.setColour (accent.withAlpha (dropping ? 0.35f : 0.18f));
            g.fillRoundedRectangle (r.expanded (1.0f), 14.0f);
        }
        g.setColour (sel ? accent.withAlpha (0.16f) : Palette::panel);
        g.fillRoundedRectangle (r, 13.0f);
        g.setColour (sel || dropping ? accent : Palette::line);
        g.drawRoundedRectangle (r, 13.0f, sel ? 1.6f : 1.0f);

        // bouton on / off
        const auto pw = powerRect();
        g.setColour (on && loaded ? accent : Palette::panel2);
        g.fillEllipse (pw);
        g.setColour (on && loaded ? accent.darker (0.3f) : Palette::line.brighter (0.3f));
        g.drawEllipse (pw, 1.2f);
        {
            juce::Path icon;
            const auto c = pw.getCentre();
            const float rad = pw.getWidth() * 0.26f;
            icon.addCentredArc (c.x, c.y, rad, rad, 0.0f, 0.75f, juce::MathConstants<float>::twoPi - 0.75f, true);
            icon.startNewSubPath (c.x, c.y - rad - 1.5f);
            icon.lineTo (c.x, c.y - rad * 0.2f);
            g.setColour (on && loaded ? (Palette::light ? juce::Colours::white : Palette::bg) : Palette::dim);
            g.strokePath (icon, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        drawVolume (g, accent, on && loaded);
        r = r.withHeight (64.0f);
        auto txt = r.withTrimmedLeft (pw.getRight() - r.getX() + 8.0f).reduced (0.0f, 6.0f);
        g.setColour (Palette::text);
        g.setFont (uiFont (14.0f, juce::Font::bold));
        g.drawText (fr ("Couche ") + juce::String (layer + 1), txt.removeFromTop (18.0f), juce::Justification::centredLeft, true);
        g.setColour (loaded ? Palette::text.withAlpha (0.8f) : Palette::dim);
        g.setFont (uiFont (12.5f));
        g.drawText (loaded ? file.getFileNameWithoutExtension() : fr ("Glisse un son ici"), txt.removeFromTop (17.0f), juce::Justification::centredLeft, true);

        if (loaded)
        {
            // mini forme d'onde
            auto snd = proc.getSound();
            if (snd != nullptr && snd->layers[(size_t) layer].valid)
            {
                const auto& b = snd->layers[(size_t) layer].source;
                auto wr = txt.withHeight (juce::jmax (8.0f, txt.getHeight())).reduced (0.0f, 1.0f);
                const int cols2 = (int) wr.getWidth() / 3;
                const int len = b.getNumSamples();
                float peak = juce::jmax (1.0e-6f, b.getMagnitude (0, len));
                for (int i = 0; i < cols2; ++i)
                {
                    const int s0 = (int) ((juce::int64) i * len / cols2), s1 = (int) ((juce::int64) (i + 1) * len / cols2);
                    const float m = b.getMagnitude (0, s0, juce::jmax (1, s1 - s0)) / peak;
                    const float hh = juce::jmax (1.0f, m * wr.getHeight());
                    g.setColour (accent.withAlpha (on ? 0.8f : 0.3f));
                    g.fillRoundedRectangle (wr.getX() + (float) i * 3.0f, wr.getCentreY() - hh * 0.5f, 2.0f, hh, 1.0f);
                }
            }
            g.setColour (Palette::dim);
            g.setFont (uiFont (12.0f));
            const int root = (int) proc.getParam (VariantProcessor::rootId (layer));
            g.drawText (theory::noteName (root), r.reduced (10.0f, 6.0f).removeFromTop (18.0f), juce::Justification::topRight);
        }
    }

    void mouseEnter (const juce::MouseEvent&) override { over = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { over = false; overVol = false; repaint(); }
    void mouseMove (const juce::MouseEvent& e) override
    {
        const bool v = volRect().expanded (2.0f, 5.0f).contains (e.position);
        if (v != overVol) { overVol = v; repaint(); }
        setMouseCursor (v ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::PointingHandCursor);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        volDrag = ! e.mods.isPopupMenu() && volRect().expanded (2.0f, 5.0f).contains (e.position);
        if (volDrag) { if (auto* p = volParam()) p->beginChangeGesture(); mouseDrag (e); }
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! volDrag) return;
        const auto vr = volRect();
        const float pos = juce::jlimit (0.0f, 1.0f, (e.position.x - vr.getX()) / vr.getWidth());
        float db = -36.0f + pos * 48.0f;
        if (std::abs (db) < 0.8f && ! e.mods.isAltDown()) db = 0.0f;   // aimanté sur 0 dB
        if (auto* p = volParam()) p->setValueNotifyingHost (p->convertTo0to1 (db));
        repaint();
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (volRect().expanded (2.0f, 5.0f).contains (e.position)) if (auto* p = volParam()) { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (0.0f)); p->endChangeGesture(); repaint(); }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (volDrag) { if (auto* p = volParam()) p->endChangeGesture(); volDrag = false; return; }
        if (! e.mods.isPopupMenu() && soloRect().expanded (2.0f).contains (e.position)) { toggleSolo(); return; }
        if (e.mods.isPopupMenu()) { if (onSelect) onSelect (layer); layermenu::show (proc, layer, [safe = juce::Component::SafePointer<juce::Component> (this)] { if (safe != nullptr) safe->repaint(); }); return; }
        if (powerRect().expanded (4.0f).contains (e.position))
        {
            const auto id = VariantProcessor::layerId ("L", layer) + "On";
            proc.setParam (id, proc.getParam (id) > 0.5f ? 0.0f : 1.0f);
            repaint();
            return;
        }
        if (onSelect) onSelect (layer);
    }

    bool isInterestedInFileDrag (const juce::StringArray& f) override
    {
        for (auto& s : f) if (VariantProcessor::isSupportedFile (juce::File (s))) return true;
        return false;
    }
    void fileDragEnter (const juce::StringArray&, int, int) override { dropping = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropping = false; repaint(); }
    void filesDropped (const juce::StringArray& f, int, int) override
    {
        dropping = false;
        proc.loadFiles (f, layer);
        if (onSelect) onSelect (layer);
    }

private:
    juce::Rectangle<float> powerRect() const { return { 10.0f, 33.0f - 15.0f, 30.0f, 30.0f }; }
    juce::Rectangle<float> volRect() const  { return { 12.0f, (float) getHeight() - 15.0f, (float) getWidth() - 92.0f, 8.0f }; }
    juce::Rectangle<float> soloRect() const { return { (float) getWidth() - 34.0f, (float) getHeight() - 20.0f, 24.0f, 17.0f }; }
    juce::RangedAudioParameter* volParam() const { return proc.apvts.getParameter (VariantProcessor::layerId ("L", layer) + "Vol"); }
    inline static int soloed = -1;                     // couche jouée seule (les autres coupées)
    inline static std::array<float, maxLayers> soloSaved {};   // états on/off avant le solo

    void drawVolume (juce::Graphics& g, juce::Colour accent, bool active)
    {
        // volume de la couche, juste sous le bouton on/off : barre à glisser (double-clic = 0 dB) + bouton Solo
        const auto vr = volRect();
        const float db = proc.getParam (VariantProcessor::layerId ("L", layer) + "Vol");
        const float pos = juce::jlimit (0.0f, 1.0f, (db + 36.0f) / 48.0f);
        g.setColour (Palette::panel2); g.fillRoundedRectangle (vr, 4.0f);
        g.setColour (accent.withAlpha (active ? 0.85f : 0.35f)); g.fillRoundedRectangle (vr.withWidth (juce::jmax (8.0f, vr.getWidth() * pos)), 4.0f);
        const float zx = vr.getX() + vr.getWidth() * 0.75f;   // repère 0 dB
        g.setColour (Palette::text.withAlpha (0.5f)); g.fillRect (zx - 0.5f, vr.getY() - 2.0f, 1.0f, vr.getHeight() + 4.0f);
        g.setColour (Palette::text.withAlpha (volDrag || overVol ? 1.0f : 0.7f)); g.setFont (uiFont (11.0f, juce::Font::bold));
        g.drawText ((db > 0.05f ? "+" : "") + juce::String (db, 1) + " dB", juce::Rectangle<float> (vr.getRight() + 4.0f, vr.getY() - 4.0f, 52.0f, 16.0f), juce::Justification::centredLeft);
        const auto sr2 = soloRect();
        const bool solo = soloed == layer;
        g.setColour (solo ? Palette::peach : Palette::panel2); g.fillRoundedRectangle (sr2, 4.0f);
        g.setColour (solo ? Palette::peach : Palette::line); g.drawRoundedRectangle (sr2, 4.0f, 1.0f);
        g.setColour (solo ? Palette::bg : Palette::dim); g.setFont (uiFont (11.0f, juce::Font::bold));
        g.drawText ("S", sr2, juce::Justification::centred);
    }
    void toggleSolo()
    {
        auto on = [this] (int l) { return VariantProcessor::layerId ("L", l) + "On"; };
        if (soloed == layer)   // on rend leur état aux autres couches
        {
            for (int l = 0; l < maxLayers; ++l) if (l != layer) proc.setParam (on (l), soloSaved[(size_t) l]);
            soloed = -1;
        }
        else
        {
            if (soloed < 0) for (int l = 0; l < maxLayers; ++l) soloSaved[(size_t) l] = proc.getParam (on (l));
            for (int l = 0; l < maxLayers; ++l) proc.setParam (on (l), l == layer ? 1.0f : 0.0f);
            soloed = layer;
        }
        if (auto* p = getParentComponent()) p->repaint();
    }

    VariantProcessor& proc;
    int layer;
    bool over = false, dropping = false, overVol = false, volDrag = false;
};
//==============================================================================
// Réglages d'une couche (recréés quand on change de couche)
class LayerControls : public juce::Component, private juce::Timer
{
public:
    LayerControls (VariantProcessor& p, int l)
        : proc (p), layer (l),
          on    (p.apvts, VariantProcessor::layerId ("L", l) + "On", fr ("Couche active"), Palette::blue, fr ("Allume / éteint cette couche")),
          vol   (p.apvts, VariantProcessor::layerId ("L", l) + "Vol", "Volume", Palette::blue, fr ("Volume de cette couche")),
          pan   (p.apvts, VariantProcessor::layerId ("L", l) + "Pan", "Pan", Palette::blue, fr ("Position gauche / droite")),
          tune  (p.apvts, VariantProcessor::layerId ("L", l) + "Tune", "Transpo", Palette::blue, fr ("Décale cette couche en demi-tons (ex. +12 = une octave au-dessus)")),
          fine  (p.apvts, VariantProcessor::layerId ("L", l) + "Fine", "Finesse", Palette::blue, fr ("Accordage fin de la couche")),
          delay (p.apvts, VariantProcessor::layerId ("L", l) + "Delay", "Retard", Palette::blue, fr ("Décale le départ de la couche (quelques ms = superposition plus naturelle)"))
    {
        for (auto* c : std::initializer_list<juce::Component*> { &on, &vol, &pan, &tune, &fine, &delay, &rootLabel, &rootSlider, &clear, &link, &loop, &loopBpm, &detachBtn, &susLoop, &loopXf, &stretch, &arpChord })
            addAndMakeVisible (c);

        rootLabel.setText (fr ("Note racine"), juce::dontSendNotification);
        rootLabel.setJustificationType (juce::Justification::centredRight);
        rootLabel.setColour (juce::Label::textColourId, Palette::dim);
        rootLabel.setFont (uiFont (12.5f));
        rootSlider.setSliderStyle (juce::Slider::IncDecButtons);
        rootSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 52, 26);
        rootSlider.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
        rootSlider.setColour (juce::Slider::textBoxTextColourId, Palette::peach);
        rootSlider.setTooltip (fr ("La note à laquelle ce son est joué tel quel. Les sons de la bibliothèque sont en C5."));
        rootAtt = std::make_unique<APVTS::SliderAttachment> (p.apvts, VariantProcessor::rootId (l), rootSlider);
        rootSlider.onRightClick = [this, l] { hostmenu::show (rootSlider, VariantProcessor::rootId (l)); };

        clear.setButtonText (fr ("Retirer"));
        clear.onClick = [this] { proc.clearLayer (layer); };
        detach::makeButton (detachBtn, [this] { if (auto& f = detach::open()) { auto* pp = &proc; const int l2 = layer; f ([pp, l2] { return std::make_unique<LayerControls> (*pp, l2); }, fr ("Couche ") + juce::String (layer + 1), 860, 150); } });
        addChildComponent (sf2Box);
        sf2Box.setTooltip (fr ("Preset de la SoundFont (comme le Fruity SoundFont Player) : change d'instrument ici"));
        sf2Box.onChange = [this]
        {
            const int i = sf2Box.getSelectedItemIndex();
            const auto& idx = proc.sf2Indices[(size_t) layer];
            if (juce::isPositiveAndBelow (i, (int) idx.size()) && idx[(size_t) i] != proc.sf2Preset[(size_t) layer])
                proc.loadSoundFontPreset (proc.getLayerFile (layer), idx[(size_t) i], layer);
        };
        refreshSf2();
        startTimerHz (4);
    }

    void paint (juce::Graphics& g) override
    {
        const juce::Colour cols[] = { Palette::blue, Palette::mint, Palette::pink, Palette::lavender };
        drawCard (g, getLocalBounds(), fr ("COUCHE ") + juce::String (layer + 1), cols[layer % 4]);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14, 10);
        auto top = r.removeFromTop (22);
        top.removeFromLeft (80);
        on.setBounds (top.removeFromLeft (120));
        top.removeFromLeft (4);
        link.setBounds (top.removeFromLeft (124));
        top.removeFromLeft (4);
        loop.setBounds (top.removeFromLeft (88));
        stretch.setBounds (top.removeFromLeft (90));
        detachBtn.setBounds (top.removeFromRight (28).withSizeKeepingCentre (26, 22));
        top.removeFromRight (6);
        clear.setBounds (top.removeFromRight (92).withSizeKeepingCentre (92, 22));
        top.removeFromRight (8);
        sf2Area = top.removeFromRight (juce::jmin (190, top.getWidth()));
        const bool isSf2 = proc.sf2Preset[(size_t) layer] >= 0;
        sf2Box.setBounds (sf2Area.withSizeKeepingCentre (190, 24));
        rootSlider.setBounds (sf2Area.removeFromRight (100).withSizeKeepingCentre (100, 24));
        rootLabel.setBounds (sf2Area);
        sf2Box.setVisible (isSf2); rootSlider.setVisible (! isSf2); rootLabel.setVisible (! isSf2);
        r.removeFromTop (4);
        const int w = r.getWidth() / 7;
        for (auto* k : { &vol, &pan, &tune, &fine, &delay, &loopBpm }) k->setBounds (r.removeFromLeft (w));
        auto lc = r;
        susLoop.setBounds (lc.removeFromTop (22));
        arpChord.setBounds (lc.removeFromTop (22));   // mode arpégiateur (one-shots d'arpège)
        loopXf.setBounds (lc);
    }

private:
    VariantProcessor& proc;
    int layer;
    ParamToggle on;
    ParamToggle link { proc.apvts, "linkTune", fr ("Accordage lié"), Palette::blue,
                       fr ("Activé : changer la transpo ou la finesse d'une couche change aussi les autres couches du même écart") };
    ParamToggle loop { proc.apvts, VariantProcessor::layerId ("L", layer) + "Loop", fr ("Boucle"), Palette::peach,
                       fr ("Pour les arpèges / loops en .wav : la boucle reste calée sur le tempo et la position de FL, et se transpose sans changer de vitesse. En accord, seule la note la plus grave la joue (réglable dans « Mélange des couches »).") };
    LabeledKnob loopBpm { proc.apvts, VariantProcessor::layerId ("L", layer) + "LoopBpm", "BPM boucle", Palette::peach,
                          fr ("Tempo d'origine de la boucle (Auto = lu dans le nom du fichier, sinon deviné avec la durée)") };
    ParamToggle arpChord { proc.apvts, VariantProcessor::layerId ("L", layer) + "ArpChord", fr ("Arpégiateur"), Palette::peach,
                           fr ("Pour les one-shots d'arpège : le motif suit tout de suite le tempo de FL (clavier, piano roll, accords), et quand tu tiens un accord chaque pas prend la note suivante de l'accord, comme un arpégiateur de synthé (au lieu de plusieurs arpèges identiques en même temps). Activé tout seul pour les sons qui sont des arpèges (nom avec arp / seq, ou notes régulières détectées).") };
    ParamToggle stretch { proc.apvts, VariantProcessor::layerId ("L", layer) + "Stretch", fr ("Stretch"), Palette::peach,
                          fr ("Pour tes one-shots enregistrés à 140 BPM (ou au BPM écrit dans le nom, ou réglé dans « BPM boucle ») : le son suit le tempo de FL et chaque note garde la même durée (une note aiguë n'est plus plus rapide, une grave plus lente) -> les accords d'arpèges restent ensemble. Les notes sont calculées en haute qualité en arrière-plan.") };
    ParamToggle susLoop { proc.apvts, VariantProcessor::layerId ("L", layer) + "SLoop", fr ("Loop points"), Palette::mint,
                          fr ("Comme les loop points de FL : le son tourne en boucle entre les 2 repères verts (à glisser sur la forme d'onde) tant que la note est tenue. Parfait pour faire tenir un one-shot à l'infini.") };
    LabeledKnob loopXf { proc.apvts, VariantProcessor::layerId ("L", layer) + "SLXf", fr ("Fondu"), Palette::mint, fr ("Fondu enchaîné à la jonction de la boucle (plus haut = boucle plus douce, sans clic)") };
    LabeledKnob vol, pan, tune, fine, delay;
    juce::Label rootLabel;
    KnobSlider rootSlider;
    std::unique_ptr<APVTS::SliderAttachment> rootAtt;
    juce::TextButton clear, detachBtn;
    juce::ComboBox sf2Box;
    juce::Rectangle<int> sf2Area;
    int shownPreset = -2;
    void refreshSf2()
    {
        const int pr = proc.sf2Preset[(size_t) layer];
        if (pr == shownPreset) return;
        shownPreset = pr;
        sf2Box.clear (juce::dontSendNotification);
        if (pr >= 0)
        {
            sf2Box.addItemList (proc.sf2Names[(size_t) layer], 1);
            const auto& idx = proc.sf2Indices[(size_t) layer];
            for (int i = 0; i < (int) idx.size(); ++i) if (idx[(size_t) i] == pr) sf2Box.setSelectedItemIndex (i, juce::dontSendNotification);
        }
        resized();
    }
    void timerCallback() override { refreshSf2(); }
};

//==============================================================================
// Des VST (Serum, Zenology…) ouverts DANS Variant, jusqu'à 8 en couches : chacun joue les mêmes notes
// (clavier, accords, arpège, piano roll), passe dans les effets de Variant, et a ses réglages comme une couche.
class HostedRow : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    HostedRow (VariantProcessor& p, int s) : proc (p), slot (s)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &pick, &openWin, &power, &vol, &pan, &tr, &fine, &dly, &removeBtn })
            addAndMakeVisible (c);
        pick.setColour (juce::TextButton::buttonColourId, Palette::lavender.withAlpha (0.25f));
        pick.setTooltip (fr ("Choisis un VST3 (Serum, Zenology, Diva…) : il joue tes notes, tes accords et ton arpège, et son son passe dans les effets de Variant.\nTu peux aussi glisser un .vst3 ou un preset .vstpreset sur cette ligne."));
        pick.onClick = [this] { showPluginMenu(); };
        openWin.setTooltip (fr ("Ouvre l'interface du plugin pour choisir un preset et modifier le son. Fenêtre devant = ton clavier d'ordinateur joue des notes."));
        openWin.onClick = [this] { open(); };
        power.setClickingTogglesState (true);
        power.setColour (juce::TextButton::buttonOnColourId, Palette::lavender);
        power.setTooltip (fr ("Allumer / couper ce plugin"));
        power.onClick = [this] { h().enabled.store (power.getToggleState()); };
        auto bar = [this] (juce::Slider& sl, double lo, double hi, double def, double stepV, const juce::String& tip, std::function<juce::String (double)> txt, std::function<void (float)> set)
        {
            sl.setSliderStyle (juce::Slider::LinearBar);
            sl.setTextBoxStyle (juce::Slider::TextBoxLeft, true, 0, 0);
            sl.setRange (lo, hi, stepV);
            sl.setDoubleClickReturnValue (true, def);
            sl.setColour (juce::Slider::trackColourId, Palette::lavender.withAlpha (0.35f));
            sl.setColour (juce::Slider::textBoxTextColourId, Palette::text);
            sl.setTooltip (tip + "\n" + fr ("Double-clic : par défaut"));
            sl.textFromValueFunction = txt;
            sl.onValueChange = [&sl, set] { set ((float) sl.getValue()); };
        };
        // clic droit = automation dans FL (paramètres « VST n : … »)
        const auto hid = "h" + juce::String (slot + 1);
        hostmenu::tag (power, hid + "On");
        vol.onRightClick = [this, hid] { hostmenu::show (vol, hid + "Vol"); };
        pan.onRightClick = [this, hid] { hostmenu::show (pan, hid + "Pan"); };
        tr.onRightClick = [this, hid] { hostmenu::show (tr, hid + "Tr"); };
        fine.onRightClick = [this, hid] { hostmenu::show (fine, hid + "Fine"); };
        dly.onRightClick = [this, hid] { hostmenu::show (dly, hid + "Dly"); };
        bar (vol, -36.0, 12.0, 0.0, 0.1, fr ("Volume du plugin"), [] (double v) { return "Vol " + juce::String (v, 1) + " dB"; }, [this] (float v) { h().volume.store (juce::Decibels::decibelsToGain (v, -60.0f)); });
        bar (pan, -1.0, 1.0, 0.0, 0.01, "Pan", [] (double v) { return std::abs (v) < 0.01 ? juce::String ("Pan C") : (v < 0 ? "Pan G" + juce::String ((int) std::round (-v * 100)) : "Pan D" + juce::String ((int) std::round (v * 100))); }, [this] (float v) { h().panPos.store (v); });
        bar (tr, -24.0, 24.0, 0.0, 1.0, fr ("Transposition (demi-tons)"), [] (double v) { return "Transpo " + juce::String ((int) v) + " st"; }, [this] (float v) { h().transpose.store (v); });
        bar (fine, -100.0, 100.0, 0.0, 1.0, fr ("Finesse (cents) : envoyée en pitch bend. Le glide et le slide de Variant passent aussi par le pitch bend : règle la même plage ici (menu du plugin) et dans le plugin (±2 par défaut dans la plupart des synthés, mets ±24 pour de grands slides)"), [] (double v) { return "Fin " + juce::String ((int) v) + " ct"; }, [this] (float v) { h().fineCents.store (v); });
        bar (dly, 0.0, 500.0, 0.0, 1.0, fr ("Retard du plugin (ms), comme le retard d'une couche"), [] (double v) { return "Retard " + juce::String ((int) v) + " ms"; }, [this] (float v) { h().delayMs.store (v); });
        removeBtn.setTooltip (fr ("Retirer ce plugin de Variant"));
        removeBtn.onClick = [this] { h().unload(); if (onChanged) onChanged(); };
        refresh();
    }
    std::function<void()> onChanged;
    std::function<void()> onWindowOpened;

    HostedInstrument& h() { return proc.hostedSlots[(size_t) slot]; }

    // les réglages bougent aussi par l'automation de FL : l'affichage suit
    void syncValues()
    {
        if (! h().isLoaded() || ! isShowing()) return;
        auto follow = [] (juce::Slider& s, double v) { if (! s.isMouseButtonDown() && std::abs (s.getValue() - v) > 1.0e-3) s.setValue (v, juce::dontSendNotification); };
        follow (vol, juce::Decibels::gainToDecibels (h().volume.load(), -36.0f));
        follow (pan, h().panPos.load());
        follow (tr, h().transpose.load());
        follow (fine, h().fineCents.load());
        follow (dly, h().delayMs.load());
        if (power.getToggleState() != h().enabled.load()) power.setToggleState (h().enabled.load(), juce::dontSendNotification);
    }

    void refresh()
    {
        const bool loaded = h().isLoaded();
        pick.setButtonText (loaded ? h().name + juce::String::fromUTF8 ("  ▾") : (slot == 0 ? fr ("Ouvrir un plugin…") : fr ("+ Ajouter un plugin…")));
        for (juce::Component* c : { (juce::Component*) &openWin, (juce::Component*) &power, (juce::Component*) &vol, (juce::Component*) &pan,
                                    (juce::Component*) &tr, (juce::Component*) &fine, (juce::Component*) &dly, (juce::Component*) &removeBtn })
            c->setVisible (loaded);
        power.setToggleState (h().enabled.load(), juce::dontSendNotification);
        vol.setValue (juce::Decibels::gainToDecibels (h().volume.load(), -36.0f), juce::dontSendNotification);
        pan.setValue (h().panPos.load(), juce::dontSendNotification);
        tr.setValue (h().transpose.load(), juce::dontSendNotification);
        fine.setValue (h().fineCents.load(), juce::dontSendNotification);
        dly.setValue (h().delayMs.load(), juce::dontSendNotification);
        resized(); repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f, 2.0f);
        g.setColour (dropping ? Palette::lavender.withAlpha (0.18f) : Palette::panel2.withAlpha (h().isLoaded() ? 0.6f : 0.25f));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (Palette::lavender.withAlpha (h().isLoaded() ? 0.9f : 0.4f));
        g.setFont (uiFont (11.0f, juce::Font::bold));
        g.drawText (juce::String (slot + 1), getLocalBounds().removeFromLeft (20), juce::Justification::centred);
        if (status.isNotEmpty())
        {
            g.setColour (status.startsWith ("!") ? Palette::pink : Palette::dim); g.setFont (uiFont (12.0f));
            g.drawFittedText (status.trimCharactersAtStart ("!"), statusArea, juce::Justification::centredLeft, 1);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (4, 5);
        r.removeFromLeft (18);
        pick.setBounds (r.removeFromLeft (170)); r.removeFromLeft (4);
        statusArea = {};
        if (! h().isLoaded()) { statusArea = r.withTrimmedLeft (6); return; }
        openWin.setBounds (r.removeFromLeft (80)); r.removeFromLeft (4);
        power.setBounds (r.removeFromLeft (38)); r.removeFromLeft (6);
        removeBtn.setBounds (r.removeFromRight (28)); r.removeFromRight (6);
        const int w = (r.getWidth() - 4 * 4) / 5;
        for (auto* s : { &vol, &pan, &tr, &fine, &dly }) { s->setBounds (r.removeFromLeft (w)); r.removeFromLeft (4); }
    }

    bool isInterestedInFileDrag (const juce::StringArray& f) override
    {
        for (auto& s : f) if (s.endsWithIgnoreCase (".vst3") || s.endsWithIgnoreCase (".component") || s.endsWithIgnoreCase (".vstpreset")) return true;
        return false;
    }
    void fileDragEnter (const juce::StringArray&, int, int) override { dropping = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropping = false; repaint(); }
    void filesDropped (const juce::StringArray& f, int, int) override
    {
        dropping = false;
        for (auto& s : f)
        {
            const juce::File file (s);
            if (s.endsWithIgnoreCase (".vst3") || s.endsWithIgnoreCase (".component")) { loadPlugin (file); return; }
            if (s.endsWithIgnoreCase (".vstpreset"))
            {
                if (! h().isLoaded()) { status = fr ("!Ouvre d'abord le plugin de ce preset"); repaint(); return; }
                status = h().loadPresetFile (file) ? fr ("Preset chargé : ") + file.getFileNameWithoutExtension() : fr ("!Ce preset n'est pas pour ") + h().name;
                resized(); repaint(); return;
            }
        }
    }

    void open() { h().openWindow(); if (onWindowOpened) onWindowOpened(); }

private:
    VariantProcessor& proc;
    int slot;
    juce::TextButton pick, openWin { fr ("Fenêtre") }, removeBtn { juce::String::fromUTF8 ("✕") };
    PopupSafeTextButton power { "ON" };
    KnobSlider vol, pan, tr, fine, dly;
    juce::String status;
    juce::Rectangle<int> statusArea;
    bool dropping = false;
    std::unique_ptr<juce::FileChooser> chooser;

    void showPluginMenu()
    {
        const auto files = HostedInstrument::installedPlugins();
        juce::PopupMenu m;
        m.addSectionHeader (fr ("Tes plugins VST3"));
        std::map<juce::String, juce::PopupMenu> byLetter;
        for (int i = 0; i < files.size(); ++i)
        {
            const auto nm = files[i].getFileNameWithoutExtension();
            byLetter[nm.substring (0, 1).toUpperCase()].addItem (1 + i, nm, true, files[i].getFullPathName() == h().path);
        }
        if (files.size() <= 30) for (int i = 0; i < files.size(); ++i) m.addItem (1 + i, files[i].getFileNameWithoutExtension(), true, files[i].getFullPathName() == h().path);
        else for (auto& [l, sub] : byLetter) m.addSubMenu (l, sub);
        if (files.isEmpty()) m.addItem (-1, fr ("Aucun plugin trouvé dans ") + HostedInstrument::defaultPluginFolder().getFullPathName(), false);
        m.addSeparator();
        m.addItem (9000, fr ("Choisir un fichier .vst3…"));
        m.addItem (-2, fr ("(les plugins VST2 seulement, ou en 32 bits comme Purity, ne peuvent pas s'ouvrir dans Variant)"), false);
        if (h().isLoaded())
        {
            // le glide, le slide et la finesse de Variant passent par le pitch bend du plugin
            juce::PopupMenu bend;
            for (int v : { 2, 7, 12, 24, 36, 48 })
                bend.addItem (9100 + v, juce::String::fromUTF8 ("±") + juce::String (v) + fr (" demi-tons"), true, h().bendRange.load() == v);
            m.addSeparator();
            m.addSubMenu (fr ("Plage du pitch bend du plugin (glide / slide)"), bend);
        }
        juce::Component::SafePointer<HostedRow> self (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&pick), [self, files] (int r)
        {
            if (self == nullptr || r <= 0) return;
            if (r > 9100 && r <= 9200) { self->h().bendRange.store (r - 9100); return; }
            if (r == 9000)
            {
                self->chooser = std::make_unique<juce::FileChooser> (fr ("Choisir un plugin VST3"), HostedInstrument::defaultPluginFolder(), HostedInstrument::pluginWildcard());
                self->chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectDirectories,
                    [self] (const juce::FileChooser& fc) { if (self != nullptr && fc.getResult().exists()) self->loadPlugin (fc.getResult()); });
                return;
            }
            if (juce::isPositiveAndBelow (r - 1, files.size())) self->loadPlugin (files[r - 1]);
        });
    }

    void loadPlugin (const juce::File& f)
    {
        status = fr ("Ouverture de ") + f.getFileNameWithoutExtension() + fr ("… (certains plugins comme Zenology mettent jusqu'à 1 minute)");
        resized(); repaint();
        juce::Component::SafePointer<HostedRow> self (this);
        juce::Timer::callAfterDelay (80, [self, f]   // le message s'affiche avant que le plugin démarre
        {
            if (self == nullptr) return;
            self->h().load (f, [self] (bool ok, const juce::String& msg)
            {
                if (self == nullptr) return;
                self->status = ok ? juce::String() : "!" + msg;
                self->refresh();
                if (self->onChanged) self->onChanged();
                if (ok) self->open();   // on ouvre tout de suite sa fenêtre pour choisir le son
            });
        });
    }
};

class HostedBar : public juce::Component, private juce::Timer
{
public:
    explicit HostedBar (VariantProcessor& p) : proc (p)
    {
        for (int s = 0; s < VariantProcessor::numHosted; ++s)
        {
            auto* r = rows.add (new HostedRow (p, s));
            r->onChanged = [this] { layoutRows(); };
            r->onWindowOpened = [this] { if (onWindowOpened) onWindowOpened(); };
            addChildComponent (r);
        }
        layoutRows();
        startTimerHz (10);   // suit aussi l automation de FL
    }
    std::function<void()> onHeightChanged, onWindowOpened;
    static constexpr int rowH = 40, headerH = 30;

    int preferredHeight() const { return headerH + visibleRows() * rowH + 8; }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (r, 12.0f);
        g.setColour (anyLoaded() ? Palette::lavender.withAlpha (0.7f) : Palette::line);
        g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);
        auto t = getLocalBounds().reduced (14, 0).removeFromTop (headerH);
        g.setColour (Palette::lavender);
        g.fillRoundedRectangle (juce::Rectangle<float> (4.0f, 14.0f).withCentre ({ (float) t.getX() + 2.0f, (float) t.getCentreY() }), 2.0f);
        g.setColour (Palette::text.withAlpha (0.75f));
        g.setFont (uiFont (12.0f, juce::Font::bold).withExtraKerningFactor (0.08f));
        g.drawText (fr ("PLUGINS DANS VARIANT"), t.withTrimmedLeft (12), juce::Justification::centredLeft);
        g.setColour (Palette::dim); g.setFont (uiFont (11.5f));
        g.drawText (fr ("jusqu'à 8 VST en couches · jouent tes notes, accords et arpège · passent dans tes effets · fenêtre devant = ton clavier joue"),
                    t.withTrimmedLeft (190), juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 0).withTrimmedTop (headerH);
        for (auto* row : rows) if (row->isVisible()) row->setBounds (r.removeFromTop (rowH));
    }

private:
    VariantProcessor& proc;
    juce::OwnedArray<HostedRow> rows;
    std::array<int, VariantProcessor::numHosted> seen {};
    int lastVisible = -1;

    bool anyLoaded() const { for (auto& h : proc.hostedSlots) if (h.isLoaded()) return true; return false; }
    int visibleRows() const
    {
        // les plugins ouverts + une ligne libre pour en ajouter un
        int n = 0, firstFree = -1;
        for (int s = 0; s < VariantProcessor::numHosted; ++s) { if (proc.hostedSlots[(size_t) s].isLoaded()) ++n; else if (firstFree < 0) firstFree = s; }
        return n + (firstFree >= 0 ? 1 : 0);
    }
    void layoutRows()
    {
        bool freeShown = false;
        for (int s = 0; s < rows.size(); ++s)
        {
            const bool loaded = proc.hostedSlots[(size_t) s].isLoaded();
            const bool show = loaded || ! freeShown;
            if (! loaded && show) freeShown = true;
            rows[s]->setVisible (show);
            rows[s]->refresh();
        }
        resized();
        repaint();
        const int v = visibleRows();
        if (v != lastVisible) { lastVisible = v; if (onHeightChanged) onHeightChanged(); }
    }
    void timerCallback() override
    {
        bool changed = false;
        for (int s = 0; s < VariantProcessor::numHosted; ++s)
        {
            const int ver = proc.hostedSlots[(size_t) s].version.load();
            if (ver != seen[(size_t) s]) { seen[(size_t) s] = ver; changed = true; }
        }
        if (changed) layoutRows();
        for (auto* r : rows) r->syncValues();
    }
};

// « PRISE » : enregistre ce que tu joues (clavier d'ordi, clavier MIDI, piano roll) en MIDI et en WAV,
// puis tu glisses le fichier dans FL. Démarre à la première note, s'arrête quand tu recliques.
class TakeCard : public juce::Component, private juce::Timer
{
public:
    explicit TakeCard (VariantProcessor& p, bool arpTake = false) : proc (p), forArp (arpTake)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &recBtn, &clearBtn, &chipMidi, &chipWav }) addAndMakeVisible (c);
        recBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffff4d6d));
        recBtn.setTooltip (fr ("Rec : l'enregistrement démarre à la première note que tu joues (clavier d'ordi, clavier MIDI ou piano roll de FL). Reclique pour arrêter."));
        recBtn.onClick = [this]
        {
            const int s = proc.takeStatus();
            if (s == 1 || s == 2) proc.takeStop();
            else { proc.setTakeSource (forArp ? 1 : 0); proc.takeArm(); wavFile = midFile = juce::File(); }
            if (auto* e = findParentComponentOfClass<juce::AudioProcessorEditor>()) e->grabKeyboardFocus();   // le clavier d'ordi joue tout de suite
            refresh();
        };
        clearBtn.setTooltip (fr ("Efface la prise pour recommencer"));
        clearBtn.onClick = [this] { proc.takeClear(); wavFile = midFile = juce::File(); refresh(); };
        chipMidi.setTooltip (fr ("Glisse les notes que tu as jouées dans FL Studio (piano roll ou playlist)"));
        chipWav.setTooltip (fr ("Glisse le son enregistré (effets compris) dans FL Studio"));
        startTimerHz (10);
        refresh();
    }

    void paint (juce::Graphics& g) override
    {
        drawCard (g, getLocalBounds(), forArp ? fr ("PRISE DE L'ARPÈGE  ·  MIDI + WAV de ce que joue l'arpège") : fr ("PRISE  ·  enregistre ce que tu joues"), Palette::pink);
        g.setColour (proc.takeStatus() == 2 ? juce::Colour (0xffff4d6d) : Palette::dim);
        g.setFont (uiFont (12.0f, proc.takeStatus() == 2 ? juce::Font::bold : juce::Font::plain));
        g.drawFittedText (statusText(), statusArea, juce::Justification::centredLeft, 2, 1.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 6).withTrimmedTop (24);
        auto row = r.removeFromTop (30);
        recBtn.setBounds (row.removeFromLeft (juce::jmin (130, row.getWidth() / 2)));
        row.removeFromLeft (6);
        clearBtn.setBounds (row.removeFromLeft (80));
        row.removeFromLeft (8);
        statusArea = row;
        r.removeFromTop (6);
        auto chips = r.removeFromTop (30);
        const int cw = (chips.getWidth() - 6) / 2;
        chipMidi.setBounds (chips.removeFromLeft (cw));
        chips.removeFromLeft (6);
        chipWav.setBounds (chips);
    }

    static constexpr int preferredHeight = 108;

private:
    juce::String statusText() const
    {
        auto secs = [] (double s) { const int t = (int) s; return juce::String (t / 60) + ":" + juce::String (t % 60).paddedLeft ('0', 2); };
        switch (proc.takeStatus())
        {
            case 1:  return fr ("Prêt : joue une note pour démarrer");
            case 2:  return fr ("● Enregistrement  ") + secs (proc.takeSeconds());
            case 3:  return fr ("Prise de ") + secs (proc.takeSeconds()) + fr ("  ·  glisse le MIDI ou le WAV dans FL");
            default: return fr ("Clique sur Rec puis joue : MIDI + son, prêts à glisser dans FL");
        }
    }

    void refresh()
    {
        const int s = proc.takeStatus();
        recBtn.setToggleState (s == 1 || s == 2, juce::dontSendNotification);
        recBtn.setButtonText (s == 1 || s == 2 ? fr ("■ Stop") : fr ("● Rec"));
        clearBtn.setEnabled (s != 0);
        chipMidi.setAlpha (s == 3 ? 1.0f : 0.4f);
        chipWav.setAlpha (s == 3 ? 1.0f : 0.4f);
        repaint();
    }

    void timerCallback() override
    {
        const int s = proc.takeStatus();
        const int sig = s * 100000 + (int) (proc.takeSeconds() * 10.0);
        if (sig != lastSig) { lastSig = sig; refresh(); }
    }

    // un seul fichier par prise (glisser deux fois ne recrée pas de copie)
    juce::File getWav() { if (! wavFile.existsAsFile()) wavFile = proc.exportTakeWav(); return wavFile; }
    juce::File getMid() { if (! midFile.existsAsFile()) midFile = proc.exportTakeMidi(); return midFile; }

    VariantProcessor& proc;
    bool forArp = false;
    juce::TextButton recBtn { fr ("● Rec") }, clearBtn { fr ("Effacer") };
    juce::File wavFile, midFile;
    DragChip chipMidi { "MIDI .mid", Palette::lavender, [this] { return getMid(); } };
    DragChip chipWav  { "Audio .wav", Palette::pink, [this] { return getWav(); } };
    juce::Rectangle<int> statusArea;
    int lastSig = -1;
};

// Carte SAMPLE de la couche (inspirée du Sampler de FL Studio) : départ, longueur, fondus, points de boucle
// en millisecondes, ping-pong et traitements "précalculés" (inverser, normaliser, DC, polarité, stéréo)
class SampleCard : public juce::Component
{
public:
    SampleCard (VariantProcessor& p, int l) : proc (p), layer (l)
    {
        const juce::String id = VariantProcessor::layerId ("L", l);
        auto knob = [this, &id] (const char* suf, const juce::String& name, const juce::String& tip, bool asMs)
        {
            auto* k = knobs.add (new LabeledKnob (proc.apvts, id + suf, name, Palette::blue, tip));
            if (asMs)
                k->setTextFunction ([this] (double v)
                {
                    // position dans le son, en millisecondes (comme dans FL)
                    auto snd = proc.getSound();
                    if (snd == nullptr || ! snd->layers[(size_t) layer].valid) return juce::String ((int) (v * 100)) + " %";
                    const auto& ld = snd->layers[(size_t) layer];
                    return juce::String ((int) (v * ld.source.getNumSamples() / ld.rate * 1000.0)) + " ms";
                });
            addAndMakeVisible (k);
        };
        knob ("SmpStart", fr ("Départ"), fr ("Où le son commence (coupe le début)"), false);
        knob ("SmpLen",   fr ("Longueur"), fr ("Longueur gardée du son (coupe la fin)"), false);
        knob ("FadeIn",   fr ("Fondu in"), fr ("Montée douce au début du son"), false);
        knob ("FadeOut",  fr ("Fondu out"), fr ("Descente douce à la fin du son"), false);
        knob ("SLStart",  fr ("Début boucle"), fr ("Début de la boucle (en ms dans le son)"), true);
        knob ("SLEnd",    fr ("Fin boucle"), fr ("Fin de la boucle (en ms dans le son)"), true);
        knob ("SLXf",     fr ("Fondu boucle"), fr ("Fondu enchaîné à la jonction de la boucle"), false);
        auto toggle = [this, &id] (const char* suf, const juce::String& name, const juce::String& tip)
        { addAndMakeVisible (toggles.add (new ParamToggle (proc.apvts, id + suf, name, Palette::blue, tip))); };
        toggle ("SLoop",     fr ("Boucle"), fr ("Le son tourne en boucle entre début et fin de boucle tant que la note est tenue"));
        toggle ("PingPong",  fr ("Ping-pong"), fr ("La boucle va et vient (aller-retour) au lieu de repartir au début"));
        toggle ("Reverse",   fr ("Inverser"), fr ("Joue le son à l'envers"));
        toggle ("Normalize", fr ("Normal."), fr ("Met le son au niveau maximum sans saturer"));
        toggle ("RemoveDC",  "DC", fr ("Supprime le décalage continu (évite les clics)"));
        toggle ("Polarity",  fr ("Polarité"), fr ("Inverse la polarité (utile quand deux couches s'annulent)"));
        toggle ("SwapSt",    juce::String::fromUTF8 ("G ↔ D"), fr ("Échange les canaux gauche et droit"));

        // « Couper le blanc » : le départ saute le silence au début du fichier (mp3, sons mal exportés)
        addAndMakeVisible (trimBtn);
        trimBtn.setColour (juce::TextButton::buttonColourId, Palette::blue.withAlpha (0.22f));
        trimBtn.setTooltip (fr ("Enlève le blanc avant le début du son (fichiers mp3, sons mal exportés) : le Départ se place juste avant l'attaque. "
                                "Reclique pour revenir au début du fichier."));
        trimBtn.onClick = [this]
        {
            const auto id = VariantProcessor::layerId ("L", layer) + "SmpStart";
            if (proc.getParam (id) > 0.0005f) proc.setParam (id, 0.0f);   // déjà coupé : on revient au début
            else if (! proc.trimLayerStart (layer)) trimBtn.setButtonText (fr ("Pas de blanc"));
            juce::Component::SafePointer<SampleCard> sp (this);
            juce::Timer::callAfterDelay (1500, [sp] { if (sp != nullptr) sp->trimBtn.setButtonText (fr ("Couper le blanc")); });
        };
    }

    void paint (juce::Graphics& g) override { drawCard (g, getLocalBounds(), fr ("SAMPLE  ·  comme le Sampler de FL"), Palette::blue); }

    void resized() override
    {
        trimBtn.setBounds (getLocalBounds().reduced (10, 4).removeFromTop (20).removeFromRight (118));
        auto r = getLocalBounds().reduced (10, 6).withTrimmedTop (20);
        // carte étroite (à côté de la forme d'onde) : interrupteurs sur 2 rangées, potards en dessous
        const int perRow = 4, th = 22, tw = r.getWidth() / perRow;
        for (int i = 0; i < toggles.size(); ++i)
            toggles[i]->setBounds (r.getX() + (i % perRow) * tw, r.getY() + (i / perRow) * (th + 2), tw - 2, th);
        r.removeFromTop (2 * (th + 2) + 4);
        const int kw = r.getWidth() / juce::jmax (1, knobs.size());
        for (auto* k : knobs) k->setBounds (r.removeFromLeft (kw));
    }

private:
    VariantProcessor& proc;
    int layer;
    juce::OwnedArray<LabeledKnob> knobs;
    juce::OwnedArray<ParamToggle> toggles;
    juce::TextButton trimBtn { fr ("Couper le blanc") };
};

//==============================================================================
// Onglet SON
//==============================================================================
//==============================================================================
// Enveloppe graphique façon FL Studio : on attrape les points (délai, attaque, tenue, déclin/sustain, relâche)
// et les petits ronds au milieu des segments pour courber (tension). Double-clic sur un rond = courbe droite.
class EnvelopeGraph : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit EnvelopeGraph (VariantProcessor& p) : proc (p)
    {
        setTooltip (fr ("Glisse les points pour régler l'enveloppe, les petits ronds pour la courber. Double-clic sur un rond : courbe droite."));
        startTimerHz (15);
    }

    void paint (juce::Graphics& g) override
    {
        auto a = area();
        g.setColour (Palette::panel2.withAlpha (0.55f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
        g.setColour (Palette::line.withAlpha (0.5f));
        for (int i = 1; i < 4; ++i) g.drawHorizontalLine ((int) (a.getY() + a.getHeight() * i / 4.0f), a.getX(), a.getRight());

        const auto pts = points();
        juce::Path curve, fill;
        curve.startNewSubPath (pts[0]);
        auto seg = [&] (juce::Point<float> p0, juce::Point<float> p1, float ten, bool rising)
        {
            for (int k = 1; k <= 24; ++k)
            {
                const float x = (float) k / 24.0f;
                const float s = rising ? FlEnvelope::shape (x, ten) : FlEnvelope::shape (x, -ten);
                curve.lineTo (p0.x + (p1.x - p0.x) * x, p0.y + (p1.y - p0.y) * s);
            }
        };
        curve.lineTo (pts[1]);
        seg (pts[1], pts[2], proc.getParam ("envAtkTen"), true);
        curve.lineTo (pts[3]);
        seg (pts[3], pts[4], proc.getParam ("envDecTen"), false);
        curve.lineTo (pts[5]);
        seg (pts[5], pts[6], proc.getParam ("envRelTen"), false);
        fill = curve;
        fill.lineTo (pts[6].x, a.getBottom()); fill.lineTo (pts[0].x, a.getBottom()); fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (Palette::mint.withAlpha (0.35f), 0, a.getY(), Palette::mint.withAlpha (0.03f), 0, a.getBottom(), false));
        g.fillPath (fill);
        g.setColour (Palette::mint);
        g.strokePath (curve, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // repère "note relâchée"
        g.setColour (Palette::pink.withAlpha (0.5f));
        g.drawVerticalLine ((int) pts[5].x, a.getY(), a.getBottom());
        g.setFont (uiFont (10.5f));
        g.drawText (fr ("relâche"), juce::Rectangle<float> (pts[5].x + 3, a.getY(), 60, 12), juce::Justification::left);

        // points
        const char* names[] = { "", "DEL", "ATT", "HOLD", "DEC/SUS", "", "REL" };
        for (int i = 1; i < 7; ++i)
        {
            if (i == 5) continue;
            const bool hot = i == hover || i == drag;
            g.setColour (hot ? Palette::text : Palette::mint);
            g.fillEllipse (juce::Rectangle<float> (hot ? 11.0f : 9.0f, hot ? 11.0f : 9.0f).withCentre (pts[i]));
            if (hot)
            {
                g.setFont (uiFont (11.0f, juce::Font::bold));
                g.drawText (juce::String (names[i]) + "  " + valueText (i), juce::Rectangle<float> (pts[i].x - 60, pts[i].y - 22, 120, 14).constrainedWithin (getLocalBounds().toFloat()), juce::Justification::centred);
            }
        }
        for (int j = 0; j < 3; ++j)
        {
            const auto c = tensionHandle (j);
            const bool hot = (10 + j) == hover || (10 + j) == drag;
            g.setColour (hot ? Palette::text : Palette::mint.withAlpha (0.8f));
            g.drawEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (c), 1.6f);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override { const int h = hit (e.position); if (h != hover) { hover = h; repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        frozenZoom = 0.0f;
        frozenZoom = zoom();
        drag = hit (e.position);
        startPos = e.position;
        if (drag >= 10) startVal = proc.getParam (tenId (drag - 10));
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag < 0) return;
        auto a = area();
        if (drag >= 10)
        {
            const float dy = (e.position.y - startPos.y) / a.getHeight() * 2.5f;
            proc.setParam (tenId (drag - 10), juce::jlimit (-1.0f, 1.0f, startVal + (drag == 10 ? -dy : dy)));
        }
        else
        {
            const auto pts = points();
            const float x = e.position.x - pts[(size_t) drag - 1].x;   // largeur du segment depuis le point précédent
            const auto& sg = segs()[(size_t) drag - 1];
            if (drag == 4)
            {
                const float y = juce::jlimit (0.0f, 1.0f, 1.0f - (e.position.y - a.getY()) / a.getHeight());
                proc.setParam ("sustain", y);
            }
            if (drag != 5) proc.setParam (sg.id, juce::jlimit (sg.minMs, sg.maxMs, msForWidth (x, sg)));
        }
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override { drag = -1; repaint(); }
    void mouseDoubleClick (const juce::MouseEvent& e) override { const int h = hit (e.position); if (h >= 10) proc.setParam (tenId (h - 10), 0.0f); repaint(); }

private:
    struct Seg { const char* id; float share, maxMs, minMs; };
    static const std::array<Seg, 6>& segs()
    {
        static const std::array<Seg, 6> s { { { "envDelay", 0.12f, 2000.0f, 0.0f }, { "attack", 0.16f, 2000.0f, 0.0f }, { "envHold", 0.12f, 2000.0f, 0.0f },
                                              { "decay", 0.20f, 4000.0f, 1.0f }, { "", 0.12f, 0.0f, 0.0f }, { "release", 0.24f, 5000.0f, 1.0f } } };
        return s;
    }
    juce::Rectangle<float> area() const { return getLocalBounds().toFloat().reduced (12.0f, 16.0f); }
    float rawWidth (float ms, const Seg& s) const { return s.maxMs <= 0.0f ? s.share * area().getWidth() : std::sqrt (juce::jmax (0.0f, ms) / s.maxMs) * s.share * area().getWidth(); }
    // zoom horizontal : la courbe remplit la case (figé pendant qu'on tire un point)
    float zoom() const
    {
        if (drag >= 0 && frozenZoom > 0.0f) return frozenZoom;
        float total = 0.0f;
        const char* ids[] = { "envDelay", "attack", "envHold", "decay", "", "release" };
        for (int i = 0; i < 6; ++i) total += rawWidth (i == 4 ? 0.0f : proc.getParam (ids[i]), segs()[(size_t) i]);
        return juce::jlimit (1.0f, 4.0f, area().getWidth() * 0.95f / juce::jmax (1.0f, total));
    }
    float widthFor (float ms, const Seg& s) const { return rawWidth (ms, s) * zoom(); }
    float msForWidth (float w, const Seg& s) const { const float r = juce::jmax (0.0f, w) / zoom() / (s.share * area().getWidth()); return r * r * s.maxMs; }
    static juce::String tenId (int j) { return j == 0 ? "envAtkTen" : (j == 1 ? "envDecTen" : "envRelTen"); }

    std::array<juce::Point<float>, 7> points() const
    {
        auto a = area();
        const float sus = proc.getParam ("sustain");
        std::array<juce::Point<float>, 7> p;
        float x = a.getX();
        p[0] = { x, a.getBottom() };
        x += widthFor (proc.getParam ("envDelay"), segs()[0]); p[1] = { x, a.getBottom() };
        x += widthFor (proc.getParam ("attack"), segs()[1]);   p[2] = { x, a.getY() };
        x += widthFor (proc.getParam ("envHold"), segs()[2]);  p[3] = { x, a.getY() };
        x += widthFor (proc.getParam ("decay"), segs()[3]);    p[4] = { x, a.getBottom() - sus * a.getHeight() };
        x += widthFor (0.0f, segs()[4]);                        p[5] = { x, p[4].y };
        x += widthFor (proc.getParam ("release"), segs()[5]);  p[6] = { x, a.getBottom() };
        return p;
    }

    juce::Point<float> tensionHandle (int j) const
    {
        const auto p = points();
        const int a = j == 0 ? 1 : (j == 1 ? 3 : 5), b = a + 1;
        const float ten = proc.getParam (tenId (j));
        const float s = j == 0 ? FlEnvelope::shape (0.5f, ten) : FlEnvelope::shape (0.5f, -ten);
        return { (p[(size_t) a].x + p[(size_t) b].x) * 0.5f, p[(size_t) a].y + (p[(size_t) b].y - p[(size_t) a].y) * s };
    }

    int hit (juce::Point<float> m) const
    {
        const auto p = points();
        for (int i = 6; i >= 1; --i) if (i != 5 && p[(size_t) i].getDistanceFrom (m) < 9.0f) return i;
        for (int j = 0; j < 3; ++j) if (tensionHandle (j).getDistanceFrom (m) < 8.0f) return 10 + j;
        return -1;
    }

    juce::String valueText (int i) const
    {
        auto ms = [] (float v) { return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " s" : juce::String ((int) v) + " ms"; };
        switch (i)
        {
            case 1: return ms (proc.getParam ("envDelay"));
            case 2: return ms (proc.getParam ("attack"));
            case 3: return ms (proc.getParam ("envHold"));
            case 4: return ms (proc.getParam ("decay")) + " / " + juce::String ((int) (proc.getParam ("sustain") * 100)) + " %";
            case 6: return ms (proc.getParam ("release"));
            default: return {};
        }
    }

    void timerCallback() override
    {
        float sig = 0.0f;
        for (auto* id : { "envDelay", "attack", "envHold", "decay", "sustain", "release", "envAtkTen", "envDecTen", "envRelTen" }) sig = sig * 1.37f + proc.getParam (id);
        if (sig != lastSig) { lastSig = sig; repaint(); }
    }

    VariantProcessor& proc;
    int hover = -1, drag = -1;
    juce::Point<float> startPos;
    float startVal = 0.0f, lastSig = -1.0f, frozenZoom = 0.0f;
};

class SoundContent : public juce::Component, private juce::Timer
{
public:
    friend class VariantEditor;   // visite guidée : montre les cartes
    explicit SoundContent (VariantProcessor& p) : proc (p)
    {
        for (int i = 0; i < maxLayers; ++i)
        {
            auto* c = chips.add (new LayerChip (proc, i));
            c->onSelect = [this] (int l) { selectLayer (l); };
            addAndMakeVisible (c);
        }
        addAndMakeVisible (morphBtn);
        addMouseListener (&cardMouse, true);   // cases déplaçables par leur titre
        morphBtn.setButtonText (juce::String::fromUTF8 ("✦  ") + fr ("Métamorphose") + juce::String::fromUTF8 ("  ▾"));
        morphBtn.setColour (juce::TextButton::buttonColourId, Palette::pink.withAlpha (0.55f));
        morphBtn.setColour (juce::TextButton::textColourOffId, Palette::text);
        morphBtn.setTooltip (fr ("Métamorphose : transforme le one-shot de la couche choisie en un autre son (nappe, cristal, grave, reverse swell, pluck, grains, chorus, résonateur, bande). Chaque clic donne un résultat différent, toujours à partir de l'original. Le nouveau son est rangé dans le dossier Métamorphoses de ta banque."));
        morphBtn.onClick = [this] { showMorphMenu(); };
        for (auto* bt : { &morphUndoBtn, &morphRedoBtn })
        {
            addAndMakeVisible (*bt);
            bt->setColour (juce::TextButton::buttonColourId, Palette::pink.withAlpha (0.25f));
            bt->setColour (juce::TextButton::textColourOffId, Palette::text);
        }
        morphUndoBtn.setButtonText (juce::String::fromUTF8 ("◀  ") + (i18n::current().load() == i18n::FR ? juce::String::fromUTF8 ("Annuler") : fr ("Annuler (défaire)")));
        morphRedoBtn.setButtonText (juce::String::fromUTF8 ("▶  ") + fr ("Refaire"));
        morphUndoBtn.setTooltip (fr ("Revient au son d'avant la dernière métamorphose (plusieurs fois de suite pour remonter plus loin)"));
        morphRedoBtn.setTooltip (fr ("Remet la métamorphose que tu viens d'annuler"));
        morphUndoBtn.onClick = [this] { proc.undoMorph (proc.selectedLayer); updateMorphButtons(); };
        morphRedoBtn.onClick = [this] { proc.redoMorph (proc.selectedLayer); updateMorphButtons(); };
        updateMorphButtons();
        startTimerHz (4);
        for (auto* c : std::initializer_list<juce::Component*> { &waveform, &hostedBar, &takeCard, &autoButton, &oneShot, &layerMode, &layerOrder, &loopChord, &layerHuman,
                                                                &attack, &decay, &sustain, &release, &envDelay, &envHold, &envGraph, &velocity, &gain, &tune,
                                                                &varOn, &varStyle, &varAmount, &varCount, &velTone, &regen, &dots,
                                                                &glideMode, &glideTime, &glideCurve, &slideIn })
            addAndMakeVisible (c);

        libraryButton.setButtonText (fr ("Bibliothèque de sons"));
        libraryButton.setColour (juce::TextButton::buttonColourId, Palette::blue.withAlpha (0.18f));
        libraryButton.onClick = [this] { if (onOpenLibrary) onOpenLibrary(); };
        // clavier d'ordinateur en gamme : chaque touche (lettres, chiffres, ponctuation des mêmes rangées) = une note de la gamme choisie
        addAndMakeVisible (scaleKbButton);
        scaleKbButton.setTooltip (fr ("Clavier en gamme : toutes les touches de ton clavier d'ordinateur jouent uniquement les notes de la gamme choisie. "
                                      "Rangée du haut (A Z E R…) = octave de base, chiffres = une octave au-dessus, rangées du dessous = une et deux octaves en dessous. Flèches haut / bas = octave."));
        scaleKbButton.onClick = [this] { showScaleKbMenu(); };
        refreshScaleKb();
        autoButton.setButtonText (juce::String::fromUTF8 ("Auto  ▾"));
        autoButton.setColour (juce::TextButton::buttonColourId, Palette::peach.withAlpha (0.18f));
        autoButton.setTooltip (fr ("Layers auto (2, 3 ou 4 sons complémentaires piochés dans ta bibliothèque) ou réglages auto (note, volumes)"));
        autoButton.onClick = [this]
        {
            juce::PopupMenu m;
            m.addSectionHeader (fr ("Layers auto  ·  garde la couche 1 et ajoute"));
            m.addItem (2, fr ("2 sons au total"));
            m.addItem (3, fr ("3 sons au total"));
            m.addItem (4, fr ("4 sons au total"));
            m.addSeparator();
            m.addItem (10, fr ("Réglages auto (note + volumes)"));
            juce::Component::SafePointer<SoundContent> self (this);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&autoButton), [safe = self] (int r)
            {
                if (safe == nullptr || r == 0) return;
                if (r == 10) safe->proc.autoSetupAll();
                else safe->proc.autoLayers (r);
                safe->selectLayer (safe->proc.selectedLayer);
            });
        };
        regen.setTooltip (fr ("Recalcule une nouvelle série de variantes à partir des mêmes sons"));
        regen.onClick = [this] { proc.regenerateVariations (true); };

        selectLayer (proc.selectedLayer);
    }

    std::function<void()> onOpenLibrary;

    void selectLayer (int l)
    {
        proc.selectedLayer = juce::jlimit (0, maxLayers - 1, l);
        layerControls = std::make_unique<LayerControls> (proc, proc.selectedLayer);
        addAndMakeVisible (*layerControls);
        sampleCard = std::make_unique<SampleCard> (proc, proc.selectedLayer);
        addAndMakeVisible (*sampleCard);
        waveform.layerChanged();
        resized();
        repaint();
        for (auto* c : chips) c->repaint();
    }

    void soundChanged() { waveform.setSound (proc.getSound()); for (auto* c : chips) c->repaint(); repaint(); }
    int currentHeight() const { return contentHeight - 50 + juce::jmax (hostedBar.preferredHeight(), TakeCard::preferredHeight) + (showExtraLayers() ? extraRowH : 0); }
    void wireHosted (std::function<void()> heightChanged, std::function<void()> windowOpened)
    {
        onContentHeight = heightChanged;
        hostedBar.onHeightChanged = [this, heightChanged] { resized(); if (heightChanged) heightChanged(); };
        hostedBar.onWindowOpened = windowOpened;
    }
    // couches 5 à 8 : une 2e rangée apparaît dès que les 4 premières sont prises (ou qu'une couche 5-8 est utilisée)
    static constexpr int extraRowH = 92;
    bool showExtraLayers() const
    {
        bool firstFull = true, extraUsed = proc.selectedLayer >= 4;
        for (int l = 0; l < maxLayers; ++l)
        {
            const bool loaded = proc.getLayerFile (l) != juce::File();
            if (l < 4 && ! loaded) firstFull = false;
            if (l >= 4 && loaded) extraUsed = true;
        }
        return firstFull || extraUsed;
    }

    // bouton « Clavier en gamme » : son texte suit le mode du clavier, la tonalité et la gamme (réglées aussi ailleurs)
    void refreshScaleKb()
    {
        const bool on = proc.kbMode == 4;
        const auto txt = on ? fr ("Clavier : ") + theory::keyNames()[proc.getKey()] + " " + theory::scaleNames()[proc.getScale()].upToFirstOccurrenceOf (" (", false, false)
                            : fr ("Clavier en gamme");
        if (scaleKbButton.getButtonText() != txt + juce::String::fromUTF8 ("  ▾")) scaleKbButton.setButtonText (txt + juce::String::fromUTF8 ("  ▾"));
        scaleKbButton.setColour (juce::TextButton::buttonColourId, on ? Palette::mint.withAlpha (0.38f) : Palette::mint.withAlpha (0.14f));
    }

    void showScaleKbMenu()
    {
        const bool on = proc.kbMode == 4;
        juce::PopupMenu m, keys, scales;
        const auto kn = theory::keyNames(), sn = theory::scaleNames();
        for (int i = 0; i < kn.size(); ++i) keys.addItem (100 + i, kn[i], true, i == proc.getKey());
        for (int i = 0; i < sn.size(); ++i) scales.addItem (200 + i, sn[i], true, i == proc.getScale());
        m.addItem (1, fr ("Jouer en gamme avec le clavier d'ordinateur"), true, on);
        m.addSeparator();
        m.addSubMenu (fr ("Tonalité : ") + kn[proc.getKey()], keys);
        m.addSubMenu (fr ("Gamme : ") + sn[proc.getScale()], scales);
        juce::Component::SafePointer<SoundContent> self (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&scaleKbButton), [self, on] (int r)
        {
            if (self == nullptr || r == 0) return;
            auto& pr = self->proc;
            if (r == 1) pr.kbMode = on ? 1 : 4;   // coupé : on revient aux notes normales (comme avant)
            else
            {
                // choisir une tonalité ou une gamme active le clavier en gamme (même tonalité / gamme que le reste du plugin)
                if (r >= 200) pr.setParam ("scale", (float) (r - 200));
                else if (r >= 100) pr.setParam ("key", (float) (r - 100));
                pr.kbMode = 4;
            }
            self->refreshScaleKb();
            if (self->onKeyboardMode) self->onKeyboardMode();
        });
    }
    std::function<void()> onKeyboardMode;   // l'éditeur reprend le clavier (focus) et met à jour la page Accords

    void tick()
    {
        refreshScaleKb();
        if (const bool ex = showExtraLayers(); ex != extraShown)
        {
            extraShown = ex;
            resized(); repaint();
            if (onContentHeight) onContentHeight();
        }
        waveform.tick();
        auto snd = proc.getSound();
        const int l = proc.selectedLayer;
        const bool on = proc.getParam ("varOn") > 0.5f;
        const int count = (snd != nullptr && snd->layers[(size_t) l].valid) ? (int) snd->layers[(size_t) l].variants.size()
                                                                              : (int) proc.getParam ("varCount");
        dots.set (count, proc.getLastVariant (l), on);
        const int style = (int) proc.getParam ("varStyle");
        if (style != lastStyle) { lastStyle = style; repaint (varText); }
        if (++chipTick % 15 == 0) for (auto* c : chips) c->repaint();
    }

    static constexpr int contentHeight = 1356;

    void paint (juce::Graphics& g) override
    {
        auto info = infoArea;
        auto snd = proc.getSound();
        const int l = proc.selectedLayer;
        if (snd != nullptr && snd->layers[(size_t) l].valid)
        {
            const auto& ld = snd->layers[(size_t) l];
            const double secs = ld.source.getNumSamples() / ld.rate;
            juce::AttributedString as;
            as.append (proc.getLayerFile (l).getFileNameWithoutExtension(), uiFont (14.0f), Palette::text);
            as.append (fr ("   ·   ") + juce::String (secs, 2) + " s  ·  " + juce::String (ld.rate / 1000.0, 1) + " kHz  ·  "
                       + (ld.source.getNumChannels() > 1 ? fr ("stéréo") : fr ("mono")), uiFont (12.0f), Palette::dim);
            as.setJustification (juce::Justification::centredLeft);
            as.setWordWrap (juce::AttributedString::none);
            as.draw (g, info.toFloat());
        }
        else
        {
            g.setColour (Palette::dim);
            g.setFont (uiFont (13.0f));
            g.drawText (fr ("Couche vide : glisse un son, clique pour parcourir, ou choisis dans la bibliothèque"), info, juce::Justification::centredLeft);
        }

        drawCard (g, mixCard, fr ("MÉLANGE DES COUCHES & SORTIE"), Palette::peach);
        g.setColour (Palette::dim);
        g.setFont (uiFont (12.0f));
        g.drawFittedText (fr ("« Une note = une couche » : dans un accord de 3 notes avec 3 sons, chaque note joue son propre son "
                              "(marche aussi avec l'arpège). « Naturel » ajoute de petits décalages entre les couches."),
                          mixText, juce::Justification::topLeft, 4);

        drawCard (g, envCard, fr ("ENVELOPPE"), Palette::mint);
        drawCard (g, varCard, fr ("VARIATIONS À CHAQUE NOTE"), Palette::peach);

        static const char* desc[] = {
            "Micro-différences d'attaque, de timbre et de durée, comme un musicien qui rejoue la même note. La hauteur ne bouge pas.",
            "Petites textures tirées du son : scintillement à l'octave, halo grave, résonance cristal, doublage, écho inversé…",
            "Textures marquées + bégaiements, cloche, lo-fi : chaque note devient une petite surprise." };
        g.setColour (Palette::dim);
        g.setFont (uiFont (12.0f));
        g.drawFittedText (fr (desc[juce::jlimit (0, 2, lastStyle < 0 ? 0 : lastStyle)]), varText, juce::Justification::topLeft, 3, 1.0f);

        drawCard (g, glideCard, fr ("SLIDE / GLIDE"), Palette::blue);
        g.setColour (Palette::dim);
        g.setFont (uiFont (12.0f));
        g.drawFittedText (fr ("Legato : les notes slide du piano roll de FL marchent direct (une note qui en chevauche une autre fait glisser le son en cours sans le relancer, comme un one-shot de 808). "
                              "Toujours : chaque note glisse depuis la précédente. Dans le piano roll du plugin, une note marquée « Slide » glisse toujours."),
                          glideText, juce::Justification::topLeft, 4, 1.0f);

    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto chipRow = r.removeFromTop (84);   // + volume et solo sous chaque couche
        auto btns = chipRow.removeFromRight (170);
        // (la bibliothèque s ouvre avec le bouton « Banque de sons » en haut : plus de 2e bouton ici)
        btns.removeFromTop (4);
        autoButton.setBounds (btns.removeFromTop (34));
        btns.removeFromTop (8);
        scaleKbButton.setBounds (btns.removeFromTop (34));
        chipRow.removeFromRight (12);
        const int cw = (chipRow.getWidth() - 3 * 8) / 4;
        extraShown = showExtraLayers();
        juce::Rectangle<int> row2;
        if (extraShown) { r.removeFromTop (8); row2 = r.removeFromTop (extraRowH - 8).withX (chipRow.getX()).withWidth (chipRow.getWidth()); }
        for (int i = 0; i < chips.size(); ++i)
        {
            auto* c = chips[i];
            if (i < 4) { c->setBounds (chipRow.removeFromLeft (cw)); chipRow.removeFromLeft (8); }
            else { c->setVisible (extraShown); if (extraShown) { c->setBounds (row2.removeFromLeft (cw)); row2.removeFromLeft (8); } }
        }

        // cases déplaçables (glisser par le titre, ou clic droit sur le titre) : ordre mémorisé
        for (int bi = 0; bi < numBlocks; ++bi)
        {
            const int b = cards.order[(size_t) bi];
            r.removeFromTop (bi == 0 ? 10 : 12);
            auto rect = r.removeFromTop (blockHeight (b));
            cards.rects[(size_t) b] = rect;
            cards.handles[(size_t) b] = {};
            layoutBlock (b, rect);
        }
    }

    // cases : 0 couche, 1 enveloppe, 2 mélange, 3 variations, 4 slide, 5 son + sample, 6 plugins + prise
    enum { numBlocks = 7, blockSound = 5, blockHosted = 6 };
    int blockHeight (int b) const
    {
        static const int heights[] = { 140, 150, 150, 190, 158, 178 + 6 + 30 };
        return b == blockHosted ? juce::jmax (hostedBar.preferredHeight(), TakeCard::preferredHeight) : heights[b];
    }
    static std::vector<int> defaultBlockOrder()
    {
        // avant v3.21 : seulement les 5 cases du bas (« soundOrder ») -> on garde leur ordre sous le son et les plugins
        std::vector<int> o { blockSound, blockHosted, 0, 1, 2, 3, 4 };
        juce::StringArray t; t.addTokens (library::settings().getValue ("soundOrder"), ",", "");
        if (t.size() == 5)
        {
            std::vector<int> old; std::array<bool, 5> seen {};
            for (auto& s : t) { const int v = s.getIntValue(); if (v < 0 || v > 4 || seen[(size_t) v]) return o; seen[(size_t) v] = true; old.push_back (v); }
            o = { blockSound, blockHosted }; o.insert (o.end(), old.begin(), old.end());
        }
        return o;
    }
    void paintOverChildren (juce::Graphics& g) override { cards.paintOverlay (g); }

private:
    CardOrder cards { "son", numBlocks, defaultBlockOrder() };
    CardOrderMouse cardMouse { *this, cards, [this] { resized(); repaint(); } };
    void layoutBlock (int b, juce::Rectangle<int> rect)
    {
      if (b == blockSound)
      {
            // forme d'onde à gauche, réglages Sample (comme le Sampler de FL) à droite, puis la ligne Métamorphose / Jouer en entier
            auto r = rect;
            auto wr = r.removeFromTop (178);
            if (sampleCard)
            {
                sampleCard->setBounds (wr.removeFromRight (wr.getWidth() * 48 / 100));
                cards.handles[(size_t) b] = sampleCard->getBounds().withHeight (26);   // on l'attrape par le titre « SAMPLE »
            }
            wr.removeFromRight (10);
            waveform.setBounds (wr);
            r.removeFromTop (6);
            auto info = r.removeFromTop (30);
            oneShot.setBounds (info.removeFromRight (140));
            info.removeFromRight (8);
            morphBtn.setBounds (info.removeFromRight (200));   // bien visible, juste sous le son
            info.removeFromRight (4);
            morphRedoBtn.setBounds (info.removeFromRight (104));
            info.removeFromRight (4);
            morphUndoBtn.setBounds (info.removeFromRight (110));
            info.removeFromRight (8);
            infoArea = info;
            return;
      }
      if (b == blockHosted)
      {
            // moitié gauche : VST ouverts dans Variant · moitié droite : « Prise » (enregistre ce que tu joues en MIDI + WAV)
            auto hb = rect;
            takeCard.setBounds (hb.removeFromRight (hb.getWidth() / 2 - 4).withHeight (TakeCard::preferredHeight));
            hb.removeFromRight (8);
            hostedBar.setBounds (hb.withHeight (hostedBar.preferredHeight()));
            return;
      }
      if (b == 0) { if (layerControls) layerControls->setBounds (rect); return; }
      if (b == 1)
      {
        envCard = rect;
        {
            auto e = envCard.reduced (14, 8).withTrimmedTop (22);
            envGraph.setBounds (e.removeFromLeft (e.getWidth() * 2 / 5));
            e.removeFromLeft (10);
            const int kw = e.getWidth() / 6;
            for (auto* k : { &envDelay, &attack, &envHold, &decay, &sustain, &release }) k->setBounds (e.removeFromLeft (kw).reduced (1, 0));
        }
        return;
      }
      if (b == 2)
      {
        mixCard = rect;
        auto m = mixCard.reduced (14, 10).withTrimmedTop (22);
        // mélange + sortie dans la même carte : réglages de couches à gauche, 4 boutons à droite
        auto knobs = m.removeFromRight (300);
        const int kw = knobs.getWidth() / 4;
        for (auto* k : { &layerHuman, &velocity, &gain, &tune }) k->setBounds (knobs.removeFromLeft (kw));
        m.removeFromRight (8);
        auto combos = m.removeFromTop (48);
        layerMode.setBounds (combos.removeFromLeft (240));
        combos.removeFromLeft (8);
        layerOrder.setBounds (combos.removeFromLeft (120));
        combos.removeFromLeft (8);
        loopChord.setBounds (combos);
        m.removeFromTop (8);
        mixText = m;
        outCard = {};
        return;
      }
      if (b == 3)
      {
        varCard = rect;
        auto v = varCard.reduced (14, 10);
        auto title = v.removeFromTop (18);
        varOn.setBounds (title.removeFromRight (110).withSizeKeepingCentre (110, 22).translated (0, 2));
        v.removeFromTop (8);
        auto left = v.removeFromLeft (390);
        varStyle.setBounds (left.removeFromTop (32));
        left.removeFromTop (8);
        varText = left.removeFromTop (34);
        left.removeFromTop (6);
        auto bottom = left.removeFromTop (32);
        regen.setBounds (bottom.removeFromLeft (190));
        bottom.removeFromLeft (10);
        dots.setBounds (bottom);
        v.removeFromLeft (20);
        const int w = v.getWidth() / 3;
        for (auto* k : { &varAmount, &varCount, &velTone }) k->setBounds (v.removeFromLeft (w).reduced (2, 0));
        return;
      }
        glideCard = rect;
        auto gc = glideCard.reduced (14, 10).withTrimmedTop (24);
        auto gl = gc.removeFromLeft (390);
        glideMode.setBounds (gl.removeFromTop (32));
        gl.removeFromTop (8);
        glideText = gl;
        gc.removeFromLeft (20);
        const int gw = gc.getWidth() / 3;
        for (auto* k : { &glideTime, &glideCurve, &slideIn }) k->setBounds (gc.removeFromLeft (gw).reduced (2, 0));
    }

private:
    void layoutKnobs (juce::Rectangle<int> card, std::initializer_list<LabeledKnob*> knobs)
    {
        auto area = card.reduced (8).withTrimmedTop (22);
        const int w = area.getWidth() / (int) knobs.size();
        for (auto* k : knobs) k->setBounds (area.removeFromLeft (w).reduced (2, 0));
    }

    VariantProcessor& proc;

public:
    WaveformView waveform { proc };
    HostedBar hostedBar { proc };
    TakeCard takeCard { proc };
    juce::TextButton libraryButton, autoButton, scaleKbButton, morphBtn, morphUndoBtn, morphRedoBtn;
    void updateMorphButtons()
    {
        morphUndoBtn.setEnabled (proc.canUndoMorph (proc.selectedLayer));
        morphRedoBtn.setEnabled (proc.canRedoMorph (proc.selectedLayer));
    }
    void timerCallback() override { updateMorphButtons(); }
    void showMorphMenu()
    {
        const int layer = proc.selectedLayer;
        juce::PopupMenu m;
        const bool has = proc.getLayerFile (layer).existsAsFile();
        m.addSectionHeader (fr ("Métamorphose : couche ") + juce::String (layer + 1));
        if (! has) m.addItem (-1, fr ("(glisse d'abord un son dans cette couche)"), false);
        m.addItem (1, fr ("Au hasard"), has);
        m.addItem (2, fr ("Légère (garde le caractère)"), has);
        m.addItem (3, fr ("Moyenne"), has);
        m.addItem (4, fr ("Totale (méconnaissable)"), has);
        m.addSeparator();
        m.addItem (5, fr ("Revenir au son d'origine"), proc.canRestore (layer));
        juce::Component::SafePointer<SoundContent> self (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&morphBtn), [self, layer] (int r)
        {
            if (self == nullptr || r <= 0) return;
            if (r == 5) { self->proc.restoreLayer (layer); self->updateMorphButtons(); return; }
            const auto what = self->proc.destroyLayer (layer, r == 1 ? -1 : r - 2);
            self->updateMorphButtons();
            if (what.isNotEmpty()) self->morphBtn.setTooltip (fr ("Dernière métamorphose : ") + what + juce::String ("\n") + fr ("Reclique pour un autre résultat, ou « Annuler » pour revenir en arrière."));
        });
    }
    ParamToggle oneShot  { proc.apvts, "oneshot", fr ("Jouer en entier"), Palette::mint,
                           fr ("Activé : le son va jusqu'au bout même si tu relâches la note") };
    LabeledCombo layerMode  { fr ("Répartition des couches"), LayerRouter::modeNames(), &proc.apvts, "layerMode" };
    LabeledCombo layerOrder { "Ordre", { fr ("Grave → aigu"), fr ("Aigu → grave") }, &proc.apvts, "layerOrder" };
    LabeledCombo loopChord { fr ("Boucles en accord"), { fr ("Note la plus grave"), fr ("Toutes les notes") }, &proc.apvts, "loopChord" };
    LabeledKnob layerHuman  { proc.apvts, "layerHuman", "Naturel", Palette::peach,
                              fr ("Petits décalages de timing, d'accordage et de volume entre les couches à chaque note") };
    LabeledKnob attack   { proc.apvts, "attack",   fr ("Attaque"),  Palette::mint,     fr ("Temps de montée du son au début de la note") };
    LabeledKnob decay    { proc.apvts, "decay",    fr ("Déclin"),   Palette::mint,     fr ("Temps pour descendre jusqu'au niveau de maintien") };
    LabeledKnob sustain  { proc.apvts, "sustain",  fr ("Maintien"), Palette::mint,     fr ("Niveau tenu tant que la touche est enfoncée") };
    LabeledKnob release  { proc.apvts, "release",  fr ("Relâche"),  Palette::pink,     fr ("Temps d'extinction quand on relâche la touche") };
    LabeledKnob envDelay { proc.apvts, "envDelay", fr ("Délai"), Palette::mint, fr ("Attente avant que le son démarre") };
    LabeledKnob envHold  { proc.apvts, "envHold",  fr ("Tenue"), Palette::mint, fr ("Temps passé au maximum avant le déclin") };
    EnvelopeGraph envGraph { proc };
    LabeledKnob velocity { proc.apvts, "velocity", fr ("Vélocité"), Palette::lavender, fr ("À quel point la force de frappe change le volume") };
    LabeledKnob gain     { proc.apvts, "gain",     fr ("Volume"),   Palette::lavender, fr ("Volume de toutes les couches") };
    LabeledKnob tune     { proc.apvts, "tune",     fr ("Transpo"),  Palette::lavender, fr ("Transposition de toutes les couches") };
    ParamToggle varOn    { proc.apvts, "varOn", fr ("Activées"), Palette::peach, fr ("Chaque note joue une variante différente du son (round robin)") };
    SegmentedChoice varStyle { proc.apvts, "varStyle", { fr ("Réaliste"), "Texture", fr ("Expérimental") }, Palette::peach };
    LabeledKnob varAmount { proc.apvts, "varAmount", fr ("Intensité"), Palette::peach, fr ("À quel point les variantes s'éloignent de l'original") };
    LabeledKnob varCount  { proc.apvts, "varCount",  fr ("Variantes"), Palette::peach, fr ("Nombre de prises différentes pré-calculées (8 à 16 conseillé)") };
    LabeledKnob velTone   { proc.apvts, "velTone",   fr ("Vél. → timbre"), Palette::peach,
                            fr ("Notes jouées doucement = son plus sombre et attaque plus douce, pas seulement moins fort") };
    juce::TextButton regen { fr ("Nouvelles variations") };
    VariantDots dots;
    SegmentedChoice glideMode { proc.apvts, "glideMode", { fr ("Désactivé"), "Legato", fr ("Toujours") }, Palette::blue };
    LabeledKnob glideTime  { proc.apvts, "glideTime",  fr ("Temps"),  Palette::blue, fr ("Temps que met la note pour glisser jusqu'à sa hauteur") };
    LabeledKnob glideCurve { proc.apvts, "glideCurve", fr ("Courbe"), Palette::blue, fr ("0 = glissement régulier, 100 = arrive vite puis se pose doucement") };
    LabeledKnob slideIn    { proc.apvts, "slideIn",    fr ("Slide d'entrée"), Palette::blue,
                             fr ("Longueur du slide : chaque note démarre ce nombre de demi-tons plus haut/bas puis glisse vers sa note") };

private:
    juce::OwnedArray<LayerChip> chips;
    bool extraShown = false;
    std::function<void()> onContentHeight;
    std::unique_ptr<LayerControls> layerControls;
    std::unique_ptr<SampleCard> sampleCard;
    juce::Rectangle<int> infoArea, mixCard, mixText, envCard, outCard, varCard, varText, glideCard, glideText;
    int lastStyle = -1, chipTick = 0;
};

class SoundPage : public juce::Component
{
public:
    explicit SoundPage (VariantProcessor& p) : content (p), scroller (content, content.currentHeight())
    {
        addAndMakeVisible (scroller);
        content.wireHosted ([this] { scroller.setContentHeight (content.currentHeight()); }, [this] { if (onPluginWindow) onPluginWindow(); });
    }
    std::function<void()> onPluginWindow;   // une fenêtre de plugin vient de s'ouvrir (le clavier d'ordi passe en mode notes)
    void resized() override { scroller.setBounds (getLocalBounds()); }
    void tick() { content.tick(); }
    void soundChanged() { content.soundChanged(); }
    SoundContent content;

private:
    ScrollPage scroller;
};

//==============================================================================
// Panneau BIBLIOTHÈQUE (par-dessus la page)
//==============================================================================
class LibraryPanel : public juce::Component, private juce::ListBoxModel
{
public:
    explicit LibraryPanel (VariantProcessor& p) : proc (p)
    {
        title.setText (fr ("BIBLIOTHÈQUE DE SONS"), juce::dontSendNotification);
        title.setFont (uiFont (13.0f, juce::Font::bold).withExtraKerningFactor (0.12f));
        title.setColour (juce::Label::textColourId, Palette::text);
        status.setFont (uiFont (12.0f));
        status.setColour (juce::Label::textColourId, Palette::dim);

        list.setModel (this);
        list.setRowHeight (30);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        list.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);

        for (auto* c : std::initializer_list<juce::Component*> { &title, &status, &list, &close, &load, &addFolder, &openFolder, &removeFolder, &catView, &search })
            addAndMakeVisible (c);
        addChildComponent (savePreset);
        savePreset.setButtonText (fr ("Sauver mon preset…"));
        savePreset.setColour (juce::TextButton::buttonColourId, Palette::mint.withAlpha (0.30f));
        savePreset.setTooltip (fr ("Garde tout ce que tu as fait : les sons de chaque couche, les VST ouverts dans Variant (avec leur son), le synthé, les effets et leurs réglages. Rangé dans « Mes presets »."));
        savePreset.onClick = [this] { askPresetName(); };
        catView.setViewedComponent (&catHolder, false);
        catView.setScrollBarsShown (true, false);
        catView.setScrollBarThickness (8);
        search.setTextToShowWhenEmpty (fr ("Rechercher un son dans toutes les catégories…"), Palette::dim);
        search.onTextChange = [this] { searchDue = juce::Time::getMillisecondCounter() + 220; };   // on attend la fin de la frappe
        search.addKeyListener (&arrowKeys);   // dans la recherche aussi, flèches haut / bas = son suivant / précédent
        list.addKeyListener (&arrowKeys);
        catView.addKeyListener (&arrowKeys);   // les flèches ne font plus défiler la colonne des catégories
        catHolder.addKeyListener (&arrowKeys);
        addKeyListener (&arrowKeys);
        search.onEscapeKey = [this] { search.clear(); refreshSounds(); };

        close.setButtonText (fr ("Fermer"));
        close.onClick = [this] { setVisible (false); };
        load.setColour (juce::TextButton::buttonColourId, Palette::blue.withAlpha (0.22f));
        load.onClick = [this] { loadSelected(); };
        addFolder.setButtonText (fr ("Localiser un dossier…"));
        addFolder.setTooltip (fr ("Montre à Variant où sont tes sons : ta bibliothèque Variant déplacée, ou n'importe quel dossier de one-shots / drumkit (chaque sous-dossier devient une catégorie)"));
        addFolder.onClick = [this] { chooseFolder(); };
        openFolder.setButtonText (fr ("Ouvrir le dossier"));
        openFolder.onClick = [this]
        {
            if (showingPresets) { auto d = showingMine ? VariantProcessor::userPresetFolder() : presets::userFolder(); d.createDirectory(); d.startAsProcess(); }
            else if (auto* c = currentCategory()) c->folder.startAsProcess();
        };
        removeFolder.setButtonText (fr ("Retirer ce dossier"));
        removeFolder.onClick = [this]
        {
            auto* c = currentCategory();
            if (c != nullptr && c->user) { library::removeUserFolder ((c->root != juce::File() ? c->root : c->folder).getFullPathName()); refresh(); }
        };

        proc.ensureLibrary();
        refresh();
    }

    std::function<void()> onLoaded;
    void visibilityChanged() override
    {
        if (! isVisible()) { proc.stopFilePreview(); arrowPoll.stopTimer(); }
        else arrowPoll.startTimer (15);
    }
    void testSearch (const juce::String& s) { search.setText (s, true); search.grabKeyboardFocus(); }   // pré-écoute coupée en fermant

    // flèche haut / bas pendant que la banque est ouverte : le son (ou preset) suivant / précédent est chargé
    // tout de suite dans la couche, on peut jouer en même temps. Seule la liste des sons bouge, pas les catégories.
    void stepRow (int dir)
    {
        const int n = sounds.size();
        if (n <= 0) return;
        const int cur = list.getSelectedRow();
        const int row = cur < 0 ? (dir > 0 ? 0 : n - 1) : juce::jlimit (0, n - 1, cur + dir);
        if (row == cur) return;
        // FL à l'arrêt : simple pré-écoute du son (comme un clic).
        // FL en lecture (Play) : le son remplace celui de la couche tout de suite, pour comparer sur le MIDI du piano roll.
        const bool hostPlaying = proc.isHostPlaying();
        steppingByArrow = hostPlaying;
        list.selectRow (row);   // à l'arrêt, selectedRowsChanged lance la pré-écoute
        steppingByArrow = false;
        list.scrollToEnsureRowIsOnscreen (row);
        if (! hostPlaying) return;
        proc.stopFilePreview();
        if (isPresetFile (sounds[row])) { proc.loadUserPreset (sounds[row]); if (onLoaded) onLoaded(); }
        else { proc.loadSample (sounds[row], targetLayer); if (onLoaded) onLoaded(); }
    }
    bool steppingByArrow = false;
    static bool isPresetFile (const juce::File& f) { return f.hasFileExtension ("vpreset"); }

    void showFor (int layer)
    {
        targetLayer = layer;
        load.setButtonText (fr ("Charger dans la couche ") + juce::String (layer + 1));
        refresh();
        setVisible (true);
        toFront (true);
        // le son de la couche est déjà sélectionné : les flèches repartent de lui (sans le rejouer)
        const int cur = sounds.indexOf (proc.getLayerFile (layer));
        if (cur >= 0)
        {
            steppingByArrow = true;
            list.selectRow (cur);
            steppingByArrow = false;
        }
        list.grabKeyboardFocus();   // flèches haut / bas = écouter le son / preset suivant
    }

    void tick()
    {
        if (proc.libraryVersion.load() != seenVersion) refresh();
        if (searchDue != 0 && juce::Time::getMillisecondCounter() >= searchDue) { searchDue = 0; refreshSounds(); }
        status.setText (proc.libraryBuilding.load() ? fr ("Création des sons de base…")
                                                    : fr ("Clique sur un son pour l'écouter, double-clique pour le charger."),
                        juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Palette::bg.withAlpha (0.97f));
        drawCard (g, getLocalBounds(), {}, Palette::blue);
        g.setColour (Palette::line);
        g.drawVerticalLine (catArea.getRight() + 8, (float) catArea.getY(), (float) catArea.getBottom());
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16, 12);
        auto top = r.removeFromTop (30);
        close.setBounds (top.removeFromRight (100));
        title.setBounds (top.removeFromLeft (240));
        status.setBounds (top);
        r.removeFromTop (10);

        auto bottom = r.removeFromBottom (34);
        load.setBounds (bottom.removeFromRight (230));
        addFolder.setBounds (bottom.removeFromLeft (170));
        bottom.removeFromLeft (8);
        openFolder.setBounds (bottom.removeFromLeft (150));
        bottom.removeFromLeft (8);
        removeFolder.setBounds (bottom.removeFromLeft (150));
        savePreset.setBounds (removeFolder.getBounds().withWidth (juce::jmax (150, juce::jmin (190, bottom.getWidth() + 150 - 8))));
        r.removeFromBottom (10);

        catArea = r.removeFromLeft (200);
        catView.setBounds (catArea);
        const int bw = catArea.getWidth() - 12;
        catHolder.setSize (bw, juce::jmax (catArea.getHeight(), catButtons.size() * 30));
        for (int i = 0; i < catButtons.size(); ++i) catButtons[i]->setBounds (0, i * 30, bw, 26);
        r.removeFromLeft (16);
        search.setBounds (r.removeFromTop (30));
        r.removeFromTop (6);
        list.setBounds (r);
    }

private:
    const library::Category* currentCategory() const
    {
        return juce::isPositiveAndBelow (selectedCat, (int) cats.size()) ? &cats[(size_t) selectedCat] : nullptr;
    }

    // bouton de catégorie : clic droit = actions (renommer, supprimer…)
    struct CatButton : public juce::TextButton
    {
        using juce::TextButton::TextButton;
        std::function<void()> onRight;
        std::function<bool()> isSelected;
        void mouseUp (const juce::MouseEvent& e) override { if (e.mods.isPopupMenu() && onRight) { onRight(); return; } juce::TextButton::mouseUp (e); }
    };

    void refresh()
    {
        seenVersion = proc.libraryVersion.load();
        cats = library::categories();
        catFiles.clear();   // listes des sons relues seulement quand la bibliothèque change
        catButtons.clear();
        {
            auto* mb = catButtons.add (new CatButton (juce::String::fromUTF8 ("★ ") + fr ("Mes presets")));
            mb->setColour (juce::TextButton::buttonOnColourId, Palette::mint);
            mb->setTooltip (fr ("Tes presets à toi (dossier « Presets utilisateur » de ta banque) : couches, VST, synthé et effets exactement comme tu les as sauvés"));
            mb->onClick = [this] { showingPresets = true; showingMine = true; selectedTag = {}; search.clear(); refreshSounds(); };
            mb->isSelected = [this] { return showingPresets && showingMine; };
            catHolder.addAndMakeVisible (mb);
            auto* pb = catButtons.add (new CatButton (juce::String::fromUTF8 ("★ ") + library::presetFolderName()));
            pb->setColour (juce::TextButton::buttonOnColourId, Palette::mint);
            pb->setTooltip (fr ("Presets d'arpège rangés dans ta banque : son + arpège + accords + effets"));
            pb->onClick = [this]
            {
                if (! factoryChecked) { factoryChecked = true; proc.ensureFactoryPresets (false); }   // une seule fois (avant : à chaque frappe, et ça bloquait la liste)
                showingPresets = true; showingMine = false; selectedTag = {}; search.clear(); refreshSounds();
            };
            pb->isSelected = [this] { return showingPresets && ! showingMine; };
            catHolder.addAndMakeVisible (pb);
        }
        // tes catégories (Trap, Ambiant…) : tu choisis le nom, un son peut être dans plusieurs
        for (auto& tn : library::tags::names())
        {
            auto* b = catButtons.add (new CatButton ("# " + tn + "  (" + juce::String (library::tags::sounds (tn).size()) + ")"));
            b->setColour (juce::TextButton::buttonOnColourId, Palette::peach);
            b->setTooltip (fr ("Ta catégorie. Clic droit sur un son pour l'y ranger · clic droit ici pour renommer ou supprimer"));
            b->onClick = [this, tn] { showingPresets = false; selectedTag = tn; search.clear(); refreshSounds(); };
            b->isSelected = [this, tn] { return ! showingPresets && selectedTag == tn; };
            b->onRight = [this, tn] { tagMenu (tn); };
            catHolder.addAndMakeVisible (b);
        }
        {
            auto* nb = catButtons.add (new CatButton (fr ("+ Nouvelle catégorie")));
            nb->setTooltip (fr ("Crée ta propre catégorie (ex. Trap, Ambiant, Drill…) puis range-y des sons (clic droit sur un son)"));
            nb->onClick = [this] { askTagName ({}, {}); };
            nb->isSelected = [] { return false; };
            nb->setColour (juce::TextButton::buttonColourId, Palette::peach.withAlpha (0.15f));
            catHolder.addAndMakeVisible (nb);
        }
        for (int i = 0; i < (int) cats.size(); ++i)
        {
            auto* b = catButtons.add (new CatButton (cats[(size_t) i].user ? juce::String::fromUTF8 ("◦ ") + cats[(size_t) i].name : fr (cats[(size_t) i].name)));
            b->setColour (juce::TextButton::buttonOnColourId, Palette::blue);
            b->onClick = [this, i] { showingPresets = false; selectedTag = {}; selectedCat = i; search.clear(); refreshSounds(); };
            b->isSelected = [this, i] { return ! showingPresets && selectedTag.isEmpty() && selectedCat == i; };
            catHolder.addAndMakeVisible (b);
        }
        if (! viewRestored) { viewRestored = true; restoreView(); }   // on retrouve la catégorie ouverte la dernière fois
        if (selectedCat >= (int) cats.size()) selectedCat = 0;
        if (selectedTag.isNotEmpty() && ! library::tags::names().contains (selectedTag)) selectedTag = {};
        refreshSounds();
        resized();
    }

    // catégorie ouverte : gardée quand on ferme / rouvre le plugin (avant : on retombait toujours sur les basses)
    juce::String viewKey() const
    {
        if (showingPresets) return showingMine ? "mine" : "presets";
        if (selectedTag.isNotEmpty()) return "tag:" + selectedTag;
        if (auto* c = currentCategory()) return "cat:" + c->name;
        return {};
    }
    void restoreView()
    {
        const auto v = proc.libView.isNotEmpty() ? proc.libView : library::settings().getValue ("libView");
        if (v == "mine") { showingPresets = true; showingMine = true; }
        else if (v == "presets") { showingPresets = true; showingMine = false; }
        else if (v.startsWith ("tag:")) { showingPresets = false; selectedTag = v.substring (4); }
        else if (v.startsWith ("cat:"))
        {
            showingPresets = false; selectedTag = {};
            for (int i = 0; i < (int) cats.size(); ++i) if (cats[(size_t) i].name == v.substring (4)) selectedCat = i;
        }
    }
    bool viewRestored = false;

    void refreshSounds()
    {
        if (viewRestored) { const auto vk = viewKey(); if (vk != proc.libView) { proc.libView = vk; library::settings().setValue ("libView", vk); } }
        for (auto* b : catButtons) b->setToggleState (b->isSelected && b->isSelected() && (showingPresets || selectedTag.isNotEmpty() || search.getText().trim().isEmpty()), juce::dontSendNotification);
        sounds.clear();
        load.setButtonText (showingPresets ? fr ("Charger le preset") : fr ("Charger dans la couche ") + juce::String (targetLayer + 1));
        savePreset.setVisible (showingPresets);
        removeFolder.setVisible (! showingPresets);
        if (showingPresets)
        {
            // (plus d'appel à ensureFactoryPresets ici : il relançait la bibliothèque, qui relançait ce rafraîchissement
            // en boucle -> les boutons des catégories étaient recréés sans arrêt et les clics se perdaient)
            const auto q0 = search.getText().trim().toLowerCase();
            if (q0.isNotEmpty()) { searchEverywhere (q0); return; }
            juce::Array<juce::File> files;
            if (showingMine) { files = VariantProcessor::userPresetFolder().findChildFiles (juce::File::findFiles, false, "*.vpreset"); files.sort(); }
            else files = presets::userPresets();
            for (auto& f : files) if (q0.isEmpty() || f.getFileNameWithoutExtension().toLowerCase().contains (q0)) sounds.add (f);
            removeFolder.setEnabled (false);
            list.updateContent(); list.repaint();
            return;
        }
        const auto q = search.getText().trim().toLowerCase();
        if (q.isNotEmpty()) { searchEverywhere (q); return; }   // la recherche couvre toute la bibliothèque, quelle que soit la catégorie ouverte
        if (selectedTag.isNotEmpty())
        {
            for (auto& f : library::tags::sounds (selectedTag)) if (q.isEmpty() || f.getFileNameWithoutExtension().toLowerCase().contains (q)) sounds.add (f);
        }
        else if (q.isNotEmpty())
        {
            for (int ci = 0; ci < (int) cats.size(); ++ci)   // recherche dans toutes les catégories (listes en mémoire)
                for (auto& f : filesOf (ci))
                    if (f.getFileNameWithoutExtension().toLowerCase().contains (q) || cats[(size_t) ci].name.toLowerCase().contains (q)) sounds.add (f);
            for (auto& tn : library::tags::names())   // et dans tes catégories (par leur nom)
                if (tn.toLowerCase().contains (q)) for (auto& f : library::tags::sounds (tn)) sounds.addIfNotAlreadyThere (f);
        }
        else if (currentCategory() != nullptr) sounds = filesOf (selectedCat);
        rowTags = library::tags::byPath();
        removeFolder.setEnabled (selectedTag.isEmpty() && currentCategory() != nullptr && currentCategory()->user);
        list.updateContent();
        list.repaint();
    }

    // recherche dans toute la bibliothèque : tous les sons (catégories + tes dossiers + tes catégories) et tous les presets
    void searchEverywhere (const juce::String& q)
    {
        for (int ci = 0; ci < (int) cats.size(); ++ci)
            for (auto& f : filesOf (ci))
                if (f.getFileNameWithoutExtension().toLowerCase().contains (q) || cats[(size_t) ci].name.toLowerCase().contains (q)) sounds.add (f);
        for (auto& tn : library::tags::names())
            if (tn.toLowerCase().contains (q)) for (auto& f : library::tags::sounds (tn)) sounds.addIfNotAlreadyThere (f);
        auto mine = VariantProcessor::userPresetFolder().findChildFiles (juce::File::findFiles, false, "*.vpreset");
        mine.sort();
        for (auto& f : mine) if (f.getFileNameWithoutExtension().toLowerCase().contains (q)) sounds.addIfNotAlreadyThere (f);
        for (auto& f : presets::userPresets()) if (f.getFileNameWithoutExtension().toLowerCase().contains (q)) sounds.addIfNotAlreadyThere (f);
        rowTags = library::tags::byPath();
        removeFolder.setEnabled (false);
        list.updateContent();
        list.repaint();
    }

    // nom d'une catégorie (création, ou renommage si "from" n'est pas vide) ; "withSound" y est rangé tout de suite
    void askTagName (const juce::String& from, const juce::File& withSound)
    {
        auto* w = new juce::AlertWindow (from.isEmpty() ? fr ("Nouvelle catégorie") : fr ("Renommer la catégorie"),
                                         fr ("Nom de la catégorie (ex. Trap, Ambiant, Drill, Lo-fi…) :"), juce::AlertWindow::NoIcon);
        w->addTextEditor ("n", from);
        w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton (fr ("Annuler"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<LibraryPanel> self (this);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([self, w, from, withSound] (int res)
        {
            if (res != 1 || self == nullptr) return;
            const auto n = w->getTextEditorContents ("n").trim();
            if (n.isEmpty()) return;
            if (from.isEmpty()) library::tags::create (n);
            else { library::tags::rename (from, n); if (self->selectedTag == from) self->selectedTag = n; }
            if (withSound.existsAsFile() && ! library::tags::contains (n, withSound)) library::tags::toggle (n, withSound);
            self->refresh();
        }), true);
    }

    void tagMenu (const juce::String& tn)
    {
        juce::PopupMenu m;
        m.addSectionHeader (tn);
        m.addItem (1, fr ("Renommer…"));
        m.addItem (2, fr ("Supprimer la catégorie (les sons restent sur le disque)"));
        juce::Component::SafePointer<LibraryPanel> self (this);
        m.showMenuAsync (juce::PopupMenu::Options(), [self, tn] (int r)
        {
            if (self == nullptr || r == 0) return;
            if (r == 1) self->askTagName (tn, {});
            else { library::tags::remove (tn); if (self->selectedTag == tn) self->selectedTag = {}; self->refresh(); }
        });
    }

    // clic droit sur un son : le ranger dans tes catégories
    void soundMenu (const juce::File& f)
    {
        juce::PopupMenu m;
        m.addSectionHeader (f.getFileNameWithoutExtension());
        const auto names = library::tags::names();
        for (int i = 0; i < names.size(); ++i) m.addItem (100 + i, "# " + names[i], true, library::tags::contains (names[i], f));
        if (! names.isEmpty()) m.addSeparator();
        m.addItem (1, fr ("Nouvelle catégorie avec ce son…"));
        m.addItem (2, fr ("Charger dans la couche ") + juce::String (targetLayer + 1));
        m.addSeparator();
        const bool isDefault = juce::File (library::settings().getValue ("defaultSound")) == f;
        m.addItem (3, fr ("Son au lancement du plugin"), true, isDefault);
        juce::Component::SafePointer<LibraryPanel> self (this);
        m.showMenuAsync (juce::PopupMenu::Options(), [self, f, names, isDefault] (int r)
        {
            if (self == nullptr || r == 0) return;
            if (r == 1) self->askTagName ({}, f);
            else if (r == 3) { library::settings().setValue ("defaultSound", isDefault ? juce::String() : f.getFullPathName()); library::settings().saveIfNeeded(); }
            else if (r == 2) { self->proc.loadSample (f, self->targetLayer); if (self->onLoaded) self->onLoaded(); }
            else if (r >= 100) { library::tags::toggle (names[r - 100], f); self->refresh(); }
        });
    }

    void chooseFolder()
    {
        chooser = std::make_unique<juce::FileChooser> (fr ("Localiser un dossier de sons"), library::defaultFolder().getParentDirectory(), "");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [safe = juce::Component::SafePointer<LibraryPanel> (this)] (const juce::FileChooser& fc)
            {
                if (safe == nullptr) return;
                const auto f = fc.getResult();
                if (! f.isDirectory()) return;
                const auto msg = library::locateFolder (f);
                safe->proc.libraryVersion.fetch_add (1);
                safe->refresh();
                if (msg.isNotEmpty())
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, fr ("Bibliothèque de sons"), msg);
            });
    }

    void loadSelected()
    {
        const int row = list.getSelectedRow();
        if (juce::isPositiveAndBelow (row, sounds.size()) && isPresetFile (sounds[row]))
        {
            if (juce::isPositiveAndBelow (row, sounds.size()))
            {
                const auto f = sounds[row];
                juce::Component::SafePointer<LibraryPanel> self (this);
                presetui::confirmLoad ([self, f]
                {
                    if (self == nullptr) return;
                    self->proc.loadUserPreset (f); if (self->onLoaded) self->onLoaded();
                });
            }
            return;
        }
        if (juce::isPositiveAndBelow (row, sounds.size()))
        {
            proc.stopFilePreview();
            proc.loadSample (sounds[row], targetLayer);   // la note est détectée automatiquement (calée sur C)
            // la banque reste ouverte : tu enchaînes les sons sans la rouvrir à chaque fois
            if (onLoaded) onLoaded();
        }
    }

    int getNumRows() override { return sounds.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (! juce::isPositiveAndBelow (row, sounds.size())) return;
        auto r = juce::Rectangle<float> (0, 1, (float) w, (float) h - 2);
        g.setColour (selected ? Palette::blue.withAlpha (0.22f) : (row % 2 ? Palette::panel2.withAlpha (0.5f) : juce::Colours::transparentBlack));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (selected ? Palette::blue : Palette::dim);
        juce::Path tri;
        const float cy = r.getCentreY();
        tri.addTriangle (12, cy - 5, 12, cy + 5, 20, cy);
        g.fillPath (tri);
        g.setColour (Palette::text);
        g.setFont (uiFont (13.0f));
        const auto nm = sounds[row].getFileNameWithoutExtension();
        g.drawText (sounds[row].isAChildOf (library::defaultFolder()) ? fr (nm) : nm, r.withTrimmedLeft (30), juce::Justification::centredLeft, true);
        if (! isPresetFile (sounds[row]))
        {
            auto it = rowTags.find (sounds[row].getFullPathName());
            if (it != rowTags.end())
            {
                auto pr = r.reduced (6.0f, 6.0f);
                g.setFont (uiFont (11.0f, juce::Font::bold));
                for (int k = it->second.size(); --k >= 0;)
                {
                    const auto t = "# " + it->second[k];
                    const float tw = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), t) + 12.0f;
                    auto pill = pr.removeFromRight (tw); pr.removeFromRight (4.0f);
                    g.setColour (Palette::peach.withAlpha (0.22f)); g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
                    g.setColour (Palette::peach); g.drawText (t, pill, juce::Justification::centred);
                }
            }
        }
    }

    // nom du preset à sauver (tout : couches, VST, synthé, effets)
    void askPresetName()
    {
        auto* w = new juce::AlertWindow (fr ("Sauver mon preset"), fr ("Nom du preset (sons des couches + VST + synthé + effets) :"), juce::AlertWindow::NoIcon);
        w->addTextEditor ("n", fr ("Mon preset"));
        w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton (fr ("Annuler"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<LibraryPanel> self (this);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([self, w] (int res)
        {
            if (res != 1 || self == nullptr) return;
            const auto saved = self->proc.saveFullPreset (w->getTextEditorContents ("n"));
            if (saved.existsAsFile()) { self->showingPresets = true; self->showingMine = true; self->search.clear(); self->refreshSounds(); self->list.selectRow (self->sounds.indexOf (saved)); }
        }), true);
    }

    void presetMenu (const juce::File& f)
    {
        juce::PopupMenu m;
        m.addSectionHeader (f.getFileNameWithoutExtension());
        m.addItem (1, fr ("Charger le preset"));
        m.addItem (2, fr ("Remplacer par mon son actuel"));
        m.addItem (3, fr ("Supprimer (corbeille)"));
        juce::Component::SafePointer<LibraryPanel> self (this);
        m.showMenuAsync (juce::PopupMenu::Options(), [self, f] (int r)
        {
            if (self == nullptr || r == 0) return;
            if (r == 1) presetui::confirmLoad ([self, f] { if (self == nullptr) return; self->proc.loadUserPreset (f); if (self->onLoaded) self->onLoaded(); });
            else if (r == 2) { self->proc.saveFullPreset (f.getFileNameWithoutExtension()); self->refreshSounds(); }
            else if (r == 3) { f.moveToTrash(); self->refreshSounds(); }
        });
    }

    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && juce::isPositiveAndBelow (row, sounds.size()) && isPresetFile (sounds[row]) && sounds[row].isAChildOf (VariantProcessor::userPresetFolder())) { if (juce::isPositiveAndBelow (row, sounds.size())) { list.selectRow (row); presetMenu (sounds[row]); } return; }
        if (e.mods.isPopupMenu() && juce::isPositiveAndBelow (row, sounds.size()) && ! isPresetFile (sounds[row])) { if (juce::isPositiveAndBelow (row, sounds.size())) { list.selectRow (row); soundMenu (sounds[row]); } return; }
        list.grabKeyboardFocus();   // les flèches haut / bas marchent ensuite dans la liste
        // (la sélection vient de changer : selectedRowsChanged a déjà lancé l'écoute ; re-clic = réécouter)
        if (juce::Time::getMillisecondCounter() - lastPreviewMs > 150 || row != lastPreviewRow) previewRow (row);
    }

    // flèches haut / bas (ou clic) : on écoute le son ou le preset sélectionné
    void selectedRowsChanged (int row) override { if (! steppingByArrow) previewRow (row); }
    void previewRow (int row)
    {
        if (! juce::isPositiveAndBelow (row, sounds.size())) return;
        lastPreviewRow = row; lastPreviewMs = juce::Time::getMillisecondCounter();
        if (isPresetFile (sounds[row]))   // pré-écoute du son du preset (1re couche qui a un son)
        {
            if (auto x = juce::XmlDocument::parse (sounds[row]))
                for (auto* e : x->getChildWithTagNameIterator ("LAYER"))
                {
                    const juce::File f (e->getStringAttribute ("path"));
                    if (e->getStringAttribute ("path").isNotEmpty() && f.existsAsFile()) { proc.previewFile (f); return; }
                }
            proc.stopFilePreview();
            return;
        }
        proc.previewFile (sounds[row]);
    }
    int lastPreviewRow = -1;
    juce::uint32 lastPreviewMs = 0;

    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { list.selectRow (row); loadSelected(); }

    VariantProcessor& proc;
    juce::Label title, status;
    juce::ListBox list { "sounds" };
    juce::TextButton close, load, addFolder, openFolder, removeFolder;
    juce::OwnedArray<CatButton> catButtons;
    juce::Viewport catView;
    juce::String selectedTag;   // catégorie perso affichée (vide = un dossier)
    std::map<juce::String, juce::StringArray> rowTags;
    std::map<int, juce::Array<juce::File>> catFiles;   // sons de chaque dossier (lus une fois)
    juce::uint32 searchDue = 0;
    const juce::Array<juce::File>& filesOf (int ci)
    {
        auto it = catFiles.find (ci);
        if (it == catFiles.end()) it = catFiles.emplace (ci, juce::isPositiveAndBelow (ci, (int) cats.size()) ? library::soundsIn (cats[(size_t) ci].folder) : juce::Array<juce::File>()).first;
        return it->second;
    }
    juce::Component catHolder;
    bool showingPresets = false, showingMine = false, factoryChecked = false;
    juce::TextButton savePreset;
    // Les flèches sont lues directement sur le clavier (FL ne les transmet pas toujours au plugin) :
    // les touches reçues normalement sont donc avalées ici, pour ne pas descendre de deux sons d'un coup.
    struct ArrowKeys : juce::KeyListener
    {
        bool keyPressed (const juce::KeyPress& k, juce::Component*) override
        {
            return k.getKeyCode() == juce::KeyPress::upKey || k.getKeyCode() == juce::KeyPress::downKey;
        }
    } arrowKeys;

    // banque ouverte, souris dessus ou fenêtre de Variant active : flèche bas = son suivant, flèche haut = précédent
    // (maintenue : défile après un court instant, comme dans un explorateur de fichiers)
    struct ArrowPoll : juce::Timer
    {
        LibraryPanel& p;
        int heldDir = 0;
        juce::uint32 nextMs = 0;
        explicit ArrowPoll (LibraryPanel& x) : p (x) {}
        void timerCallback() override
        {
            const bool up = juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::upKey);
            const bool dn = juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::downKey);
            const int dir = up == dn ? 0 : (up ? -1 : 1);
            auto* peer = p.getPeer();
            // Ctrl / Maj / Alt + flèches = raccourcis de FL (ex. changer de gamme dans le piano roll) : jamais pour la banque
            const auto mods = juce::ModifierKeys::getCurrentModifiersRealtime();
            const bool modifier = mods.isCtrlDown() || mods.isShiftDown() || mods.isAltDown() || mods.isCommandDown();
            // souris sur la banque ET la banque réellement visible à cet endroit (pas cachée sous le piano roll de FL)
            auto* under = juce::Desktop::getInstance().findComponentAt (juce::Desktop::getMousePosition());
            const bool overBank = under != nullptr && (under == &p || p.isParentOf (under));
            const bool active = p.isShowing() && juce::Process::isForegroundProcess() && ! modifier
                                && ((peer != nullptr && peer->isFocused()) || overBank);
            const bool typingElsewhere = dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()) != nullptr
                                         && juce::Component::getCurrentlyFocusedComponent() != &p.search;
            if (dir == 0 || ! active || typingElsewhere) { heldDir = 0; return; }
            const auto now = juce::Time::getMillisecondCounter();
            if (dir != heldDir) { heldDir = dir; nextMs = now + 380; p.stepRow (dir); }
            else if (now >= nextMs) { nextMs = now + 160; p.stepRow (dir); }   // maintenue : un son chargé toutes les 0,16 s
        }
    } arrowPoll { *this };
    juce::TextEditor search;
    std::vector<library::Category> cats;
    juce::Array<juce::File> sounds;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> catArea;
    int selectedCat = 0, targetLayer = 0, seenVersion = -1;
};

//==============================================================================
// Onglet EFFETS : on ajoute seulement les effets voulus
//==============================================================================
// Visuel animé d'un effet dans sa carte (forme du shaper, motif du glitch, clavier de l'harmoniseur…)
class FxVisual : public juce::Component
{
public:
    FxVisual (VariantProcessor& p, int s, int ty, juce::Colour c) : proc (p), slot (s), type (ty), col (c)
    {
        const auto ax = axes (type);
        if (ax.first < 0 && ax.second < 0) setInterceptsMouseClicks (false, false);
        else setMouseCursor (juce::MouseCursor::UpDownLeftRightResizeCursor);
    }
    float time = 0.0f;
    bool compact = false;   // version réduite (colonne d'effets de la page Synthé) : pas de texte

    // les 2 réglages qu'on attrape en touchant le dessin : (horizontal, vertical), -1 = aucun
    static std::pair<int, int> axes (int t)
    {
        switch (t)
        {
            case fxs::Saturation: return { 1, 4 };   // drive, dureté
            case fxs::Filter:     return { 1, 2 };   // fréquence, résonance
            case fxs::Compressor: return { 1, 0 };   // ratio, seuil
            case fxs::Chorus:     return { 1, 3 };
            case fxs::Flanger:    return { 0, 1 };
            case fxs::Phaser:     return { 1, 2 };
            case fxs::Tremolo:    return { 0, 1 };
            case fxs::Lofi:       return { 1, 0 };
            case fxs::Portal:     return { 1, 0 };
            case fxs::Crystal:    return { 0, 1 };
            case fxs::Delay:      return { 0, 1 };   // temps, réinjection
            case fxs::Reverb:     return { 0, 2 };   // durée, amorti
            case fxs::Stereo:     return { 0, 2 };
            case fxs::HalfTime:   return { 1, 0 };
            case fxs::Pad:        return { 2, 1 };
            case fxs::Harmonic:   return { 3, 2 };   // correction, couleur
            case fxs::Shaper: case fxs::Glitch: return { 1, -1 };
            case fxs::EQ: case fxs::Liquid: return { -1, -1 };
            default:              return { 0, 1 };
        }
    }

    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { hover = false; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (auto* p = getParentComponent()) p->mouseUp (e.getEventRelativeTo (p)); return; }
        const auto ax = axes (type);
        for (int k = 0; k < 2; ++k)
        {
            const int idx = k == 0 ? ax.first : ax.second;
            grabbed[k] = idx >= 0 ? proc.apvts.getParameter (VariantProcessor::slotId (slot, "P" + juce::String (idx + 1))) : nullptr;
            if (grabbed[k] != nullptr) { start[k] = grabbed[k]->getValue(); grabbed[k]->beginChangeGesture(); }
        }
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const float sens = e.mods.isShiftDown() ? 0.25f : 1.0f;   // Maj = réglage fin
        if (grabbed[0] != nullptr) grabbed[0]->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, start[0] + sens * (float) e.getDistanceFromDragStartX() / juce::jmax (60.0f, (float) getWidth())));
        if (grabbed[1] != nullptr) grabbed[1]->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, start[1] - sens * (float) e.getDistanceFromDragStartY() / juce::jmax (50.0f, (float) getHeight())));
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        for (auto*& g : grabbed) if (g != nullptr) { g->endChangeGesture(); g = nullptr; }
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (! (hover || grabbed[0] != nullptr || grabbed[1] != nullptr)) return;
        const auto ax = axes (type);
        if (ax.first < 0 && ax.second < 0) return;
        const auto& d = fxs::def (type);
        juce::String s;
        if (ax.first >= 0)  s << juce::String::fromUTF8 ("↔ ") << fr (d.params[(size_t) ax.first].name);
        if (ax.second >= 0) s << (s.isEmpty() ? "" : "   ") << juce::String::fromUTF8 ("↕ ") << fr (d.params[(size_t) ax.second].name);
        auto r = getLocalBounds().reduced (6, 4).removeFromBottom (16);
        g.setFont (uiFont (11.0f, juce::Font::bold));
        const int w = juce::jmin (r.getWidth(), juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), s) + 14);
        auto pill = r.withWidth (w).toFloat();
        g.setColour (Palette::bg.withAlpha (0.8f)); g.fillRoundedRectangle (pill, 8.0f);
        g.setColour (col); g.drawFittedText (s, pill.toNearestInt(), juce::Justification::centred, 1);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Palette::panel2.withAlpha (0.7f));
        g.fillRoundedRectangle (r, 10.0f);
        r = r.reduced (10.0f, 8.0f);
        float pv[fxs::numP];
        for (int k = 0; k < fxs::numP; ++k) pv[k] = proc.getParam (VariantProcessor::slotId (slot, "P" + juce::String (k + 1)));
        const auto& h = fxs::host();
        const double beat = h.playing ? h.ppq : (double) time * h.bpm / 60.0;

        switch (type)
        {
            case fxs::Shaper:
            {
                const int shape = fxs::step (pv[0], 12);
                const double div = fxs::divisionBeats (fxs::step (pv[1], 12));
                juce::Path curve, fill;
                for (int i = 0; i <= 120; ++i)
                {
                    const float x = (float) i / 120.0f;
                    const float y = fxs::shaperShape (shape, fxs::swingPhase (x, pv[7]));
                    const float px = r.getX() + x * r.getWidth(), py = r.getBottom() - y * r.getHeight();
                    if (i == 0) { curve.startNewSubPath (px, py); fill.startNewSubPath (px, r.getBottom()); }
                    curve.lineTo (px, py); fill.lineTo (px, py);
                }
                fill.lineTo (r.getRight(), r.getBottom()); fill.closeSubPath();
                g.setColour (col.withAlpha (0.18f)); g.fillPath (fill);
                g.setColour (col); g.strokePath (curve, juce::PathStrokeType (2.0f));
                const float ph = (float) std::fmod (beat / div + pv[6], 1.0);
                g.setColour (Palette::text);
                g.drawVerticalLine ((int) (r.getX() + ph * r.getWidth()), r.getY(), r.getBottom());
                g.setFont (uiFont (11.5f));
                g.setColour (Palette::dim);
                g.drawText (fr (fxs::shapeNames()[shape]) + juce::String::fromUTF8 ("  ·  ") + fxs::divisionNames()[fxs::step (pv[1], 12)], r.removeFromTop (14), juce::Justification::topRight);
                break;
            }
            case fxs::Glitch:
            {
                const double div = fxs::divisionBeats (fxs::step (pv[1], 12));
                const auto cur = (juce::int64) std::floor (beat / div);
                const juce::Colour evCol[] = { Palette::line, Palette::peach, Palette::blue, Palette::lavender, Palette::pink, Palette::mint };
                const char* evName[] = { "", "S", "R", "T", "C", "P" };
                const float cw = r.getWidth() / 8.0f, ch = r.getHeight() / 2.0f;
                for (int k = 0; k < 16; ++k)
                {
                    const int ev = fxs::glitchEventFor (k, pv);
                    auto cell = juce::Rectangle<float> (r.getX() + (k % 8) * cw, r.getY() + (k / 8) * ch, cw, ch).reduced (2.0f);
                    g.setColour (evCol[ev].withAlpha (ev == 0 ? 0.35f : 0.75f));
                    g.fillRoundedRectangle (cell, 4.0f);
                    if (((cur % 16) + 16) % 16 == k) { g.setColour (Palette::text); g.drawRoundedRectangle (cell, 4.0f, 1.8f); }
                    g.setColour (Palette::bg);
                    g.setFont (uiFont (12.0f, juce::Font::bold));
                    g.drawText (evName[ev], cell, juce::Justification::centred);
                }
                break;
            }
            case fxs::HalfTime:
            {
                const double segBeats = fxs::halftimeBeats (fxs::step (pv[1], 4));
                const double rate = fxs::halftimeRate (fxs::step (pv[0], 5));
                const float ph = (float) std::fmod (beat / segBeats, 1.0);
                auto top = r.removeFromTop (r.getHeight() * 0.5f).reduced (0, 4);
                auto bot = r.reduced (0, 4);
                g.setFont (uiFont (11.5f));
                g.setColour (Palette::dim);
                g.drawText (fr ("son d'origine"), top.removeFromTop (14), juce::Justification::topLeft);
                g.drawText (fr ("halftime"), bot.removeFromTop (14), juce::Justification::topLeft);
                for (int b = 0; b < 2; ++b)
                {
                    auto lane = b == 0 ? top : bot;
                    juce::Path w;
                    const double speed = b == 0 ? 1.0 : rate;
                    for (int i = 0; i <= 80; ++i)
                    {
                        const float x = (float) i / 80.0f;
                        const float y = std::sin ((float) (x * speed * 18.0)) * std::exp (-std::fmod (x * (float) speed * 4.0f, 1.0f) * 3.0f);
                        if (i == 0) w.startNewSubPath (lane.getX(), lane.getCentreY() - y * lane.getHeight() * 0.4f);
                        else w.lineTo (lane.getX() + x * lane.getWidth(), lane.getCentreY() - y * lane.getHeight() * 0.4f);
                    }
                    g.setColour (b == 0 ? Palette::dim : col);
                    g.strokePath (w, juce::PathStrokeType (1.8f));
                }
                g.setColour (Palette::text);
                g.drawVerticalLine ((int) (r.getX() + ph * r.getWidth()), top.getY(), bot.getBottom());
                break;
            }
            case fxs::Harmonic:
            {
                // spectre façon Chroma : une barre par demi-ton, bleu = dans la gamme, violet = hors gamme (déplacé)
                const int sIdx = juce::jlimit (0, fxs::numSlots - 1, slot);
                const auto& v = fxs::chromaView()[(size_t) sIdx];
                const int mask = fxs::chromaMaskView()[(size_t) sIdx] != 0 ? fxs::chromaMaskView()[(size_t) sIdx] : fxs::harmonicMask (fxs::step (pv[1], 14));
                const int key = fxs::chromaKeyView()[(size_t) sIdx];
                const juce::Colour inC (0xff5aa8ff), outC (0xffb26bff);
                auto spec = r.removeFromTop (r.getHeight() * (compact ? 1.0f : 0.72f));
                float mx = 1.0e-6f; for (float x : v) mx = juce::jmax (mx, x);
                const float bw = spec.getWidth() / 72.0f;
                for (int i = 0; i < 72; ++i)   // C2..B7
                {
                    const int note = 36 + i;
                    const bool in = ((mask >> (((note - key) % 12 + 12) % 12)) & 1) != 0;
                    const float db = 20.0f * std::log10 (juce::jmax (1.0e-6f, v[(size_t) (note - 24)] / mx));
                    const float h = juce::jlimit (0.02f, 1.0f, (db + 54.0f) / 54.0f) * spec.getHeight();
                    g.setColour ((in ? inC : outC).withAlpha (in ? 0.9f : 0.75f));
                    g.fillRect (spec.getX() + i * bw + 0.5f, spec.getBottom() - h, juce::jmax (1.0f, bw - 1.0f), h);
                }
                if (! compact)
                {
                    static const bool black[] = { false, true, false, true, false, false, true, false, true, false, true, false };
                    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
                    auto kb = r.reduced (0.0f, 3.0f);
                    const float kw = kb.getWidth() / 12.0f;
                    for (int n = 0; n < 12; ++n)
                    {
                        const bool in = ((mask >> ((n - key + 12) % 12)) & 1) != 0;
                        auto k = juce::Rectangle<float> (kb.getX() + n * kw, kb.getY(), kw, kb.getHeight()).reduced (1.0f);
                        g.setColour (in ? inC.withAlpha (0.8f) : (black[n] ? Palette::panel : Palette::line));
                        g.fillRoundedRectangle (k, 3.0f);
                        g.setColour (in ? Palette::bg : Palette::dim);
                        g.setFont (uiFont (10.5f, n == key ? juce::Font::bold : juce::Font::plain));
                        g.drawText (names[n], k, juce::Justification::centred);
                    }
                    g.setColour (Palette::dim); g.setFont (uiFont (11.0f));
                    g.drawText (fr ("bleu = dans la gamme · violet = recalé"), spec.toNearestInt().removeFromTop (14), juce::Justification::topRight);
                }
                break;
            }
            default:
                drawReactive (g, r, pv, beat);
                break;
        }
    }

private:
    // visuel qui suit les réglages : chaque potard change le dessin
    void drawReactive (juce::Graphics& g, juce::Rectangle<float> r, const float* pv, double beat)
    {
        const float t = time;
        auto stroke = [&] (const juce::Path& p, float w = 2.0f, float a = 1.0f) { g.setColour (col.withAlpha (a)); g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved)); };
        auto label = [&] (const juce::String& s) { g.setColour (Palette::dim); g.setFont (uiFont (11.0f)); g.drawText (s, r.toNearestInt().removeFromTop (14), juce::Justification::topRight); };
        juce::Path p;
        switch (type)
        {
            case fxs::Saturation:
            {
                const float drive = 1.0f + pv[1] * 12.0f, hard = pv[4];
                for (int i = 0; i <= 100; ++i)
                {
                    const float x = (float) i / 50.0f - 1.0f;
                    const float soft = std::tanh (x * drive) / std::tanh (drive);
                    const float y = soft * (1.0f - hard) + juce::jlimit (-1.0f, 1.0f, x * drive * 0.5f) * hard;
                    const float px = r.getX() + (float) i / 100.0f * r.getWidth(), py = r.getCentreY() - y * r.getHeight() * 0.42f;
                    if (i == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
                }
                stroke (p, 2.2f);
                juce::Path w;   // onde qui passe dedans
                for (int i = 0; i <= 120; ++i)
                {
                    const float x = std::sin ((float) i / 120.0f * 12.0f + t * 4.0f);
                    const float y = std::tanh (x * drive) / std::tanh (drive);
                    const float px = r.getX() + (float) i / 120.0f * r.getWidth(), py = r.getCentreY() - y * r.getHeight() * 0.25f;
                    if (i == 0) w.startNewSubPath (px, py); else w.lineTo (px, py);
                }
                stroke (w, 1.2f, 0.45f);
                label ("drive x" + juce::String (drive, 1));
                break;
            }
            case fxs::Filter:
            {
                const float move = pv[4] * 0.25f * std::sin (t * (0.5f + pv[5] * 6.0f));
                const float fc = juce::jlimit (0.02f, 0.98f, pv[1] + move), res = pv[2];
                const int ty = fxs::step (pv[0], 4);
                for (int i = 0; i <= 160; ++i)
                {
                    const float x = (float) i / 160.0f, d = (x - fc) * 9.0f;
                    float lp = 1.0f / (1.0f + std::exp (d * 1.6f)), hp = 1.0f - lp;
                    float y = ty == 1 ? hp : (ty == 2 ? std::exp (-d * d * 0.6f) : (ty == 3 ? 1.0f - 0.95f * std::exp (-d * d * 1.5f) : lp));
                    y += res * 0.9f * std::exp (-d * d * 3.0f);
                    const float px = r.getX() + x * r.getWidth(), py = r.getBottom() - juce::jlimit (0.0f, 1.25f, y) * r.getHeight() * 0.75f;
                    if (i == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
                }
                stroke (p, 2.2f);
                g.setColour (col.withAlpha (0.25f));
                g.drawVerticalLine ((int) (r.getX() + fc * r.getWidth()), r.getY(), r.getBottom());
                break;
            }
            case fxs::Compressor:
            {
                const juce::String bn[] = { fr ("BAS"), fr ("MÉD"), fr ("HAUT") };
                const float bw = r.getWidth() / 3.0f;
                for (int b = 0; b < 3; ++b)
                {
                    const float pump = 0.5f + 0.5f * std::sin (t * (3.0f + b) + b * 1.7f);
                    const float depth = juce::jlimit (0.05f, 1.0f, 0.3f + pv[0] * 0.7f);
                    const float lvl = 0.25f + 0.65f * (1.0f - depth * pump * 0.6f);
                    auto bar = juce::Rectangle<float> (r.getX() + b * bw + 8.0f, r.getBottom() - lvl * (r.getHeight() - 16.0f) - 14.0f, bw - 16.0f, lvl * (r.getHeight() - 16.0f));
                    g.setColour (col.withAlpha (0.35f + 0.5f * pump));
                    g.fillRoundedRectangle (bar, 4.0f);
                    g.setColour (Palette::dim); g.setFont (uiFont (10.5f));
                    g.drawText (bn[b], juce::Rectangle<float> (r.getX() + b * bw, r.getBottom() - 13.0f, bw, 12.0f), juce::Justification::centred);
                }
                break;
            }
            case fxs::Chorus: case fxs::Flanger: case fxs::Phaser:
            {
                const float rate = (type == fxs::Flanger ? pv[0] : pv[1]) * 4.0f + 0.3f;
                const float depth = type == fxs::Chorus ? 0.3f + pv[3] * 0.7f : 0.2f + pv[type == fxs::Flanger ? 1 : 2] * 0.8f;
                const int voices = type == fxs::Phaser ? 2 + fxs::step (pv[0], 5) : 3;
                for (int v = 0; v < voices; ++v)
                {
                    juce::Path w;
                    const float off = depth * std::sin (t * rate + v * 2.1f);
                    for (int i = 0; i <= 100; ++i)
                    {
                        const float x = (float) i / 100.0f;
                        const float y = std::sin (x * 14.0f + off * 3.0f + v * 0.4f) * (0.6f + 0.4f * std::cos (x * 3.0f + t));
                        if (i == 0) w.startNewSubPath (r.getX(), r.getCentreY() - y * r.getHeight() * 0.35f);
                        else w.lineTo (r.getX() + x * r.getWidth(), r.getCentreY() - y * r.getHeight() * 0.35f);
                    }
                    stroke (w, 1.6f, 0.35f + 0.6f / (float) (v + 1));
                }
                break;
            }
            case fxs::Tremolo:
            {
                const float depth = pv[1], shape = pv[2];
                const float ph = (float) std::fmod (beat * (0.5 + pv[0] * 4.0), 1.0);
                for (int i = 0; i <= 160; ++i)
                {
                    const float x = (float) i / 160.0f;
                    const float s = std::sin ((x * 2.0f + ph) * juce::MathConstants<float>::twoPi);
                    const float sq = s > 0 ? 1.0f : -1.0f;
                    const float y = 1.0f - depth * 0.5f * (1.0f + (s * (1.0f - shape) + sq * shape));
                    const float px = r.getX() + x * r.getWidth(), py = r.getBottom() - y * r.getHeight() * 0.9f;
                    if (i == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
                }
                stroke (p, 2.2f);
                break;
            }
            case fxs::Lofi:
            {
                const int bits = juce::jmax (2, (int) (16 - pv[0] * 14));
                const float levels = (float) (1 << juce::jmin (bits, 8));
                for (int i = 0; i <= 140; ++i)
                {
                    const float x = (float) i / 140.0f;
                    const float wob = pv[1] * 0.4f * std::sin (t * 1.3f) + pv[2] * 0.15f * std::sin (t * 9.0f + x * 30.0f);
                    float y = std::sin (x * 10.0f + wob);
                    y = std::round (y * levels * 0.5f) / (levels * 0.5f);
                    const float px = r.getX() + x * r.getWidth(), py = r.getCentreY() - y * r.getHeight() * 0.38f;
                    if (i == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
                }
                stroke (p, 1.8f);
                label (juce::String (bits) + " bits");
                break;
            }
            case fxs::Delay:
            {
                // échos : espacés selon le temps choisi (valeur de note), hauteur = réinjection, couleur = ping-pong
                const double div = fxs::divisionBeats (fxs::step (pv[0], 12));
                const float fb = pv[1], ping = pv[2];
                const double span = 4.0;   // une mesure
                const float ph = (float) std::fmod (beat, span) / (float) span;
                float a = 1.0f;
                for (int k = 0; k < 24; ++k)
                {
                    const float x = (float) (k * div / span);
                    if (x > 1.0f || a < 0.03f) break;
                    const bool right = ping > 0.4f && (k % 2 == 1);
                    const float h = a * (r.getHeight() - 14.0f) * (1.0f - pv[6] * 0.4f * (k == 0 ? 0.0f : 1.0f));
                    g.setColour ((right ? Palette::blue : col).withAlpha (0.25f + 0.7f * a));
                    g.fillRoundedRectangle (r.getX() + x * r.getWidth() - 3.0f, r.getBottom() - h, 6.0f, h, 3.0f);
                    a *= juce::jmax (0.05f, fb);
                }
                g.setColour (Palette::text.withAlpha (0.7f));
                g.drawVerticalLine ((int) (r.getX() + ph * r.getWidth()), r.getY(), r.getBottom());
                label (fxs::divisionNames()[fxs::step (pv[0], 12)] + juce::String::fromUTF8 ("  ·  ") + juce::String ((int) (60.0 / juce::jmax (20.0, fxs::host().bpm) * div * 1000.0)) + " ms");
                break;
            }
            case fxs::Reverb:
            {
                const double spb = 60.0 / juce::jmax (20.0, fxs::host().bpm);
                static const double preBeats[] = { 0.0, 1.0 / 16.0, 1.0 / 8.0, 0.25, 0.5 };
                static const double durBars[] = { 0.0, 0.5, 1.0, 2.0, 4.0 };
                const int ps = fxs::step (pv[6], 5), ds = fxs::step (pv[7], 5);
                const float t60 = ds > 0 ? (float) (durBars[ds] * 4.0 * spb) : fxs::expMap (pv[0], 0.2f, 20.0f);
                const float pre = ps > 0 ? (float) (preBeats[ps] * spb) : fxs::lin (pv[1], 0, 250) / 1000.0f;
                const float view = juce::jmax (1.0f, t60 * 1.2f + pre);
                juce::Path fill;
                fill.startNewSubPath (r.getX(), r.getBottom());
                for (int i = 0; i <= 160; ++i)
                {
                    const float x = (float) i / 160.0f, sec = x * view;
                    float y = sec < pre ? 0.0f : std::pow (10.0f, -3.0f * (sec - pre) / t60 * 0.5f);
                    y *= 0.85f + 0.15f * std::sin (sec * 40.0f * (0.3f + pv[3]) + t * 2.0f);
                    fill.lineTo (r.getX() + x * r.getWidth(), r.getBottom() - y * r.getHeight() * 0.85f);
                }
                fill.lineTo (r.getRight(), r.getBottom()); fill.closeSubPath();
                g.setColour (col.withAlpha (0.18f + 0.25f * (1.0f - pv[2]))); g.fillPath (fill);
                stroke (fill, 1.6f);
                if (pv[4] > 0.05f)   // shimmer : étincelles
                    for (int k = 0; k < (int) (pv[4] * 20); ++k)
                    {
                        const float x = std::fmod (k * 0.137f + t * 0.05f, 1.0f), y = std::fmod (k * 0.311f + t * 0.2f, 1.0f);
                        g.setColour (Palette::text.withAlpha (0.6f * (1.0f - y)));
                        g.fillEllipse (r.getX() + x * r.getWidth(), r.getBottom() - y * r.getHeight(), 3.0f, 3.0f);
                    }
                label (juce::String (t60, 1) + " s" + (pre > 0.001f ? fr ("  ·  pré ") + juce::String ((int) (pre * 1000)) + " ms" : juce::String()));
                break;
            }
            case fxs::Stereo:
            {
                const float wdt = pv[0] * 2.0f;
                const auto c = r.getCentre();
                for (int k = 0; k < 3; ++k)
                {
                    const float s = 1.0f - k * 0.28f;
                    g.setColour (col.withAlpha (0.25f + 0.25f * k));
                    g.drawEllipse (c.x - r.getWidth() * 0.22f * (0.2f + wdt) * s, c.y - r.getHeight() * 0.38f * s, r.getWidth() * 0.44f * (0.2f + wdt) * s, r.getHeight() * 0.76f * s, 2.0f);
                }
                label (fr ("largeur ") + juce::String ((int) (wdt * 100)) + " %");
                break;
            }
            case fxs::Pad:
            {
                // nappe : bosses lentes qui gonflent selon la montée et la taille
                const float swell = 0.3f + pv[2] * 0.7f, size = pv[1];
                for (int v = 0; v < 4; ++v)
                {
                    juce::Path w;
                    for (int i = 0; i <= 120; ++i)
                    {
                        const float x = (float) i / 120.0f;
                        const float env = (1.0f - std::exp (-x * 6.0f / swell)) * (0.6f + 0.4f * size);
                        const float y = env * (0.55f + 0.25f * std::sin (x * (3.0f + v) + t * (0.6f + pv[5]) + v));
                        if (i == 0) w.startNewSubPath (r.getX(), r.getBottom() - y * r.getHeight());
                        else w.lineTo (r.getX() + x * r.getWidth(), r.getBottom() - y * r.getHeight());
                    }
                    stroke (w, 2.0f, 0.25f + 0.18f * v);
                }
                break;
            }
            default:
                fxs::drawIcon (g, type, r.reduced (r.getWidth() * 0.15f, r.getHeight() * 0.1f), time, col);
                break;
        }
    }

public:

private:
    VariantProcessor& proc;
    int slot, type;
    juce::Colour col;
    bool hover = false;
    juce::RangedAudioParameter* grabbed[2] { nullptr, nullptr };
    float start[2] { 0.0f, 0.0f };
};

// Éditeur façon Gross Beat : slots à gauche, enveloppe de temps / volume dessinable à droite
class GrossPanel : public juce::Component
{
public:
    GrossPanel (VariantProcessor& p, int s, juce::Colour c) : proc (p), slot (s), col (c)
    {
        for (int i = 0; i < 2; ++i)
        {
            auto* b = tabs.add (new juce::TextButton (i == 0 ? fr ("TEMPS") : fr ("VOLUME")));
            b->setClickingTogglesState (true);
            b->setRadioGroupId (4711 + s);
            b->setColour (juce::TextButton::buttonOnColourId, col);
            b->onClick = [this, i] { editVolume = i == 1; repaint(); };
            addAndMakeVisible (b);
        }
        tabs[0]->setToggleState (true, juce::dontSendNotification);
        setTooltip ("");
    }
    float time = 0.0f;
    void setTooltip (const juce::String&) {}

    void resized() override
    {
        auto r = getLocalBounds();
        auto t = r.removeFromTop (26);
        tabs[0]->setBounds (t.removeFromLeft (90)); t.removeFromLeft (4);
        tabs[1]->setBounds (t.removeFromLeft (90));
        r.removeFromTop (6);
        list = r.removeFromLeft (150);
        r.removeFromLeft (6);
        gridCol = r.removeFromLeft (40);   // taille de grille, comme Gross Beat (X = libre)
        r.removeFromLeft (6);
        editor = r;
    }

    void paint (juce::Graphics& g) override
    {
        const int cur = currentSlot();
        const auto& names = editVolume ? fxs::volumeSlotNames() : fxs::timeSlotNames();
        // liste des slots (2 colonnes)
        const int rows = 7;
        const float cw = list.getWidth() / 2.0f, chh = list.getHeight() / (float) rows;
        for (int i = 0; i < 13; ++i)
        {
            auto cell = juce::Rectangle<float> (list.getX() + (i / rows) * cw, list.getY() + (i % rows) * chh, cw, chh).reduced (2.0f);
            g.setColour (i == cur ? col.withAlpha (0.85f) : Palette::panel2.withAlpha (0.8f));
            g.fillRoundedRectangle (cell, 4.0f);
            g.setColour (i == cur ? Palette::bg : Palette::text.withAlpha (0.85f));
            g.setFont (uiFont (11.0f, i == cur ? juce::Font::bold : juce::Font::plain));
            g.drawFittedText (fr (names[i]), cell.toNearestInt().reduced (3, 0), juce::Justification::centred, 1, 0.8f);
        }

        // colonne de grille
        {
            const float bh = gridCol.getHeight() / (float) gridDivs().size();
            for (int i = 0; i < (int) gridDivs().size(); ++i)
            {
                auto cell = juce::Rectangle<float> ((float) gridCol.getX(), gridCol.getY() + i * bh, (float) gridCol.getWidth(), bh).reduced (1.5f);
                const bool on = gridDivs()[(size_t) i] == gridDiv;
                g.setColour (on ? col.withAlpha (0.85f) : Palette::panel2.withAlpha (0.8f));
                g.fillRoundedRectangle (cell, 4.0f);
                g.setColour (on ? Palette::bg : Palette::text.withAlpha (0.85f));
                g.setFont (uiFont (10.5f, juce::Font::bold));
                g.drawText (gridDivs()[(size_t) i] == 0 ? juce::String ("X") : "1/" + juce::String (gridDivs()[(size_t) i]), cell, juce::Justification::centred);
            }
        }
        // éditeur
        auto e = editor.toFloat();
        g.setColour (Palette::panel2.withAlpha (0.7f));
        g.fillRoundedRectangle (e, 8.0f);
        e = e.reduced (6.0f);
        // grille bien visible : lignes fines à chaque pas, épaisses à chaque quart
        const int gd = gridDiv > 0 ? gridDiv : 16;
        for (int k = 1; k < gd; ++k)
        {
            const bool major = (k * 4) % gd == 0;
            g.setColour (major ? Palette::text.withAlpha (0.35f) : Palette::line.withAlpha (0.75f));
            g.fillRect ((float) (e.getX() + e.getWidth() * k / (float) gd) - (major ? 0.75f : 0.5f), e.getY(), major ? 1.5f : 1.0f, e.getHeight());
        }
        const int hl = 8;   // les points s'aimantent aussi sur ces lignes
        for (int k = 1; k < hl; ++k)
        {
            g.setColour (Palette::line.withAlpha (k % 2 == 0 ? 0.7f : 0.4f));
            g.drawHorizontalLine ((int) (e.getY() + e.getHeight() * k / (float) hl), e.getX(), e.getRight());
        }
        juce::Path curve;
        const fxs::Envelope custom = getCustom();
        for (int i = 0; i <= 240; ++i)
        {
            const float x = (float) i / 240.0f;
            const float v = valueAt (cur, x, custom);
            const juce::Point<float> pt (e.getX() + x * e.getWidth(), toY (v, e));
            if (i == 0) curve.startNewSubPath (pt); else curve.lineTo (pt);
        }
        g.setColour (col);
        g.strokePath (curve, juce::PathStrokeType (2.2f));
        if (cur == 0)
            for (int i = 0; i < custom.n; ++i)
            {
                const auto& pt = custom.pts[(size_t) i];
                g.setColour (i == dragIndex ? Palette::text : col);
                g.fillEllipse (e.getX() + pt.x * e.getWidth() - 4.5f, toY (pt.y, e) - 4.5f, 9.0f, 9.0f);
            }
        if (ghost.x >= 0.0f && dragIndex < 0)   // où le point va tomber (sur la grille)
        {
            g.setColour (Palette::text.withAlpha (0.7f));
            g.drawEllipse (e.getX() + ghost.x * e.getWidth() - 5.0f, toY (ghost.y, e) - 5.0f, 10.0f, 10.0f, 1.4f);
        }
        // tête de lecture
        const auto& h = fxs::host();
        static const double cycles[] = { 1.0, 2.0, 4.0, 8.0 };
        const double cb = cycles[fxs::step (proc.getParam (VariantProcessor::slotId (slot, "P5")), 4)];
        const double beat = h.playing ? h.ppq : (double) time * h.bpm / 60.0;
        const float ph = (float) std::fmod (beat / cb, 1.0);
        g.setColour (Palette::text.withAlpha (0.8f));
        g.drawVerticalLine ((int) (e.getX() + ph * e.getWidth()), e.getY(), e.getBottom());
        g.setColour (Palette::dim);
        g.setFont (uiFont (11.0f));
        g.drawText (editVolume ? fr ("haut = fort · bas = silence") : fr ("haut = son direct · bas = on relit le passé"),
                    e.toNearestInt().removeFromBottom (14), juce::Justification::centredRight);
        if (cur != 0)
        {
            g.drawText (fr ("clique pour modifier (copie dans « Perso »)"), e.toNearestInt().removeFromTop (14), juce::Justification::centredRight);
        }
    }

    void mouseDown (const juce::MouseEvent& m) override
    {
        const auto pos = m.position;
        if (gridCol.contains (pos.toInt()))
        {
            const int i = juce::jlimit (0, (int) gridDivs().size() - 1, (int) ((pos.y - gridCol.getY()) / (gridCol.getHeight() / (float) gridDivs().size())));
            gridDiv = gridDivs()[(size_t) i];
            repaint();
            return;
        }
        if (list.contains (pos.toInt()))
        {
            const int rows = 7;
            const int c = (int) ((pos.x - list.getX()) / (list.getWidth() / 2.0f)), r = (int) ((pos.y - list.getY()) / (list.getHeight() / (float) rows));
            const int idx = juce::jlimit (0, 12, c * rows + r);
            proc.setParam (VariantProcessor::slotId (slot, editVolume ? "P2" : "P1"), (float) idx / 12.0f);
            if (! editVolume && proc.getParam (VariantProcessor::slotId (slot, "P3")) < 0.05f) proc.setParam (VariantProcessor::slotId (slot, "P3"), 1.0f);
            if (editVolume && proc.getParam (VariantProcessor::slotId (slot, "P4")) < 0.05f) proc.setParam (VariantProcessor::slotId (slot, "P4"), 1.0f);
            repaint();
            return;
        }
        if (! editor.contains (pos.toInt())) return;
        auto e = editor.toFloat().reduced (6.0f);
        int cur = currentSlot();
        if (cur != 0)
        {
            // copie du slot d'usine dans "Perso" pour l'éditer
            fxs::Envelope env; env.n = 17;
            for (int i = 0; i < 17; ++i) { const float x = (float) i / 16.0f; env.pts[(size_t) i] = { x, valueAt (cur, juce::jmin (x, 0.999f), env) }; }
            setCustom (env);
            proc.setParam (VariantProcessor::slotId (slot, editVolume ? "P2" : "P1"), 0.0f);
            cur = 0;
        }
        auto env = getCustom();
        const float x = snapX (juce::jlimit (0.0f, 1.0f, (pos.x - e.getX()) / e.getWidth()), m.mods.isAltDown());
        const float y = snapY (fromY (pos.y, e), m.mods.isAltDown());
        dragIndex = -1;
        for (int i = 0; i < env.n; ++i)
            if (std::abs (e.getX() + env.pts[(size_t) i].x * e.getWidth() - pos.x) < 8.0f && std::abs (toY (env.pts[(size_t) i].y, e) - pos.y) < 8.0f) { dragIndex = i; break; }
        if (m.mods.isPopupMenu())
        {
            if (dragIndex > 0 && dragIndex < env.n - 1) { for (int i = dragIndex; i < env.n - 1; ++i) env.pts[(size_t) i] = env.pts[(size_t) i + 1]; --env.n; setCustom (env); }
            dragIndex = -1;
            repaint();
            return;
        }
        if (dragIndex < 0)   // un point existe déjà sur cette colonne de la grille : on le prend
            for (int i = 0; i < env.n; ++i) if (std::abs (env.pts[(size_t) i].x - x) < 1.0e-4f) { dragIndex = i; env.pts[(size_t) i].y = y; setCustom (env); break; }
        if (dragIndex < 0 && env.n < 32)
        {
            int at = 0;
            while (at < env.n && env.pts[(size_t) at].x <= x) ++at;
            for (int i = env.n; i > at; --i) env.pts[(size_t) i] = env.pts[(size_t) i - 1];
            env.pts[(size_t) at] = { x, y };
            ++env.n;
            dragIndex = at;
            setCustom (env);
        }
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& m) override
    {
        if (dragIndex < 0) return;
        auto e = editor.toFloat().reduced (6.0f);
        auto env = getCustom();
        if (dragIndex >= env.n) return;
        float x = snapX (juce::jlimit (0.0f, 1.0f, (m.position.x - e.getX()) / e.getWidth()), m.mods.isAltDown());
        if (dragIndex == 0) x = 0.0f;
        else if (dragIndex == env.n - 1) x = 1.0f;
        else x = juce::jlimit (env.pts[(size_t) dragIndex - 1].x, env.pts[(size_t) dragIndex + 1].x, x);
        env.pts[(size_t) dragIndex] = { x, snapY (fromY (m.position.y, e), m.mods.isAltDown()) };
        setCustom (env);
        repaint();
    }

    void mouseMove (const juce::MouseEvent& m) override
    {
        auto e = editor.toFloat().reduced (6.0f);
        ghost = e.contains (m.position) ? juce::Point<float> (snapX ((m.position.x - e.getX()) / e.getWidth(), m.mods.isAltDown()), snapY (fromY (m.position.y, e), m.mods.isAltDown())) : juce::Point<float> (-1.0f, -1.0f);
        repaint (editor);
    }
    void mouseExit (const juce::MouseEvent&) override { ghost = { -1.0f, -1.0f }; repaint (editor); }
    void mouseUp (const juce::MouseEvent&) override { dragIndex = -1; repaint(); }

private:
    int currentSlot() const { return fxs::step (proc.getParam (VariantProcessor::slotId (slot, editVolume ? "P2" : "P1")), 13); }
    float valueAt (int cur, float x, const fxs::Envelope& custom) const
    {
        if (cur == 0) return custom.at (x);
        return editVolume ? fxs::shaperShape (cur - 1, x) : fxs::factoryTime (cur, x);
    }
    // temps : 0 en haut (direct) -> 1 cycle en bas ; volume : 1 en haut
    float toY (float v, juce::Rectangle<float> e) const { return editVolume ? e.getBottom() - v * e.getHeight() : e.getY() + juce::jlimit (0.0f, 1.0f, v) * e.getHeight(); }
    float fromY (float y, juce::Rectangle<float> e) const
    {
        const float t = juce::jlimit (0.0f, 1.0f, (y - e.getY()) / e.getHeight());
        return editVolume ? 1.0f - t : t;
    }
    float snapX (float x, bool free) const { return (free || gridDiv == 0) ? x : std::round (x * (float) gridDiv) / (float) gridDiv; }
    float snapY (float v, bool free) const { return (free || gridDiv == 0) ? v : std::round (v * 8.0f) / 8.0f; }   // sur les lignes horizontales (Alt = libre)
    static const std::vector<int>& gridDivs() { static const std::vector<int> d { 0, 32, 16, 12, 8, 6, 4, 3, 2 }; return d; }
    juce::Point<float> ghost { -1.0f, -1.0f };
    int gridDiv = 16;
    juce::Rectangle<int> gridCol;
    fxs::Envelope getCustom() const
    {
        auto& c = proc.fxStores.env;
        const juce::SpinLock::ScopedLockType l (c.lock);
        return c.env[(size_t) juce::jlimit (0, fxs::numSlots - 1, slot)][editVolume ? 1 : 0];
    }
    void setCustom (const fxs::Envelope& env)
    {
        auto& c = proc.fxStores.env;
        { const juce::SpinLock::ScopedLockType l (c.lock); c.env[(size_t) juce::jlimit (0, fxs::numSlots - 1, slot)][editVolume ? 1 : 0] = env; }
        ++c.version;
    }

    VariantProcessor& proc;
    int slot;
    juce::Colour col;
    bool editVolume = false;
    int dragIndex = -1;
    juce::OwnedArray<juce::TextButton> tabs;
    juce::Rectangle<int> list, editor;
};

// Grille du Glitch façon Digitalis : motifs à gauche, 6 pistes x 16 pas cliquables à droite
class GlitchPanel : public juce::Component
{
public:
    GlitchPanel (VariantProcessor& p, int s, juce::Colour c) : proc (p), slot (s), col (c)
    {
        for (auto* b : { &clearBtn, &randBtn, &fillBtn }) addAndMakeVisible (b);
        clearBtn.setTooltip (fr ("Vide la grille (motif perso)"));
        randBtn.setTooltip (fr ("Remplit la grille au hasard, avec goût (quelques glitchs bien placés)"));
        fillBtn.setTooltip (fr ("Répète sur les contretemps (un bon point de départ)"));
        clearBtn.onClick = [this] { store ({}); usePerso(); };
        randBtn.onClick = [this]
        {
            fxs::GlitchPattern pat; juce::Random rr;
            for (int k = 0; k < 4 + rr.nextInt (4); ++k) { const int l = rr.nextInt (fxs::NumLanes); const int st = (rr.nextInt (8) * 2 + (rr.nextBool() ? 1 : 0)) % 16; if (! pat.on (l, st)) pat.toggle (l, st); }
            store (pat); usePerso();
        };
        fillBtn.onClick = [this] { fxs::GlitchPattern pat; for (int st = 2; st < 16; st += 4) pat.toggle (0, st); pat.toggle (1, 14); store (pat); usePerso(); };
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }
    float time = 0.0f;

    void resized() override
    {
        auto r = getLocalBounds();
        list = r.removeFromLeft (150);
        r.removeFromLeft (8);
        auto tb = r.removeFromBottom (24);
        r.removeFromBottom (4);
        grid = r;
        clearBtn.setBounds (tb.removeFromLeft (70)); tb.removeFromLeft (4);
        randBtn.setBounds (tb.removeFromLeft (70)); tb.removeFromLeft (4);
        fillBtn.setBounds (tb.removeFromLeft (110));
        hintArea = tb;
    }

    void paint (juce::Graphics& g) override
    {
        const int cur = fxs::step (proc.getParam (VariantProcessor::slotId (slot, "P1")), 13);
        const int rows = 7;
        const float cw = list.getWidth() / 2.0f, chh = list.getHeight() / (float) rows;
        for (int i = 0; i < 13; ++i)
        {
            auto cell = juce::Rectangle<float> (list.getX() + (i / rows) * cw, list.getY() + (i % rows) * chh, cw, chh).reduced (2.0f);
            g.setColour (i == cur ? col.withAlpha (0.85f) : Palette::panel2.withAlpha (0.8f));
            g.fillRoundedRectangle (cell, 4.0f);
            g.setColour (i == cur ? Palette::bg : Palette::text.withAlpha (0.85f));
            g.setFont (uiFont (11.0f, i == cur ? juce::Font::bold : juce::Font::plain));
            g.drawFittedText (fr (fxs::glitchPatternNames()[i]), cell.toNearestInt().reduced (3, 0), juce::Justification::centred, 1, 0.8f);
        }

        auto gr = grid.toFloat();
        g.setColour (Palette::panel2.withAlpha (0.7f));
        g.fillRoundedRectangle (gr, 8.0f);
        gr = gr.reduced (6.0f);
        const float labelW = 70.0f;
        const auto pat = fxs::glitchPatternFor (proc.fxStores.glitch, slot, cur);
        const float sw = (gr.getWidth() - labelW) / 16.0f, sh = gr.getHeight() / (float) fxs::NumLanes;
        const auto& h = fxs::host();
        const double div = fxs::divisionBeats (fxs::step (proc.getParam (VariantProcessor::slotId (slot, "P2")), 12));
        const double beat = h.playing ? h.ppq : (double) time * h.bpm / 60.0;
        const int curStep = (int) (((juce::int64) std::floor (beat / div) % 16 + 16) % 16);
        const juce::Colour laneCol[] = { Palette::peach, Palette::blue, Palette::lavender, Palette::mint, Palette::pink, Palette::text };
        for (int l = 0; l < fxs::NumLanes; ++l)
        {
            g.setColour (Palette::dim);
            g.setFont (uiFont (11.0f));
            g.drawText (fr (fxs::glitchLaneNames()[l]), juce::Rectangle<float> (gr.getX(), gr.getY() + l * sh, labelW - 4.0f, sh), juce::Justification::centredLeft);
            for (int s = 0; s < 16; ++s)
            {
                auto cell = juce::Rectangle<float> (gr.getX() + labelW + s * sw, gr.getY() + l * sh, sw, sh).reduced (1.5f);
                const bool on = pat.on (l, s);
                g.setColour (on ? laneCol[l].withAlpha (0.85f) : (s % 4 == 0 ? Palette::line.brighter (0.15f) : Palette::line).withAlpha (0.6f));
                g.fillRoundedRectangle (cell, 3.0f);
                if (s == curStep) { g.setColour (Palette::text.withAlpha (0.8f)); g.drawRoundedRectangle (cell, 3.0f, 1.2f); }
                if (hoverCell == juce::Point<int> (l, s)) { g.setColour (laneCol[l]); g.drawRoundedRectangle (cell, 3.0f, 2.0f); }
            }
        }
        g.setColour (Palette::dim);
        g.setFont (uiFont (11.0f));
        g.drawText (cur != 0 ? fr ("clique les cases : ça copie ce motif dans « Perso » et tu le modifies")
                             : fr ("clique ou glisse sur les cases : chaque ligne = un glitch, chaque colonne = un pas"),
                    hintArea.withTrimmedLeft (8), juce::Justification::centredLeft, true);
    }

    void mouseDown (const juce::MouseEvent& m) override
    {
        const auto pos = m.position;
        if (list.contains (pos.toInt()))
        {
            const int rows = 7;
            const int c = (int) ((pos.x - list.getX()) / (list.getWidth() / 2.0f)), r = (int) ((pos.y - list.getY()) / (list.getHeight() / (float) rows));
            proc.setParam (VariantProcessor::slotId (slot, "P1"), (float) juce::jlimit (0, 12, c * rows + r) / 12.0f);
            repaint();
            return;
        }
        auto gr = grid.toFloat().reduced (6.0f);
        const float labelW = 70.0f;
        if (pos.x < gr.getX() + labelW || ! gr.contains (pos)) return;
        const int s = (int) ((pos.x - gr.getX() - labelW) / ((gr.getWidth() - labelW) / 16.0f));
        const int l = (int) ((pos.y - gr.getY()) / (gr.getHeight() / (float) fxs::NumLanes));
        if (s < 0 || s > 15 || l < 0 || l >= fxs::NumLanes) return;
        const int cur = fxs::step (proc.getParam (VariantProcessor::slotId (slot, "P1")), 13);
        auto pat = fxs::glitchPatternFor (proc.fxStores.glitch, slot, cur);
        pat.toggle (l, s);
        paintValue = pat.on (l, s);
        store (pat);
        if (cur != 0) proc.setParam (VariantProcessor::slotId (slot, "P1"), 0.0f);
        lastCell = { l, s };
        repaint();
    }

    void mouseMove (const juce::MouseEvent& m) override
    {
        auto gr = grid.toFloat().reduced (6.0f);
        const float labelW = 70.0f;
        juce::Point<int> c { -1, -1 };
        if (gr.contains (m.position) && m.position.x >= gr.getX() + labelW)
            c = { (int) ((m.position.y - gr.getY()) / (gr.getHeight() / (float) fxs::NumLanes)), (int) ((m.position.x - gr.getX() - labelW) / ((gr.getWidth() - labelW) / 16.0f)) };
        if (c != hoverCell) { hoverCell = c; repaint(); }
    }
    void mouseExit (const juce::MouseEvent&) override { hoverCell = { -1, -1 }; repaint(); }

    void mouseDrag (const juce::MouseEvent& m) override
    {
        // glisser pour peindre plusieurs cases
        auto gr = grid.toFloat().reduced (6.0f);
        const float labelW = 70.0f;
        const auto pos = m.position;
        if (! gr.contains (pos) || pos.x < gr.getX() + labelW) return;
        const int s = (int) ((pos.x - gr.getX() - labelW) / ((gr.getWidth() - labelW) / 16.0f));
        const int l = (int) ((pos.y - gr.getY()) / (gr.getHeight() / (float) fxs::NumLanes));
        if (s < 0 || s > 15 || l < 0 || l >= fxs::NumLanes || juce::Point<int> (l, s) == lastCell) return;
        lastCell = { l, s };
        auto pat = fxs::glitchPatternFor (proc.fxStores.glitch, slot, 0);
        if (pat.on (l, s) != paintValue) pat.toggle (l, s);
        store (pat);
        repaint();
    }

private:
    void store (const fxs::GlitchPattern& pat)
    {
        auto& gp = proc.fxStores.glitch;
        { const juce::SpinLock::ScopedLockType lk (gp.lock); gp.perso[(size_t) juce::jlimit (0, fxs::numSlots - 1, slot)] = pat; }
        ++gp.version;
    }

    VariantProcessor& proc;
    int slot;
    juce::Colour col;
    bool paintValue = true;
    juce::Point<int> lastCell { -1, -1 }, hoverCell { -1, -1 };
    juce::Rectangle<int> list, grid, hintArea;
    juce::TextButton clearBtn { fr ("Vider") }, randBtn { fr ("Hasard") }, fillBtn { fr ("Contretemps") };
    void usePerso() { proc.setParam (VariantProcessor::slotId (slot, "P1"), 0.0f); repaint(); }
};

//==============================================================================
// Éditeur de courbe de l'effet Liquide (façon ShaperBox) : clic = point, glisser = déplacer,
// clic droit = supprimer, double-clic = remettre la courbe choisie ; menu de formes en haut
class LiquidPanel : public juce::Component, public juce::SettableTooltipClient
{
public:
    LiquidPanel (VariantProcessor& p, int s, juce::Colour c) : proc (p), slot (s), col (c)
    {
        setTooltip (fr ("Dessine la courbe : clic = ajouter un point, glisser = déplacer, clic droit = supprimer. Elle fait bouger l'effet en rythme."));
    }
    float time = 0.0f;

    void paint (juce::Graphics& g) override
    {
        auto a = area();
        g.setColour (Palette::panel2.withAlpha (0.7f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 10.0f);
        // grille : temps et demi-temps, comme ShaperBox
        const int beats = beatsShown();
        for (int b = 0; b <= beats * 4; ++b)
        {
            const float x = a.getX() + a.getWidth() * (float) b / (float) (beats * 4);
            g.setColour (b % 4 == 0 ? Palette::line.brighter (0.25f) : Palette::line.withAlpha (0.35f));
            g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
        }
        for (int k = 1; k < 4; ++k) { g.setColour (Palette::line.withAlpha (0.3f)); g.drawHorizontalLine ((int) (a.getY() + a.getHeight() * k / 4.0f), a.getX(), a.getRight()); }

        const auto env = currentCurve();
        juce::Path p;
        for (int i = 0; i <= 300; ++i)
        {
            const float x = (float) i / 300.0f;
            const float y = a.getBottom() - env.at (x) * a.getHeight();
            if (i == 0) p.startNewSubPath (a.getX(), y); else p.lineTo (a.getX() + x * a.getWidth(), y);
        }
        g.setColour (col.withAlpha (0.15f));
        juce::Path f = p; f.lineTo (a.getRight(), a.getBottom()); f.lineTo (a.getX(), a.getBottom()); f.closeSubPath(); g.fillPath (f);
        g.setColour (col);
        g.strokePath (p, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved));
        if (isPerso())
            for (int i = 0; i < env.n; ++i)
            {
                const auto pt = toScreen (env.pts[(size_t) i]);
                g.setColour (i == drag ? Palette::text : Palette::panel);
                g.fillEllipse (pt.x - 4.5f, pt.y - 4.5f, 9.0f, 9.0f);
                g.setColour (col); g.drawEllipse (pt.x - 4.5f, pt.y - 4.5f, 9.0f, 9.0f, 1.5f);
            }
        // tête de lecture
        const auto& h = fxs::host();
        const double lenBeats = (double) beatsShown();
        const double beat = h.playing ? h.ppq : (double) time * h.bpm / 60.0;
        const float ph = (float) std::fmod (beat / lenBeats, 1.0);
        g.setColour (Palette::text.withAlpha (0.8f));
        g.drawVerticalLine ((int) (a.getX() + ph * a.getWidth()), a.getY(), a.getBottom());
        g.setColour (Palette::dim);
        g.setFont (uiFont (11.0f));
        g.drawText (fr (fxs::liquidCurveNames()[curveIdx()]) + juce::String::fromUTF8 ("  ·  ") + juce::String (beatsShown() / 4.0, 1) + fr (" mesure(s)"),
                    getLocalBounds().reduced (10, 3).removeFromTop (14), juce::Justification::topLeft);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto env = editable();
        const int hit = hitPoint (env, e.position);
        if (e.mods.isPopupMenu())
        {
            if (hit > 0 && hit < env.n - 1) { for (int i = hit; i < env.n - 1; ++i) env.pts[(size_t) i] = env.pts[(size_t) i + 1]; --env.n; store (env); }
            return;
        }
        if (hit >= 0) { drag = hit; return; }
        if (env.n >= 32) return;
        const auto np = fromScreen (e.position);
        int at = 0; while (at < env.n && env.pts[(size_t) at].x < np.x) ++at;
        for (int i = env.n; i > at; --i) env.pts[(size_t) i] = env.pts[(size_t) i - 1];
        env.pts[(size_t) at] = np; ++env.n;
        store (env);
        drag = at;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag < 0) return;
        auto env = editable();
        if (drag >= env.n) return;
        auto np = fromScreen (e.position);
        const float lo = drag == 0 ? 0.0f : env.pts[(size_t) drag - 1].x;
        const float hi = drag == env.n - 1 ? 1.0f : env.pts[(size_t) drag + 1].x;
        if (drag == 0) np.x = 0.0f; else if (drag == env.n - 1) np.x = 1.0f; else np.x = juce::jlimit (lo, hi, np.x);
        env.pts[(size_t) drag] = np;
        store (env);
    }

    void mouseUp (const juce::MouseEvent&) override { drag = -1; repaint(); }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        // remet une forme de base (sinus) dans la courbe perso
        fxs::Envelope env; env.n = 17;
        for (int i = 0; i < 17; ++i) env.pts[(size_t) i] = { i / 16.0f, fxs::liquidShape (1, i / 16.0f) };
        store (env);
    }

private:
    juce::Rectangle<float> area() const { return getLocalBounds().toFloat().reduced (10.0f, 8.0f).withTrimmedTop (12.0f); }
    int curveIdx() const { return fxs::step (proc.getParam (VariantProcessor::slotId (slot, "P8")), fxs::liquidCurveNames().size()); }
    bool isPerso() const { return curveIdx() == 0; }
    int beatsShown() const { static const int b[] = { 2, 4, 8, 16 }; return b[fxs::step (proc.getParam (VariantProcessor::slotId (slot, "P2")), 4)]; }
    juce::Point<float> toScreen (fxs::EnvPt p) const { const auto a = area(); return { a.getX() + p.x * a.getWidth(), a.getBottom() - p.y * a.getHeight() }; }
    fxs::EnvPt fromScreen (juce::Point<float> s) const { const auto a = area(); return { juce::jlimit (0.0f, 1.0f, (s.x - a.getX()) / a.getWidth()), juce::jlimit (0.0f, 1.0f, (a.getBottom() - s.y) / a.getHeight()) }; }
    int hitPoint (const fxs::Envelope& env, juce::Point<float> m) const { for (int i = 0; i < env.n; ++i) if (toScreen (env.pts[(size_t) i]).getDistanceFrom (m) < 8.0f) return i; return -1; }

    fxs::Envelope currentCurve() const
    {
        if (! isPerso())
        {
            fxs::Envelope e; e.n = 32;
            for (int i = 0; i < 32; ++i) e.pts[(size_t) i] = { i / 31.0f, fxs::liquidShape (curveIdx(), i / 31.0f) };
            return e;
        }
        auto& lc = proc.fxStores.liquid;
        const juce::SpinLock::ScopedLockType l (lc.lock);
        return lc.env[(size_t) juce::jlimit (0, fxs::numSlots - 1, slot)];
    }
    // modifier une forme toute faite la copie d'abord dans "Perso"
    fxs::Envelope editable()
    {
        auto env = currentCurve();
        if (! isPerso())
        {
            fxs::Envelope e; e.n = 17;
            for (int i = 0; i < 17; ++i) e.pts[(size_t) i] = { i / 16.0f, fxs::liquidShape (curveIdx(), i / 16.0f) };
            proc.setParam (VariantProcessor::slotId (slot, "P8"), 0.0f);
            store (e);
            return e;
        }
        return env;
    }
    void store (const fxs::Envelope& env)
    {
        auto& lc = proc.fxStores.liquid;
        { const juce::SpinLock::ScopedLockType l (lc.lock); lc.env[(size_t) juce::jlimit (0, fxs::numSlots - 1, slot)] = env; }
        ++lc.version;
        repaint();
    }

    VariantProcessor& proc;
    int slot;
    juce::Colour col;
    int drag = -1;
};

//==============================================================================
// Égaliseur paramétrique façon FL : glisse les points (fréquence / gain), molette = largeur (Q),
// clic droit = type de bande et pente, double-clic = activer / couper la bande
class EqPanel : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    EqPanel (VariantProcessor& p, int s, juce::Colour c) : proc (p), slot (s), accent (c)
    {
        setTooltip (fr ("Glisse un point : fréquence et gain · molette : largeur · clic droit : type (cloche, plateau, coupe…) et pente · double-clic : activer / couper la bande"));
        startTimerHz (15);
    }
    float time = 0.0f;

    void paint (juce::Graphics& g) override
    {
        auto a = plot();
        g.setColour (Palette::panel2.withAlpha (0.6f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 10.0f);
        // grille
        g.setFont (uiFont (9.5f));
        for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            const float x = xOf (f);
            g.setColour (Palette::line.withAlpha (0.5f));
            g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
            g.setColour (Palette::dim);
            g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f), juce::Rectangle<float> (x - 15, a.getBottom() + 1, 30, 11), juce::Justification::centred);
        }
        for (float db : { -18.0f, -12.0f, -6.0f, 0.0f, 6.0f, 12.0f, 18.0f })
        {
            g.setColour (db == 0.0f ? Palette::dim.withAlpha (0.6f) : Palette::line.withAlpha (0.4f));
            g.drawHorizontalLine ((int) yOf (db), a.getX(), a.getRight());
        }

        const auto c = curve();
        const double fs = 48000.0;
        // courbe de la bande sélectionnée
        if (juce::isPositiveAndBelow (sel, fxs::eqBandsCount) && c[(size_t) sel].on)
        {
            fxs::EqCurve one = fxs::defaultEq();
            for (auto& b : one) b.on = false;
            one[(size_t) sel] = c[(size_t) sel];
            juce::Path p; bool first = true;
            for (int i = 0; i <= 240; ++i)
            {
                const float f = 20.0f * std::pow (1000.0f, i / 240.0f);
                const float y = yOf ((float) fxs::eqResponseDb (one, f, fs));
                if (first) { p.startNewSubPath (xOf (f), y); first = false; } else p.lineTo (xOf (f), y);
            }
            juce::Path fill = p; fill.lineTo (a.getRight(), yOf (0)); fill.lineTo (a.getX(), yOf (0)); fill.closeSubPath();
            g.setColour (bandColour (sel).withAlpha (0.18f));
            g.fillPath (fill);
        }
        // courbe totale
        juce::Path p; bool first = true;
        for (int i = 0; i <= 320; ++i)
        {
            const float f = 20.0f * std::pow (1000.0f, i / 320.0f);
            const float y = juce::jlimit (a.getY(), a.getBottom(), yOf ((float) fxs::eqResponseDb (c, f, fs)));
            if (first) { p.startNewSubPath (xOf (f), y); first = false; } else p.lineTo (xOf (f), y);
        }
        g.setColour (accent);
        g.strokePath (p, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved));

        // points
        for (int b = 0; b < fxs::eqBandsCount; ++b)
        {
            const auto pt = nodePos (c[(size_t) b]);
            const bool on = c[(size_t) b].on, hot = b == sel || b == hover;
            const float r = hot ? 8.0f : 6.5f;
            g.setColour (on ? bandColour (b) : Palette::dim.withAlpha (0.4f));
            g.fillEllipse (pt.x - r, pt.y - r, r * 2, r * 2);
            g.setColour (Palette::bg);
            g.setFont (uiFont (9.5f, juce::Font::bold));
            g.drawText (juce::String (b + 1), juce::Rectangle<float> (pt.x - r, pt.y - r, r * 2, r * 2), juce::Justification::centred);
        }
        if (juce::isPositiveAndBelow (sel, fxs::eqBandsCount))
        {
            const auto& b = c[(size_t) sel];
            juce::String t = fr (fxs::eqTypeName (b.type)) + "  " + fxs::hzText (b.freq);
            if (b.type == fxs::EqLowCut || b.type == fxs::EqHighCut) t << "  " << (b.slope * 12) << " dB/oct";
            else if (b.type != fxs::EqNotch) t << "  " << (b.gain >= 0 ? "+" : "") << juce::String (b.gain, 1) << " dB";
            t << "  Q " << juce::String (b.q, 2) << (b.on ? "" : fr ("  (coupée)"));
            g.setColour (Palette::text);
            g.setFont (uiFont (11.5f, juce::Font::bold));
            g.drawText (t, getLocalBounds().reduced (10, 4).removeFromTop (14), juce::Justification::centredLeft);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override { const int h = hit (e.position); if (h != hover) { hover = h; repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int h = hit (e.position);
        if (h >= 0) sel = h;
        if (e.mods.isPopupMenu() && h >= 0) { bandMenu (h); return; }
        dragging = h;
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging < 0) return;
        auto c = curve();
        auto& b = c[(size_t) dragging];
        const auto a = plot();
        b.freq = juce::jlimit (20.0f, 20000.0f, 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, (e.position.x - a.getX()) / a.getWidth())));
        if (b.type != fxs::EqLowCut && b.type != fxs::EqHighCut && b.type != fxs::EqNotch)
            b.gain = juce::jlimit (-24.0f, 24.0f, gainAt (e.position.y));
        b.on = true;
        setCurve (c);
    }

    void mouseUp (const juce::MouseEvent&) override { dragging = -1; }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const int h = hit (e.position) >= 0 ? hit (e.position) : sel;
        if (h < 0) return;
        auto c = curve();
        auto& b = c[(size_t) h];
        b.q = juce::jlimit (0.1f, 18.0f, b.q * (w.deltaY > 0 ? 1.15f : 1.0f / 1.15f));
        sel = h;
        setCurve (c);
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int h = hit (e.position);
        auto c = curve();
        if (h >= 0) { c[(size_t) h].on = ! c[(size_t) h].on; sel = h; setCurve (c); return; }
        // double-clic dans le vide : on allume la bande coupée la plus proche et on la pose ici
        int best = -1; float bd = 1.0e9f;
        for (int b = 0; b < fxs::eqBandsCount; ++b)
            if (! c[(size_t) b].on) { const float d = std::abs (xOf (c[(size_t) b].freq) - e.position.x); if (d < bd) { bd = d; best = b; } }
        if (best < 0) return;
        auto& b = c[(size_t) best];
        b.on = true;
        b.freq = 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, (e.position.x - plot().getX()) / plot().getWidth()));
        if (b.type == fxs::EqLowCut || b.type == fxs::EqHighCut) b.type = fxs::EqBell;
        b.gain = juce::jlimit (-24.0f, 24.0f, gainAt (e.position.y));
        sel = best;
        setCurve (c);
    }

private:
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (12.0f, 8.0f).withTrimmedTop (12.0f).withTrimmedBottom (10.0f); }
    float xOf (float f) const { const auto a = plot(); return a.getX() + a.getWidth() * std::log (f / 20.0f) / std::log (1000.0f); }
    float yOf (float db) const { const auto a = plot(); return a.getCentreY() - db / 26.0f * a.getHeight() * 0.5f; }
    float gainAt (float y) const { const auto a = plot(); return (a.getCentreY() - y) / (a.getHeight() * 0.5f) * 26.0f; }
    static juce::Colour bandColour (int b)
    {
        const juce::Colour cols[] = { Palette::pink, Palette::peach, Palette::mint, Palette::blue, Palette::lavender, Palette::peach.brighter (0.3f), Palette::pink.brighter (0.3f) };
        return cols[b % 7];
    }
    juce::Point<float> nodePos (const fxs::EqBand& b) const
    {
        const bool flat = b.type == fxs::EqLowCut || b.type == fxs::EqHighCut || b.type == fxs::EqNotch;
        return { xOf (b.freq), yOf (flat ? 0.0f : b.gain) };
    }
    int hit (juce::Point<float> m) const
    {
        const auto c = curve();
        for (int b = fxs::eqBandsCount - 1; b >= 0; --b) if (nodePos (c[(size_t) b]).getDistanceFrom (m) < 10.0f) return b;
        return -1;
    }
    fxs::EqCurve curve() const
    {
        auto& es = proc.fxStores.eq;
        const juce::SpinLock::ScopedLockType l (es.lock);
        return es.curves[(size_t) juce::jlimit (0, fxs::numSlots - 1, slot)];
    }
    void setCurve (const fxs::EqCurve& c)
    {
        auto& es = proc.fxStores.eq;
        { const juce::SpinLock::ScopedLockType l (es.lock); es.curves[(size_t) juce::jlimit (0, fxs::numSlots - 1, slot)] = c; }
        ++es.version[(size_t) juce::jlimit (0, fxs::numSlots - 1, slot)];
        repaint();
    }
    void bandMenu (int b)
    {
        auto c = curve();
        juce::PopupMenu m;
        for (int t = 0; t < fxs::NumEqTypes; ++t) m.addItem (1 + t, fr (fxs::eqTypeName (t)), true, c[(size_t) b].type == t);
        m.addSeparator();
        for (int s : { 1, 2, 4 }) m.addItem (10 + s, juce::String (s * 12) + " dB/oct", c[(size_t) b].type == fxs::EqLowCut || c[(size_t) b].type == fxs::EqHighCut, c[(size_t) b].slope == s);
        m.addSeparator();
        m.addItem (20, c[(size_t) b].on ? fr ("Couper la bande") : fr ("Activer la bande"));
        m.addItem (21, fr ("Remettre à zéro"));
        juce::Component::SafePointer<EqPanel> self (this);
        m.showMenuAsync (juce::PopupMenu::Options(), [safe = self, b] (int r)
        {
            if (safe == nullptr || r == 0) return;
            auto cc = safe->curve();
            auto& bd = cc[(size_t) b];
            if (r <= fxs::NumEqTypes) { bd.type = r - 1; bd.on = true; }
            else if (r >= 11 && r <= 14) bd.slope = r - 10;
            else if (r == 20) bd.on = ! bd.on;
            else if (r == 21) bd = fxs::defaultEq()[(size_t) b];
            safe->setCurve (cc);
        });
    }
    void timerCallback() override
    {
        const int v = proc.fxStores.eq.version[(size_t) juce::jlimit (0, fxs::numSlots - 1, slot)].load();
        if (v != seen) { seen = v; repaint(); }
    }

    VariantProcessor& proc;
    int slot;
    juce::Colour accent;
    int sel = -1, hover = -1, dragging = -1, seen = -1;
};

class SlotCard : public juce::Component
{
public:
    SlotCard (VariantProcessor& p, int s) : proc (p), slot (s), type (p.getSlotType (s)),
        power (p.apvts, VariantProcessor::slotId (s, "On"), "", Palette::peach, fr ("Activer / contourner cet effet"))
    {
        const auto& d = fxs::def (type);
        title = upperFr (p.fxNames[(size_t) s].isNotEmpty() ? p.fxNames[(size_t) s] : fr (d.name));
        subtitle = fr (d.desc);

        for (int k = 0; k < fxs::numP; ++k)
        {
            const auto& pd = d.params[(size_t) k];
            if (pd.name == nullptr || juce::String (pd.name).isEmpty()) continue;
            auto* knob = knobs.add (new LabeledKnob (p.apvts, VariantProcessor::slotId (s, "P" + juce::String (k + 1)), fr (pd.name), accent(), fr (pd.name)));
            auto fn = pd.text;
            knob->setTextFunction ([fn] (double v) { return fn ? fn ((float) v) : juce::String (v, 2); });
            addAndMakeVisible (knob);
        }
        mix = std::make_unique<LabeledKnob> (p.apvts, VariantProcessor::slotId (s, "Mix"), "Mix", Palette::dim, fr ("Dosage avec le son d'origine"));
        if (type == fxs::Shaper) { gross = std::make_unique<GrossPanel> (p, s, accent()); addAndMakeVisible (*gross); }
        else if (type == fxs::Glitch) { glitch = std::make_unique<GlitchPanel> (p, s, accent()); addAndMakeVisible (*glitch); }
        else if (type == fxs::EQ) { eq = std::make_unique<EqPanel> (p, s, accent()); addAndMakeVisible (*eq); }
        else if (type == fxs::Liquid) { liquid = std::make_unique<LiquidPanel> (p, s, accent()); addAndMakeVisible (*liquid); }
        else { visual = std::make_unique<FxVisual> (p, s, type, accent()); addAndMakeVisible (*visual); }
        addAndMakeVisible (*mix);

        presets.setTextWhenNothingSelected (fr ("Presets…"));
        int id = 1;
        for (auto& pr : d.presets) presets.addItem (fr (pr.name), id++);
        presets.onChange = [this] { const int i = presets.getSelectedItemIndex(); if (i >= 0) proc.applyEffectPreset (slot, i); };
        // flèches : preset précédent / suivant sans ouvrir la liste
        prevPreset.setButtonText (juce::String::fromUTF8 ("◀"));
        nextPreset.setButtonText (juce::String::fromUTF8 ("▶"));
        prevPreset.setTooltip (fr ("Preset précédent"));
        nextPreset.setTooltip (fr ("Preset suivant"));
        prevPreset.onClick = [this] { stepPreset (-1); };
        nextPreset.onClick = [this] { stepPreset (1); };
        addAndMakeVisible (prevPreset);
        addAndMakeVisible (nextPreset);

        for (auto* b : { &up, &down, &remove, &randomize, &detachBtn }) addAndMakeVisible (b);
        detach::makeButton (detachBtn, [this] { if (auto& f = detach::open()) { auto* pp = &proc; const int s2 = slot; f ([pp, s2] { return std::make_unique<SlotCard> (*pp, s2); }, title, 860, heightFor (type)); } });
        randomize.setTooltip (fr ("Réglages au hasard autour des presets : toujours musical"));
        randomize.onClick = [this] { proc.randomizeEffect (slot); presets.setSelectedItemIndex (-1, juce::dontSendNotification); };
        up.setTooltip (fr ("Monter dans la chaîne"));
        down.setTooltip (fr ("Descendre dans la chaîne"));
        remove.setTooltip (fr ("Retirer l'effet"));
        up.onClick = [this] { proc.moveEffect (slot, -1); if (onChanged) onChanged(); };
        down.onClick = [this] { proc.moveEffect (slot, 1); if (onChanged) onChanged(); };
        remove.onClick = [this] { auto* pp = &proc; const int s2 = slot; proc.fxAction ([pp, s2] { pp->removeEffect (s2); }); if (onChanged) onChanged(); };

        addAndMakeVisible (power);
        addAndMakeVisible (presets);
    }

    void stepPreset (int dir)
    {
        const int n = presets.getNumItems();
        if (n <= 0) return;
        const int i = presets.getSelectedItemIndex();
        presets.setSelectedItemIndex (i < 0 ? (dir > 0 ? 0 : n - 1) : (i + dir + n) % n, juce::sendNotificationSync);
    }

    std::function<void()> onChanged;

    void paint (juce::Graphics& g) override
    {
        const auto rf = getLocalBounds().toFloat();
        const bool on = proc.getParam (VariantProcessor::slotId (slot, "On")) > 0.5f;
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (rf, 12.0f);
        g.setColour (on ? accent().withAlpha (0.5f) : Palette::line);
        g.drawRoundedRectangle (rf.reduced (0.5f), 12.0f, 1.0f);

        auto t = getLocalBounds().reduced (16, 10).removeFromTop (24);
        g.setColour (Palette::dim);
        g.setFont (uiFont (11.0f));
        g.drawText (juce::String (slot + 1), t.removeFromLeft (16), juce::Justification::centredLeft);
        g.setColour (accent());
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ (float) t.getX() + 4.0f, (float) t.getCentreY() }));
        g.setColour (Palette::text);
        g.setFont (uiFont (13.0f, juce::Font::bold).withExtraKerningFactor (0.1f));
        g.drawText (title, t.withTrimmedLeft (16).removeFromLeft (130), juce::Justification::centredLeft);
        g.setColour (Palette::dim);
        g.setFont (uiFont (11.5f));
        g.drawText (subtitle, t.withTrimmedLeft (150).withTrimmedRight (508), juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 8);
        auto top = r.removeFromTop (28);
        remove.setBounds (top.removeFromRight (28).reduced (1));
        top.removeFromRight (4);
        detachBtn.setBounds (top.removeFromRight (28).reduced (1));
        top.removeFromRight (4);
        down.setBounds (top.removeFromRight (28).reduced (1));
        top.removeFromRight (4);
        up.setBounds (top.removeFromRight (28).reduced (1));
        top.removeFromRight (10);
        power.setBounds (top.removeFromRight (44));
        top.removeFromRight (6);
        nextPreset.setBounds (top.removeFromRight (26).reduced (0, 1));
        top.removeFromRight (3);
        presets.setBounds (top.removeFromRight (160).reduced (0, 1));
        top.removeFromRight (3);
        prevPreset.setBounds (top.removeFromRight (26).reduced (0, 1));
        top.removeFromRight (6);
        randomize.setBounds (top.removeFromRight (76).reduced (0, 1));
        r.removeFromTop (8);
        // grand visuel à gauche, réglages en grille à droite (2 rangées si besoin)
        if (collapsed) return;
        if (gross) gross->setBounds (r.removeFromLeft (560)); else if (glitch) glitch->setBounds (r.removeFromLeft (520));
        else if (eq) eq->setBounds (r.removeFromLeft (600)); else if (liquid) liquid->setBounds (r.removeFromLeft (520)); else visual->setBounds (r.removeFromLeft (230));
        r.removeFromLeft (12);
        const int n = knobs.size() + 1;
        const int perRow = n > 5 ? (n + 1) / 2 : n;
        const int rows = (n + perRow - 1) / perRow;
        const int w = juce::jmin (110, r.getWidth() / perRow);
        const int h = r.getHeight() / rows;
        int idx = 0;
        auto place = [&] (juce::Component& c) { const int row = idx / perRow, col = idx % perRow; c.setBounds (r.getX() + col * w, r.getY() + row * h, w, h); ++idx; };
        for (auto* k : knobs) place (*k);
        place (*mix);
    }

    // clic droit sur la carte : actions rapides (retirer, réinitialiser, annuler…)
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu()) { if (onExpand && (collapsed || e.y < 44)) onExpand (slot); return; }   // clic sur le titre d'un effet ouvert = le replier
        const bool on = proc.getParam (VariantProcessor::slotId (slot, "On")) > 0.5f;
        juce::PopupMenu m;
        m.addSectionHeader (title);
        m.addItem (1, on ? fr ("Contourner (bypass)") : fr ("Réactiver"));
        m.addItem (2, fr ("Réinitialiser (premier preset)"));
        m.addItem (3, fr ("Réglages au hasard"));
        m.addItem (4, fr ("Monter dans la chaîne"), slot > 0);
        m.addItem (5, fr ("Descendre dans la chaîne"));
        m.addItem (6, fr ("Retirer l'effet"));
        m.addSeparator();
        m.addItem (9, fr ("Renommer l'effet…"));
        m.addItem (7, fr ("Annuler la dernière action  (Ctrl+Z)"));
        m.addItem (8, fr ("Rétablir  (Ctrl+Y)"));
        juce::Component::SafePointer<SlotCard> self (this);
        m.showMenuAsync (juce::PopupMenu::Options(), [safe = self] (int r)
        {
            if (safe == nullptr || r == 0) return;
            auto& p = safe->proc;
            const int s = safe->slot;
            auto changed = safe->onChanged;
            switch (r)
            {
                case 1: p.setParam (VariantProcessor::slotId (s, "On"), p.getParam (VariantProcessor::slotId (s, "On")) > 0.5f ? 0.0f : 1.0f); break;
                case 2: p.fxAction ([&p, s] { p.applyEffectPreset (s, 0); }); break;
                case 3: p.fxAction ([&p, s] { p.randomizeEffect (s); }); break;
                case 4: p.fxAction ([&p, s] { p.moveEffect (s, -1); }); if (changed) changed(); return;
                case 5: p.fxAction ([&p, s] { p.moveEffect (s, 1); }); if (changed) changed(); return;
                case 6: p.fxAction ([&p, s] { p.removeEffect (s); }); if (changed) changed(); return;
                case 7: p.undoAny(); if (changed) changed(); return;
                case 8: p.redoAny(); if (changed) changed(); return;
                case 9: safe->askRename(); return;
                default: break;
            }
            if (safe != nullptr) safe->repaint();
        });
    }

    // effet seul (Variant Reverb, Variant Delay…) : pas de flèches, de bouton retirer ni de fenêtre détachée
    void setSingleMode()
    {
        for (juce::Component* b : { (juce::Component*) &up, (juce::Component*) &down, (juce::Component*) &remove, (juce::Component*) &detachBtn }) b->setVisible (false);
        singleMode = true;
    }
    // renommer l'effet (double-clic sur son nom, ou clic droit) : le nom est gardé dans le projet
    void askRename()
    {
        auto* w = new juce::AlertWindow (fr ("Renommer l'effet"), fr ("Nouveau nom (vide = nom d'origine) :"), juce::AlertWindow::NoIcon);
        w->addTextEditor ("n", proc.fxNames[(size_t) slot].isNotEmpty() ? proc.fxNames[(size_t) slot] : fr (fxs::def (type).name));
        w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton (fr ("Annuler"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<SlotCard> self (this);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([self, w] (int res)
        {
            if (res != 1 || self == nullptr) return;
            auto n = w->getTextEditorContents ("n").trim().substring (0, 40);
            if (n == fr (fxs::def (self->type).name)) n = {};
            self->proc.fxNames[(size_t) self->slot] = n;
            self->title = upperFr (n.isNotEmpty() ? n : fr (fxs::def (self->type).name));
            self->repaint();
        }), true);
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override { if (e.y < 40) askRename(); }

    // carte repliée : seulement le titre et les boutons (une seule carte ouverte à la fois)
    std::function<void (int)> onExpand;
    int getSlot() const { return slot; }
    bool isCollapsed() const { return collapsed; }
    void setCollapsed (bool c)
    {
        collapsed = c;
        for (auto* k : knobs) k->setVisible (! c);
        if (mix) mix->setVisible (! c);
        for (juce::Component* p : { (juce::Component*) visual.get(), (juce::Component*) gross.get(), (juce::Component*) glitch.get(), (juce::Component*) eq.get(), (juce::Component*) liquid.get() })
            if (p != nullptr) p->setVisible (! c);
        resized(); repaint();
    }
    void animate (float dt) { if (gross) { gross->time += dt; gross->repaint(); } else if (glitch) { glitch->time += dt; glitch->repaint(); } else if (eq) { eq->time += dt; } else if (liquid) { liquid->time += dt; liquid->repaint(); } else { visual->time += dt; visual->repaint(); } }
    static constexpr int cardHeight = 236;
    static int heightFor (int t) { return t == fxs::Shaper ? 400 : (t == fxs::Glitch ? 350 : (t == fxs::EQ ? 290 : (t == fxs::Liquid ? 300 : cardHeight))); }
    int getType() const { return type; }

private:
    juce::Colour accent() const
    {
        const juce::Colour cols[] = { Palette::peach, Palette::pink, Palette::blue, Palette::mint, Palette::lavender };
        return cols[type % 5];
    }

    VariantProcessor& proc;
    int slot, type;
    ParamToggle power;
    juce::String title, subtitle;
    juce::OwnedArray<LabeledKnob> knobs;
    std::unique_ptr<LabeledKnob> mix;
    std::unique_ptr<FxVisual> visual;
    std::unique_ptr<GrossPanel> gross;
    std::unique_ptr<GlitchPanel> glitch;
    std::unique_ptr<EqPanel> eq;
    std::unique_ptr<LiquidPanel> liquid;
    juce::ComboBox presets;
    juce::TextButton prevPreset, nextPreset;
    juce::TextButton up { juce::String::fromUTF8 ("↑") }, down { juce::String::fromUTF8 ("↓") }, remove { juce::String::fromUTF8 ("✕") }, randomize { fr ("Hasard") };
    juce::TextButton detachBtn;
    bool collapsed = false, singleMode = false;
};

// Tuile d'effet : icône animée, à glisser dans la chaîne (ou cliquer)
class FxTile : public juce::Component, public juce::SettableTooltipClient
{
public:
    FxTile (int t) : type (t)
    {
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        setTooltip (fr (fxs::def (t).name) + "  —  " + fr (fxs::def (t).desc) + "\n" + fr ("Glisse-le dans la chaîne, ou clique pour l'ajouter"));
    }
    std::function<void (int)> onAdd;
    float time = 0.0f;

    void paint (juce::Graphics& g) override
    {
        const juce::Colour cols[] = { Palette::peach, Palette::pink, Palette::blue, Palette::mint, Palette::lavender };
        const auto accent = cols[type % 5];
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        if (over)
        {
            g.setColour (accent.withAlpha (0.18f));
            g.fillRoundedRectangle (r.expanded (2.0f), 14.0f);
        }
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (r, 12.0f);
        g.setColour (over ? accent : Palette::line);
        g.drawRoundedRectangle (r, 12.0f, 1.0f);

        auto iconArea = r.reduced (10.0f, 6.0f).removeFromTop (30.0f);
        g.setColour (accent.withAlpha (0.08f));
        g.fillRoundedRectangle (iconArea.expanded (4.0f, 2.0f), 8.0f);
        fxs::drawIcon (g, type, iconArea, over ? time * 1.8f : time, accent);

        g.setColour (Palette::text);
        g.setFont (uiFont (13.5f, juce::Font::bold));
        g.drawText (fr (fxs::def (type).name), r.withTrimmedTop (40.0f).removeFromTop (17.0f), juce::Justification::centred, true);
        // description lisible : sur 2 lignes, sous le nom (plus de texte écrasé)
        g.setColour (Palette::text.withAlpha (0.62f));
        g.setFont (uiFont (11.0f));
        g.drawFittedText (fr (fxs::def (type).desc), r.withTrimmedTop (57.0f).withTrimmedBottom (3.0f).toNearestInt().reduced (6, 0),
                          juce::Justification::centredTop, 2, 0.85f);
    }

    void mouseEnter (const juce::MouseEvent&) override { over = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { over = false; repaint(); }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging || e.getDistanceFromDragStart() < 6) return;
        if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor (this))
        {
            dragging = true;
            c->startDragging ("fx:" + juce::String (type), this);
        }
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (! dragging && onAdd) onAdd (type);
        dragging = false;
    }

private:
    int type;
    bool over = false, dragging = false;
};

class FxContent : public juce::Component, public juce::DragAndDropTarget, private juce::Timer
{
public:
    friend class VariantEditor;   // visite guidée : montre les cartes
    explicit FxContent (VariantProcessor& p) : proc (p)
    {
       #ifdef VARIANT_SINGLE_FX
        if (false)   // plugin d'un seul effet : pas de palette, juste la carte de l'effet
       #endif
        for (int ty = 1; ty < fxs::NumTypes; ++ty)
        {
            auto* tile = tiles.add (new FxTile (ty));
            tile->onAdd = [this] (int type) { addAt (type, -1); };
            addAndMakeVisible (tile);
        }
        addAndMakeVisible (width);
        addAndMakeVisible (out);
        rebuild();
       #ifndef VARIANT_SINGLE_FX
        addAndMakeVisible (randChain);
       #endif
        randChain.setColour (juce::TextButton::buttonColourId, Palette::peach.withAlpha (0.28f));
        randChain.setTooltip (fr ("Crée une chaîne d'effets au hasard mais cohérente (choisis un style ou laisse faire)"));
        randChain.onClick = [this]
        {
            juce::PopupMenu m;
            m.addSectionHeader (fr ("Chaîne d'effets au hasard"));
            const auto names = VariantProcessor::fxChainStyleNames();
            m.addItem (1, fr ("Surprise (style au hasard)"));
            for (int i = 0; i < names.size(); ++i) m.addItem (10 + i, names[i]);
            juce::Component::SafePointer<FxContent> self (this);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&randChain), [safe = self] (int r)
            {
                if (safe == nullptr || r == 0) return;
                safe->proc.randomFxChain (r == 1 ? -1 : r - 10);
                safe->openSlots = { 0 };
                safe->rebuild();
            });
        };
        startTimerHz (30);
    }

    std::function<void (int)> onHeightChanged;
    std::function<void (int)> onEffectAdded;

    void tick()
    {
        std::array<int, fxs::numSlots> ty {};
        for (int s = 0; s < fxs::numSlots; ++s) ty[(size_t) s] = proc.getSlotType (s);
        if (ty != lastTypes) rebuild();
        if (++repaintTick % 10 == 0) for (auto* c : cards) c->repaint();
    }

    static constexpr int collapsedH = 48;
    int cardH (const SlotCard* c) const { return openSlots.count (c->getSlot()) ? SlotCard::heightFor (c->getType()) : collapsedH; }
    int neededHeight() const { int h = chainTop + 40; for (auto* c : cards) h += cardH (c) + 8; return cards.isEmpty() ? h + SlotCard::cardHeight : h; }

    // glisser-déposer depuis la palette
    bool isInterestedInDragSource (const SourceDetails& d) override { return d.description.toString().startsWith ("fx:"); }
    void itemDragEnter (const SourceDetails&) override { dropHighlight = true; repaint(); }
    void itemDragMove (const SourceDetails& d) override { dropY = d.localPosition.y; repaint(); }
    void itemDragExit (const SourceDetails&) override { dropHighlight = false; repaint(); }
    void itemDropped (const SourceDetails& d) override
    {
        dropHighlight = false;
        const int type = d.description.toString().fromFirstOccurrenceOf ("fx:", false, false).getIntValue();
        int index = 0;
        for (auto* c : cards) if (d.localPosition.y > c->getBounds().getCentreY()) ++index;
        addAt (type, index);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (Palette::text.withAlpha (0.65f));
        g.setFont (uiFont (12.5f, juce::Font::bold).withExtraKerningFactor (0.08f));
       #ifndef VARIANT_SINGLE_FX
        g.drawText (fr ("EFFETS DISPONIBLES  ·  glisse-les dans ta chaîne"), paletteTitle, juce::Justification::centredLeft, true);
        g.drawText (fr ("TA CHAÎNE D'EFFETS  ·  le son passe de haut en bas"), chainTitle, juce::Justification::centredLeft, true);
       #endif

        auto zone = getLocalBounds().withTrimmedTop (chainTop - 6).reduced (0, 0);
        if (cards.isEmpty() || dropHighlight)
        {
            auto e = cards.isEmpty() ? zone.removeFromTop (140) : zone;
            juce::Path border, dashed;
            border.addRoundedRectangle (e.toFloat().reduced (1.0f), 14.0f);
            const float dashes[] = { 7.0f, 5.0f };
            juce::PathStrokeType (1.5f).createDashedStroke (dashed, border, dashes, 2);
            g.setColour (dropHighlight ? Palette::peach.withAlpha (0.08f) : Palette::panel.withAlpha (0.5f));
            g.fillRoundedRectangle (e.toFloat(), 14.0f);
            g.setColour (dropHighlight ? Palette::peach : Palette::line.brighter (0.3f));
            g.fillPath (dashed);
            if (cards.isEmpty())
            {
                g.setColour (Palette::dim);
                g.setFont (uiFont (15.0f));
                g.drawFittedText (fr ("Glisse un effet ici (ou clique sur une tuile au-dessus)"), e.reduced (20), juce::Justification::centred, 2);
            }
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (24);
        out.setBounds (getWidth() - 80, 0, 80, 92);
        width.setBounds (getWidth() - 165, 0, 80, 92);
        paletteTitle = top.withTrimmedRight (180);
        r.removeFromTop (6);
        const int cols = 7, gap = 6;
        const int tw = (r.getWidth() - 175 - (cols - 1) * gap) / cols;
        const int th = 88;   // assez haut pour lire la description de chaque effet
        const int rowsN = (tiles.size() + cols - 1) / cols;
        for (int i = 0; i < tiles.size(); ++i)
        {
            const int row = i / cols, col = i % cols;
            const int extra = row == 0 ? 0 : 0;
            tiles[i]->setBounds (r.getX() + col * (tw + gap) + extra, r.getY() + row * (th + gap), tw, th);
        }
        chainTitle = juce::Rectangle<int> (0, r.getY() + rowsN * (th + gap) + 8, getWidth(), 22);
        chainTop = chainTitle.getBottom() + 8;
        auto c = getLocalBounds().withTrimmedTop (chainTop);
        for (auto* card : cards) { card->setBounds (c.removeFromTop (cardH (card))); c.removeFromTop (8); }
        randChain.setBounds (chainTitle.withTrimmedLeft (chainTitle.getWidth() - 190).withSizeKeepingCentre (190, 26));
    }

private:
    void timerCallback() override
    {
        if (! isShowing()) return;
        for (auto* tl : tiles) { tl->time += 1.0f / 30.0f; tl->repaint(); }
        for (auto* c : cards) c->animate (1.0f / 30.0f);
    }

    void addAt (int type, int index)
    {
        const int s = proc.addEffect (type);
        if (s < 0) return;
        if (index >= 0) for (int k = s; k > index; --k) proc.moveEffect (k, -1);
        openSlots.insert (index >= 0 ? index : s);   // l'effet ajouté s'ouvre (les autres restent comme ils sont : plusieurs effets ouverts à la fois)
        if (onEffectAdded) onEffectAdded (type);
        rebuild();
    }

    void rebuild()
    {
        for (int s = 0; s < fxs::numSlots; ++s) lastTypes[(size_t) s] = proc.getSlotType (s);
        cards.clear();
        for (int s = 0; s < fxs::numSlots; ++s)
            if (lastTypes[(size_t) s] > 0)
            {
                auto* c = cards.add (new SlotCard (proc, s));
                juce::Component::SafePointer<FxContent> safe (this);
                c->setCollapsed (openSlots.count (s) == 0);
                c->onExpand = [safe] (int slot) { if (safe != nullptr) { auto& o = safe->openSlots; if (o.count (slot)) o.erase (slot); else o.insert (slot); safe->rebuild(); } };
                c->onChanged = [safe]
                {
                    juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe.getComponent()->rebuild(); });
                };
                addAndMakeVisible (c);
            }
        if (onHeightChanged) onHeightChanged (neededHeight());
        resized();
        repaint();
    }

    VariantProcessor& proc;
    juce::OwnedArray<FxTile> tiles;
    LabeledKnob width { proc.apvts, "spcWidth", "Largeur", Palette::dim, fr ("Largeur stéréo finale (0 % = mono)") };
    LabeledKnob out   { proc.apvts, "spcOut", "Sortie", Palette::dim, fr ("Volume final du plugin") };
    juce::OwnedArray<SlotCard> cards;
    std::array<int, fxs::numSlots> lastTypes {};
    juce::Rectangle<int> paletteTitle, chainTitle;
    int repaintTick = 0, chainTop = 280, dropY = 0;
    std::set<int> openSlots { 0 };   // effets ouverts (plusieurs à la fois)
    juce::TextButton randChain { fr ("Chaîne au hasard") + juce::String::fromUTF8 ("  ▾") };
    bool dropHighlight = false;
};
class FxPage : public juce::Component
{
public:
    friend class VariantEditor;   // visite guidée
    explicit FxPage (VariantProcessor& p) : content (p), scroller (content, 600)
    {
        content.onHeightChanged = [this] (int h) { scroller.setContentHeight (h); };
        content.onEffectAdded = [this] (int ty) { if (onEffectAdded) onEffectAdded (ty); };
        scroller.setContentHeight (content.neededHeight());
        addAndMakeVisible (scroller);
    }
    void resized() override { scroller.setBounds (getLocalBounds()); }
    void tick() { content.tick(); }
    std::function<void (int)> onEffectAdded;

private:
    FxContent content;
    ScrollPage scroller;
};

//==============================================================================
// Bouton d'accord : maintenir pour écouter
class ChordPad : public juce::Component, public juce::SettableTooltipClient
{
public:
    ChordPad (VariantProcessor& p, juce::Colour c) : proc (p), colour (c) {}

    void set (const juce::String& top, const juce::String& bottom, std::vector<int> n) { topText = top; bottomText = bottom; notes = std::move (n); repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (down ? colour.withAlpha (0.35f) : (isMouseOver() ? Palette::panel2.brighter (0.1f) : Palette::panel2));
        g.fillRoundedRectangle (r, 9.0f);
        g.setColour (down ? colour : Palette::line);
        g.drawRoundedRectangle (r, 9.0f, 1.0f);
        g.setColour (colour);
        g.setFont (uiFont (11.5f, juce::Font::bold));
        g.drawText (topText, r.removeFromTop (r.getHeight() * 0.42f).withTrimmedTop (5.0f), juce::Justification::centred);
        g.setColour (Palette::text);
        g.setFont (uiFont (13.5f));
        g.drawFittedText (bottomText, r.toNearestInt().reduced (4, 0).withTrimmedBottom (6), juce::Justification::centred, 1, 0.6f);
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }
    void mouseDown (const juce::MouseEvent&) override  { down = true;  proc.playUiNotes (notes, true);  repaint(); }
    void mouseUp (const juce::MouseEvent&) override    { down = false; proc.playUiNotes (notes, false); repaint(); }

private:
    VariantProcessor& proc;
    juce::Colour colour;
    juce::String topText, bottomText;
    std::vector<int> notes;
    bool down = false;
};

//==============================================================================
// Éditeur de progression : glisser pour déplacer, bord droit pour allonger, double-clic = ajouter
class ProgTimeline : public juce::Component
{
public:
    ProgTimeline (VariantProcessor& p, int& sel) : proc (p), selected (sel) {}
    std::function<void()> onSelectionChanged;

protected:
    float keysW = 0.0f;   // largeur du clavier de piano à gauche (piano roll)
    double hStart = 0.0, hLen = 0.0;   // fenêtre visible en temps (hLen = 0 : toute la progression)
    double viewLen() const { const double L = juce::jmax (1.0, proc.prog.lengthBeats); return hLen > 0.0 ? juce::jmin (hLen, L) : L; }
    double viewStart() const { return juce::jlimit (0.0, juce::jmax (0.0, proc.prog.lengthBeats - viewLen()), hStart); }
    float beatToX (double b) const { return keysW + (float) ((b - viewStart()) / viewLen() * (getWidth() - keysW)); }
    double xToBeat (float x) const { return viewStart() + (x - keysW) / juce::jmax (1.0f, (float) getWidth() - keysW) * viewLen(); }

    int chordAt (float x) const
    {
        const double b = xToBeat (x);
        for (int i = (int) proc.prog.chords.size(); --i >= 0;)
        {
            const auto& c = proc.prog.chords[(size_t) i];
            if (b >= c.start && b < c.start + c.length) return i;
        }
        return -1;
    }

    void drawGrid (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        const int beats = (int) proc.prog.lengthBeats;
        for (int b = 0; b <= beats; ++b)
        {
            const float x = area.getX() + beatToX (b);
            g.setColour (b % 4 == 0 ? Palette::line.brighter (0.25f) : Palette::line.withAlpha (0.5f));
            g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
        }
    }

    void notifyAll()
    {
        if (onSelectionChanged) onSelectionChanged();
        if (auto* p = getParentComponent()) p->repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = chordAt (e.position.x);
        selected = i;
        dragMode = 0;
        if (i >= 0)
        {
            const auto& c = proc.prog.chords[(size_t) i];
            dragStartBeat = c.start;
            dragStartLen = c.length;
            dragMode = std::abs (e.position.x - beatToX (c.start + c.length)) < 8.0f ? 2 : 1;
            dragNotes = c.close;
            heard = c.voiced.empty() ? c.close : c.voiced;
            proc.playUiNotes (heard, true, 0.7f);
        }
        notifyAll();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (selected < 0 || dragMode == 0) return;
        if (! heard.empty()) { proc.playUiNotes (heard, false); heard.clear(); }
        const double delta = std::round (xToBeat ((float) e.getDistanceFromDragStartX()) * 2.0) / 2.0;   // aimanté au demi-temps
        if (dragMode == 1) proc.moveGuideChord (selected, dragStartBeat + delta);
        else               proc.resizeGuideChord (selected, dragStartLen + delta);
        if (auto* p = getParentComponent()) p->repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (! heard.empty()) { proc.playUiNotes (heard, false); heard.clear(); }
        if (selected >= 0 && dragMode != 0)
        {
            const double st = proc.prog.chords[(size_t) selected].start;
            proc.renderGuide();   // trie les accords dans l'ordre
            for (int k = 0; k < (int) proc.prog.chords.size(); ++k)
                if (std::abs (proc.prog.chords[(size_t) k].start - st) < 1.0e-6 && proc.prog.chords[(size_t) k].close == dragNotes) selected = k;
        }
        dragMode = 0;
        notifyAll();
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int i = chordAt (e.position.x);
        bool edge = false;
        if (i >= 0) { const auto& c = proc.prog.chords[(size_t) i]; edge = std::abs (e.position.x - beatToX (c.start + c.length)) < 8.0f; }
        setMouseCursor (edge ? juce::MouseCursor::LeftRightResizeCursor : (i >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor));
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (chordAt (e.position.x) < 0) { selected = proc.addGuideChord (xToBeat (e.position.x)); notifyAll(); }
    }

    VariantProcessor& proc;
    int& selected;
    int dragMode = 0;
    double dragStartBeat = 0, dragStartLen = 0;
    std::vector<int> dragNotes, heard;
};

class ChordLane : public ProgTimeline
{
public:
    using ProgTimeline::ProgTimeline;
    bool big = false;

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        g.setColour (Palette::panel2.withAlpha (0.5f));
        g.fillRoundedRectangle (area, 8.0f);
        drawGrid (g, area);
        for (int i = 0; i < (int) proc.prog.chords.size(); ++i)
        {
            const auto& c = proc.prog.chords[(size_t) i];
            auto cell = juce::Rectangle<float> (beatToX (c.start) + 2.0f, 3.0f, beatToX (c.start + c.length) - beatToX (c.start) - 4.0f, area.getHeight() - 6.0f);
            const auto accent = c.outside ? Palette::peach : Palette::mint;
            const bool sel = i == selected;
            g.setColour (sel ? accent.withAlpha (0.35f) : accent.withAlpha (0.14f));
            g.fillRoundedRectangle (cell, 7.0f);
            g.setColour (sel ? accent : accent.withAlpha (0.55f));
            g.drawRoundedRectangle (cell, 7.0f, sel ? 1.6f : 1.0f);
            g.fillRoundedRectangle (cell.withLeft (cell.getRight() - 4.0f).reduced (0.0f, cell.getHeight() * 0.3f), 2.0f);   // poignée

            auto t = cell.reduced (5.0f, 4.0f);
            if (big)
            {
                g.setColour (accent);
                g.setFont (uiFont (11.5f, juce::Font::bold));
                g.drawFittedText (c.roman, t.removeFromTop (18.0f).toNearestInt(), juce::Justification::centred, 1, 0.6f);
                g.setColour (Palette::text);
                g.setFont (uiFont (cell.getWidth() > 90 ? 17.0f : 13.0f));
                g.drawFittedText (c.name, t.toNearestInt(), juce::Justification::centred, 2, 0.6f);
            }
            else
            {
                g.setColour (Palette::text);
                g.setFont (uiFont (12.5f, juce::Font::bold));
                g.drawFittedText (c.name, t.removeFromTop (t.getHeight() * 0.55f).toNearestInt(), juce::Justification::centredLeft, 1, 0.6f);
                g.setColour (accent);
                g.setFont (uiFont (10.5f));
                g.drawFittedText (c.roman, t.toNearestInt(), juce::Justification::centredLeft, 1, 0.6f);
            }
        }
        if (proc.isPreviewing())
        {
            g.setColour (Palette::text);
            g.fillRect (juce::Rectangle<float> (getWidth() * proc.getPreviewProgress() - 1.0f, 0.0f, 2.0f, (float) getHeight()));
        }
    }
};

class PianoRoll : public ProgTimeline, public juce::FileDragAndDropTarget
{
public:
    using ProgTimeline::ProgTimeline;
    enum Tool { Draw = 0, Paint, Erase, Slice, Select, Slide, Stamp };
    bool editNotes = true;   // on clique les notes directement (comme dans FL)
    bool manualRange = false;   // défilement / zoom vertical choisi à la molette
    int viewLo = 48, viewSpan = 36;
    int selectedNote = -1;
    int tool = Draw;
    double snap = 0.25;                 // en temps ; 0 = pas d'aimant
    std::set<int> sel;
    static std::vector<gen::Note>& clipboard() { static std::vector<gen::Note> c; return c; }
    static constexpr float velH = 44.0f;   // bande de vélocité (comme FL)

    static constexpr float rulerH = 18.0f;   // règle des mesures en haut (comme FL)

    void paint (juce::Graphics& g) override
    {
        keysW = 54.0f;
        editNotes = true;   // le piano et l'édition des notes ne font qu'un
        auto area = getLocalBounds().toFloat();
        if (! dragging) computeRange();
        const float rh = noteAreaH() / (float) (hi - lo + 1);
        const float gridW = area.getWidth() - keysW;
        const bool lightUi = Palette::light;
        const auto rowWhite = lightUi ? Palette::panel.darker (0.02f) : Palette::bg.brighter (0.07f);
        const auto rowBlack = lightUi ? Palette::panel2.darker (0.06f) : Palette::bg.darker (0.25f);

        g.setColour (rowBlack);
        g.fillRect (area);
        g.saveState();   // les notes ne passent pas sous le clavier ni sous la règle
        g.reduceClipRegion (juce::Rectangle<float> (keysW, rulerH, gridW, area.getHeight() - rulerH).toNearestInt());
        // lignes : touches blanches plus claires, noires plus sombres, gamme surlignée
        for (int n = lo; n <= hi; ++n)
        {
            const int pc = n % 12;
            const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
            const float y = rowY (n);
            g.setColour (black ? rowBlack : rowWhite);
            g.fillRect (keysW, y, gridW, rh);
            if (hlScale >= 0 && theory::snapToScale (n, hlKey, theory::scaleAt (hlScale)) == n)
            { g.setColour (Palette::mint.withAlpha (pc == hlKey ? 0.12f : 0.05f)); g.fillRect (keysW, y, gridW, rh); }
            g.setColour (juce::Colours::black.withAlpha (lightUi ? 0.05f : 0.22f));
            g.drawHorizontalLine ((int) (y + rh), keysW, area.getRight());
            if (pc == 0) { g.setColour (Palette::line.brighter (0.15f)); g.drawHorizontalLine ((int) (y + rh), keysW, area.getRight()); }
        }
        // une mesure sur deux légèrement teintée (repère visuel de FL)
        const int beats = (int) std::ceil (proc.prog.lengthBeats);
        for (int bar = 0; bar * 4 < beats; ++bar)
            if (bar % 2 == 1)
            {
                g.setColour (Palette::text.withAlpha (lightUi ? 0.025f : 0.022f));
                g.fillRect (juce::Rectangle<float> (beatToX (bar * 4.0), rulerH, beatToX (bar * 4.0 + 4.0) - beatToX (bar * 4.0), rollH() - rulerH));
            }
        if (snap > 0.0 && (beatToX (snap) - beatToX (0.0)) > 5.0f)   // grille de l'aimant
        {
            g.setColour (Palette::line.withAlpha (0.30f));
            for (double b = snap; b < proc.prog.lengthBeats; b += snap) g.drawVerticalLine ((int) beatToX (b), rulerH, rollH());
        }
        for (int b = 0; b <= beats; ++b)   // temps et mesures
        {
            g.setColour (b % 4 == 0 ? Palette::line.brighter (lightUi ? 0.0f : 0.35f) : Palette::line.withAlpha (0.75f));
            g.drawVerticalLine ((int) beatToX (b), rulerH, rollH());
        }

        if (juce::isPositiveAndBelow (selected, (int) proc.prog.chords.size()))   // accord sélectionné (celui de la note cliquée)
        {
            const auto& c = proc.prog.chords[(size_t) selected];
            g.setColour (Palette::text.withAlpha (0.035f));
            g.fillRect (juce::Rectangle<float> (beatToX (c.start), rulerH, beatToX (c.start + c.length) - beatToX (c.start), rollH() - rulerH));
        }

        const auto parts = proc.enabledParts();
        const juce::Colour cols[] = { Palette::mint, Palette::blue, Palette::lavender };
        for (int pass = 2; pass >= 0; --pass)   // arpège, basse, puis accords par-dessus
        {
            const int part = pass == 2 ? 2 : (pass == 1 ? 1 : 0);
            if (! parts[(size_t) part]) continue;
            for (int i = 0; i < (int) proc.notes.size(); ++i)
            {
                const auto& n = proc.notes[(size_t) i];
                if (n.part != part) continue;
                auto rect = noteRect (n);
                const bool isSel = editNotes && sel.count (i) > 0;
                const auto base = cols[part].interpolatedWith (Palette::bg, 0.35f * (1.0f - n.vel));
                const auto body = isSel ? Palette::pink.brighter (0.15f) : base;
                // note pleine, bord sombre net, liseré clair en haut (relief simple, pas de halo)
                g.setColour (body);
                g.fillRect (rect);
                g.setColour (body.brighter (0.35f));
                g.fillRect (rect.withHeight (juce::jmin (1.5f, rect.getHeight() * 0.25f)));
                g.setColour (body.darker (0.45f));
                g.fillRect (rect.withWidth (juce::jmin (2.0f, rect.getWidth() * 0.2f)));
                g.setColour (juce::Colours::black.withAlpha (0.55f));
                g.drawRect (rect, 1.0f);
                if (rect.getWidth() > 24.0f && rect.getHeight() >= 9.0f)   // nom de la note dans la note, comme FL
                {
                    g.setColour (juce::Colours::black.withAlpha (0.78f));
                    g.setFont (uiFont (juce::jmin (11.0f, rect.getHeight() - 1.0f), juce::Font::bold));
                    g.drawText (theory::noteName (n.pitch), rect.reduced (4.0f, 0.0f), juce::Justification::centredLeft, false);
                }
                if (n.slide)
                {
                    // note slide (comme FL) : forme en triangle, elle fait glisser les notes de sa couleur jusqu'à elle
                    juce::Path tri;
                    tri.addTriangle (rect.getX(), rect.getBottom(), rect.getRight(), rect.getY(), rect.getRight(), rect.getBottom());
                    g.setColour (body.darker (0.25f)); g.fillPath (tri);
                    g.setColour (Palette::text.withAlpha (0.9f));
                    juce::Path arrow;
                    const float cy = rect.getCentreY(), x0 = rect.getX() - 10.0f;
                    arrow.startNewSubPath (x0, cy); arrow.lineTo (rect.getX() - 2.0f, cy);
                    arrow.addTriangle (rect.getX() - 4.0f, cy - 3.0f, rect.getX() - 4.0f, cy + 3.0f, rect.getX(), cy);
                    g.strokePath (arrow, juce::PathStrokeType (1.4f));
                    if (rect.getWidth() > 34.0f && rect.getHeight() >= 9.0f)
                    {
                        g.setFont (uiFont (juce::jmin (10.0f, rect.getHeight() - 1.0f), juce::Font::bold));
                        g.drawText ("slide", rect.reduced (4.0f, 0.0f), juce::Justification::centredRight, false);
                    }
                }
                if (isSel)
                {
                    g.setColour (Palette::text);
                    g.drawRect (rect.expanded (1.0f), 1.0f);
                }
            }
        }

        if (editNotes && rubber) { g.setColour (Palette::blue.withAlpha (0.12f)); g.fillRect (rubberRect); g.setColour (Palette::blue); g.drawRect (rubberRect, 1.0f); }
        g.restoreState();

        // bande de vélocité : tiges fines à tête carrée (comme FL)
        {
            auto v = area.withTrimmedTop (rollH()).withTrimmedLeft (keysW);
            g.setColour (lightUi ? Palette::panel2 : Palette::bg.darker (0.35f));
            g.fillRect (v);
            g.setColour (Palette::line.withAlpha (0.5f));
            for (float q : { 0.5f })
                g.drawHorizontalLine ((int) (v.getBottom() - 3.0f - (v.getHeight() - 8.0f) * q), v.getX(), v.getRight());
            for (int b = 0; b <= beats; b += 4) g.drawVerticalLine ((int) beatToX (b), v.getY(), v.getBottom());
            g.setColour (Palette::line.brighter (0.2f));
            g.drawHorizontalLine ((int) v.getY(), 0.0f, v.getRight());
            g.saveState();
            g.reduceClipRegion (v.toNearestInt());
            for (int i = 0; i < (int) proc.notes.size(); ++i)
            {
                const auto& n = proc.notes[(size_t) i];
                if (! parts[(size_t) juce::jlimit (0, 2, n.part)]) continue;
                const float x = beatToX (n.start), hgt = (v.getHeight() - 8.0f) * n.vel;
                g.setColour ((sel.count (i) ? Palette::pink : cols[juce::jlimit (0, 2, n.part)]).withAlpha (0.9f));
                g.fillRect (x, v.getBottom() - 3.0f - hgt, 1.0f, hgt);
                g.fillRect (x - 2.0f, v.getBottom() - 5.0f - hgt, 5.0f, 5.0f);
            }
            g.restoreState();
            auto lab = area.withTrimmedTop (rollH()).withWidth (keysW);
            g.setColour (Palette::panel2.darker (0.15f)); g.fillRect (lab);
            g.setColour (Palette::dim); g.setFont (uiFont (10.5f, juce::Font::bold));
            g.drawText ("VEL", lab, juce::Justification::centred);
        }

        // règle : numéros de mesure, graduations, accord sélectionné, tête de lecture
        {
            auto ru = juce::Rectangle<float> (keysW, 0.0f, gridW, rulerH);
            g.setGradientFill (juce::ColourGradient (Palette::panel2.brighter (lightUi ? 0.0f : 0.05f), 0.0f, 0.0f, Palette::panel2.darker (0.15f), 0.0f, rulerH, false));
            g.fillRect (ru);
            g.saveState();
            g.reduceClipRegion (ru.toNearestInt());
            if (juce::isPositiveAndBelow (selected, (int) proc.prog.chords.size()))
            {
                const auto& c = proc.prog.chords[(size_t) selected];
                g.setColour (Palette::mint.withAlpha (0.45f));
                g.fillRect (juce::Rectangle<float> (beatToX (c.start), rulerH - 4.0f, beatToX (c.start + c.length) - beatToX (c.start), 4.0f));
            }
            for (int b = 0; b <= beats; ++b)
            {
                const float x = beatToX (b);
                const bool barLine = b % 4 == 0;
                g.setColour (barLine ? Palette::text.withAlpha (0.55f) : Palette::dim.withAlpha (0.5f));
                g.drawVerticalLine ((int) x, barLine ? 2.0f : rulerH - 6.0f, rulerH);
                if (barLine)
                {
                    g.setColour (Palette::text.withAlpha (0.85f));
                    g.setFont (uiFont (10.5f, juce::Font::bold));
                    g.drawText (juce::String (b / 4 + 1), juce::Rectangle<float> (x + 3.0f, 1.0f, 30.0f, rulerH - 3.0f), juce::Justification::centredLeft, false);
                }
            }
            {
                // point de départ de la lecture (clic sur la règle)
                const float sx = beatToX (proc.guideStart);
                juce::Path m; m.addTriangle (sx - 6.0f, 1.0f, sx + 6.0f, 1.0f, sx, rulerH - 3.0f);
                g.setColour (Palette::mint); g.fillPath (m);
            }
            g.restoreState();
            g.setColour (juce::Colours::black.withAlpha (0.4f));
            g.drawHorizontalLine ((int) rulerH - 1, keysW, area.getRight());
        }
        if (proc.isPreviewing())   // tête de lecture : part du début des notes, pas du clavier
        {
            const float x = beatToX (proc.getPreviewProgress() * proc.prog.lengthBeats);
            if (x >= keysW)
            {
                g.setColour (Palette::peach.withAlpha (0.9f));
                g.fillRect (juce::Rectangle<float> (x, rulerH, 1.0f, rollH() - rulerH));
                juce::Path tri; tri.addTriangle (x - 5.0f, 2.0f, x + 6.0f, 2.0f, x + 0.5f, rulerH - 2.0f);
                g.fillPath (tri);
            }
        }

        // clavier de piano dessiné par-dessus : vraies touches (blanches dégradées, noires en relief), nom des do
        {
            const auto kb = juce::Rectangle<float> (0.0f, rulerH, keysW, rollH() - rulerH);
            g.saveState();
            g.reduceClipRegion (kb.toNearestInt());
            g.setColour (juce::Colour (0xff9a9aa2));
            g.fillRect (kb);
            for (int n = lo; n <= hi; ++n)
            {
                const int pc = n % 12;
                const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
                if (black) continue;
                // la touche blanche couvre aussi la moitié des noires voisines
                const bool blackBelow = pc == 2 || pc == 4 || pc == 7 || pc == 9 || pc == 11;
                const bool blackAbove = pc == 0 || pc == 2 || pc == 5 || pc == 7 || pc == 9;
                const float top = rowY (n) - (blackAbove ? rh * 0.5f : 0.0f);
                const float bot = rowY (n) + rh + (blackBelow ? rh * 0.5f : 0.0f);
                const auto key = juce::Rectangle<float> (0.0f, top, keysW - 1.0f, bot - top);
                const bool hit = n == heardNote;
                g.setGradientFill (juce::ColourGradient (hit ? Palette::mint : juce::Colour (0xfff4f4f6), 0.0f, 0.0f,
                                                         hit ? Palette::mint.darker (0.2f) : juce::Colour (0xffd6d6dc), keysW, 0.0f, false));
                g.fillRect (key.reduced (0.0f, 0.5f));
                g.setColour (juce::Colours::black.withAlpha (0.35f));
                g.drawHorizontalLine ((int) key.getBottom(), 0.0f, keysW - 1.0f);
                if (pc == 0 && rh > 4.5f)
                {
                    g.setColour (juce::Colour (0xff3a3a46));
                    g.setFont (uiFont (juce::jmin (10.5f, rh * 1.4f + 2.0f), juce::Font::bold));
                    g.drawText (theory::noteName (n), key.reduced (3.0f, 0.0f), juce::Justification::centredRight);
                }
            }
            for (int n = lo; n <= hi; ++n)
            {
                const int pc = n % 12;
                if (! (pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10)) continue;
                const auto key = juce::Rectangle<float> (0.0f, rowY (n) + 0.5f, keysW * 0.6f, rh - 1.0f);
                const bool hit = n == heardNote;
                g.setGradientFill (juce::ColourGradient (hit ? Palette::mint.darker (0.2f) : juce::Colour (0xff3a3a44), 0.0f, key.getY(),
                                                         hit ? Palette::mint.darker (0.5f) : juce::Colour (0xff121216), key.getRight(), key.getY(), false));
                g.fillRoundedRectangle (key, 1.5f);
                g.setColour (juce::Colours::white.withAlpha (hit ? 0.3f : 0.12f));
                g.fillRect (key.withHeight (1.0f).reduced (1.0f, 0.0f));
            }
            g.restoreState();
            // ombre du clavier sur la grille
            g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (lightUi ? 0.10f : 0.35f), keysW, 0.0f, juce::Colours::black.withAlpha (0.0f), keysW + 8.0f, 0.0f, false));
            g.fillRect (juce::Rectangle<float> (keysW, rulerH, 8.0f, rollH() - rulerH));
            // coin au-dessus du clavier : aimant actuel
            auto corner = juce::Rectangle<float> (0.0f, 0.0f, keysW, rulerH);
            g.setColour (Palette::panel2.darker (0.15f)); g.fillRect (corner);
            g.setColour (Palette::dim); g.setFont (uiFont (10.0f, juce::Font::bold));
            g.drawText (snap <= 0.0 ? juce::String ("libre") : (snap >= 1.0 ? juce::String ((int) snap) + " t" : "1/" + juce::String ((int) std::round (4.0 / snap))), corner, juce::Justification::centred);
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.drawVerticalLine ((int) keysW - 1, 0.0f, (float) getHeight());
        }
        if (hLen > 0.0 && hLen < proc.prog.lengthBeats)   // barre de défilement horizontale quand on a zoomé
        {
            const float w = (float) getWidth() - keysW, L = (float) proc.prog.lengthBeats;
            g.setColour (Palette::text.withAlpha (0.35f));
            g.fillRoundedRectangle (keysW + (float) viewStart() / L * w, rollH() - 4.0f, (float) viewLen() / L * w, 3.0f, 1.5f);
        }
        g.setColour (Palette::line);
        g.drawRect (area, 1.0f);
        if (dropOver) { g.setColour (Palette::peach.withAlpha (0.15f)); g.fillRect (area); }
    }

    // zoom (Ctrl + molette, ou les boutons de la barre d'outils)
    void zoomH (double factor, float atX = -1.0f)
    {
        const double L = juce::jmax (1.0, proc.prog.lengthBeats);
        const double anchor = atX >= 0.0f ? xToBeat (atX) : viewStart() + viewLen() * 0.5;
        const double rel = (anchor - viewStart()) / viewLen();
        const double nl = juce::jlimit (1.0, L, viewLen() * factor);
        hLen = nl >= L - 1.0e-6 ? 0.0 : nl;
        hStart = juce::jlimit (0.0, juce::jmax (0.0, L - nl), anchor - rel * nl);
        repaint();
    }
    void zoomV (int delta)
    {
        if (! manualRange) { viewLo = lo; viewSpan = hi - lo; manualRange = true; }
        const int mid = viewLo + viewSpan / 2;
        viewSpan = juce::jlimit (12, 84, viewSpan + delta);
        viewLo = juce::jlimit (0, 127 - viewSpan, mid - viewSpan / 2);
        repaint();
    }
    void zoomFit() { hLen = 0.0; hStart = 0.0; manualRange = false; repaint(); }

    // ---------------------------------------------------------------- souris
    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        const auto pos = e.position;
        mode = None;
        if (pos.y >= rollH()) { proc.beginEdit(); mode = Velocity; setVelAt (pos); return; }
        if (pos.y < rulerH) { if (pos.x >= keysW) { proc.setGuideStart (snapBeat (xToBeat (pos.x), e.mods.isAltDown(), false)); repaint(); } return; }   // règle : la lecture démarre ici (comme FL)
        if (pos.x < keysW)   // clic sur le clavier : on entend la note (glisser = glissando)
        {
            heardNote = pitchAt (pos.y);
            proc.playUiNotes ({ heardNote }, true, 0.8f);
            mode = Keys; repaintAll();
            return;
        }

        int i = noteAt (pos);
        const double beat = snapBeat (xToBeat (pos.x), e.mods.isAltDown(), true);
        const int pitch = pitchAt (pos.y);
        if (e.mods.isPopupMenu() || tool == Erase)   // clic droit = gomme, comme FL
        {
            proc.beginEdit();
            if (i >= 0) { proc.deleteGuideNotes ({ i }); sel.clear(); }
            mode = Erasing;
            repaintAll();
            return;
        }
        if (tool == Slice) { if (i >= 0) proc.splitGuideNote (i, beat); repaintAll(); return; }
        if (tool == Slide) { if (i >= 0) proc.toggleGuideSlide (i); repaintAll(); return; }
        if (tool == Stamp && i < 0)
        {
            // tampon d'accord (comme le « Stamp » de FL) : un accord de la gamme posé d'un clic
            const auto& sc = theory::scaleAt (proc.getScale());
            const int root = theory::snapToScale (pitch, proc.getKey(), sc);
            std::vector<gen::Note> chord;
            for (int k = 0, step = 0; k < stampSize && step < 24; ++step)
            {
                const int nn = root + step;
                if (theory::snapToScale (nn, proc.getKey(), sc) != nn) continue;
                // empilement en tierces : 1re, 3e, 5e, 7e… notes de la gamme à partir de la fondamentale
                int idx = 0; for (int q = root; q < nn; ++q) if (theory::snapToScale (q, proc.getKey(), sc) == q) ++idx;
                if (idx % 2 == 0) { chord.push_back ({ 0.0, lastLen, nn, 0.8f, defaultPart() }); ++k; }
            }
            proc.beginEdit();
            auto idxs = proc.pasteGuideNotes (chord, beat);
            sel.clear(); for (int x : idxs) sel.insert (x);
            tool = Draw;   // retour au crayon : on peut reposer des notes une par une
            if (onToolChanged) onToolChanged (Draw);
            repaintAll();
            return;
        }
        if ((tool == Select && i < 0) || (e.mods.isCtrlDown() && ! e.mods.isAltDown()))   // Ctrl + glisser = sélection (comme FL)
        {
            if (! e.mods.isShiftDown()) sel.clear();
            rubber = true; rubberStart = pos; rubberRect = { pos, pos };
            mode = Rubber;
            repaintAll();
            return;
        }

        if (i < 0)   // crayon / pinceau : nouvelle note à la dernière longueur utilisée
        {
            i = proc.addGuideNote (beat, pitch, lastLen, defaultPart());
            sel = { i };
            if (slideMode) proc.toggleGuideSlide (i);   // bouton Slide : la note glisse depuis la précédente
            proc.playUiNotes ({ pitch }, true, 0.8f);
            heardNote = pitch;
            if (tool == Paint) { mode = Painting; lastPaintBeat = beat; paintPitch = pitch; repaintAll(); return; }
        }
        else
        {
            if (e.mods.isShiftDown()) { if (sel.count (i)) sel.erase (i); else sel.insert (i); }
            else if (! sel.count (i)) sel = { i };
            heardNote = proc.notes[(size_t) i].pitch;
            proc.playUiNotes ({ heardNote }, true, proc.notes[(size_t) i].vel);
            lastLen = proc.notes[(size_t) i].length;
        }
        selectedNote = i;
        {
            // l'accord qui contient cette note devient l'accord sélectionné (panneau du dessous)
            const double st = proc.notes[(size_t) i].start;
            selected = -1;
            for (int k = 0; k < (int) proc.prog.chords.size(); ++k)
                if (st >= proc.prog.chords[(size_t) k].start - 1.0e-6 && st < proc.prog.chords[(size_t) k].start + proc.prog.chords[(size_t) k].length) selected = k;
        }
        const auto& n = proc.notes[(size_t) i];
        mode = std::abs (pos.x - noteRect (n).getRight()) < 7.0f ? Resize : Move;
        proc.beginEdit();
        originals.clear();
        for (int s : sel) if (juce::isPositiveAndBelow (s, (int) proc.notes.size())) originals.push_back ({ s, proc.notes[(size_t) s] });
        anchorPitch = n.pitch;
        dragging = true;
        duplicated = false;
        repaintAll();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto pos = e.position;
        switch (mode)
        {
            case Velocity: setVelAt (pos); break;
            case Keys:
            {
                const int p = pitchAt (juce::jmin (pos.y, rollH() - 1.0f));
                if (p != heardNote) { if (heardNote >= 0) proc.playUiNotes ({ heardNote }, false); heardNote = p; proc.playUiNotes ({ p }, true, 0.8f); }
                break;
            }
            case Erasing:
            {
                const int i = noteAt (pos);
                if (i >= 0) proc.deleteGuideNotes ({ i });
                break;
            }
            case Rubber:
            {
                rubberRect = juce::Rectangle<float> (rubberStart, pos);
                sel.clear();
                for (int i = 0; i < (int) proc.notes.size(); ++i) if (noteRect (proc.notes[(size_t) i]).intersects (rubberRect)) sel.insert (i);
                break;
            }
            case Painting:
            {
                const double step = snap > 0.0 ? snap : lastLen;
                const double b = snapBeat (xToBeat (pos.x), e.mods.isAltDown(), true);
                if (b >= lastPaintBeat + step - 1.0e-6)
                {
                    lastPaintBeat = b;
                    sel.insert (proc.addGuideNote (b, paintPitch, juce::jmin (lastLen, step), defaultPart()));
                }
                break;
            }
            case Move: case Resize:
            {
                if (mode == Move && e.mods.isShiftDown() && ! duplicated && ! originals.empty())
                {
                    // Maj + glisser = copie (comme FL) : on duplique puis on déplace la copie
                    duplicated = true;
                    double minStart = 1.0e9;
                    for (auto& o : originals) minStart = juce::jmin (minStart, o.second.start);
                    std::vector<gen::Note> clip;
                    for (auto& o : originals) { auto c = o.second; c.start -= minStart; clip.push_back (c); }
                    const auto idxs = proc.pasteGuideNotes (clip, minStart);
                    std::vector<std::pair<int, gen::Note>> copies;
                    for (size_t k = 0; k < idxs.size() && k < originals.size(); ++k) copies.push_back ({ idxs[k], originals[k].second });
                    originals = copies;
                    sel.clear(); for (int x : idxs) sel.insert (x);
                }
                const double rawDb = xToBeat ((float) e.getDistanceFromDragStartX()) - xToBeat (0.0f);
                const double db = (snap > 0.0 && ! e.mods.isAltDown()) ? std::round (rawDb / snap) * snap : rawDb;
                const float rh = noteAreaH() / (float) (hi - lo + 1);
                const int dp = mode == Move ? -(int) std::round ((float) e.getDistanceFromDragStartY() / rh) : 0;
                for (auto& o : originals)
                {
                    if (! juce::isPositiveAndBelow (o.first, (int) proc.notes.size())) continue;
                    if (mode == Move) proc.moveGuideNote (o.first, o.second.start + db, o.second.pitch + dp);
                    else proc.resizeGuideNote (o.first, juce::jmax (snap > 0.0 ? snap : 0.0625, o.second.length + db));
                }
                if (mode == Resize && ! originals.empty()) lastLen = proc.notes[(size_t) originals.front().first].length;
                if (mode == Move && anchorPitch + dp != heardNote && heardNote >= 0)
                {
                    proc.playUiNotes ({ heardNote }, false);
                    heardNote = anchorPitch + dp;
                    proc.playUiNotes ({ heardNote }, true, 0.8f);
                }
                break;
            }
            default: break;
        }
        repaintAll();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (heardNote >= 0) { proc.playUiNotes ({ heardNote }, false); heardNote = -1; }
        dragging = false;
        rubber = false;
        mode = None;
        repaintAll();
    }

    void mouseDoubleClick (const juce::MouseEvent&) override {}

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        // comme FL : molette = monter / descendre, Maj + molette = défiler dans le temps,
        // Ctrl + molette = zoom horizontal, Ctrl + Maj + molette = zoom vertical, Alt + molette sur une note = vélocité
        const float d = std::abs (w.deltaY) >= std::abs (w.deltaX) ? w.deltaY : w.deltaX;
        if (std::abs (d) < 1.0e-4f) return;
        const bool up = d > 0.0f;
        const auto m = e.mods;
        if (m.isAltDown())
        {
            const int i = noteAt (e.position);
            if (i >= 0) proc.setGuideNoteVelocity (i, proc.notes[(size_t) i].vel + (up ? 0.06f : -0.06f));
        }
        else if (m.isCtrlDown() || m.isCommandDown())
        {
            if (m.isShiftDown()) zoomV (up ? -4 : 4);
            else zoomH (up ? 0.8 : 1.25, e.position.x);
        }
        else if (m.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
        {
            const double L = proc.prog.lengthBeats;
            hStart = juce::jlimit (0.0, juce::jmax (0.0, L - viewLen()), viewStart() + (up ? -1.0 : 1.0) * viewLen() * 0.15);
        }
        else
        {
            if (! manualRange) { viewLo = lo; viewSpan = hi - lo; manualRange = true; }
            viewLo = juce::jlimit (0, 127 - viewSpan, viewLo + (up ? 2 : -2));
        }
        repaintAll();
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        if (e.position.y >= rollH()) { setMouseCursor (juce::MouseCursor::UpDownResizeCursor); return; }
        const int i = noteAt (e.position);
        const bool edge = i >= 0 && std::abs (e.position.x - noteRect (proc.notes[(size_t) i]).getRight()) < 7.0f;
        setMouseCursor (tool == Erase ? juce::MouseCursor::CrosshairCursor
                      : edge ? juce::MouseCursor::LeftRightResizeCursor
                      : (i >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor));
    }

    // ---------------------------------------------------------------- clavier (comme FL)
    bool keyAction (const juce::KeyPress& k)
    {
        if (! editNotes) return false;
        const auto m = k.getModifiers();
        const int code = k.getKeyCode();
        std::vector<int> s (sel.begin(), sel.end());
        if (m.isCommandDown() && (code == 'A' || code == 'a')) { sel.clear(); for (int i = 0; i < (int) proc.notes.size(); ++i) sel.insert (i); repaintAll(); return true; }
        if (m.isCommandDown() && (code == 'C' || code == 'c')) { copySel(); return true; }
        if (m.isCommandDown() && (code == 'V' || code == 'v')) { paste(); return true; }
        if (m.isCommandDown() && (code == 'B' || code == 'b')) { copySel(); paste (true); return true; }
        if (m.isCommandDown() && (code == 'Q' || code == 'q')) { quantize(); return true; }
        if (m.isCommandDown() && (code == 'L' || code == 'l')) { legato(); return true; }
        if (m.isCommandDown() && (code == 'D' || code == 'd')) { sel.clear(); repaintAll(); return true; }   // tout désélectionner
        if (m.isCommandDown() && (code == 'I' || code == 'i')) { std::set<int> inv; for (int i = 0; i < (int) proc.notes.size(); ++i) if (! sel.count (i)) inv.insert (i); sel = inv; repaintAll(); return true; }   // inverser la sélection
        if (s.empty()) return false;
        if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) { proc.deleteGuideNotes (s); sel.clear(); repaintAll(); return true; }
        if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
        {
            proc.transposeGuideNotes (s, (code == juce::KeyPress::upKey ? 1 : -1) * (m.isShiftDown() ? 12 : 1));
            repaintAll();
            return true;
        }
        if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)
        {
            proc.beginEdit();
            proc.shiftGuideNotes (s, (code == juce::KeyPress::rightKey ? 1.0 : -1.0) * (snap > 0.0 ? snap : 0.25), 0);
            repaintAll();
            return true;
        }
        return false;
    }

    void quantize() { std::vector<int> s (sel.begin(), sel.end()); if (s.empty()) for (int i = 0; i < (int) proc.notes.size(); ++i) s.push_back (i); proc.quantizeGuideNotes (s, snap > 0.0 ? snap : 0.25); repaintAll(); }
    void legato()   { std::vector<int> s (sel.begin(), sel.end()); if (s.empty()) for (int i = 0; i < (int) proc.notes.size(); ++i) s.push_back (i); proc.legatoGuideNotes (s); repaintAll(); }

    // ---------------------------------------------------------------- glisser un .mid
    bool isInterestedInFileDrag (const juce::StringArray& f) override { return f.size() > 0 && juce::File (f[0]).hasFileExtension ("mid;midi"); }
    void fileDragEnter (const juce::StringArray&, int, int) override { dropOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropOver = false; repaint(); }
    void filesDropped (const juce::StringArray& f, int, int) override
    {
        dropOver = false;
        if (proc.importMidiFile (juce::File (f[0]))) { editNotes = true; sel.clear(); }
        repaintAll();
    }

private:
    enum Mode { None, Move, Resize, Rubber, Painting, Erasing, Velocity, Keys };

    float rollH() const { return (float) getHeight() - (editNotes ? velH : 0.0f); }
    float noteAreaH() const { return juce::jmax (10.0f, rollH() - rulerH); }

    double snapBeat (double b, bool free, bool floorIt) const
    {
        if (free || snap <= 0.0) return juce::jmax (0.0, b);
        return juce::jmax (0.0, (floorIt ? std::floor (b / snap) : std::round (b / snap)) * snap);
    }

    void setVelAt (juce::Point<float> p)
    {
        const float laneTop = rollH(), laneH = (float) getHeight() - laneTop - 8.0f;
        const float v = juce::jlimit (0.05f, 1.0f, ((float) getHeight() - 3.0f - p.y) / laneH);
        std::vector<int> hit;
        for (int i = 0; i < (int) proc.notes.size(); ++i)
            if (std::abs (beatToX (proc.notes[(size_t) i].start) - p.x) < 5.0f && (sel.empty() || sel.count (i))) hit.push_back (i);
        if (hit.empty())
            for (int i = 0; i < (int) proc.notes.size(); ++i)
                if (std::abs (beatToX (proc.notes[(size_t) i].start) - p.x) < 5.0f) hit.push_back (i);
        if (! hit.empty()) proc.setGuideNotesVelocity (hit, v);
    }

    void copySel()
    {
        auto& c = clipboard(); c.clear();
        double first = 1.0e9;
        for (int i : sel) if (juce::isPositiveAndBelow (i, (int) proc.notes.size())) first = juce::jmin (first, proc.notes[(size_t) i].start);
        for (int i : sel) if (juce::isPositiveAndBelow (i, (int) proc.notes.size())) { auto n = proc.notes[(size_t) i]; n.start -= first; c.push_back (n); }
    }

    void paste (bool afterSelection = false)
    {
        auto& c = clipboard();
        if (c.empty()) return;
        double at = 0.0, len = 0.0;
        for (auto& n : c) len = juce::jmax (len, n.start + n.length);
        if (! sel.empty())
        {
            double first = 1.0e9, last = 0.0;
            for (int i : sel) if (juce::isPositiveAndBelow (i, (int) proc.notes.size())) { first = juce::jmin (first, proc.notes[(size_t) i].start); last = juce::jmax (last, proc.notes[(size_t) i].start + proc.notes[(size_t) i].length); }
            at = afterSelection ? first + std::ceil ((last - first) / juce::jmax (0.25, snap)) * juce::jmax (0.25, snap) : last;
        }
        const auto added = proc.pasteGuideNotes (c, at);
        sel.clear();
        for (int i : added) sel.insert (i);
        repaintAll();
    }

    void computeRange()
    {
        if (manualRange) { lo = viewLo; hi = viewLo + viewSpan; return; }
        lo = 127; hi = 0;
        const auto parts = proc.enabledParts();
        for (auto& n : proc.notes) if (parts[(size_t) juce::jlimit (0, 2, n.part)]) { lo = juce::jmin (lo, n.pitch); hi = juce::jmax (hi, n.pitch); }
        if (lo > hi) { lo = 48; hi = 72; }
        lo -= 3; hi += 3;
        if (hi - lo < 24) { const int mid = (lo + hi) / 2; lo = mid - 12; hi = mid + 12; }
    }

    float rowY (int pitch) const { const float rh = noteAreaH() / (float) (hi - lo + 1); return rollH() - (float) (pitch - lo + 1) * rh; }
    int pitchAt (float y) const { const float rh = noteAreaH() / (float) (hi - lo + 1); return lo + (int) std::floor ((rollH() - y) / rh); }

    juce::Rectangle<float> noteRect (const gen::Note& n) const
    {
        const float rh = noteAreaH() / (float) (hi - lo + 1);
        return { beatToX (n.start), rowY (n.pitch) + 0.5f, juce::jmax (3.0f, beatToX (n.start + n.length) - beatToX (n.start) - 1.0f), juce::jmax (2.0f, rh - 1.0f) };
    }

    int noteAt (juce::Point<float> p) const
    {
        const auto parts = proc.enabledParts();
        for (int i = (int) proc.notes.size(); --i >= 0;)
        {
            const auto& n = proc.notes[(size_t) i];
            if (parts[(size_t) juce::jlimit (0, 2, n.part)] && noteRect (n).expanded (0.0f, 1.0f).contains (p)) return i;
        }
        return -1;
    }

    int defaultPart() const
    {
        const auto parts = proc.enabledParts();
        if (parts[0]) return 0;
        if (parts[2]) return 2;
        return 1;
    }

    void repaintAll() { if (onSelectionChanged) onSelectionChanged(); if (auto* p = getParentComponent()) p->repaint(); repaint(); }

    int lo = 48, hi = 72, heardNote = -1, anchorPitch = 60, paintPitch = 60;
public:
    int stampSize = 4;   // notes du tampon d'accord (3 = triade, 4 = septième…)
    bool slideMode = false;   // bouton Slide (comme FL) : les nouvelles notes glissent
    int hlKey = 0, hlScale = -1;   // gamme surlignée dans le fond (-1 = aucune)
    std::function<void (int)> onToolChanged;
    bool duplicated = false;
private:
    Mode mode = None;
    bool dragging = false, rubber = false, dropOver = false;
    juce::Point<float> rubberStart;
    juce::Rectangle<float> rubberRect;
    double lastLen = 1.0, lastPaintBeat = 0.0;
    std::vector<std::pair<int, gen::Note>> originals;
};
// Bouton d'outil du piano roll (icône + nom, comme la barre d'outils de FL)
class RollToolButton : public juce::TextButton
{
public:
    RollToolButton (const juce::String& name, int iconIndex) : juce::TextButton (name), icon (iconIndex) {}

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        // bouton plat de barre d'outils : icône + nom, l'outil actif est souligné (pas de pastille arrondie)
        auto r = getLocalBounds().toFloat().reduced (1.0f, 2.0f);
        const bool on = getToggleState();
        const auto acc = Palette::peach;
        if (on || over)
        {
            g.setColour (on ? acc.withAlpha (0.20f) : Palette::text.withAlpha (0.06f));
            g.fillRoundedRectangle (r, 3.0f);
        }
        if (on)
        {
            g.setColour (acc);
            g.fillRect (r.getX() + 3.0f, r.getBottom() - 2.0f, r.getWidth() - 6.0f, 2.0f);
        }
        const auto ink = on ? acc.brighter (Palette::light ? -0.3f : 0.25f) : Palette::text.withAlpha (over ? 0.95f : 0.72f);
        auto ic = r.removeFromLeft (r.getHeight()).reduced (6.5f);
        if (down) ic = ic.translated (0.0f, 1.0f);
        juce::Path p;
        const float x = ic.getX(), y = ic.getY(), w = ic.getWidth(), h = ic.getHeight();
        switch (icon)
        {
            case 0: p.startNewSubPath (x + 1, y + h - 1); p.lineTo (x + w - 3, y + 3); p.lineTo (x + w - 1, y + 5); p.lineTo (x + 3, y + h - 1); p.closeSubPath(); break;   // crayon
            case 1: p.addEllipse (x + 1, y + h * 0.5f, w * 0.45f, h * 0.45f); p.startNewSubPath (x + w * 0.4f, y + h * 0.6f); p.lineTo (x + w - 1, y + 1); break;          // pinceau
            case 2: p.addRoundedRectangle (x + 1, y + h * 0.35f, w - 2, h * 0.45f, 2.0f); p.startNewSubPath (x + w * 0.45f, y + h * 0.35f); p.lineTo (x + w * 0.45f, y + h * 0.8f); break;   // gomme
            case 3: p.startNewSubPath (x + w * 0.5f, y); p.lineTo (x + w * 0.5f, y + h); p.addTriangle (x + w * 0.5f, y + h * 0.15f, x + w, y + h * 0.5f, x + w * 0.5f, y + h * 0.5f); break;   // couteau
            case 4: { const float dl[] = { 3.0f, 2.0f }; juce::Path q; q.addRectangle (ic.reduced (1.0f)); juce::PathStrokeType (1.4f).createDashedStroke (p, q, dl, 2); break; }   // sélection
            case 5: p.startNewSubPath (x, y + h - 1); p.quadraticTo (x + w * 0.4f, y + h - 1, x + w * 0.55f, y + h * 0.3f); p.lineTo (x + w, y + h * 0.3f); break;   // slide
            default: for (int k = 0; k < 3; ++k) p.addRectangle (x + 1, y + h - 3.0f - k * (h * 0.3f), w - 2, 2.2f); break;   // tampon d'accord
        }
        g.setColour (ink);
        if (icon == 4) g.fillPath (p);
        else g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setFont (uiFont (12.0f, on ? juce::Font::bold : juce::Font::plain));
        g.drawFittedText (getButtonText(), r.reduced (2.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.75f);
    }

private:
    int icon;
};

//==============================================================================
// Onglet ACCORDS & MIDI (fusionné, la page défile)
//==============================================================================
class ChordMidiContent : public juce::Component
{
public:
    friend class VariantEditor;   // visite guidée : montre les cartes
    static constexpr int contentHeight = 1640;

    explicit ChordMidiContent (VariantProcessor& p) : proc (p)
    {
        for (auto* c : std::initializer_list<juce::Component*> {
                 &chordOn, &mode, &style, &key, &scale, &type, &size, &voicing, &reg, &inversion, &strum, &humanize, &spice,
                 &mood, &bars, &rich, &genReg, &genSpice, &genVariety, &layChords, &layBass, &layArp, &generate, &listen, &surprise,
                 &viewRoll, &viewNames, &editNotesBtn, &resetNotes, &buildChords, &otherIdea, &lane, &roll, &quality,
                 &kbOff, &kbNotes, &kbChords, &kbLayout, &kbOctDown, &kbOctUp, &recButton, &recQuant, &cutToggle, &noOverlap, &inv, &octDown, &octUp, &change, &duplicate, &del, &addChord,
                 &chipChords, &chipBass, &chipArp, &chipAll, &exportAll, &openFolder, &status, &detachGen, &detachExport, &likeBtn, &dislikeBtn, &tasteBtn })
            addAndMakeVisible (c);

        style.box.setTextWhenNothingSelected (fr ("Choisir…"));
        style.box.onChange = [this]
        {
            const int i = style.box.getSelectedItemIndex();
            if (i >= 0) { selectedMood = i; proc.applyChordStyle (i); proc.setParam ("chordSpice", moods::colourFor (i).spice); refreshGuide(); }
        };

        for (int i = 0; i < 8; ++i)
        {
            auto* pad = pads.add (new ChordPad (proc, Palette::pink));
            pad->setTooltip (fr ("Maintiens pour écouter cet accord"));
            addChildComponent (pad);
            auto* o = outPads.add (new ChordPad (proc, Palette::peach));
            o->setTooltip (fr ("Accord hors gamme qui sonne bien : maintiens pour l'écouter"));
            addAndMakeVisible (o);
        }

        // --- générateur
        auto& gs = proc.genSettings;
        mood.box.setSelectedItemIndex (gs.mood, juce::dontSendNotification);
        bars.box.setSelectedItemIndex (gs.bars > 4 ? 1 : 0, juce::dontSendNotification);
        rich.box.setSelectedItemIndex (gs.richness, juce::dontSendNotification);
        layChords.setToggleState (gs.chords, juce::dontSendNotification);
        layBass.setToggleState (gs.bass, juce::dontSendNotification);
        layArp.setToggleState (gs.arp, juce::dontSendNotification);
        for (auto* t : { &layChords, &layBass, &layArp }) t->setColour (juce::ToggleButton::tickColourId, Palette::mint);
        mood.box.onChange = [this]
        {
            proc.genSettings.mood = mood.box.getSelectedItemIndex();
            proc.setParam ("scale", (float) moods::list()[(size_t) proc.genSettings.mood].scales.front());
            proc.regenerateGuide (true);
            selectedChord = -1;
            selectionChanged();
        };
        bars.box.onChange = [this] { proc.setGuideLength (bars.box.getSelectedItemIndex() == 1 ? 8 : 4); selectionChanged(); };
        rich.box.onChange = [this] { proc.genSettings.richness = rich.box.getSelectedItemIndex(); };
        layChords.onClick = [this] { proc.genSettings.chords = layChords.getToggleState(); proc.renderGuide(); };
        layBass.onClick   = [this] { proc.genSettings.bass   = layBass.getToggleState();   proc.renderGuide(); };
        layArp.onClick    = [this] { proc.genSettings.arp    = layArp.getToggleState();    proc.renderGuide(); };
        generate.setColour (juce::TextButton::buttonColourId, Palette::mint.withAlpha (0.25f));
        generate.setTooltip (fr ("Nouvelle progression complète dans cette ambiance"));
        generate.onClick = [this] { proc.regenerateGuide (true); selectedChord = -1; selectionChanged(); };
        listen.onClick = [this] { proc.previewGuide (! proc.isPreviewing()); };
        surprise.setColour (juce::TextButton::buttonColourId, Palette::peach.withAlpha (0.25f));
        surprise.setTooltip (fr ("Ambiance, richesse, couleur et arpège au hasard, puis nouvelle progression"));
        detachGen.setTooltip (fr ("Ouvre le générateur dans une petite fenêtre à placer où tu veux"));
        detachGen.onClick = [this] { if (onDetachMini) onDetachMini (0); };
        likeBtn.setColour (juce::TextButton::buttonColourId, Palette::mint.withAlpha (0.3f));
        likeBtn.setTooltip (fr ("Cette progression te plaît : le générateur s'en inspirera (avec des variations)"));
        likeBtn.onClick = [this] { proc.rateProgression (true); refreshTaste(); };
        dislikeBtn.setTooltip (fr ("Pas ton style : le générateur évitera ce genre d'enchaînement, et en propose un autre"));
        dislikeBtn.onClick = [this] { proc.rateProgression (false); proc.regenerateGuide (true); selectedChord = -1; selectionChanged(); };
        tasteBtn.setClickingTogglesState (true);
        tasteBtn.setColour (juce::TextButton::buttonOnColourId, Palette::peach);
        tasteBtn.setTooltip (fr ("Activé : les nouvelles progressions partent souvent de celles que tu as aimées"));
        tasteBtn.onClick = [this] { proc.tasteOn = tasteBtn.getToggleState(); proc.saveTaste(); refreshTaste(); };
        refreshTaste();
        detachExport.setTooltip (fr ("Ouvre l'export vers FL dans une petite fenêtre à placer où tu veux"));
        detachExport.onClick = [this] { if (onDetachMini) onDetachMini (1); };
        surprise.onClick = [this]
        {
            proc.surpriseGuide();
            auto& g2 = proc.genSettings;
            mood.box.setSelectedItemIndex (g2.mood, juce::dontSendNotification);
            rich.box.setSelectedItemIndex (g2.richness, juce::dontSendNotification);
            layArp.setToggleState (g2.arp, juce::dontSendNotification);
            selectedChord = -1;
            selectionChanged();
        };

        // --- éditeur
        for (auto* b : { &viewRoll, &viewNames }) b->setColour (juce::TextButton::buttonOnColourId, Palette::mint);
        viewRoll.onClick = [this] { showRoll = true; resized(); repaint(); };
        viewNames.onClick = [this] { showRoll = false; roll.editNotes = false; resized(); repaint(); };
        buildChords.setColour (juce::TextButton::buttonColourId, Palette::mint.withAlpha (0.2f));
        buildChords.setTooltip (fr ("Pose tes notes (une mélodie, ou quelques notes longues = les fondamentales) puis clique : tes notes restent, le plugin ajoute les accords en dessous (et la basse si elle est activée)"));
        buildChords.onClick = [this] { proc.buildChordsFromNotes (ideaVariant); selectedChord = -1; resized(); selectionChanged(); };
        otherIdea.setTooltip (fr ("Propose d'autres accords autour des mêmes notes"));
        otherIdea.onClick = [this] { proc.undoAny(); proc.buildChordsFromNotes (++ideaVariant); selectedChord = -1; resized(); selectionChanged(); };

        // clavier d'ordinateur & enregistrement
        for (auto* b : { &kbOff, &kbNotes, &kbChords }) { b->setClickingTogglesState (false); b->setColour (juce::TextButton::buttonOnColourId, Palette::lavender); }
        kbOff.onClick    = [this] { proc.kbMode = 0; updateKb(); };
        kbNotes.onClick  = [this] { proc.kbMode = 1; updateKb(); };
        kbChords.onClick = [this] { proc.kbMode = 2; updateKb(); };
        kbLayout.addItemList ({ "AZERTY", "QWERTY" }, 1);
        kbLayout.setSelectedItemIndex (proc.kbLayout, juce::dontSendNotification);
        kbLayout.onChange = [this] { proc.kbLayout = kbLayout.getSelectedItemIndex(); repaint(); };
        kbOctDown.onClick = [this] { proc.kbOctave = juce::jmax (2, proc.kbOctave - 1); repaint(); };
        kbOctUp.onClick   = [this] { proc.kbOctave = juce::jmin (8, proc.kbOctave + 1); repaint(); };
        recButton.setColour (juce::TextButton::buttonOnColourId, Palette::pink);
        recButton.setTooltip (fr ("Enregistre ce que tu joues (clavier d'ordinateur, clavier MIDI, piano roll de FL) puis le met dans le piano roll"));
        recButton.onClick = [this]
        {
            const bool wasArmed = proc.isRecordArmed();
            proc.armRecording (! wasArmed);
            if (wasArmed) { roll.editNotes = true; showRoll = true; resized(); selectionChanged(); }
        };
        recQuant.addItemList ({ fr ("Sans quantification"), fr ("Quantifier 1/16"), fr ("Quantifier 1/8") }, 1);
        recQuant.setSelectedItemIndex (proc.recQuantize, juce::dontSendNotification);
        recQuant.onChange = [this] { proc.recQuantize = recQuant.getSelectedItemIndex(); };
        noOverlap.setColour (juce::ToggleButton::tickColourId, Palette::mint);
        noOverlap.setToggleState (proc.genSettings.noOverlap, juce::dontSendNotification);
        noOverlap.setTooltip (fr ("Dans le MIDI généré, chaque accord s'arrête quand le suivant commence"));
        noOverlap.onClick = [this] { proc.genSettings.noOverlap = noOverlap.getToggleState(); proc.renderGuide(); };
        updateKb();

        editNotesBtn.setColour (juce::TextButton::buttonOnColourId, Palette::lavender);
        editNotesBtn.setTooltip (fr ("Modifier les notes une par une dans le piano roll"));
        emptyHint.setText (fr ("Grille vide : clique sur « Générer une progression », dessine des notes au crayon, ou joue et enregistre."), juce::dontSendNotification);
        emptyHint.setJustificationType (juce::Justification::centred);
        emptyHint.setFont (uiFont (14.0f));
        emptyHint.setColour (juce::Label::textColourId, Palette::dim);
        emptyHint.setInterceptsMouseClicks (false, false);
        addChildComponent (emptyHint);
        addAndMakeVisible (kbKey); addAndMakeVisible (kbScale);
        emptyHint.setVisible (proc.prog.chords.empty() && proc.notes.empty());
        // --- outils du piano roll façon FL Studio
        {
            static const char* names[] = { "Crayon", "Pinceau", "Gomme", "Couper", "Sélection", "Slide", "Accord" };
            static const char* tips[] = { "Clic = nouvelle note (à la dernière longueur utilisée), glisser = déplacer, bord droit = durée",
                                          "Glisse pour peindre des notes à la suite, au pas de l'aimant",
                                          "Clique ou glisse sur les notes pour les effacer (clic droit = gomme avec n'importe quel outil)",
                                          "Clique dans une note pour la couper en deux",
                                          "Trace un rectangle pour sélectionner (Maj = ajouter), puis glisse pour tout déplacer",
                                          "Slide (comme FL) : clique une note pour en faire une note slide (en triangle). Elle ne se joue pas : les notes de sa couleur qui jouent déjà glissent jusqu'à elle pendant sa durée. Reclique pour annuler.",
                                          "Tampon (comme le Stamp de FL) : clique pour poser un accord de la gamme, la note cliquée est la fondamentale" };
            for (int i = 0; i < 7; ++i)
            {
                auto* b = toolBtns.add (new RollToolButton (fr (names[i]), i));
                b->setClickingTogglesState (true);
                b->setRadioGroupId (7300);
                b->setColour (juce::TextButton::buttonOnColourId, Palette::peach);
                b->setTooltip (fr (tips[i]));
                b->onClick = [this, i] { roll.tool = i; };
                addChildComponent (b);
            }
            toolBtns[0]->setToggleState (true, juce::dontSendNotification);
            snapBox.addItemList ({ fr ("Pas d'aimant"), "1/32", "1/16", "1/8", fr ("1/4 (temps)"), "1/2", fr ("Mesure") }, 1);
            snapBox.setSelectedItemIndex (2, juce::dontSendNotification);
            snapBox.setTooltip (fr ("Aimant (comme dans FL) : où se posent les notes. Alt = sans aimant le temps d'un geste"));
            snapBox.onChange = [this] { static const double v[] = { 0.0, 0.125, 0.25, 0.5, 1.0, 2.0, 4.0 }; roll.snap = v[juce::jlimit (0, 6, snapBox.getSelectedItemIndex())]; roll.repaint(); };
            slideBtn.setClickingTogglesState (true);
            slideBtn.setColour (juce::TextButton::buttonOnColourId, Palette::blue);
            slideBtn.setTooltip (fr ("Notes slide (comme FL) : allumé, les notes que tu dessines sont des notes slide (en triangle). Une note slide ne se joue pas : elle fait glisser jusqu'à sa hauteur les notes de la même couleur qui jouent déjà, pendant sa durée."));
            slideBtn.onClick = [this] { roll.slideMode = slideBtn.getToggleState(); };
            hlKeyBox.addItemList (theory::keyNames(), 1);
            hlScaleBox.addItem (fr ("Pas de gamme"), 1);
            hlScaleBox.addItemList (theory::scaleNames(), 2);
            hlKeyBox.setSelectedItemIndex (proc.getKey(), juce::dontSendNotification);
            hlScaleBox.setSelectedItemIndex (proc.getScale() + 1, juce::dontSendNotification);
            roll.hlKey = proc.getKey(); roll.hlScale = proc.getScale();
            hlKeyBox.setTooltip (fr ("Tonique de la gamme : surlignée dans le fond, et les accords créés la suivent"));
            hlScaleBox.setTooltip (fr ("Gamme du piano roll (comme « Scale highlighting » de FL) : tous les accords créés restent dans cette gamme.\nPour du hors gamme, monte le potard « Hors gamme »."));
            hlKeyBox.onChange = [this]
            {
                roll.hlKey = hlKeyBox.getSelectedItemIndex(); roll.repaint();
                if (roll.hlScale >= 0) proc.setParam ("key", (float) roll.hlKey);   // les accords créés suivent cette tonique
            };
            hlScaleBox.onChange = [this]
            {
                roll.hlScale = hlScaleBox.getSelectedItemIndex() - 1; roll.repaint();
                if (roll.hlScale >= 0)
                {
                    // choisir une gamme ici = tous les accords créés y restent (le potard « Hors gamme » redonne de la liberté)
                    proc.setParam ("key", (float) roll.hlKey);
                    proc.setParam ("scale", (float) roll.hlScale);
                    proc.setParam ("genSpice", 0.0f);
                }
            };
            roll.onToolChanged = [this] (int t) { if (juce::isPositiveAndBelow (t, toolBtns.size())) toolBtns[t]->setToggleState (true, juce::dontSendNotification); };
            for (juce::Component* c : { (juce::Component*) &slideBtn, (juce::Component*) &hlKeyBox, (juce::Component*) &hlScaleBox }) addChildComponent (c);
            quantBtn.setTooltip (fr ("Cale les notes sélectionnées (ou toutes) sur l'aimant (Ctrl+Q)"));
            quantBtn.onClick = [this] { roll.quantize(); selectionChanged(); };
            legatoBtn.setTooltip (fr ("Prolonge chaque note jusqu'à la suivante (Ctrl+L)"));
            legatoBtn.onClick = [this] { roll.legato(); selectionChanged(); };
            importBtn.setTooltip (fr ("Charge un fichier .mid dans le piano roll (tu peux aussi le glisser dessus)"));
            importBtn.onClick = [this]
            {
                midiChooser = std::make_unique<juce::FileChooser> (fr ("Choisir un fichier MIDI"), juce::File(), "*.mid;*.midi");
                juce::Component::SafePointer<ChordMidiContent> self (this);
                midiChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                    [safe = self] (const juce::FileChooser& fc)
                    {
                        if (safe != nullptr && fc.getResult().existsAsFile() && safe->proc.importMidiFile (fc.getResult()))
                        { safe->roll.editNotes = true; safe->showRoll = true; safe->resized(); safe->selectionChanged(); }
                    });
            };
            for (juce::Component* c : std::initializer_list<juce::Component*> { &snapBox, &quantBtn, &legatoBtn, &importBtn }) addChildComponent (c);
            zoomIn.setTooltip (fr ("Zoom avant dans le temps (Ctrl + molette)"));
            zoomOut.setTooltip (fr ("Zoom arrière (Ctrl + molette)"));
            zoomFitBtn.setTooltip (fr ("Tout voir : toute la progression et toutes les notes"));
            zoomIn.onClick = [this] { roll.zoomH (0.7); };
            zoomOut.onClick = [this] { roll.zoomH (1.0 / 0.7); };
            zoomFitBtn.onClick = [this] { roll.zoomFit(); };
            for (auto* z : { &zoomIn, &zoomOut, &zoomFitBtn }) addChildComponent (z);
        }
        editNotesBtn.onClick = [this] { roll.editNotes = ! roll.editNotes; showRoll = true; roll.selectedNote = -1; resized(); repaint(); };
        resetNotes.setTooltip (fr ("Annule les modifications de notes et revient aux accords"));
        resetNotes.onClick = [this] { proc.beginEdit(); proc.notesEdited = false; proc.renderGuide(); roll.selectedNote = -1; repaint(); };
        lane.onSelectionChanged = [this] { selectionChanged(); };
        roll.onSelectionChanged = [this] { selectionChanged(); };

        for (int pc = 0; pc < 12; ++pc)
        {
            auto* b = rootButtons.add (new juce::TextButton (theory::pcNames[pc]));
            b->setColour (juce::TextButton::buttonOnColourId, Palette::mint);
            b->onClick = [this, pc] { if (selectedChord >= 0) { proc.setGuideChordRoot (selectedChord, pc); selectionChanged(); } };
            addAndMakeVisible (b);
        }
        static const char* quick[] = { "Majeur", "Mineur", "Diminué", "Augmenté", "sus2", "sus4", "7", "Maj7", "m7", "m7b5", "9", "m9" };
        for (auto* q : quick)
        {
            const auto name = fr (q);
            auto* b = qualityChips.add (new juce::TextButton (name == fr ("Diminué") ? "dim" : (name == fr ("Augmenté") ? "aug" : (name == "Majeur" ? "Maj" : (name == "Mineur" ? "min" : name)))));
            b->setColour (juce::TextButton::buttonOnColourId, Palette::peach);
            b->setTooltip (name);
            b->onClick = [this, name] { applyQuality (theory::chordTypeNames().indexOf (name)); };
            addAndMakeVisible (b);
            chipNames.add (name);
        }
        quality.box.onChange = [this] { applyQuality (quality.box.getSelectedItemIndex()); };
        inv.box.onChange = [this] { if (selectedChord >= 0) proc.setGuideChordInversion (selectedChord, inv.box.getSelectedItemIndex() - 1); };
        octDown.onClick = [this] { shiftOctave (-1); };
        octUp.onClick   = [this] { shiftOctave (1); };
        change.setTooltip (fr ("Remplace seulement cet accord par un autre (le reste ne bouge pas)"));
        change.onClick = [this] { if (selectedChord >= 0) { proc.regenerateGuideChord (selectedChord); selectionChanged(); } };
        duplicate.onClick = [this] { if (selectedChord >= 0) { const int k = proc.duplicateGuideChord (selectedChord); if (k >= 0) selectedChord = k; selectionChanged(); } };
        del.onClick = [this] { if (selectedChord >= 0) { proc.deleteGuideChord (selectedChord); selectedChord = -1; selectionChanged(); } };
        addChord.setTooltip (fr ("Ajoute un accord au premier espace libre (ou double-clique dans une zone vide)"));
        addChord.onClick = [this] { selectedChord = proc.addGuideChord (firstGap()); selectionChanged(); };

        addAndMakeVisible (hostPlay);
        hostPlay.setColour (juce::ToggleButton::tickColourId, Palette::mint);
        hostPlay.setToggleState (proc.hostPlayGuide.load(), juce::dontSendNotification);
        hostPlay.setTooltip (fr ("Allumé : quand tu fais Play dans FL, le plugin joue SA progression (accords, basse, arpège de cette page), calée sur le tempo, et ignore les notes du piano roll de FL.\nÉteint : le plugin joue les notes envoyées par FL."));
        hostPlay.onClick = [this] { proc.hostPlayGuide.store (hostPlay.getToggleState()); };
        exportAll.onClick = [this]
        {
            int ok = 0;
            for (int l = 0; l < 4; ++l)
            {
                const bool enabled = l == 3 || (l == 0 && proc.genSettings.chords) || (l == 1 && proc.genSettings.bass) || (l == 2 && proc.genSettings.arp);
                if (enabled && proc.exportGuide (l).existsAsFile()) ++ok;
            }
            status.setText (juce::String (ok) + fr (" fichier(s) .mid exporté(s) dans : ") + proc.getExportFolder().getFullPathName(), juce::dontSendNotification);
        };
        openFolder.onClick = [this] { proc.getExportFolder().startAsProcess(); };
        status.setFont (uiFont (12.0f));
        status.setColour (juce::Label::textColourId, Palette::dim);
        status.setText (fr ("Dossier d'export : ") + proc.getExportFolder().getFullPathName(), juce::dontSendNotification);

        selectionChanged();
    }

    void tick()
    {
        const auto cs = proc.getChordSettings();
        if (cs.key != lastKey || cs.scaleIdx != lastScale || cs.size != lastSize || cs.voicing != lastVoicing)
        {
            lastKey = cs.key; lastScale = cs.scaleIdx; lastSize = cs.size; lastVoicing = cs.voicing;
            refreshGuide();
        }
        type.setAlpha (cs.mode == 0 ? 1.0f : 0.4f);
        size.setAlpha (cs.mode == 0 ? 0.4f : 1.0f);

        // la progression suit la tonalité (transposée, les modifications sont gardées)
        if (proc.getKey() != proc.genSettings.key) { proc.transposeGuide (proc.getKey()); selectionChanged(); }
        if (proc.getKey() != roll.hlKey && roll.hlScale >= 0) { roll.hlKey = proc.getKey(); hlKeyBox.setSelectedItemIndex (roll.hlKey, juce::dontSendNotification); roll.repaint(); }
        if (proc.getScale() != proc.genSettings.scaleIdx)
        {
            proc.genSettings.scaleIdx = proc.getScale();
            for (auto& c : proc.prog.chords) { const auto rom = c.roman; gen::relabel (c, proc.genSettings.key, proc.genSettings.scaleIdx); c.roman = rom; }
            proc.renderGuide();
            proc.conformGuideToScale();   // tous les accords dans la nouvelle gamme
            if (roll.hlScale >= 0 && roll.hlScale != proc.getScale()) { roll.hlScale = proc.getScale(); hlScaleBox.setSelectedItemIndex (roll.hlScale + 1, juce::dontSendNotification); roll.repaint(); }
        }
        const int gr = (int) proc.getParam ("genRegister");
        if (gr != proc.genSettings.reg)
        {
            proc.genSettings.reg = gr;
            if (gr != gen::RegAuto) proc.prog.reg = gr;
            proc.renderGuide();
        }
        proc.genSettings.spice = proc.getParam ("genSpice");
        proc.genSettings.variety = proc.getParam ("genVariety");

        const int v = proc.chordEngine.lastVersion.load();
        if (v != lastLiveVersion)
        {
            lastLiveVersion = v;
            std::vector<int> notes;
            for (int i = 0; i < proc.chordEngine.lastCount.load(); ++i) notes.push_back (proc.chordEngine.lastNotes[(size_t) i].load());
            const bool flats = theory::prefersFlats (cs.key, cs.scaleIdx);
            chordText = theory::chordName (notes, flats);
            notesText.clear();
            for (int x : notes) notesText << theory::noteName (x, flats) << "  ";
            repaint (displayCard);
        }

        listen.setButtonText (proc.isPreviewing() ? fr ("Stop") : fr ("Écouter"));
        listen.setToggleState (proc.isPreviewing(), juce::dontSendNotification);
        listen.setColour (juce::TextButton::buttonOnColourId, Palette::mint);
        recButton.setToggleState (proc.isRecordArmed(), juce::dontSendNotification);
        recButton.setButtonText (proc.isRecordArmed() ? fr ("■  Arrêter") : fr ("●  Enregistrer"));
        if (proc.isRecordArmed() || recWasArmed) { recWasArmed = proc.isRecordArmed(); repaint (kbCard); }

        if (proc.guideVersion.load() != lastGuideVersion || proc.isPreviewing())
        {
            lastGuideVersion = proc.guideVersion.load();
            resetNotes.setVisible (proc.notesEdited);
            lane.repaint();
            emptyHint.setVisible (proc.prog.chords.empty() && proc.notes.empty());
            roll.repaint();
            repaint (editPanel);
        }
    }

    void paint (juce::Graphics& g) override
    {
        drawSection (g, 1, liveCard, fr ("JOUER EN DIRECT & ENREGISTRER  ·  accords auto, clavier d'ordinateur, rec"), fr ("joue des accords, au clavier, et enregistre"), Palette::pink);
        if (sectionOpen[1])
        {
            drawCard (g, displayCard, fr ("ACCORD JOUÉ"), Palette::pink);
            drawCard (g, feelCard, fr ("JEU"), Palette::pink);
        }
        drawSection (g, 0, guideCard, fr ("GUIDE DES ACCORDS  ·  maintiens pour écouter"), fr ("tous les accords de la gamme à écouter"), Palette::pink);
        if (sectionOpen[1]) drawCard (g, kbCard, fr ("CLAVIER D'ORDINATEUR & ENREGISTREMENT"), Palette::lavender);
        if (sectionOpen[1])
        {
            g.setColour (Palette::text);
            g.setFont (uiFont (14.0f, juce::Font::bold));
            g.drawText ("C" + juce::String (proc.kbOctave), kbOctLabel, juce::Justification::centred);
            g.setColour (Palette::dim);
            g.setFont (uiFont (12.5f));
            const bool az = proc.kbLayout == 0;
            juce::String help = proc.kbMode == 0 ? fr ("Choisis Notes ou Accords, clique dans le plugin, puis joue avec les lettres du clavier.")
                              : (az ? fr ("Notes comme un piano : A Z E R T Y U I O P = touches blanches (A = C5), Q S F G H K L = touches noires, W X C V B N = blanches une octave plus bas · Accords : A à P = accords de la gamme, Q à J = couleurs hors gamme · flèches haut/bas = octave")
                                    : fr ("Notes comme un piano : Q W E R T Y U I O P = touches blanches (Q = C5), A S F G H K L = touches noires, Z X C V B N M = blanches une octave plus bas · Accords : Q à P = accords de la gamme, A à J = couleurs hors gamme · flèches haut/bas = octave"));
            g.drawFittedText (help, kbHelp, juce::Justification::centredLeft, 2);
            juce::String rs;
            if (proc.isRecordArmed())
                rs = proc.isRecordingStarted() ? fr ("● Enregistrement…  ") + juce::String (proc.recordedBeats() / 4.0, 1) + fr (" mesures")
                                               : fr ("Prêt : l'enregistrement démarre à la première note jouée");
            g.setColour (proc.isRecordArmed() ? Palette::pink : Palette::dim);
            g.drawText (rs, kbRecStatus, juce::Justification::centredLeft, true);
        }
        drawCard (g, genCard.getUnion (editorCard), fr ("GÉNÉRER & ÉDITER LA PROGRESSION"), Palette::mint);
        g.setColour (Palette::line);
        g.drawHorizontalLine (editorCard.getY(), (float) editorCard.getX() + 14.0f, (float) editorCard.getRight() - 14.0f);
        if (roll.isVisible() && ! toolBtns.isEmpty())
        {
            // cadre du piano roll : barre d'outils + grille d'un seul bloc (comme une fenêtre de FL)
            auto fr0 = toolBtns.getFirst()->getBounds().getUnion (roll.getBounds()).withLeft (roll.getX()).withRight (roll.getRight()).expanded (6, 6).toFloat();
            g.setColour (juce::Colours::black.withAlpha (Palette::light ? 0.06f : 0.25f));
            g.fillRoundedRectangle (fr0.translated (0.0f, 2.0f), 6.0f);
            g.setColour (Palette::panel2.darker (Palette::light ? 0.03f : 0.25f));
            g.fillRoundedRectangle (fr0, 6.0f);
            auto bar = fr0.withBottom ((float) roll.getY() - 3.0f);
            g.setGradientFill (juce::ColourGradient (Palette::panel2.brighter (Palette::light ? 0.0f : 0.06f), 0.0f, bar.getY(), Palette::panel2.darker (0.1f), 0.0f, bar.getBottom(), false));
            g.fillRoundedRectangle (bar.reduced (1.0f), 5.0f);
            g.setColour (Palette::line);
            g.drawRoundedRectangle (fr0, 6.0f, 1.0f);
            g.drawHorizontalLine ((int) bar.getBottom(), fr0.getX(), fr0.getRight());
        }

        auto d = displayCard.reduced (16, 10).withTrimmedTop (20);
        if (! sectionOpen[1]) {}
        else if (chordText.isEmpty())
        {
            g.setColour (Palette::dim);
            g.setFont (uiFont (13.0f));
            g.drawFittedText (fr ("Active les accords puis joue une note\n(piano roll ou clavier en bas)."), d, juce::Justification::centredLeft, 2);
        }
        else
        {
            g.setColour (Palette::text);
            g.setFont (uiFont (28.0f, juce::Font::bold));
            g.drawText (chordText, d.removeFromTop (40), juce::Justification::centredLeft);
            g.setColour (Palette::dim);
            g.setFont (uiFont (12.5f));
            g.drawText (notesText, d, juce::Justification::topLeft);
        }

        g.setFont (uiFont (11.0f, juce::Font::bold));
        g.setColour (Palette::dim);
        g.drawText (fr ("DANS LA GAMME"), inLabel, juce::Justification::centredLeft);
        g.drawText (fr ("HORS GAMME  ·  couleurs groovy / jazzy"), outLabel, juce::Justification::centredLeft);
        auto pr = progArea;
        if (sectionOpen[0])
            g.drawText (fr ("PROGRESSIONS CONSEILLÉES  ·  ") + moods::names()[selectedMood].toUpperCase(), pr.removeFromTop (16), juce::Justification::centredLeft);
        g.setFont (uiFont (12.5f));
        for (auto& line : progLines)
        {
            if (pr.getHeight() < 16) break;
            g.setColour (Palette::text.withAlpha (0.9f));
            g.drawText (line, pr.removeFromTop (18), juce::Justification::centredLeft, true);
        }

        // panneau de l'accord sélectionné
        auto e = editPanel;
        g.setColour (Palette::panel2.withAlpha (0.5f));
        g.fillRoundedRectangle (e.toFloat(), 10.0f);
        auto head = e.reduced (14, 8).removeFromTop (20).withTrimmedRight (455);
        if (juce::isPositiveAndBelow (selectedChord, (int) proc.prog.chords.size()))
        {
            const auto& c = proc.prog.chords[(size_t) selectedChord];
            juce::AttributedString as;
            as.append (fr ("Accord sélectionné : "), uiFont (12.0f), Palette::dim);
            as.append (c.name + "  ", uiFont (14.0f, juce::Font::bold), Palette::text);
            as.append (c.roman + (c.outside ? fr ("  ·  hors gamme") : juce::String()), uiFont (12.0f), c.outside ? Palette::peach : Palette::mint);
            as.setJustification (juce::Justification::centredLeft);
            as.draw (g, head.toFloat());
        }
        else
        {
            g.setColour (Palette::dim);
            g.setFont (uiFont (12.5f));
            g.drawFittedText (fr ("Clique une note : son accord se modifie ici"), head, juce::Justification::centredLeft, 1, 0.8f);
        }
        g.setColour (Palette::dim);
        g.setFont (uiFont (11.0f, juce::Font::bold));
        g.drawText (fr ("FONDAMENTALE"), rootLabel, juce::Justification::centredLeft);
        g.drawText (fr ("TYPE"), qualLabel, juce::Justification::centredLeft);

        g.setFont (uiFont (12.5f));
        g.drawFittedText (fr ("Glisse une pastille directement dans FL Studio (playlist, piano roll ou channel rack), ou exporte les .mid dans le dossier."),
                          exportText, juce::Justification::topLeft, 2);
        for (int b = 0; b < 3; ++b) CardMover::paint (g, blockCard (b));
    }

    void resized() override
    {
        auto r = getLocalBounds();
        const int gap = 10;
        for (int bi = 0; bi < 3; ++bi)   // cartes déplaçables avec les flèches ▲ ▼ (ordre mémorisé)
        {
            if (bi > 0) r.removeFromTop (gap);
            const int b = cmOrder()[(size_t) bi];
            if (b == 0) layoutEditorBlock (r);
            else if (b == 1) layoutLiveBlock (r);
            else layoutGuideBlock (r);
        }
    }

    void layoutEditorBlock (juce::Rectangle<int>& r)
    {
        const int gap = 10;
        // générateur et piano roll dans le même bloc : on génère puis on retouche juste en dessous
        genCard = r.removeFromTop (160);
        layoutGenerator();
        editorCard = r.removeFromTop (610);
        auto ed = editorCard.reduced (14, 6).withTrimmedTop (6);
        {
            // export vers FL directement sous le piano roll (pastilles à glisser + dossier)
            auto ex = ed.removeFromBottom (40);
            ed.removeFromBottom (6);
            exportCard = {};
            exportText = {};
            detachExport.setBounds (ex.removeFromRight (32).withSizeKeepingCentre (32, 26));
            ex.removeFromRight (6);
            openFolder.setBounds (ex.removeFromRight (120).withSizeKeepingCentre (120, 30));
            ex.removeFromRight (6);
            exportAll.setBounds (ex.removeFromRight (150).withSizeKeepingCentre (150, 30));
            ex.removeFromRight (10);
            const int cw = (ex.getWidth() - 3 * 6) / 4;
            for (auto* c : { &chipChords, &chipBass, &chipArp, &chipAll }) { c->setBounds (ex.removeFromLeft (cw).withSizeKeepingCentre (cw, 34)); ex.removeFromLeft (6); }
            status.setBounds ({});
        }
        // barre d'outils façon FL Studio, au-dessus du piano roll (le piano et l'édition ne font qu'un)
        for (juce::Component* c : std::initializer_list<juce::Component*> { &viewRoll, &viewNames, &editNotesBtn, &addChord, &lane }) c->setVisible (false);
        showRoll = true;
        lane.big = false;
        {
            auto tb = ed.removeFromTop (32);
            for (auto* b : toolBtns) { b->setVisible (true); b->setBounds (tb.removeFromLeft (78)); tb.removeFromLeft (3); }
            tb.removeFromLeft (8);
            snapBox.setVisible (true);
            snapBox.setBounds (tb.removeFromLeft (92).reduced (0, 2));
            tb.removeFromLeft (8);
            for (auto* z : { &zoomOut, &zoomIn }) { z->setBounds (tb.removeFromLeft (30)); tb.removeFromLeft (3); }
            zoomFitBtn.setBounds (tb.removeFromLeft (58));
            ed.removeFromTop (4);
            auto tb2 = ed.removeFromTop (28);
            hlKeyBox.setBounds (tb2.removeFromLeft (64)); tb2.removeFromLeft (4);
            hlScaleBox.setBounds (tb2.removeFromLeft (170)); tb2.removeFromLeft (8);
            slideBtn.setBounds (tb2.removeFromLeft (88)); tb2.removeFromLeft (4);
            quantBtn.setBounds (tb2.removeFromLeft (72)); tb2.removeFromLeft (4);
            legatoBtn.setBounds (tb2.removeFromLeft (64)); tb2.removeFromLeft (4);
            importBtn.setBounds (tb2.removeFromLeft (90));
            for (juce::Component* c : std::initializer_list<juce::Component*> { &snapBox, &quantBtn, &legatoBtn, &importBtn, &slideBtn, &hlKeyBox, &hlScaleBox, &zoomOut, &zoomIn, &zoomFitBtn })
                c->setVisible (true);
            resetNotes.setVisible (proc.notesEdited);
            ed.removeFromTop (6);
            roll.setVisible (true);
            roll.setBounds (ed.removeFromTop (288));
        }
        emptyHint.setBounds ((roll.isVisible() ? roll.getBounds() : lane.getBounds()).withSizeKeepingCentre (560, 60));
        emptyHint.toFront (false);
        ed.removeFromTop (10);
        editPanel = ed;
        auto ep = editPanel.reduced (14, 8);
        {
            // « Accords autour de mes notes » : en haut à droite du panneau d'accord
            auto hb = ep.withHeight (26).translated (0, -3);
            resetNotes.setBounds (hb.removeFromRight (150)); hb.removeFromRight (4);
            otherIdea.setBounds (hb.removeFromRight (90)); hb.removeFromRight (4);
            buildChords.setBounds (hb.removeFromRight (200));
        }
        ep.removeFromTop (26);
        auto rr = ep.removeFromTop (28);
        rootLabel = rr.removeFromLeft (100);
        for (auto* b : rootButtons) b->setBounds (rr.removeFromLeft (40).reduced (2, 0));
        ep.removeFromTop (8);
        auto qr = ep.removeFromTop (28);
        qualLabel = qr.removeFromLeft (100);
        for (auto* b : qualityChips) b->setBounds (qr.removeFromLeft (46).reduced (2, 0));
        ep.removeFromTop (6);
        auto br = ep.removeFromTop (48);
        quality.setBounds (br.removeFromLeft (180));
        br.removeFromLeft (gap);
        inv.setBounds (br.removeFromLeft (170));
        br.removeFromLeft (gap);
        auto btns = br.withTrimmedTop (18);
        octDown.setBounds (btns.removeFromLeft (80));  btns.removeFromLeft (4);
        octUp.setBounds (btns.removeFromLeft (80));    btns.removeFromLeft (gap);
        del.setBounds (btns.removeFromRight (90));     btns.removeFromRight (4);
        duplicate.setBounds (btns.removeFromRight (90)); btns.removeFromRight (4);
        change.setBounds (btns);


    }

    // --- sections repliables (moins utilisées)
    void layoutLiveBlock (juce::Rectangle<int>& r)
    {
        const int gap = 10;
        const juce::Rectangle<int> none;
        if (sectionOpen[1])
        {
            liveCard = r.removeFromTop (150);
            auto m = liveCard.reduced (14, 10).withTrimmedTop (22);
            auto row1 = m.removeFromTop (46);
            chordOn.setBounds (row1.removeFromLeft (190).withTrimmedTop (16));
            row1.removeFromLeft (gap);
            mode.setBounds (row1.removeFromLeft (290).withTrimmedTop (16));
            row1.removeFromLeft (gap * 2);
            style.setBounds (row1.withTrimmedRight (40));
            m.removeFromTop (8);
            auto row2 = m.removeFromTop (48);
            key.setBounds (row2.removeFromLeft (70));      row2.removeFromLeft (gap);
            scale.setBounds (row2.removeFromLeft (200));   row2.removeFromLeft (gap);
            type.setBounds (row2.removeFromLeft (135));    row2.removeFromLeft (gap);
            size.setBounds (row2.removeFromLeft (90));     row2.removeFromLeft (gap);
            voicing.setBounds (row2.removeFromLeft (120)); row2.removeFromLeft (gap);
            reg.setBounds (row2);

            r.removeFromTop (gap);
            auto mid = r.removeFromTop (110);
            displayCard = mid.removeFromLeft (290);
            mid.removeFromLeft (12);
            feelCard = mid;
            auto f = feelCard.reduced (8, 6).withTrimmedTop (16);
            const int w = f.getWidth() / 4;
            for (auto* k : { &inversion, &strum, &humanize, &spice }) k->setBounds (f.removeFromLeft (w));
        }
        else
        {
            liveCard = r.removeFromTop (collapsedH);
            displayCard = feelCard = none;
            for (juce::Component* c : std::initializer_list<juce::Component*> { &chordOn, &mode, &style, &key, &scale, &type, &size, &voicing, &reg,
                                                                               &inversion, &strum, &humanize, &spice })
                c->setBounds (none);
        }
        sectionHeader[1] = sectionOpen[1] ? liveCard.withHeight (32) : liveCard;

        if (sectionOpen[1])
        {
            r.removeFromTop (gap);
            kbCard = r.removeFromTop (214);
            auto k = kbCard.reduced (14, 10).withTrimmedTop (24);
            {
                auto kr = k.removeFromTop (48);
                kbKey.setBounds (kr.removeFromLeft (90)); kr.removeFromLeft (10);
                kbScale.setBounds (kr.removeFromLeft (230));
                k.removeFromTop (6);
            }
            auto k1 = k.removeFromTop (32);
            kbOff.setBounds (k1.removeFromLeft (110)); k1.removeFromLeft (4);
            kbNotes.setBounds (k1.removeFromLeft (100)); k1.removeFromLeft (4);
            kbChords.setBounds (k1.removeFromLeft (100)); k1.removeFromLeft (12);
            kbLayout.setBounds (k1.removeFromLeft (110)); k1.removeFromLeft (12);
            kbOctDown.setBounds (k1.removeFromLeft (32));
            kbOctLabel = k1.removeFromLeft (50);
            kbOctUp.setBounds (k1.removeFromLeft (32));
            cutToggle.setBounds (k1.removeFromRight (260));
            k.removeFromTop (8);
            auto k2 = k.removeFromTop (32);
            recButton.setBounds (k2.removeFromLeft (150)); k2.removeFromLeft (8);
            recQuant.setBounds (k2.removeFromLeft (170)); k2.removeFromLeft (12);
            noOverlap.setBounds (k2.removeFromRight (260));
            kbRecStatus = k2;
            k.removeFromTop (6);
            kbHelp = k;
        }
        else
        {
            kbCard = none;
            for (juce::Component* c : std::initializer_list<juce::Component*> { &kbOff, &kbNotes, &kbChords, &kbLayout, &kbOctDown, &kbOctUp,
                                                                               &cutToggle, &recButton, &recQuant, &noOverlap, &kbKey, &kbScale })
                c->setBounds (none);
            kbOctLabel = kbRecStatus = kbHelp = none;
        }
        sectionHeader[2] = {};
    }

    void layoutGuideBlock (juce::Rectangle<int>& r)
    {
        const juce::Rectangle<int> none;
        if (sectionOpen[0])
        {
            guideCard = r.removeFromTop (250);
            auto gA = guideCard.reduced (14, 8).withTrimmedTop (26);
            inLabel = gA.removeFromTop (16);
            layoutPads (pads, gA.removeFromTop (52), numPads);
            gA.removeFromTop (6);
            outLabel = gA.removeFromTop (16);
            layoutPads (outPads, gA.removeFromTop (52), 8);
            gA.removeFromTop (8);
            progArea = gA;
        }
        else
        {
            guideCard = r.removeFromTop (collapsedH);
            layoutPads (pads, none, numPads);
            layoutPads (outPads, none, 8);
            inLabel = outLabel = progArea = none;
        }
        sectionHeader[0] = sectionOpen[0] ? guideCard.withHeight (32) : guideCard;


    }

    void layoutGenerator()
    {
        const int gap = 10;
        detachGen.setBounds (genCard.getRight() - 46 - 60, genCard.getY() + 8, 32, 26);
        {
            auto tr = juce::Rectangle<int> (genCard.getRight() - 46 - 60 - 8 - 370, genCard.getY() + 8, 370, 26);
            tasteBtn.setBounds (tr.removeFromRight (140)); tr.removeFromRight (6);
            dislikeBtn.setBounds (tr.removeFromRight (120)); tr.removeFromRight (6);
            likeBtn.setBounds (tr.removeFromRight (98));
        }
        auto gm = genCard.reduced (14, 10).withTrimmedTop (22);
        auto gr1 = gm.removeFromTop (48);
        mood.setBounds (gr1.removeFromLeft (190));   gr1.removeFromLeft (gap);
        bars.setBounds (gr1.removeFromLeft (120));   gr1.removeFromLeft (gap);
        rich.setBounds (gr1.removeFromLeft (130));   gr1.removeFromLeft (gap);
        genReg.setBounds (gr1.removeFromLeft (120));
        surprise.setBounds (gr1.removeFromRight (170).withTrimmedTop (14));
        gr1.removeFromLeft (gap);
        hostPlay.setBounds (gr1.withTrimmedTop (14).withSizeKeepingCentre (juce::jmin (gr1.getWidth(), 150), 30));
        gm.removeFromTop (4);
        auto gr2 = gm;
        auto left = gr2.removeFromLeft (330);
        layChords.setBounds (left.removeFromLeft (115).withSizeKeepingCentre (115, 30));
        layBass.setBounds (left.removeFromLeft (105).withSizeKeepingCentre (105, 30));
        layArp.setBounds (left.withSizeKeepingCentre (115, 30));
        genSpice.setBounds (gr2.removeFromLeft (100));
        genVariety.setBounds (gr2.removeFromLeft (100));
        listen.setBounds (gr2.removeFromRight (100).withSizeKeepingCentre (100, 34));
        gr2.removeFromRight (gap);
        generate.setBounds (gr2.removeFromRight (180).withSizeKeepingCentre (180, 34));
    }

    static std::vector<int>& cmOrder() { return CardMover::order ("chordOrder", 3); }
    juce::Rectangle<int> blockCard (int b) const { return b == 0 ? genCard.getUnion (editorCard) : (b == 1 ? liveCard : guideCard); }
    // sections repliables
    static constexpr int collapsedH = 46;
    inline static std::array<bool, 3> sectionOpen { false, true, false };
    std::function<void()> onLayoutChanged;
    int layoutHeight() const
    {
        return 160 + 610
             + 10 + (sectionOpen[1] ? 150 + 10 + 110 + 10 + 214 : collapsedH)
             + 10 + (sectionOpen[0] ? 250 : collapsedH) + 16;
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        for (int b = 0; b < 3; ++b)
            if (const int d = CardMover::hit (blockCard (b), e.getPosition()))
            {
                CardMover::move ("chordOrder", 3, b, d);
                resized(); repaint();
                return;
            }
        for (int i = 0; i < 2; ++i)
            if (sectionHeader[(size_t) i].contains (e.getPosition()))
            {
                sectionOpen[(size_t) i] = ! sectionOpen[(size_t) i];
                if (onLayoutChanged) onLayoutChanged();
                resized();
                repaint();
                return;
            }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        bool overHeader = false;
        for (auto& h : sectionHeader) overHeader |= h.contains (e.getPosition());
        setMouseCursor (overHeader ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }

private:
    std::array<juce::Rectangle<int>, 3> sectionHeader;

    void drawSection (juce::Graphics& g, int i, juce::Rectangle<int> card, const juce::String& title, const juce::String& hint, juce::Colour col)
    {
        drawCard (g, card, title, col);
        auto h = sectionHeader[(size_t) i].reduced (16, 0).withTrimmedRight (60);
        const bool open = sectionOpen[(size_t) i];
        juce::Path chev;
        const float cx = (float) h.getRight() - 8.0f, cy = (float) card.getY() + 21.0f;
        if (open) { chev.startNewSubPath (cx - 5.0f, cy - 2.5f); chev.lineTo (cx, cy + 2.5f); chev.lineTo (cx + 5.0f, cy - 2.5f); }
        else      { chev.startNewSubPath (cx - 2.5f, cy - 5.0f); chev.lineTo (cx + 2.5f, cy); chev.lineTo (cx - 2.5f, cy + 5.0f); }
        g.setColour (col);
        g.strokePath (chev, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (Palette::dim);
        g.setFont (uiFont (12.0f));
        g.drawText (open ? fr ("Fermer") : hint + juce::String::fromUTF8 ("  ·  ") + fr ("Ouvrir"),
                    juce::Rectangle<int> (h.getX(), card.getY() + 12, h.getWidth() - 20, 20), juce::Justification::centredRight, true);
    }

    static void layoutPads (juce::OwnedArray<ChordPad>& arr, juce::Rectangle<int> row, int count)
    {
        const int n = juce::jmax (1, count);
        const int pw = juce::jmin (100, (row.getWidth() - (n - 1) * 8) / n);
        for (int i = 0; i < arr.size(); ++i)
        {
            arr[i]->setVisible (i < count);
            if (i < count) arr[i]->setBounds (row.getX() + i * (pw + 8), row.getY(), pw, row.getHeight());
        }
    }

    double firstGap() const
    {
        for (double b = 0; b < proc.prog.lengthBeats; b += 1.0)
        {
            bool used = false;
            for (auto& c : proc.prog.chords) if (b >= c.start && b < c.start + c.length) used = true;
            if (! used) return b;
        }
        return juce::jmax (0.0, proc.prog.lengthBeats - 2.0);
    }

    void applyQuality (int typeIdx)
    {
        if (selectedChord < 0 || ! juce::isPositiveAndBelow (typeIdx, (int) theory::chordTypes().size())) return;
        proc.setGuideChordIntervals (selectedChord, theory::chordTypes()[(size_t) typeIdx].iv);
        selectionChanged();
    }

public:
    void selectionChanged()
    {
        const bool has = juce::isPositiveAndBelow (selectedChord, (int) proc.prog.chords.size());
        for (auto* c : std::initializer_list<juce::Component*> { &quality, &inv, &change, &duplicate, &del })
            c->setEnabled (has);
        for (auto* b : rootButtons) b->setEnabled (has);
        for (auto* b : qualityChips) b->setEnabled (has);

        if (has)
        {
            const auto& c = proc.prog.chords[(size_t) selectedChord];
            std::vector<int> sorted = c.close;
            std::sort (sorted.begin(), sorted.end());
            const int root = sorted.front();
            const auto& sc = theory::scaleAt (proc.genSettings.scaleIdx);
            for (int pc = 0; pc < 12; ++pc)
            {
                rootButtons[pc]->setToggleState (theory::mod12 (root) == pc, juce::dontSendNotification);
                rootButtons[pc]->setAlpha (theory::inScale (pc, proc.genSettings.key, sc) ? 1.0f : 0.5f);
            }
            int mask = 0;
            for (int x : sorted) mask |= 1 << theory::mod12 (x - root);
            int found = -1;
            for (int t = 0; t < (int) theory::chordTypes().size(); ++t)
                if (theory::maskOf (theory::chordTypes()[(size_t) t].iv) == mask) { found = t; break; }
            quality.box.setSelectedItemIndex (found, juce::dontSendNotification);
            if (found < 0) quality.box.setText (fr ("Personnalisé"), juce::dontSendNotification);
            for (int i = 0; i < qualityChips.size(); ++i)
                qualityChips[i]->setToggleState (found >= 0 && theory::chordTypeNames()[found] == chipNames[i], juce::dontSendNotification);
            inv.box.setSelectedItemIndex (c.inversion + 1, juce::dontSendNotification);
        }
        lane.repaint();
        roll.repaint();
        repaint (editPanel);
    }

    void refreshGuide()
    {
        const auto cs = proc.getChordSettings();
        const bool flats = theory::prefersFlats (cs.key, cs.scaleIdx);
        numPads = juce::jmin (8, theory::degreeCount (cs.scaleIdx));
        for (int i = 0; i < numPads; ++i)
        {
            auto close = theory::degreeChord (i, cs.key, cs.scaleIdx, cs.size, 48);
            pads[i]->set (theory::roman (i, close), theory::chordName (close, flats), theory::applyVoicing (close, cs.voicing, 0));
        }
        struct Out { int offset; std::initializer_list<int> iv; const char* label; };
        const int k = 48 + cs.key;
        const Out outs[] = { { 2, { 0, 4, 7, 10, 14 }, "V7/V" }, { 4, { 0, 4, 7, 10, 13 }, "V7/vi" }, { 9, { 0, 4, 7, 10, 15 }, "V7/ii" },
                             { 1, { 0, 4, 7, 10, 14 }, "subV" }, { 5, { 0, 3, 7, 9 }, "iv m6" }, { 8, { 0, 4, 7, 11, 14 }, "bVI maj9" },
                             { 10, { 0, 4, 7, 10, 14 }, "bVII9" }, { 6, { 0, 3, 6, 10 }, "#iv ø" } };
        for (int i = 0; i < 8; ++i)
        {
            auto notes = theory::chordFrom (k + outs[i].offset, outs[i].iv);
            outPads[i]->set (fr (outs[i].label), theory::chordName (notes, flats), notes);
        }
        progLines.clear();
        const auto& mo = moods::list()[(size_t) selectedMood];
        for (auto& pg : mo.progs)
        {
            juce::String romans, names;
            for (size_t i = 0; i < pg.size(); ++i)
            {
                auto close = theory::degreeChord (pg[i], cs.key, cs.scaleIdx, juce::jlimit (3, 7, mo.ext), 48);
                if (i > 0) { romans << fr ("  –  "); names << "  "; }
                romans << theory::roman (pg[i] % 8, close);
                names << theory::chordName (close, flats);
            }
            progLines.add (romans + fr ("      →   ") + names);
        }
        resized();
        repaint();
    }

    juce::File dragFile (int layer)
    {
        auto f = proc.exportGuide (layer);
        status.setText (f.existsAsFile() ? fr ("Exporté : ") + f.getFullPathName() : fr ("Cette couche est vide : active-la dans le générateur."),
                        juce::dontSendNotification);
        return f;
    }

    VariantProcessor& proc;
    int selectedChord = -1;
    bool showRoll = true;

public:
    ParamToggle chordOn { proc.apvts, "chordOn", fr ("Activer les accords"), Palette::pink, fr ("Chaque note jouée déclenche un accord complet") };
    SegmentedChoice mode { proc.apvts, "chordMode", { "Accord fixe", "Selon la gamme" }, Palette::pink };
    LabeledCombo style   { fr ("Appliquer un style"), moods::names() };
    LabeledCombo key     { fr ("Tonalité"), theory::keyNames(), &proc.apvts, "key" };
    LabeledCombo scale   { "Gamme", theory::scaleNames(), &proc.apvts, "scale" };
    LabeledCombo type    { fr ("Type (accord fixe)"), theory::chordTypeNames(), &proc.apvts, "chordType" };
    LabeledCombo size    { "Taille (gamme)", { "Triade", "7e", "9e", "11e", "13e" }, &proc.apvts, "chordSize" };
    LabeledCombo voicing { "Voicing", theory::voicingNames(), &proc.apvts, "voicing" };
    LabeledCombo reg     { "Registre", { fr ("Comme jouée"), fr ("Enchaînement fluide"), "Grave", "Aigu" }, &proc.apvts, "chordRegister" };
    LabeledKnob inversion { proc.apvts, "inversion", fr ("Renversement"), Palette::pink, fr ("Quelle note de l'accord est en bas") };
    LabeledKnob strum     { proc.apvts, "strum", "Strum", Palette::pink, fr ("Décale chaque note de l'accord, comme une guitare") };
    LabeledKnob humanize  { proc.apvts, "humanize", fr ("Humanisation"), Palette::pink, fr ("Petites imprécisions de timing et de vélocité") };
    LabeledKnob spice     { proc.apvts, "chordSpice", fr ("Couleur jazzy"), Palette::peach, fr ("Ajoute des 9e, 13e et des altérations hors gamme (b9, #9, #11, b13)") };
    LabeledCombo mood  { "Ambiance", moods::names() };
    LabeledCombo bars  { "Longueur", { "4 mesures", "8 mesures" } };
    LabeledCombo rich  { "Richesse", { "Simple", "Riche", fr ("Très riche") } };
    LabeledCombo genReg { "Registre", gen::registerNames(), &proc.apvts, "genRegister" };
    LabeledKnob genSpice { proc.apvts, "genSpice", "Hors gamme", Palette::peach,
                           fr ("0 % = tout dans la gamme. Plus haut = dominantes secondaires, substitutions tritoniques, approches chromatiques…") };
    LabeledKnob genVariety { proc.apvts, "genVariety", fr ("Variété"), Palette::mint,
                             fr ("Plus haut = accords sus, add9, 6/9, en quartes, renversements et accords de passage : des progressions moins prévisibles") };
    juce::ToggleButton layChords { fr ("Accords") }, layBass { fr ("Basse") }, layArp { fr ("Arpège") };
    juce::TextButton generate { fr ("Générer une progression") }, listen { fr ("Écouter") }, surprise { fr ("Surprends-moi") };
    juce::ToggleButton hostPlay { fr ("Play de FL") };
    juce::TextButton detachGen { juce::String::fromUTF8 ("⧉") }, detachExport { juce::String::fromUTF8 ("⧉") };
    juce::TextButton likeBtn { fr ("J'aime") }, dislikeBtn { fr ("Pas mon style") }, tasteBtn { fr ("Mes goûts") };
    juce::OwnedArray<juce::TextButton> toolBtns;
    juce::ComboBox snapBox;
    juce::TextButton quantBtn { fr ("Quantif.") }, legatoBtn { fr ("Legato") }, importBtn { fr ("Import .mid") };
    juce::TextButton zoomIn { "+" }, zoomOut { juce::String::fromUTF8 ("−") }, zoomFitBtn { fr ("Tout") };
    juce::Label emptyHint;
    juce::TextButton slideBtn { fr ("Mode slide") };
    juce::ComboBox hlKeyBox, hlScaleBox;
    LabeledCombo kbKey   { fr ("Tonalité"), theory::keyNames(), &proc.apvts, "key" };
    LabeledCombo kbScale { fr ("Gamme jouée au clavier"), theory::scaleNames(), &proc.apvts, "scale" };
    std::unique_ptr<juce::FileChooser> midiChooser;
    void refreshTaste() { tasteBtn.setToggleState (proc.tasteOn, juce::dontSendNotification); tasteBtn.setButtonText (fr ("Mes goûts") + " (" + juce::String (proc.likedCount()) + ")"); }
    std::function<void (int)> onDetachMini;   // 0 générateur, 1 export
    juce::TextButton viewRoll { fr ("Piano roll") }, viewNames { fr ("Noms d'accords") }, addChord { fr ("+ Ajouter un accord") },
                     editNotesBtn { fr ("Éditer les notes") }, resetNotes { fr ("Revenir aux accords") },
                     buildChords { fr ("Accords autour de mes notes") }, otherIdea { fr ("Autre idée") },
                     kbOff { fr ("Désactivé") }, kbNotes { fr ("Notes") }, kbChords { fr ("Accords") },
                     kbOctDown { juce::String::fromUTF8 ("−") }, kbOctUp { "+" }, recButton { fr ("●  Enregistrer") };
    juce::ComboBox kbLayout, recQuant;
    ParamToggle cutToggle { proc.apvts, "cutMode", fr ("Cut : un accord coupe le précédent"), Palette::lavender,
                            fr ("Comme le Cut de FL Studio : quand tu joues un nouvel accord, le précédent s'arrête") };
    juce::ToggleButton noOverlap { fr ("MIDI généré sans chevauchement") };
    ChordLane lane { proc, selectedChord };
    PianoRoll roll { proc, selectedChord };
    LabeledCombo quality { "Tous les types", theory::chordTypeNames() };
    LabeledCombo inv { "Renversement", { "Auto (fluide)", "Fondamentale", "1er renversement", "2e renversement", "3e renversement" } };
    juce::TextButton octDown { fr ("Octave −") }, octUp { fr ("Octave +") }, change { fr ("Changer cet accord") }, duplicate { fr ("Dupliquer") }, del { fr ("Supprimer") };
    DragChip chipChords { "Accords .mid", Palette::mint,     [this] { return dragFile (0); } };
    DragChip chipBass   { "Basse .mid",   Palette::blue,     [this] { return dragFile (1); } };
    DragChip chipArp    { fr ("Arpège .mid"), Palette::lavender, [this] { return dragFile (2); } };
    DragChip chipAll    { "Tout .mid",    Palette::peach,    [this] { return dragFile (3); } };
    juce::TextButton exportAll { fr ("Exporter dans le dossier") }, openFolder { fr ("Ouvrir le dossier") };
    juce::Label status;

    // Raccourcis clavier propres à la page
    bool handleKey (const juce::KeyPress& k)
    {
        const bool cmd = k.getModifiers().isCommandDown(), shift = k.getModifiers().isShiftDown();
        const int code = k.getKeyCode();
        if (roll.keyAction (k)) { selectionChanged(); return true; }
        if (roll.editNotes && juce::isPositiveAndBelow (roll.selectedNote, (int) proc.notes.size()))
        {
            const auto n = proc.notes[(size_t) roll.selectedNote];
            if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) { proc.deleteGuideNote (roll.selectedNote); roll.selectedNote = -1; repaint(); return true; }
            juce::ignoreUnused (n, shift);
        }
        if (juce::isPositiveAndBelow (selectedChord, (int) proc.prog.chords.size()))
        {
            const auto& c = proc.prog.chords[(size_t) selectedChord];
            if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) { proc.deleteGuideChord (selectedChord); selectedChord = -1; selectionChanged(); return true; }
            if (cmd && code == 'D') { const int k2 = proc.duplicateGuideChord (selectedChord); if (k2 >= 0) selectedChord = k2; selectionChanged(); return true; }
            juce::ignoreUnused (c);
        }
        return false;
    }

private:
    juce::OwnedArray<ChordPad> pads, outPads;
    juce::OwnedArray<juce::TextButton> rootButtons, qualityChips;
    juce::StringArray chipNames;
    juce::Rectangle<int> kbCard, kbOctLabel, kbHelp, kbRecStatus;
    int ideaVariant = 0;
    bool recWasArmed = false;

    void updateKb()
    {
        kbOff.setToggleState (proc.kbMode == 0, juce::dontSendNotification);
        kbNotes.setToggleState (proc.kbMode == 1, juce::dontSendNotification);
        kbChords.setToggleState (proc.kbMode == 2, juce::dontSendNotification);
        repaint();
    }

    // octave − / + : sur la sélection, ou sur toute la progression si rien n'est sélectionné
    void shiftOctave (int d)
    {
        if (roll.editNotes)
        {
            proc.beginEdit();
            if (juce::isPositiveAndBelow (roll.selectedNote, (int) proc.notes.size()))
            {
                const auto& n = proc.notes[(size_t) roll.selectedNote];
                proc.moveGuideNote (roll.selectedNote, n.start, n.pitch + 12 * d);
            }
            else
                for (int i = 0; i < (int) proc.notes.size(); ++i) proc.moveGuideNote (i, proc.notes[(size_t) i].start, proc.notes[(size_t) i].pitch + 12 * d);
        }
        else if (juce::isPositiveAndBelow (selectedChord, (int) proc.prog.chords.size()))
            proc.shiftGuideChordOctave (selectedChord, d);
        else
        {
            proc.beginEdit();
            for (auto& c : proc.prog.chords) c.octave = juce::jlimit (-2, 2, c.octave + d);
            proc.notesEdited = false;
            proc.renderGuide();
        }
        repaint();
    }
    int numPads = 7, selectedMood = 0;
    int lastKey = -1, lastScale = -1, lastSize = -1, lastVoicing = -1, lastLiveVersion = -1, lastGuideVersion = -1;
    juce::String chordText, notesText;
    juce::StringArray progLines;
    juce::Rectangle<int> liveCard, displayCard, feelCard, guideCard, progArea, inLabel, outLabel, genCard, editorCard, editPanel,
                         rootLabel, qualLabel, exportCard, exportText;
};

class ChordMidiPage : public juce::Component
{
public:
    explicit ChordMidiPage (VariantProcessor& p) : content (p), scroller (content, content.layoutHeight())
    {
        addAndMakeVisible (scroller);
        content.onLayoutChanged = [this] { scroller.setContentHeight (content.layoutHeight()); };
    }
    void resized() override { scroller.setBounds (getLocalBounds()); }
    void tick() { content.tick(); }
    ChordMidiContent content;

private:
    ScrollPage scroller;
};

//==============================================================================
// Onglet ARPÈGE
//==============================================================================
//==============================================================================
// Visualiseur de l'arpège : les notes qui sortent de l'arpège défilent en petites cases (façon piano roll simplifié),
// à droite = maintenant. Les touches tenues sont allumées sur la gauche. Tourne un potard : le motif change en direct.
class ArpVisualizer : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit ArpVisualizer (VariantProcessor& p) : proc (p)
    {
        setTooltip (fr ("Les notes de l'arpège en direct : à droite = maintenant. Joue des notes et tourne les réglages pour voir le motif changer."));
        setOpaque (false);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Palette::bg.withAlpha (0.55f));
        g.fillRoundedRectangle (r, 8.0f);

        const double now = proc.arpViewNow.load();
        const double span = 4.0;   // 1 mesure visible
        const double t0 = now - span;
        struct Box { double a, b; int note; float vel; bool input; };
        std::vector<Box> boxes; boxes.reserve (256);
        std::array<double, 128> startOut, startIn; startOut.fill (-1.0); startIn.fill (-1.0);
        std::array<float, 128> velOut {};
        const int w = proc.arpViewWrite.load (std::memory_order_acquire);
        const int first = juce::jmax (0, w - VariantProcessor::arpViewSize + 1);
        for (int i = first; i < w; ++i)
        {
            const auto e = proc.arpView[(size_t) (i % VariantProcessor::arpViewSize)];
            if (e.note < 0 || e.note > 127) continue;
            auto& st = e.input ? startIn : startOut;
            if (e.on)
            {
                if (st[(size_t) e.note] >= 0.0) boxes.push_back ({ st[(size_t) e.note], e.beat, e.note, velOut[(size_t) e.note], e.input });
                st[(size_t) e.note] = e.beat;
                if (! e.input) velOut[(size_t) e.note] = e.vel;
            }
            else if (st[(size_t) e.note] >= 0.0)
            {
                boxes.push_back ({ st[(size_t) e.note], e.beat, e.note, e.input ? 0.6f : velOut[(size_t) e.note], e.input });
                st[(size_t) e.note] = -1.0;
            }
        }
        std::array<bool, 128> heldNow {}, soundingNow {};
        for (int k = 0; k < 128; ++k)
        {
            if (startOut[(size_t) k] >= 0.0 && now - startOut[(size_t) k] < 32.0) { boxes.push_back ({ startOut[(size_t) k], now, k, velOut[(size_t) k], false }); soundingNow[(size_t) k] = true; }
            if (startIn[(size_t) k] >= 0.0 && now - startIn[(size_t) k] < 64.0) heldNow[(size_t) k] = true;
        }

        // hauteurs visibles : les notes de la fenêtre (au moins une octave)
        int lo = 128, hi = -1;
        for (auto& b : boxes) if (b.b >= t0 && ! b.input) { lo = juce::jmin (lo, b.note); hi = juce::jmax (hi, b.note); }
        for (int k = 0; k < 128; ++k) if (heldNow[(size_t) k]) { lo = juce::jmin (lo, k); hi = juce::jmax (hi, k); }
        if (hi < 0) { lo = shownLo; hi = shownHi; }
        else
        {
            if (hi - lo < 11) { const int c = (lo + hi) / 2; lo = c - 6; hi = c + 6; }
            lo -= 1; hi += 1;
            // pas de saut brusque : la plage suit en douceur
            shownLo = lo < shownLo ? lo : juce::jmin (lo, shownLo + 1);
            shownHi = hi > shownHi ? hi : juce::jmax (hi, shownHi - 1);
            lo = shownLo; hi = shownHi;
        }
        const int rows = juce::jmax (1, hi - lo + 1);

        auto area = r.reduced (6.0f, 6.0f);
        auto keys = area.removeFromLeft (22.0f);
        area.removeFromLeft (4.0f);
        const float rowH = area.getHeight() / (float) rows;
        auto yOf = [&] (int note) { return area.getBottom() - (float) (note - lo + 1) * rowH; };
        auto xOf = [&] (double beat) { return area.getX() + (float) ((beat - t0) / span) * area.getWidth(); };

        // grille : temps et doubles-croches, rangées des notes noires plus sombres
        for (int k = lo; k <= hi; ++k)
        {
            const int pc = ((k % 12) + 12) % 12;
            const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
            g.setColour ((black ? Palette::panel : Palette::panel2).withAlpha (black ? 0.55f : 0.35f));
            g.fillRect (juce::Rectangle<float> (area.getX(), yOf (k), area.getWidth(), rowH));
            g.fillRect (juce::Rectangle<float> (keys.getX(), yOf (k), keys.getWidth(), rowH - 0.5f));
            if (heldNow[(size_t) k] || soundingNow[(size_t) k])
            {
                g.setColour ((heldNow[(size_t) k] ? Palette::peach : Palette::lavender).withAlpha (heldNow[(size_t) k] ? 0.9f : 0.55f));
                g.fillRoundedRectangle (juce::Rectangle<float> (keys.getX(), yOf (k), keys.getWidth(), rowH - 0.5f).reduced (0.0f, rowH > 4.0f ? 0.5f : 0.0f), 2.0f);
            }
            if (pc == 0 && rowH >= 7.0f)
            {
                g.setColour (Palette::dim.withAlpha (0.8f));
                g.setFont (uiFont (juce::jmin (10.0f, rowH)));
                g.drawText ("C" + juce::String (k / 12), keys.withY (yOf (k)).withHeight (rowH), juce::Justification::centred, false);
            }
        }
        for (double b = std::ceil (t0 * 4.0) / 4.0; b <= now; b += 0.25)
        {
            const bool beat = std::abs (b - std::round (b)) < 1.0e-6;
            g.setColour (Palette::line.withAlpha (beat ? 0.55f : 0.18f));
            g.drawVerticalLine (juce::roundToInt (xOf (b)), area.getY(), area.getBottom());
        }

        // les cases : une par note de l'arpège (opacité = vélocité), la note qui sonne est plus claire
        g.saveState();
        g.reduceClipRegion (area.toNearestInt());
        for (auto& b : boxes)
        {
            if (b.input || b.b < t0 || b.note < lo || b.note > hi) continue;
            const float x0 = xOf (juce::jmax (t0, b.a)), x1 = juce::jmax (x0 + 3.0f, xOf (b.b) - 1.0f);
            auto box = juce::Rectangle<float> (x0, yOf (b.note) + 0.5f, x1 - x0, juce::jmax (2.0f, rowH - 1.0f));
            const bool live = b.b >= now - 1.0e-9;
            const auto c = live ? Palette::lavender.brighter (0.35f) : Palette::lavender;
            g.setColour (c.withAlpha (0.35f + 0.6f * juce::jlimit (0.0f, 1.0f, b.vel)));
            g.fillRoundedRectangle (box, juce::jmin (3.0f, rowH * 0.3f));
            if (live) { g.setColour (juce::Colours::white.withAlpha (0.55f)); g.drawRoundedRectangle (box, juce::jmin (3.0f, rowH * 0.3f), 1.0f); }
        }
        g.restoreState();
        g.setColour (Palette::peach.withAlpha (0.8f));
        g.drawVerticalLine (juce::roundToInt (area.getRight()) - 1, area.getY(), area.getBottom());

        if (boxes.empty())
        {
            g.setColour (Palette::dim);
            g.setFont (uiFont (12.5f));
            g.drawText (proc.apvts.getRawParameterValue ("arpOn")->load() > 0.5f ? fr ("Joue des notes : l'arpège s'affiche ici en direct")
                                                 : fr ("Arpège éteint : tes notes s'affichent ici. Active l'arpège pour voir le motif."),
                        area.toNearestInt(), juce::Justification::centred, true);
        }
    }

private:
    VariantProcessor& proc;
    int shownLo = 54, shownHi = 72;
};

class ArpPage : public juce::Component
{
public:
    explicit ArpPage (VariantProcessor& p) : proc (p)
    {
        // tout le contenu est dans une zone qu'on peut faire défiler (menu « Plus d'options » ouvert = page plus haute)
        body.onPaint = [this] (juce::Graphics& g) { paintBody (g); };
        scrollView.setViewedComponent (&body, false);
        scrollView.setScrollBarsShown (true, false);
        scrollView.setScrollBarThickness (8);
        scrollView.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, Palette::line.brighter (0.3f));
        addAndMakeVisible (scrollView);

        for (auto* c : std::initializer_list<juce::Component*> { &arpOn, &style, &key, &scale, &mode, &rate, &source, &vel, &octDir,
                                                                &octaves, &gate, &swing, &repeat, &steps, &human, &freedom,
                                                                &latch, &snap, &seq, &randAll, &randPattern, &detachSeq, &presetBtn })
            body.addAndMakeVisible (c);
        body.addAndMakeVisible (viz);
        body.addAndMakeVisible (arpTake);
        body.addMouseListener (&cardDragger, true);   // cases déplaçables (titre)

        randAll.setColour (juce::TextButton::buttonColourId, Palette::peach.withAlpha (0.28f));
        randAll.setTooltip (fr ("Mode, vitesse, octaves, durée, swing et motif tirés au hasard, toujours calés sur le temps"));
        randAll.onClick = [this] { proc.randomizeArp (false); seq.repaint(); };
        randPattern.setColour (juce::TextButton::buttonColourId, Palette::lavender.withAlpha (0.22f));
        randPattern.setTooltip (fr ("Garde les réglages, change seulement le rythme et les accents du motif"));
        randPattern.onClick = [this] { proc.randomizeArp (true); seq.repaint(); };
        presetBtn.setColour (juce::TextButton::buttonColourId, Palette::mint.withAlpha (0.30f));
        presetBtn.setTooltip (fr ("Presets prêts à jouer : un son de ta bibliothèque + arpège, accords, enveloppe et effets réglés d'un coup. Tu peux aussi sauver les tiens."));
        presetBtn.onClick = [this] { showPresetMenu(); };
        detach::makeButton (detachSeq, [this] { if (auto& f = detach::open()) { auto* pp = &proc; f ([pp] { return std::make_unique<StepSequencer> (*pp); }, fr ("Motif de l'arpège"), 820, 170); } });

        style.box.setTextWhenNothingSelected (fr ("Choisir…"));
        style.box.onChange = [this] { const int i = style.box.getSelectedItemIndex(); if (i >= 0) { proc.applyArpStyle (i); seq.repaint(); } };

        // menu dépliable « Plus d'options » (fermé par défaut, état mémorisé)
        for (auto* c : std::initializer_list<juce::Component*> { &prob, &ratchet, &ratchetN, &transpose, &rotate, &accent, &velRamp, &restart, &bass })
            body.addChildComponent (c);
        body.addAndMakeVisible (moreBtn);
        moreBtn.setColour (juce::TextButton::buttonColourId, Palette::lavender.withAlpha (0.14f));
        moreBtn.setTooltip (fr ("Probabilité, ratchet, transpo, décalage du motif, accents, rampe de vélocité…"));
        moreBtn.onClick = [this] { setMoreOpen (! moreOpen); library::settings().setValue ("arpMoreOpen", moreOpen); };
        setMoreOpen (library::settings().getBoolValue ("arpMoreOpen", false));
    }

    void setMoreOpen (bool open)
    {
        moreOpen = open;
        moreBtn.setButtonText (fr ("Plus d'options") + juce::String::fromUTF8 (open ? "  \xe2\x96\xb4" : "  \xe2\x96\xbe"));
        for (auto* c : std::initializer_list<juce::Component*> { &prob, &ratchet, &ratchetN, &transpose, &rotate, &accent, &velRamp, &restart, &bass })
            c->setVisible (open);
        resized();
        body.repaint();
    }

    void tick()
    {
        seq.repaint();
        if (viz.isShowing()) viz.repaint();
        if (sideStrip != nullptr && sideStrip->isVisible() && sideTick) sideTick();
    }

    // colonne « Effets rapides » qui défile (la même que sur la page Synthé), donnée par l'éditeur
    void setSideStrip (std::unique_ptr<juce::Component> strip, std::function<void()> tickFn)
    {
        sideStrip = std::move (strip);
        sideTick = std::move (tickFn);
        if (sideStrip != nullptr) addAndMakeVisible (*sideStrip);
        resized();
    }

    // hauteur du contenu : la page défile quand elle ne tient pas dans la fenêtre
    // cases de la page (déplaçables) : 0 arpège, 1 plus d'options, 2 motif, 3 en direct, 4 prise
    enum { secMain = 0, secMore, secMotif, secLive, secTake, numSecs };
    int sectionHeight (int id) const
    {
        switch (id)
        {
            case secMain:  return 262;
            case secMore:  return 26 + (moreOpen ? 6 + 124 : 0);
            case secMotif: return 190;
            case secLive:  return 170;
            default:       return TakeCard::preferredHeight;
        }
    }
    int contentHeight() const { int h = 0; for (int id = 0; id < numSecs; ++id) h += sectionHeight (id) + 10; return h + 12 + 44; }

    void paintBody (juce::Graphics& g)
    {
        drawCard (g, mainCard, fr ("ARPÈGE  ·  synchronisé au tempo de FL Studio"), Palette::lavender);
        if (moreOpen) drawCard (g, moreCard, fr ("PLUS D'OPTIONS"), Palette::lavender);
        drawCard (g, seqCard, fr ("MOTIF"), Palette::lavender);   // (le texte long passait sous les boutons)
        drawCard (g, vizCard, fr ("EN DIRECT  ·  les notes de l'arpège"), Palette::lavender);
        g.setColour (Palette::dim);
        g.setFont (uiFont (12.0f));
        g.drawFittedText (fr ("Motif : hauteur de barre = vélocité, double-clic = pas actif / silence.  "
                              "Avec plusieurs couches (onglet Son) en mode « Une note = une couche », chaque note de l'accord arpégé "
                              "joue son propre son.  Humanisation = accents et imprécisions naturelles.  Liberté = le plugin improvise un peu.  "
                              "Les cases se déplacent : glisse une case par son titre (ou clic droit sur le titre)."),
                          hintArea, juce::Justification::topLeft, 3);
        cards.paintOverlay (g);
    }

    void resized() override
    {
        auto all = getLocalBounds();
        if (sideStrip != nullptr)
        {
            // comme la page Synthé : colonne d'effets à droite quand il y a la place
            if (all.getWidth() >= 820) { sideStrip->setBounds (all.removeFromRight (all.getWidth() >= 1040 ? 200 : 150)); all.removeFromRight (8); sideStrip->setVisible (true); }
            else sideStrip->setVisible (false);
        }
        scrollView.setBounds (all);
        const int h = contentHeight();
        body.setSize (all.getWidth() - (h > all.getHeight() ? 12 : 0), juce::jmax (h, all.getHeight()));
        layoutBody();
    }

    void layoutBody()
    {
        auto r = body.getLocalBounds();
        for (int i = 0; i < numSecs; ++i)
        {
            const int id = cards.order[(size_t) i];
            auto rect = r.removeFromTop (sectionHeight (id));
            layoutSection (id, rect);
            r.removeFromTop (10);
        }
        r.removeFromTop (2);
        hintArea = r.reduced (4, 0);
        body.repaint();
    }

    void layoutSection (int id, juce::Rectangle<int> rect)
    {
        const int gap = 10;
        if (id == secMain)
        {
        mainCard = rect;
        cards.rects[(size_t) id] = mainCard;
        auto m = mainCard.reduced (14, 10).withTrimmedTop (22);
        auto row1 = m.removeFromTop (48);
        arpOn.setBounds (row1.removeFromLeft (180).withTrimmedTop (16)); row1.removeFromLeft (gap);   // (« gap » : espacement entre les réglages)
        key.setBounds (row1.removeFromLeft (70));     row1.removeFromLeft (gap);
        scale.setBounds (row1.removeFromLeft (210));  row1.removeFromLeft (gap);
        style.setBounds (row1);
        m.removeFromTop (8);
        auto row2 = m.removeFromTop (48);
        {
            // les 5 listes se partagent la largeur (la page est plus étroite avec la colonne d'effets)
            const float k = juce::jmin (1.0f, (float) (row2.getWidth() - 4 * gap) / (190.0f + 120.0f + 140.0f + 130.0f + 170.0f));
            auto w = [k] (int px) { return juce::roundToInt ((float) px * k); };
            mode.setBounds (row2.removeFromLeft (w (190)));   row2.removeFromLeft (gap);
            rate.setBounds (row2.removeFromLeft (w (120)));   row2.removeFromLeft (gap);
            source.setBounds (row2.removeFromLeft (w (140))); row2.removeFromLeft (gap);
            vel.setBounds (row2.removeFromLeft (w (130)));    row2.removeFromLeft (gap);
            octDir.setBounds (row2);
        }
        m.removeFromTop (8);
        auto row3 = m;
        auto toggles = row3.removeFromRight (200);
        latch.setBounds (toggles.removeFromTop (toggles.getHeight() / 2).withSizeKeepingCentre (200, 26));
        snap.setBounds (toggles.withSizeKeepingCentre (200, 26));
        const int w = row3.getWidth() / 7;
        for (auto* k : { &octaves, &gate, &swing, &repeat, &steps, &human, &freedom }) k->setBounds (row3.removeFromLeft (w));
        return;
        }
        if (id == secMore)
        {
            auto r = rect;
            moreBtn.setBounds (r.removeFromTop (26).withSizeKeepingCentre (220, 26));
            moreCard = {};
            cards.rects[(size_t) id] = {};   // fermé : on le déplace quand il est ouvert (par son titre)
            if (moreOpen)
            {
                r.removeFromTop (6);
                moreCard = r.removeFromTop (124);
                cards.rects[(size_t) id] = moreCard;
                auto mc = moreCard.reduced (14, 8).withTrimmedTop (20);
                auto tg = mc.removeFromRight (250);
                restart.setBounds (tg.removeFromTop (tg.getHeight() / 2).withSizeKeepingCentre (250, 26));
                bass.setBounds (tg.withSizeKeepingCentre (250, 26));
                const int kw = mc.getWidth() / 7;
                for (auto* k : { &prob, &ratchet, &ratchetN, &transpose, &rotate, &accent, &velRamp }) k->setBounds (mc.removeFromLeft (kw));
            }
            return;
        }
        if (id == secMotif)
        {
            seqCard = rect;
            cards.rects[(size_t) id] = seqCard;
            auto st = seqCard.reduced (12, 6).removeFromTop (26);
            detachSeq.setBounds (st.removeFromRight (30));
            st.removeFromRight (6);
            randAll.setBounds (st.removeFromRight (150));
            st.removeFromRight (6);
            randPattern.setBounds (st.removeFromRight (150));
            st.removeFromRight (6);
            presetBtn.setBounds (st.removeFromRight (170));
            seq.setBounds (seqCard.reduced (16, 12).withTrimmedTop (30));
            return;
        }
        if (id == secLive)
        {
            vizCard = rect;
            cards.rects[(size_t) id] = vizCard;
            viz.setBounds (vizCard.reduced (14, 10).withTrimmedTop (22));
            return;
        }
        arpTake.setBounds (rect);
        cards.rects[(size_t) id] = rect;
    }

    // glisser une case par son titre / clic droit sur le titre
    CardOrder cards { "arp", numSecs };
    struct CardDragger : public juce::MouseListener
    {
        explicit CardDragger (ArpPage& o) : p (o) {}
        ArpPage& p;
        bool onTitleArea (const juce::MouseEvent& e) const { return e.eventComponent == &p.body || e.eventComponent == &p.arpTake; }
        void mouseDown (const juce::MouseEvent& e0) override
        {
            if (! onTitleArea (e0)) return;
            const auto e = e0.getEventRelativeTo (&p.body);
            if (e.mods.isPopupMenu()) { const int id = p.cards.titleAt (e.getPosition()); if (id >= 0) p.cards.menu (id, p.body, [this] { p.resized(); }); return; }
            p.cards.down (e);
        }
        void mouseDrag (const juce::MouseEvent& e0) override { if (p.cards.dragging < 0) return; p.cards.drag (e0.getEventRelativeTo (&p.body)); p.body.repaint(); }
        void mouseUp (const juce::MouseEvent& e0) override
        {
            if (p.cards.dragging < 0) return;
            if (p.cards.up (e0.getEventRelativeTo (&p.body))) p.resized();
            p.body.repaint();
        }
    } cardDragger { *this };

private:
    VariantProcessor& proc;
    std::unique_ptr<juce::Component> sideStrip;
    std::function<void()> sideTick;
    struct Body : juce::Component
    {
        std::function<void (juce::Graphics&)> onPaint;
        void paint (juce::Graphics& g) override { if (onPaint) onPaint (g); }
    } body;
    juce::Viewport scrollView;

public:
    ParamToggle arpOn   { proc.apvts, "arpOn", fr ("Activer l'arpège"), Palette::lavender, fr ("Les notes tenues sont jouées une par une, en rythme") };
    LabeledCombo style  { fr ("Appliquer un style"), moods::names() };
    LabeledCombo key    { fr ("Tonalité"), theory::keyNames(), &proc.apvts, "key" };
    LabeledCombo scale  { "Gamme", theory::scaleNames(), &proc.apvts, "scale" };
    LabeledCombo mode   { "Mode", arp::modeNames(), &proc.apvts, "arpMode" };
    LabeledCombo rate   { "Vitesse", arp::rateNames(), &proc.apvts, "arpRate" };
    LabeledCombo source { fr ("Notes de l'arpège"), { fr ("Notes jouées"), "Gamme", fr ("Tranches du slicer") }, &proc.apvts, "arpSource" };
    LabeledCombo vel    { fr ("Vélocité"), { fr ("Comme jouée"), "Fixe" }, &proc.apvts, "arpVel" };
    LabeledCombo octDir { "Octaves vers", arp::octDirNames(), &proc.apvts, "arpOctDir" };
    LabeledKnob octaves { proc.apvts, "arpOct", "Octaves", Palette::lavender, fr ("Sur combien d'octaves l'arpège se déploie") };
    LabeledKnob gate    { proc.apvts, "arpGate", fr ("Durée"), Palette::lavender, fr ("Longueur de chaque note (plus de 100 % = notes liées)") };
    LabeledKnob swing   { proc.apvts, "arpSwing", "Swing", Palette::lavender, fr ("Décale un pas sur deux pour un groove plus humain") };
    LabeledKnob repeat  { proc.apvts, "arpRepeat", fr ("Répétition"), Palette::lavender, fr ("Joue chaque note plusieurs fois de suite") };
    LabeledKnob steps   { proc.apvts, "arpSteps", "Pas", Palette::lavender, fr ("Longueur du motif rythmique (1 à 16 pas)") };
    LabeledKnob human   { proc.apvts, "arpHuman", fr ("Humanisation"), Palette::peach, fr ("Accents naturels, timing et durées légèrement imparfaits") };
    LabeledKnob freedom { proc.apvts, "arpFree", fr ("Liberté"), Palette::peach, fr ("Le plugin improvise : sauts d'octave, notes de passage, répétitions rapides, silences") };
    ParamToggle latch   { proc.apvts, "arpLatch", fr ("Maintien (latch)"), Palette::lavender, fr ("L'arpège continue après avoir relâché les touches") };
    ParamToggle snap    { proc.apvts, "arpSnap", fr ("Contraindre à la gamme"), Palette::lavender, fr ("Ramène chaque note de l'arpège dans la gamme choisie") };
    StepSequencer seq   { proc };
    ArpVisualizer viz   { proc };
    TakeCard arpTake    { proc, true };   // enregistre le MIDI et le son de l'arpège
    juce::Rectangle<int> vizCard;
    // « Plus d'options »
    juce::TextButton moreBtn;
    bool moreOpen = false;
    juce::Rectangle<int> moreCard;
    LabeledKnob prob      { proc.apvts, "arpProb", fr ("Probabilité"), Palette::lavender, fr ("Chance que chaque pas joue : en dessous de 100 %, l'arpège saute des notes au hasard et devient vivant") };
    LabeledKnob ratchet   { proc.apvts, "arpRatchet", "Ratchet", Palette::peach, fr ("Chance qu'un pas soit rejoué très vite plusieurs fois (roulement façon trap)") };
    LabeledKnob ratchetN  { proc.apvts, "arpRatchetN", "Coups", Palette::peach, fr ("Nombre de coups d'un ratchet (2 à 4)") };
    LabeledKnob transpose { proc.apvts, "arpTranspose", "Transpo", Palette::lavender, fr ("Monte ou descend toutes les notes de l'arpège (en demi-tons)") };
    LabeledKnob rotate    { proc.apvts, "arpRotate", fr ("Décalage"), Palette::lavender, fr ("Fait démarrer le motif rythmique plus loin : même motif, autre groove") };
    LabeledKnob accent    { proc.apvts, "arpAccent", "Accent", Palette::peach, fr ("Appuie les notes qui tombent sur les temps, adoucit les autres") };
    LabeledKnob velRamp   { proc.apvts, "arpVelRamp", "Rampe", Palette::peach, fr ("Vélocité qui monte (à droite) ou descend (à gauche) sur la longueur du motif") };
    ParamToggle restart   { proc.apvts, "arpRestart", fr ("Repartir du début à chaque note"), Palette::lavender, fr ("Chaque nouvelle note jouée relance l'arpège depuis sa première note") };
    ParamToggle bass      { proc.apvts, "arpBass", fr ("Basse sur le 1er pas"), Palette::lavender, fr ("Ajoute la note la plus grave, une octave dessous, au début de chaque motif") };
    juce::TextButton randAll { fr ("Tout au hasard") }, randPattern { fr ("Motif au hasard") };
    juce::TextButton presetBtn { fr ("Presets d'arpège") + juce::String::fromUTF8 ("  ▾") };
    std::unique_ptr<juce::AlertWindow> saveWin;
    void showPresetMenu()
    {
        proc.ensureFactoryPresets (false);
        juce::PopupMenu m, factoryM, mineM;
        const auto files = presets::userPresets();
        for (int i = 0; i < files.size(); ++i)
        {
            auto x = juce::XmlDocument::parse (files[i]);
            const bool fac = x != nullptr && x->getBoolAttribute ("factory");
            juce::String snd;
            if (x != nullptr) if (auto* e = x->getChildByName ("LAYER")) snd = juce::File (e->getStringAttribute ("path")).getFileNameWithoutExtension();
            const auto label = files[i].getFileNameWithoutExtension() + (snd.isNotEmpty() ? "   (" + snd + ")" : juce::String());
            (fac ? factoryM : mineM).addItem (500 + i, label);
        }
        auto mine = VariantProcessor::userPresetFolder().findChildFiles (juce::File::findFiles, false, "*.vpreset");
        mine.sort();
        for (int i = 0; i < juce::jmin (300, mine.size()); ++i) mineM.addItem (1000 + i, mine[i].getFileNameWithoutExtension());
        m.addSectionHeader (fr ("Presets rangés dans ta banque de sons"));
        m.addSubMenu (fr ("Presets prêts à jouer"), factoryM, factoryM.getNumItems() > 0);
        m.addSubMenu (fr ("Mes presets"), mineM, mineM.getNumItems() > 0);
        m.addSeparator();
        m.addItem (900, fr ("Sauver mon preset…"));
        m.addItem (901, fr ("Ouvrir le dossier des presets (dans la banque)"));
        juce::Component::SafePointer<ArpPage> self (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetBtn), [safe = self, files, mine] (int r)
        {
            if (safe == nullptr || r == 0) return;
            if (r == 900) { safe->askPresetName(); return; }
            if (r == 901) { auto d = VariantProcessor::userPresetFolder(); d.createDirectory(); d.startAsProcess(); return; }
            if (r == 902) { safe->proc.ensureFactoryPresets (true); return; }
            const juce::File f = r >= 1000 ? mine[r - 1000] : (r >= 500 ? files[r - 500] : juce::File());
            if (f != juce::File())
                presetui::confirmLoad ([safe, f] { if (safe == nullptr) return; safe->proc.loadUserPreset (f); safe->seq.repaint(); });
        });
    }
    void askPresetName()
    {
        saveWin = std::make_unique<juce::AlertWindow> (fr ("Sauver le preset"), fr ("Nom du preset (son + arpège + accords + effets) :"), juce::AlertWindow::NoIcon);
        saveWin->addTextEditor ("name", fr ("Mon arpège"));
        saveWin->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        saveWin->addButton (fr ("Annuler"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<ArpPage> self (this);
        saveWin->enterModalState (true, juce::ModalCallbackFunction::create ([safe = self] (int r)
        {
            if (safe == nullptr || safe->saveWin == nullptr) return;
            if (r == 1) safe->proc.saveUserPreset (safe->saveWin->getTextEditorContents ("name").trim());
            safe->saveWin.reset();
        }), false);
    }
    juce::TextButton detachSeq;

private:
    juce::Rectangle<int> mainCard, seqCard, hintArea;
};

//==============================================================================
// Onglet OUTILS : détection de tempo / tonalité, audio -> MIDI
//==============================================================================
class AudioDropZone : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    std::function<void (const juce::File&)> onFile;
    juce::String text;

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (1.0f);
        juce::Path border, dashed;
        border.addRoundedRectangle (b, 12.0f);
        const float dashes[] = { 6.0f, 5.0f };
        juce::PathStrokeType (1.5f).createDashedStroke (dashed, border, dashes, 2);
        g.setColour (over ? Palette::peach.withAlpha (0.1f) : Palette::panel2.withAlpha (0.4f));
        g.fillRoundedRectangle (b, 12.0f);
        g.setColour (over ? Palette::peach : Palette::line.brighter (0.4f));
        g.fillPath (dashed);
        g.setColour (Palette::text);
        g.setFont (uiFont (15.0f));
        g.drawFittedText (text, getLocalBounds().reduced (16), juce::Justification::centred, 2);
    }
    bool isInterestedInFileDrag (const juce::StringArray& f) override { return f.size() > 0 && VariantProcessor::isSupportedFile (juce::File (f[0])); }
    void fileDragEnter (const juce::StringArray&, int, int) override { over = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { over = false; repaint(); }
    void filesDropped (const juce::StringArray& f, int, int) override { over = false; repaint(); if (onFile) onFile (juce::File (f[0])); }
    void mouseUp (const juce::MouseEvent&) override
    {
        chooser = std::make_unique<juce::FileChooser> (fr ("Choisir un fichier audio"), juce::File(), VariantProcessor::getSupportedWildcard());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe = juce::Component::SafePointer<AudioDropZone> (this)] (const juce::FileChooser& fc)
            { if (safe != nullptr && fc.getResult().existsAsFile() && safe->onFile) safe->onFile (fc.getResult()); });
    }

private:
    bool over = false;
    std::unique_ptr<juce::FileChooser> chooser;
};

class MidiNotesView : public juce::Component
{
public:
    explicit MidiNotesView (VariantProcessor& p) : proc (p) {}
    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        g.setColour (Palette::panel2.withAlpha (0.4f));
        g.fillRoundedRectangle (area, 10.0f);
        const auto r = proc.getAnalysis();
        if (r.midi.empty())
        {
            g.setColour (Palette::dim);
            g.setFont (uiFont (14.0f));
            g.drawText (fr ("Les notes détectées apparaîtront ici"), area, juce::Justification::centred);
            return;
        }
        int lo = 127, hi = 0;
        for (auto& n : r.midi) { lo = juce::jmin (lo, n.pitch); hi = juce::jmax (hi, n.pitch); }
        lo -= 2; hi += 2;
        const float rh = area.getHeight() / (float) (hi - lo + 1);
        const double len = juce::jmax (4.0, r.midiLength);
        for (int b = 0; b <= (int) len; b += 4)
        {
            g.setColour (Palette::line);
            g.drawVerticalLine ((int) (b / len * area.getWidth()), 0.0f, area.getHeight());
        }
        for (auto& n : r.midi)
        {
            auto rect = juce::Rectangle<float> ((float) (n.start / len * area.getWidth()), area.getHeight() - (float) (n.pitch - lo + 1) * rh,
                                                juce::jmax (2.0f, (float) (n.length / len * area.getWidth()) - 1.0f), juce::jmax (2.0f, rh - 1.0f));
            g.setColour (Palette::peach.withAlpha (0.4f + 0.6f * n.vel));
            g.fillRoundedRectangle (rect, 2.0f);
        }
        if (proc.isPreviewing())
        {
            g.setColour (Palette::text);
            g.fillRect (juce::Rectangle<float> (getWidth() * proc.getPreviewProgress() - 1.0f, 0.0f, 2.0f, (float) getHeight()));
        }
    }
private:
    VariantProcessor& proc;
};

class ToolsContent : public juce::Component
{
public:
    static constexpr int contentHeight = 380 + SlicerCard::preferredHeight;

    explicit ToolsContent (VariantProcessor& p) : proc (p)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &drop, &layerBox, &analyseLayer, &applyKey, &slicerCard })
            addAndMakeVisible (c);

        drop.text = fr ("Glisse un sample ici (ou clique) : tempo, tonalité et découpe en tranches d'un coup");
        drop.onFile = [this] (const juce::File& f) { proc.loadSliceFile (f); };

        for (int i = 0; i < maxLayers; ++i) layerBox.addItem (fr ("Couche ") + juce::String (i + 1), i + 1);
        layerBox.setSelectedItemIndex (0, juce::dontSendNotification);
        analyseLayer.onClick = [this] { proc.useLayerForSlices (layerBox.getSelectedItemIndex()); };

        applyKey.setColour (juce::TextButton::buttonColourId, Palette::mint.withAlpha (0.22f));
        applyKey.setTooltip (fr ("Règle la tonalité et la gamme du plugin sur celles détectées"));
        applyKey.onClick = [this]
        {
            const auto r = proc.getAnalysis();
            if (r.keys.empty()) return;
            proc.setParam ("key", (float) r.keys[0].key);
            proc.setParam ("scale", (float) (r.keys[0].minor ? theory::Minor : theory::Major));
        };
        for (int i = 0; i < 3; ++i)
        {
            addAndMakeVisible (arrows[i]);
            arrows[i].onMove = [this, i] (int d) { CardMover::move ("toolsOrder", 3, i, d); resized(); repaint(); };
        }

    }

    void tick()
    {
        if (proc.analysisVersion.load() != seen || proc.analysisBusy.load() != wasBusy)
        {
            seen = proc.analysisVersion.load();
            wasBusy = proc.analysisBusy.load();
            repaint();
        }
        slicerCard.tick();
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = proc.getAnalysis();
        drawCard (g, sourceCard, fr ("TON SAMPLE"), Palette::peach);
        drawCard (g, tempoCard, fr ("TEMPO"), Palette::blue);
        drawCard (g, keyCard, fr ("TONALITÉ"), Palette::mint);

        g.setColour (Palette::dim);
        g.setFont (uiFont (13.0f));
        juce::String info = proc.analysisBusy.load() ? fr ("Analyse en cours…")
                          : (r.name.isNotEmpty() ? r.name + juce::String::fromUTF8 ("  ·  ") + juce::String (r.seconds, 1) + " s" : fr ("Aucun son analysé"));
        g.drawText (info, infoArea, juce::Justification::centredLeft, true);

        auto t = tempoCard.reduced (18, 12).withTrimmedTop (24);
        g.setColour (Palette::text);
        g.setFont (uiFont (40.0f, juce::Font::bold));
        g.drawText (r.bpm > 0 ? juce::String (r.bpm, r.bpm == std::round (r.bpm) ? 0 : 1) : juce::String::fromUTF8 ("–"), t.removeFromTop (52), juce::Justification::centredLeft);
        g.setColour (Palette::dim);
        g.setFont (uiFont (14.0f));
        g.drawText ("BPM", t.removeFromTop (20), juce::Justification::centredLeft);
        if (r.bpm > 0)
        {
            auto bar = t.removeFromTop (22).withTrimmedTop (8).withWidth (160).toFloat();
            g.setColour (Palette::line);
            g.fillRoundedRectangle (bar, 3.0f);
            g.setColour (Palette::blue);
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jmax (0.05f, r.bpmConfidence)), 3.0f);
            g.setColour (Palette::dim);
            g.setFont (uiFont (12.5f));
            g.drawText (fr ("fiabilité") + juce::String::fromUTF8 ("  ·  ") + fr ("moitié / double : ") + juce::String (r.bpm / 2, 1) + " / " + juce::String (r.bpm * 2, 1),
                        t.removeFromTop (22), juce::Justification::centredLeft, true);
        }

        auto k = keyCard.reduced (18, 12).withTrimmedTop (24);
        g.setColour (Palette::text);
        g.setFont (uiFont (34.0f, juce::Font::bold));
        g.drawText (r.keys.empty() ? juce::String::fromUTF8 ("–") : keyName (r.keys[0]), k.removeFromTop (52), juce::Justification::centredLeft);
        if (r.keys.size() > 1)
        {
            g.setColour (Palette::dim);
            g.setFont (uiFont (14.0f));
            g.drawText (fr ("Autres possibilités : ") + keyName (r.keys[1]) + ", " + keyName (r.keys[2]), k.removeFromTop (22), juce::Justification::centredLeft, true);
        }

    }

    void resized() override
    {
        auto r = getLocalBounds();
        const auto& order = CardMover::order ("toolsOrder", 3);
        for (int bi = 0; bi < 3; ++bi)   // cartes déplaçables avec les flèches ▲ ▼ (ordre mémorisé)
        {
            if (bi > 0) r.removeFromTop (12);
            const int b = order[(size_t) bi];
            if (b == 0)
            {
                sourceCard = r.removeFromTop (170);
                auto s = sourceCard.reduced (16, 12).withTrimmedTop (24);
                auto right = s.removeFromRight (260);
                drop.setBounds (s.withTrimmedRight (14));
                infoArea = right.removeFromTop (30);
                right.removeFromTop (6);
                layerBox.setBounds (right.removeFromTop (34));
                right.removeFromTop (8);
                analyseLayer.setBounds (right.removeFromTop (36));
                arrows[0].place (sourceCard);
            }
            else if (b == 1)
            {
                auto row = r.removeFromTop (170);
                tempoCard = row.removeFromLeft ((row.getWidth() - 12) / 2);
                row.removeFromLeft (12);
                keyCard = row;
                applyKey.setBounds (keyCard.reduced (18, 14).removeFromBottom (36).removeFromLeft (230));
                arrows[1].place (keyCard);
            }
            else
            {
                slicerCard.setBounds (r.removeFromTop (SlicerCard::preferredHeight));
                arrows[2].place (slicerCard.getBounds());
            }
        }
    }

private:
    static juce::String keyName (const analysis::KeyCandidate& k)
    {
        return juce::String (theory::pcName (k.key, theory::prefersFlats (k.key, k.minor ? theory::Minor : theory::Major)))
               + " " + (k.minor ? fr ("mineur") : fr ("majeur"));
    }

    VariantProcessor& proc;
    AudioDropZone drop;
    juce::ComboBox layerBox;
    juce::TextButton analyseLayer { fr ("Utiliser cette couche") }, applyKey { fr ("Appliquer au plugin") };
    juce::Rectangle<int> sourceCard, tempoCard, keyCard, infoArea;
    CardArrows arrows[3];

public:
    SlicerCard slicerCard { proc };
    int seen = -1;
    bool wasBusy = false;
};

class ToolsPage : public juce::Component
{
public:
    explicit ToolsPage (VariantProcessor& p) : content (p), scroller (content, ToolsContent::contentHeight) { addAndMakeVisible (scroller); }
    void resized() override { scroller.setBounds (getLocalBounds()); }
    void tick() { content.tick(); }
    ToolsContent content;

private:
    ScrollPage scroller;
};
//==============================================================================
// Petits effets visuels par-dessus le plugin (ne bloquent jamais la souris)
// Cristal = paillettes, Portal = grains, Réverb = ondes, Saturation = braises, Delay = échos, Lo-fi = pixels
class SparkleOverlay : public juce::Component, private juce::Timer
{
public:
    explicit SparkleOverlay (VariantProcessor& p) : proc (p)
    {
        setInterceptsMouseClicks (false, false);
        startTimerHz (30);
    }

    // 0 = désactivés, 1 = discrets (l'ancien mode normal), 2 = normaux (plus forts, défaut)
    static int& intensity() { static int v = library::settings().getIntValue ("fxVisuals2", 2); return v; }
    static float power() { return intensity() == 0 ? 0.0f : (intensity() == 1 ? 1.0f : 1.7f); }

    void burst (int type, int count = 40)
    {
        if (intensity() == 0) return;
        const int n = (int) (count * (intensity() == 1 ? 1.0f : 1.6f));
        for (int i = 0; i < n; ++i) spawn (type, true);
    }

    void paint (juce::Graphics& g) override
    {
        const float master = intensity() == 2 ? 1.0f : 0.75f;
        for (auto& p : parts)
        {
            const float lifeT = p.age / p.life;
            const float a = juce::jlimit (0.0f, 1.0f, std::sin (lifeT * juce::MathConstants<float>::pi) * p.alpha * master);
            const juce::Point<float> c (p.x, p.y);
            const float s = p.size * (p.grow ? 0.6f + lifeT * 0.9f : 1.0f);
            if (p.glow) { g.setColour (p.colour.withAlpha (a * 0.18f)); g.fillEllipse (juce::Rectangle<float> (s * 3.2f, s * 3.2f).withCentre (c)); }
            g.setColour (p.colour.withAlpha (a));
            switch (p.shape)
            {
                case Star4: case Star5: case Star6:
                {
                    juce::Path star;
                    star.addStar (c, p.shape == Star4 ? 4 : (p.shape == Star5 ? 5 : 6), s * 0.25f, s, p.spin * p.age);
                    g.fillPath (star);
                    break;
                }
                case Twinkle:   // croix fine qui scintille
                {
                    const float k = s * (0.6f + 0.4f * std::sin (p.age * 14.0f));
                    g.drawLine (c.x - k, c.y, c.x + k, c.y, 1.0f);
                    g.drawLine (c.x, c.y - k, c.x, c.y + k, 1.0f);
                    break;
                }
                case Diamond:
                {
                    juce::Path d;
                    d.addPolygon (c, 4, s * 0.8f, p.spin * p.age);
                    g.fillPath (d);
                    break;
                }
                case Ring:
                {
                    const float rad = s * (0.3f + lifeT * 2.2f);
                    g.drawEllipse (juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (c), 1.0f);
                    break;
                }
                case Bubble:
                    g.drawEllipse (juce::Rectangle<float> (s, s).withCentre (c), 1.0f);
                    g.fillEllipse (juce::Rectangle<float> (s * 0.22f, s * 0.22f).withCentre (c.translated (-s * 0.18f, -s * 0.18f)));
                    break;
                case Pixel:
                    g.fillRect (juce::Rectangle<float> (s, s).withCentre ({ std::round (p.x / 4.0f) * 4.0f, std::round (p.y / 4.0f) * 4.0f }));
                    break;
                case Streak:
                {
                    const float len = juce::jmax (4.0f, std::hypot (p.vx, p.vy) * 0.12f);
                    const float ang = std::atan2 (p.vy, p.vx);
                    g.drawLine (c.x, c.y, c.x - std::cos (ang) * len, c.y - std::sin (ang) * len, 1.2f);
                    break;
                }
                case Wave:
                {
                    juce::Path w;
                    for (int i = 0; i <= 12; ++i)
                    {
                        const float x = c.x - s * 2.0f + (float) i * s / 3.0f;
                        const float y = c.y + std::sin ((float) i * 0.9f + p.age * 6.0f) * s * 0.35f;
                        if (i == 0) w.startNewSubPath (x, y); else w.lineTo (x, y);
                    }
                    g.strokePath (w, juce::PathStrokeType (1.0f));
                    break;
                }
                case Heart:
                {
                    juce::Path h;
                    const float r = s * 0.5f;
                    h.startNewSubPath (c.x, c.y + r);
                    h.cubicTo (c.x - r * 2.0f, c.y - r * 0.4f, c.x - r * 0.6f, c.y - r * 1.8f, c.x, c.y - r * 0.6f);
                    h.cubicTo (c.x + r * 0.6f, c.y - r * 1.8f, c.x + r * 2.0f, c.y - r * 0.4f, c.x, c.y + r);
                    g.fillPath (h);
                    break;
                }
                default:
                    g.fillEllipse (juce::Rectangle<float> (s, s).withCentre (c));
                    break;
            }
        }
    }

private:
    enum Shape { Dot = 0, Star4, Star5, Star6, Twinkle, Diamond, Ring, Bubble, Pixel, Streak, Wave, Heart };
    struct Particle { float x, y, vx, vy, age, life, size, alpha, spin; int shape; bool glow, grow; juce::Colour colour; };

    template <typename... S> int pick (S... s) { const int a[] = { s... }; return a[rng.nextInt ((int) sizeof...(s))]; }
    juce::Colour mixed (juce::Colour a, juce::Colour b) { return a.interpolatedWith (b, rng.nextFloat()); }

    void spawn (int type, bool burstMode)
    {
        if (parts.size() > (intensity() == 2 ? 240u : 140u)) return;
        const float W = (float) getWidth(), H = (float) getHeight();
        Particle p { rng.nextFloat() * W, rng.nextFloat() * H, 0, 0, 0, 1.6f + rng.nextFloat() * 2.0f, 3.0f, 0.6f,
                     (rng.nextFloat() - 0.5f) * 3.0f, Dot, false, false, Palette::lavender };
        auto drift = [&] (float sp) { p.vx = (rng.nextFloat() - 0.5f) * sp; p.vy = (rng.nextFloat() - 0.5f) * sp; };
        switch (type)
        {
            case fxs::Crystal:
                p.shape = pick (Star4, Star4, Star6, Twinkle, Diamond); p.glow = rng.nextFloat() < 0.4f;
                p.colour = mixed (juce::Colours::white, Palette::blue); p.size = 3.0f + rng.nextFloat() * 5.0f;
                p.vy = -5.0f - rng.nextFloat() * 9.0f; p.vx = (rng.nextFloat() - 0.5f) * 7.0f; p.alpha = 0.8f; break;
            case fxs::Portal:
                p.shape = pick (Dot, Bubble, Diamond); p.colour = mixed (Palette::lavender, Palette::pink);
                p.size = 2.0f + rng.nextFloat() * 4.0f; drift (22.0f); p.alpha = 0.5f; break;
            case fxs::Reverb:
                p.shape = pick (Ring, Ring, Bubble); p.colour = mixed (Palette::blue, Palette::lavender);
                p.size = 8.0f + rng.nextFloat() * 16.0f; p.life = 2.5f + rng.nextFloat() * 2.0f; p.alpha = 0.3f; p.vy = -3.0f; break;
            case fxs::Delay:
                p.shape = pick (Ring, Dot, Twinkle); p.colour = mixed (Palette::peach, Palette::pink);
                p.size = 5.0f + rng.nextFloat() * 7.0f; p.life = 1.5f; p.alpha = 0.38f; p.vx = 12.0f; break;
            case fxs::Saturation:
                p.shape = pick (Dot, Dot, Streak, Diamond); p.glow = true; p.colour = mixed (Palette::peach, Palette::pink);
                p.y = H - rng.nextFloat() * 50.0f; p.size = 2.0f + rng.nextFloat() * 3.0f;
                p.vy = -18.0f - rng.nextFloat() * 28.0f; p.vx = (rng.nextFloat() - 0.5f) * 10.0f; p.alpha = 0.65f; break;
            case fxs::Lofi:
                p.shape = Pixel; p.colour = mixed (Palette::mint, Palette::text); p.size = 2.0f + (float) rng.nextInt (3) * 2.0f;
                p.life = 0.4f + rng.nextFloat() * 0.6f; p.alpha = 0.4f; break;
            case fxs::Chorus: case fxs::Flanger: case fxs::Phaser:
                p.shape = pick (Dot, Wave, Bubble); p.colour = mixed (Palette::mint, Palette::blue); p.size = p.shape == Wave ? 6.0f : 2.5f;
                p.vx = 22.0f + rng.nextFloat() * 20.0f; p.x = 0; p.alpha = 0.4f; p.life = 3.0f; p.y = H * (0.2f + 0.6f * rng.nextFloat()); break;
            case fxs::Filter:
                p.shape = Wave; p.colour = mixed (Palette::blue, Palette::mint); p.size = 5.0f + rng.nextFloat() * 6.0f;
                p.vx = (rng.nextFloat() - 0.5f) * 12.0f; p.alpha = 0.35f; p.life = 2.2f; break;
            case fxs::Tremolo:
                p.shape = pick (Heart, Dot); p.colour = mixed (Palette::pink, Palette::peach); p.size = 4.0f + rng.nextFloat() * 4.0f;
                p.vy = -8.0f; p.alpha = 0.45f; p.grow = true; break;
            case fxs::Compressor: case fxs::EQ:
                p.shape = pick (Ring, Diamond); p.colour = mixed (Palette::lavender, Palette::blue); p.size = 4.0f + rng.nextFloat() * 6.0f;
                p.alpha = 0.3f; p.life = 1.2f; p.grow = true; break;
            case fxs::Stereo:
                p.shape = Streak; p.colour = mixed (Palette::blue, Palette::text); p.x = W * 0.5f; p.y = H * rng.nextFloat();
                p.vx = (rng.nextBool() ? 1.0f : -1.0f) * (40.0f + rng.nextFloat() * 40.0f); p.alpha = 0.35f; p.life = 1.6f; break;
            default:
                p.colour = Palette::text; p.size = 2.0f; p.alpha = 0.25f; break;
        }
        if (burstMode) { p.alpha = juce::jmin (1.0f, p.alpha + 0.15f); p.life *= 0.8f; }
        parts.push_back (p);
    }

    void timerCallback() override
    {
        if (! isShowing()) return;
        const float dt = 1.0f / 30.0f;
        const float level = juce::jlimit (0.0f, 1.0f, proc.outLevel.load() * 6.0f);
        const float amount = power();

        // les effets actifs font apparaître leurs particules (davantage quand le son joue)
        for (int s = 0; s < fxs::numSlots && amount > 0.0f; ++s)
        {
            const int ty = proc.getSlotType (s);
            if (ty <= 0 || proc.getParam (VariantProcessor::slotId (s, "On")) < 0.5f) continue;
            float rate = 0.0f;
            switch (ty)
            {
                case fxs::Crystal: rate = 0.2f + 2.0f * level; break;
                case fxs::Portal:  rate = 0.12f + 1.2f * level; break;
                case fxs::Reverb:  rate = 0.03f + 0.35f * level; break;
                case fxs::Delay:   rate = 0.02f + 0.4f * level; break;
                case fxs::Saturation: rate = 0.04f + 1.2f * level; break;
                case fxs::Lofi:    rate = 0.04f + 1.0f * level; break;
                case fxs::Chorus: case fxs::Flanger: case fxs::Phaser: rate = 0.03f + 0.35f * level; break;
                case fxs::Filter: case fxs::Tremolo: rate = 0.01f + 0.3f * level; break;
                case fxs::Compressor: case fxs::EQ: case fxs::Stereo: rate = 0.25f * level; break;
                default: break;
            }
            rate *= amount;
            while (rate > 0.0f) { if (rng.nextFloat() < rate) spawn (ty, false); rate -= 1.0f; }
        }

        for (auto& p : parts)
        {
            p.age += dt;
            p.x += p.vx * dt;
            p.y += p.vy * dt;
            if (p.shape == Dot || p.shape == Bubble) p.y += std::sin (p.age * 4.0f + p.spin) * 0.4f;
        }
        parts.erase (std::remove_if (parts.begin(), parts.end(), [] (const Particle& p) { return p.age >= p.life; }), parts.end());
        if (! parts.empty() || hadParts) repaint();
        hadParts = ! parts.empty();
    }

    VariantProcessor& proc;
    std::vector<Particle> parts;
    juce::Random rng;
    bool hadParts = false;
};
