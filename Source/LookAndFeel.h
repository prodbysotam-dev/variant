#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include "I18n.h"
#include <atomic>

// Couleurs du thème actif (modifiables : voir themes::apply)
namespace Palette
{
    inline juce::Colour bg       { 0xff0d0d0f };
    inline juce::Colour panel    { 0xff161619 };
    inline juce::Colour panel2   { 0xff1f1f23 };
    inline juce::Colour line     { 0xff2c2c32 };
    inline juce::Colour text     { 0xffededf0 };
    inline juce::Colour dim      { 0xff85858e };

    inline juce::Colour blue     { 0xffa8c8f0 };
    inline juce::Colour mint     { 0xffb5e3c8 };
    inline juce::Colour pink     { 0xfff2b8c6 };
    inline juce::Colour lavender { 0xffc9b8f0 };
    inline juce::Colour peach    { 0xfff5d0a9 };

    inline juce::Colour keyWhite { 0xffe4e4e8 };
    inline juce::Colour keyBlack { 0xff1a1a1d };
    inline bool light = false;
    inline bool y2k = false;     // thème Y2K : décor (grille, étoiles, titre chromé)
    inline int decor = 0;        // 2 Palestine, 3 LFI, 4 Brésil : emblème dans l'en-tête ; 5 jungle, 6 désert : décor de fond
}

// Vibration des textes avec les basses (à la place de toute la fenêtre qui tremblait)
namespace Vibe
{
    inline float amount = 0.0f;   // en pixels
    inline float phase = 0.0f;    // avance à chaque image
    // décalage propre à chaque texte (selon son composant / sa position) : les textes ne bougent pas tous pareil
    inline juce::Point<float> offsetFor (const void* key, float salt = 0.0f)
    {
        if (amount < 0.05f) return {};
        const auto h = (float) (((juce::pointer_sized_uint) key >> 4) % 997u) + salt * 13.7f;
        return { amount * std::sin (phase * 1.9f + h * 0.71f), amount * 0.8f * std::cos (phase * 2.3f + h * 1.37f) };
    }
}

namespace themes
{
    struct Theme { const char* name; juce::uint32 c[13]; };

