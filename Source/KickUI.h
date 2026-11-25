#pragma once
// Kick : répond aux questions sur le plugin (base de connaissances locale, aucune connexion)
// + visite guidée pour les débutants
#include "Buddy.h"
#include "KickBrain.h"


//==============================================================================
// Fenêtre de discussion avec Kick
class KickChat : public juce::Component
{
public:
    KickChat()
    {
        addAndMakeVisible (input);
        input.setTextToShowWhenEmpty (i18n::kickLang ("Pose ta question… (ex : comment exporter en MIDI ?)"), Palette::dim);
        input.setFont (uiFont (13.5f));
        input.setColour (juce::TextEditor::backgroundColourId, Palette::panel2);
        input.setColour (juce::TextEditor::textColourId, Palette::text);
        input.setColour (juce::TextEditor::outlineColourId, Palette::line);
        input.setColour (juce::TextEditor::focusedOutlineColourId, Palette::pink);
        input.onReturnKey = [this] { ask (input.getText()); };
        addAndMakeVisible (send);
        send.setColour (juce::TextButton::buttonColourId, Palette::pink.withAlpha (0.35f));
        send.onClick = [this] { ask (input.getText()); };
        addAndMakeVisible (close);
        close.onClick = [this] { setVisible (false); if (onClosed) onClosed(); };
        addAndMakeVisible (showMe);
        showMe.setColour (juce::TextButton::buttonColourId, Palette::mint.withAlpha (0.3f));
        showMe.onClick = [this] { if (pendingTab >= 0 && onShowTab) onShowTab (pendingTab); };
        static const char* sugg[] = { "Comment exporter en MIDI ?", "Comment découper un sample ?", "Une astuce" };
        for (int i = 0; i < 3; ++i)
        {
            auto* b = chips.add (new juce::TextButton (i18n::kickLang (sugg[i])));
            b->onClick = [this, i] { if (i == 2) { if (onTip) onTip(); } else ask (i18n::kickLang (sugg[i])); };
            addAndMakeVisible (b);
        }
        reset();
    }

    std::function<void (int)> onShowTab;
    std::function<void()> onTour, onTip, onClosed;

    void reset()
    {
        heading = i18n::kickLang ("Demande à Kick");
        answer = i18n::kickLang ("Vas-y demande ce que tu veux sur le plugin\nJe réponds direct même sans internet");
        pendingTab = -1;
        showMe.setVisible (false);
        repaint();
    }

    void say (const juce::String& title, const juce::String& text)
    {
        heading = title;
        answer = i18n::current().load() == i18n::FR ? kick::potes (text, juce::Random::getSystemRandom(), false) : text;
        pendingTab = -1; showMe.setVisible (false); repaint();
    }

    // texte affiché tel quel (messages du créateur)
    void sayExact (const juce::String& title, const juce::String& text) { heading = title; answer = text; pendingTab = -1; showMe.setVisible (false); repaint(); }
    void focusInput() { input.grabKeyboardFocus(); }
    const juce::String& getHeading() const { return heading; }
    const juce::String& getAnswer() const { return answer; }

