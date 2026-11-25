#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "LookAndFeel.h"

using APVTS = juce::AudioProcessorValueTreeState;

//==============================================================================
// Clic droit sur n'importe quel réglage : automatisation dans FL Studio
// - le menu de FL pour ce paramètre (« Create automation clip », « Link to controller »…) quand FL le fournit
// - « Créer un clip d'automation » : le réglage devient le « dernier réglage touché » de FL, puis
//   Tools > Last tweaked > Create automation clip dans FL (marche dans tous les cas)
namespace hostmenu
{
    inline juce::AudioProcessorEditor* editorOf (juce::Component& c)
    {
        if (auto* e = dynamic_cast<juce::AudioProcessorEditor*> (&c)) return e;
        return c.findParentComponentOfClass<juce::AudioProcessorEditor>();
    }
    inline juce::RangedAudioParameter* paramOf (juce::Component& c, const juce::String& id)
    {
        auto* ed = editorOf (c);
        if (ed == nullptr) return nullptr;
        for (auto* p : ed->processor.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (r->getParameterID() == id) return r;
        return nullptr;
    }
    // le réglage devient le « dernier réglage touché » de FL (sans changer sa valeur)
    inline void touch (juce::RangedAudioParameter& p)
    {
        const float v = p.getValue();
        p.beginChangeGesture();
        p.setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v + (v < 0.5f ? 1.0e-4f : -1.0e-4f)));
        p.setValueNotifyingHost (v);
        p.endChangeGesture();
    }
    // ajoute la partie « automation » à un menu existant
    inline void append (juce::PopupMenu& m, juce::Component& c, const juce::String& id)
    {
        auto* p = paramOf (c, id);
        if (p == nullptr) return;
        // chaque logiciel a sa façon de faire : le menu et l'explication suivent le logiciel où Variant est ouvert
        const juce::PluginHostType host;
        const bool fl = host.isFruityLoops(), live = host.isAbletonLive(), logic = host.isLogic() || host.isGarageBand();
        m.addSeparator();
        m.addSectionHeader (fl ? fr ("Automation dans FL Studio") : (live ? fr ("Automation dans Ableton") : (logic ? fr ("Automation dans Logic") : fr ("Automation"))));
        if (auto* ed = editorOf (c))
            if (auto* ctx = ed->getHostContext())
                if (auto hm = ctx->getContextMenuForParameter (p))
                {
                    auto pm = hm->getEquivalentPopupMenu();
                    for (juce::PopupMenu::MenuItemIterator it (pm); it.next();) m.addItem (it.getItem());
                }
        juce::Component::SafePointer<juce::Component> safe (&c);
        m.addItem (fl ? fr ("Créer un clip d'automation…") : fr ("Automatiser ce réglage…"), [safe, id, fl, live, logic]
        {
            if (safe == nullptr) return;
            auto* q = paramOf (*safe, id);
            if (q == nullptr) return;
            touch (*q);
            const auto name = fr ("« ") + q->getName (60) + fr (" »");
            juce::String how;
            if (fl)        how = fr ("Dans FL Studio : menu Tools > Last tweaked > Create automation clip.\nLe clip apparaît dans l'arrangement (Playlist) et fait bouger ce réglage de Variant.");
            else if (live) how = fr ("Dans Ableton : en vue Arrangement, sous le nom de la piste choisis « Variant » puis ce réglage, et dessine la courbe.\nOu active le bouton d'enregistrement d'automation et tourne le potard pendant la lecture.");
            else if (logic) how = fr ("Dans Logic : touche A pour voir l'automation, choisis « Variant » puis ce réglage sur la piste, et dessine la courbe.\nOu passe la piste en mode Latch et tourne le potard pendant la lecture.");
            else           how = fr ("Dans ton logiciel : ouvre l'automation de la piste, choisis « Variant » puis ce réglage.\nOu active l'enregistrement d'automation et tourne le potard pendant la lecture.");
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, fr ("Automation"), name + fr (" est prêt.\n\n") + how);
        });
    }
    // menu complet (réinitialiser + automation) pour un réglage qui n'a pas son propre menu
    inline void show (juce::Component& c, const juce::String& id)
    {
        if (paramOf (c, id) == nullptr) return;
        juce::PopupMenu m;
        juce::Component::SafePointer<juce::Component> safe (&c);
        m.addItem (fr ("Réinitialiser"), [safe, id]
        {
            if (safe == nullptr) return;
            if (auto* q = paramOf (*safe, id)) { q->beginChangeGesture(); q->setValueNotifyingHost (q->getDefaultValue()); q->endChangeGesture(); }
        });
        append (m, c, id);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&c));
    }
    // marque un composant : clic droit dessus = menu d'automation de ce paramètre
    inline void tag (juce::Component& c, const juce::String& id) { c.getProperties().set ("vparam", id); }

    // branché sur l'éditeur (addMouseListener (…, true)) : clic droit sur un réglage marqué
    struct Listener : public juce::MouseListener
    {
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (! e.mods.isPopupMenu()) return;
            for (auto* c = e.eventComponent; c != nullptr; c = c->getParentComponent())
            {
                const auto id = c->getProperties()["vparam"].toString();
                if (id.isNotEmpty()) { show (*c, id); return; }
                if (dynamic_cast<juce::AudioProcessorEditor*> (c) != nullptr) return;
            }
        }
    };
}

