#pragma once
// Page SOUND DESIGN : synthé à tables d'ondes façon Serum
#include "Pages.h"   // FxVisual (colonne d'effets)
#include "SynthPresets.h"

namespace synthui
{
    // destination de modulation d'un réglage (pour « Moduler avec… » au clic droit)
    inline int dstFor (int param)
    {
        using namespace wt;
        switch (param)
        {
            case A_Pos: return D_APos; case A_Level: return D_ALevel; case A_Semi: return D_APitch; case A_Detune: return D_ADetune; case A_Pan: return D_APan; case A_Warp: return D_AWarp;
            case B_Pos: return D_BPos; case B_Level: return D_BLevel; case B_Semi: return D_BPitch; case B_Detune: return D_BDetune; case B_Pan: return D_BPan; case B_Warp: return D_BWarp;
            case Sub_Level: return D_SubLevel; case Noise_Level: return D_NoiseLevel;
            case F_Cutoff: return D_Cutoff; case F_Res: return D_Res; case F_Drive: return D_Drive; case F_Mix: return D_FMix;
            case G_Volume: return D_Volume; case L1_Rate: return D_L1Rate; case L2_Rate: return D_L2Rate;
            default: return D_None;
        }
    }

    inline juce::String valueText (int param, float v)
    {
        using namespace wt;
        switch (param)
        {
            case A_Oct: case B_Oct: case Sub_Oct: return juce::String ((int) v) + " oct";
            case A_Semi: case B_Semi: return juce::String ((int) v) + " st";
            case A_Fine: case B_Fine: return juce::String ((int) v) + " ct";
            case A_Uni: case B_Uni: return juce::String ((int) v) + " voix";
            case G_Voices: return juce::String ((int) v) + " voix";
            case G_Bend: return juce::String::fromUTF8 ("±") + juce::String ((int) v) + " st";
            case F_Cutoff: { const float hz = cutoffHz (v); return hz >= 1000.0f ? juce::String (hz / 1000.0f, 1) + " kHz" : juce::String ((int) hz) + " Hz"; }
            case A_Pan: case B_Pan: case Sub_Pan: case Noise_Pan: case A_Warp: case B_Warp: return juce::String ((int) std::round (v * 100));
            case G_Glide: return v < 0.001f ? juce::String ("off") : juce::String ((int) std::round (v * 100)) + " %";
            case E1_A: case E1_D: case E1_R: case E2_A: case E2_D: case E2_R: case E3_A: case E3_D: case E3_R:
                return v < 1.0f ? juce::String ((int) std::round (v * 1000)) + " ms" : juce::String (v, 2) + " s";
            default: return juce::String ((int) std::round (v * 100)) + " %";
        }
    }

    //==========================================================================
    // Bouton rotatif relié à un paramètre du synthé (clic droit : moduler avec…, réinitialiser, taper une valeur)
    class SKnob : public juce::Component
    {
    public:
        SKnob (wt::Engine& e, int param, juce::StringRef name, juce::Colour c, juce::StringRef tip = {})   // StringRef : les textes accentués (UTF-8) restent justes
            : eng (e), index (param), title (fr (juce::String (name))), colour (c)
        {
            const auto& in = wt::info()[(size_t) param];
            slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
            slider.setColour (juce::Slider::rotarySliderFillColourId, c);
            const bool stepped = isStepped (param);
            slider.setRange (in.lo, in.hi, stepped ? 1.0 : 0.0);
            if (isTime (param)) slider.setSkewFactorFromMidPoint (in.hi > 5.0f ? 0.6 : 0.3);
            slider.setDoubleClickReturnValue (true, in.def);
            slider.textFromValueFunction = [param] (double v) { return valueText (param, (float) v); };
            slider.setValue (eng.patch.get (param), juce::dontSendNotification);
            slider.setTooltip (fr (tip.isNotEmpty() ? juce::String (tip) : juce::String (name)) + "\n" + fr ("Double-clic : par défaut · clic droit : moduler avec un LFO / une enveloppe…"));
            slider.onValueChange = [this] { eng.patch.set (index, (float) slider.getValue()); label.setText (valueText (index, (float) slider.getValue()), juce::dontSendNotification); if (onChange) onChange(); };
            slider.onDragEnd = [this] { label.setText (title, juce::dontSendNotification); };
            slider.onRightClick = [this] { showMenu(); };
            addAndMakeVisible (slider);
            label.setText (title, juce::dontSendNotification);
            label.setJustificationType (juce::Justification::centred);
            label.setFont (uiFont (11.5f));
            label.setMinimumHorizontalScale (0.6f);
            label.setColour (juce::Label::textColourId, Palette::text.withAlpha (0.8f));
            label.setInterceptsMouseClicks (false, false);
            addAndMakeVisible (label);
        }
        std::function<void()> onChange;

        void refresh()
        {
            const float v = eng.patch.get (index);
            if (std::abs ((float) slider.getValue() - v) > 1.0e-5f && ! slider.isMouseButtonDown()) slider.setValue (v, juce::dontSendNotification);
            const float m = modDepth();
            if (std::abs (m - shownMod) > 1.0e-4f) { shownMod = m; repaint(); }
        }

        void resized() override
        {
            auto r = getLocalBounds();
            label.setBounds (r.removeFromBottom (15));
            slider.setBounds (r);
        }