    void ask (const juce::String& q)
    {
        if (q.trim().isEmpty()) return;
        input.clear();
        auto& rng = juce::Random::getSystemRandom();
        const auto nq = " " + kick::normalise (q) + " ";
        if (nq.contains (" visite ") || nq.contains (" tour guide") || nq.trim() == "tour")
        {
            if (onTour) onTour();
            return;
        }
        // question courte de suite ("et pour le volume ?") : on favorise le sujet d'avant
        const auto qWords = kick::questionWords (q);
        const bool followUp = lastTab > -2 && qWords.size() <= 2
                              && (nq.startsWith (" et ") || nq.contains (" aussi ") || nq.contains (" ca ") || nq.contains (" celui ") || nq.contains (" celle "));
        const int tabNow = getTab ? getTab() : -2;
        const auto m = kick::find (q, followUp ? lastTab : -2, tabNow);

        // on cherche aussi dans les explications de tous les boutons du plugin
        float uiScore = 0.0f;
        UiHint bestUi;
        if (gatherUi)
        {
            std::vector<UiHint> hints;
            gatherUi (hints);
            for (auto& h : hints)
            {
                float s = kick::scoreHint (q, h.title, h.text);
                if (tabNow >= 0 && h.tab == tabNow) s += 0.5f;
                if (s > uiScore) { uiScore = s; bestUi = h; }
            }
        }

        static const char* intros[] = { "Wsh écoute", "Bon ok", "Facile frérot", "Attends je t'explique", "Trust", "T'es sérieux là ? Bon",
                                        "Ah celle-là", "Écoute bien je répète pas", "Question de débutant mais ok", "Vas-y je t'explique" };
        static const char* tabNames[] = { "Son", "Effets", "Accords & MIDI", "Arpège", "Sampler", "Synthé" };
        const float th = kick::threshold();
        const bool frKick = i18n::current().load() == i18n::FR;
        // la base connaît tous les potards : les infobulles ne servent que si elle ne sait pas
        const bool useUi = uiScore >= th && m.score < th && uiScore > m.score * 1.25f;
        if (! useUi && m.index >= 0 && m.score >= th)
        {
            const auto& f = kick::faq()[(size_t) m.index];
            heading = i18n::kickLang (intros[rng.nextInt (10)]);
            answer = kick::answerOf (f, frKick);
            // il hésite entre deux sujets : il propose aussi l'autre
            if (m.second >= 0 && m.secondScore >= m.score * 0.9f && kick::faq()[(size_t) m.second].answer != f.answer
                && kick::faq()[(size_t) m.second].tab != f.tab)
            {
                const auto alt = kick::answerOf (kick::faq()[(size_t) m.second], frKick);
                answer << "\n\n" << i18n::kickLang ("Ou alors tu parlais de ça : ") << alt.upToFirstOccurrenceOf (".", true, false);
            }
            lastTab = f.tab;
            pendingTab = f.tab;
        }
        else if (useUi)
        {
            heading = bestUi.title.isNotEmpty() ? bestUi.title : fr (intros[rng.nextInt (10)]);
            answer = bestUi.text.upToFirstOccurrenceOf ("\n" + fr ("Double-clic"), false, false);
            if (juce::isPositiveAndBelow (bestUi.tab, 6)) answer << "\n" << fr ("C'est dans l'onglet ") << fr (tabNames[bestUi.tab]) << ".";
            lastTab = bestUi.tab;
            pendingTab = bestUi.tab;
        }
        else if (m.index >= 0 && m.score >= kick::guessThreshold())
        {
            // pas sûr : il propose sa meilleure idée
            const auto& f = kick::faq()[(size_t) m.index];
            heading = i18n::kickLang ("Je crois que tu parles de ça mais t'es pas clair");
            answer = kick::answerOf (f, frKick) + "\n\n" + i18n::kickLang ("Si c'est pas ça reformule avec le nom du potard ou de la page");
            lastTab = f.tab;
            pendingTab = f.tab;
        }
        else
        {
            static const char* lost[] = {
                "J'ai rien capté frérot\nJe suis un skateur pas Google\nEssaie avec le nom d'un potard ou d'une page genre export slicer arpège accords réverb attaque slide",
                "Hein ? Reformule\nGenre « comment exporter en MIDI » ou « à quoi sert le Ratchet »\nOu tape « visite » et je te fais le tour",
                "Là tu m'as perdu mon reuf\nDis-moi ce que tu veux faire (« découper un sample » « faire une 808 ») ou le nom du bouton\nJe fais de mon mieux promis" };
            heading = i18n::kickLang ("Wsh ?");
            answer = i18n::kickLang (lost[rng.nextInt (3)]);
            pendingTab = -1;
        }
        if (i18n::current().load() == i18n::FR) answer = kick::potes (answer, rng, true);   // Kick parle comme un pote
        showMe.setVisible (pendingTab >= 0);
        resized();
        repaint();
    }

