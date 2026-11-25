#pragma once

#include "Pages.h"
#include "KickUI.h"
#include "Obliterate.h"
#include "SynthUI.h"
#include "Telemetry.h"
#include "WheelHook.h"
#include <map>

// Aide : liste des raccourcis clavier
class ShortcutsOverlay : public juce::Component
{
public:
    void paint (juce::Graphics& g) override
    {
        g.fillAll (Palette::bg.withAlpha (0.85f));
        auto card = getLocalBounds().withSizeKeepingCentre (560, 470);
        drawCard (g, card, fr ("RACCOURCIS CLAVIER"), Palette::lavender);
        static const char* rows[][2] = {
            { "Espace", "Lecture / pause de la progression (ou du MIDI dans Sampler)" },
            { "Lettres", "Jouer des notes ou des accords (si le clavier d'ordinateur est activé dans Accords & MIDI)" },
            { "Suppr", "Effacer l'accord ou la note sélectionné(e)" },
            { "Ctrl + Z  /  Ctrl + Y", "Annuler / rétablir" },
            { "Ctrl + D", "Dupliquer l'accord sélectionné" },
            { "F1", "Afficher cette aide" },
        };
        auto r = card.reduced (24, 16).withTrimmedTop (34);
        for (auto& row : rows)
        {
            auto line = r.removeFromTop (30);
            g.setColour (Palette::lavender);
            g.setFont (uiFont (14.0f, juce::Font::bold));
            g.drawText (juce::String::fromUTF8 (row[0]), line.removeFromLeft (190), juce::Justification::centredLeft);
            g.setColour (Palette::text);
            g.setFont (uiFont (14.0f));
            g.drawText (fr (row[1]), line, juce::Justification::centredLeft, true);
        }
        g.setColour (Palette::dim);
        g.setFont (uiFont (12.5f));
        g.drawText (fr ("Clique n'importe où pour fermer.  Dans FL Studio, clique d'abord dans la fenêtre du plugin."),
                    card.reduced (24, 12).removeFromBottom (20), juce::Justification::centred, true);
    }
    void mouseUp (const juce::MouseEvent&) override { setVisible (false); }
};

// Fenêtre séparée pour une page détachée (à placer où on veut à côté de FL Studio)
class PageHolder : public juce::Component, public juce::DragAndDropContainer
{
public:
    PageHolder (juce::Component& p, int logicalWidth, std::function<bool (const juce::KeyPress&)> keys, std::function<void()> keyState)
        : page (p), logicalW (logicalWidth), onKey (std::move (keys)), onKeyState (std::move (keyState))
    {
        addAndMakeVisible (page);
        page.setVisible (true);
        setWantsKeyboardFocus (true);
    }
    ~PageHolder() override { removeChildComponent (&page); page.setTransform ({}); }

    void paint (juce::Graphics& g) override { g.fillAll (Palette::bg); }
    void resized() override
    {
        const float s = juce::jmax (0.3f, (float) getWidth() / (float) logicalW);
        page.setBounds (0, 0, logicalW, (int) std::ceil ((float) getHeight() / s));
        page.setTransform (juce::AffineTransform::scale (s));
    }
    bool keyPressed (const juce::KeyPress& k) override { return onKey && onKey (k); }
    bool keyStateChanged (bool) override { if (onKeyState) onKeyState(); return false; }

private:
    juce::Component& page;
    int logicalW;
    std::function<bool (const juce::KeyPress&)> onKey;
    std::function<void()> onKeyState;
};

class PageWindow : public juce::DocumentWindow
{
public:
    PageWindow (const juce::String& title, std::unique_ptr<PageHolder> h, juce::LookAndFeel& lf, std::function<void()> closed)
        : juce::DocumentWindow ("Variant  ·  " + title, Palette::bg, juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton, true),
          holder (std::move (h)), onClosed (std::move (closed))
    {
        setLookAndFeel (&lf);
        setUsingNativeTitleBar (true);
        setContentNonOwned (holder.get(), false);
        setResizable (true, false);
        setAlwaysOnTop (true);
    }
    ~PageWindow() override { clearContentComponent(); holder.reset(); setLookAndFeel (nullptr); }
    void closeButtonPressed() override { if (onClosed) onClosed(); }

private:
    std::unique_ptr<PageHolder> holder;
    std::function<void()> onClosed;
};