        void paintOverChildren (juce::Graphics& g) override
        {
            if (std::abs (shownMod) < 1.0e-4f) return;
            // anneau de modulation (comme Serum) : jusqu'où le réglage bouge
            auto b = slider.getBounds().toFloat();
            const float d = juce::jmin (b.getWidth(), b.getHeight()) - 2.0f;
            const auto c = b.getCentre();
            const auto& in = wt::info()[(size_t) index];
            const float v = (eng.patch.get (index) - in.lo) / (in.hi - in.lo);
            const float a0 = juce::MathConstants<float>::pi * 1.2f, a1 = juce::MathConstants<float>::pi * 2.8f;
            const float from = a0 + (a1 - a0) * v;
            const float to = a0 + (a1 - a0) * juce::jlimit (0.0f, 1.0f, v + shownMod);
            juce::Path arc;
            arc.addCentredArc (c.x, c.y, d * 0.5f + 2.0f, d * 0.5f + 2.0f, 0.0f, from, to, true);
            g.setColour (Palette::lavender.withAlpha (0.9f));
            g.strokePath (arc, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

    private:
        wt::Engine& eng;
        int index;
        juce::String title;
        juce::Colour colour;
        KnobSlider slider;
        juce::Label label;
        float shownMod = 0.0f;

        static bool isStepped (int p) { using namespace wt; return p == A_Oct || p == B_Oct || p == A_Semi || p == B_Semi || p == A_Uni || p == B_Uni || p == G_Voices || p == G_Bend || p == Sub_Oct || p == A_Fine || p == B_Fine; }
        static bool isTime (int p) { using namespace wt; return p == E1_A || p == E1_D || p == E1_R || p == E2_A || p == E2_D || p == E2_R || p == E3_A || p == E3_D || p == E3_R; }

        float modDepth() const
        {
            const int d = dstFor (index);
            if (d == wt::D_None) return 0.0f;
            float s = 0.0f;
            for (int m = 0; m < wt::numMods; ++m)
                if ((int) eng.patch.get (wt::Mod0_Dst + m * 3) == d && (int) eng.patch.get (wt::Mod0_Src + m * 3) > 0) s += eng.patch.get (wt::Mod0_Amt + m * 3);
            return s;
        }

        void showMenu()
        {
            juce::PopupMenu m, srcs;
            const int d = dstFor (index);
            const auto names = wt::srcNames();
            for (int s = 1; s < names.size(); ++s) srcs.addItem (100 + s, names[s]);
            if (d != wt::D_None) { m.addSubMenu (fr ("Moduler avec"), srcs); m.addItem (3, fr ("Enlever les modulations de ce réglage"), modDepth() != 0.0f); m.addSeparator(); }
            m.addItem (1, fr ("Réinitialiser"));
            m.addItem (2, fr ("Taper une valeur…"));
            hostmenu::append (m, slider, "syn_" + juce::String (wt::info()[(size_t) index].id));   // automation dans FL
            juce::Component::SafePointer<SKnob> self (this);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&slider), [self, d] (int r)
            {
                if (self == nullptr || r == 0) return;
                auto& e = self->eng;
                if (r == 1) self->slider.setValue (wt::info()[(size_t) self->index].def, juce::sendNotificationSync);
                else if (r == 2)
                {
                    auto* w = new juce::AlertWindow (self->title, fr ("Nouvelle valeur :"), juce::AlertWindow::NoIcon);
                    w->addTextEditor ("v", juce::String (self->slider.getValue(), 2));
                    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
                    w->addButton (fr ("Annuler"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
                    w->enterModalState (true, juce::ModalCallbackFunction::create ([self, w] (int res)
                    {
                        if (res == 1 && self != nullptr) self->slider.setValue (w->getTextEditorContents ("v").retainCharacters ("0123456789.,-").replaceCharacter (',', '.').getDoubleValue(), juce::sendNotificationSync);
                    }), true);
                }
                else if (r == 3)
                {
                    for (int k = 0; k < wt::numMods; ++k) if ((int) e.patch.get (wt::Mod0_Dst + k * 3) == d) { e.patch.set (wt::Mod0_Src + k * 3, 0); e.patch.set (wt::Mod0_Dst + k * 3, 0); e.patch.set (wt::Mod0_Amt + k * 3, 0); }
                }
                else if (r > 100)
                {
                    // première case libre de la matrice
                    for (int k = 0; k < wt::numMods; ++k)
                        if ((int) e.patch.get (wt::Mod0_Src + k * 3) == 0 || (int) e.patch.get (wt::Mod0_Dst + k * 3) == 0)
                        {
                            e.patch.set (wt::Mod0_Src + k * 3, (float) (r - 100));
                            e.patch.set (wt::Mod0_Dst + k * 3, (float) d);
                            e.patch.set (wt::Mod0_Amt + k * 3, 0.3f);
                            break;
                        }
                }
                self->refresh();
                if (self->onChange) self->onChange();
            });
        }
    };

    // Interrupteur relié à un paramètre (0/1)
    class SToggle : public PopupSafeToggle
    {
    public:
        SToggle (wt::Engine& e, int p, juce::StringRef text, juce::Colour c) : PopupSafeToggle (fr (juce::String (text))), eng (e), index (p)
        {
            hostmenu::tag (*this, "syn_" + juce::String (wt::info()[(size_t) p].id));
            setColour (juce::ToggleButton::tickColourId, c);
            setToggleState (eng.patch.get (p) > 0.5f, juce::dontSendNotification);
            onClick = [this] { eng.patch.set (index, getToggleState() ? 1.0f : 0.0f); if (onChange) onChange(); };
        }
        std::function<void()> onChange;
        void refresh() { const bool v = eng.patch.get (index) > 0.5f; if (v != getToggleState()) setToggleState (v, juce::dontSendNotification); }
    private:
        wt::Engine& eng; int index;
    };

    // Liste déroulante reliée à un paramètre entier
    class SCombo : public PopupSafeCombo
    {
    public:
        SCombo (wt::Engine& e, int p, const juce::StringArray& items) : eng (e), index (p)
        {
            hostmenu::tag (*this, "syn_" + juce::String (wt::info()[(size_t) p].id));
            for (int i = 0; i < items.size(); ++i) addItem (fr (items[i]), i + 1);
            setSelectedItemIndex ((int) eng.patch.get (p), juce::dontSendNotification);
            onChange = [this] { eng.patch.set (index, (float) getSelectedItemIndex()); if (onEdit) onEdit(); };
        }
        std::function<void()> onEdit;
        void refresh() { const int v = (int) eng.patch.get (index); if (v != getSelectedItemIndex()) setSelectedItemIndex (v, juce::dontSendNotification); }
    private:
        wt::Engine& eng; int index;
    };

    //==========================================================================
    // Affichage de la table d'ondes en 3D (cycles empilés), glisser = position, déposer un .wav = nouvelle table
    class WavetableView : public juce::Component, public juce::FileDragAndDropTarget, public juce::SettableTooltipClient
    {
    public:
        WavetableView (wt::Engine& e, int o, juce::Colour c) : eng (e), osc (o), colour (c)
        {
            setTooltip (fr ("Glisse à gauche / à droite pour changer la position dans la table. Dépose un .wav pour en faire une table d'ondes."));
        }
        std::function<void (const juce::File&)> onFile;

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (Palette::bg.withAlpha (0.55f));
            g.fillRoundedRectangle (r, 8.0f);
            auto t = eng.getTable (osc);
            if (t == nullptr) return;
            const float pos = eng.patch.get (osc == 0 ? wt::A_Pos : wt::B_Pos);
            const float live = eng.activeVoices.load() > 0 ? eng.uiPos[osc].load() : pos;
            const int shown = juce::jmin (t->frames, 24);
            auto area = r.reduced (10.0f, 8.0f);
            const float dx = area.getWidth() * 0.18f / juce::jmax (1, shown), dy = area.getHeight() * 0.35f / juce::jmax (1, shown);
            const float w = area.getWidth() * 0.82f, h = area.getHeight() * 0.65f;
            const int cur = (int) std::round (live * (t->frames - 1));
            for (int k = shown - 1; k >= 0; --k)
            {
                const int f = shown == 1 ? 0 : (int) std::round ((float) k / (shown - 1) * (t->frames - 1));
                const float ox = area.getX() + k * dx, oy = area.getY() + area.getHeight() * 0.35f - k * dy;
                juce::Path p;
                const float* d = t->raw (f);
                for (int i = 0; i <= 96; ++i)
                {
                    const float x = ox + w * i / 96.0f;
                    const float y = oy + h * 0.5f - d[(i * wt::N / 96) % wt::N] * h * 0.45f;
                    if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
                }
                g.setColour (colour.withAlpha (0.12f + 0.1f * (1.0f - (float) k / shown)));
                g.strokePath (p, juce::PathStrokeType (1.0f));
            }
            // cycle courant, en clair
            {
                const int k = shown == 1 ? 0 : (int) std::round (live * (shown - 1));
                const float ox = area.getX() + k * dx, oy = area.getY() + area.getHeight() * 0.35f - k * dy;
                juce::Path p, fill;
                const float* d = t->raw (cur);
                for (int i = 0; i <= 200; ++i)
                {
                    const float x = ox + w * i / 200.0f;
                    const float y = oy + h * 0.5f - d[(i * wt::N / 200) % wt::N] * h * 0.45f;
                    if (i == 0) { p.startNewSubPath (x, y); fill.startNewSubPath (x, oy + h * 0.5f); }
                    p.lineTo (x, y); fill.lineTo (x, y);
                }
                fill.lineTo (ox + w, oy + h * 0.5f); fill.closeSubPath();
                g.setColour (colour.withAlpha (0.18f)); g.fillPath (fill);
                g.setColour (colour); g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            g.setColour (Palette::text.withAlpha (0.75f));
            g.setFont (uiFont (11.0f));
            g.drawText (juce::String (cur + 1) + " / " + juce::String (t->frames), r.reduced (8.0f, 4.0f), juce::Justification::bottomRight);
            if (over) { g.setColour (Palette::peach.withAlpha (0.2f)); g.fillRoundedRectangle (r, 8.0f); }
        }
        void mouseDown (const juce::MouseEvent&) override { startPos = eng.patch.get (posParam()); }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            eng.patch.set (posParam(), juce::jlimit (0.0f, 1.0f, startPos + (float) e.getDistanceFromDragStartX() / (float) juce::jmax (1, getWidth()) - (float) e.getDistanceFromDragStartY() / 300.0f));
            repaint();
        }
        bool isInterestedInFileDrag (const juce::StringArray& f) override { return f.size() > 0 && juce::File (f[0]).hasFileExtension ("wav;aif;aiff;flac"); }
        void fileDragEnter (const juce::StringArray&, int, int) override { over = true; repaint(); }
        void fileDragExit (const juce::StringArray&) override { over = false; repaint(); }
        void filesDropped (const juce::StringArray& f, int, int) override { over = false; if (onFile) onFile (juce::File (f[0])); repaint(); }
    private:
        wt::Engine& eng; int osc; juce::Colour colour; float startPos = 0.0f; bool over = false;
        int posParam() const { return osc == 0 ? wt::A_Pos : wt::B_Pos; }
    };

    //==========================================================================
    // Panneau d'oscillateur (A ou B)
    class OscPanel : public juce::Component
    {
    public:
        OscPanel (wt::Engine& e, int o) : eng (e), osc (o), b (o * wt::oscStride),
            on (e, wt::A_On + b, o == 0 ? "OSC A" : "OSC B", accent()), view (e, o, accent()),
            pos (e, wt::A_Pos + b, "Position", accent(), "Position dans la table d'ondes (le timbre change)"),
            uni (e, wt::A_Uni + b, "Unisson", accent(), "Nombre de voix superposées (son plus large et plus gros)"),
            det (e, wt::A_Detune + b, "Detune", accent(), "Écart de hauteur entre les voix d'unisson"),
            blend (e, wt::A_Blend + b, "Blend", accent(), "Volume des voix d'unisson extérieures"),
            width (e, wt::A_Width + b, "Largeur", accent(), "Écartement stéréo des voix d'unisson"),
            oct (e, wt::A_Oct + b, "Octave", accent()), semi (e, wt::A_Semi + b, "Demi-ton", accent()), fine (e, wt::A_Fine + b, "Fin", accent(), "Accord fin en cents"),
            level (e, wt::A_Level + b, "Volume", accent()), pan (e, wt::A_Pan + b, "Pan", accent()),
            warp (e, wt::A_Warp + b, "Warp", accent(), "Bend : tord la forme d'onde (plus brillant ou plus rond)"),
            phase (e, wt::A_Phase + b, "Phase", accent(), "Point de départ de l'onde"), rand (e, wt::A_Rand + b, "Phase aléa.", accent(), "Phase de départ au hasard à chaque note")
        {
            for (auto* c : std::initializer_list<juce::Component*> { &on, &view, &tableBtn, &prev, &next, &pos, &uni, &det, &blend, &width, &oct, &semi, &fine, &level, &pan, &warp, &phase, &rand })
                addAndMakeVisible (c);
            tableBtn.setTooltip (fr ("Choisir une table d'ondes : celles de Serum sur ton PC, les tables de base, ou un fichier"));
            tableBtn.onClick = [this] { showTableMenu(); };
            refresh();
            prev.onClick = [this] { step (-1); };
            next.onClick = [this] { step (1); };
            view.onFile = [this] (const juce::File& f) { loadFile (f); };
        }

        juce::Colour accent() const { return osc == 0 ? Palette::mint : Palette::blue; }

        void refresh()
        {
            on.refresh();
            for (auto* k : knobs()) k->refresh();
            auto t = eng.getTable (osc);
            const auto name = t != nullptr ? fr (t->name) : juce::String ("—");
            if (tableBtn.getButtonText() != name) tableBtn.setButtonText (name);
            view.repaint();
        }

        void paint (juce::Graphics& g) override { drawCard (g, getLocalBounds(), {}, accent()); }
        void resized() override
        {
            auto r = getLocalBounds().reduced (10, 8);
            auto top = r.removeFromTop (24);
            on.setBounds (top.removeFromLeft (92));
            next.setBounds (top.removeFromRight (24)); top.removeFromRight (2);
            prev.setBounds (top.removeFromRight (24)); top.removeFromRight (4);
            tableBtn.setBounds (top);
            r.removeFromTop (4);
            view.setBounds (r.removeFromTop (r.getHeight() - 2 * 58 - 6));
            r.removeFromTop (4);
            auto row1 = r.removeFromTop (58);
            const int w1 = row1.getWidth() / 6;
            for (auto* k : { &pos, &uni, &det, &blend, &width, &level }) k->setBounds (row1.removeFromLeft (w1));
            r.removeFromTop (2);
            auto row2 = r.removeFromTop (58);
            const int w2 = row2.getWidth() / 7;
            for (auto* k : { &oct, &semi, &fine, &pan, &warp, &phase, &rand }) k->setBounds (row2.removeFromLeft (w2));
        }

    private:
        wt::Engine& eng;
        int osc, b;
        SToggle on;
        WavetableView view;
        juce::TextButton tableBtn, prev { "<" }, next { ">" };
        SKnob pos, uni, det, blend, width, oct, semi, fine, level, pan, warp, phase, rand;
        juce::Array<juce::File> fileList;

        std::vector<SKnob*> knobs() { return { &pos, &uni, &det, &blend, &width, &oct, &semi, &fine, &level, &pan, &warp, &phase, &rand }; }