//==============================================================================
// Cases déplaçables d'une page : on glisse une case par son titre (ou clic droit sur le titre : monter / descendre).
// L'ordre est gardé dans les réglages de Variant (« cards.<page> »).
struct CardOrder
{
    CardOrder (juce::String settingsKey, int count, std::vector<int> defaultOrder = {}) : key ("cards." + settingsKey), n (count), defOrder (std::move (defaultOrder))
    {
        if ((int) defOrder.size() != n) { defOrder.clear(); for (int i = 0; i < n; ++i) defOrder.push_back (i); }
        order = defOrder;
        juce::StringArray t; t.addTokens (library::settings().getValue (key), ",", "");
        if (t.size() == n)
        {
            std::vector<int> o; std::vector<bool> seen ((size_t) n, false); bool ok = true;
            for (auto& s : t) { const int v = s.getIntValue(); if (v < 0 || v >= n || seen[(size_t) v]) { ok = false; break; } seen[(size_t) v] = true; o.push_back (v); }
            if (ok) order = o;
        }
        rects.resize ((size_t) n);
        handles.resize ((size_t) n);
    }
    bool hasSaved() const { return library::settings().containsKey (key); }
    void save() const { juce::StringArray t; for (int v : order) t.add (juce::String (v)); library::settings().setValue (key, t.joinIntoString (",")); library::settings().saveIfNeeded(); }
    int indexOf (int id) const { for (int i = 0; i < n; ++i) if (order[(size_t) i] == id) return i; return 0; }
    void moveTo (int id, int index)
    {
        index = juce::jlimit (0, n - 1, index);
        order.erase (order.begin() + indexOf (id));
        order.insert (order.begin() + index, id);
        save();
    }
    void reset() { order = defOrder; save(); }
    // zone qu'on attrape pour déplacer la case (par défaut : le haut de la case, là où est son titre)
    juce::Rectangle<int> handleOf (int id) const { const auto& h = handles[(size_t) id]; return ! h.isEmpty() ? h : rects[(size_t) id].withHeight (26); }