    inline const std::vector<Theme>& list()
    {
        //                         bg          panel       panel2      line        text        dim         accent1     accent2     accent3     accent4     accent5     keyWhite    keyBlack
        static const std::vector<Theme> t {
            { "Pastel nuit",    { 0xff0d0d0f, 0xff161619, 0xff1f1f23, 0xff2c2c32, 0xffededf0, 0xff8c8c95, 0xffa8c8f0, 0xffb5e3c8, 0xfff2b8c6, 0xffc9b8f0, 0xfff5d0a9, 0xffe4e4e8, 0xff1a1a1d } },
            { "Graphite",       { 0xff151517, 0xff1d1d20, 0xff26262a, 0xff34343a, 0xfff3f3f4, 0xff92929a, 0xffb4c6dc, 0xffbcd9c8, 0xffe2bcc6, 0xffc8bfdf, 0xffe6cfb5, 0xffe8e8ea, 0xff1c1c1f } },
            { "Nuit bleue",     { 0xff0e1220, 0xff151a2b, 0xff1c2236, 0xff2a3149, 0xffe8edf8, 0xff8b94ad, 0xff9fc3f5, 0xffa9e5cf, 0xfff4b9cf, 0xffc6b9f6, 0xfff6d2ab, 0xffe2e7f2, 0xff141a2a } },
            { "Rosé",           { 0xff171214, 0xff20191c, 0xff2a2125, 0xff3a2e33, 0xfff6ecef, 0xff9e8e94, 0xfff2b8c6, 0xffc4e2d2, 0xfff5c9a8, 0xffd7c2ef, 0xfff7dcc4, 0xfff3e9ec, 0xff1d1619 } },
            { "Brume pastel",   { 0xff1a1d24, 0xff22262f, 0xff2a2f3a, 0xff363c49, 0xffe9edf5, 0xff8d95a6, 0xffb9d4f2, 0xffc3e8d3, 0xfff3c6d3, 0xffd5c8f3, 0xfff6dcbc, 0xffe6e9f0, 0xff1e2129 } },
            { "Encre",          { 0xff050505, 0xff0e0e0e, 0xff171717, 0xff262626, 0xfff5f5f5, 0xff8a8a8a, 0xffc9b8f0, 0xffc9b8f0, 0xffc9b8f0, 0xffc9b8f0, 0xffc9b8f0, 0xffeeeeee, 0xff111111 } },
            { "Noir & blanc",   { 0xff000000, 0xff0c0c0c, 0xff181818, 0xff2e2e2e, 0xffffffff, 0xff8a8a8a, 0xffffffff, 0xffd6d6d6, 0xffbdbdbd, 0xffe8e8e8, 0xfff2f2f2, 0xfff0f0f0, 0xff111111 } },
            { "Blanc pastel",   { 0xfff7f7f8, 0xffffffff, 0xfff0f0f3, 0xffe2e2e7, 0xff1d1d22, 0xff85858f, 0xff7fa8dc, 0xff6fbd92, 0xffe08ea4, 0xffa08ad8, 0xffdea06e, 0xffffffff, 0xff2a2a2e } },
            { "Blanc pur",      { 0xffffffff, 0xffffffff, 0xfff4f4f4, 0xffe6e6e6, 0xff111111, 0xff858585, 0xff111111, 0xff3a3a3a, 0xff5a5a5a, 0xff2a2a2a, 0xff444444, 0xffffffff, 0xff1c1c1c } },
            { "Papier",         { 0xfff6f3ee, 0xfffdfbf8, 0xffefeae2, 0xffe0d9cd, 0xff26231f, 0xff8b847a, 0xff6f94c4, 0xff6fa889, 0xffcf7f91, 0xff9583c4, 0xffcf925c, 0xfffdfbf8, 0xff2b2722 } },
            { "Lavande",        { 0xfff5f3fb, 0xffffffff, 0xffeeeaf8, 0xffdfd9f0, 0xff23202d, 0xff8a85a0, 0xff8e9fe0, 0xff74bfa0, 0xffdb8eb0, 0xff9a7fdc, 0xffdca07a, 0xffffffff, 0xff2a2733 } },
            { "Menthe",         { 0xfff2f8f5, 0xffffffff, 0xffe8f2ec, 0xffd6e7dd, 0xff1f2a24, 0xff7f9187, 0xff6fa3cf, 0xff4fae84, 0xffd98a9c, 0xff8d86cf, 0xffd29a63, 0xffffffff, 0xff25302a } },
            { "Pêche crème",    { 0xfffbf6f1, 0xffffffff, 0xfff6ede4, 0xffeadccd, 0xff2b241f, 0xff958779, 0xff7c9fcf, 0xff79b291, 0xffdc8b8b, 0xffa58bd1, 0xffe0955a, 0xffffffff, 0xff2e2620 } },
            { "Y2K skate",      { 0xff120f24, 0xff1c1838, 0xff262148, 0xff3a3366, 0xfff4f1ff, 0xff9d96c4, 0xff5ce1ff, 0xffc6ff4d, 0xffff5fbf, 0xffb18cff, 0xffffa94d, 0xffeeeaff, 0xff17132c } },
            { "Y2K chrome",     { 0xffdfe6f0, 0xfff4f7fb, 0xffe6ecf4, 0xffc3cddb, 0xff1b1d3a, 0xff6d7494, 0xff2f8cff, 0xff2fbf62, 0xffff3fa4, 0xff8a5cff, 0xffff8a1f, 0xffffffff, 0xff22243f } },
            { "Palestine",      { 0xff0f0f0f, 0xff181818, 0xff222222, 0xff343434, 0xfff5f5f5, 0xff9a9a9a, 0xff2fa35c, 0xff7bcf98, 0xffe5484d, 0xffdedede, 0xffc9a227, 0xfff2f2f2, 0xff141414 } },
            { "La France insoumise", { 0xff1a0f2a, 0xff25163c, 0xff301d4d, 0xff4b2d70, 0xfff8f0ff, 0xffb9a0d6, 0xffb04fd6, 0xffe8332f, 0xffff5c8a, 0xff8e44ff, 0xffff8a3d, 0xfffaf5ff, 0xff1d1030 } },
            { "Brésil",         { 0xff0b2a1a, 0xff103522, 0xff15402a, 0xff22603e, 0xfffffbe6, 0xffa9c7a0, 0xff4f7ff0, 0xff2fd06f, 0xffffd23f, 0xffffffff, 0xffffb020, 0xfffffdf0, 0xff0d2216 } },
            { "FL Studio",      { 0xff2b3137, 0xff353c43, 0xff3e464e, 0xff4d565f, 0xffdde3e8, 0xff8f99a2, 0xff5fb3e6, 0xff8fd14f, 0xffe56f9c, 0xffa98ee8, 0xffff9b3d, 0xffe9edf0, 0xff23282d } },
            { "Ableton Live",   { 0xff1e1e1e, 0xff2a2a2a, 0xff333333, 0xff464646, 0xffe6e6e6, 0xff9a9a9a, 0xff5bc0eb, 0xffb6e3a0, 0xffff6f61, 0xffc4a5ff, 0xffff9f43, 0xfff0f0f0, 0xff171717 } },
            { "Logic Pro",      { 0xff232427, 0xff2e3034, 0xff383a3f, 0xff4a4d53, 0xffe9eaec, 0xff9b9ea4, 0xff4b8bf5, 0xff63d07a, 0xffef6a8d, 0xffa48cf0, 0xfff2c94c, 0xfff2f3f5, 0xff1b1c1f } },
            { "Bitwig",         { 0xff1c1c1c, 0xff262626, 0xff2f2f2f, 0xff424242, 0xffececec, 0xff8f8f8f, 0xff53b8e6, 0xff8fcf5a, 0xffe85a8c, 0xffb294f0, 0xffff6a00, 0xfff4f4f4, 0xff141414 } },
            { "Pro Tools",      { 0xff262b33, 0xff2f353e, 0xff39404b, 0xff4b5361, 0xffe3e8ef, 0xff95a0ae, 0xff3db3e3, 0xff6cbf4a, 0xffd9567a, 0xff9d8de6, 0xffe8a23a, 0xffeef2f7, 0xff1e2229 } },
            { "Cubase",         { 0xff20232a, 0xff2a2e37, 0xff333844, 0xff454b59, 0xffe4e8f0, 0xff97a0b2, 0xff4a90e2, 0xff5fd3a0, 0xffe0607e, 0xffa98bf0, 0xfff09a3a, 0xffeef1f8, 0xff191b21 } },
            { "Jungle",         { 0xff0b1a10, 0xff112418, 0xff173020, 0xff24452f, 0xffeef6e4, 0xff92b292, 0xff5cc8d6, 0xff7fd36b, 0xffff8a5c, 0xffc6e86a, 0xffffd45c, 0xffeef4e6, 0xff0e1a12 } },
            { "Désert",         { 0xff22160e, 0xff2e2015, 0xff3a2a1c, 0xff58422b, 0xfffaeedd, 0xffbaa084, 0xff6ab0d0, 0xff9cc46a, 0xffe8664a, 0xffd9b36a, 0xffe8a25a, 0xfff6ead6, 0xff20150d } },
        };
        return t;
    }