    struct UiHint { juce::String title, text; int tab = -1; };
    std::function<int()> getTab;   // page affichée (pour départager les potards du même nom)
    std::function<void (std::vector<UiHint>&)> gatherUi;
    int lastTab = -2;   // sujet de la question précédente (pour les questions de suite)

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (1.5f);
        b.removeFromBottom (10.0f);
        juce::Path p;
        p.addRoundedRectangle (b, 16.0f);
        juce::Path tail;
        tail.addTriangle (b.getRight() - 46.0f, b.getBottom() - 1.0f, b.getRight() - 22.0f, b.getBottom() - 1.0f, b.getRight() - 18.0f, b.getBottom() + 10.0f);
        p.addPath (tail);
        g.setColour (juce::Colours::black.withAlpha (Palette::light ? 0.12f : 0.4f));
        g.fillPath (p, juce::AffineTransform::translation (0.0f, 4.0f));
        g.setColour (Palette::panel);
        g.fillPath (p);
        g.setColour (Palette::pink);
        g.strokePath (p, juce::PathStrokeType (2.0f));

        g.setColour (Palette::pink);
        g.setFont (uiFont (14.5f, juce::Font::bold));
        g.drawText (heading, titleArea, juce::Justification::centredLeft, true);

        juce::AttributedString as;
        as.append (answer, uiFont (13.5f), Palette::text);
        as.setWordWrap (juce::AttributedString::byWord);
        as.setLineSpacing (2.0f);
        juce::TextLayout tl;
        tl.createLayout (as, (float) answerArea.getWidth());
        tl.draw (g, answerArea.toFloat());
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16, 12).withTrimmedBottom (10);
        auto top = r.removeFromTop (24);
        close.setBounds (top.removeFromRight (24));
        titleArea = top;
        r.removeFromTop (4);
        auto bottom = r.removeFromBottom (32);
        send.setBounds (bottom.removeFromRight (80));
        bottom.removeFromRight (6);
        input.setBounds (bottom);
        r.removeFromBottom (8);
        auto chipRow = r.removeFromBottom (28);
        const int cw = (chipRow.getWidth() - 12) / 3;
        for (auto* c : chips) { c->setBounds (chipRow.removeFromLeft (cw)); chipRow.removeFromLeft (6); }
        r.removeFromBottom (6);
        if (showMe.isVisible()) { showMe.setBounds (r.removeFromBottom (28).removeFromLeft (150)); r.removeFromBottom (6); }
        answerArea = r;
    }

private:
    juce::TextEditor input;
    juce::TextButton send { fr ("Envoyer") }, close { juce::String::fromUTF8 ("✕") }, showMe { fr ("Montre-moi") };
    juce::OwnedArray<juce::TextButton> chips;
    juce::String heading, answer;
    juce::Rectangle<int> titleArea, answerArea;
    int pendingTab = -1;
};

//==============================================================================
// Visite guidée : on éclaire une zone, Kick explique, Suivant / Passer
class TourOverlay : public juce::Component, private juce::Timer
{
public:
    struct Step { int tab; std::function<juce::Rectangle<int>()> target; juce::String title, text; };

    TourOverlay()
    {
        addAndMakeVisible (next);
        addAndMakeVisible (skip);
        next.setColour (juce::TextButton::buttonColourId, Palette::pink.withAlpha (0.4f));
        next.onClick = [this] { go (index + 1); };
        skip.onClick = [this] { finish(); };
        setVisible (false);
    }

    std::function<void (int)> onTab;
    std::function<void()> onFinished;

    void goTo (int i) { go (i); }   // outil de dev (captures de la visite)
    int numSteps() const { return (int) steps.size(); }
    void start (std::vector<Step> s)
    {
        steps = std::move (s);
        setVisible (true);
        toFront (false);
        startTimerHz (30);
        go (0);
    }