// Page laissée dans la fenêtre principale quand la vraie page est détachée
class DetachedPlaceholder : public juce::Component
{
public:
    DetachedPlaceholder()
    {
        addAndMakeVisible (back);
        back.setColour (juce::TextButton::buttonColourId, Palette::lavender.withAlpha (0.25f));
        back.onClick = [this] { if (onReattach) onReattach(); };
        addAndMakeVisible (show);
        show.onClick = [this] { if (onShow) onShow(); };
    }
    std::function<void()> onReattach, onShow;
    void paint (juce::Graphics& g) override
    {
        drawCard (g, getLocalBounds().withSizeKeepingCentre (520, 200), fr ("PAGE DÉTACHÉE"), Palette::lavender);
        g.setColour (Palette::dim);
        g.setFont (uiFont (14.0f));
        g.drawFittedText (fr ("Cette page est ouverte dans sa propre fenêtre : déplace-la où tu veux dans FL Studio."),
                          getLocalBounds().withSizeKeepingCentre (470, 60).translated (0, -24), juce::Justification::centred, 2);
    }
    void resized() override
    {
        auto c = getLocalBounds().withSizeKeepingCentre (380, 36).translated (0, 40);
        back.setBounds (c.removeFromLeft (200));
        c.removeFromLeft (10);
        show.setBounds (c);
    }
private:
    juce::TextButton back { fr ("Rattacher ici") }, show { fr ("Montrer la fenêtre") };
};

#include "MiniPanels.h"

class VariantEditor : public juce::AudioProcessorEditor,
                      public juce::DragAndDropContainer,
                      private juce::Timer,
                      private juce::ChangeListener
{
public:
    explicit VariantEditor (VariantProcessor&);
    ~VariantEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& k) override;
    bool keyStateChanged (bool isKeyDown) override;

    static constexpr int baseW = 900, baseH = 730;

