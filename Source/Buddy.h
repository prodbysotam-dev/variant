#pragma once
// "Kick" : petit skateur esprit Y2K qui explique ce qu'il y a sous la souris (à la manière de Clippy)
#include "LookAndFeel.h"

class BuddyBubble : public juce::Component
{
public:
    BuddyBubble() { setInterceptsMouseClicks (false, false); }

    void setText (const juce::String& title, const juce::String& body)
    {
        heading = title; text = body;
        repaint();
    }

    // taille idéale pour une largeur donnée
    int idealHeight (int width) const
    {
        juce::AttributedString as = build();
        juce::TextLayout tl;
        tl.createLayout (as, (float) (width - 28));
        return (int) std::ceil (tl.getHeight()) + 26 + 10;   // + pointe
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (1.5f);
        b.removeFromBottom (10.0f);
        juce::Path p;
        p.addRoundedRectangle (b, 14.0f);
        juce::Path tail;   // pointe vers le bonhomme (en bas à droite)
        tail.addTriangle (b.getRight() - 46.0f, b.getBottom() - 1.0f, b.getRight() - 22.0f, b.getBottom() - 1.0f, b.getRight() - 18.0f, b.getBottom() + 10.0f);
        p.addPath (tail);

        g.setColour (juce::Colours::black.withAlpha (Palette::light ? 0.10f : 0.35f));
        g.fillPath (p, juce::AffineTransform::translation (0.0f, 3.0f));
        g.setColour (Palette::panel);
        g.fillPath (p);
        g.setColour (Palette::y2k ? Palette::pink : Palette::lavender.withAlpha (0.8f));
        g.strokePath (p, juce::PathStrokeType (Palette::y2k ? 2.0f : 1.2f));

        juce::TextLayout tl;
        tl.createLayout (build(), b.getWidth() - 28.0f);
        tl.draw (g, b.reduced (14.0f, 12.0f));
    }

private:
    juce::AttributedString build() const
    {
        juce::AttributedString as;
        if (heading.isNotEmpty()) as.append (heading + "\n", uiFont (13.5f, juce::Font::bold), Palette::y2k ? Palette::pink : Palette::lavender);
        as.append (text, uiFont (13.0f), Palette::text);
        as.setWordWrap (juce::AttributedString::byWord);
        as.setLineSpacing (2.0f);
        return as;
    }

    juce::String heading, text;
};

// Kick en pixel art : chaque personnage est dessiné sur une grille 32 x 52 (silhouette + détails fins),
// avec un contour automatique, une animation d'attente et une pose "action" + ses répliques.
struct BuddySkin
{
    const char* name;
    std::vector<const char*> quotes;
};

inline const std::vector<BuddySkin>& buddySkins()
{
    // répliques : les skins francophones disent tous la même phrase, les skins anglais gardent leurs phrases d'origine,
    // Tony Soprano dit juste « Gabagool ». (1 chance sur 100 : « TA GUEULE », 1 sur 200 : « GROS CON VA » — voir randomQuote)
    static const char* fr1 = "Ça dit quoi le sang ? Tranquille.";
    static const std::vector<BuddySkin> s {
        { "Jean-Luc Mélenchon", { fr1 } },
        { "Tony Soprano",       { "Gabagool" } },
        { "Orang-outan",        { fr1 } },
        { "Steve",              { fr1 } },
        { "Terroriste",         { "Bomb has been planted.", "Rush B, no stop!", "Go go go!" } },
        { "Anti-terroriste",    { "Counter-terrorists win.", "Fire in the hole!", "Bomb has been defused." } },
        { "Claude Nougaro",     { fr1 } },
        { "Patrick Saint-Éloi", { fr1 } },
        { "Wiz Khalifa",        { "Black and yellow!", "Work hard, play hard.", "We dem boyz!" } },
        { "Caballero",          { fr1 } },
        { "JeanJass",           { fr1 } },
        { "Cube Geometry",      { fr1 } },
        { "Sassy the Sasquatch", { "Oi!", "Yeah nah, mate.", "How ya goin', mate?" } },
        { "Aussie",             { "G'day mate!", "No worries, mate.", "Throw another shrimp on the barbie!" } },
    };
    return s;
}

// grille de pixels d'un personnage : 32 x 52 pixels fins.
// set/rect travaillent en "gros" pixels (2 x 2) pour la silhouette, f/fr en pixels fins pour les détails.
struct PixelGrid
{
    static constexpr int W = 32, H = 52;
    std::array<juce::uint32, W * H> c {};   // 0 = transparent
    void f (int x, int y, juce::uint32 col) { if (x >= 0 && x < W && y >= 0 && y < H) c[(size_t) (y * W + x)] = col; }
    void fr (int x, int y, int w, int h, juce::uint32 col) { for (int j = y; j < y + h; ++j) for (int i = x; i < x + w; ++i) f (i, j, col); }
    void set (int x, int y, juce::uint32 col) { fr (x * 2, y * 2, 2, 2, col); }
    void rect (int x, int y, int w, int h, juce::uint32 col) { fr (x * 2, y * 2, w * 2, h * 2, col); }
    juce::uint32 get (int x, int y) const { return (x >= 0 && x < W && y >= 0 && y < H) ? c[(size_t) (y * W + x)] : 0u; }
};

inline juce::uint32 shade (juce::uint32 c, float amount)   // > 0 éclaircit, < 0 assombrit
{
    const juce::Colour col (c);
    return (amount >= 0.0f ? col.brighter (amount) : col.darker (-amount)).getARGB();
}

class Buddy : public juce::Component, public juce::SettableTooltipClient
{
public:
    Buddy() { setRepaintsOnMouseActivity (true); }

    void setLookTarget (juce::Point<float> p) { look = p; }
    void setTalking (bool tk) { talking = tk; }
    void setSkin (int s) { skin = juce::jlimit (0, (int) buddySkins().size() - 1, s); repaint(); }
    int getSkin() const { return skin; }
    void act (float seconds = 1.6f) { actT = seconds; }   // pose d'action (réplique, clic…)
    // danse calée sur le tempo de FL (hors du plugin) : position en temps, lecture en cours, énergie (basses)
    void setDance (bool d) { dancing = d; }
    void setBeat (double ppq, bool playing, float level) { beatPos = ppq; beatPlaying = playing; energy = juce::jlimit (0.0f, 1.0f, level); }
    juce::String randomQuote()
    {
        const auto& q = buddySkins()[(size_t) skin].quotes;
        act();
        // n'importe quel personnage : 1 chance sur 200 « GROS CON VA », 1 chance sur 100 « TA GUEULE »
        const int r = juce::Random::getSystemRandom().nextInt (200);
        if (r == 0) return "GROS CON VA";
        if (r <= 2) return "TA GUEULE";
        return juce::String::fromUTF8 (q[(size_t) rng.nextInt ((int) q.size())]);
    }
    std::function<void()> onClick, onRightClick;