    // --- glisser une case par son titre (coordonnées du composant qui contient les cases)
    int titleAt (juce::Point<int> p) const { for (int id = 0; id < n; ++id) if (! rects[(size_t) id].isEmpty() && handleOf (id).contains (p)) return id; return -1; }
    bool down (const juce::MouseEvent& e)
    {
        dragging = titleAt (e.getPosition());
        dropIndex = -1;
        return dragging >= 0;
    }
    void drag (const juce::MouseEvent& e)
    {
        if (dragging < 0 || e.getDistanceFromDragStart() < 6) return;
        int idx = 0;
        for (int i = 0; i < n; ++i) { const auto& r = rects[(size_t) order[(size_t) i]]; if (! r.isEmpty() && e.y > r.getCentreY()) idx = i + 1; }
        dropIndex = idx;
    }
    // true si l'ordre a changé
    bool up (const juce::MouseEvent& e)
    {
        const int id = dragging, to = dropIndex;
        dragging = dropIndex = -1;
        if (id < 0 || to < 0 || e.getDistanceFromDragStart() < 6) return false;
        const int from = indexOf (id);
        const int target = to > from ? to - 1 : to;
        if (target == from) return false;
        moveTo (id, target);
        return true;
    }
    // menu du titre : monter, descendre, ordre d'origine
    void menu (int id, juce::Component& target, std::function<void()> changed)
    {
        juce::PopupMenu m;
        const int at = indexOf (id);
        m.addItem (1, fr ("Monter cette case"), at > 0);
        m.addItem (2, fr ("Descendre cette case"), at < n - 1);
        m.addItem (3, fr ("Mettre tout en haut"), at > 0);
        m.addSeparator();
        m.addItem (4, fr ("Remettre l'ordre d'origine"));
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target).withMousePosition(), [this, id, changed] (int r)
        {
            if (r == 1) moveTo (id, indexOf (id) - 1);
            else if (r == 2) moveTo (id, indexOf (id) + 1);
            else if (r == 3) moveTo (id, 0);
            else if (r == 4) reset();
            else return;
            if (changed) changed();
        });
    }
    // repère de dépôt + poignée sur les titres
    void paintOverlay (juce::Graphics& g) const
    {
        for (int id = 0; id < n; ++id)
        {
            if (rects[(size_t) id].isEmpty()) continue;
            const auto r = handleOf (id);
            g.setColour (Palette::dim.withAlpha (dragging == id ? 0.9f : 0.35f));
            for (int k = 0; k < 3; ++k) for (int j = 0; j < 2; ++j)
                g.fillEllipse ((float) r.getX() + 5.0f + j * 4.0f, (float) r.getY() + 8.0f + k * 4.0f, 2.2f, 2.2f);
        }
        if (dragging >= 0 && dropIndex >= 0)
        {
            int y = 0;
            if (dropIndex >= n) { const auto& r = rects[(size_t) order.back()]; y = r.getBottom() + 4; }
            else { const auto& r = rects[(size_t) order[(size_t) dropIndex]]; y = r.getY() - 5; }
            const auto& any = rects[(size_t) dragging];
            g.setColour (Palette::peach);
            g.fillRoundedRectangle ((float) any.getX(), (float) y - 1.5f, (float) any.getWidth(), 3.0f, 1.5f);
        }
    }

    juce::String key;
    int n;
    std::vector<int> order;
    std::vector<juce::Rectangle<int>> rects;   // rectangle de chaque case (rempli par la mise en page)
    std::vector<juce::Rectangle<int>> handles;   // zone à attraper (vide = le haut de la case)
    std::vector<int> defOrder;
    int dragging = -1, dropIndex = -1;
};

// Souris pour CardOrder, à brancher sur la page (addMouseListener (…, true)) : on attrape le titre d'une case
// même s'il est dessiné par un composant enfant, mais jamais sur un bouton, un potard ou une liste.
struct CardOrderMouse : public juce::MouseListener
{
    CardOrderMouse (juce::Component& h, CardOrder& o, std::function<void()> changed) : host (h), cards (o), onChanged (std::move (changed)) {}
    static bool interactive (juce::Component* c)
    {
        return dynamic_cast<juce::Button*> (c) != nullptr || dynamic_cast<juce::Slider*> (c) != nullptr || dynamic_cast<juce::ComboBox*> (c) != nullptr
            || dynamic_cast<juce::TextEditor*> (c) != nullptr || dynamic_cast<juce::Label*> (c) != nullptr && static_cast<juce::Label*> (c)->isEditable();
    }
    void mouseDown (const juce::MouseEvent& e0) override
    {
        if (interactive (e0.eventComponent)) return;
        const auto e = e0.getEventRelativeTo (&host);
        if (e.mods.isPopupMenu())
        {
            const int id = cards.titleAt (e.getPosition());
            if (id >= 0) cards.menu (id, host, [this] { if (onChanged) onChanged(); });
            return;
        }
        cards.down (e);
    }
    void mouseDrag (const juce::MouseEvent& e0) override
    {
        if (cards.dragging < 0) return;
        cards.drag (e0.getEventRelativeTo (&host));
        host.repaint();
    }
    void mouseUp (const juce::MouseEvent& e0) override
    {
        if (cards.dragging < 0) return;
        if (cards.up (e0.getEventRelativeTo (&host)) && onChanged) onChanged();
        host.repaint();
    }
    juce::Component& host;
    CardOrder& cards;
    std::function<void()> onChanged;
};

// majuscules avec les accents (« Réverb » -> « RÉVERB » : toUpperCase de Windows laisse le é en minuscule)
inline juce::String upperFr (const juce::String& s)
{
    static const juce::String lo = juce::String::fromUTF8 ("éèêëàâîïôûùüç"),
                              up = juce::String::fromUTF8 ("ÉÈÊËÀÂÎÏÔÛÙÜÇ");
    return s.replaceCharacters (lo, up).toUpperCase();
}