    inline void apply (int index)
    {
        const auto& th = list()[(size_t) juce::jlimit (0, (int) list().size() - 1, index)];
        juce::Colour* targets[] = { &Palette::bg, &Palette::panel, &Palette::panel2, &Palette::line, &Palette::text, &Palette::dim,
                                    &Palette::blue, &Palette::mint, &Palette::pink, &Palette::lavender, &Palette::peach,
                                    &Palette::keyWhite, &Palette::keyBlack };
        for (int i = 0; i < 13; ++i) *targets[i] = juce::Colour (th.c[i]);
        Palette::light = Palette::bg.getPerceivedBrightness() > 0.5f;
        Palette::y2k = juce::String (th.name).startsWith ("Y2K");
        const auto nm = juce::String::fromUTF8 (th.name);
        Palette::decor = nm == "Palestine" ? 2 : (nm == "La France insoumise" ? 3 : (nm == juce::String::fromUTF8 ("Brésil") ? 4 : (nm == "Jungle" ? 5 : (nm.startsWith ("D") && nm.endsWith ("sert") ? 6 : 0))));
    }

    inline juce::StringArray names()
    {
        juce::StringArray a;
        for (auto& t : list()) a.add (fr (t.name));
        return a;
    }
}

// Police : plus grande, adaptée à la langue (japonais / chinois)
inline juce::Font uiFont (float h, int style = juce::Font::plain)
{
    h = h < 12.5f ? 12.5f : h;
    const int l = i18n::current().load();
    if (l != i18n::JA && l != i18n::ZH)
   #if JUCE_MAC   // polices du Mac (Segoe UI n existe que sur Windows)
    if (l == i18n::JA) return juce::Font (juce::FontOptions ("Hiragino Sans", h, style));
    if (l == i18n::ZH) return juce::Font (juce::FontOptions ("PingFang SC", h, style));
    return juce::Font (juce::FontOptions ("Helvetica Neue", h, style));
   #else
    if (l == i18n::JA) return juce::Font (juce::FontOptions ("Yu Gothic UI", h, style));
    if (l == i18n::ZH) return juce::Font (juce::FontOptions ("Microsoft YaHei UI", h, style));
    return juce::Font (juce::FontOptions ("Segoe UI", h, style));
   #endif
}

// Icônes dessinées en vectoriel (pas d'emoji) : remplacent les symboles ⚙ ⧉ ✂ ★ ● ■ ▶ ◀ en tête du texte des boutons
namespace icons
{
    inline bool isIcon (juce::juce_wchar c)
    {
        return c == 0x2699 || c == 0x29C9 || c == 0x2702 || c == 0x2605 || c == 0x25CF || c == 0x25A0 || c == 0x25B6 || c == 0x25C0 || c == 0x25A3;
    }

