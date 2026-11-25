#pragma once
// Mini-fenêtres détachables : générateur de progression, export vers FL, pads du slicer
#include "Pages.h"

class MiniGenerator : public juce::Component, private juce::Timer
{
public:
    explicit MiniGenerator (VariantProcessor& p) : proc (p)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &mood, &bars, &rich, &spice, &variety, &generate, &surprise, &listen })
            addAndMakeVisible (c);
        mood.box.setSelectedItemIndex (proc.genSettings.mood, juce::dontSendNotification);
        bars.box.setSelectedItemIndex (proc.genSettings.bars > 4 ? 1 : 0, juce::dontSendNotification);
        rich.box.setSelectedItemIndex (proc.genSettings.richness, juce::dontSendNotification);
        mood.box.onChange = [this]
        {
            proc.genSettings.mood = mood.box.getSelectedItemIndex();
            proc.setParam ("scale", (float) moods::list()[(size_t) proc.genSettings.mood].scales.front());
            proc.regenerateGuide (true);
        };
        bars.box.onChange = [this] { proc.setGuideLength (bars.box.getSelectedItemIndex() == 1 ? 8 : 4); };
        rich.box.onChange = [this] { proc.genSettings.richness = rich.box.getSelectedItemIndex(); };
        generate.setColour (juce::TextButton::buttonColourId, Palette::mint.withAlpha (0.3f));
        generate.onClick = [this] { sync(); proc.regenerateGuide (true); };
        surprise.setColour (juce::TextButton::buttonColourId, Palette::peach.withAlpha (0.3f));
        surprise.onClick = [this] { proc.surpriseGuide(); mood.box.setSelectedItemIndex (proc.genSettings.mood, juce::dontSendNotification);
                                    rich.box.setSelectedItemIndex (proc.genSettings.richness, juce::dontSendNotification); };
        listen.onClick = [this] { proc.previewGuide (! proc.isPreviewing()); };
        startTimerHz (8);
    }

    static constexpr int logicalW = 520, logicalH = 190;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Palette::bg);
        drawCard (g, getLocalBounds().reduced (6), fr ("GÉNÉRER UNE PROGRESSION"), Palette::mint);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (20, 14).withTrimmedTop (22);
        auto r1 = r.removeFromTop (48);
        mood.setBounds (r1.removeFromLeft (170)); r1.removeFromLeft (8);
        bars.setBounds (r1.removeFromLeft (110)); r1.removeFromLeft (8);
        rich.setBounds (r1);
        r.removeFromTop (8);
        auto r2 = r;
        spice.setBounds (r2.removeFromLeft (90));
        variety.setBounds (r2.removeFromLeft (90));
        r2.removeFromLeft (8);
        auto btns = r2.withSizeKeepingCentre (r2.getWidth(), 34);
        listen.setBounds (btns.removeFromRight (80)); btns.removeFromRight (6);
        surprise.setBounds (btns.removeFromRight (120)); btns.removeFromRight (6);
        generate.setBounds (btns);
    }

private:
    void sync()
    {
        proc.genSettings.spice = proc.getParam ("genSpice");
        proc.genSettings.variety = proc.getParam ("genVariety");
    }
    void timerCallback() override { listen.setButtonText (proc.isPreviewing() ? fr ("Stop") : fr ("Écouter")); sync(); }

    VariantProcessor& proc;
    LabeledCombo mood { fr ("Ambiance"), moods::names() };
    LabeledCombo bars { "Longueur", { fr ("4 mesures"), fr ("8 mesures") } };
    LabeledCombo rich { "Richesse", { "Simple", "Riche", fr ("Très riche") } };
    LabeledKnob spice { proc.apvts, "genSpice", "Hors gamme", Palette::peach, fr ("Plus haut = accords plus colorés, hors de la gamme") };
    LabeledKnob variety { proc.apvts, "genVariety", fr ("Variété"), Palette::mint, fr ("Plus haut = sus, add9, quartes, renversements…") };
    juce::TextButton generate { fr ("Générer") }, surprise { fr ("Surprends-moi") }, listen { fr ("Écouter") };
};

class MiniExport : public juce::Component
{
public:
    explicit MiniExport (VariantProcessor& p) : proc (p)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &chords, &bass, &arp, &all, &exportAll })
            addAndMakeVisible (c);
        exportAll.onClick = [this] { for (int i = 0; i < 4; ++i) proc.exportGuide (i); };
    }

    static constexpr int logicalW = 520, logicalH = 150;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Palette::bg);
        drawCard (g, getLocalBounds().reduced (6), fr ("EXPORT VERS FL STUDIO  ·  glisse une pastille"), Palette::mint);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (20, 14).withTrimmedTop (24);
        auto row = r.removeFromTop (42);
        const int w = (row.getWidth() - 24) / 4;
        for (auto* c : { &chords, &bass, &arp, &all }) { c->setBounds (row.removeFromLeft (w)); row.removeFromLeft (8); }
        r.removeFromTop (10);
        exportAll.setBounds (r.removeFromTop (32).removeFromLeft (200));
    }