// boutons / listes qui ne réagissent pas au clic droit (il ouvre le menu d'automation)
struct PopupSafeToggle : public juce::ToggleButton
{
    using juce::ToggleButton::ToggleButton;
    void mouseDown (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) juce::ToggleButton::mouseDown (e); }
    void mouseUp (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) juce::ToggleButton::mouseUp (e); }
};
struct PopupSafeTextButton : public juce::TextButton
{
    using juce::TextButton::TextButton;
    void mouseDown (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) juce::TextButton::mouseDown (e); }
    void mouseUp (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) juce::TextButton::mouseUp (e); }
};
struct PopupSafeCombo : public juce::ComboBox
{
    using juce::ComboBox::ComboBox;
    void mouseDown (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) juce::ComboBox::mouseDown (e); }
    void mouseUp (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) juce::ComboBox::mouseUp (e); }
};

//==============================================================================
// Bouton rotatif + nom en dessous
// Bouton rotatif : clic droit = menu façon FL (réinitialiser, taper une valeur)
struct KnobSlider : public juce::Slider
{
    std::function<void()> onRightClick;
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onRightClick) { onRightClick(); return; }
        juce::Slider::mouseDown (e);
    }
};

class LabeledKnob : public juce::Component
{
public:
    LabeledKnob (APVTS& state, const juce::String& id, const juce::String& name, juce::Colour colour, const juce::String& tip)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
        slider.setTooltip (fr (tip) + "\n" + fr ("Double-clic : valeur par défaut"));
        addAndMakeVisible (slider);

        label.setText (fr (name), juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (uiFont (13.0f));
        label.setColour (juce::Label::textColourId, Palette::text.withAlpha (0.8f));
        label.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (label);

        attachment = std::make_unique<APVTS::SliderAttachment> (state, id, slider);
        if (auto* p = state.getParameter (id))
            slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        slider.onRightClick = [this, id]
        {
            juce::PopupMenu m;
            m.addItem (1, fr ("Réinitialiser"));
            m.addItem (2, fr ("Taper une valeur…"));
            hostmenu::append (m, slider, id);   // automation dans FL
            juce::Component::SafePointer<LabeledKnob> self (this);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&slider), [safe = self] (int r)
            {
                if (safe == nullptr) return;
                auto& s = safe->slider;
                if (r == 1) s.setValue (s.getDoubleClickReturnValue(), juce::sendNotificationSync);
                if (r == 2)
                {
                    auto* w = new juce::AlertWindow (safe->label.getText(), fr ("Nouvelle valeur :"), juce::AlertWindow::NoIcon);
                    w->addTextEditor ("v", juce::String (s.getValue(), 2));
                    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
                    w->addButton (fr ("Annuler"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
                    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int res)
                    {
                        if (res == 1 && safe != nullptr) safe->slider.setValue (w->getTextEditorContents ("v").retainCharacters ("0123456789.,-").replaceCharacter (',', '.').getDoubleValue(), juce::sendNotificationSync);
                    }), true);
                }
            });
        };
    }

    void resized() override
    {
        auto r = getLocalBounds();
        label.setBounds (r.removeFromBottom (20));
        const int size = juce::jmin (r.getWidth(), r.getHeight(), 86);
        slider.setBounds (r.withSizeKeepingCentre (size, size));
    }

    void setEnabledLook (bool e) { slider.setEnabled (e); label.setAlpha (e ? 1.0f : 0.4f); }
    void setTextFunction (std::function<juce::String (double)> f) { slider.textFromValueFunction = std::move (f); slider.repaint(); }

    KnobSlider slider;
    juce::Label label;

private:
    std::unique_ptr<APVTS::SliderAttachment> attachment;
};