    void tick (float dt)
    {
        t += dt;
        if (actT > 0.0f) actT -= dt;
        blinkIn -= dt;
        if (blinkIn < -0.12f) blinkIn = 2.5f + rng.nextFloat() * 3.5f;
        idleActIn -= dt;
        if (idleActIn < 0.0f) { idleActIn = 6.0f + rng.nextFloat() * 8.0f; actT = juce::jmax (actT, 0.9f); }   // petite anim de temps en temps
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); }
        else if (onClick) onClick();
    }

    void paint (juce::Graphics& g) override
    {
        PixelGrid px;
        const bool beatOn = dancing && beatPlaying;
        const double ph = beatOn ? beatPos - std::floor (beatPos) : 0.0;
        // en dansant : pose "action" sur le premier temps de chaque mesure
        const bool action = actT > 0.0f || talking || (beatOn && std::fmod (beatPos, 4.0) < 0.5);
        const bool blink = blinkIn < 0.0f && ! action;
        const bool mouthOpen = talking && std::fmod (t * 8.0f, 1.0f) < 0.5f;
        {
            // les yeux suivent la souris (1 pixel fin à gauche / à droite)
            const auto centre = getLocalBounds().toFloat().getCentre();
            const auto lp = look;
            lookDx = look.isOrigin() ? 0 : (lp.x < centre.x - 40.0f ? -1 : (lp.x > centre.x + 40.0f ? 1 : 0));
        }
        draw (px, action, blink, mouthOpen);
        const auto shaded = light (px);

        const float cell = juce::jmin ((float) getWidth() / (float) PixelGrid::W, (float) getHeight() / (float) (PixelGrid::H + 2));
        const float ox = ((float) getWidth() - cell * PixelGrid::W) * 0.5f;
        const float bob = beatOn ? (float) std::pow (1.0 - ph, 2.0) * (2.0f + 6.0f * energy)   // saute sur chaque temps, plus haut si ça cogne
                                 : ((! action && std::fmod (t, 1.2f) < 0.6f) ? 1.0f : 0.0f);
        const float oy = (float) getHeight() - cell * (PixelGrid::H + 1.0f) - bob * cell;
        if (beatOn)
        {
            // se penche à gauche / à droite un temps sur deux
            const float lean = ((int) std::floor (beatPos) % 2 == 0 ? 1.0f : -1.0f) * 0.10f * (float) (1.0 - ph);
            g.addTransform (juce::AffineTransform::shear (lean, 0.0f).translated (-lean * ((float) getHeight()), 0.0f));
        }

        // ombre
        g.setColour (juce::Colours::black.withAlpha (0.22f));
        g.fillEllipse (ox + cell * 4.0f, oy + cell * (PixelGrid::H - 0.6f), cell * 24.0f, cell * 2.4f);
        // contour fin (style sprite) puis pixels ombrés
        const juce::Colour outline (0xff120e18);
        for (int y = 0; y < PixelGrid::H; ++y)
            for (int x = 0; x < PixelGrid::W; ++x)
                if (px.get (x, y) == 0u && (px.get (x - 1, y) || px.get (x + 1, y) || px.get (x, y - 1) || px.get (x, y + 1)))
                {
                    g.setColour (outline);
                    g.fillRect (ox + x * cell, oy + y * cell, cell + 0.4f, cell + 0.4f);
                }
        for (int y = 0; y < PixelGrid::H; ++y)
            for (int x = 0; x < PixelGrid::W; ++x)
                if (auto c = shaded[(size_t) (y * PixelGrid::W + x)])
                {
                    g.setColour (juce::Colour (c));
                    g.fillRect (ox + x * cell, oy + y * cell, cell + 0.4f, cell + 0.4f);
                }
        overlay (g, ox, oy, cell * 2.0f, action);
    }

