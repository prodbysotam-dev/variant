#pragma once
// OBLITERATE : bouton bombe + explosion pixel art qui remet tout le plugin à zéro
#include "LookAndFeel.h"

// Petite bombe en pixels. 1er clic = armée (3 s), 2e clic = boum.
class ObliterateButton : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    ObliterateButton()
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        startTimerHz (20);
    }

    std::function<void()> onArm, onFire;
    bool isArmed() const { return armedFor > 0.0f; }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! getLocalBounds().contains (e.getPosition())) return;
        if (isArmed()) { armedFor = 0.0f; if (onFire) onFire(); }
        else           { armedFor = 3.0f; if (onArm) onArm(); }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (1.0f);
        const bool armed = isArmed();
        const float pulse = 0.5f + 0.5f * std::sin (t * 18.0f);
        g.setColour (armed ? juce::Colour (0xffff3b3b).withAlpha (0.35f + 0.4f * pulse)
                           : (isMouseOver() ? Palette::panel2.brighter (0.1f) : Palette::panel2));
        g.fillRoundedRectangle (b, 8.0f);
        g.setColour (armed ? juce::Colour (0xffff3b3b) : Palette::line);
        g.drawRoundedRectangle (b, 8.0f, 1.0f);

        // bombe 9x9 pixels
        static const char* bomb[] = {
            "......#..",
            ".....#...",
            "...###...",
            "..#####..",
            ".###o###.",
            ".##o####.",
            ".#######.",
            "..#####..",
            "...###...." };
        const float px = juce::jmin (b.getWidth(), b.getHeight()) / 13.0f;
        const float ox = b.getCentreX() - 4.5f * px, oy = b.getCentreY() - 4.2f * px;
        for (int y = 0; y < 9; ++y)
            for (int x = 0; x < 9; ++x)
            {
                const char c = bomb[y][x];
                if (c == '.') continue;
                juce::Colour col = c == 'o' ? juce::Colours::white.withAlpha (0.8f) : (Palette::light ? juce::Colour (0xff1c1c22) : juce::Colour (0xff0a0a0c));
                if (y < 2) col = juce::Colour (0xff9a7b5a);   // mèche
                g.setColour (col);
                g.fillRect (ox + x * px, oy + y * px, px + 0.3f, px + 0.3f);
            }
        // étincelle
        const juce::Colour spark = (int) (t * 12.0f) % 2 ? juce::Colour (0xffffd84a) : juce::Colour (0xffff7a2f);
        g.setColour (spark);
        const float sx = ox + 7.0f * px, sy = oy - 1.0f * px;
        g.fillRect (sx, sy, px, px);
        if (armed || (int) (t * 6.0f) % 3 == 0)
        {
            g.fillRect (sx - px, sy, px * 0.8f, px * 0.8f);
            g.fillRect (sx + px, sy - px, px * 0.8f, px * 0.8f);
        }
    }

private:
    void timerCallback() override
    {
        t += 0.05f;
        if (armedFor > 0.0f) armedFor = juce::jmax (0.0f, armedFor - 0.05f);
        repaint();
    }

    float t = 0.0f, armedFor = 0.0f;
};

// Calque d'explosion : onde de choc en gros pixels, débris, flash, "OBLITERATED"
class ObliterateOverlay : public juce::Component, private juce::Timer
{
public:
    ObliterateOverlay() { setInterceptsMouseClicks (true, true); setVisible (false); }

    std::function<void()> onPeak, onDone;
    std::function<void (float, float)> onShake;

    void start (juce::Point<float> from)
    {
        centre = from;
        t = 0.0f;
        peaked = false;
        debris.clear();
        for (int i = 0; i < 90; ++i)
        {
            const float a = rng.nextFloat() * juce::MathConstants<float>::twoPi;
            const float sp = 200.0f + rng.nextFloat() * 650.0f;
            debris.push_back ({ centre.x, centre.y, std::cos (a) * sp, std::sin (a) * sp - 200.0f,
                                (float) (2 + rng.nextInt (4)) * 4.0f, rng.nextInt (4) });
        }
        setVisible (true);
        toFront (false);
        startTimerHz (60);
    }