//==============================================================================
// Petite légende + liste déroulante liée à un paramètre (ou libre si id vide)
class LabeledCombo : public juce::Component
{
public:
    LabeledCombo (const juce::String& caption, const juce::StringArray& items, APVTS* state = nullptr,
                  const juce::String& id = {})
    {
        label.setText (fr (caption), juce::dontSendNotification);
        label.setFont (uiFont (12.5f));
        label.setColour (juce::Label::textColourId, Palette::dim);
        label.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (label);

        juce::StringArray tr;
        for (auto& s : items) tr.add (fr (s));
        box.addItemList (tr, 1);
        addAndMakeVisible (box);

        if (state != nullptr && id.isNotEmpty())
        {
            attachment = std::make_unique<APVTS::ComboBoxAttachment> (*state, id, box);
            hostmenu::tag (box, id);   // clic droit = automation dans FL
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        label.setBounds (r.removeFromTop (16));
        r.removeFromTop (2);
        box.setBounds (r.removeFromTop (30));
    }

    juce::Label label;
    PopupSafeCombo box;

private:
    std::unique_ptr<APVTS::ComboBoxAttachment> attachment;
};

//==============================================================================
// Interrupteur lié à un paramètre
class ParamToggle : public juce::Component
{
public:
    ParamToggle (APVTS& state, const juce::String& id, const juce::String& text, juce::Colour accent, const juce::String& tip = {})
    {
        button.setButtonText (fr (text));
        button.setColour (juce::ToggleButton::tickColourId, accent);
        button.setTooltip (fr (tip));
        addAndMakeVisible (button);
        attachment = std::make_unique<APVTS::ButtonAttachment> (state, id, button);
        hostmenu::tag (button, id);   // clic droit = automation dans FL
    }

    void resized() override { button.setBounds (getLocalBounds()); }

    PopupSafeToggle button;

private:
    std::unique_ptr<APVTS::ButtonAttachment> attachment;
};

//==============================================================================
// Boutons segmentés liés à un paramètre "choix"
class SegmentedChoice : public juce::Component
{
public:
    SegmentedChoice (APVTS& state, const juce::String& id, const juce::StringArray& labels, juce::Colour accent)
    {
        auto* param = state.getParameter (id);
        jassert (param != nullptr);

        for (int i = 0; i < labels.size(); ++i)
        {
            auto* b = buttons.add (new PopupSafeTextButton (fr (labels[i])));
            hostmenu::tag (*b, id);
            b->setClickingTogglesState (false);
            b->setColour (juce::TextButton::buttonOnColourId, accent);
            b->onClick = [this, i] { if (attachment) attachment->setValueAsCompleteGesture ((float) i); };
            addAndMakeVisible (b);
        }

        attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v)
        {
            for (int i = 0; i < buttons.size(); ++i)
                buttons[i]->setToggleState (i == juce::roundToInt (v), juce::dontSendNotification);
        });
        attachment->sendInitialUpdate();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        const int gap = 6;
        const int w = (r.getWidth() - gap * (buttons.size() - 1)) / juce::jmax (1, buttons.size());
        for (auto* b : buttons) { b->setBounds (r.removeFromLeft (w)); r.removeFromLeft (gap); }
    }

private:
    juce::OwnedArray<juce::TextButton> buttons;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

//==============================================================================
// Zone glisser-déposer + forme d'onde + têtes de lecture
class WaveformView : public juce::Component,
                     public juce::FileDragAndDropTarget
{
public:
    explicit WaveformView (VariantProcessor& p);

    void setSound (LayerSound::Ptr s);
    void layerChanged() { peaksWidth = 0; repaint(); }
    void openChooser();
    void tick();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    int loopMarkerAt (float x) const;
    int loopDrag = -1;
    // enveloppe sur la forme d'onde
    float sampleSecs() const;
    std::array<juce::Point<float>, 4> envPoints() const;
    int envPointAt (juce::Point<float> p) const;
    void setEnvFromDrag (int k, juce::Point<float> p);
    int envDrag = -1, envHover = -1;
    float lastLoopSig = -1.0f;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true;  repaint(); }
    void fileDragExit (const juce::StringArray&) override             { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void computePeaks (int width);

    VariantProcessor& proc;
    LayerSound::Ptr sound;
    std::vector<float> peaks;
    int   peaksWidth = 0;
    float peakScale  = 1.0f;
    bool  dragOver   = false;
    std::array<float, VariantProcessor::numVoices> lastPos {};
    int shownLayer = -1;
    std::unique_ptr<juce::FileChooser> chooser;
};

//==============================================================================
// Clavier qui signale la note racine
class RootKeyboard : public juce::MidiKeyboardComponent
{
public:
    RootKeyboard (juce::MidiKeyboardState& s, std::function<int()> rootFn)
        : juce::MidiKeyboardComponent (s, horizontalKeyboard), getRoot (std::move (rootFn)) {}

protected:
    void drawWhiteNote (int n, juce::Graphics& g, juce::Rectangle<float> area, bool down, bool over,
                        juce::Colour lineCol, juce::Colour textCol) override
    {
        juce::MidiKeyboardComponent::drawWhiteNote (n, g, area, down, over, lineCol, textCol);
        if (n == getRoot())
        {
            g.setColour (Palette::peach.darker (0.15f));
            g.fillRoundedRectangle (area.reduced (4.0f, 0.0f).removeFromBottom (18.0f).removeFromTop (5.0f), 2.5f);
        }
    }