    inline void draw (juce::Graphics& g, juce::juce_wchar c, juce::Rectangle<float> b, juce::Colour col)
    {
        g.setColour (col);
        const float s = juce::jmin (b.getWidth(), b.getHeight());
        b = b.withSizeKeepingCentre (s, s);
        const float lw = juce::jmax (1.3f, s * 0.11f);
        const float x = b.getX(), y = b.getY();
        const auto ctr = b.getCentre();
        const float pi2 = juce::MathConstants<float>::twoPi;
        switch (c)
        {
            case 0x2699:   // réglages : roue dentée
            {
                juce::Path p;
                const int teeth = 8;
                const float R = s * 0.49f, r = s * 0.36f;
                for (int i = 0; i < teeth; ++i)
                {
                    const float a = (float) i * pi2 / (float) teeth;
                    const float pts[4][2] = { { r, a - 0.36f }, { R, a - 0.17f }, { R, a + 0.17f }, { r, a + 0.36f } };
                    for (int k = 0; k < 4; ++k)
                    {
                        const juce::Point<float> q (ctr.x + pts[k][0] * std::sin (pts[k][1]), ctr.y - pts[k][0] * std::cos (pts[k][1]));
                        if (i == 0 && k == 0) p.startNewSubPath (q); else p.lineTo (q);
                    }
                }
                p.closeSubPath();
                p.addEllipse (juce::Rectangle<float> (s * 0.30f, s * 0.30f).withCentre (ctr));
                p.setUsingNonZeroWinding (false);
                g.fillPath (p);
                break;
            }
            case 0x29C9:   // détacher : deux fenêtres qui se chevauchent
            {
                const juce::Rectangle<float> back (x + s * 0.32f, y + s * 0.06f, s * 0.60f, s * 0.60f);
                juce::Rectangle<float> front (x + s * 0.08f, y + s * 0.34f, s * 0.60f, s * 0.60f);
                g.drawRoundedRectangle (back, s * 0.10f, lw);
                g.drawRoundedRectangle (front, s * 0.10f, lw);
                g.fillRect (front.removeFromTop (s * 0.16f).reduced (lw * 0.5f, 0.0f));   // barre de titre de la fenêtre détachée
                break;
            }
            case 0x2702:   // ciseaux (couper)
            {
                const float rr = s * 0.15f;
                g.drawEllipse (juce::Rectangle<float> (rr * 2, rr * 2).withCentre ({ x + s * 0.22f, y + s * 0.27f }), lw);
                g.drawEllipse (juce::Rectangle<float> (rr * 2, rr * 2).withCentre ({ x + s * 0.22f, y + s * 0.73f }), lw);
                g.drawLine (x + s * 0.34f, y + s * 0.35f, x + s * 0.96f, y + s * 0.72f, lw);
                g.drawLine (x + s * 0.34f, y + s * 0.65f, x + s * 0.96f, y + s * 0.28f, lw);
                break;
            }
            case 0x2605:   // étoile (presets)
            {
                juce::Path p;
                p.addStar (ctr, 5, s * 0.22f, s * 0.50f, 0.0f);
                g.fillPath (p);
                break;
            }
            case 0x25A3:   // sauver : disquette
            {
                juce::Path p;
                const float k = s * 0.14f;
                p.startNewSubPath (x + s * 0.1f, y + s * 0.1f); p.lineTo (x + s * 0.74f, y + s * 0.1f); p.lineTo (x + s * 0.9f, y + s * 0.26f);
                p.lineTo (x + s * 0.9f, y + s * 0.9f); p.lineTo (x + s * 0.1f, y + s * 0.9f); p.closeSubPath();
                g.strokePath (p, juce::PathStrokeType (lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.fillRect (juce::Rectangle<float> (x + s * 0.28f, y + s * 0.1f, s * 0.36f, s * 0.24f));   // volet
                g.drawRoundedRectangle (juce::Rectangle<float> (x + s * 0.24f, y + s * 0.54f, s * 0.52f, s * 0.36f), k * 0.3f, lw * 0.8f);   // étiquette
                break;
            }
            case 0x25CF: g.fillEllipse (b.reduced (s * 0.2f)); break;                  // enregistrer
            case 0x25A0: g.fillRoundedRectangle (b.reduced (s * 0.24f), s * 0.06f); break;   // stop
            case 0x25B6: case 0x25C0:                                                  // flèches
            {
                juce::Path p;
                if (c == 0x25B6) p.addTriangle (x + s * 0.25f, y + s * 0.18f, x + s * 0.25f, y + s * 0.82f, x + s * 0.82f, y + s * 0.5f);
                else             p.addTriangle (x + s * 0.75f, y + s * 0.18f, x + s * 0.75f, y + s * 0.82f, x + s * 0.18f, y + s * 0.5f);
                g.fillPath (p);
                break;
            }
            default: break;
        }
    }
}

class VariantLookAndFeel : public juce::LookAndFeel_V4
{
public:
    VariantLookAndFeel() { refreshColours(); }

    void refreshColours()
    {
        setColour (juce::ResizableWindow::backgroundColourId, Palette::bg);
        setColour (juce::Label::textColourId, Palette::text);
        setColour (juce::TextButton::buttonColourId, Palette::panel2);
        setColour (juce::TextButton::buttonOnColourId, Palette::blue);
        setColour (juce::TextButton::textColourOffId, Palette::text);
        setColour (juce::TextButton::textColourOnId, Palette::light ? juce::Colours::white : Palette::bg);
        setColour (juce::Slider::textBoxTextColourId, Palette::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::ToggleButton::textColourId, Palette::text);
        setColour (juce::TooltipWindow::backgroundColourId, Palette::panel2);
        setColour (juce::TooltipWindow::textColourId, Palette::text);
        setColour (juce::TooltipWindow::outlineColourId, Palette::line);
        setColour (juce::ComboBox::backgroundColourId, Palette::panel2);
        setColour (juce::ComboBox::textColourId, Palette::text);
        setColour (juce::ComboBox::outlineColourId, Palette::line);
        setColour (juce::ComboBox::arrowColourId, Palette::dim);
        setColour (juce::PopupMenu::backgroundColourId, Palette::panel.withAlpha (0.99f));   // non opaque : coins arrondis
        setColour (juce::PopupMenu::textColourId, Palette::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Palette::blue.withAlpha (0.25f));
        setColour (juce::PopupMenu::highlightedTextColourId, Palette::text);
        setColour (juce::PopupMenu::headerTextColourId, Palette::dim);
        setColour (juce::ListBox::textColourId, Palette::text);
        setColour (juce::ScrollBar::thumbColourId, Palette::line.brighter (0.3f));
    }


    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float a0, float a1, juce::Slider& s) override
    {
        const auto b      = juce::Rectangle<int> (x, y, w, h).toFloat();
        const auto c      = b.getCentre();
        const float rad   = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f - 3.0f;
        const float st    = rad > 30.0f ? 4.5f : 3.5f;
        const float arcR  = rad - st;
        const auto accent = s.isEnabled() ? s.findColour (juce::Slider::rotarySliderFillColourId) : Palette::dim;

        juce::Path track;
        track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, a0, a1, true);
        g.setColour (Palette::line);
        g.strokePath (track, juce::PathStrokeType (st, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float ang = a0 + pos * (a1 - a0);
        float from = a0;
        if (s.getMinimum() < 0.0 && s.getMaximum() > 0.0)
            from = a0 + (float) s.valueToProportionOfLength (0.0) * (a1 - a0);

        if (std::abs (ang - from) > 0.001f)
        {
            juce::Path v;
            v.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, juce::jmin (from, ang), juce::jmax (from, ang), true);
            g.setColour (accent.withAlpha (0.18f));   // halo doux
            g.strokePath (v, juce::PathStrokeType (st * 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (accent);
            g.strokePath (v, juce::PathStrokeType (st, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        const float bodyR = arcR - (rad > 30.0f ? 8.0f : 6.0f);
        juce::ColourGradient body (Palette::panel2.brighter (Palette::light ? 0.02f : 0.12f), c.x, c.y - bodyR,
                                   Palette::panel2.darker (Palette::light ? 0.04f : 0.15f), c.x, c.y + bodyR, false);
        g.setGradientFill (body);
        g.fillEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
        g.setColour (Palette::line.withAlpha (0.8f));
        g.drawEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);

        g.setColour (accent);
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (c.getPointOnCircumference (bodyR - 6.0f, ang)));

        g.setColour (Palette::text.withAlpha (s.isEnabled() ? 0.92f : 0.35f));
        g.setFont (uiFont (rad > 30.0f ? 13.0f : 12.5f));
        g.drawFittedText (s.getTextFromValue (s.getValue()),
                          juce::Rectangle<float> (bodyR * 1.8f, 18.0f).withCentre (c.translated (0.0f, 3.0f) + Vibe::offsetFor (&s)).toNearestInt(),
                          juce::Justification::centred, 1, 0.6f);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                               bool over, bool down) override
    {
        // style moderne : pilule arrondie, verre sombre quand éteint, dégradé lumineux + halo quand allumé,
        // le bouton se resserre légèrement au clic
        auto r = b.getLocalBounds().toFloat().reduced (2.0f);
        if (down) r = r.reduced (1.0f);
        const bool on = b.getToggleState();
        const auto accent = b.findColour (juce::TextButton::buttonOnColourId);
        const auto base = b.findColour (juce::TextButton::buttonColourId);
        const float radius = juce::jmin (r.getHeight() * 0.5f, 12.0f);

        if (on)
        {
            for (int k = 3; k >= 1; --k)   // halo doux autour
            {
                g.setColour (accent.withAlpha (0.07f * (float) (4 - k)));
                g.fillRoundedRectangle (r.expanded ((float) k * 1.2f), radius + (float) k * 1.2f);
            }
            const auto a2 = accent.withRotatedHue (-0.06f).brighter (0.15f);
            g.setGradientFill (juce::ColourGradient (a2.brighter (over ? 0.12f : 0.0f), r.getX(), r.getY(),
                                                     accent.darker (0.12f), r.getRight(), r.getBottom(), false));
            g.fillRoundedRectangle (r, radius);
            g.setColour (juce::Colours::white.withAlpha (0.28f));
            g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
        }
        else
        {
            const auto top = base.brighter (Palette::light ? 0.03f : (over ? 0.16f : 0.09f));
            g.setGradientFill (juce::ColourGradient (top, r.getX(), r.getY(), base.darker (Palette::light ? 0.02f : 0.08f), r.getX(), r.getBottom(), false));
            g.fillRoundedRectangle (r, radius);
            // fin liseré : clair en haut (reflet de verre), accent au survol
            g.setColour (over ? accent.withAlpha (0.75f) : Palette::text.withAlpha (Palette::light ? 0.22f : 0.12f));
            g.drawRoundedRectangle (r.reduced (0.5f), radius, over ? 1.3f : 1.0f);
            if (! Palette::light)
            {
                g.setColour (juce::Colours::white.withAlpha (0.06f));
                g.drawLine (r.getX() + radius, r.getY() + 1.0f, r.getRight() - radius, r.getY() + 1.0f, 1.0f);
            }
        }
    }

    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return uiFont (juce::jmin (14.0f, h * 0.48f)); }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool) override
    {
        auto r = b.getLocalBounds().toFloat();
        const auto sw = r.removeFromLeft (44.0f).withSizeKeepingCentre (38.0f, 20.0f);
        const bool on = b.getToggleState();
        const auto accent = b.isColourSpecified (juce::ToggleButton::tickColourId)
                              ? b.findColour (juce::ToggleButton::tickColourId) : Palette::mint;

        g.setColour (on ? accent : (over ? Palette::panel2.brighter (0.1f) : Palette::panel2));
        g.fillRoundedRectangle (sw, 10.0f);
        g.setColour (on ? accent.brighter (0.3f) : Palette::text.withAlpha (over ? 0.6f : 0.3f));
        g.drawRoundedRectangle (sw, 10.0f, 1.0f);

        const float d  = 14.0f;
        const float kx = on ? sw.getRight() - d - 3.0f : sw.getX() + 3.0f;
        g.setColour (on ? (Palette::light ? juce::Colours::white : Palette::bg) : Palette::dim);
        g.fillEllipse (kx, sw.getCentreY() - d * 0.5f, d, d);

        g.setColour (on ? Palette::text : Palette::dim);
        // mots longs (allemand, russe…) : la police rétrécit puis le texte passe sur 2 lignes, jamais coupé par « … »
        const auto text = b.getButtonText();
        const auto area = r.withTrimmedLeft (6.0f) + Vibe::offsetFor (&b);
        float fs = 14.0f;
        while (fs > 10.0f && (float) juce::GlyphArrangement::getStringWidthInt (uiFont (fs), text) > area.getWidth()) fs -= 0.5f;
        g.setFont (uiFont (fs));
        const int lines = area.getHeight() >= fs * 2.1f ? 2 : 1;
        g.drawFittedText (text, area.toNearestInt(), juce::Justification::centredLeft, lines, 0.7f);
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool down, int, int, int, int, juce::ComboBox& box) override
    {
        const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w - 2.0f, (float) h - 2.0f).reduced (0.5f);
        const bool over = box.isMouseOver (true);
        const float rad = juce::jmin (7.0f, r.getHeight() * 0.3f);
        // fond légèrement creusé (comme un champ), bordure qui s'allume au survol
        g.setGradientFill (juce::ColourGradient (Palette::panel2.darker (Palette::light ? 0.02f : 0.18f), 0.0f, 0.0f,
                                                 Palette::panel2.brighter (over ? 0.10f : 0.03f), 0.0f, (float) h, false));
        g.fillRoundedRectangle (r, rad);
        g.setColour (down || box.hasKeyboardFocus (true) ? Palette::blue : (over ? Palette::text.withAlpha (0.7f) : Palette::text.withAlpha (0.3f)));
        g.drawRoundedRectangle (r, rad, 1.0f);

        // bouton flèche : pastille de la couleur du thème à droite
        auto btn = juce::Rectangle<float> ((float) w - 26.0f, 3.0f, 20.0f, (float) h - 8.0f);
        g.setColour (Palette::blue.withAlpha (over || down ? 0.30f : 0.16f));
        g.fillRoundedRectangle (btn, juce::jmin (6.0f, btn.getHeight() * 0.4f));
        juce::Path arrow;
        const float ax = btn.getCentreX(), ay = btn.getCentreY() + (down ? 1.0f : 0.0f);
        arrow.startNewSubPath (ax - 4.0f, ay - 1.8f);
        arrow.lineTo (ax, ay + 2.2f);
        arrow.lineTo (ax + 4.0f, ay - 1.8f);
        g.setColour (over || down ? Palette::text : Palette::blue);
        g.strokePath (arrow, juce::PathStrokeType (1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return uiFont (14.0f); }
    juce::Font getPopupMenuFont() override                  { return uiFont (14.0f); }
    juce::Font getLabelFont (juce::Label& l) override       { return uiFont (juce::jmax (12.5f, l.getFont().getHeight())); }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (10, 1, box.getWidth() - 36, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }

    // textes : vibrent légèrement avec les basses (voir Vibe)
    void drawLabel (juce::Graphics& g, juce::Label& label) override
    {
        g.fillAll (label.findColour (juce::Label::backgroundColourId));
        if (! label.isBeingEdited())
        {
            const float alpha = label.isEnabled() ? 1.0f : 0.5f;
            g.setColour (label.findColour (juce::Label::textColourId).withMultipliedAlpha (alpha));
            g.setFont (getLabelFont (label));
            auto textArea = getLabelBorderSize (label).subtractedFrom (label.getLocalBounds()).toFloat() + Vibe::offsetFor (&label);
            g.drawFittedText (label.getText(), textArea.toNearestInt(), label.getJustificationType(),
                              juce::jmax (1, (int) ((float) textArea.getHeight() / g.getCurrentFont().getHeight())), label.getMinimumHorizontalScale());
            g.setColour (label.findColour (juce::Label::outlineColourId).withMultipliedAlpha (alpha));
        }
        else if (label.isEnabled())
            g.setColour (label.findColour (juce::Label::outlineColourId));
        g.drawRect (label.getLocalBounds());
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool) override
    {
        const auto font = getTextButtonFont (button, button.getHeight());
        g.setFont (font);
        const auto textCol = button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId)
                         .withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f);
        g.setColour (textCol);
        const int yIndent = juce::jmin (4, button.proportionOfHeight (0.3f));
        const int cornerSize = juce::jmin (button.getHeight(), button.getWidth()) / 2;
        const int fontHeight = juce::roundToInt (font.getHeight() * 0.6f);
        const int leftIndent = juce::jmin (fontHeight, 2 + cornerSize / (button.isConnectedOnLeft() ? 4 : 2));
        const int rightIndent = juce::jmin (fontHeight, 2 + cornerSize / (button.isConnectedOnRight() ? 4 : 2));
        const int textWidth = button.getWidth() - leftIndent - rightIndent;
        if (textWidth > 0)
        {
            auto off = Vibe::offsetFor (&button) + (button.isDown() ? juce::Point<float> (0.0f, 0.5f) : juce::Point<float>());
            const auto area = juce::Rectangle<int> (leftIndent, yIndent, textWidth, button.getHeight() - yIndent * 2).translated (juce::roundToInt (off.x), juce::roundToInt (off.y));
            const auto text = button.getButtonText();
            if (text.isNotEmpty() && icons::isIcon (text[0]))
            {
                // symbole dessiné en vectoriel (roue, ciseaux, étoile…) au lieu d'un emoji
                const auto rest = text.substring (1).trimStart();
                const float isz = juce::jmin ((float) area.getHeight(), font.getHeight() * 1.15f);
                const float tw = rest.isEmpty() ? 0.0f : (float) juce::GlyphArrangement::getStringWidthInt (font, rest);
                const float gap = rest.isEmpty() ? 0.0f : isz * 0.4f;
                const float total = juce::jmin ((float) area.getWidth(), isz + gap + tw);
                auto row = area.toFloat().withSizeKeepingCentre (total, (float) area.getHeight());
                icons::draw (g, text[0], row.removeFromLeft (isz).withSizeKeepingCentre (isz, isz), textCol);
                g.setColour (textCol);
                if (rest.isNotEmpty()) { row.removeFromLeft (gap); g.drawFittedText (rest, row.toNearestInt(), juce::Justification::centredLeft, 1); }
                return;
            }
            g.drawFittedText (text, area, juce::Justification::centred, 2);
        }
    }

    // menus (clic droit, listes) dans le style du plugin : carte arrondie aux couleurs du thème,
    // plus de liste « Windows » grise
    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
        g.setGradientFill (juce::ColourGradient (Palette::panel2, 0, 0, Palette::panel, 0, (float) h, false));
        g.fillRoundedRectangle (r, 10.0f);
        // liseré de couleur en haut (dégradé du thème)
        juce::ColourGradient top (Palette::blue, r.getX(), 0, Palette::pink, r.getRight(), 0, false);
        top.addColour (0.5, Palette::lavender);
        g.saveState();
        juce::Path clip; clip.addRoundedRectangle (r, 10.0f);
        g.reduceClipRegion (clip);
        g.setGradientFill (top);
        g.fillRect (r.withHeight (3.0f));
        g.restoreState();
        g.setColour (Palette::text.withAlpha (Palette::light ? 0.16f : 0.12f));
        g.drawRoundedRectangle (r, 10.0f, 1.0f);
    }