    void paint (juce::Graphics& g) override
    {
        const float W = (float) getWidth(), H = (float) getHeight();
        const float cell = 12.0f;
        const juce::Colour fire[] = { juce::Colour (0xffffffff), juce::Colour (0xfffff3a0), juce::Colour (0xffffc23a),
                                      juce::Colour (0xffff7a1f), juce::Colour (0xffe8322a), juce::Colour (0xff6b1a3a) };
        const juce::Colour smoke (0xff1a1026);

        // 1. onde de choc pixel
        const float R = t < 0.05f ? 0.0f : (t - 0.05f) * 1600.0f;
        const float dissolve = juce::jlimit (0.0f, 1.0f, (t - 0.55f) / 0.9f);   // la fumée se dissout et révèle le plugin neuf
        for (float y = 0.0f; y < H; y += cell)
            for (float x = 0.0f; x < W; x += cell)
            {
                const float d = juce::Point<float> (x + cell * 0.5f, y + cell * 0.5f).getDistanceFrom (centre);
                if (d > R) continue;
                const float behind = R - d;
                const juce::uint32 h = hash ((int) (x / cell), (int) (y / cell));
                const float noise = (float) (h % 1000) / 1000.0f;
                if (noise < dissolve * 1.15f) continue;
                juce::Colour c;
                const float band = behind + noise * 70.0f;
                if (band < 40.0f)       c = fire[0];
                else if (band < 90.0f)  c = fire[1];
                else if (band < 150.0f) c = fire[2];
                else if (band < 230.0f) c = fire[3];
                else if (band < 330.0f) c = fire[4];
                else if (band < 450.0f) c = fire[5];
                else                    c = smoke.withAlpha (0.92f);
                g.setColour (c);
                g.fillRect (x, y, cell, cell);
            }

        // 2. flash blanc au moment du reset
        const float flash = juce::jlimit (0.0f, 1.0f, 1.0f - std::abs (t - 0.38f) / 0.16f);
        if (flash > 0.0f) { g.setColour (juce::Colours::white.withAlpha (flash)); g.fillAll(); }

        // 3. débris
        for (auto& p : debris)
        {
            const float a = juce::jlimit (0.0f, 1.0f, 1.6f - t);
            if (a <= 0.0f) continue;
            g.setColour (fire[1 + p.colour].withAlpha (a));
            g.fillRect (std::round (p.x / 4.0f) * 4.0f, std::round (p.y / 4.0f) * 4.0f, p.size, p.size);
        }

        // 4. texte pixel
        if (t > 0.45f && t < 1.75f && ((int) (t * 14.0f) % 5 != 0 || t < 0.9f))
        {
            const float a = juce::jlimit (0.0f, 1.0f, (1.75f - t) * 3.0f);
            drawPixelText (g, "OBLITERATED", { W * 0.5f, H * 0.45f }, 9.0f, juce::Colour (0xffff3b6b).withAlpha (a), juce::Colours::black.withAlpha (a));
        }
    }

private:
    struct Debris { float x, y, vx, vy, size; int colour; };

    static juce::uint32 hash (int x, int y)
    {
        juce::uint32 h = (juce::uint32) (x * 73856093) ^ (juce::uint32) (y * 19349663);
        h ^= h >> 13; h *= 0x5bd1e995; h ^= h >> 15;
        return h;
    }

    static void drawPixelText (juce::Graphics& g, const char* text, juce::Point<float> centre, float px, juce::Colour col, juce::Colour shadow)
    {
        auto glyph = [] (char c) -> const char* const*
        {
            static const char* O[] = { " ### ", "#   #", "#   #", "#   #", "#   #", "#   #", " ### " };
            static const char* B[] = { "#### ", "#   #", "#   #", "#### ", "#   #", "#   #", "#### " };
            static const char* L[] = { "#    ", "#    ", "#    ", "#    ", "#    ", "#    ", "#####" };
            static const char* I[] = { "#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "#####" };
            static const char* T[] = { "#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  " };
            static const char* E[] = { "#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#####" };
            static const char* R[] = { "#### ", "#   #", "#   #", "#### ", "# #  ", "#  # ", "#   #" };
            static const char* A[] = { " ### ", "#   #", "#   #", "#####", "#   #", "#   #", "#   #" };
            static const char* D[] = { "#### ", "#   #", "#   #", "#   #", "#   #", "#   #", "#### " };
            switch (c) { case 'O': return O; case 'B': return B; case 'L': return L; case 'I': return I; case 'T': return T;
                         case 'E': return E; case 'R': return R; case 'A': return A; default: return D; }
        };
        const int n = (int) std::strlen (text);
        const float w = (float) n * 6.0f * px - px;
        const float x0 = centre.x - w * 0.5f, y0 = centre.y - 3.5f * px;
        for (int pass = 0; pass < 2; ++pass)
        {
            g.setColour (pass == 0 ? shadow : col);
            const float off = pass == 0 ? px * 0.6f : 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const auto* gl = glyph (text[i]);
                for (int y = 0; y < 7; ++y)
                    for (int x = 0; x < 5; ++x)
                        if (gl[y][x] == '#') g.fillRect (x0 + ((float) i * 6.0f + (float) x) * px + off, y0 + (float) y * px + off, px, px);
            }
        }
    }

    void timerCallback() override
    {
        const float dt = 1.0f / 60.0f;
        t += dt;
        for (auto& p : debris) { p.x += p.vx * dt; p.y += p.vy * dt; p.vy += 900.0f * dt; }
        const float shake = t < 0.7f ? (0.7f - t) * 14.0f : 0.0f;
        if (onShake) onShake ((rng.nextFloat() - 0.5f) * shake, (rng.nextFloat() - 0.5f) * shake);
        if (! peaked && t >= 0.38f) { peaked = true; if (onPeak) onPeak(); toFront (false); }
        if (t >= 1.9f)
        {
            stopTimer();
            setVisible (false);
            if (onShake) onShake (0.0f, 0.0f);
            if (onDone) onDone();
            return;
        }
        repaint();
    }

    juce::Point<float> centre;
    std::vector<Debris> debris;
    juce::Random rng;
    float t = 0.0f;
    bool peaked = false;
};