        void loadFile (const juce::File& f)
        {
            if (auto t = wt::loadWav (f)) { eng.setTable (osc, t); refresh(); }
        }

        juce::Array<juce::File> allFiles()
        {
            juce::Array<juce::File> a;
            for (auto& d : wt::tableFolders (library::defaultFolder()))
            {
                auto fs = d.findChildFiles (juce::File::findFiles, true, "*.wav");
                fs.sort();
                a.addArray (fs);
            }
            return a;
        }

        void step (int dir)
        {
            auto t = eng.getTable (osc);
            if (t != nullptr && t->path.startsWith ("builtin:"))
            {
                const int n = wt::builtinNames().size();
                const int i = t->path.fromFirstOccurrenceOf (":", false, false).getIntValue();
                eng.setTable (osc, wt::builtin (((i + dir) % n + n) % n));
                refresh();
                return;
            }
            if (fileList.isEmpty()) fileList = allFiles();
            if (fileList.isEmpty()) return;
            int at = 0;
            for (int i = 0; i < fileList.size(); ++i) if (t != nullptr && fileList[i].getFullPathName() == t->path) at = i;
            loadFile (fileList[((at + dir) % fileList.size() + fileList.size()) % fileList.size()]);
        }

        void showTableMenu()
        {
            juce::PopupMenu m, base;
            const auto bn = wt::builtinNames();
            for (int i = 0; i < bn.size(); ++i) base.addItem (1 + i, fr (bn[i]));
            m.addSubMenu (fr ("Tables de base"), base);
            fileList = allFiles();
            std::map<juce::String, juce::PopupMenu> groups;
            const auto folders = wt::tableFolders (library::defaultFolder());
            for (int i = 0; i < fileList.size(); ++i)
            {
                juce::String group;
                for (auto& d : folders)
                    if (fileList[i].isAChildOf (d))
                    {
                        const auto top = d.getParentDirectory().getFileName().contains ("Serum 2") ? "Serum 2" : (d.getFileName() == "Wavetables" ? "Variant" : "Serum");
                        group = juce::String (top) + " / " + fileList[i].getParentDirectory().getFileName();
                    }
                groups[group].addItem (1000 + i, fileList[i].getFileNameWithoutExtension());
            }
            if (groups.empty()) m.addItem (-1, fr ("(Serum introuvable : dépose tes .wav dans « Variant Bibliothèque/Wavetables »)"), false);
            for (auto& [name, sub] : groups) m.addSubMenu (name, sub);
            m.addSeparator();
            m.addItem (2000, fr ("Charger un fichier .wav…"));
            m.addItem (2001, fr ("Ouvrir le dossier Wavetables de Variant"));
            juce::Component::SafePointer<OscPanel> self (this);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&tableBtn), [self] (int r)
            {
                if (self == nullptr || r <= 0) return;
                if (r < 1000) { self->eng.setTable (self->osc, wt::builtin (r - 1)); self->refresh(); }
                else if (r < 2000) { if (juce::isPositiveAndBelow (r - 1000, self->fileList.size())) self->loadFile (self->fileList[r - 1000]); }
                else if (r == 2001) synthpresets::wavetableFolder().startAsProcess();
                else
                {
                    self->chooser = std::make_unique<juce::FileChooser> (fr ("Choisir une table d'ondes (.wav)"), juce::File(), "*.wav;*.aif;*.aiff;*.flac");
                    self->chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [self] (const juce::FileChooser& fc)
                    { if (self != nullptr && fc.getResult().existsAsFile()) self->loadFile (fc.getResult()); });
                }
            });
        }
        std::unique_ptr<juce::FileChooser> chooser;
    };

    //==========================================================================
    class SubNoisePanel : public juce::Component
    {
    public:
        explicit SubNoisePanel (wt::Engine& e) : eng (e),
            subOn (e, wt::Sub_On, "SUB", Palette::peach), subShape (e, wt::Sub_Shape, { "Sinus", "Triangle", juce::String::fromUTF8 ("Carré") }),
            subOct (e, wt::Sub_Oct, "Octave", Palette::peach), subLevel (e, wt::Sub_Level, "Volume", Palette::peach), subPan (e, wt::Sub_Pan, "Pan", Palette::peach),
            noiseOn (e, wt::Noise_On, "BRUIT", Palette::lavender), noiseType (e, wt::Noise_Type, { "Blanc", "Grave (rose)", juce::String::fromUTF8 ("Vinyle (crépitements)") }),
            noiseLevel (e, wt::Noise_Level, "Volume", Palette::lavender), noisePan (e, wt::Noise_Pan, "Pan", Palette::lavender)
        {
            for (auto* c : std::initializer_list<juce::Component*> { &subOn, &subShape, &subOct, &subLevel, &subPan, &noiseOn, &noiseType, &noiseLevel, &noisePan }) addAndMakeVisible (c);
        }
        void refresh() { subOn.refresh(); subShape.refresh(); noiseOn.refresh(); noiseType.refresh(); for (auto* k : { &subOct, &subLevel, &subPan, &noiseLevel, &noisePan }) k->refresh(); }
        void paint (juce::Graphics& g) override { drawCard (g, subCard, {}, Palette::peach); drawCard (g, noiseCard, {}, Palette::lavender); }
        void resized() override
        {
            auto r = getLocalBounds();
            subCard = r.removeFromTop ((r.getHeight() - 8) / 2);
            r.removeFromTop (8);
            noiseCard = r;
            auto s = subCard.reduced (10, 8);
            auto st = s.removeFromTop (24); subOn.setBounds (st.removeFromLeft (80)); subShape.setBounds (st.reduced (0, 1));
            s.removeFromTop (4);
            const int w = s.getWidth() / 3;
            for (auto* k : { &subOct, &subLevel, &subPan }) k->setBounds (s.removeFromLeft (w).withSizeKeepingCentre (w, juce::jmin (62, s.getHeight())));
            auto nn = noiseCard.reduced (10, 8);
            auto nt = nn.removeFromTop (24); noiseOn.setBounds (nt.removeFromLeft (90)); noiseType.setBounds (nt.reduced (0, 1));
            nn.removeFromTop (4);
            const int w2 = nn.getWidth() / 2;
            for (auto* k : { &noiseLevel, &noisePan }) k->setBounds (nn.removeFromLeft (w2).withSizeKeepingCentre (w2, juce::jmin (62, nn.getHeight())));
        }
    private:
        wt::Engine& eng;
        SToggle subOn; SCombo subShape; SKnob subOct, subLevel, subPan;
        SToggle noiseOn; SCombo noiseType; SKnob noiseLevel, noisePan;
        juce::Rectangle<int> subCard, noiseCard;
    };

    //==========================================================================
    class FilterPanel : public juce::Component
    {
    public:
        explicit FilterPanel (wt::Engine& e) : eng (e),
            on (e, wt::F_On, "FILTRE", Palette::pink), type (e, wt::F_Type, wt::filterNames()),
            cutoff (e, wt::F_Cutoff, "Cutoff", Palette::pink, "Fréquence de coupure"), res (e, wt::F_Res, juce::String::fromUTF8 ("Résonance"), Palette::pink),
            drive (e, wt::F_Drive, "Drive", Palette::pink, "Saturation avant le filtre"), key (e, wt::F_Key, "Suivi clavier", Palette::pink, "Le filtre s'ouvre pour les notes aiguës"),
            mix (e, wt::F_Mix, "Mix", Palette::pink, "Dosage son filtré / son direct")
        {
            for (auto* c : std::initializer_list<juce::Component*> { &on, &type, &cutoff, &res, &drive, &key, &mix }) addAndMakeVisible (c);
            type.onEdit = [this] { repaint(); };
        }
        void refresh() { on.refresh(); type.refresh(); for (auto* k : { &cutoff, &res, &drive, &key, &mix }) k->refresh(); repaint (curve); }
        void paint (juce::Graphics& g) override
        {
            drawCard (g, getLocalBounds(), {}, Palette::pink);
            auto c = curve.toFloat();
            g.setColour (Palette::bg.withAlpha (0.55f)); g.fillRoundedRectangle (c, 8.0f);
            // réponse du filtre (approchée)
            const float fc = wt::cutoffHz (eng.patch.get (wt::F_Cutoff)), q = 0.5f + eng.patch.get (wt::F_Res) * 8.0f;
            const int t = (int) eng.patch.get (wt::F_Type);
            juce::Path p;
            for (int i = 0; i <= 120; ++i)
            {
                const float f = 20.0f * std::pow (1000.0f, i / 120.0f);
                const float x = f / fc;
                const std::complex<float> s (0.0f, x);
                const auto den = s * s + s / q + 1.0f;
                std::complex<float> h;
                switch (t)
                {
                    case 0: h = 1.0f / den; break;
                    case 1: h = (1.0f / den) * (1.0f / (s * s + s * 1.4f + 1.0f)); break;
                    case 2: h = (s * s) / den; break;
                    case 3: h = (s * s) / den * (s * s) / (s * s + s * 1.4f + 1.0f); break;
                    case 4: h = (s / q) / den; break;
                    case 5: h = (s * s + 1.0f) / den; break;
                    default: h = 1.0f + 0.5f * std::cos (x * 6.28f); break;
                }
                const float db = 20.0f * std::log10 (juce::jmax (1.0e-4f, std::abs (h)));
                const float y = c.getY() + c.getHeight() * 0.45f - juce::jlimit (-40.0f, 24.0f, db) / 48.0f * c.getHeight() * 0.8f;
                const float px = c.getX() + c.getWidth() * i / 120.0f;
                if (i == 0) p.startNewSubPath (px, y); else p.lineTo (px, y);
            }
            g.setColour (on.getToggleState() ? Palette::pink : Palette::dim);
            g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            // poignée : glisser = fréquence (gauche/droite) et résonance (haut/bas), molette = drive
            const auto hp = handlePos();
            g.setColour (Palette::pink.withAlpha (hover || drag ? 0.30f : 0.14f));
            g.fillEllipse (juce::Rectangle<float> (22.0f, 22.0f).withCentre (hp));
            g.setColour (Palette::pink);
            g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (hp));
            g.setColour (Palette::bg);
            g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre (hp));
            if (hover || drag)
            {
                g.setColour (Palette::dim); g.setFont (uiFont (11.0f));
                g.drawText (fr ("glisse : fréquence / résonance · molette : drive"), c.reduced (8.0f, 4.0f), juce::Justification::bottomLeft);
            }
        }
        void mouseMove (const juce::MouseEvent& e) override { const bool h = curve.toFloat().contains (e.position); if (h != hover) { hover = h; repaint (curve); } setMouseCursor (h ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor); }
        void mouseExit (const juce::MouseEvent&) override { hover = false; repaint (curve); }
        void mouseDown (const juce::MouseEvent& e) override { drag = curve.toFloat().contains (e.position); if (drag) setFromPoint (e.position); }
        void mouseDrag (const juce::MouseEvent& e) override { if (drag) setFromPoint (e.position); }
        void mouseUp (const juce::MouseEvent&) override { drag = false; repaint (curve); }
        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            if (! curve.toFloat().contains (e.position)) return;
            eng.patch.set (wt::F_Cutoff, wt::info()[(size_t) wt::F_Cutoff].def); eng.patch.set (wt::F_Res, wt::info()[(size_t) wt::F_Res].def);
            refresh();
        }
        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
        {
            if (! curve.toFloat().contains (e.position)) return;
            eng.patch.set (wt::F_Drive, juce::jlimit (0.0f, 1.0f, eng.patch.get (wt::F_Drive) + w.deltaY * 0.15f));
            refresh();
        }
        void resized() override
        {
            auto r = getLocalBounds().reduced (10, 8);
            auto top = r.removeFromTop (24);
            on.setBounds (top.removeFromLeft (84)); type.setBounds (top.reduced (0, 1));
            r.removeFromTop (6);
            auto knobsRow = r.removeFromBottom (60);
            curve = r.reduced (0, 2);
            const int w = knobsRow.getWidth() / 5;
            for (auto* k : { &cutoff, &res, &drive, &key, &mix }) k->setBounds (knobsRow.removeFromLeft (w));
        }
        void timerRefresh() { repaint (curve); }
    private:
        wt::Engine& eng;
        SToggle on; SCombo type; SKnob cutoff, res, drive, key, mix;
        juce::Rectangle<int> curve;
        bool hover = false, drag = false;
        float baseY() const { return (float) curve.getY() + (float) curve.getHeight() * 0.45f; }
        juce::Point<float> handlePos() const
        {
            const auto c = curve.toFloat();
            return { c.getX() + c.getWidth() * eng.patch.get (wt::F_Cutoff), baseY() - eng.patch.get (wt::F_Res) * c.getHeight() * 0.40f };
        }
        void setFromPoint (juce::Point<float> p)
        {
            const auto c = curve.toFloat();
            eng.patch.set (wt::F_Cutoff, juce::jlimit (0.0f, 1.0f, (p.x - c.getX()) / c.getWidth()));
            eng.patch.set (wt::F_Res, juce::jlimit (0.0f, 1.0f, (baseY() - p.y) / (c.getHeight() * 0.40f)));
            if (! on.getToggleState()) { eng.patch.set (wt::F_On, 1.0f); on.refresh(); }
            refresh();
        }
    };

    //==========================================================================
    class EnvPanel : public juce::Component
    {
    public:
        explicit EnvPanel (wt::Engine& e) : eng (e)
        {
            for (int i = 0; i < 3; ++i)
            {
                auto* b = tabs.add (new juce::TextButton (i == 0 ? fr ("ENV 1 (volume)") : "ENV " + juce::String (i + 1)));
                b->setClickingTogglesState (true); b->setRadioGroupId (8100);
                b->setColour (juce::TextButton::buttonOnColourId, Palette::mint.withAlpha (0.8f));
                b->onClick = [this, i] { show (i); };
                addAndMakeVisible (b);
                const int base = wt::E1_A + i * 4;
                for (int k = 0; k < 4; ++k)
                {
                    static const char* n[] = { "Attaque", "Decay", "Sustain", "Release" };
                    auto* kn = knobs.add (new SKnob (e, base + k, n[k], Palette::mint));
                    kn->onChange = [this] { repaint (graph); };
                    addChildComponent (kn);
                }
            }
            tabs[0]->setToggleState (true, juce::dontSendNotification);
            show (0);
        }
        void refresh() { for (auto* k : knobs) k->refresh(); repaint (graph); }
        void paint (juce::Graphics& g) override
        {
            drawCard (g, getLocalBounds(), {}, Palette::mint);
            auto c = graph.toFloat();
            g.setColour (Palette::bg.withAlpha (0.55f)); g.fillRoundedRectangle (c, 8.0f);
            const int base = wt::E1_A + cur * 4;
            const float a = eng.patch.get (base), d = eng.patch.get (base + 1), s = eng.patch.get (base + 2), rr = eng.patch.get (base + 3);
            const float total = dragPt >= 0 ? juce::jmax (frozenTotal, a + d + 0.4f + rr) : a + d + 0.4f + rr;
            auto X = [&] (float t) { return c.getX() + 6.0f + (c.getWidth() - 12.0f) * t / total; };
            auto Y = [&] (float v) { return c.getBottom() - 6.0f - (c.getHeight() - 12.0f) * v; };
            juce::Path p;
            p.startNewSubPath (X (0), Y (0));
            p.lineTo (X (a), Y (1));
            p.quadraticTo (X (a + d * 0.3f), Y (s), X (a + d), Y (s));
            p.lineTo (X (a + d + 0.4f), Y (s));
            p.quadraticTo (X (a + d + 0.4f + rr * 0.3f), Y (0), X (total), Y (0));
            g.setColour (Palette::mint.withAlpha (0.18f));
            auto fill = p; fill.lineTo (X (total), Y (0)); fill.closeSubPath(); g.fillPath (fill);
            g.setColour (Palette::mint); g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            // points à attraper : sommet de l'attaque, fin du decay / niveau du sustain, fin du release
            for (int k = 0; k < 3; ++k)
            {
                const auto pt = pointPos (k);
                const bool hot = k == hot_ || k == dragPt;
                if (hot) { g.setColour (Palette::mint.withAlpha (0.28f)); g.fillEllipse (juce::Rectangle<float> (22.0f, 22.0f).withCentre (pt)); }
                g.setColour (Palette::bg); g.fillEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre (pt));
                g.setColour (Palette::mint); g.drawEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre (pt), 2.0f);
            }
            g.setColour (Palette::dim); g.setFont (uiFont (11.0f));
            g.drawText (cur == 0 ? fr ("volume de chaque note") : fr ("à brancher dans la matrice (ou clic droit sur un bouton)"), c.reduced (8.0f, 4.0f), juce::Justification::topRight);
            if (hot_ >= 0 || dragPt >= 0)
            {
                static const char* tips[] = { "attaque", "decay / sustain", "release" };
                g.drawText (fr ("glisse le point : ") + fr (tips[juce::jmax (hot_, dragPt)]), c.reduced (8.0f, 4.0f), juce::Justification::bottomLeft);
            }
        }
        void mouseMove (const juce::MouseEvent& e) override { const int h = pointAt (e.position); if (h != hot_) { hot_ = h; repaint (graph); } setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor); }
        void mouseExit (const juce::MouseEvent&) override { hot_ = -1; repaint (graph); }
        void mouseDown (const juce::MouseEvent& e) override
        {
            dragPt = pointAt (e.position);
            frozenTotal = totalTime();   // l'échelle ne bouge pas pendant qu'on glisse
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragPt < 0) return;
            const int base = wt::E1_A + cur * 4;
            auto& P = eng.patch;
            const auto c = graph.toFloat();
            const float tm = juce::jmax (0.0f, (e.position.x - c.getX() - 6.0f) / (c.getWidth() - 12.0f) * frozenTotal);
            auto lim = [] (int idx, float v) { const auto& in = wt::info()[(size_t) idx]; return juce::jlimit (in.lo, in.hi, v); };
            const float a = P.get (base), d = P.get (base + 1);
            if (dragPt == 0) P.set (base, lim (base, tm));
            else if (dragPt == 1)
            {
                P.set (base + 1, lim (base + 1, tm - a));
                P.set (base + 2, juce::jlimit (0.0f, 1.0f, (c.getBottom() - 6.0f - e.position.y) / (c.getHeight() - 12.0f)));
            }
            else P.set (base + 3, lim (base + 3, tm - (a + d + 0.4f)));
            refresh();
        }
        void mouseUp (const juce::MouseEvent&) override { dragPt = -1; repaint (graph); }
        void resized() override
        {
            auto r = getLocalBounds().reduced (10, 8);
            auto top = r.removeFromTop (24);
            const int tw = top.getWidth() / 3;
            for (auto* t : tabs) t->setBounds (top.removeFromLeft (tw).reduced (1, 0));
            r.removeFromTop (6);
            auto kr = r.removeFromBottom (60);
            graph = r.reduced (0, 2);
            for (int i = 0; i < 3; ++i)
            {
                auto row = kr;
                const int w = row.getWidth() / 4;
                for (int k = 0; k < 4; ++k) knobs[i * 4 + k]->setBounds (row.removeFromLeft (w));
            }
        }
    private:
        wt::Engine& eng;
        juce::OwnedArray<juce::TextButton> tabs;
        juce::OwnedArray<SKnob> knobs;
        juce::Rectangle<int> graph;
        int cur = 0;
        int hot_ = -1, dragPt = -1;
        float frozenTotal = 1.0f;
        void show (int i) { cur = i; for (int k = 0; k < knobs.size(); ++k) knobs[k]->setVisible (k / 4 == i); repaint(); }
        float totalTime() const { const int base = wt::E1_A + cur * 4; return eng.patch.get (base) + eng.patch.get (base + 1) + 0.4f + eng.patch.get (base + 3); }
        juce::Point<float> pointPos (int k) const
        {
            const auto c = graph.toFloat();
            const int base = wt::E1_A + cur * 4;
            const float a = eng.patch.get (base), d = eng.patch.get (base + 1), s = eng.patch.get (base + 2), r = eng.patch.get (base + 3);
            const float total = dragPt >= 0 ? juce::jmax (frozenTotal, a + d + 0.4f + r) : a + d + 0.4f + r;
            auto X = [&] (float t) { return c.getX() + 6.0f + (c.getWidth() - 12.0f) * t / total; };
            auto Y = [&] (float v) { return c.getBottom() - 6.0f - (c.getHeight() - 12.0f) * v; };
            if (k == 0) return { X (a), Y (1.0f) };
            if (k == 1) return { X (a + d), Y (s) };
            return { X (a + d + 0.4f + r), Y (0.0f) };
        }
        int pointAt (juce::Point<float> p) const
        {
            int best = -1; float bd = 14.0f;
            for (int k = 0; k < 3; ++k) { const float d = pointPos (k).getDistanceFrom (p); if (d < bd) { bd = d; best = k; } }
            return best;
        }
    };

    //==========================================================================
    class LfoPanel : public juce::Component
    {
    public:
        explicit LfoPanel (wt::Engine& e) : eng (e)
        {
            for (int i = 0; i < 4; ++i)
            {
                auto* b = tabs.add (new juce::TextButton ("LFO " + juce::String (i + 1)));
                b->setClickingTogglesState (true); b->setRadioGroupId (8200);
                b->setColour (juce::TextButton::buttonOnColourId, Palette::lavender.withAlpha (0.8f));
                b->onClick = [this, i] { show (i); };
                addAndMakeVisible (b);
                const int base = wt::L1_Shape + i * 4;
                auto* sh = shapes.add (new SCombo (e, base, wt::lfoShapeNames()));
                sh->onEdit = [this] { repaint (graph); };
                addChildComponent (sh);
                auto* rt = rates.add (new SKnob (e, base + 1, "Vitesse", Palette::lavender, "Vitesse du LFO (ou division de mesure si Sync)"));
                addChildComponent (rt);
                auto* sy = syncs.add (new SToggle (e, base + 2, "Sync", Palette::lavender));
                sy->setTooltip (fr ("Sync : la vitesse suit le tempo de FL (1/4, 1/8…)"));
                addChildComponent (sy);
                auto* rg = retrigs.add (new SToggle (e, base + 3, "Par note", Palette::lavender));
                rg->setTooltip (fr ("Par note : le LFO repart du début à chaque note jouée"));
                addChildComponent (rg);
            }
            tabs[0]->setToggleState (true, juce::dontSendNotification);
            show (0);
        }
        void refresh() { for (auto* s : shapes) s->refresh(); for (auto* k : rates) k->refresh(); for (auto* t : syncs) t->refresh(); for (auto* t : retrigs) t->refresh(); repaint (graph); }
        void paint (juce::Graphics& g) override
        {
            drawCard (g, getLocalBounds(), {}, Palette::lavender);
            auto c = graph.toFloat();
            g.setColour (Palette::bg.withAlpha (0.55f)); g.fillRoundedRectangle (c, 8.0f);
            const int shape = (int) eng.patch.get (wt::L1_Shape + cur * 4);
            juce::Path p; float hold = 0.6f, prev = -0.3f; juce::Random rr (3);
            for (int i = 0; i <= 160; ++i)
            {
                const float ph = std::fmod (i / 80.0f, 1.0f);
                const bool wrapped = i % 80 == 0;
                const float v = wt::lfoShape (shape, ph, hold, prev, rr, wrapped);
                const float x = c.getX() + 6.0f + (c.getWidth() - 12.0f) * i / 160.0f, y = c.getCentreY() - v * (c.getHeight() * 0.4f);
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            g.setColour (Palette::lavender); g.strokePath (p, juce::PathStrokeType (2.0f));
            const float live = eng.uiLfo[cur].load();
            if (eng.activeVoices.load() > 0)
            {
                g.setColour (Palette::text);
                g.fillEllipse (c.getRight() - 16.0f, c.getCentreY() - live * (c.getHeight() * 0.4f) - 4.0f, 8.0f, 8.0f);
            }
            g.setColour (Palette::dim); g.setFont (uiFont (11.0f));
            const bool sync = eng.patch.get (wt::L1_Sync + cur * 4) > 0.5f;
            const float rate = eng.patch.get (wt::L1_Rate + cur * 4);
            g.drawText (sync ? wt::lfoSyncNames()[(int) std::round (rate * 11.0f)] : juce::String (wt::lfoHz (rate), 2) + " Hz", c.reduced (8.0f, 4.0f), juce::Justification::topRight);
            if (hover || dragging)
                g.drawText (fr ("glisse : vitesse · clic : forme suivante"), c.reduced (8.0f, 4.0f), juce::Justification::bottomLeft);
        }
        void mouseMove (const juce::MouseEvent& e) override { const bool h = graph.toFloat().contains (e.position); if (h != hover) { hover = h; repaint (graph); } setMouseCursor (h ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor); }
        void mouseExit (const juce::MouseEvent&) override { hover = false; repaint (graph); }
        void mouseDown (const juce::MouseEvent& e) override
        {
            dragging = graph.toFloat().contains (e.position);
            moved = false;
            startRate = eng.patch.get (wt::L1_Rate + cur * 4);
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (! dragging) return;
            if (e.getDistanceFromDragStart() > 3) moved = true;
            if (! moved) return;
            const float d = (float) (e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY()) / 220.0f;
            eng.patch.set (wt::L1_Rate + cur * 4, juce::jlimit (0.0f, 1.0f, startRate + d));
            rates[cur]->refresh();
            repaint (graph);
        }
        void mouseUp (const juce::MouseEvent&) override
        {
            if (dragging && ! moved)   // simple clic : forme suivante
            {
                const int n = wt::lfoShapeNames().size();
                const int idx = wt::L1_Shape + cur * 4;
                eng.patch.set (idx, (float) (((int) eng.patch.get (idx) + 1) % juce::jmax (1, n)));
                shapes[cur]->refresh();
            }
            dragging = false;
            repaint (graph);
        }
        void resized() override
        {
            auto r = getLocalBounds().reduced (10, 8);
            auto top = r.removeFromTop (24);
            const int tw = top.getWidth() / 4;
            for (auto* t : tabs) t->setBounds (top.removeFromLeft (tw).reduced (1, 0));
            r.removeFromTop (6);
            auto bottom = r.removeFromBottom (60);
            graph = r.reduced (0, 2);
            for (int i = 0; i < 4; ++i)
            {
                auto b = bottom;
                rates[i]->setBounds (b.removeFromLeft (66));
                b.removeFromLeft (6);
                shapes[i]->setBounds (b.removeFromTop (26).reduced (0, 1));
                b.removeFromTop (4);
                syncs[i]->setBounds (b.removeFromLeft (b.getWidth() / 2));
                retrigs[i]->setBounds (b);
            }
        }
        void liveRepaint() { if (eng.activeVoices.load() > 0 || wasActive) { wasActive = eng.activeVoices.load() > 0; repaint (graph); } }
    private:
        wt::Engine& eng;
        juce::OwnedArray<juce::TextButton> tabs;
        juce::OwnedArray<SCombo> shapes;
        juce::OwnedArray<SKnob> rates;
        juce::OwnedArray<SToggle> syncs, retrigs;
        juce::Rectangle<int> graph;
        int cur = 0; bool wasActive = false, hover = false, dragging = false, moved = false;
        float startRate = 0.0f;
        void show (int i) { cur = i; for (int k = 0; k < 4; ++k) { shapes[k]->setVisible (k == i); rates[k]->setVisible (k == i); syncs[k]->setVisible (k == i); retrigs[k]->setVisible (k == i); } repaint(); }
    };

    //==========================================================================
    class ModMatrix : public juce::Component
    {
    public:
        explicit ModMatrix (wt::Engine& e) : eng (e)
        {
            for (int m = 0; m < wt::numMods; ++m)
            {
                auto* s = srcs.add (new SCombo (e, wt::Mod0_Src + m * 3, wt::srcNames()));
                auto* d = dsts.add (new SCombo (e, wt::Mod0_Dst + m * 3, wt::dstNames()));
                auto* a = amts.add (new juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox));
                a->setRange (-1.0, 1.0);
                a->setDoubleClickReturnValue (true, 0.0);
                a->setColour (juce::Slider::trackColourId, Palette::lavender);
                a->setValue (e.patch.get (wt::Mod0_Amt + m * 3), juce::dontSendNotification);
                a->setTooltip (fr ("Quantité de modulation (au milieu = rien, à gauche = inversé). Double-clic = 0"));
                a->onValueChange = [this, m, a] { eng.patch.set (wt::Mod0_Amt + m * 3, (float) a->getValue()); };
                for (juce::Component* c : { (juce::Component*) s, (juce::Component*) d, (juce::Component*) a }) addAndMakeVisible (c);
            }
        }
        void refresh()
        {
            for (auto* s : srcs) s->refresh();
            for (auto* d : dsts) d->refresh();
            for (int m = 0; m < amts.size(); ++m)
            {
                const float v = eng.patch.get (wt::Mod0_Amt + m * 3);
                if (std::abs ((float) amts[m]->getValue() - v) > 1.0e-4f && ! amts[m]->isMouseButtonDown()) amts[m]->setValue (v, juce::dontSendNotification);
            }
        }
        void paint (juce::Graphics& g) override
        {
            drawCard (g, getLocalBounds(), fr ("MATRICE DE MODULATION  ·  source → destination"), Palette::lavender);
        }
        void resized() override
        {
            auto r = getLocalBounds().reduced (12, 8).withTrimmedTop (26);
            const int rowH = juce::jmax (20, r.getHeight() / wt::numMods);
            for (int m = 0; m < wt::numMods; ++m)
            {
                auto row = r.removeFromTop (rowH).reduced (0, 1);
                srcs[m]->setBounds (row.removeFromLeft (130)); row.removeFromLeft (4);
                dsts[m]->setBounds (row.removeFromLeft (150)); row.removeFromLeft (6);
                amts[m]->setBounds (row);
            }
        }
    private:
        wt::Engine& eng;
        juce::OwnedArray<SCombo> srcs, dsts;
        juce::OwnedArray<juce::Slider> amts;
    };

    //==========================================================================
    // Réglages globaux + macros
    class GlobalPanel : public juce::Component
    {
    public:
        explicit GlobalPanel (wt::Engine& e) : eng (e),
            volume (e, wt::G_Volume, "Volume", Palette::peach), voices (e, wt::G_Voices, "Polyphonie", Palette::peach, "Nombre de notes en même temps"),
            glide (e, wt::G_Glide, "Glide", Palette::peach, "Les notes glissent de l'une à l'autre"), bendR (e, wt::G_Bend, "Pitch bend", Palette::peach),
            vel (e, wt::G_Vel, juce::String::fromUTF8 ("Vélocité"), Palette::peach, "Plus haut = les notes jouées fort sont plus fortes"),
            mono (e, wt::G_Mono, { "Poly", "Mono", "Legato" }),
            m1 (e, wt::M1, "Macro 1", Palette::blue, "Macro : branche-la sur plusieurs réglages dans la matrice"),
            m2 (e, wt::M2, "Macro 2", Palette::blue, "Macro : branche-la sur plusieurs réglages dans la matrice"),
            m3 (e, wt::M3, "Macro 3", Palette::blue, "Macro : branche-la sur plusieurs réglages dans la matrice"),
            m4 (e, wt::M4, "Macro 4", Palette::blue, "Macro : branche-la sur plusieurs réglages dans la matrice")
        {
            for (auto* c : std::initializer_list<juce::Component*> { &volume, &voices, &glide, &bendR, &vel, &mono, &m1, &m2, &m3, &m4 }) addAndMakeVisible (c);
            mono.setTooltip (fr ("Poly = accords. Mono = une note à la fois. Legato = une note à la fois, sans relancer l'enveloppe (basses, leads)"));
        }
        void refresh() { mono.refresh(); for (auto* k : { &volume, &voices, &glide, &bendR, &vel, &m1, &m2, &m3, &m4 }) k->refresh(); }
        void paint (juce::Graphics& g) override
        {
            drawCard (g, voiceCard, fr ("JEU"), Palette::peach);
            drawCard (g, macroCard, fr ("MACROS"), Palette::blue);
        }
        void resized() override
        {
            auto r = getLocalBounds();
            voiceCard = r.removeFromTop ((r.getHeight() - 8) * 55 / 100);
            r.removeFromTop (8);
            macroCard = r;
            auto v = voiceCard.reduced (10, 8).withTrimmedTop (24);
            mono.setBounds (v.removeFromTop (24));
            v.removeFromTop (4);
            const int w = v.getWidth() / 5;
            for (auto* k : { &volume, &voices, &glide, &bendR, &vel }) k->setBounds (v.removeFromLeft (w));
            auto m = macroCard.reduced (10, 8).withTrimmedTop (22);
            const int w2 = m.getWidth() / 4;
            for (auto* k : { &m1, &m2, &m3, &m4 }) k->setBounds (m.removeFromLeft (w2));
        }
    private:
        wt::Engine& eng;
        SKnob volume, voices, glide, bendR, vel;
        SCombo mono;
        SKnob m1, m2, m3, m4;
        juce::Rectangle<int> voiceCard, macroCard;
    };

    //==========================================================================
    // Contenu de la page (défile)
    class SynthContent : public juce::Component
    {
    public:
        static constexpr int contentHeight = 1010;

        explicit SynthContent (VariantProcessor& p) : proc (p), eng (p.wtSynth), oscA (eng, 0), oscB (eng, 1), subNoise (eng), filter (eng), envs (eng), lfos (eng), matrix (eng), global (eng)
        {
            for (auto* c : std::initializer_list<juce::Component*> { &onBtn, &soloBtn, &mixKnob, &presetBox, &prevP, &nextP, &saveP, &initP, &randP, &folderP, &oscA, &oscB, &subNoise, &filter, &envs, &lfos, &matrix, &global, &hint })
                addAndMakeVisible (c);
            onBtn.setClickingTogglesState (true);
            onBtn.setColour (juce::TextButton::buttonOnColourId, Palette::mint);
            onBtn.setTooltip (fr ("Allume le synthé : il joue les mêmes notes que les couches (clavier, accords, arpège, piano roll, FL), et passe dans tes effets"));
            onBtn.onClick = [this] { proc.wtSynth.enabled.store (onBtn.getToggleState()); refreshOn(); };
            soloBtn.setClickingTogglesState (true);
            soloBtn.setColour (juce::TextButton::buttonOnColourId, Palette::peach);
            soloBtn.setToggleState (library::settings().getBoolValue ("synthSolo", true), juce::dontSendNotification);
            soloBtn.setTooltip (fr ("Allumé : sur cette page, ton clavier joue le synthé SEUL (les sons des couches et les VST se taisent) pour régler le son tranquille. Éteint : tu entends tout ensemble (couches + VST + synthé)."));
            soloBtn.onClick = [this] { library::settings().setValue ("synthSolo", soloBtn.getToggleState()); library::settings().saveIfNeeded(); if (isShowing()) proc.synthFocus.store (soloBtn.getToggleState()); };
            mixKnob.setSliderStyle (juce::Slider::LinearHorizontal);
            mixKnob.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
            mixKnob.setRange (0.0, 1.5);
            mixKnob.setValue (eng.mix.load(), juce::dontSendNotification);
            mixKnob.setDoubleClickReturnValue (true, 0.8);
            mixKnob.setTooltip (fr ("Volume du synthé dans le plugin (à côté des couches de samples)"));
            mixKnob.onValueChange = [this] { eng.mix.store ((float) mixKnob.getValue()); };
            mixKnob.onRightClick = [this] { hostmenu::show (mixKnob, "synMix"); };
            hostmenu::tag (onBtn, "synOn");
            presetBox.setTextWhenNothingSelected (fr ("Presets de synthé…"));
            presetBox.onChange = [this] { const int i = presetBox.getSelectedItemIndex(); if (juce::isPositiveAndBelow (i, presets.size())) loadPreset (i); };
            prevP.onClick = [this] { stepPreset (-1); };
            nextP.onClick = [this] { stepPreset (1); };
            saveP.setTooltip (fr ("Enregistre ce son dans ta banque (dossier « Presets de synthé »)"));
            saveP.onClick = [this] { savePreset(); };
            initP.setTooltip (fr ("Repart d'un son de base (une scie)"));
            initP.onClick = [this] { synthpresets::applyFactory (eng, 0); presetBox.setSelectedItemIndex (-1, juce::dontSendNotification); refreshAll(); };
            randP.setColour (juce::TextButton::buttonColourId, Palette::peach.withAlpha (0.3f));
            randP.setTooltip (fr ("Un son au hasard (tables, filtre, enveloppes, modulations) : appuie jusqu'à ce que ça te plaise"));
            randP.onClick = [this] { randomPatch(); };
            folderP.setTooltip (fr ("Ouvrir le dossier des presets de synthé (dans ta banque de sons)"));
            folderP.onClick = [] { auto d = synthpresets::folder(); d.createDirectory(); d.startAsProcess(); };
            hint.setFont (uiFont (12.0f));
            hint.setColour (juce::Label::textColourId, Palette::dim);
            hint.setText (fr ("Clic droit sur un bouton = « Moduler avec » un LFO, une enveloppe, une macro… · glisse sur une table pour changer sa position · dépose un .wav sur une table"), juce::dontSendNotification);
            refreshPresets();
            refreshOn();
        }

        void tick()
        {
            if (! firstShown) { firstShown = true; synthpresets::ensureFactory(); refreshPresets(); }
            if (++frame % 6 == 0) refreshAll();
            lfos.liveRepaint();
            if (eng.activeVoices.load() > 0) { oscA.repaint(); oscB.repaint(); }
            if (onBtn.getToggleState() != eng.enabled.load()) refreshOn();
        }

        void paint (juce::Graphics& g) override
        {
            auto bar = barArea.toFloat();
            g.setColour (Palette::panel.withAlpha (0.8f)); g.fillRoundedRectangle (bar, 10.0f);
            g.setColour (Palette::line); g.drawRoundedRectangle (bar, 10.0f, 1.0f);
            g.setColour (Palette::dim); g.setFont (uiFont (11.5f));
            g.drawText (fr ("Volume"), mixLabel, juce::Justification::centredRight);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (2, 0);
            barArea = r.removeFromTop (44);
            {
                auto b = barArea.reduced (8, 7);
                const bool narrow = b.getWidth() < 760;
                // (fenêtre étroite, à côté des effets rapides : boutons plus serrés pour garder de la place au volume)
                onBtn.setBounds (b.removeFromLeft (narrow ? 114 : 150)); b.removeFromLeft (4);
                soloBtn.setBounds (b.removeFromLeft (narrow ? 46 : 52)); b.removeFromLeft (narrow ? 4 : 8);
                prevP.setBounds (b.removeFromLeft (narrow ? 22 : 26)); b.removeFromLeft (2);
                presetBox.setBounds (b.removeFromLeft (narrow ? 112 : 170)); b.removeFromLeft (2);
                nextP.setBounds (b.removeFromLeft (narrow ? 22 : 26)); b.removeFromLeft (narrow ? 4 : 6);
                saveP.setBounds (b.removeFromLeft (narrow ? 82 : 92)); b.removeFromLeft (4);
                initP.setBounds (b.removeFromLeft (narrow ? 42 : 52)); b.removeFromLeft (4);
                randP.setBounds (b.removeFromLeft (narrow ? 62 : 76)); b.removeFromLeft (4);
                folderP.setBounds (b.removeFromLeft (narrow ? 26 : 30)); b.removeFromLeft (8);
                if (b.getWidth() < 130) { mixKnob.setBounds (b); mixLabel = {}; }   // pas la place pour le mot « Volume » : le curseur prend tout
                else
                {
                    mixKnob.setBounds (b.removeFromRight (juce::jmin (140, juce::jmax (60, b.getWidth() - 100))));
                    mixLabel = b;
                }
            }
            r.removeFromTop (8);
            auto row1 = r.removeFromTop (300);
            const int third = (row1.getWidth() - 16) * 38 / 100;
            oscA.setBounds (row1.removeFromLeft (third)); row1.removeFromLeft (8);
            oscB.setBounds (row1.removeFromLeft (third)); row1.removeFromLeft (8);
            subNoise.setBounds (row1);
            r.removeFromTop (8);
            auto row2 = r.removeFromTop (220);
            const int w3 = (row2.getWidth() - 16) / 3;
            filter.setBounds (row2.removeFromLeft (w3)); row2.removeFromLeft (8);
            envs.setBounds (row2.removeFromLeft (w3)); row2.removeFromLeft (8);
            lfos.setBounds (row2);
            r.removeFromTop (8);
            auto row3 = r.removeFromTop (330);
            matrix.setBounds (row3.removeFromLeft (row3.getWidth() * 58 / 100)); row3.removeFromLeft (8);
            global.setBounds (row3);
            r.removeFromTop (6);
            hint.setBounds (r.removeFromTop (22));
        }

    private:
        VariantProcessor& proc;
        wt::Engine& eng;
        PopupSafeTextButton onBtn { fr ("Synthé : éteint") };
        juce::TextButton soloBtn { fr ("Seul") };
        KnobSlider mixKnob;
        juce::ComboBox presetBox;
        juce::TextButton prevP { "<" }, nextP { ">" }, saveP { fr ("Enregistrer") }, initP { "Init" }, randP { fr ("Hasard") }, folderP { juce::String::fromUTF8 ("…") };
        OscPanel oscA, oscB;
        SubNoisePanel subNoise;
        FilterPanel filter;
        EnvPanel envs;
        LfoPanel lfos;
        ModMatrix matrix;
        GlobalPanel global;
        juce::Label hint;
        juce::Rectangle<int> barArea, mixLabel;
        juce::Array<juce::File> presets;
        bool firstShown = false;
        int frame = 0;
        juce::Random rng;
        std::unique_ptr<juce::AlertWindow> askName;

        void refreshOn()
        {
            const bool on = eng.enabled.load();
            onBtn.setToggleState (on, juce::dontSendNotification);
            onBtn.setButtonText (on ? fr ("Synthé : allumé") : fr ("Synthé : éteint"));
        }
        void refreshAll()
        {
            oscA.refresh(); oscB.refresh(); subNoise.refresh(); filter.refresh(); envs.refresh(); lfos.refresh(); matrix.refresh(); global.refresh();
            if (std::abs ((float) mixKnob.getValue() - eng.mix.load()) > 1.0e-4f && ! mixKnob.isMouseButtonDown()) mixKnob.setValue (eng.mix.load(), juce::dontSendNotification);
        }
        void refreshPresets()
        {
            presets = synthpresets::list();
            const int sel = presetBox.getSelectedItemIndex();
            presetBox.clear (juce::dontSendNotification);
            for (int i = 0; i < presets.size(); ++i) presetBox.addItem (presets[i].getFileNameWithoutExtension(), i + 1);
            if (sel >= 0) presetBox.setSelectedItemIndex (sel, juce::dontSendNotification);
        }
        void loadPreset (int i)
        {
            if (! juce::isPositiveAndBelow (i, presets.size())) return;
            synthpresets::load (eng, presets[i]);
            if (! eng.enabled.load()) { eng.enabled.store (true); refreshOn(); }   // choisir un son = l'entendre
            refreshAll();
        }
        void stepPreset (int d)
        {
            if (presets.isEmpty()) return;
            int i = presetBox.getSelectedItemIndex();
            i = i < 0 ? 0 : ((i + d) % presets.size() + presets.size()) % presets.size();
            presetBox.setSelectedItemIndex (i, juce::sendNotificationSync);
        }
        void savePreset()
        {
            auto* w = new juce::AlertWindow (fr ("Enregistrer le son"), fr ("Nom du preset (rangé dans ta banque, dossier « Presets de synthé ») :"), juce::AlertWindow::NoIcon);
            w->addTextEditor ("n", fr ("Mon son"));
            w->addComboBox ("c", { "Basse", "Lead", "Pad", "Pluck", "Clavier", "FX", "Autre" }, fr ("Catégorie"));
            w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton (fr ("Annuler"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
            juce::Component::SafePointer<SynthContent> self (this);
            w->enterModalState (true, juce::ModalCallbackFunction::create ([self, w] (int res)
            {
                if (res != 1 || self == nullptr) return;
                const auto name = w->getTextEditorContents ("n").trim();
                const auto cat = w->getComboBoxComponent ("c")->getText();
                if (name.isEmpty()) return;
                const auto f = synthpresets::folder().getChildFile (juce::File::createLegalFileName (cat + " - " + name) + ".vsynth");
                synthpresets::save (self->eng, f, cat);
                self->refreshPresets();
                for (int i = 0; i < self->presets.size(); ++i) if (self->presets[i] == f) self->presetBox.setSelectedItemIndex (i, juce::dontSendNotification);
            }), true);
        }

        // son au hasard, mais musical : tables, unisson, filtre, enveloppes et 1-3 modulations cohérentes
        void randomPatch()
        {
            using namespace wt;
            auto& p = eng.patch;
            p.reset();
            auto files = juce::Array<juce::File>();
            for (auto& d : tableFolders (library::defaultFolder())) files.addArray (d.findChildFiles (juce::File::findFiles, true, "*.wav"));
            auto pickTable = [&] (int osc)
            {
                if (! files.isEmpty() && rng.nextFloat() < 0.7f) { if (auto t = loadWav (files[rng.nextInt (files.size())])) { eng.setTable (osc, t); return; } }
                eng.setTable (osc, builtin (rng.nextInt (builtinNames().size())));
            };
            const int kind = rng.nextInt (4);   // 0 basse, 1 lead, 2 pad, 3 pluck
            pickTable (0);
            p.set (A_Pos, rng.nextFloat());
            p.set (A_Uni, (float) (kind == 0 ? 1 + rng.nextInt (2) : 1 + rng.nextInt (7)));
            p.set (A_Detune, 0.1f + rng.nextFloat() * 0.35f);
            p.set (A_Width, 0.5f + rng.nextFloat() * 0.5f);
            if (rng.nextFloat() < 0.6f)
            {
                pickTable (1);
                p.set (B_On, 1); p.set (B_Pos, rng.nextFloat()); p.set (B_Level, 0.35f + rng.nextFloat() * 0.4f);
                const int iv[] = { 0, 0, 7, 12, -12, 5 };
                const int k = iv[rng.nextInt (6)]; p.set (B_Oct, (float) (k / 12)); p.set (B_Semi, (float) (k % 12)); p.set (B_Fine, (rng.nextFloat() - 0.5f) * 20.0f);
            }
            if (kind == 0) { p.set (A_Oct, -1); p.set (B_Oct, -1); p.set (Sub_On, rng.nextBool() ? 1.0f : 0.0f); p.set (G_Mono, 2); p.set (G_Glide, rng.nextFloat() * 0.2f); }
            if (rng.nextFloat() < 0.25f) { p.set (Noise_On, 1); p.set (Noise_Type, (float) rng.nextInt (3)); p.set (Noise_Level, 0.1f + rng.nextFloat() * 0.25f); }
            p.set (F_Type, (float) (rng.nextFloat() < 0.75f ? rng.nextInt (2) : rng.nextInt (7)));
            p.set (F_Cutoff, kind == 2 ? 0.5f + rng.nextFloat() * 0.3f : 0.3f + rng.nextFloat() * 0.5f);
            p.set (F_Res, rng.nextFloat() * 0.6f);
            p.set (F_Drive, rng.nextFloat() < 0.4f ? rng.nextFloat() * 0.6f : 0.0f);
            switch (kind)
            {
                case 2:  synthpresets::env (p, 0, 0.3f + rng.nextFloat() * 1.2f, 1.0f, 0.8f, 1.0f + rng.nextFloat() * 2.0f); break;
                case 3:  synthpresets::env (p, 0, 0.002f, 0.2f + rng.nextFloat() * 0.5f, 0.0f, 0.2f + rng.nextFloat() * 0.3f); break;
                default: synthpresets::env (p, 0, 0.003f + rng.nextFloat() * 0.02f, 0.5f, 0.7f + rng.nextFloat() * 0.3f, 0.2f + rng.nextFloat() * 0.4f); break;
            }
            synthpresets::env (p, 1, 0.001f + rng.nextFloat() * 0.05f, 0.1f + rng.nextFloat() * 0.6f, rng.nextFloat() * 0.3f, 0.3f);
            int slot = 0;
            if (kind == 3 || rng.nextBool()) synthpresets::mod (p, slot++, S_Env2, D_Cutoff, 0.3f + rng.nextFloat() * 0.5f);
            synthpresets::lfo (p, 0, rng.nextInt (7), rng.nextBool() ? synthpresets::syncRate (4 + rng.nextInt (5)) : 0.1f + rng.nextFloat() * 0.4f, rng.nextBool(), rng.nextBool());
            synthpresets::mod (p, slot++, S_Lfo1, rng.nextBool() ? D_APos : D_Cutoff, 0.15f + rng.nextFloat() * 0.4f);
            if (rng.nextBool()) synthpresets::mod (p, slot++, S_Random, D_BPos, 0.2f);
            synthpresets::mod (p, slot++, S_Macro1, D_Cutoff, 0.5f);
            if (! eng.enabled.load()) { eng.enabled.store (true); refreshOn(); }
            presetBox.setSelectedItemIndex (-1, juce::dontSendNotification);
            refreshAll();
        }
    };

    //==========================================================================
    // Colonne d'effets à droite de la page : une version réduite de chaque effet du plugin,
    // qui défile doucement pendant qu'on fait du son (s'arrête sous la souris, molette = défiler)
    class MiniFxCard : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        MiniFxCard (VariantProcessor& p, int t) : proc (p), type (t)
        {
            setTooltip (fr (fxs::def (t).name) + "  —  " + fr (fxs::def (t).desc) + "\n"
                        + fr ("Clic : ajouter à ta chaîne d'effets · touche le dessin pour le régler · clic droit : presets, retirer"));
        }
        static constexpr int height = 112;
        float time = 0.0f;

        juce::Colour accent() const { const juce::Colour cols[] = { Palette::peach, Palette::pink, Palette::blue, Palette::mint, Palette::lavender }; return cols[type % 5]; }

        void sync()
        {
            int s = -1;
            for (int k = 0; k < fxs::numSlots; ++k) if (proc.getSlotType (k) == type) { s = k; break; }
            if (s == slot) return;
            slot = s;
            visual.reset();
            if (slot >= 0)
            {
                visual = std::make_unique<FxVisual> (proc, slot, type, accent());
                visual->compact = true;
                addAndMakeVisible (*visual);
            }
            resized(); repaint();
        }

        void animate (float dt)
        {
            time += dt;
            if (visual != nullptr) { visual->time += dt; visual->repaint(); }
            else repaint (iconArea);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (6, 4);
            header = r.removeFromTop (22);
            mixBar = r.removeFromBottom (14);
            r.removeFromBottom (4);
            iconArea = r;
            if (visual != nullptr) visual->setBounds (r);
        }

        void paint (juce::Graphics& g) override
        {
            const auto c = accent();
            const bool in = slot >= 0;
            const bool on = in && proc.getParam (VariantProcessor::slotId (slot, "On")) > 0.5f;
            auto rf = getLocalBounds().toFloat().reduced (3.0f, 2.0f);
            g.setColour (Palette::panel.withAlpha (0.92f)); g.fillRoundedRectangle (rf, 10.0f);
            g.setColour (on ? c.withAlpha (0.7f) : (over ? Palette::text.withAlpha (0.25f) : Palette::line));
            g.drawRoundedRectangle (rf, 10.0f, on ? 1.4f : 1.0f);
            auto h = header;
            g.setColour (c); g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ (float) h.getX() + 5.0f, (float) h.getCentreY() }));
            // bouton à droite : + (ajouter) ou interrupteur (allumé / contourné)
            auto btn = h.removeFromRight (34).toFloat().reduced (2.0f, 3.0f);
            if (! in)
            {
                g.setColour (c.withAlpha (over ? 0.35f : 0.18f)); g.fillRoundedRectangle (btn, btn.getHeight() * 0.5f);
                g.setColour (Palette::text); g.setFont (uiFont (14.0f, juce::Font::bold)); g.drawText ("+", btn, juce::Justification::centred);
            }
            else
            {
                g.setColour (on ? c : Palette::panel2); g.fillRoundedRectangle (btn, btn.getHeight() * 0.5f);
                g.setColour (on ? c : Palette::line); g.drawRoundedRectangle (btn, btn.getHeight() * 0.5f, 1.0f);
                const float d = btn.getHeight() - 4.0f;
                g.setColour (on ? Palette::bg : Palette::dim);
                g.fillEllipse (on ? btn.getRight() - d - 2.0f : btn.getX() + 2.0f, btn.getY() + 2.0f, d, d);
            }
            g.setColour (Palette::text);
            g.setFont (uiFont (12.5f, juce::Font::bold));
            g.drawText (fr (fxs::def (type).name), h.withTrimmedLeft (14).toFloat() + Vibe::offsetFor (this), juce::Justification::centredLeft, true);
            if (! in)
            {
                g.setColour (c.withAlpha (0.07f)); g.fillRoundedRectangle (iconArea.toFloat(), 8.0f);
                g.setOpacity (0.75f);
                fxs::drawIcon (g, type, iconArea.toFloat().reduced (iconArea.getWidth() * 0.18f, 8.0f), time, c);
                g.setOpacity (1.0f);
                g.setColour (Palette::dim); g.setFont (uiFont (11.0f));
                g.drawText (fr ("clique pour ajouter"), mixBar, juce::Justification::centred);
            }
            else
            {
                // dosage (Mix) : barre à glisser
                const float mixV = proc.getParam (VariantProcessor::slotId (slot, "Mix"));
                auto mb = mixBar.toFloat().reduced (0.0f, 1.0f);
                g.setColour (Palette::panel2); g.fillRoundedRectangle (mb, 3.0f);
                g.setColour (c.withAlpha (on ? 0.9f : 0.4f)); g.fillRoundedRectangle (mb.withWidth (juce::jmax (6.0f, mb.getWidth() * mixV)), 3.0f);
                g.setColour (mixV > 0.3f && on ? Palette::bg : Palette::text.withAlpha (0.8f)); g.setFont (uiFont (10.5f, juce::Font::bold));
                g.drawText ("mix " + juce::String ((int) std::round (mixV * 100)) + " %", mixBar.reduced (6, 0), juce::Justification::centredLeft);
            }
        }

        void mouseEnter (const juce::MouseEvent&) override { over = true; repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { over = false; repaint(); }
        void mouseDown (const juce::MouseEvent& e) override
        {
            draggingMix = slot >= 0 && mixBar.expanded (0, 4).contains (e.getPosition()) && ! e.mods.isPopupMenu();
            if (draggingMix) { mixParam = proc.apvts.getParameter (VariantProcessor::slotId (slot, "Mix")); if (mixParam) mixParam->beginChangeGesture(); mouseDrag (e); }
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (draggingMix && mixParam != nullptr)
            {
                mixParam->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, (float) (e.x - mixBar.getX()) / (float) juce::jmax (1, mixBar.getWidth())));
                repaint();
            }
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (draggingMix) { if (mixParam) mixParam->endChangeGesture(); draggingMix = false; mixParam = nullptr; return; }
            if (e.mods.isPopupMenu()) { showMenu(); return; }
            if (slot < 0) { add(); return; }
            if (header.contains (e.getPosition()))   // interrupteur
                proc.setParam (VariantProcessor::slotId (slot, "On"), proc.getParam (VariantProcessor::slotId (slot, "On")) > 0.5f ? 0.0f : 1.0f);
            repaint();
        }

    private:
        VariantProcessor& proc;
        int type, slot = -2;
        std::unique_ptr<FxVisual> visual;
        juce::Rectangle<int> header, iconArea, mixBar;
        bool over = false, draggingMix = false;
        juce::RangedAudioParameter* mixParam = nullptr;

        void add()
        {
            auto* pp = &proc; const int t = type; int got = -1;
            proc.fxAction ([pp, t, &got] { got = pp->addEffect (t); });
            if (got < 0) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, fr ("Chaîne pleine"),
                                                                 fr ("Ta chaîne d'effets est pleine : retire un effet (page Effets) pour en ajouter un autre."));
            sync();
        }

        void showMenu()
        {
            juce::PopupMenu m, pr;
            m.addSectionHeader (fr (fxs::def (type).name));
            if (slot < 0) m.addItem (1, fr ("Ajouter à la chaîne"));
            else
            {
                const auto& d = fxs::def (type);
                for (int i = 0; i < (int) d.presets.size(); ++i) pr.addItem (100 + i, fr (d.presets[(size_t) i].name));
                if (! d.presets.empty()) m.addSubMenu ("Presets", pr);
                m.addItem (2, fr ("Réglages au hasard"));
                m.addItem (3, fr ("Retirer de la chaîne"));
            }
            juce::Component::SafePointer<MiniFxCard> self (this);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [self] (int r)
            {
                if (self == nullptr || r == 0) return;
                auto& p = self->proc; const int s = self->slot;
                if (r == 1) self->add();
                else if (r == 2 && s >= 0) p.fxAction ([&p, s] { p.randomizeEffect (s); });
                else if (r == 3 && s >= 0) p.fxAction ([&p, s] { p.removeEffect (s); });
                else if (r >= 100 && s >= 0) p.fxAction ([&p, s, r] { p.applyEffectPreset (s, r - 100); });
                if (self != nullptr) { self->sync(); self->repaint(); }
            });
        }
    };

    class FxStrip : public juce::Component
    {
    public:
        explicit FxStrip (VariantProcessor& p) : proc (p)
        {
            addAndMakeVisible (lane);
            for (int t = 1; t < fxs::NumTypes; ++t)
            {
                if (fxs::def (t).name == nullptr || juce::String (fxs::def (t).name).isEmpty()) continue;
                lane.addAndMakeVisible (cards.add (new MiniFxCard (p, t)));
            }
            for (auto* c : cards) c->sync();
        }
        static constexpr int width = 200;

        void tick (float dt)
        {
            const bool paused = isMouseOverOrDragging (true) || juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown() && isMouseOver (true);
            if (! paused) offset += speed * dt;
            if (++frame % 10 == 0) for (auto* c : cards) c->sync();
            layoutCards();
            for (auto* c : cards) if (c->getBottom() > 0 && c->getY() < lane.getHeight()) c->animate (dt);
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (Palette::panel.withAlpha (0.55f)); g.fillRoundedRectangle (r, 12.0f);
            g.setColour (Palette::line); g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);
            auto t = getLocalBounds().reduced (12, 0).removeFromTop (30);
            g.setColour (Palette::lavender);
            g.fillRoundedRectangle (juce::Rectangle<float> (4.0f, 14.0f).withCentre ({ (float) t.getX() + 2.0f, (float) t.getCentreY() }), 2.0f);
            g.setColour (Palette::text.withAlpha (0.75f));
            g.setFont (uiFont (12.5f, juce::Font::bold).withExtraKerningFactor (0.08f));
            g.drawText (fr ("EFFETS RAPIDES"), t.withTrimmedLeft (12).toFloat() + Vibe::offsetFor (this), juce::Justification::centredLeft);
        }
        void paintOverChildren (juce::Graphics& g) override
        {
            // fondu en haut et en bas de la colonne qui défile
            auto l = lane.getBounds().toFloat();
            const auto bgc = Palette::bg.interpolatedWith (Palette::panel, 0.5f);
            g.setGradientFill (juce::ColourGradient (bgc, 0.0f, l.getY(), bgc.withAlpha (0.0f), 0.0f, l.getY() + 22.0f, false));
            g.fillRect (l.withHeight (22.0f));
            g.setGradientFill (juce::ColourGradient (bgc.withAlpha (0.0f), 0.0f, l.getBottom() - 22.0f, bgc, 0.0f, l.getBottom(), false));
            g.fillRect (l.withTrimmedTop (l.getHeight() - 22.0f));
        }
        void resized() override { lane.setBounds (getLocalBounds().reduced (4, 0).withTrimmedTop (30).withTrimmedBottom (6)); layoutCards(); }
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override { offset -= w.deltaY * 160.0f; layoutCards(); }

    private:
        VariantProcessor& proc;
        juce::Component lane;
        juce::OwnedArray<MiniFxCard> cards;
        float offset = 0.0f, speed = 9.0f;
        int frame = 0;

        void layoutCards()
        {
            const int h = MiniFxCard::height;
            const float total = (float) (cards.size() * h);
            if (total <= 0.0f) return;
            const bool loop = total > (float) lane.getHeight() + h;
            for (int i = 0; i < cards.size(); ++i)
            {
                float y = (float) (i * h) - (loop ? offset : 0.0f);
                if (loop) { y = std::fmod (y + (float) h, total); if (y < 0.0f) y += total; y -= (float) h; }
                cards[i]->setBounds (0, juce::roundToInt (y), lane.getWidth(), h);
            }
        }
    };

    class SynthPage : public juce::Component
    {
    public:
        explicit SynthPage (VariantProcessor& p) : content (p), scroller (content, SynthContent::contentHeight), strip (p)
        {
            addAndMakeVisible (scroller);
            addAndMakeVisible (strip);
        }
        void resized() override
        {
            auto r = getLocalBounds();
            if (r.getWidth() >= 820) { strip.setBounds (r.removeFromRight (r.getWidth() >= 1040 ? FxStrip::width : 150)); r.removeFromRight (8); strip.setVisible (true); }
            else strip.setVisible (false);
            scroller.setBounds (r);
        }
        void tick() { content.tick(); if (strip.isVisible()) strip.tick (1.0f / 30.0f); }
        SynthContent content;
    private:
        ScrollPage scroller;
        FxStrip strip;
    };
}