    void drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& shortcut,
                            const juce::Drawable* icon, const juce::Colour* textColour) override
    {
        if (isSeparator)
        {
            g.setColour (Palette::line.withAlpha (0.8f));
            g.fillRect (area.reduced (12, 0).withSizeKeepingCentre (area.getWidth() - 24, 1));
            return;
        }
        auto r = area.reduced (5, 1).toFloat();
        if (isHighlighted && isActive)
        {
            g.setGradientFill (juce::ColourGradient (Palette::blue.withAlpha (0.34f), r.getX(), 0, Palette::blue.withAlpha (0.10f), r.getRight(), 0, false));
            g.fillRoundedRectangle (r, 7.0f);
        }
        auto t = area.reduced (14, 0);
        // pastille à gauche : cochée = pleine, survolée = contour
        auto dot = juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ (float) t.getX() + 4.0f, (float) t.getCentreY() });
        if (isTicked) { g.setColour (Palette::mint); g.fillEllipse (dot); }
        else if (isHighlighted && isActive) { g.setColour (Palette::blue.withAlpha (0.8f)); g.drawEllipse (dot.reduced (1.0f), 1.3f); }
        t.removeFromLeft (16);
        if (icon != nullptr)   // aperçu (thème, skin de Kick…) avant le nom
        {
            auto ia = t.removeFromLeft (t.getHeight() * 3 / 2 + 6).reduced (0, 2).toFloat();
            icon->drawWithin (g, ia.withTrimmedRight (6.0f), juce::RectanglePlacement::centred, 1.0f);
        }
        auto ink = textColour != nullptr ? *textColour : Palette::text;
        g.setColour (isActive ? (isHighlighted ? ink : ink.withAlpha (0.86f)) : Palette::dim.withAlpha (0.5f));
        g.setFont (getPopupMenuFont());
        if (hasSubMenu)
        {
            auto a = t.removeFromRight (14).toFloat();
            juce::Path p; p.startNewSubPath (a.getX() + 4, a.getCentreY() - 3.5f); p.lineTo (a.getX() + 7.5f, a.getCentreY()); p.lineTo (a.getX() + 4, a.getCentreY() + 3.5f);
            g.setColour (isHighlighted ? Palette::blue : Palette::dim);
            g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (isActive ? (isHighlighted ? ink : ink.withAlpha (0.86f)) : Palette::dim.withAlpha (0.5f));
        }
        if (shortcut.isNotEmpty()) { g.setColour (Palette::dim); g.drawText (shortcut, t, juce::Justification::centredRight); g.setColour (ink); }
        g.drawFittedText (text, t, juce::Justification::centredLeft, 1);
    }

    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight, int& idealWidth, int& idealHeight) override
    {
        juce::LookAndFeel_V4::getIdealPopupMenuItemSize (text, isSeparator, standardMenuItemHeight, idealWidth, idealHeight);
        if (isSeparator) idealHeight = 9;
        else { idealHeight = juce::jmax (idealHeight, 30); idealWidth += 70; }   // place pour un aperçu éventuel
    }

    void drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& name) override
    {
        g.setColour (Palette::blue);
        g.setFont (uiFont (11.5f, juce::Font::bold).withExtraKerningFactor (0.08f));
        g.drawFittedText (name.toUpperCase(), area.reduced (16, 0), juce::Justification::bottomLeft, 1);
    }

    int getPopupMenuBorderSize() override { return 6; }

    void drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                        int thumbStart, int thumbSize, bool over, bool) override
    {
        auto thumb = vertical ? juce::Rectangle<int> (x, thumbStart, w, thumbSize) : juce::Rectangle<int> (thumbStart, y, thumbSize, h);
        g.setColour (Palette::text.withAlpha (over ? 0.35f : 0.18f));
        g.fillRoundedRectangle (thumb.toFloat().reduced (2.0f), 3.0f);
    }

    void drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h) override
    {
        g.setColour (Palette::panel2);
        g.fillRoundedRectangle (0, 0, (float) w, (float) h, 6.0f);
        g.setColour (Palette::line);
        g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1, (float) h - 1, 6.0f, 1.0f);
        g.setColour (Palette::text);
        g.setFont (uiFont (13.5f));
        g.drawFittedText (text, juce::Rectangle<int> (8, 4, w - 16, h - 8), juce::Justification::centredLeft, 6);
    }

    juce::Rectangle<int> getTooltipBounds (const juce::String& tip, juce::Point<int> pos, juce::Rectangle<int> parent) override
    {
        const int w = juce::jmin (420, 24 + (int) (tip.length() * 7.2f));
        const int lines = juce::jmax (1, (int) std::ceil (tip.length() * 7.2f / 400.0f)) + tip.retainCharacters ("\n").length();
        const int h = 14 + lines * 19;
        return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 18,
                                     pos.y > parent.getCentreY() ? pos.y - (h + 12) : pos.y + 18, w, h).constrainedWithin (parent);
    }
};