    void drawBlackNote (int n, juce::Graphics& g, juce::Rectangle<float> area, bool down, bool over, juce::Colour fill) override
    {
        juce::MidiKeyboardComponent::drawBlackNote (n, g, area, down, over, fill);
        if (n == getRoot())
        {
            g.setColour (Palette::peach);
            g.fillRoundedRectangle (area.reduced (3.0f, 0.0f).removeFromBottom (10.0f).removeFromTop (4.0f), 2.0f);
        }
    }

private:
    std::function<int()> getRoot;
};

//==============================================================================
// Pastilles : une par variante, la dernière jouée est allumée
class VariantDots : public juce::Component
{
public:
    void set (int count, int active, bool enabled)
    {
        if (count != n || active != act || enabled != en) { n = count; act = active; en = enabled; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        if (n <= 0) return;
        const float d = 9.0f, gap = 5.0f;
        const float total = n * d + (n - 1) * gap;
        float x = (getWidth() - total) * 0.5f;
        const float y = getHeight() * 0.5f - d * 0.5f;
        for (int i = 0; i < n; ++i, x += d + gap)
        {
            const bool on = en && i == act;
            g.setColour (on ? Palette::peach : (en ? Palette::line.brighter (0.3f) : Palette::line));
            g.fillEllipse (x, y, d, d);
        }
    }

private:
    int n = 0, act = -1;
    bool en = true;
};

//==============================================================================
// Séquenceur 16 pas de l'arpège (cliquer/glisser = niveau, double-clic = on/off)
class StepSequencer : public juce::Component
{
public:
    explicit StepSequencer (VariantProcessor& p) : proc (p) {}

    void paint (juce::Graphics& g) override
    {
        const int numSteps = (int) proc.apvts.getRawParameterValue ("arpSteps")->load();
        const int cur = proc.arpeggiator.currentStep.load();
        const bool arpOn = proc.apvts.getRawParameterValue ("arpOn")->load() > 0.5f;

        for (int i = 0; i < 16; ++i)
        {
            auto cell = cellBounds (i);
            const float level = proc.apvts.getRawParameterValue ("step" + juce::String (i + 1))->load();
            const bool active = i < numSteps;

            g.setColour (Palette::panel2.withAlpha (active ? 1.0f : 0.4f));
            g.fillRoundedRectangle (cell, 5.0f);

            if (level > 0.0f)
            {
                auto bar = cell.reduced (3.0f);
                bar = bar.removeFromBottom (bar.getHeight() * level);
                g.setColour ((i % 4 == 0 ? Palette::pink : Palette::lavender).withAlpha (active ? 0.9f : 0.25f));
                g.fillRoundedRectangle (bar, 3.0f);
            }

            if (arpOn && i == cur)
            {
                g.setColour (Palette::text);
                g.drawRoundedRectangle (cell.reduced (0.5f), 5.0f, 1.5f);
            }

            g.setColour (Palette::dim.withAlpha (active ? 0.8f : 0.3f));
            g.setFont (uiFont (10.0f));
            g.drawText (juce::String (i + 1), cell.withY (cell.getBottom() + 2.0f).withHeight (12.0f), juce::Justification::centred);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override { edit (e); }
    void mouseDrag (const juce::MouseEvent& e) override { edit (e); }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int i = indexAt (e.position.x);
        if (i < 0) return;
        const auto id = "step" + juce::String (i + 1);
        const float v = proc.apvts.getRawParameterValue (id)->load();
        proc.setParam (id, v > 0.0f ? 0.0f : 1.0f);
        repaint();
    }

private:
    juce::Rectangle<float> cellBounds (int i) const
    {
        const float gap = 5.0f;
        const float w = (getWidth() - gap * 15.0f) / 16.0f;
        return { i * (w + gap), 0.0f, w, (float) getHeight() - 16.0f };
    }

    int indexAt (float x) const
    {
        for (int i = 0; i < 16; ++i)
        {
            auto c = cellBounds (i);
            if (x >= c.getX() - 2.5f && x < c.getRight() + 2.5f) return i;
        }
        return -1;
    }

    void edit (const juce::MouseEvent& e)
    {
        const int i = indexAt (e.position.x);
        if (i < 0) return;
        const auto c = cellBounds (i).reduced (3.0f);
        const float level = juce::jlimit (0.0f, 1.0f, (c.getBottom() - e.position.y) / c.getHeight());
        proc.setParam ("step" + juce::String (i + 1), level < 0.06f ? 0.0f : level);
        repaint();
    }

    VariantProcessor& proc;
};

//==============================================================================
// Pastille qu'on glisse directement dans FL Studio (fichier .mid)
class DragChip : public juce::Component,
                 public juce::SettableTooltipClient
{
public:
    DragChip (const juce::String& t, juce::Colour c, std::function<juce::File()> make)
        : text (fr (t)), colour (c), makeFile (std::move (make))
    {
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        setTooltip (fr ("Glisse-moi dans FL Studio (playlist, piano roll ou channel rack)"));
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (isMouseOver() ? colour.withAlpha (0.28f) : colour.withAlpha (0.16f));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (colour.withAlpha (0.8f));
        g.drawRoundedRectangle (r, 8.0f, 1.0f);

        // icône "poignée"
        g.setColour (colour);
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 2; ++col)
                g.fillEllipse (12.0f + col * 5.0f, r.getCentreY() - 6.0f + row * 5.0f, 2.5f, 2.5f);

        g.setColour (Palette::text);
        g.setFont (uiFont (14.0f));
        g.drawText (text, r.withTrimmedLeft (28.0f), juce::Justification::centredLeft);
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging || e.getDistanceFromDragStart() < 6) return;
        const auto f = makeFile();
        if (! f.existsAsFile()) return;
        dragging = true;
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this,
                                                                   [this] { dragging = false; });
    }

    void mouseUp (const juce::MouseEvent&) override { dragging = false; }