    bool isRunning() const { return isVisible(); }

    void paint (juce::Graphics& g) override
    {
        juce::Path dim;
        dim.addRectangle (getLocalBounds().toFloat());
        if (! spot.isEmpty())
        {
            dim.addRoundedRectangle (spot.toFloat().expanded (6.0f), 12.0f);
            dim.setUsingNonZeroWinding (false);
        }
        g.setColour (juce::Colours::black.withAlpha (0.62f));
        g.fillPath (dim);
        if (! spot.isEmpty())
        {
            const float pulse = 0.5f + 0.5f * std::sin (t * 5.0f);
            g.setColour (Palette::pink.withAlpha (0.5f + 0.5f * pulse));
            g.drawRoundedRectangle (spot.toFloat().expanded (6.0f + pulse * 2.0f), 12.0f, 2.5f);
        }

        // carte d'explication
        auto c = card.toFloat();
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (c, 16.0f);
        g.setColour (Palette::pink);
        g.drawRoundedRectangle (c, 16.0f, 2.0f);
        auto inner = card.reduced (18, 14);
        g.setColour (Palette::dim);
        g.setFont (uiFont (12.0f));
        g.drawText (juce::String (index + 1) + " / " + juce::String ((int) steps.size()), inner.removeFromTop (16), juce::Justification::centredRight);
        if (juce::isPositiveAndBelow (index, (int) steps.size()))
        {
            const auto& st = steps[(size_t) index];
            juce::AttributedString as;
            as.append (st.title + "\n", uiFont (17.0f, juce::Font::bold), Palette::pink);
            as.append (st.text, uiFont (14.0f), Palette::text);
            as.setWordWrap (juce::AttributedString::byWord);
            as.setLineSpacing (3.0f);
            juce::TextLayout tl;
            tl.createLayout (as, (float) inner.getWidth());
            tl.draw (g, inner.withTrimmedBottom (40).toFloat());
        }
    }

    void resized() override { place(); }
    void mouseUp (const juce::MouseEvent&) override {}   // bloque les clics pendant la visite

private:
    void go (int i)
    {
        if (i >= (int) steps.size()) { finish(); return; }
        index = i;
        const auto& st = steps[(size_t) i];
        if (st.tab >= 0 && onTab) onTab (st.tab);
        next.setButtonText (i == (int) steps.size() - 1 ? fr ("C'est parti !") : fr ("Suivant"));
        place();
        repaint();
    }

    void place()
    {
        if (! juce::isPositiveAndBelow (index, (int) steps.size())) return;
        spot = steps[(size_t) index].target ? steps[(size_t) index].target() : juce::Rectangle<int>();
        const int w = 420, h = 190;
        auto area = getLocalBounds().reduced (20);
        juce::Rectangle<int> c;
        if (spot.isEmpty()) c = area.withSizeKeepingCentre (w, h);
        else if (spot.getBottom() + h + 20 < area.getBottom()) c = { juce::jlimit (area.getX(), area.getRight() - w, spot.getCentreX() - w / 2), spot.getBottom() + 18, w, h };
        else if (spot.getY() - h - 20 > area.getY())          c = { juce::jlimit (area.getX(), area.getRight() - w, spot.getCentreX() - w / 2), spot.getY() - h - 18, w, h };
        else c = area.withSizeKeepingCentre (w, h);
        card = c;
        auto b = card.reduced (18, 14).removeFromBottom (32);
        next.setBounds (b.removeFromRight (140));
        b.removeFromRight (8);
        skip.setBounds (b.removeFromRight (100));
    }

    void finish()
    {
        stopTimer();
        setVisible (false);
        if (onFinished) onFinished();
    }

    void timerCallback() override { t += 1.0f / 30.0f; repaint (spot.expanded (14)); }

    std::vector<Step> steps;
    int index = 0;
    float t = 0.0f;
    juce::Rectangle<int> spot, card;
    juce::TextButton next { fr ("Suivant") }, skip { fr ("Passer") };
};