private:
    VariantProcessor& proc;
    DragChip chords { "Accords .mid", Palette::mint, [this] { return proc.exportGuide (0); } };
    DragChip bass   { "Basse .mid", Palette::blue, [this] { return proc.exportGuide (1); } };
    DragChip arp    { "Arpège .mid", Palette::lavender, [this] { return proc.exportGuide (2); } };
    DragChip all    { "Tout .mid", Palette::peach, [this] { return proc.exportGuide (3); } };
    juce::TextButton exportAll { fr ("Exporter dans le dossier") };
};

class MiniPads : public juce::Component, private juce::Timer
{
public:
    explicit MiniPads (VariantProcessor& p) : proc (p)
    {
        for (int i = 0; i < slicer::padsPerBank; ++i) addAndMakeVisible (pads.add (new SlicePad (proc, i)));
        for (auto* b : { &kb, &rec }) addAndMakeVisible (b);
        kb.setColour (juce::TextButton::buttonOnColourId, Palette::peach);
        kb.onClick = [this] { proc.kbMode = proc.kbMode == 3 ? 0 : 3; library::settings().setValue ("samplerKbOff", proc.kbMode != 3); refresh(); };
        rec.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffff4d6d));
        rec.onClick = [this] { proc.armSliceRec (! proc.isSliceRecArmed()); refresh(); };
        startTimerHz (20);
        refresh();
    }

    static constexpr int logicalW = 520, logicalH = 200;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Palette::bg);
        drawCard (g, getLocalBounds().reduced (6), fr ("PADS DU SLICER"), Palette::peach);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (20, 14).withTrimmedTop (22);
        auto top = r.removeFromTop (30);
        kb.setBounds (top.removeFromLeft (160)); top.removeFromLeft (8);
        rec.setBounds (top.removeFromLeft (100));
        r.removeFromTop (8);
        const int pw = r.getWidth() / 8, ph = r.getHeight() / 2;
        for (int i = 0; i < pads.size(); ++i) pads[i]->setBounds (r.getX() + (i % 8) * pw, r.getY() + (i / 8) * ph, pw, ph);
    }

private:
    void refresh()
    {
        kb.setToggleState (proc.kbMode == 3, juce::dontSendNotification);
        kb.setButtonText (proc.kbMode == 3 ? fr ("Clavier : ON") : fr ("Jouer au clavier"));
        rec.setToggleState (proc.isSliceRecArmed(), juce::dontSendNotification);
        rec.setButtonText (proc.isSliceRecArmed() ? fr ("■ Stop") : fr ("● Rec"));
    }
    void timerCallback() override { refresh(); for (auto* p : pads) p->repaint(); }

    VariantProcessor& proc;
    juce::OwnedArray<SlicePad> pads;
    juce::TextButton kb { fr ("Jouer au clavier") }, rec { fr ("● Rec") };
};

// Fenêtre flottante qui possède son panneau
class MiniWindow : public juce::DocumentWindow
{
public:
    MiniWindow (const juce::String& title, std::unique_ptr<juce::Component> p, int logicalW, int logicalH, float scale,
                juce::LookAndFeel& lf, std::function<bool (const juce::KeyPress&)> keys, std::function<void()> keyState, std::function<void()> closed)
        : juce::DocumentWindow ("Variant  ·  " + title, Palette::bg, juce::DocumentWindow::closeButton, true),
          panel (std::move (p)), onClosed (std::move (closed))
    {
        setLookAndFeel (&lf);
        holder = std::make_unique<PageHolder> (*panel, logicalW, std::move (keys), std::move (keyState));
        setUsingNativeTitleBar (true);
        setContentNonOwned (holder.get(), false);
        setResizable (true, false);
        setResizeLimits (200, 80, 3000, 2000);
        setAlwaysOnTop (true);
        centreWithSize ((int) (logicalW * scale), (int) (logicalH * scale));
        setVisible (true);
    }
    ~MiniWindow() override { clearContentComponent(); holder.reset(); panel.reset(); setLookAndFeel (nullptr); }
    void closeButtonPressed() override { if (onClosed) onClosed(); }
    void setOnClosed (std::function<void()> f) { onClosed = std::move (f); }

private:
    std::unique_ptr<juce::Component> panel;
    std::unique_ptr<PageHolder> holder;
    std::function<void()> onClosed;
};