private:
    juce::String text;
    juce::Colour colour;
    std::function<juce::File()> makeFile;
    bool dragging = false;
};

// Ouvrir n'importe quel panneau dans sa propre fenêtre (branché par l'éditeur)
namespace detach
{
    using Maker = std::function<std::unique_ptr<juce::Component>()>;
    inline std::function<void (Maker, const juce::String&, int, int)>& open()
    {
        static std::function<void (Maker, const juce::String&, int, int)> f;
        return f;
    }
    inline juce::TextButton* makeButton (juce::TextButton& b, std::function<void()> onClick)
    {
        b.setButtonText (juce::String::fromUTF8 ("⧉"));
        b.setTooltip (fr ("Ouvrir dans une fenêtre à part, à placer où tu veux"));
        b.onClick = std::move (onClick);
        return &b;
    }
}

//==============================================================================
// Bouton métronome de l'en-tête : clic = marche / arrêt, clic droit = son et volume
class MetroButton : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit MetroButton (VariantProcessor& p) : proc (p)
    {
        setTooltip (fr ("Métronome : clic = marche / arrêt. Il joue quand tu écoutes tes accords ou ton motif dans Variant, et pendant un enregistrement (pas pendant la lecture de FL). Clic droit = son et volume."));
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        startTimerHz (8);
    }

    void paint (juce::Graphics& g) override
    {
        const bool on = proc.getParam ("metroOn") > 0.5f;
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (on ? Palette::peach.withAlpha (0.85f) : Palette::panel2);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (on ? Palette::bg : Palette::text.withAlpha (0.85f));
        // petit métronome : trapèze + aiguille
        const auto c = r.getCentre();
        juce::Path body;
        body.startNewSubPath (c.x - 6.0f, c.y + 8.0f); body.lineTo (c.x - 3.0f, c.y - 8.0f);
        body.lineTo (c.x + 3.0f, c.y - 8.0f); body.lineTo (c.x + 6.0f, c.y + 8.0f); body.closeSubPath();
        g.strokePath (body, juce::PathStrokeType (1.6f));
        const float sw = on ? std::sin ((float) juce::Time::getMillisecondCounter() * 0.006f) * 5.0f : 3.0f;
        g.drawLine (c.x, c.y + 5.0f, c.x + sw, c.y - 7.0f, 1.6f);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu()) { proc.setParam ("metroOn", proc.getParam ("metroOn") > 0.5f ? 0.0f : 1.0f); repaint(); return; }
        juce::PopupMenu m, sounds, vol;
        const int cur = (int) proc.getParam ("metroSound");
        const char* names[] = { "Clic", "Bois", "Cloche", "Charleston", "Bip 8 bits", "Rimshot" };
        for (int i = 0; i < 6; ++i) sounds.addItem (1 + i, fr (names[i]), true, cur == i);
        const float v = proc.getParam ("metroVol");
        const float vols[] = { -24.0f, -16.0f, -8.0f, -3.0f, 0.0f };
        for (int i = 0; i < 5; ++i) vol.addItem (20 + i, juce::String (vols[i], 0) + " dB", true, std::abs (v - vols[i]) < 0.6f);
        m.addItem (100, fr ("Métronome actif"), true, proc.getParam ("metroOn") > 0.5f);
        m.addSubMenu (fr ("Son"), sounds);
        m.addSubMenu (fr ("Volume"), vol);
        juce::Component::SafePointer<MetroButton> self (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe = self, vols] (int r)
        {
            if (safe == nullptr || r == 0) return;
            auto& pr = safe->proc;
            if (r == 100) pr.setParam ("metroOn", pr.getParam ("metroOn") > 0.5f ? 0.0f : 1.0f);
            else if (r >= 20) pr.setParam ("metroVol", vols[r - 20]);
            else { pr.setParam ("metroSound", (float) (r - 1)); pr.setParam ("metroOn", 1.0f); }
            safe->repaint();
        });
    }