private:
    // contenu dessiné à la taille "logique" puis agrandi
    struct Root : public juce::Component
    {
        explicit Root (VariantEditor& e) : ed (e) {}
        void paint (juce::Graphics& g) override { ed.paintRoot (g); }
        void resized() override { ed.layoutRoot(); }
        VariantEditor& ed;
    };

    void paintRoot (juce::Graphics& g);
    void layoutRoot();
    void timerCallback() override;
    // historique des réglages : on note les changements faits à la souris
    struct TouchWatch : juce::MouseListener
    {
        double last = 0.0;
        void mouseDown (const juce::MouseEvent&) override { last = juce::Time::getMillisecondCounterHiRes(); }
        void mouseDrag (const juce::MouseEvent&) override { last = juce::Time::getMillisecondCounterHiRes(); }
        void mouseUp (const juce::MouseEvent&) override { last = juce::Time::getMillisecondCounterHiRes(); }
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override { last = juce::Time::getMillisecondCounterHiRes(); }
    } touch;
    std::vector<float> paramSnap, paramBefore;
    double paramChangedAt = 0.0;
    bool paramPending = false;
    void trackParamHistory();
    void paintAnimatedBackground (juce::Graphics&);
    void paintThemeScenery (juce::Graphics&);
    void releaseAllKeys();
    void toggleKickOut();
    void setBuddyOn (bool on);
    static juce::Image skinIcon (int skin);
    static std::vector<int> sortedSkins();
    std::unique_ptr<struct KickDesktop> kickOut;
    float bgTime = 0.0f;
    int bgTick = 0;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void showTab (int index);
    void updateSynthFocus (int previousTab);
    bool synthPageActive = false, synthAutoOn = false;
    juce::int64 synthSig = 0;
    int kbBeforeSynth = 0, kbBeforeSampler = 0;
    bool samplerPageActive = false;
    void buildAll();
    void applyTheme (int index);
    void setLanguage (int lang);
    void setScale (float s);
    juce::Rectangle<int> screenArea() const;   // zone utile de l écran (la fenêtre ne doit jamais la dépasser)
    void styleKeyboard();
    void showSettingsMenu();
    static void removeFocus (juce::Component& c);

    hostmenu::Listener automationClicks;   // clic droit sur un réglage = automation dans FL
    VariantProcessor& proc;
    juce::SharedResourcePointer<GlobalLookAndFeel> globalLnf;   // style des menus (fenêtres à part)
    VariantLookAndFeel lnf;
    Root root { *this };
    std::unique_ptr<juce::TooltipWindow> tooltips;

    juce::OwnedArray<juce::TextButton> tabs;
    juce::ComboBox themeBox;   // (liste des thèmes : maintenant dans le menu Réglages)
    juce::TextButton bankButton;   // ouvre la bibliothèque / banque de sons
    juce::TextButton bankPrev { juce::String::fromUTF8 ("◀") }, bankNext { juce::String::fromUTF8 ("▶") };   // son précédent / suivant sans ouvrir la banque
    void stepSound (int dir);
    int bankLabelTicket = 0;
    MetroButton metroButton { proc };
    juce::TextButton saveButton { juce::String::fromUTF8 ("▣") };   // sauver le preset (disquette dessinée)
    juce::TextButton settingsButton { juce::String::fromUTF8 ("⚙") };

    std::unique_ptr<SoundPage> soundPage;
    std::unique_ptr<FxPage>    fxPage;
    std::unique_ptr<ChordMidiPage> chordPage;
    std::unique_ptr<ArpPage>   arpPage;
    std::unique_ptr<ToolsPage> toolsPage;
    std::unique_ptr<synthui::SynthPage> synthPage;
    std::unique_ptr<LibraryPanel> libraryPanel;
    ShortcutsOverlay help;
    telemetry::StatsPanel statsPanel;
    telemetry::ReportPanel reportPanel;   // Réglages > Signaler un bug
    telemetry::HandleGate handleGate;
    telemetry::BlockedScreen blockedScreen;
    std::unique_ptr<SparkleOverlay> sparkles;
    std::map<int, std::vector<int>> kbHeld;   // touche -> notes jouées
    std::map<int, int> kbVk;                  // touche -> touche Windows (relâchement fiable des chiffres / ponctuation)
    void* wheelHwnd = nullptr;                // fenêtre inscrite pour la molette (FL)
    bool kbChordChannel = false;
    int keyIndex (const juce::KeyPress& k, bool& black, int& octaveDelta) const;

    RootKeyboard keyboard;

    // pages détachables
    juce::Component* pageAt (int i) const;
    void detachPage (int i, bool restorePosition = true);
    void reattachPage (int i);
    juce::TextButton detachButton { juce::String::fromUTF8 ("⧉") };
    DetachedPlaceholder placeholder;
    static constexpr int numPages = 6;
    std::array<std::unique_ptr<PageWindow>, numPages> windows;
    std::array<std::unique_ptr<MiniWindow>, 3> minis;   // générateur, export, pads
    void openMini (int kind);
    void openPanel (detach::Maker make, const juce::String& title, int w, int h);
    std::vector<std::unique_ptr<MiniWindow>> panels;   // cartes détachées (effets, couche, motif…)

    // OBLITERATE
    ObliterateButton obliterateBtn;
    ObliterateOverlay boom;
    float shakeX = 0.0f, shakeY = 0.0f;
    bool bassShaking = false;
    float boomVibe = 0.0f;
    juce::Random bassRng;

    // guide "Kick"
    Buddy buddy;
    BuddyBubble bubble;
    bool buddyOn = true;
    juce::String hoverText;
    int hoverTicks = 0, quietTicks = 0, tipIndex = 0, introIndex = 0, muteTicks = 0, buddySpam = 0, idleTicks = 0;
    std::vector<int> tipOrder;   // astuces de Kick mélangées (pas de répétition)
    bool arrowWasDown[2] { false, false };
    double lastBuddyClick = 0.0;
    juce::Point<float> lastMouse;
    bool idleTeased = false;
    void updateBuddy();
    void buddySay (const juce::String& title, const juce::String& text);
    juce::String pageHelp (int tab) const;
    KickChat chat;
    TourOverlay tour;
    void startTour();
    void showSkinMenu();
    void sayQuote();
    void sayTip();
    void initObliterate();

    juce::Rectangle<int> headerArea, pageArea, hintArea;
    juce::String hintText;
    void updateHint();
    int currentTab = 0, lastRoot = -1;
    float uiScale = 1.25f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VariantEditor)
};

// Variant Reverb, Variant Delay… (un seul effet) : juste la carte de l'effet, sans le reste de Variant
juce::AudioProcessorEditor* createSingleFxEditor (VariantProcessor& p);