private:
    // Lumière venant d'en haut à gauche : chaque zone de couleur reçoit un reflet à gauche / en haut
    // et une ombre à droite / en bas -> volume "cel-shading" automatique pour tous les skins.
    static std::array<juce::uint32, PixelGrid::W * PixelGrid::H> light (const PixelGrid& p)
    {
        std::array<juce::uint32, PixelGrid::W * PixelGrid::H> out = p.c;
        for (int y = 0; y < PixelGrid::H; ++y)
        {
            int x = 0;
            while (x < PixelGrid::W)
            {
                const auto c = p.get (x, y);
                int e = x; while (e + 1 < PixelGrid::W && p.get (e + 1, y) == c) ++e;
                const int len = e - x + 1;
                if (c != 0u && len >= 4)
                    for (int i = x; i <= e; ++i)
                    {
                        float a = 0.0f;
                        if (i >= e - (len >= 8 ? 2 : 1)) a -= 0.26f;           // côté à l'ombre
                        else if (i == x) a += 0.16f;                             // reflet
                        if (p.get (i, y - 1) == 0u) a += 0.22f;                   // lumière du dessus
                        else if (p.get (i, y - 1) != c && p.get (i, y - 2) != c) a -= 0.14f;   // ombre portée (sous le menton, les cheveux…)
                        if (p.get (i, y + 1) == 0u) a -= 0.12f;
                        if (a != 0.0f) out[(size_t) (y * PixelGrid::W + i)] = shade (c, a);
                    }
                x = e + 1;
            }
        }
        return out;
    }

    // ---------------------------------------------------------------------------------
    // Dessin des personnages (silhouette en gros pixels + détails en pixels fins)
    void body (PixelGrid& p, juce::uint32 skinC, juce::uint32 top, juce::uint32 sleeve, juce::uint32 pants, juce::uint32 shoes,
               bool armUp, bool wide = false) const
    {
        const int bx = wide ? 3 : 4, bw = wide ? 10 : 8;
        p.rect (bx, 12, bw, 8, top);                                // torse
        p.rect (5, 20, 2, 4, pants); p.rect (9, 20, 2, 4, pants);   // jambes
        p.fr (10, 40, 13, 2, pants);                                // bassin
        p.rect (4, 24, 3, 1, shoes); p.rect (9, 24, 3, 1, shoes);
        p.fr (8, 49, 6, 1, shade (shoes, 0.5f)); p.fr (18, 49, 6, 1, shade (shoes, 0.5f));   // semelles
        p.f (11, 48, shade (shoes, 0.7f)); p.f (21, 48, shade (shoes, 0.7f));                   // lacets
        p.fr (15, 41, 2, 5, shade (pants, -0.25f));                 // entrejambe
        p.f (12, 44, shade (pants, -0.2f)); p.f (20, 44, shade (pants, -0.2f));                 // plis aux genoux
        p.rect (bx - 2, 12, 2, 7, sleeve); p.rect (bx - 2, 19, 2, 1, skinC);   // bras gauche
        p.f ((bx - 2) * 2, 39, shade (skinC, -0.3f));                         // pouce
        if (armUp) { p.rect (bx + bw, 7, 2, 6, sleeve); p.rect (bx + bw, 5, 2, 2, skinC); p.fr ((bx + bw) * 2, 12, 4, 1, shade (skinC, -0.3f)); }   // bras droit levé, poing
        else       { p.rect (bx + bw, 12, 2, 7, sleeve); p.rect (bx + bw, 19, 2, 1, skinC); p.f ((bx + bw) * 2 + 3, 39, shade (skinC, -0.3f)); }
        p.fr (bx * 2 + 1, 26, 1, 12, shade (top, -0.12f));          // pli du vêtement
        p.rect (7, 11, 2, 1, shade (skinC, -0.15f));                // cou
    }

    void face (PixelGrid& p, juce::uint32 skinC, bool blink, bool mouthOpen, juce::uint32 eye = 0xff15111a, int headX = 4, int headW = 8,
               juce::uint32 brow = 0) const
    {
        p.rect (headX, 3, headW, 8, skinC);
        p.fr (headX * 2 - 1, 12, 1, 4, shade (skinC, -0.1f)); p.fr ((headX + headW) * 2, 12, 1, 4, shade (skinC, -0.25f));   // oreilles
        p.f (headX * 2 - 1, 13, shade (skinC, -0.35f));
        const juce::uint32 lid = shade (skinC, -0.45f);
        if (! blink)
        {
            for (int ex : { 11, 18 })
            {
                p.fr (ex, 12, 3, 2, 0xfff4f1ec);                        // blanc de l'œil
                p.fr (ex + 1 + lookDx, 12, 2, 2, eye);                  // pupille
                p.f (ex + 1 + lookDx, 12, 0xffffffff);                   // reflet
                p.fr (ex, 11, 3, 1, lid);                               // paupière
            }
        }
        else { p.fr (11, 13, 3, 1, lid); p.fr (18, 13, 3, 1, lid); }
        const juce::uint32 b = brow != 0 ? brow : shade (skinC, -0.6f);
        p.fr (10, 9, 4, 1, b); p.fr (18, 9, 4, 1, b);                   // sourcils
        p.f (16, 14, shade (skinC, -0.12f)); p.fr (15, 15, 3, 1, shade (skinC, -0.28f));   // nez
        p.f (11, 16, juce::Colour (skinC).interpolatedWith (juce::Colour (0xffff8a8a), 0.25f).getARGB());
        p.f (20, 16, juce::Colour (skinC).interpolatedWith (juce::Colour (0xffff8a8a), 0.25f).getARGB());   // joues
        if (mouthOpen)
        {
            p.fr (13, 17, 6, 3, 0xff4a1418);
            p.fr (14, 17, 4, 1, 0xfff4f1ec);                            // dents
            p.fr (14, 19, 4, 1, 0xffd0606a);                            // langue
        }
        else
        {
            p.fr (13, 18, 6, 1, shade (skinC, -0.5f));
            p.f (12, 17, shade (skinC, -0.4f)); p.f (19, 17, shade (skinC, -0.4f));   // coins (petit sourire)
            p.fr (14, 19, 4, 1, shade (skinC, -0.12f));                // lèvre du bas
        }
        p.fr (headX * 2 + 2, 20, headW * 2 - 4, 1, shade (skinC, -0.18f));   // menton
    }

    // dessin à utiliser pour chaque skin de la liste
    static int artFor (int s) { static const int a[] = { 0, 2, 3, 4, 7, 8, 100, 101, 104, 105, 106, 109, 110, 111 }; return a[juce::jlimit (0, 13, s)]; }

    void draw (PixelGrid& p, bool action, bool blink, bool mouthOpen) const
    {
        switch (artFor (skin))
        {
            case 0:   // Mélenchon : cheveux gris plaqués, veste col Mao, pin φ rouge, poing levé
            {
                const juce::uint32 sk = 0xffeec2a0, grey = 0xffd2d2d2, jacket = 0xff2b2f3a;
                body (p, sk, jacket, jacket, 0xff2b2f3a, 0xff151515, action);
                p.rect (6, 12, 4, 1, 0xff3d4352);                                       // col Mao
                p.fr (12, 24, 1, 2, 0xff4a5163); p.fr (19, 24, 1, 2, 0xff4a5163);
                for (int y : { 28, 32, 36 }) { p.f (16, y, 0xffb8b8b8); p.f (16, y + 1, 0xff6a6a6a); }   // boutons
                p.fr (15, 25, 2, 14, shade (jacket, -0.2f));                            // fermeture
                p.fr (9, 27, 3, 3, 0xffe8332f); p.f (10, 27, 0xffffffff); p.fr (10, 28, 1, 2, 0xffffffff);   // pin φ
                p.fr (20, 30, 4, 1, shade (jacket, 0.25f));                             // poche poitrine
                face (p, sk, blink, mouthOpen, 0xff3a2c22, 4, 8, 0xff8a8a8a);
                p.rect (4, 2, 8, 2, grey); p.rect (4, 4, 1, 3, grey); p.rect (11, 4, 1, 3, grey);
                for (int x = 9; x < 24; x += 2) p.f (x, 5, 0xffa8a8a8);                 // mèches peignées en arrière
                for (int x = 10; x < 23; x += 3) p.f (x, 6, 0xffeeeeee);
                p.fr (10, 7, 12, 1, shade (sk, -0.12f));                                // front marqué
                p.fr (10, 9, 4, 1, 0xff7a7a7a); p.fr (18, 9, 4, 1, 0xff7a7a7a);
                p.fr (12, 21, 8, 1, shade (sk, -0.25f));                                // mâchoire carrée
                if (action) { p.rect (12, 5, 2, 2, sk); p.fr (24, 10, 4, 1, shade (sk, -0.35f)); p.fr (24, 12, 4, 1, shade (sk, -0.35f)); }
                break;
            }
            case 1:   // Rima Hassan : longs cheveux bruns, keffieh, blazer noir, salue
            {
                const juce::uint32 sk = 0xffd9a47c, hair = 0xff2a1a12, black = 0xff1c1c22;
                p.rect (3, 2, 10, 3, hair); p.rect (3, 4, 2, 10, hair); p.rect (11, 4, 2, 10, hair);
                for (int y = 9; y < 28; y += 3) { p.f (7, y, 0xff4a3022); p.f (24, y + 1, 0xff4a3022); }   // reflets dans les cheveux
                p.fr (6, 26, 2, 2, hair); p.fr (24, 26, 2, 2, hair);                    // pointes
                body (p, sk, black, black, black, 0xff101010, action);
                p.rect (7, 13, 2, 6, 0xfff2f2f2);
                p.fr (12, 24, 2, 8, 0xff2e2e38); p.fr (18, 24, 2, 8, 0xff2e2e38);       // revers du blazer
                p.f (15, 34, 0xff8a8a8a); p.f (15, 37, 0xff8a8a8a);
                face (p, sk, blink, mouthOpen, 0xff2a160c, 4, 8, hair);
                if (! mouthOpen) p.fr (13, 18, 6, 1, 0xffa84a4a);                       // rouge à lèvres
                p.rect (4, 2, 8, 2, hair); p.set (8, 2, 0xff4a3022);
                p.fr (8, 6, 6, 2, hair); p.fr (18, 6, 6, 2, hair); p.fr (14, 5, 4, 2, 0xff3a2418);   // raie au milieu
                p.f (8, 16, 0xffe8c25a); p.f (8, 17, 0xffe8c25a);                       // boucle d'oreille
                p.rect (4, 11, 8, 3, 0xfff5f5f5);                                      // keffieh, motif fin
                for (int x = 8; x < 24; ++x)
                    for (int y = 22; y < 28; ++y)
                        if ((x + y) % 4 == 0 || (x - y + 64) % 4 == 0) p.f (x, y, 0xff1a1a1a);
                p.fr (8, 27, 16, 1, 0xff1a1a1a);
                p.rect (6, 14, 2, 3, 0xfff5f5f5);
                for (int y = 28; y < 34; y += 2) p.f (13, y, 0xff1a1a1a);
                p.fr (12, 34, 4, 1, 0xff1a1a1a);                                        // franges
                break;
            }
            case 2:   // Tony Soprano : chauve, couronne sombre, débardeur, chaîne en or, cigare
            {
                const juce::uint32 sk = 0xfff0c4a4;
                body (p, sk, 0xfff7f6f2, sk, 0xff2e3a55, 0xff222222, action, true);
                p.rect (5, 12, 1, 3, sk); p.rect (10, 12, 1, 3, sk);                    // bretelles du débardeur
                p.fr (12, 24, 8, 3, sk); p.fr (13, 27, 6, 1, sk);                       // décolleté
                for (int x = 13; x < 19; x += 2) p.f (x, 25, 0xff6b5040);               // poils
                for (int x = 12; x < 20; ++x) p.f (x, 24 + (std::abs (x - 16) < 2 ? 2 : (std::abs (x - 16) < 3 ? 1 : 0)), (x % 2) ? 0xffffe07a : 0xffc89a2a);   // chaîne
                p.fr (15, 27, 2, 2, 0xffffe07a);                                        // pendentif
                for (int y = 26; y < 36; y += 3) { p.f (3, y, 0xffc8a080); p.f (28, y + 1, 0xffc8a080); }   // bras poilus
                p.fr (10, 38, 12, 1, 0xffe4e1da);                                        // bas du débardeur
                face (p, sk, blink, mouthOpen, 0xff2a1e16, 3, 10, 0xff2b201a);
                p.rect (3, 5, 1, 4, 0xff3b2c24); p.rect (12, 5, 1, 4, 0xff3b2c24);       // côtés
                p.fr (8, 6, 2, 2, 0xff3b2c24); p.fr (22, 6, 2, 2, 0xff3b2c24);
                p.fr (11, 6, 6, 1, 0xfffbe0cc); p.f (12, 7, 0xffffffff);               // crâne qui brille
                p.fr (9, 9, 5, 1, 0xff2b201a); p.fr (18, 9, 5, 1, 0xff2b201a);          // gros sourcils
                for (int x = 9; x < 23; ++x) if (x % 2 == 0 || x > 18) p.f (x, 20 + (x % 3 == 0), 0xffb89a86);   // barbe de 3 jours
                p.fr (8, 16, 1, 4, 0xffb89a86); p.fr (23, 16, 1, 4, 0xffb89a86);
                p.fr (20, 18, 6, 2, 0xff6b4226); p.fr (20, 18, 6, 1, 0xff8a5a36); p.fr (26, 18, 1, 2, 0xffff6a2a); p.f (27, 18, 0xffffc070);   // cigare
                break;
            }
            case 3:   // orang-outan : fourrure rousse texturée, bajoues, longs bras, banane
            {
                const juce::uint32 fur = 0xffc8531e, furD = 0xffa3401a, furL = 0xffe0702e, face0 = 0xffd9a17a;
                p.rect (2, 12, 12, 9, fur); p.rect (4, 21, 3, 3, fur); p.rect (9, 21, 3, 3, fur);
                p.rect (0, 12, 2, 10, fur); p.rect (14, 12, 2, 10, fur);              // longs bras
                if (action) { p.rect (14, 4, 2, 8, fur); }                             // se gratte la tête
                p.rect (2, 3, 12, 9, fur);
                for (int y = 6; y < 48; ++y)                                            // poils (texture)
                    for (int x = 0; x < 32; ++x)
                        if (p.get (x, y) == fur && (((unsigned) x * 73856093u ^ (unsigned) y * 19349663u) % 7u == 0u)) p.f (x, y, ((x + y) % 3) ? furD : furL);
                for (int y = 40; y < 46; ++y) { p.f (1 + (y % 2), y, furL); p.f (29 + (y % 2), y, furL); }   // poils qui pendent
                p.rect (1, 5, 2, 5, furD); p.rect (13, 5, 2, 5, furD);                  // bajoues
                p.fr (2, 10, 1, 10, 0xff7a2e12); p.fr (29, 10, 1, 10, 0xff7a2e12);
                p.rect (4, 4, 8, 7, face0);
                p.fr (10, 10, 12, 2, shade (face0, -0.2f));                             // arcade
                p.fr (11, 18, 10, 4, 0xffe8b890);                                       // museau
                if (! blink) { p.fr (12, 12, 2, 2, 0xff3a2210); p.fr (18, 12, 2, 2, 0xff3a2210); p.f (12 + (lookDx > 0), 12, 0xffffffff); p.f (18 + (lookDx > 0), 12, 0xffffffff); }
                else { p.fr (12, 13, 2, 1, 0xff6b3b20); p.fr (18, 13, 2, 1, 0xff6b3b20); }
                p.f (14, 16, 0xff6b3b20); p.f (17, 16, 0xff6b3b20);                     // narines
                if (mouthOpen) { p.fr (12, 19, 8, 3, 0xff5a1f22); p.fr (13, 19, 6, 1, 0xfff4f1ec); }
                else p.fr (11, 20, 10, 1, 0xff8a4a2a);
                p.rect (5, 2, 6, 1, fur); p.set (6, 1, fur); p.set (9, 1, fur); p.f (15, 1, furL); p.f (17, 0, furL);   // touffe
                p.fr (28, 41, 2, 6, 0xffffd84a); p.fr (29, 40, 2, 1, 0xffffd84a); p.f (30, 39, 0xff6b4a1a); p.f (28, 47, 0xff6b4a1a);   // banane
                p.fr (29, 42, 1, 4, 0xffe8b830);
                break;
            }
            case 4:   // Steve : tout en cubes, texture de skin Minecraft, pioche en diamant
            {
                const juce::uint32 sk = 0xffc89a74, hair = 0xff4a2f1f, shirt = 0xff00a6a6, jeans = 0xff3b3e9c;
                body (p, sk, shirt, sk, jeans, 0xff6b6b6b, false);
                p.rect (2, 12, 2, 2, shirt); p.rect (12, 12, 2, 2, shirt);              // manches
                for (int y = 24; y < 50; ++y)                                            // texture "bruit" des textures Minecraft
                    for (int x = 0; x < 32; ++x)
                    {
                        const auto c = p.get (x, y);
                        if ((c == shirt || c == jeans || c == sk) && (((unsigned) x * 73856093u ^ (unsigned) y * 19349663u) % 6u == 0u)) p.f (x, y, shade (c, (x + y) % 2 ? 0.12f : -0.12f));
                    }
                p.rect (4, 3, 8, 8, sk);
                p.rect (4, 2, 8, 2, hair); p.set (4, 4, hair); p.set (11, 4, hair);
                p.fr (8, 8, 2, 4, hair); p.fr (22, 8, 2, 4, hair); p.f (12, 8, 0xff5a3a28); p.f (18, 9, 0xff5a3a28);
                if (! blink) { p.fr (10, 12, 2, 2, 0xffffffff); p.fr (12, 12, 2, 2, 0xff5a3fb0); p.fr (18, 12, 2, 2, 0xff5a3fb0); p.fr (20, 12, 2, 2, 0xffffffff); }
                else { p.fr (10, 13, 4, 1, 0xff8a6a4a); p.fr (18, 13, 4, 1, 0xff8a6a4a); }
                p.rect (7, 7, 2, 1, 0xff9a6a4a);
                p.rect (6, 9, 4, 1, hair); p.set (5, 8, hair); p.set (10, 8, hair);    // barbe en pixels
                p.fr (14, 18, 4, 1, 0xff6a4030);
                if (mouthOpen) p.rect (7, 9, 2, 1, 0xff3a1a10);
                // pioche en diamant (levée en action)
                const juce::uint32 stick = 0xff7a5230, dia = 0xff6fe3e0, diaL = 0xffc8fffd, diaD = 0xff2fa8a6;
                if (action)
                {
                    for (int k = 0; k < 12; ++k) p.fr (26 + k / 3, 26 - k, 2, 1, (k % 3) ? stick : 0xff5a3a20);
                    p.fr (22, 12, 10, 2, dia); p.f (22, 14, dia); p.f (31, 14, dia); p.fr (24, 12, 4, 1, diaL); p.fr (29, 13, 2, 1, diaD);
                }
                else
                {
                    for (int k = 0; k < 12; ++k) p.fr (28, 30 + k, 2, 1, (k % 3) ? stick : 0xff5a3a20);
                    p.fr (25, 28, 7, 2, dia); p.f (25, 30, dia); p.f (31, 30, dia); p.fr (26, 28, 3, 1, diaL); p.fr (30, 29, 1, 1, diaD);
                }
                break;
            }
            case 5:   // ZywOo : maillot jaune/noir façon guêpe, casque gamer, AWP en action
            {
                const juce::uint32 sk = 0xffd8a27a, hair = 0xff15110f, yel = 0xfff9e000;
                body (p, sk, yel, yel, 0xff111111, 0xffffffff, false);
                p.rect (4, 15, 8, 1, 0xff111111); p.rect (4, 18, 8, 1, 0xff111111);
                p.rect (6, 12, 4, 1, 0xff111111);
                for (int x = 8; x < 24; x += 4) { p.f (x, 31, 0xff111111); p.f (x + 1, 32, 0xff111111); }   // motif en zigzag
                p.fr (13, 26, 6, 3, 0xff111111); p.fr (14, 27, 4, 1, yel);              // logo abeille
                p.f (12, 26, 0xfff8f8f8); p.f (19, 26, 0xfff8f8f8);
                p.fr (21, 34, 3, 2, 0xffffffff);                                        // sponsor
                p.fr (8, 50, 5, 1, 0xffdcdcdc); p.fr (18, 50, 5, 1, 0xffdcdcdc);
                face (p, sk, blink, mouthOpen, 0xff1e140e, 4, 8, hair);
                p.rect (4, 2, 8, 2, hair); p.set (4, 4, hair); p.set (11, 4, hair);
                p.fr (10, 4, 4, 2, 0xff2a2220); p.fr (18, 3, 3, 2, 0xff2a2220);         // cheveux courts texturés
                p.rect (3, 4, 1, 4, 0xff1b1b1b); p.rect (12, 4, 1, 4, 0xff1b1b1b);    // casque
                p.fr (8, 3, 16, 1, 0xff1b1b1b);                                         // arceau
                p.fr (6, 10, 1, 4, yel); p.fr (25, 10, 1, 4, yel);                      // anneaux jaunes
                p.rect (4, 8, 2, 1, 0xff1b1b1b); p.f (12, 17, 0xff3a3a3a); p.f (12, 16, yel);   // micro
                if (action)
                {
                    p.rect (0, 14, 16, 1, 0xff2f4a2a); p.fr (0, 27, 6, 3, 0xff2f4a2a);  // AWP
                    p.fr (16, 25, 8, 2, 0xff1b1b1b); p.f (17, 25, 0xff7fd6ff);           // lunette
                    p.fr (20, 30, 2, 3, 0xff1b1b1b); p.f (31, 28, 0xff1b1b1b);
                    p.fr (6, 28, 10, 1, 0xff3e5c38);
                }
                else { p.rect (13, 19, 2, 2, 0xff1b1b1b); p.fr (27, 38, 2, 1, 0xffff3a3a); p.f (28, 40, 0xff444444); }   // souris RGB
                break;
            }
            case 7:   // Terroriste : cagoule tricotée, veste marron, AK-47
            {
                const juce::uint32 sk = 0xffd9a47c, mask = 0xff2a2622, jacket = 0xff6a4a2e;
                body (p, sk, jacket, jacket, 0xffa38f68, 0xff3a2a1a, false);
                p.rect (4, 2, 8, 9, mask);
                for (int y = 4; y < 22; y += 2) for (int x = 8 + (y % 4 == 0); x < 24; x += 2) p.f (x, y, 0xff3a3530);   // maille
                p.fr (9, 10, 14, 4, sk); p.fr (9, 10, 14, 1, shade (sk, -0.3f));          // fente des yeux
                if (! blink) { p.fr (11, 11, 3, 2, 0xfff4f1ec); p.fr (12 + lookDx, 11, 2, 2, 0xff15111a); p.fr (18, 11, 3, 2, 0xfff4f1ec); p.fr (19 + lookDx, 11, 2, 2, 0xff15111a); }
                else { p.fr (11, 12, 3, 1, 0xff15111a); p.fr (18, 12, 3, 1, 0xff15111a); }
                p.fr (14, 17, 4, 1, mouthOpen ? 0xff5a1f22 : 0xff15111a);
                p.rect (4, 12, 8, 1, 0xff4a3420);
                p.fr (9, 28, 5, 4, 0xff4e3622); p.fr (18, 28, 5, 4, 0xff4e3622); p.fr (9, 28, 5, 1, 0xff7e5a3a); p.fr (18, 28, 5, 1, 0xff7e5a3a);   // poches
                for (int x = 8; x < 24; x += 3) p.f (x, 38, 0xff3a2818);                    // ceinture
                if (action) { p.rect (5, 20, 4, 2, 0xff2a2a2a); p.fr (11, 41, 6, 1, 0xff8a1a1a); p.f (12, 40, (std::fmod (t * 4.0f, 1.0f) < 0.5f) ? 0xffff3030 : 0xff551010); p.fr (14, 41, 3, 1, 0xff3a8a3a); }   // C4
                p.rect (1, 15, 13, 1, 0xff1b1b1b);                                          // AK : canon / boîtier
                p.fr (2, 31, 2, 1, 0xff3a3a3a);                                             // guidon
                p.fr (6, 32, 6, 2, 0xff8a5a2a); p.fr (6, 32, 6, 1, 0xffa8763e);              // garde-main en bois
                p.fr (18, 32, 2, 4, 0xff1b1b1b); p.fr (19, 35, 2, 2, 0xff1b1b1b); p.f (20, 37, 0xff1b1b1b);   // chargeur courbé
                p.fr (24, 31, 4, 3, 0xff8a5a2a); p.fr (25, 31, 3, 1, 0xffa8763e);            // crosse
                break;
            }
            case 8:   // Anti-terroriste : casque, lunettes, gilet à poches, M4
            {
                const juce::uint32 sk = 0xfff0c6a6, helm = 0xff3d4a3a, vest = 0xff1d2430;
                body (p, sk, 0xff2c3646, 0xff2c3646, 0xff3a4250, 0xff151515, false);
                p.rect (5, 13, 6, 5, vest);                                                // gilet
                for (int x : { 11, 15, 19 }) { p.fr (x, 30, 3, 4, 0xff2a3444); p.fr (x, 30, 3, 1, 0xff3a4658); }   // poches
                p.fr (12, 27, 8, 2, 0xff2a3444); p.fr (13, 27, 2, 2, 0xff7fd6ff);           // écusson
                p.fr (10, 26, 1, 10, 0xff10151c); p.fr (21, 26, 1, 10, 0xff10151c);         // sangles
                p.fr (24, 26, 2, 3, 0xff3b6aa8); p.f (24, 26, 0xffffffff);                  // drapeau sur la manche
                face (p, sk, blink, mouthOpen, 0xff2a3040, 4, 8, 0xff6b4a33);
                p.rect (3, 2, 10, 3, helm); p.rect (3, 5, 1, 2, helm); p.rect (12, 5, 1, 2, helm);
                p.fr (8, 3, 4, 1, shade (helm, 0.3f));                                       // reflet du casque
                for (int x = 9; x < 24; x += 3) p.f (x, 8, 0xff2e382c);                     // bord
                p.fr (7, 14, 1, 7, 0xff222222); p.fr (24, 14, 1, 7, 0xff222222);            // jugulaire
                p.rect (5, 5, 6, 2, 0xff1b1b1b);                                            // lunettes de protection
                p.fr (11, 11, 3, 2, 0xff7fd6ff); p.fr (18, 11, 3, 2, 0xff7fd6ff); p.f (11, 11, 0xffeafaff); p.f (18, 11, 0xffeafaff);
                p.rect (1, 15, 14, 1, 0xff141414); p.rect (10, 16, 2, 2, 0xff141414); p.set (0, 15, 0xff141414);   // M4
                p.fr (8, 29, 6, 1, 0xff2a2a2a); p.fr (12, 28, 3, 1, 0xff3a3a3a);            // rail + viseur
                p.fr (21, 32, 2, 4, 0xff141414); p.f (4, 31, 0xff2a2a2a);
                if (action) { p.rect (2, 18, 2, 3, 0xffffd84a); p.fr (5, 37, 2, 1, 0xffff3030); p.fr (5, 39, 2, 1, 0xff30ff60); }   // kit de désamorçage
                break;
            }
            case 100:   // Claude Nougaro : cheveux noirs bouclés en volume, chemise noire, foulard rouge, micro
            {
                const juce::uint32 sk = 0xffd9a17a, hair = 0xff1a1414, shirt = 0xff17171d;
                p.rect (2, 1, 12, 5, hair); p.rect (2, 5, 2, 6, hair); p.rect (12, 5, 2, 6, hair);   // grosse chevelure
                body (p, sk, shirt, shirt, 0xff26262e, 0xff111111, action);
                p.fr (12, 22, 8, 3, 0xffd8332f); p.fr (14, 25, 3, 4, 0xffd8332f);                    // foulard rouge
                p.fr (15, 26, 2, 10, 0xff2a2a33);                                                    // chemise ouverte
                face (p, sk, blink, mouthOpen, 0xff2a1a10, 4, 8, hair);
                p.fr (10, 8, 4, 2, hair); p.fr (18, 8, 4, 2, hair);                                  // sourcils intenses
                for (int y = 2; y < 22; y += 3) for (int x = 4 + (y % 2); x < 28; x += 4) if (p.get (x, y) == hair) p.f (x, y, 0xff3a2e2a);   // boucles
                if (action) { p.fr (26, 10, 3, 3, 0xff444444); p.fr (27, 13, 1, 6, 0xff777777); }      // micro levé
                break;
            }
            case 101:   // Patrick Saint-Éloi (Kassav) : peau foncée, moustache, chemise madras rouge / jaune / vert
            {
                const juce::uint32 sk = 0xff6b4630, hair = 0xff141010;
                body (p, sk, 0xffd8332f, 0xffd8332f, 0xff1f2a44, 0xff111111, action);
                for (int y = 24; y < 40; ++y) for (int x = 8; x < 24; ++x)                           // madras
                {
                    if (p.get (x, y) != 0xffd8332f) continue;
                    if (x % 6 == 0 || x % 6 == 1) p.f (x, y, 0xfff2c12e);
                    if (y % 6 == 0) p.f (x, y, 0xff2f9a4a);
                }
                face (p, sk, blink, mouthOpen, 0xff1a100a, 4, 8, hair);
                p.rect (4, 2, 8, 2, hair); p.fr (8, 6, 2, 3, hair); p.fr (22, 6, 2, 3, hair);         // cheveux courts
                if (! mouthOpen) p.fr (12, 17, 8, 1, hair);                                           // moustache
                break;
            }
            case 103:   // 8ruki : capuche relevée, lunettes teintées, tenue sombre violette
            {
                const juce::uint32 sk = 0xffc89270, hood = 0xff3a1f5c;
                p.rect (2, 1, 12, 11, hood);                                                         // capuche
                body (p, sk, hood, hood, 0xff17121f, 0xff0f0f0f, action);
                face (p, sk, blink, mouthOpen, 0xff1a1414, 4, 8, 0xff1a1414);
                p.fr (9, 11, 14, 3, 0xff111111); p.fr (10, 12, 4, 1, 0xffd43b6a); p.fr (18, 12, 4, 1, 0xffd43b6a);   // lunettes teintées
                p.fr (7, 4, 18, 2, shade (hood, -0.25f));
                for (int y = 26; y < 38; y += 4) p.fr (10, y, 12, 1, shade (hood, 0.18f));
                break;
            }
            case 104:   // Wiz Khalifa : grand et fin, dreads en chignon, tatouages, débardeur blanc, noir et jaune
            {
                const juce::uint32 sk = 0xff6e4a33, dread = 0xff1a1410;
                body (p, sk, 0xfff5f5f0, sk, 0xff141414, 0xfff2c12e, action);
                p.rect (5, 12, 1, 3, sk); p.rect (10, 12, 1, 3, sk);                                 // bretelles
                for (int y = 24; y < 38; y += 2) { p.f (5, y, 0xff2a1c14); p.f (27, y + 1, 0xff2a1c14); p.f (4, y + 1, 0xff2a1c14); }   // tatouages
                face (p, sk, blink, mouthOpen, 0xff120c08, 4, 8, dread);
                p.rect (5, 0, 6, 3, dread); p.fr (12, 0, 8, 1, 0xfff2c12e);                          // chignon + élastique jaune
                p.rect (4, 2, 8, 1, dread); p.fr (8, 6, 2, 4, dread); p.fr (22, 6, 2, 4, dread);
                for (int x = 10; x < 22; x += 2) p.f (x, 20, 0xff3a2a1e);                            // petite barbe
                break;
            }
            case 105:   // Caballero : casquette à l'envers, grosse barbe, t-shirt large, chaîne
            {
                const juce::uint32 sk = 0xffe2b38c, beard = 0xff3a2414, cap = 0xff1d1d1d;
                body (p, sk, 0xff5a6b7a, 0xff5a6b7a, 0xff2a3040, 0xffeeeeee, action, true);
                face (p, sk, blink, mouthOpen, 0xff2a1a10, 4, 8, beard);
                p.rect (4, 2, 8, 2, cap); p.fr (4, 7, 6, 2, cap);                                    // casquette à l'envers (visière derrière)
                p.fr (8, 15, 16, 7, beard); p.fr (10, 22, 12, 2, beard);                             // grosse barbe
                if (mouthOpen) p.fr (14, 18, 4, 2, 0xff4a1418); else p.fr (13, 18, 6, 1, shade (beard, 0.3f));
                p.fr (12, 25, 8, 1, 0xffe8c25a); p.fr (15, 26, 2, 2, 0xffe8c25a);                    // chaîne
                break;
            }
            case 106:   // JeanJass : lunettes, cheveux courts, barbe courte, veste
            {
                const juce::uint32 sk = 0xffe0b08a, hair = 0xff2a1c14;
                body (p, sk, 0xff2f5a46, 0xff2f5a46, 0xff1f2430, 0xff111111, action);
                p.fr (15, 24, 2, 14, 0xffd0d0d0);                                                    // fermeture
                face (p, sk, blink, mouthOpen, 0xff2a1a10, 4, 8, hair);
                p.rect (4, 2, 8, 2, hair); p.fr (8, 6, 2, 2, hair); p.fr (22, 6, 2, 2, hair);
                p.fr (10, 11, 5, 4, 0xff111111); p.fr (17, 11, 5, 4, 0xff111111); p.fr (15, 12, 2, 1, 0xff111111);   // lunettes
                p.fr (11, 12, 3, 2, 0xff9fc8e8); p.fr (18, 12, 3, 2, 0xff9fc8e8);
                for (int x = 9; x < 23; x += 2) p.f (x, 21, hair);                                   // barbe courte
                break;
            }
            case 107:   // Serane : tresses, bomber, boucle d'oreille
            {
                const juce::uint32 sk = 0xff8a5a3c, hair = 0xff161010;
                body (p, sk, 0xff6b7a3a, 0xff6b7a3a, 0xff1c1c22, 0xfff2f2f2, action);
                p.fr (8, 24, 16, 2, 0xffd88a2a);                                                     // col orange du bomber
                face (p, sk, blink, mouthOpen, 0xff120c08, 4, 8, hair);
                p.rect (4, 2, 8, 2, hair);
                for (int x = 7; x < 26; x += 3) p.fr (x, 6, 1, 16, hair);                            // tresses
                p.f (7, 16, 0xffe8c25a); p.f (7, 17, 0xffe8c25a);
                break;
            }
            case 108:   // Sheriff Lazone : chapeau de shérif, étoile, lunettes noires
            {
                const juce::uint32 sk = 0xffc89270, hat = 0xff6b4226;
                body (p, sk, 0xff8a6a3e, 0xff8a6a3e, 0xff2a3a5a, 0xff3a2414, action);
                p.fr (9, 27, 4, 4, 0xfff2c12e); p.f (10, 26, 0xfff2c12e); p.f (13, 28, 0xfff2c12e); p.f (8, 28, 0xfff2c12e);   // étoile de shérif
                face (p, sk, blink, mouthOpen, 0xff1a1410, 4, 8, 0xff2a1c14);
                p.rect (1, 3, 14, 1, hat); p.rect (4, 0, 8, 3, hat); p.fr (8, 5, 16, 1, shade (hat, -0.3f));   // chapeau
                p.fr (9, 11, 6, 3, 0xff111111); p.fr (17, 11, 6, 3, 0xff111111); p.fr (15, 12, 2, 1, 0xff111111);   // lunettes noires
                break;
            }
            case 109:   // cube façon Geometry Dash : carré jaune à bord cyan avec un visage, qui saute
            {
                const int jump = action ? (int) (std::abs (std::sin (t * 9.0f)) * 8.0f) : 0;
                const juce::uint32 yel = 0xfff2e12e, cyan = 0xff38e0f0, dark = 0xff10303a;
                const int y0 = 22 - jump;
                p.fr (4, y0, 24, 24, cyan);
                p.fr (7, y0 + 3, 18, 18, yel);
                p.fr (10, y0 + 7, 4, 4, dark); p.fr (18, y0 + 7, 4, 4, dark);                        // yeux carrés
                if (! blink) { p.f (11, y0 + 8, 0xffffffff); p.f (19, y0 + 8, 0xffffffff); }
                p.fr (11, y0 + 15, 10, mouthOpen ? 3 : 2, dark);                                     // bouche
                p.fr (9, y0 + 5, 6, 1, shade (yel, 0.4f));
                break;
            }
            case 110:   // Sassy le Sasquatch : fourrure brune hirsute, visage et torse gris, grands yeux noirs, cigarette qui fume
            {
                const juce::uint32 fur = 0xff9a5a2c, furD = 0xff74401c, furL = 0xffb87038, grey = 0xff8e8e8c, greyD = 0xff6a6a68, greyL = 0xffa6a6a4, ink = 0xff121214;
                p.rect (1, 11, 14, 10, fur);                                                // grosses épaules / corps
                p.rect (4, 21, 3, 3, fur); p.rect (9, 21, 3, 3, fur);                       // jambes
                p.rect (3, 24, 4, 1, greyD); p.rect (9, 24, 4, 1, greyD);                   // pieds gris
                p.rect (0, 12, 2, 9, fur); p.rect (0, 21, 2, 1, grey);                      // bras gauche le long du corps
                // tête : un tas de poils qui s'élargit vers les épaules (pas de cou)
                p.fr (12, 1, 8, 3, fur); p.fr (10, 4, 12, 4, fur); p.fr (8, 8, 16, 16, fur); p.fr (6, 16, 20, 8, fur);
                p.f (15, 0, fur); p.f (17, 0, furD); p.f (13, 0, furL);                     // épis sur le dessus
                // poils : mèches sombres / claires, bords en pointes
                for (int y = 0; y < 48; ++y)
                    for (int x = 0; x < 32; ++x)
                        if (p.get (x, y) == fur)
                        {
                            const unsigned h = ((unsigned) x * 73856093u) ^ ((unsigned) y * 19349663u);
                            if (h % 5u == 0u) p.f (x, y, furD); else if (h % 11u == 0u) p.f (x, y, furL);
                        }
                {
                    // contour hirsute : quelques pixels du bord enlevés, d'autres qui dépassent
                    PixelGrid q = p;
                    for (int y = 1; y < 47; ++y)
                        for (int x = 1; x < 31; ++x)
                        {
                            const auto c = q.get (x, y);
                            const bool edge = c != 0u && (q.get (x - 1, y) == 0u || q.get (x + 1, y) == 0u || q.get (x, y - 1) == 0u);
                            const unsigned h = ((unsigned) x * 2654435761u) ^ ((unsigned) y * 40503u);
                            if (edge && (c == fur || c == furD || c == furL) && h % 3u == 0u) p.f (x, y, 0);
                            if (c == 0u && h % 7u == 0u && (q.get (x + 1, y) == fur || q.get (x - 1, y) == fur)) p.f (x, y, furD);
                        }
                }
                // torse gris, pectoraux et ventre
                p.fr (8, 25, 16, 15, grey);
                p.fr (8, 25, 1, 15, greyD); p.fr (23, 25, 1, 15, greyD);
                p.fr (10, 30, 5, 1, greyD); p.fr (17, 30, 5, 1, greyD);                     // pectoraux
                p.fr (12, 36, 8, 1, greyL); p.f (16, 34, greyD);                            // ventre
                // visage gris dans la capuche de poils
                p.fr (10, 7, 12, 14, grey); p.fr (11, 6, 10, 1, grey);
                p.f (10, 7, fur); p.f (21, 7, fur); p.f (10, 20, fur); p.f (21, 20, fur);
                p.fr (12, 7, 8, 1, greyD);                                                  // rides du front
                if (! blink)
                {
                    for (int ex : { 10, 17 })                                               // grands yeux noirs, ronds, creux
                    {
                        p.fr (ex, 9, 5, 5, ink);
                        p.fr (ex + 1, 8, 3, 1, ink); p.fr (ex + 1, 14, 3, 1, ink);
                        p.f (ex, 9, greyD); p.f (ex + 4, 9, greyD); p.f (ex, 13, greyD); p.f (ex + 4, 13, greyD);   // yeux bien ronds
                        p.f (ex + 1 + (lookDx > 0 ? 2 : 0), 10, 0xff4a4a52);                   // petit reflet
                    }
                }
                else { p.fr (11, 11, 4, 1, ink); p.fr (17, 11, 4, 1, ink); }
                p.fr (12, 14, 2, 1, greyD); p.fr (18, 14, 2, 1, greyD);                     // poches sous les yeux
                p.f (15, 15, ink); p.f (17, 15, ink);                                       // narines
                // grande bouche aux lèvres épaisses
                p.fr (11, 16, 10, 1, greyL); p.fr (11, 19, 10, 1, greyL);
                p.fr (11, 17, 10, mouthOpen ? 2 : 1, 0xff2a2a2c);
                if (! mouthOpen) p.fr (11, 18, 10, 1, greyD);
                p.f (10, 17, greyD); p.f (21, 17, greyD);
                p.fr (12, 20, 8, 1, greyD);                                                 // menton
                if (action)   // il tire sur sa cigarette
                {
                    p.fr (22, 18, 6, 12, fur); p.fr (20, 16, 4, 3, grey);                   // bras levé, main à la bouche
                    p.fr (17, 17, 4, 1, 0xfff2f2ee); p.f (16, 17, 0xffff7a2a);
                }
                else          // main posée sur la poitrine, la cigarette entre les doigts
                {
                    p.fr (17, 29, 12, 5, fur); p.fr (24, 26, 6, 4, fur);                    // avant-bras en travers
                    for (int x = 17; x < 29; x += 3) p.f (x, 33, furD);
                    p.fr (13, 29, 5, 4, grey); p.fr (13, 32, 4, 1, greyD);                   // main
                    p.fr (8, 30, 5, 1, 0xfff2f2ee); p.f (7, 30, 0xffff7a2a); p.f (12, 30, 0xffd9b88a);   // cigarette, bout rouge, filtre
                }
                break;
            }
            case 111:   // Aussie : chapeau à bouchons, chemise kaki, short, tongs, nez cramé, boomerang
            {
                const juce::uint32 sk = 0xffe0a07a, khaki = 0xffc8a868, shorts = 0xff8a7448, hat = 0xff7a5232, thong = 0xff2f6fd0;
                body (p, sk, khaki, khaki, shorts, thong, action);
                p.fr (4, 31, 4, 7, sk);                                                     // avant-bras nus (manches courtes)
                if (action) p.fr (24, 14, 4, 6, sk); else p.fr (24, 31, 4, 7, sk);
                p.fr (10, 44, 4, 4, sk); p.fr (18, 44, 4, 4, sk);                           // jambes nues sous le short
                p.f (11, 47, thong); p.f (21, 47, thong);                                   // brides des tongs
                p.fr (14, 24, 4, 2, sk); p.fr (15, 26, 2, 1, sk);                           // col ouvert
                p.fr (10, 27, 4, 3, shade (khaki, -0.15f)); p.fr (18, 27, 4, 3, shade (khaki, -0.15f));   // poches
                for (int y : { 29, 33, 37 }) p.f (16, y, 0xff6b5a3a);                       // boutons
                face (p, sk, blink, mouthOpen, 0xff2a5a8a, 4, 8, 0xff8a5a30);
                p.fr (15, 14, 3, 2, 0xffe8665a);                                            // nez coup de soleil
                p.fr (15, 13, 2, 1, 0xfff4f4f4);                                            // crème solaire
                p.fr (4, 7, 24, 2, hat); p.f (3, 8, hat); p.f (28, 8, hat);                 // chapeau : bord large
                p.fr (9, 2, 14, 5, hat); p.fr (10, 1, 12, 1, shade (hat, 0.12f));           // calotte
                p.fr (9, 5, 14, 1, 0xff3a2414); p.fr (15, 2, 2, 2, shade (hat, -0.3f));     // bandeau, pli
                for (int cx : { 5, 9, 22, 26 })                                             // bouchons qui pendent
                {
                    const int sw = (int) std::round (std::sin (t * 3.0f + cx) * (action ? 1.0f : 0.4f));
                    p.f (cx, 9, 0xff3a2a1a); p.f (cx + sw, 10, 0xff3a2a1a); p.fr (cx + sw, 11, 1, 2, 0xffd8b07a);
                }
                if (! action) { p.fr (1, 33, 4, 7, 0xffd8dce4); p.fr (1, 35, 4, 3, 0xffe0b030); p.fr (1, 33, 4, 1, 0xff9aa0aa); }   // canette
                break;
            }
        }
    }

    // effets animés par-dessus (fumée du cigare, ondes du mégaphone, tir, bombe…)
    void overlay (juce::Graphics& g, float ox, float oy, float cell, bool action) const
    {
        auto pix = [&] (float x, float y, juce::Colour c) { g.setColour (c); g.fillRect (ox + x * cell, oy + y * cell, cell, cell); };
        // couteaux des persos CS : inspection de karambit (T) et de butterfly (CT), main gauche
        const int art = artFor (skin);
        if ((art == 7 || art == 8) && action)
        {
            const juce::Point<float> hand (ox + 2.5f * cell, oy + 19.6f * cell);
            g.saveState();
            if (art == 7)
            {
                // karambit : lame courbe + anneau, qui tourne autour du doigt
                const float ang = t * 9.0f;
                g.addTransform (juce::AffineTransform::rotation (ang).translated (hand));
                g.setColour (juce::Colour (0xff2a2a2a));
                g.drawEllipse (-cell * 0.9f, -cell * 0.9f, cell * 1.8f, cell * 1.8f, cell * 0.45f);
                juce::Path blade;
                blade.startNewSubPath (cell * 0.8f, 0.0f);
                blade.quadraticTo (cell * 3.4f, -cell * 0.6f, cell * 3.6f, cell * 2.4f);
                blade.quadraticTo (cell * 2.8f, cell * 0.6f, cell * 0.8f, cell * 0.7f);
                blade.closeSubPath();
                g.setColour (juce::Colour (0xffd8dde4)); g.fillPath (blade);
                g.setColour (juce::Colour (0xff6a7480)); g.strokePath (blade, juce::PathStrokeType (cell * 0.18f));
            }
            else
            {
                // butterfly : deux manches qui s'ouvrent et se referment autour de la lame
                const float open = 0.5f + 0.5f * std::sin (t * 11.0f);
                g.addTransform (juce::AffineTransform::rotation (std::sin (t * 5.0f) * 0.6f).translated (hand));
                juce::Path bladeP; bladeP.addRoundedRectangle (0.0f, -cell * 0.3f, cell * 3.4f, cell * 0.6f, cell * 0.25f);
                g.setColour (juce::Colour (0xffe2e6ec)); g.fillPath (bladeP);
                for (int side = -1; side <= 1; side += 2)
                {
                    g.saveState();
                    g.addTransform (juce::AffineTransform::rotation (side * (0.15f + open * 2.6f)));
                    g.setColour (side < 0 ? juce::Colour (0xff3b6aa8) : juce::Colour (0xff2d4f80));
                    g.fillRoundedRectangle (-cell * 3.2f, -cell * 0.32f, cell * 3.2f, cell * 0.64f, cell * 0.3f);
                    g.restoreState();
                }
            }
            g.restoreState();
        }
        switch (artFor (skin))
        {
            case 0: if (action) { pix (14.0f, 3.0f, juce::Colour (0xffe8332f)); pix (14.0f, 1.0f, juce::Colour (0xffe8332f)); } break;   // "!"
            case 2:
                for (int k = 0; k < 3; ++k)
                {
                    const float ph = std::fmod (t * 0.5f + k * 0.33f, 1.0f);
                    pix (13.0f + std::round (std::sin (ph * 6.0f)), 8.0f - std::round (ph * 7.0f), juce::Colours::white.withAlpha (0.55f * (1.0f - ph)));
                }
                break;
            case 5: if (action && std::fmod (t * 6.0f, 1.0f) < 0.3f) pix (16.0f, 14.0f, juce::Colour (0xffffe08a)); break;   // tir
            case 8: if (action && std::fmod (t * 3.0f, 1.0f) < 0.5f) pix (14.0f, 13.0f, juce::Colour (0xffffe08a)); break;
            case 110:   // fumée de la cigarette de Sassy
                for (int k = 0; k < 5; ++k)
                {
                    const float ph = std::fmod (t * 0.35f + k * 0.2f, 1.0f);
                    const float bx = action ? 8.0f : 3.5f, by = action ? 8.5f : 15.0f;   // bout de la cigarette (en gros pixels)
                    const float x = bx + std::sin (ph * 7.0f + k) * (0.5f + ph * 1.2f) - (action ? ph * 2.0f : 0.0f);
                    const float y = by - ph * 9.0f;
                    g.setColour (juce::Colours::white.withAlpha (0.55f * (1.0f - ph)));
                    g.fillEllipse (ox + x * cell, oy + y * cell, cell * (0.5f + ph), cell * (0.5f + ph));
                }
                break;
            case 111:   // boomerang qui tourne au-dessus de la main levée
                if (action)
                {
                    g.saveState();
                    const float a = t * 10.0f;
                    g.addTransform (juce::AffineTransform::rotation (a).translated (ox + (12.8f + 1.6f * std::cos (t * 4.0f)) * cell, oy + (2.2f + 0.8f * std::sin (t * 4.0f)) * cell));
                    juce::Path b;
                    b.startNewSubPath (-cell * 1.6f, -cell * 0.2f);
                    b.quadraticTo (0.0f, -cell * 1.1f, cell * 1.6f, -cell * 0.2f);
                    g.setColour (juce::Colour (0xffb5652a));
                    g.strokePath (b, juce::PathStrokeType (cell * 0.55f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                    g.setColour (juce::Colour (0xfff2d29a));
                    g.fillEllipse (-cell * 0.2f, -cell * 0.85f, cell * 0.4f, cell * 0.3f);
                    g.restoreState();
                }
                break;
            default: break;
        }
    }

    juce::Point<float> look;
    float t = 0.0f, blinkIn = 3.0f, actT = 0.0f, idleActIn = 5.0f;
    int skin = 0, lookDx = 0;
    bool dancing = false, beatPlaying = false;
    double beatPos = 0.0;
    float energy = 0.0f;
    bool talking = false;
    juce::Random rng;
};