private:
    void timerCallback() override { if (proc.getParam ("metroOn") > 0.5f || lastOn) repaint(); lastOn = proc.getParam ("metroOn") > 0.5f; }
    VariantProcessor& proc;
    bool lastOn = false;
};

//==============================================================================
// Menu clic droit d'une couche (pastille ou forme d'onde) : remplacer, retirer, copier, annuler…
namespace layermenu
{
    inline std::function<void (int)>& openLibrary() { static std::function<void (int)> f; return f; }   // branché par l'éditeur
    inline std::unique_ptr<juce::FileChooser>& chooser() { static std::unique_ptr<juce::FileChooser> c; return c; }

    inline void show (VariantProcessor& proc, int layer, std::function<void()> changed)
    {
        const bool has = proc.getLayerFile (layer) != juce::File();
        const bool on = proc.getParam (VariantProcessor::layerId ("L", layer) + "On") > 0.5f;
        juce::PopupMenu m, copy;
        m.addSectionHeader (fr ("Couche ") + juce::String (layer + 1) + (has ? "  ·  " + proc.getLayerFile (layer).getFileNameWithoutExtension() : juce::String()));
        m.addItem (1, has ? fr ("Remplacer par un fichier…") : fr ("Charger un fichier…"));
        m.addItem (2, fr ("Choisir dans la bibliothèque…"));
        m.addItem (3, fr ("Retirer le son"), has);
        for (int l = 0; l < maxLayers; ++l) if (l != layer) copy.addItem (10 + l, fr ("Vers la couche ") + juce::String (l + 1));
        m.addSubMenu (fr ("Copier ce son"), copy, has);
        m.addItem (4, on ? fr ("Couper la couche") : fr ("Activer la couche"), has);
        m.addItem (5, fr ("Réglages auto de cette couche (note, volume)"), has);
        m.addSeparator();
        m.addItem (6, fr ("Annuler la dernière action  (Ctrl+Z)"));
        m.addItem (7, fr ("Rétablir  (Ctrl+Y)"));
        auto* pp = &proc;
        m.showMenuAsync (juce::PopupMenu::Options(), [pp, layer, changed] (int r)
        {
            auto& p = *pp;
            switch (r)
            {
                case 1:
                    chooser() = std::make_unique<juce::FileChooser> (fr ("Choisir un son"), juce::File(), VariantProcessor::getSupportedWildcard());
                    chooser()->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                            [pp, layer, changed] (const juce::FileChooser& fc)
                                            { if (fc.getResult().existsAsFile() && pp->loadSample (fc.getResult(), layer)) if (changed) changed(); });
                    return;
                case 2: if (openLibrary()) openLibrary() (layer); return;
                case 3: p.clearLayer (layer); break;
                case 4: p.setParam (VariantProcessor::layerId ("L", layer) + "On", p.getParam (VariantProcessor::layerId ("L", layer) + "On") > 0.5f ? 0.0f : 1.0f); break;
                case 5: p.autoSetupLayer (layer); break;
                case 6: p.undoAny(); break;
                case 7: p.redoAny(); break;
                default:
                    if (r >= 10 && r < 10 + maxLayers)
                    {
                        const auto f = p.getLayerFile (layer);
                        if (f.hasFileExtension ("sf2")) p.loadSoundFontPreset (f, p.sf2Preset[(size_t) layer], r - 10);
                        else p.loadSample (f, r - 10);
                    }
                    else return;
            }
            if (changed) changed();
        });
    }
}