// Carte arrondie avec ombre douce, un titre et un point de couleur
inline void drawCard (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title, juce::Colour accent)
{
    const auto rf = r.toFloat();
    juce::Path p;
    p.addRoundedRectangle (rf, 14.0f);
    juce::DropShadow (juce::Colours::black.withAlpha (Palette::light ? 0.06f : 0.30f), 14, { 0, 4 }).drawForPath (g, p);
    g.setColour (Palette::panel);
    g.fillPath (p);
    g.setColour (Palette::line.withAlpha (0.8f));
    g.strokePath (p, juce::PathStrokeType (1.0f));

    if (title.isEmpty()) return;
    auto t = r.reduced (16, 10).removeFromTop (20);
    g.setColour (accent);
    g.fillRoundedRectangle (juce::Rectangle<float> (4.0f, 14.0f).withCentre ({ (float) t.getX() + 2.0f, (float) t.getCentreY() }), 2.0f);
    g.setColour (Palette::text.withAlpha (0.65f));
    g.setFont (uiFont (12.5f, juce::Font::bold).withExtraKerningFactor (0.08f));
    g.drawText (title, t.withTrimmedLeft (12).toFloat() + Vibe::offsetFor (nullptr, (float) (r.getX() + r.getY() * 3) * 0.1f), juce::Justification::centredLeft, true);
}

// Les menus (clic droit, listes) s'ouvrent dans leur propre fenêtre : sans ça ils prennent le style Windows/JUCE par défaut.
// Un seul style partagé par toutes les fenêtres du plugin, installé comme style par défaut tant qu'une fenêtre est ouverte.
struct GlobalLookAndFeel
{
    GlobalLookAndFeel() { juce::LookAndFeel::setDefaultLookAndFeel (&lnf); }
    ~GlobalLookAndFeel() { juce::LookAndFeel::setDefaultLookAndFeel (nullptr); }
    VariantLookAndFeel lnf;
};
