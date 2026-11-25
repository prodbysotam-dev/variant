#include "PluginEditor.h"
static juce::Image themePreview (int index);   // aperçu d'un thème (liste des thèmes)
#include "SoundFont.h"
#include <map>

//==============================================================================
// Touche Windows réelle derrière un caractère (pour savoir quand une touche chiffre / ponctuation est relâchée)
#if JUCE_WINDOWS
extern "C" __declspec(dllimport) short __stdcall VkKeyScanW (wchar_t ch);
extern "C" __declspec(dllimport) short __stdcall GetAsyncKeyState (int vKey);
#endif

static int keyVk (const juce::KeyPress& k)
{
   #if JUCE_WINDOWS
    const auto c = k.getTextCharacter();
    if (c > 32 && c < 0x10000)
    {
        const short r = VkKeyScanW ((wchar_t) c);
        if (r != -1 && (r & 0xff) != 0xff) return r & 0xff;
    }
   #else
    juce::ignoreUnused (k);
   #endif
    return 0;   // inconnu : on garde la méthode de JUCE
}

static bool vkDown (int vk)
{
   #if JUCE_WINDOWS
    return (GetAsyncKeyState (vk) & 0x8000) != 0;
   #else
    juce::ignoreUnused (vk);
    return false;
   #endif
}

//==============================================================================
// Kick hors du plugin : fenêtre transparente, toujours devant, qui danse sur le tempo de FL
struct KickDesktop : public juce::Component
{
    KickDesktop (int skin)
    {
        buddy.setSkin (skin);
        buddy.setDance (true);
        buddy.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (buddy);
        setOpaque (false);
        scale = (float) juce::jlimit (0.4, 4.0, library::settings().getDoubleValue ("kickDesktopScale", 1.0));
        setSize (sizeFor (scale).x, sizeFor (scale).y);
        addToDesktop (juce::ComponentPeer::windowIsSemiTransparent | juce::ComponentPeer::windowIsTemporary);
        setAlwaysOnTop (true);
        if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
            setTopLeftPosition (d->userArea.getRight() - getWidth() - 70, d->userArea.getBottom() - getHeight() - 60);
        setVisible (true);
    }
    static juce::Point<int> sizeFor (float s) { return { juce::roundToInt (190 * s), juce::roundToInt (240 * s) }; }
    void resized() override { buddy.setBounds (getLocalBounds()); }
    void paint (juce::Graphics&) override {}
    // taille : molette sur Kick, ou clic droit -> Taille (le bas de Kick reste au même endroit)
    void setScale (float s)
    {
        s = juce::jlimit (0.4f, 4.0f, s);
        const auto bottomRight = getBounds().getBottomRight();
        scale = s;
        const auto sz = sizeFor (s);
        setBounds (bottomRight.x - sz.x, bottomRight.y - sz.y, sz.x, sz.y);
        library::settings().setValue ("kickDesktopScale", (double) s);
        library::settings().saveIfNeeded();
    }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override { setScale (scale * (w.deltaY > 0 ? 1.12f : 1.0f / 1.12f)); }
    void mouseDown (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) dragger.startDraggingComponent (this, e); }
    void mouseDrag (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) dragger.dragComponent (this, e, nullptr); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu()) return;
        juce::PopupMenu m, sizes;
        const float opts[] = { 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f };
        const char* names[] = { "Mini", "Petit", "Normal", "Grand", "Très grand", "Géant" };
        for (int i = 0; i < 6; ++i) sizes.addItem (10 + i, fr (names[i]), true, std::abs (scale - opts[i]) < 0.05f);
        m.addSubMenu (fr ("Taille de Kick (ou molette sur lui)"), sizes);
        m.addItem (1, fr ("Rentrer Kick dans le plugin"));
        juce::Component::SafePointer<KickDesktop> self (this);
        m.showMenuAsync (juce::PopupMenu::Options(), [self, opts] (int r)
        {
            if (self == nullptr || r == 0) return;
            if (r == 1) { if (self->onClose) self->onClose(); return; }
            if (r >= 10 && r < 16) self->setScale (opts[r - 10]);
        });
    }
    std::function<void()> onClose;
    Buddy buddy;
    juce::ComponentDragger dragger;
    float scale = 1.0f;
};


//==============================================================================
// WaveformView
//==============================================================================
WaveformView::WaveformView (VariantProcessor& p) : proc (p)
{
    lastPos.fill (-1.0f);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void WaveformView::setSound (LayerSound::Ptr s)
{
    sound = std::move (s);
    peaksWidth = 0;
    repaint();
}

void WaveformView::openChooser()
{
    chooser = std::make_unique<juce::FileChooser> (fr ("Choisir un one-shot"), juce::File(),
                                                   VariantProcessor::getSupportedWildcard());

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe = juce::Component::SafePointer<WaveformView> (this)] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (safe != nullptr && f.existsAsFile())
                safe->proc.loadSample (f, safe->proc.selectedLayer);
        });
}

int WaveformView::loopMarkerAt (float x) const
{
    if (sound == nullptr || ! sound->layers[(size_t) proc.selectedLayer].valid) return -1;
    const auto id = VariantProcessor::layerId ("L", proc.selectedLayer);
    if (proc.getParam (id + "SLoop") < 0.5f) return -1;
    const auto area = getLocalBounds().toFloat().reduced (18.0f, 20.0f);
    const float xs = area.getX() + proc.getParam (id + "SLStart") * area.getWidth();
    const float xe = area.getX() + proc.getParam (id + "SLEnd") * area.getWidth();
    if (std::abs (x - xs) < 8.0f) return 0;
    if (std::abs (x - xe) < 8.0f) return 1;
    return -1;
}

// ---- enveloppe dessinée sur la forme d'onde (comme le Sampler de FL) : points à glisser
float WaveformView::sampleSecs() const
{
    if (sound == nullptr) return 1.0f;
    const auto& l = sound->layers[(size_t) juce::jlimit (0, maxLayers - 1, proc.selectedLayer)];
    if (! l.valid || l.rate <= 0.0) return 1.0f;
    return juce::jmax (0.05f, (float) (l.source.getNumSamples() / l.rate));
}

std::array<juce::Point<float>, 4> WaveformView::envPoints() const
{
    const auto area = getLocalBounds().toFloat().reduced (18.0f, 20.0f);
    const float T = sampleSecs() * 1000.0f;
    const float dl = proc.getParam ("envDelay"), a = proc.getParam ("attack"), h = proc.getParam ("envHold"), d = proc.getParam ("decay");
    const float s = proc.getParam ("sustain"), r = proc.getParam ("release");
    auto X = [&] (float ms) { return area.getX() + juce::jlimit (0.0f, 1.0f, ms / T) * area.getWidth(); };
    auto Y = [&] (float v) { return area.getBottom() - v * area.getHeight(); };
    const float relStart = juce::jmax (dl + a + h + d, T - r);
    return { juce::Point<float> { X (dl + a), Y (1.0f) },
             juce::Point<float> { X (dl + a + h + d), Y (s) },
             juce::Point<float> { X (relStart), Y (s) },
             juce::Point<float> { X (relStart + r), Y (0.0f) } };
}

int WaveformView::envPointAt (juce::Point<float> p) const
{
    if (sound == nullptr || ! sound->layers[(size_t) proc.selectedLayer].valid) return -1;
    const auto pts = envPoints();
    int best = -1; float bd = 11.0f;
    for (int k = 0; k < 4; ++k) if (k != 3 && pts[(size_t) k].getDistanceFrom (p) < bd) { bd = pts[(size_t) k].getDistanceFrom (p); best = k; }
    return best;
}

void WaveformView::setEnvFromDrag (int k, juce::Point<float> p)
{
    const auto area = getLocalBounds().toFloat().reduced (18.0f, 20.0f);
    const float T = sampleSecs() * 1000.0f;
    const float ms = juce::jlimit (0.0f, 1.0f, (p.x - area.getX()) / area.getWidth()) * T;
    const float lvl = juce::jlimit (0.0f, 1.0f, (area.getBottom() - p.y) / area.getHeight());
    const float dl = proc.getParam ("envDelay"), a = proc.getParam ("attack"), h = proc.getParam ("envHold");
    auto set = [this] (const char* id, float v)
    {
        if (auto* prm = proc.apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (prm->getNormalisableRange().snapToLegalValue (v)));
    };
    if (k == 0) set ("attack", ms - dl);
    else if (k == 1) { set ("decay", juce::jmax (1.0f, ms - dl - a - h)); set ("sustain", lvl); }
    else if (k == 2) { set ("release", juce::jmax (1.0f, T - ms)); set ("sustain", lvl); }
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    envDrag = e.mods.isPopupMenu() ? -1 : envPointAt (e.position);
    if (envDrag >= 0) { for (auto* id : { "attack", "decay", "sustain", "release" }) if (auto* p = proc.apvts.getParameter (id)) p->beginChangeGesture(); return; }
    loopDrag = loopMarkerAt (e.position.x);
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (envDrag >= 0) { setEnvFromDrag (envDrag, e.position); repaint(); return; }
    if (loopDrag < 0) return;
    const auto area = getLocalBounds().toFloat().reduced (18.0f, 20.0f);
    const auto id = VariantProcessor::layerId ("L", proc.selectedLayer);
    float v = juce::jlimit (0.0f, 1.0f, (e.position.x - area.getX()) / area.getWidth());
    if (loopDrag == 0) proc.setParam (id + "SLStart", juce::jmin (v, proc.getParam (id + "SLEnd") - 0.01f));
    else               proc.setParam (id + "SLEnd", juce::jmax (v, proc.getParam (id + "SLStart") + 0.01f));
    repaint();
}

void WaveformView::mouseUp (const juce::MouseEvent& e)
{
    if (envDrag >= 0) { for (auto* id : { "attack", "decay", "sustain", "release" }) if (auto* p = proc.apvts.getParameter (id)) p->endChangeGesture(); envDrag = -1; repaint(); return; }
    if (e.mods.isPopupMenu()) { loopDrag = -1; layermenu::show (proc, proc.selectedLayer, [safe = juce::Component::SafePointer<WaveformView> (this)] { if (safe != nullptr) safe->repaint(); }); return; }
    if (loopDrag >= 0 || e.mouseWasDraggedSinceMouseDown()) { loopDrag = -1; return; }
    openChooser();
}

void WaveformView::mouseMove (const juce::MouseEvent& e)
{
    const int ep = envPointAt (e.position);
    if (ep != envHover) { envHover = ep; repaint(); }
    setMouseCursor (ep >= 0 ? juce::MouseCursor::DraggingHandCursor : (loopMarkerAt (e.position.x) >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::PointingHandCursor));
}

bool WaveformView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (VariantProcessor::isSupportedFile (juce::File (f)))
            return true;
    return false;
}

void WaveformView::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    proc.loadFiles (files, proc.selectedLayer);
    repaint();
}

void WaveformView::tick()
{
    bool changed = false;
    {
        const auto id = VariantProcessor::layerId ("L", proc.selectedLayer);
        const float sig = proc.getParam (id + "SLoop") * 3.0f + proc.getParam (id + "SLStart") * 7.0f + proc.getParam (id + "SLEnd") * 11.0f
                        + proc.getParam ("attack") * 0.013f + proc.getParam ("decay") * 0.0071f + proc.getParam ("sustain") * 5.0f + proc.getParam ("release") * 0.0031f
                        + proc.getParam ("envDelay") * 0.017f + proc.getParam ("envHold") * 0.019f;
        if (sig != lastLoopSig) { lastLoopSig = sig; changed = true; }
    }
    for (int i = 0; i < VariantProcessor::numVoices; ++i)
    {
        const float p = proc.getVoicePosition (i, proc.selectedLayer);
        if (std::abs (p - lastPos[(size_t) i]) > 0.0005f)
        {
            lastPos[(size_t) i] = p;
            changed = true;
        }
    }
    if (changed)
        repaint();
}

void WaveformView::computePeaks (int width)
{
    peaksWidth = width;
    shownLayer = proc.selectedLayer;
    peaks.assign ((size_t) juce::jmax (0, width) * 2, 0.0f);
    if (sound == nullptr || width <= 0 || ! sound->layers[(size_t) shownLayer].valid)
        return;

    const auto& d  = sound->layers[(size_t) shownLayer].source;
    const int len  = d.getNumSamples();
    float globalMax = 0.0f;

    for (int x = 0; x < width; ++x)
    {
        const int s0 = (int) ((juce::int64) x * len / width);
        const int s1 = juce::jmin (len, juce::jmax (s0 + 1, (int) ((juce::int64) (x + 1) * len / width)));
        float mn = 0.0f, mx = 0.0f;

        for (int ch = 0; ch < d.getNumChannels(); ++ch)
        {
            const float* src = d.getReadPointer (ch);
            for (int i = s0; i < s1; ++i)
            {
                mn = juce::jmin (mn, src[i]);
                mx = juce::jmax (mx, src[i]);
            }
        }

        peaks[(size_t) x * 2]     = mn;
        peaks[(size_t) x * 2 + 1] = mx;
        globalMax = juce::jmax (globalMax, std::abs (mn), mx);
    }

    peakScale = globalMax > 0.0001f ? 0.92f / globalMax : 1.0f;
}

void WaveformView::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();

    g.setColour (Palette::panel);
    g.fillRoundedRectangle (b, 14.0f);

    if (sound == nullptr || ! sound->layers[(size_t) proc.selectedLayer].valid)
    {
        // --- état vide : zone de dépôt
        juce::Path border, dashed;
        border.addRoundedRectangle (b.reduced (1.5f), 13.0f);
        const float dashes[] = { 6.0f, 5.0f };
        juce::PathStrokeType (1.5f).createDashedStroke (dashed, border, dashes, 2);

        if (dragOver)
        {
            g.setColour (Palette::blue.withAlpha (0.07f));
            g.fillRoundedRectangle (b, 14.0f);
        }

        const auto accent = dragOver ? Palette::blue : Palette::text.withAlpha (0.75f);
        g.setColour (dragOver ? Palette::blue : Palette::line.brighter (0.4f));
        g.fillPath (dashed);

        const auto c = b.getCentre().translated (0.0f, -24.0f);
        juce::Path icon;
        icon.startNewSubPath (c.x, c.y - 14.0f);  icon.lineTo (c.x, c.y + 5.0f);
        icon.startNewSubPath (c.x - 7.0f, c.y - 2.0f); icon.lineTo (c.x, c.y + 5.0f); icon.lineTo (c.x + 7.0f, c.y - 2.0f);
        icon.startNewSubPath (c.x - 15.0f, c.y + 3.0f); icon.lineTo (c.x - 15.0f, c.y + 12.0f);
        icon.lineTo (c.x + 15.0f, c.y + 12.0f); icon.lineTo (c.x + 15.0f, c.y + 3.0f);
        g.setColour (accent);
        g.strokePath (icon, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour (Palette::text);
        g.setFont (uiFont (16.0f));
        g.drawText (dragOver ? fr ("Relâche pour charger") : fr ("Glisse un one-shot ici"),
                    b.withTrimmedTop (b.getHeight() * 0.5f + 2.0f).removeFromTop (24.0f),
                    juce::Justification::centred);

        g.setColour (Palette::dim);
        g.setFont (uiFont (12.0f));
        g.drawText (fr ("ou clique pour parcourir  ·  WAV  AIFF  FLAC  MP3  OGG  SF2 (SoundFont)"),
                    b.withTrimmedTop (b.getHeight() * 0.5f + 28.0f).removeFromTop (18.0f),
                    juce::Justification::centred);
        return;
    }

    // --- forme d'onde
    const auto area = b.reduced (18.0f, 20.0f);
    const int w = (int) area.getWidth();
    if (w != peaksWidth || shownLayer != proc.selectedLayer)
        computePeaks (w);

    const float midY = area.getCentreY();
    const float half = area.getHeight() * 0.5f;

    g.setColour (Palette::line);
    g.drawHorizontalLine ((int) midY, area.getX(), area.getRight());

    juce::Path wave;
    wave.startNewSubPath (area.getX(), midY);
    for (int x = 0; x < w; ++x)
        wave.lineTo (area.getX() + (float) x, midY - peaks[(size_t) x * 2 + 1] * peakScale * half);
    for (int x = w - 1; x >= 0; --x)
        wave.lineTo (area.getX() + (float) x, midY - peaks[(size_t) x * 2] * peakScale * half);
    wave.closeSubPath();

    g.setGradientFill (juce::ColourGradient (Palette::text.withAlpha (0.92f), area.getX(), midY,
                                             Palette::lavender.withAlpha (0.85f), area.getRight(), midY, false));
    g.fillPath (wave);

    // --- enveloppe de volume posée sur le son (points à glisser : attaque, déclin/maintien, relâche)
    {
        const auto pts = envPoints();
        const float T = sampleSecs() * 1000.0f;
        const float dl = proc.getParam ("envDelay"), h = proc.getParam ("envHold");
        const float x0 = area.getX() + juce::jlimit (0.0f, 1.0f, dl / T) * area.getWidth();
        const float xh = pts[0].x + juce::jlimit (0.0f, 1.0f, h / T) * area.getWidth();
        juce::Path env;
        env.startNewSubPath (area.getX(), area.getBottom());
        env.lineTo (x0, area.getBottom());
        env.lineTo (pts[0]);
        env.lineTo (xh, pts[0].y);
        env.lineTo (pts[1]); env.lineTo (pts[2]); env.lineTo (pts[3]);
        auto fillP = env; fillP.lineTo (pts[3].x, area.getBottom()); fillP.closeSubPath();
        // ce qui est hors de l'enveloppe est assombri
        g.saveState();
        juce::Path outside; outside.addRectangle (area.expanded (0.0f, 2.0f)); outside.addPath (fillP); outside.setUsingNonZeroWinding (false);
        g.setColour (Palette::panel.withAlpha (0.55f)); g.fillPath (outside);
        g.restoreState();
        g.setColour (Palette::mint.withAlpha (0.10f)); g.fillPath (fillP);
        g.setColour (Palette::mint.withAlpha (0.9f));
        g.strokePath (env, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        static const char* tips[] = { "Attaque", "Déclin / maintien", "Relâche" };
        for (int k = 0; k < 3; ++k)
        {
            const bool hot = k == envHover || k == envDrag;
            if (hot) { g.setColour (Palette::mint.withAlpha (0.3f)); g.fillEllipse (juce::Rectangle<float> (20.0f, 20.0f).withCentre (pts[(size_t) k])); }
            g.setColour (Palette::panel); g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre (pts[(size_t) k]));
            g.setColour (Palette::mint); g.drawEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (pts[(size_t) k]), 2.0f);
            if (hot)
            {
                g.setFont (uiFont (11.0f, juce::Font::bold));
                g.drawText (fr (tips[k]), juce::Rectangle<float> (pts[(size_t) k].x - 60.0f, pts[(size_t) k].y - 24.0f, 120.0f, 14.0f).constrainedWithin (b), juce::Justification::centred);
            }
        }
    }

    // --- points de boucle (comme dans FL : le son tourne entre les deux repères tant que la note est tenue)
    {
        const auto id = VariantProcessor::layerId ("L", proc.selectedLayer);
        if (proc.getParam (id + "SLoop") > 0.5f)
        {
            const float xs = area.getX() + proc.getParam (id + "SLStart") * area.getWidth();
            const float xe = area.getX() + proc.getParam (id + "SLEnd") * area.getWidth();
            g.setColour (Palette::mint.withAlpha (0.16f));
            g.fillRect (juce::Rectangle<float> (xs, area.getY() - 8.0f, juce::jmax (1.0f, xe - xs), area.getHeight() + 16.0f));
            for (int k = 0; k < 2; ++k)
            {
                const float x = k == 0 ? xs : xe;
                g.setColour (Palette::mint);
                g.fillRect (juce::Rectangle<float> (x - 1.0f, area.getY() - 8.0f, 2.0f, area.getHeight() + 16.0f));
                juce::Path tri;
                if (k == 0) tri.addTriangle (x, area.getY() - 8.0f, x + 10.0f, area.getY() - 8.0f, x, area.getY() + 2.0f);
                else        tri.addTriangle (x, area.getY() - 8.0f, x - 10.0f, area.getY() - 8.0f, x, area.getY() + 2.0f);
                g.fillPath (tri);
            }
            g.setFont (uiFont (11.0f, juce::Font::bold));
            g.drawText (fr ("BOUCLE"), juce::Rectangle<float> (xs + 4.0f, area.getBottom() - 4.0f, 80.0f, 14.0f), juce::Justification::left);
        }
    }

    // --- têtes de lecture (une par note jouée)
    for (int i = 0; i < VariantProcessor::numVoices; ++i)
    {
        const float p = lastPos[(size_t) i];
        if (p < 0.0f || p > 1.0f)
            continue;

        const float x = area.getX() + p * area.getWidth();
        g.setColour (Palette::blue.withAlpha (0.9f));
        g.fillRect (juce::Rectangle<float> (x - 0.75f, area.getY() - 6.0f, 1.5f, area.getHeight() + 12.0f));
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ x, area.getY() - 6.0f }));
    }

    if (dragOver)
    {
        g.setColour (Palette::blue);
        g.drawRoundedRectangle (b.reduced (1.0f), 14.0f, 2.0f);
    }

    g.setColour (Palette::dim.withAlpha (0.8f));
    g.setFont (uiFont (11.0f));
    g.drawText (fr ("Glisse un autre son pour remplacer"), b.reduced (14.0f, 8.0f), juce::Justification::topRight);
}


//==============================================================================
// VariantEditor
//==============================================================================
VariantEditor::VariantEditor (VariantProcessor& p)
    : AudioProcessorEditor (p), proc (p),
      keyboard (p.keyboardState, [&p] { return (int) p.apvts.getRawParameterValue ("root")->load(); })
{
    addMouseListener (&automationClicks, true);
    // préférences globales (langue, taille, thème)
    auto& st = library::settings();
    i18n::current().store (st.getIntValue ("language", i18n::systemDefault()));
    if (st.containsKey ("theme")) proc.themeIndex = st.getIntValue ("theme", proc.themeIndex);
    uiScale = (float) st.getDoubleValue ("uiScale", 1.25);
    {
        auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
        if (d != nullptr) uiScale = juce::jmin (uiScale, (float) (d->userArea.getHeight() - 60) / (float) baseH);
        uiScale = juce::jlimit (0.8f, 2.0f, uiScale);
    }

    themes::apply (proc.themeIndex);
    lnf.refreshColours();
    globalLnf->lnf.refreshColours();
    setLookAndFeel (&lnf);
    addAndMakeVisible (root);
    root.addMouseListener (&touch, true);

    keyboard.setOctaveForMiddleC (5);   // même numérotation que FL Studio
    keyboard.setAvailableRange (36, 96);
    keyboard.setScrollButtonsVisible (false);
    keyboard.setWantsKeyboardFocus (false);
    root.addAndMakeVisible (keyboard);

    themeBox.onChange = [this] { const int i = themeBox.getSelectedId() - 1; if (i >= 0 && i != proc.themeIndex) applyTheme (i); };
    themeBox.setTooltip (fr ("Thème de couleurs"));
    settingsButton.setTooltip (fr ("Thème, langue, taille de l'interface, raccourcis clavier"));
    settingsButton.onClick = [this] { showSettingsMenu(); };
    bankButton.setButtonText (fr ("Banque de sons"));
    bankButton.setColour (juce::TextButton::buttonColourId, Palette::blue.withAlpha (0.18f));
    bankButton.setTooltip (fr ("Ta banque de sons : tous tes sons, tes catégories et tes presets (clic sur un son = écouter, double-clic = charger dans la couche choisie)"));
    bankButton.onClick = [this]
    {
        if (libraryPanel != nullptr && libraryPanel->isVisible() && currentTab == 0) { libraryPanel->setVisible (false); return; }
        showTab (0);
        libraryPanel->setBounds (pageArea); libraryPanel->showFor (proc.selectedLayer);
    };
    root.addAndMakeVisible (bankButton);
    for (auto* b : { &bankPrev, &bankNext }) { b->setColour (juce::TextButton::buttonColourId, Palette::blue.withAlpha (0.12f)); root.addAndMakeVisible (b); }
    bankPrev.setTooltip (fr ("Son précédent du même dossier (dans la couche choisie), sans ouvrir la banque"));
    bankNext.setTooltip (fr ("Son suivant du même dossier (dans la couche choisie), sans ouvrir la banque"));
    bankPrev.onClick = [this] { stepSound (-1); };
    bankNext.onClick = [this] { stepSound (1); };
    root.addAndMakeVisible (metroButton);
    saveButton.setTooltip (fr ("Sauver ton preset : les sons de chaque couche, les VST ouverts dans Variant (avec leur son), le synthé, les effets et leurs réglages. Retrouve-le dans Banque de sons > Mes presets."));
    saveButton.onClick = [this, self = juce::Component::SafePointer<VariantEditor> (this)]
    {
        presetui::askSave (proc, [safe = self] (const juce::File& f)
        {
            if (safe != nullptr) safe->buddySay (fr ("Preset sauvé"), f.getFileNameWithoutExtension() + fr (" est rangé dans Banque de sons > Mes presets."));
        });
    };
    root.addAndMakeVisible (saveButton);
    root.addAndMakeVisible (settingsButton);
    detachButton.setTooltip (fr ("Détacher cette page dans sa propre fenêtre, à placer où tu veux dans FL Studio"));
    detachButton.onClick = [this] { detachPage (currentTab); };
    root.addAndMakeVisible (detachButton);
    placeholder.onReattach = [this] { reattachPage (currentTab); };
    placeholder.onShow = [this] { if (auto& w = windows[(size_t) currentTab]) w->toFront (true); };
    if (! proc.tasteLoaded) { proc.loadTaste(); proc.tasteLoaded = true; }
    if (telemetry::devEnv ("VARIANT_DUMP_KICK", {}).isNotEmpty())   // outil de dev : planche des skins
    {
        const int n = (int) buddySkins().size();
        juce::Image sheet (juce::Image::ARGB, n * 170, 540, true, juce::SoftwareImageType());
        {
        juce::Graphics sg (sheet);
        sg.fillAll (juce::Colour (0xff2b3137));
        for (int s = 0; s < n; ++s)
            for (int pose = 0; pose < 2; ++pose)
            {
                Buddy b; b.setSize (160, 260); b.setSkin (s); if (pose == 1) b.act(); b.tick (0.01f);
                sg.drawImageAt (b.createComponentSnapshot (b.getLocalBounds(), true, 1.0f), s * 170 + 5, 5 + pose * 270);
            }
            }
        juce::PNGImageFormat png;
        auto out = juce::File (telemetry::devEnv ("VARIANT_DUMP_KICK", {}));
        out.deleteFile();
        if (auto os = out.createOutputStream()) png.writeImageToStream (sheet, *os);
    }
    if (telemetry::devEnv ("VARIANT_TEST_LIB", {}).isNotEmpty())   // outil de dev : bibliothèque ouverte avec une recherche
    {
        juce::Component::SafePointer<VariantEditor> self (this);
        juce::Timer::callAfterDelay (1500, [self] { if (self != nullptr) { self->showTab (0); self->libraryPanel->showFor (0); if (telemetry::devEnv ("VARIANT_TEST_LIB", {}) == "1") self->libraryPanel->testSearch ("pi"); } });
    }
    if (telemetry::devEnv ("VARIANT_TEST_HOST", {}).isNotEmpty())   // outil de dev : ouvre des VST dans Variant et vérifie qu'ils sonnent
    {
        juce::Component::SafePointer<VariantEditor> self (this);
        juce::StringArray plugs; plugs.addTokens (telemetry::devEnv ("VARIANT_TEST_HOST", {}), "|", "");
        auto log = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("variant_host_test.txt");
        log.replaceWithText ({});
        for (int k = 0; k < plugs.size(); ++k)
        {
            const juce::File plug (plugs[k]);
            juce::Timer::callAfterDelay (1500 + k * 400, [self, plug, k, log]
            {
                if (self == nullptr) return;
                self->proc.hostedSlots[(size_t) k].load (plug, [self, k, log] (bool ok, const juce::String& msg)
                {
                    if (self == nullptr) return;
                    log.appendText ((ok ? "OK " : "ECHEC ") + msg + " slot " + juce::String (k) + "\n");
                    if (ok) { self->showTab (0); self->soundPage->content.resized(); self->proc.hostedSlots[(size_t) k].openWindow(); if (self->proc.kbMode == 0) self->proc.kbMode = 1; }
                });
            });
        }
        // niveau de sortie mesuré pendant quelques secondes (le script appuie sur une touche du clavier d'ordi pendant ce temps)
        for (int t = 0; t < 16; ++t)
            juce::Timer::callAfterDelay (5000 + t * 400, [self, log, t]
            {
                if (self == nullptr) return;
                bool front = false; for (auto& h : self->proc.hostedSlots) if (h.windowActive()) front = true;
                log.appendText ("t" + juce::String (t) + " niveau " + juce::String (self->proc.outLevel.load(), 4) + " fenetre devant " + juce::String ((int) front) + " touches " + juce::String ((int) self->kbHeld.size()) + "\n");
                if (t == 15) { juce::MemoryBlock st; self->proc.getStateInformation (st); log.appendText ("etat projet " + juce::String ((int) st.getSize()) + " octets\n"); }
            });
    }
    if (telemetry::devEnv ("VARIANT_TEST_SLICEBPM", {}).isNotEmpty())   // outil de dev : BPM du Sampler (nom du fichier, à la main, auto)
    {
        juce::Component::SafePointer<VariantEditor> self (this);
        auto* pp = &proc;
        const juce::File out (telemetry::devEnv ("VARIANT_TEST_SLICEBPM", {}));
        auto line = [pp, out] (const juce::String& what) { out.appendText (what + " : bpm=" + juce::String (pp->sliceBpm.load()) + " nom=" + juce::String (pp->sliceNamedBpm.load()) + " main=" + juce::String ((int) pp->sliceBpmManual.load()) + " | "); };
        juce::Timer::callAfterDelay (1500, [pp, line]
        {
            const auto f = library::defaultFolder().getChildFile ("Variant Seq").getChildFile ("3 Seq 140bpm ( @conz.xo ).wav");
            pp->loadSliceFile (f); line ("charge " + f.getFileName() + " existe=" + juce::String ((int) f.existsAsFile()));
        });
        juce::Timer::callAfterDelay (5000, [line] { line ("apres analyse"); });
        juce::Timer::callAfterDelay (5200, [pp, line] { pp->setSliceBpm (150.0); line ("a la main 150"); });
        juce::Timer::callAfterDelay (5400, [pp, line] { pp->setSliceBpm (0.0); line ("retour auto"); });
        juce::Timer::callAfterDelay (5600, [self] { if (self != nullptr) { self->showTab (4); } });
    }
    if (telemetry::devEnv ("VARIANT_TEST_WHEELREAD", {}).isNotEmpty())   // outil de dev : position de la page Son (après une vraie molette)
    {
        juce::Component::SafePointer<VariantEditor> self (this);
        for (int t : { 3000, 9000 })
            juce::Timer::callAfterDelay (t, [self, t]
            {
                if (self == nullptr) return;
                juce::File (telemetry::devEnv ("VARIANT_TEST_WHEELREAD", {})).appendText ("t" + juce::String (t) + " y=" + juce::String (self->soundPage->content.getY()) + " ");
            });
    }
    if (telemetry::devEnv ("VARIANT_TEST_WHEEL", {}).isNotEmpty())   // outil de dev : la molette fait-elle défiler la page ?
    {
        juce::Component::SafePointer<VariantEditor> self (this);
        juce::Timer::callAfterDelay (2500, [self]
        {
            if (self == nullptr) return;
            const juce::File out (telemetry::devEnv ("VARIANT_TEST_WHEEL", {}));
            juce::String res;
            auto& c = self->soundPage->content;
            for (auto rel : { juce::Point<int> (60, 330), juce::Point<int> (300, 450), juce::Point<int> (700, 330), juce::Point<int> (420, 600), juce::Point<int> (200, 760) })
            {
                c.setTopLeftPosition (c.getX(), 0);
                const auto before = c.getY();
                const auto global = c.localPointToGlobal (rel);
                auto* under = juce::Desktop::getInstance().findComponentAt (global);
                if (auto* peer = self->getPeer())
                {
                    juce::MouseWheelDetails w; w.deltaX = 0.0f; w.deltaY = -0.4f; w.isReversed = false; w.isSmooth = false; w.isInertial = false;
                    peer->handleMouseWheel (juce::MouseInputSource::InputSourceType::mouse, peer->globalToLocal (global.toFloat()), juce::Time::currentTimeMillis(), w);
                }
                res << rel.toString() << " sous=" << (under != nullptr ? juce::String (typeid (*under).name()) : juce::String ("rien")) << " avant=" << before << " apres=" << c.getY() << " | ";
            }
            out.replaceWithText (res);
        });
    }
    if (telemetry::devEnv ("VARIANT_TEST_TAKE", {}).isNotEmpty())   // outil de dev : enregistre une prise et l'exporte
    {
        auto* pp = &proc;
        const juce::File out (telemetry::devEnv ("VARIANT_TEST_TAKE", {}));
        juce::Timer::callAfterDelay (1000, [pp]
        {
            for (auto& c : library::categories()) { const auto fs = library::soundsIn (c.folder); if (! fs.isEmpty()) { pp->loadSample (fs[0], 0); break; } }
        });
        juce::Timer::callAfterDelay (2000, [pp] { pp->takeArm(); });
        juce::Timer::callAfterDelay (2600, [pp] { pp->keyboardState.noteOn (1, 60, 0.9f); });
        juce::Timer::callAfterDelay (3400, [pp] { pp->keyboardState.noteOff (1, 60, 0.0f); pp->keyboardState.noteOn (1, 64, 0.7f); });
        juce::Timer::callAfterDelay (4200, [pp] { pp->keyboardState.noteOff (1, 64, 0.0f); });
        juce::Timer::callAfterDelay (5000, [pp, out]
        {
            pp->takeStop();
            const auto w = pp->exportTakeWav(), m = pp->exportTakeMidi();
            juce::String res; res << "status=" << pp->takeStatus() << " secs=" << pp->takeSeconds() << " | "
                                  << "wav=" << w.getFullPathName() << " size=" << w.getSize() << " | "
                                  << "mid=" << m.getFullPathName() << " size=" << m.getSize()
                                  << " | couche1=" << pp->getLayerFile (0).getFileName() << " canPlay=" << (int) telemetry::canPlay()
                                  << " slicer=" << pp->getParam ("slicerOn") << " gain=" << pp->getParam ("gain") << " vol1=" << pp->getParam ("L1Vol");
            out.replaceWithText (res);
        });
    }
    if (telemetry::devEnv ("VARIANT_TEST_GATE", {}).isNotEmpty())   // outil de dev : tape un @ dans l'écran Bienvenue
    {
        juce::Component::SafePointer<VariantEditor> self (this);
        juce::Timer::callAfterDelay (1500, [self]
        {
            if (self == nullptr) return;
            juce::StringArray a; a.addTokens (telemetry::devEnv ("VARIANT_TEST_GATE", {}), "|", "");
            self->handleGate.setBounds (self->root.getLocalBounds());
            self->handleGate.setVisible (true);
            self->handleGate.toFront (true);
            self->handleGate.testSubmit (a[0]);
            juce::Timer::callAfterDelay (2500, [self, out = a[1]]
            {
                if (self == nullptr) return;
                auto img = self->handleGate.createComponentSnapshot (self->handleGate.getLocalBounds(), true, 1.0f);
                juce::Image soft (juce::Image::ARGB, img.getWidth(), img.getHeight(), true, juce::SoftwareImageType());
                { juce::Graphics sg (soft); sg.fillAll (juce::Colours::black); sg.drawImageAt (img, 0, 0); }
                juce::File f (out); f.deleteFile();
                if (auto os = f.createOutputStream()) juce::PNGImageFormat().writeImageToStream (soft, *os);
                juce::File (out + ".txt").replaceWithText (juce::String ("gateVisible=") + (self->handleGate.isVisible() ? "1" : "0")
                                                           + " hasHandle=" + (telemetry::hasHandleFlag().load() ? "1" : "0"));
            });
        });
    }
    if (telemetry::devEnv ("VARIANT_TEST_KICKASK", {}).isNotEmpty())   // outil de dev : questions posées à la vraie fenêtre de Kick
    {
        juce::Component::SafePointer<VariantEditor> self (this);
        juce::Timer::callAfterDelay (1800, [self]
        {
            if (self == nullptr) return;
            juce::StringArray qs;
            qs.addTokens (telemetry::devEnv ("VARIANT_TEST_KICKASK", {}), ";", "");
            juce::String out;
            for (auto& q : qs)
            {
                if (q.startsWith ("tab=")) { self->showTab (q.fromFirstOccurrenceOf ("=", false, false).getIntValue()); continue; }
                self->chat.ask (q);
                out << "Q: " << q << "\nT: " << self->chat.getHeading() << "\nA: " << self->chat.getAnswer().replace ("\n", " / ") << "\n\n";
            }
            juce::File (telemetry::devEnv ("VARIANT_TEST_KICKOUT", {})).replaceWithText (out, false, false, "\n");
        });
    }
    if (telemetry::devEnv ("VARIANT_HOSTSNAP", {}).isNotEmpty())   // outil de dev : ouvre un plugin dans Variant et photographie sa fenêtre (« plugin.vst3|image.png »)
    {
        const auto spec = telemetry::devEnv ("VARIANT_HOSTSNAP", {});
        const juce::File plug (spec.upToFirstOccurrenceOf ("|", false, false)), png (spec.fromFirstOccurrenceOf ("|", false, false));
        auto* pp = &proc;
        juce::Timer::callAfterDelay (1500, [pp, plug, png]
        {
            pp->hostedSlots[0].load (plug, [pp, png] (bool ok, const juce::String&)
            {
                if (! ok) { png.withFileExtension ("txt").replaceWithText ("echec"); return; }
                pp->hostedSlots[0].openWindow();
                juce::Timer::callAfterDelay (1500, [png]
                {
                    for (int i = 0; i < juce::TopLevelWindow::getNumTopLevelWindows(); ++i)
                        if (auto* w = juce::TopLevelWindow::getTopLevelWindow (i))
                            if (w->getName().contains ("dans Variant"))
                                if (auto* dw = dynamic_cast<juce::DocumentWindow*> (w))
                                    if (auto* c = dw->getContentComponent())
                                    {
                                        auto img = c->createComponentSnapshot (c->getLocalBounds(), true, 1.0f);
                                        png.deleteFile();
                                        juce::FileOutputStream os (png);
                                        if (os.openedOk()) juce::PNGImageFormat().writeImageToStream (img, os);
                                    }
                });
            });
        });
    }
    if (telemetry::devEnv ("VARIANT_I18N_DUMP", {}).isNotEmpty())   // outil de dev : tous les textes affichés sans traduction
    {
        juce::Component::SafePointer<VariantEditor> self (this);
        const int lang = telemetry::devEnv ("VARIANT_I18N_LANG", "1").getIntValue();
        juce::Timer::callAfterDelay (1200, [self, lang]
        {
            if (self == nullptr) return;
            i18n::current().store (lang);
            i18n::recording().store (true);
            self->buildAll();
            // listes de données (effets, réglages, presets d'effets, ambiances…)
            for (int t = 0; t < fxs::NumTypes; ++t)
            {
                const auto& d = fxs::def (t);
                fr (d.name); fr (d.desc);
                for (auto& pd : d.params) if (pd.name != nullptr) { fr (pd.name); if (pd.text) for (int k = 0; k <= 200; ++k) pd.text ((float) k / 200.0f); }
                for (auto& pr : d.presets) fr (pr.name);
            }
            moods::names(); fxs::typeNames(); LayerRouter::modeNames();
        });
        for (int t = 0; t < 6; ++t)
            juce::Timer::callAfterDelay (2000 + t * 700, [self, t] { if (self != nullptr && t < self->tabs.size()) { self->showTab (t); self->repaint(); } });
        juce::Timer::callAfterDelay (7000, [self]
        {
            if (self == nullptr) return;
            if (self->arpPage != nullptr) { self->showTab (3); self->arpPage->setMoreOpen (true); }
            self->handleGate.setVisible (true); self->blockedScreen.setVisible (true);
            if (auto* top = self->getTopLevelComponent()) top->createComponentSnapshot (top->getLocalBounds());
        });
        juce::Timer::callAfterDelay (9000, []
        {
            juce::String out;
            out << "#lang=" << i18n::current().load() << " rec=" << (int) i18n::recording().load() << " probe=" << fr ("zzz sonde") << "\n";
            { const std::lock_guard<std::mutex> g (i18n::missLock()); for (auto& s : i18n::missed()) out << juce::String::fromUTF8 (s.c_str()).replace ("\n", "\\n") << "\n"; }
            juce::File (telemetry::devEnv ("VARIANT_I18N_DUMP", {})).replaceWithText (out, false, false, "\n");
            juce::JUCEApplicationBase::quit();
        });
    }
    if (telemetry::devEnv ("VARIANT_TOUR_SNAP", {}).isNotEmpty())   // outil de dev : une image de chaque étape de la visite
    {
        juce::Component::SafePointer<VariantEditor> self (this);
        const int lang = telemetry::devEnv ("VARIANT_I18N_LANG", "-1").getIntValue();
        juce::Timer::callAfterDelay (1500, [self, lang]
        {
            if (self == nullptr) return;
            if (lang >= 0) { i18n::current().store (lang); self->buildAll(); }
            self->startTour();
            const int n = self->tour.numSteps();
            for (int k = 0; k < n; ++k)
                juce::Timer::callAfterDelay (800 + k * 1100, [self, k, n]
                {
                    if (self == nullptr) return;
                    self->tour.goTo (k);
                    juce::Timer::callAfterDelay (700, [self, k, n]
                    {
                        if (self == nullptr) return;
                        const juce::File dir (telemetry::devEnv ("VARIANT_TOUR_SNAP", {}));
                        dir.createDirectory();
                        auto img = self->createComponentSnapshot (self->getLocalBounds(), true, 1.0f);
                        juce::FileOutputStream os (dir.getChildFile ("tour" + juce::String (k).paddedLeft ('0', 2) + ".png"));
                        if (os.openedOk()) { os.setPosition (0); os.truncate(); juce::PNGImageFormat().writeImageToStream (img, os); }
                        if (k == n - 1) juce::JUCEApplicationBase::quit();
                    });
                });
        });
    }
    if (telemetry::devEnv ("VARIANT_SNAPSHOT", {}).isNotEmpty())   // outil de dev : une image de chaque onglet
    {
        juce::Component::SafePointer<VariantEditor> self (this);
        if (telemetry::devEnv ("VARIANT_I18N_LANG", {}).isNotEmpty() && telemetry::devEnv ("VARIANT_I18N_DUMP", {}).isEmpty())
        {
            const int lang = telemetry::devEnv ("VARIANT_I18N_LANG", "1").getIntValue();
            juce::Timer::callAfterDelay (800, [self, lang] { if (self == nullptr) return; i18n::current().store (lang); self->buildAll(); });
        }
        for (int t = 0; t < 6; ++t)
            juce::Timer::callAfterDelay (1500 + t * 900, [self, t]
            {
                if (self == nullptr || t >= self->tabs.size()) return;
                self->showTab (t);
                juce::Timer::callAfterDelay (500, [self, t]
                {
                    if (self == nullptr) return;
                    const juce::File dir (telemetry::devEnv ("VARIANT_SNAPSHOT", {}));
                    dir.createDirectory();
                    auto img = self->createComponentSnapshot (self->getLocalBounds(), true, 1.0f);
                    juce::FileOutputStream os (dir.getChildFile ("tab" + juce::String (t) + ".png"));
                    if (os.openedOk()) { os.setPosition (0); os.truncate(); juce::PNGImageFormat().writeImageToStream (img, os); }
                });
            });
    }
    if (telemetry::devEnv ("VARIANT_SELFTEST", {}).isNotEmpty())   // outil de dev : automation du synthé / des VST, visualiseur de l'arpège
    {
        auto* pp = &proc;
        auto out = std::make_shared<juce::String>();
        auto* cut = pp->apvts.getParameter ("syn_F_Cutoff");
        juce::Timer::callAfterDelay (1200, [pp, out, cut]
        {
            if (cut == nullptr) { *out << "ECHEC : pas de parametre syn_F_Cutoff\n"; return; }
            pp->wtSynth.patch.set (wt::F_Cutoff, 0.33f);   // comme un potard tourné dans Variant
            if (auto* a = pp->apvts.getParameter ("arpOn")) a->setValueNotifyingHost (1.0f);
            pp->keyboardState.noteOn (1, 60, 0.8f); pp->keyboardState.noteOn (1, 64, 0.8f); pp->keyboardState.noteOn (1, 67, 0.8f);
        });
        juce::Timer::callAfterDelay (1700, [pp, out, cut]
        {
            if (cut == nullptr) return;
            const float raw = pp->apvts.getRawParameterValue ("syn_F_Cutoff")->load();
            *out << (std::abs (raw - 0.33f) < 1.0e-3f ? "ok" : "ECHEC") << " : potard du synthe -> parametre FL (" << raw << ")\n";
            cut->setValueNotifyingHost (cut->convertTo0to1 (0.7f));   // comme un clip d'automation de FL
            if (auto* v = pp->apvts.getParameter ("h1Vol")) v->setValueNotifyingHost (v->convertTo0to1 (-12.0f));
        });
        juce::Timer::callAfterDelay (2600, [pp, out]
        {
            const float c = pp->wtSynth.patch.get (wt::F_Cutoff);
            *out << (std::abs (c - 0.7f) < 1.0e-3f ? "ok" : "ECHEC") << " : automation FL -> synthe (" << c << ")\n";
            const float g = juce::Decibels::gainToDecibels (pp->hostedSlots[0].volume.load());
            *out << (std::abs (g + 12.0f) < 0.05f ? "ok" : "ECHEC") << " : automation FL -> volume du VST 1 (" << g << " dB)\n";
            int in = 0, outN = 0;
            const int w = pp->arpViewWrite.load();
            for (int i = juce::jmax (0, w - VariantProcessor::arpViewSize); i < w; ++i) { const auto& e = pp->arpView[(size_t) (i % VariantProcessor::arpViewSize)]; if (e.on) (e.input ? in : outN)++; }
            *out << (in >= 3 && outN >= 3 ? "ok" : "ECHEC") << " : visualiseur (" << in << " notes tenues, " << outN << " notes d'arpege)\n";
            pp->keyboardState.allNotesOff (1);
            juce::File (telemetry::devEnv ("VARIANT_SELFTEST", {})).replaceWithText (*out);
        });
    }
    if (telemetry::devEnv ("VARIANT_PRESET_AUDIT", {}).isNotEmpty())   // outil de dev : chaque preset de la banque joué sur un accord
    {
        struct FakeHead : juce::AudioPlayHead
        {
            juce::int64 pos = 0; bool playing = false;
            juce::Optional<PositionInfo> getPosition() const override
            {
                PositionInfo i; i.setBpm (120.0); i.setIsPlaying (playing); i.setTimeInSamples (pos);
                i.setPpqPosition (pos / 44100.0 * 2.0); i.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
                return i;
            }
        };
        struct Audit : juce::Timer
        {
            FakeHead head;
            std::unique_ptr<VariantProcessor> p = std::make_unique<VariantProcessor>();
            juce::Array<juce::File> files = presets::userPresets();
            int idx = -1, phase = 0;
            juce::String out;
            Audit() { files.addArray (synthpresets::list()); p->prepareToPlay (44100.0, 512); head.playing = telemetry::devEnv ("VARIANT_PRESET_PLAY", {}).isNotEmpty(); p->setPlayHead (&head); startTimer (400); }
            void render (std::vector<float>& mono, bool notes)
            {
                juce::AudioBuffer<float> b (2, 512);
                const int blocks = (int) (44100.0 * 3.0 / 512);
                for (int k = 0; k < blocks; ++k)
                {
                    b.clear();
                    juce::MidiBuffer m;
                    if (notes && k == 0) for (int n : { 60, 64, 67 }) m.addEvent (juce::MidiMessage::noteOn (1, n, 0.8f), 0);
                    if (notes && k == blocks - 20) for (int n : { 60, 64, 67 }) m.addEvent (juce::MidiMessage::noteOff (1, n), 0);
                    p->processBlock (b, m);
                    head.pos += 512;
                    for (int i = 0; i < 512; ++i) mono.push_back (0.5f * (b.getSample (0, i) + b.getSample (1, i)));
                }
            }
            void timerCallback() override
            {
                if (phase == 0)
                {
                    if (++idx >= files.size())
                    {
                        stopTimer();
                        juce::File (telemetry::devEnv ("VARIANT_PRESET_AUDIT", {})).replaceWithText (out);
                        juce::JUCEApplicationBase::quit();
                        return;
                    }
                    if (files[idx].hasFileExtension ("vsynth"))
                    {
                        p = std::make_unique<VariantProcessor>(); p->prepareToPlay (44100.0, 512); p->setPlayHead (&head);   // synthé seul, sans arpège ni sons
                        for (int l = 1; l <= 8; ++l) if (auto* q = p->apvts.getParameter ("L" + juce::String (l) + "On")) q->setValueNotifyingHost (0.0f);
                        if (auto* q = p->apvts.getParameter ("arpOn")) q->setValueNotifyingHost (0.0f);
                        synthpresets::load (p->wtSynth, files[idx]);
                        p->wtSynth.enabled.store (true);
                    }
                    else
                    p->loadUserPreset (files[idx]);
                    phase = 1;   // laisse le temps aux sons de se charger
                    return;
                }
                phase = 0;
                std::vector<float> warm; render (warm, false);
                std::vector<float> y; render (y, true);
                const int w = 441; int nw = 0, silent = 0; double e = 0, peak = 0; int jumps = 0;
                for (size_t s0 = 0; s0 + (size_t) w <= y.size() - 20 * 512; s0 += (size_t) w)
                {
                    double we = 0;
                    for (int i = 0; i < w; ++i) { const double v = y[s0 + (size_t) i]; we += v * v; peak = juce::jmax (peak, std::abs (v)); }
                    e += we; ++nw;
                    if (std::sqrt (we / w) < 0.003) ++silent;
                }
                for (size_t i = 1; i < y.size(); ++i) if (std::abs (y[i] - y[i - 1]) > 0.25f) ++jumps;
                const double rms = std::sqrt (e / juce::jmax (1, nw * w));
                // durée moyenne de chaque note audible (fenêtres de 5 ms au-dessus de 10 % du max) : des clics = quelques ms
                std::vector<double> env5;
                for (size_t s0 = 0; s0 + 220 <= y.size(); s0 += 220) { double we = 0; for (int i = 0; i < 220; ++i) we += y[s0 + (size_t) i] * y[s0 + (size_t) i]; env5.push_back (std::sqrt (we / 220)); }
                double emax = 0; for (auto v : env5) emax = juce::jmax (emax, v);
                int onsets = 0, loud = 0; bool low = true;
                for (auto v : env5) { if (v > 0.1 * emax) { ++loud; if (low) ++onsets; low = false; } else if (v < 0.03 * emax) low = true; }
                const double msPerNote = onsets > 0 ? 5.0 * loud / onsets : 0.0;
                if (telemetry::devEnv ("VARIANT_PRESET_WAVS", {}).isNotEmpty())
                {
                    juce::File dir (telemetry::devEnv ("VARIANT_PRESET_WAVS", {})); dir.createDirectory();
                    juce::AudioBuffer<float> ab (1, (int) y.size()); for (size_t i = 0; i < y.size(); ++i) ab.setSample (0, (int) i, y[i]);
                    juce::WavAudioFormat wf; std::unique_ptr<juce::OutputStream> os (new juce::FileOutputStream (dir.getChildFile (files[idx].getFileNameWithoutExtension() + ".wav")));
                    if (auto* wr = wf.createWriterFor (os.get(), 44100.0, 1, 16, {}, 0)) { os.release(); std::unique_ptr<juce::AudioFormatWriter> ww (wr); ww->writeFromAudioSampleBuffer (ab, 0, ab.getNumSamples()); }
                }
                out << files[idx].getFileNameWithoutExtension() << " | rms " << juce::String (juce::Decibels::gainToDecibels ((float) rms), 1)
                    << " dB | silence " << juce::String (100.0 * silent / juce::jmax (1, nw), 0) << " % | crete " << juce::String (juce::Decibels::gainToDecibels ((float) (peak / (rms + 1e-9))), 1)
                    << " dB | sauts " << jumps << " | notes " << onsets << " | ms/note " << juce::String (msPerNote, 0) << "\n";
            }
        };
        static std::unique_ptr<Audit> audit;
        juce::Timer::callAfterDelay (2500, [] { audit = std::make_unique<Audit>(); });
    }
    if (telemetry::devEnv ("VARIANT_DUMP_HINTS", {}).isNotEmpty())   // outil de dev : toutes les explications que Kick peut lire
    {
        juce::Component::SafePointer<VariantEditor> self (this);
        juce::Timer::callAfterDelay (1500, [self]
        {
            if (self == nullptr) return;
            std::vector<KickChat::UiHint> hints;
            self->chat.gatherUi (hints);
            juce::String out;
            for (auto& h : hints) out << h.tab << " | " << h.title << " | " << h.text.replace ("\n", " / ") << "\n";
            juce::File (telemetry::devEnv ("VARIANT_DUMP_HINTS", {})).replaceWithText (out, false, false, "\n");
        });
    }
    if (telemetry::devEnv ("VARIANT_DUMP_UI", {}).isNotEmpty())   // outil de dev : captures des pages
    {
        juce::Component::SafePointer<VariantEditor> self (this);
        juce::Timer::callAfterDelay (1500, [self]
        {
            if (self == nullptr) return;
            const juce::File dir (telemetry::devEnv ("VARIANT_DUMP_UI", {}));
            dir.createDirectory();
            auto save = [dir] (juce::Component& c, const juce::String& name)
            {
                auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
                juce::Image soft (juce::Image::ARGB, img.getWidth(), img.getHeight(), true, juce::SoftwareImageType());
                { juce::Graphics sg (soft); sg.fillAll (Palette::bg); sg.drawImageAt (img, 0, 0); }
                juce::PNGImageFormat png;
                auto f = dir.getChildFile (name + ".png"); f.deleteFile();
                if (auto os = f.createOutputStream()) png.writeImageToStream (soft, *os);
            };
            {
                juce::File snd;
                for (auto& c : library::categories()) { for (auto& s : library::soundsIn (c.folder)) if (s.hasFileExtension ("wav")) { snd = s; break; } if (snd.existsAsFile()) break; }
                if (snd.existsAsFile()) self->proc.loadFiles ({ snd.getFullPathName() }, 0);
            }
            self->showTab (0); save (self->soundPage->content, "son");
            self->showTab (2); save (self->chordPage->content, "accords");
            self->showTab (4); save (self->toolsPage->content, "outils");
            self->showTab (5); save (self->synthPage->content, "synthe");
            self->showTab (3); self->arpPage->setMoreOpen (false); save (*self->arpPage, "arpege");
            self->arpPage->setMoreOpen (true); save (*self->arpPage, "arpege_options"); self->arpPage->setMoreOpen (false);
            self->handleGate.setBounds (self->root.getLocalBounds()); self->handleGate.setVisible (true);
            save (self->handleGate, "bienvenue"); self->handleGate.setVisible (false);
            { const bool was = self->blockedScreen.isVisible(); self->blockedScreen.setBounds (self->root.getLocalBounds()); self->blockedScreen.setVisible (true);
              save (self->blockedScreen, "desactive"); self->blockedScreen.setVisible (was); }
            self->reportPanel.setBounds (self->root.getLocalBounds()); self->reportPanel.open(); save (self->reportPanel, "bug"); self->reportPanel.setVisible (false);
            self->statsPanel.setBounds (self->root.getLocalBounds()); self->statsPanel.open();
            juce::Timer::callAfterDelay (4000, [self, save] { if (self != nullptr) { auto s = save; s (self->statsPanel, "stats"); self->statsPanel.setVisible (false); } });
            self->showTab (1);
            self->proc.setParam ("fx1Type", (float) fxs::EQ); self->proc.applyEffectPreset (0, 6);
            self->proc.setParam ("fx2Type", (float) fxs::Harmonic); self->proc.applyEffectPreset (1, 0);
            self->proc.setParam ("fx3Type", (float) fxs::Reverb); self->proc.applyEffectPreset (2, 4);
            juce::Timer::callAfterDelay (1200, [self, save]
            {
                if (self == nullptr) return;
                save (*self->fxPage, "effets");
                self->showTab (0); save (self->soundPage->content, "son2");   // avec le son chargé (enveloppe sur la forme d'onde)
                self->showTab (1);
                save (*self, "fenetre");
                self->showTab (5);
                self->synthPage->tick();
                save (*self, "fenetre_synthe");
                {
                    // aperçu d'un menu (fenêtre à part : dessiné à la main avec le style)
                    juce::Image img (juce::Image::ARGB, 300, 190, true, juce::SoftwareImageType());
                    juce::Graphics mg (img);
                    mg.fillAll (Palette::bg);
                    auto& lf = self->globalLnf->lnf;
                    mg.setOrigin ({ 20, 10 });
                    lf.drawPopupMenuBackground (mg, 260, 170);
                    const char* items[] = { "Tables de base", "Serum / Analog", "Serum / Digital", "", "Charger un fichier .wav…" };
                    for (int i = 0; i < 5; ++i)
                        lf.drawPopupMenuItem (mg, { 6, 8 + i * 30, 248, items[i][0] ? 28 : 9 }, items[i][0] == 0, true, i == 1, i == 0, i < 3, juce::String::fromUTF8 (items[i]), {}, nullptr, nullptr);
                    juce::PNGImageFormat png;
                    auto f = juce::File (telemetry::devEnv ("VARIANT_DUMP_UI", {})).getChildFile ("menu.png"); f.deleteFile();
                    if (auto os = f.createOutputStream()) png.writeImageToStream (img, *os);
                }
                {
                    // liste des thèmes (avec aperçus) et des skins (avec portraits) telle qu'elle s'affiche
                    const int nT = (int) themes::list().size(), nS = (int) buddySkins().size();
                    juce::Image img (juce::Image::ARGB, 620, 30 * juce::jmax (nT, nS) + 20, true, juce::SoftwareImageType());
                    juce::Graphics mg (img);
                    mg.fillAll (Palette::bg);
                    auto& lf = self->globalLnf->lnf;
                    const auto nms = themes::names();
                    std::vector<int> order; for (int i = 0; i < nT; ++i) order.push_back (i);
                    std::sort (order.begin(), order.end(), [&nms] (int a, int b) { return nms[a].compareNatural (nms[b]) < 0; });
                    for (int k = 0; k < nT; ++k)
                    {
                        juce::DrawableImage di; di.setImage (themePreview (order[(size_t) k]));
                        lf.drawPopupMenuItem (mg, { 6, 10 + k * 30, 290, 30 }, false, true, false, order[(size_t) k] == self->proc.themeIndex, false, nms[order[(size_t) k]], {}, &di, nullptr);
                    }
                    const auto so = sortedSkins();
                    for (int k = 0; k < (int) so.size(); ++k)
                    {
                        juce::DrawableImage di; di.setImage (skinIcon (so[(size_t) k]));
                        lf.drawPopupMenuItem (mg, { 316, 10 + k * 30, 290, 30 }, false, true, false, false, false, fr (buddySkins()[(size_t) so[(size_t) k]].name), {}, &di, nullptr);
                    }
                    juce::PNGImageFormat png;
                    auto f = juce::File (telemetry::devEnv ("VARIANT_DUMP_UI", {})).getChildFile ("menus.png"); f.deleteFile();
                    if (auto os = f.createOutputStream()) png.writeImageToStream (img, *os);
                }
                const auto themesEnv = telemetry::devEnv ("VARIANT_DUMP_THEMES", {});
                if (themesEnv.isNotEmpty())
                {
                    const int n = (int) themes::list().size(), orig = self->proc.themeIndex;
                    self->applyTheme (n - 2); self->showTab (2); save (*self, "theme_jungle");
                    self->applyTheme (n - 1); self->showTab (5); save (*self, "theme_desert");
                    self->applyTheme (orig);   // on remet le thème de l'utilisateur
                }
            });
        });
    }
    // SoundFont (.sf2) : on propose la liste de ses presets, classés par banque
    proc.onSoundFontDropped = [this] (const juce::File& f, int layer)
    {
        auto sf = std::make_shared<sf2::File>();
        if (! sf->open (f)) { sayQuote(); return; }
        const auto& ps = sf->getPresets();
        juce::PopupMenu m;
        m.addSectionHeader (f.getFileNameWithoutExtension() + juce::String::fromUTF8 ("  ·  choisis un preset"));
        std::map<int, juce::PopupMenu> banks;
        for (auto& p : ps)
            banks[p.bank].addItem (p.index + 1, juce::String (p.number).paddedLeft ('0', 3) + "  " + p.name);
        if (banks.size() == 1 && ps.size() <= 40) { for (auto& p : ps) m.addItem (p.index + 1, juce::String (p.number).paddedLeft ('0', 3) + "  " + p.name); }
        else for (auto& [b, sub] : banks) m.addSubMenu ("Banque " + juce::String (b), sub);
        juce::Component::SafePointer<VariantEditor> self (this);
        m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
            [safe = self, f, layer] (int r)
            {
                if (safe != nullptr && r > 0 && safe->proc.loadSoundFontPreset (f, r - 1, layer))
                    safe->repaint();
            });
    };
    buddyOn = st.getBoolValue ("buddy", true);
    buddy.setTooltip ({});
    buddy.setSkin (st.getIntValue ("buddySkin", 0));
    buddy.onClick = [this]
    {
        // clic sur Kick : on ouvre / ferme la discussion
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (now - lastBuddyClick < 700.0) { if (++buddySpam >= 3) { buddySpam = 0; chat.setVisible (false);
            static const char* spam[] = { "Arrête de me cliquer dessus, ça chatouille.", "Encore un clic et je me barre.", "T'es en train de tester ma patience là ?", "Je suis pas un bouton hein. Enfin si. Mais quand même." };
            sayQuote(); muteTicks = 60; lastBuddyClick = now; return; } }
        else buddySpam = 0;
        lastBuddyClick = now;
        if (chat.isVisible()) { chat.setVisible (false); return; }
        bubble.setVisible (false);
        chat.reset();
        chat.setVisible (true);
        chat.toFront (false);
        buddy.toFront (false);
        chat.focusInput();
    };
    buddy.onRightClick = [this] { showSkinMenu(); };
    chat.onShowTab = [this] (int tab) { chat.setVisible (false); showTab (tab); };
    chat.onTour = [this] { chat.setVisible (false); startTour(); };
    chat.onTip = [this] { sayTip(); };
    chat.getTab = [this] { return currentTab; };
    chat.gatherUi = [this] (std::vector<KickChat::UiHint>& out)
    {
        std::function<void (juce::Component&, int)> walk = [&] (juce::Component& c, int tab)
        {
            if (auto* tc = dynamic_cast<juce::TooltipClient*> (&c))
            {
                const auto tip = tc->getTooltip();
                if (tip.isNotEmpty())
                {
                    juce::String title;
                    if (auto* b = dynamic_cast<juce::Button*> (&c)) title = b->getButtonText();
                    // potards et listes : le nom écrit à côté (label du LabeledKnob / SKnob / LabeledCombo)
                    if (title.isEmpty() && c.getParentComponent() != nullptr && c.getParentComponent()->getNumChildComponents() <= 3)
                        for (auto* sib : c.getParentComponent()->getChildren())
                            if (auto* lb = dynamic_cast<juce::Label*> (sib))
                                if (lb->getText().trim().isNotEmpty() && lb->getText().length() <= 40) { title = lb->getText().trim(); break; }
                    if (title.length() > 40) title = {};
                    out.push_back ({ title, tip, tab });
                }
            }
            for (auto* ch : c.getChildren()) walk (*ch, tab);
        };
        for (int t = 0; t < numPages; ++t) if (auto* p = pageAt (t)) walk (*p, t);
        for (juce::Component* c : { (juce::Component*) &obliterateBtn, (juce::Component*) &detachButton, (juce::Component*) &settingsButton, (juce::Component*) &bankButton })
            walk (*c, -1);
    };
    tour.onTab = [this] (int tab) { showTab (tab); };
    tour.onFinished = [this]
    {
        library::settings().setValue ("tourDone", true);
        library::settings().saveIfNeeded();
        showTab (0);
        if (buddyOn) { buddySay (fr ("Bienvenue dans la team"), fr ("Si t'es perdu, clique sur moi et pose ta question. Allez, fais du son.")); muteTicks = 90; }
    };
    detach::open() = [this] (detach::Maker make, const juce::String& title, int w, int h) { openPanel (std::move (make), title, w, h); };
    initObliterate();

    // premier lancement sur ce PC : la personne entre son @ Instagram (écran par-dessus tout le plugin)
    const bool devRun = telemetry::devEnv ("VARIANT_DUMP_UI", {}).isNotEmpty()
                        || telemetry::devEnv ("VARIANT_DUMP_KICK", {}).isNotEmpty();
    const bool askHandle = telemetry::HandleGate::needed() && ! devRun;
    if (askHandle)
    {
        handleGate.setBounds (root.getLocalBounds());
        handleGate.setVisible (true);
        handleGate.toFront (true);
    }

    // au lancement : Kick sort sa réplique (pas pendant la visite guidée de la première fois)
    if (library::settings().getBoolValue ("tourDone", false) && ! askHandle)
        juce::Timer::callAfterDelay (1200, [safe = juce::Component::SafePointer<VariantEditor> (this)] { if (safe != nullptr && safe->isShowing()) safe->sayQuote(); });
}

void VariantEditor::sayTip()
{
    {
        static const char* tips[] = {
            "Le bouton Détacher (deux fenêtres) en haut détache la page. Tu la poses où tu veux, c'est toi le boss.",
            "Plus d'inspi pour l'arpège ? « Tout au hasard » et ça repart direct.",
            "Balance un break dans le Sampler, clique « Arpéger les tranches »… ça part vite en freestyle.",
            "Outil Slide du piano roll : clique une note, elle fait glisser les notes de sa couleur jusqu'à elle, comme dans FL.",
            "« Surprends-moi » dans le générateur : zéro effort, que des vibes.",
            "Glisse les pastilles MIDI direct dans FL. Pas besoin d'exporter, on n'est plus en 2005.",
            "Espace pour écouter. Basique, mais efficace.",
            "Rien ne marche et ça te saoule ? La bombe en haut remet tout à zéro. Thérapeutique.",
            "Ouvre Serum ou Zenology DANS Variant (page Son, « Plugin dans Variant ») : mes effets et mon arpège passent dessus.",
            "Clic droit sur un son de la bibliothèque : range-le dans tes catégories à toi (Trap, Ambiant…).",
            "Le bouton S sous une couche = elle joue seule. Pratique pour régler un son sans les autres.",
            "Les points verts sur la forme d'onde, c'est l'enveloppe : attrape-les, c'est plus rapide que les potards.",
            "Un son trop fort qui grésille ? Réglages → « Niveau auto » le baisse tout seul au chargement.",
            "Chroma recale n'importe quel son sur ta gamme. Même une batterie. Si si.",
            "Couleur de Chroma au-delà de 100 % : les notes de ta gamme se mettent à résonner.",
            "Dans les effets, attrape le dessin et glisse : chaque effet a 2 réglages cachés dedans.",
            "Plusieurs effets ouverts en même temps ? Oui monsieur. Clique sur le titre pour replier.",
            "Le Gross Beat (Temps & Volume) aimante tes points sur la grille. Alt pour poser librement.",
            "Sur la page Synthé, touche les courbes : filtre, enveloppes et LFO se règlent à la souris.",
            "Clic droit sur un bouton du synthé = « Moduler avec » un LFO ou une enveloppe. Ça bouge tout seul.",
            "Le bouton Hasard du synthé fabrique un son musical à chaque clic. Clique jusqu'à ce que ça claque.",
            "Dépose un .wav sur une table d'onde du synthé : il devient une nouvelle table.",
            "La colonne « Effets rapides » du Synthé : clique une tuile, l'effet rejoint ta chaîne.",
            "Choisis une gamme dans le piano roll : tous les accords créés restent dedans. « Hors gamme » pour de la liberté.",
            "« Play de FL » allumé : quand tu lances FL, c'est la progression du plugin qui joue.",
            "Clique sur la règle du piano roll pour choisir d'où la lecture démarre.",
            "Ctrl + molette dans le piano roll = zoom. Maj + molette = défiler dans le temps.",
            "Ctrl + D duplique ta sélection dans le piano roll. Le même raccourci que FL.",
            "L'outil Accord (tampon) pose un accord entier de la gamme en un clic.",
            "« Accords autour de mes notes » : dessine une mélodie, je pose des accords dessous.",
            "« J'aime » / « Pas mon style » : je retiens tes goûts pour les prochaines progressions.",
            "Monte « Variété » pour des accords plus colorés : sus, add9, quartes, renversements.",
            "Le registre (grave / aigu) change où se placent les accords. Essaie « aigu » pour un truc aérien.",
            "Clavier d'ordi activé ? A, Z, E… jouent des notes, et une autre rangée joue des accords entiers.",
            "Le bouton Rec enregistre ce que tu joues au clavier, directement dans le piano roll.",
            "Les presets « Synth - … » de l'arpège ont leur propre son de synthé. Aucun sample nécessaire.",
            "Arpège trop sage ? « Motif au hasard » change le rythme sans toucher au son.",
            "Swing sur l'arpège = groove. 15 % suffit souvent.",
            "Tes one-shots d'arpège (arp, seq…) se calent tout seuls sur le tempo de FL.",
            "Le Stretch garde la même durée pour toutes les notes : tes accords d'arpège restent ensemble.",
            "Le Sampler découpe un break en tranches. C0 = tranche 1, C#0 = tranche 2, et ainsi de suite.",
            "« Mes chops » dans le Sampler : trace tes propres zones à découper.",
            "Les pads du Sampler s'enregistrent : rec, tape tes chops, ça part dans le piano roll.",
            "Glisse une boucle dans le Sampler : je trouve son tempo et sa tonalité.",
            "Les variations à chaque note rendent un one-shot vivant. « Réaliste » = comme un vrai musicien.",
            "Loop points : le son tourne entre deux repères tant que tu tiens la note. Parfait pour les nappes.",
            "Le mode Legato du Slide = les 808 qui glissent. Temps = durée du glissement.",
            "« Naturel » dans le mélange des couches ajoute de petits décalages entre elles. Plus organique.",
            "Une note = une couche : un accord de 3 notes joue 3 sons différents. Mode « Une note = une couche ».",
            "Thème Jungle, Thème Désert… va voir dans la liste des thèmes, il y a des surprises.",
            "Les effets visuels se règlent dans Réglages : désactivés, discrets ou normaux.",
            "Je danse quand FL joue. Si ça t'énerve, clic droit sur moi → « Désactiver Kick ».",
            "Ctrl + Z annule presque tout : notes, effets, réglages. Ctrl + Y pour refaire.",
            "Le métronome de l'en-tête suit la lecture du plugin. Un clic pour l'allumer.",
            "Chaîne au hasard (page Effets) : une chaîne cohérente d'effets dans un style, en un clic.",
            "Le Delay se cale sur le tempo. Ping-pong pour que ça rebondisse gauche-droite.",
            "Réverb : « Durée tempo » cale la queue sur la mesure. Propre pour les drops.",
            "L'effet Nappe transforme n'importe quel son en pad. Essaie sur une voix.",
            "Glitch : 6 pistes d'effets sur 16 pas. Clique la grille, ça découpe en rythme.",
            "Liquide : dessine une courbe, elle fait bouger un flanger ou un phaser en rythme.",
            "Le Bruit ajoute du souffle (vinyle, bande, radio) qui suit le son. Lo-fi instantané.",
            "Les presets d'effets se changent en un clic sur la liste en haut de chaque carte.",
            "Exporte « Tout .mid » et glisse-le dans la playlist de FL : accords, basse et arpège d'un coup.",
            "Tu peux étirer la fenêtre du plugin comme tu veux. Tout se réarrange.",
            "F1 = la liste des raccourcis clavier. Pour les pros.",
            "Les catégories de la bibliothèque : la recherche marche aussi sur leurs noms." };
        static const char* intros[] = { "Petit tips", "Écoute bien", "Le move", "Entre nous", "Cadeau", "Trust", "Le savais-tu", "Astuce" };
        constexpr int n = (int) (sizeof (tips) / sizeof (tips[0]));
        // toutes les astuces passent avant qu'une revienne (ordre mélangé, sans répétition)
        if (tipOrder.empty() || tipIndex >= (int) tipOrder.size())
        {
            tipOrder.resize ((size_t) n);
            for (int i = 0; i < n; ++i) tipOrder[(size_t) i] = i;
            juce::Random r;
            for (int i = n - 1; i > 0; --i) std::swap (tipOrder[(size_t) i], tipOrder[(size_t) r.nextInt (i + 1)]);
            tipIndex = 0;
        }
        const int t = tipOrder[(size_t) tipIndex];
        const int in = juce::Random::getSystemRandom().nextInt (8);
        if (chat.isVisible()) chat.say (fr (intros[in]), fr (tips[t]));
        else buddySay (fr (intros[in]), fr (tips[t]));
        ++tipIndex;
    }
}

void VariantEditor::initObliterate()
{
    auto& st = library::settings();
    juce::ignoreUnused (st);
    // OBLITERATE
    obliterateBtn.setTooltip (fr ("OBLITERATE : explose tout et remet le plugin à zéro, comme si tu venais de l'ouvrir. Clique deux fois."));
    obliterateBtn.onArm = [this]
    {
        if (buddyOn) buddySay (fr ("Attends attends…"), fr ("T'es sûr ? Reclique sur la bombe et tout part en fumée. Pas de retour en arrière."));
    };
    obliterateBtn.onFire = [this]
    {
        proc.playExplosion();
        bubble.setVisible (false);
        boom.setBounds (root.getLocalBounds());
        boom.start (obliterateBtn.getBounds().getCentre().toFloat());
    };
    boom.onShake = [this] (float dx, float dy) { boomVibe = juce::jmin (6.0f, (std::abs (dx) + std::abs (dy)) * 0.6f); };   // l'explosion fait trembler les textes
    boom.onPeak = [this]
    {
        proc.obliterate();
        currentTab = 0;
        buildAll();
        root.addChildComponent (boom);
        boom.toFront (false);
    };
    boom.onDone = [this]
    {
        if (buddyOn) { buddySay (fr ("Session pulvérisée."), fr ("Page blanche, on repart de zéro. Ça arrive aux meilleurs.")); muteTicks = 100; }
    };
    root.addAndMakeVisible (obliterateBtn);
    root.addChildComponent (boom);

    buildAll();
    {
        const int mask = st.getIntValue ("detached", 0);
        for (int i = 0; i < numPages; ++i) if (mask & (1 << i)) detachPage (i);
    }
    proc.addChangeListener (this);

    setResizable (true, true);
    {
        // redimensionner à la souris garde les proportions (tout grandit ou rétrécit ensemble)
        // et la fenêtre ne peut pas devenir plus grande que l'écran (avant : l'interface sortait de l'écran)
        const auto ua = screenArea();
        const double ratio = (double) baseW / (double) baseH;
        int maxW = ua.getWidth() - 20, maxH = ua.getHeight() - 60;
        maxW = juce::jmin (maxW, (int) (maxH * ratio));
        maxH = (int) (maxW / ratio);
        setResizeLimits ((int) (baseW * 0.6f), (int) (baseH * 0.6f), juce::jmax ((int) (baseW * 0.6f), maxW), juce::jmax ((int) (baseH * 0.6f), maxH));
        if (auto* c = getConstrainer()) c->setFixedAspectRatio (ratio);
        uiScale = juce::jmin (uiScale, (float) maxW / (float) baseW);
    }
    setSize ((int) (baseW * uiScale), (int) (baseH * uiScale));
    setWantsKeyboardFocus (true);
    showTab (0);
   #if ! VARIANT_FX_ONLY
    // les presets d'arpège sont rangés dans la banque de sons (créés une fois, jamais écrasés)
    juce::Timer::callAfterDelay (2000, [safe = juce::Component::SafePointer<VariantEditor> (this)] { if (safe != nullptr) safe->proc.ensureFactoryPresets (false); });
   #endif
   #if VARIANT_FX_ONLY
    // Variant FX (sur le mixer de FL) : seulement l'onglet Effets
    for (int i = 0; i < tabs.size(); ++i) tabs[i]->setVisible (i == 1);
    showTab (1);
   #endif
    startTimerHz (30);
    if (! library::settings().getBoolValue ("tourDone", false))
        juce::Timer::callAfterDelay (900, [safe = juce::Component::SafePointer<VariantEditor> (this)] { if (safe != nullptr && safe->buddyOn) safe->startTour(); });
}

VariantEditor::~VariantEditor()
{
    stopTimer();
    removeMouseListener (&automationClicks);
    if (wheelHwnd != nullptr) { wheelhook::remove (wheelHwnd); wheelHwnd = nullptr; }   // molette : crochet retiré avant que FL décharge Variant
    proc.synthFocus.store (false);   // fenêtre fermée : les couches rejouent normalement
    proc.onSoundFontDropped = nullptr;
    layermenu::openLibrary() = nullptr;
    root.removeMouseListener (&touch);
    int mask = 0;
    for (int i = 0; i < numPages; ++i)
        if (windows[(size_t) i] != nullptr)
        {
            mask |= 1 << i;
            library::settings().setValue ("win" + juce::String (i), windows[(size_t) i]->getWindowStateAsString());
        }
    library::settings().setValue ("detached", mask);
    library::settings().saveIfNeeded();
    for (auto& w : windows) w.reset();
    for (auto& m : minis) m.reset();
    panels.clear();
    detach::open() = nullptr;
    for (auto& h : proc.hostedSlots) h.closeWindow();   // les fenêtres des plugins hébergés se ferment avec Variant
    proc.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void VariantEditor::removeFocus (juce::Component& c)
{
    // les boutons ne prennent pas le clavier : Espace & co arrivent toujours au plugin
    if (dynamic_cast<juce::TextEditor*> (&c) == nullptr)
    {
        c.setWantsKeyboardFocus (false);
        c.setMouseClickGrabsKeyboardFocus (false);
    }
    for (auto* ch : c.getChildren()) removeFocus (*ch);
}

void VariantEditor::styleKeyboard()
{
    keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, Palette::keyWhite);
    keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, Palette::keyBlack);
    keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, Palette::keyWhite.darker (0.2f));
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, Palette::blue.withAlpha (0.35f));
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, Palette::blue);
    keyboard.setColour (juce::MidiKeyboardComponent::textLabelColourId, Palette::keyWhite.darker (0.5f));
    keyboard.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
    keyboard.repaint();
}

void VariantEditor::buildAll()
{
    // les pages détachées sont recréées puis redétachées (thème / langue)
    int mask = 0;
    for (int i = 0; i < numPages; ++i)
        if (windows[(size_t) i] != nullptr)
        {
            mask |= 1 << i;
            library::settings().setValue ("win" + juce::String (i), windows[(size_t) i]->getWindowStateAsString());
            windows[(size_t) i].reset();
        }

    // onglets
    tabs.clear();
    const char* tabNames[] = { "Son", "Effets", "Accords & MIDI", "Arpège", "Sampler", "Synthé" };
    const juce::Colour tabColours[] = { Palette::blue, Palette::peach, Palette::mint, Palette::lavender, Palette::pink, Palette::mint.interpolatedWith (Palette::blue, 0.5f) };
    for (int i = 0; i < numPages; ++i)
    {
        auto* t = tabs.add (new juce::TextButton (fr (tabNames[i])));
        t->setClickingTogglesState (true);
        t->setRadioGroupId (1001);
        t->setColour (juce::TextButton::buttonOnColourId, tabColours[i]);
        t->setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        t->onClick = [this, i] { showTab (i); };
        static const char* tips[] = { "Ton son : jusqu'à 8 sons + 8 VST + le synthé ensemble, variations, enveloppe et slide",
                                      "Effets : glisse des effets dans la chaîne, presets et hasard",
                                      "Accords & MIDI : génère des progressions, édite-les et glisse le MIDI dans FL",
                                      "Arpège : tiens des notes, il joue en rythme",
                                      "Sampler : tempo, tonalité et slicer pour découper un sample",
                                      "Synthé : sound design façon Serum (tables d'ondes, filtre, enveloppes, LFO, modulations)" };
        t->setTooltip (fr (tips[i]));
        root.addAndMakeVisible (t);
    }

    themeBox.clear (juce::dontSendNotification);
    {
        // thèmes par ordre alphabétique, chacun avec son aperçu (l'identifiant reste le numéro du thème)
        const auto nms = themes::names();
        std::vector<int> order;
        for (int i = 0; i < nms.size(); ++i) order.push_back (i);
        std::sort (order.begin(), order.end(), [&nms] (int a, int b) { return nms[a].compareNatural (nms[b]) < 0; });
        for (int i : order)
        {
            juce::PopupMenu::Item it (nms[i]);
            it.itemID = i + 1;
            auto di = std::make_unique<juce::DrawableImage>(); di->setImage (themePreview (i));
            it.image = std::move (di);
            themeBox.getRootMenu()->addItem (std::move (it));
        }
    }
    themeBox.setSelectedId (proc.themeIndex + 1, juce::dontSendNotification);
    bankButton.setColour (juce::TextButton::buttonColourId, Palette::blue.withAlpha (0.18f));   // couleurs du thème choisi
    styleKeyboard();

    libraryPanel.reset(); soundPage.reset(); fxPage.reset(); chordPage.reset(); arpPage.reset(); toolsPage.reset(); synthPage.reset();
    soundPage = std::make_unique<SoundPage> (proc);
    soundPage->onPluginWindow = [this] { if (proc.kbMode == 0 || proc.kbMode == 3) { proc.kbMode = 1; chordPage->content.repaint(); } };   // clavier d'ordi en mode notes pour essayer le plugin
    soundPage->content.onKeyboardMode = [this] { releaseAllKeys(); grabKeyboardFocus(); if (chordPage != nullptr) chordPage->content.repaint(); };
    fxPage    = std::make_unique<FxPage> (proc);
    chordPage = std::make_unique<ChordMidiPage> (proc);
    arpPage   = std::make_unique<ArpPage> (proc);
    {
        // colonne « Effets rapides » qui défile, comme sur la page Synthé
        auto strip = std::make_unique<synthui::FxStrip> (proc);
        auto* sp = strip.get();
        arpPage->setSideStrip (std::move (strip), [sp] { sp->tick (1.0f / 30.0f); });
    }
    toolsPage = std::make_unique<ToolsPage> (proc);
    synthPage = std::make_unique<synthui::SynthPage> (proc);
    toolsPage->content.slicerCard.onArp = [this] { showTab (3); };
    toolsPage->content.slicerCard.onDetachPads = [this] { openMini (2); };
    chordPage->content.onDetachMini = [this] (int k) { openMini (k); };
    libraryPanel = std::make_unique<LibraryPanel> (proc);
    soundPage->content.onOpenLibrary = [this] { libraryPanel->setBounds (pageArea); libraryPanel->showFor (proc.selectedLayer); };
    layermenu::openLibrary() = [safe = juce::Component::SafePointer<VariantEditor> (this)] (int layer)
    {
        if (safe == nullptr) return;
        safe->showTab (0);
        safe->libraryPanel->setBounds (safe->pageArea); safe->libraryPanel->showFor (layer);
    };
    libraryPanel->onLoaded = [this] { soundPage->soundChanged(); };
    for (juce::Component* c : { (juce::Component*) soundPage.get(), (juce::Component*) fxPage.get(), (juce::Component*) chordPage.get(),
                                (juce::Component*) arpPage.get(), (juce::Component*) toolsPage.get(), (juce::Component*) synthPage.get() })
    {
        root.addChildComponent (c);
        c->setBounds (pageArea);
    }
    root.addChildComponent (*libraryPanel);
    sparkles = std::make_unique<SparkleOverlay> (proc);
    root.addAndMakeVisible (*sparkles);
    fxPage->onEffectAdded = [this] (int ty) { sparkles->burst (ty, 45); };
    root.addChildComponent (placeholder);
    root.addChildComponent (bubble);
    root.addChildComponent (chat);
    root.addChildComponent (tour);
    root.addChildComponent (buddy);
    buddy.setVisible (buddyOn);
    root.addChildComponent (help);
    root.addChildComponent (statsPanel);   // Réglages > Statistiques de Variant
    root.addChildComponent (reportPanel);
    reportPanel.hostName = juce::PluginHostType().getHostDescription() + juce::String (" (") + juce::AudioProcessor::getWrapperTypeDescription (processor.wrapperType) + ")";
    root.addChildComponent (handleGate);   // 1er lancement : @ Instagram (toujours au-dessus)
    root.addChildComponent (blockedScreen);   // Variant désactivé pour ce @
    soundPage->soundChanged();

    if (buddyOn) tooltips.reset();
    else tooltips = std::make_unique<juce::TooltipWindow> (this, 500);
    removeFocus (root);
    layoutRoot();
    for (int i = 0; i < numPages; ++i) if (mask & (1 << i)) detachPage (i);
    showTab (currentTab);
}

juce::Component* VariantEditor::pageAt (int i) const
{
    switch (i)
    {
        case 0: return soundPage.get();
        case 1: return fxPage.get();
        case 2: return chordPage.get();
        case 3: return arpPage.get();
        case 4: return toolsPage.get();
        default: return synthPage.get();
    }
}

void VariantEditor::detachPage (int i, bool restorePosition)
{
    i = juce::jlimit (0, numPages - 1, i);
    if (windows[(size_t) i] != nullptr) { windows[(size_t) i]->toFront (true); return; }
    auto* page = pageAt (i);
    if (page == nullptr) return;
    if (i == 0) libraryPanel->setVisible (false);
    root.removeChildComponent (page);
    const char* names[] = { "Son", "Effets", "Accords & MIDI", "Arpège", "Sampler", "Synthé" };
    auto holder = std::make_unique<PageHolder> (*page, pageArea.getWidth(),
                                                [this] (const juce::KeyPress& k) { return keyPressed (k); },
                                                [this] { keyStateChanged (false); });
    windows[(size_t) i] = std::make_unique<PageWindow> (fr (names[i]), std::move (holder), lnf,
        [safe = juce::Component::SafePointer<VariantEditor> (this), i]
        { juce::MessageManager::callAsync ([safe, i] { if (safe != nullptr) safe->reattachPage (i); }); });
    auto& w = *windows[(size_t) i];
    const int ww = (int) ((float) pageArea.getWidth() * uiScale), wh = (int) ((float) pageArea.getHeight() * uiScale);
    w.setResizeLimits (300, 200, 4000, 3000);
    w.centreWithSize (ww, wh);
    const auto saved = library::settings().getValue ("win" + juce::String (i));
    if (restorePosition && saved.isNotEmpty()) w.restoreWindowStateFromString (saved);
    w.setVisible (true);
    showTab (currentTab);
}

void VariantEditor::reattachPage (int i)
{
    i = juce::jlimit (0, numPages - 1, i);
    if (windows[(size_t) i] == nullptr) return;
    library::settings().setValue ("win" + juce::String (i), windows[(size_t) i]->getWindowStateAsString());
    windows[(size_t) i].reset();
    if (auto* page = pageAt (i))
    {
        page->setTransform ({});
        root.addChildComponent (page);
        page->setBounds (pageArea);
        sparkles->toFront (false);
        bubble.toFront (false);
        buddy.toFront (false);
    }
    showTab (i);
}

// aperçu d'un thème pour la liste : ses couleurs + un petit motif (feuilles pour la jungle, soleil pour le désert, drapeaux…)
static juce::Image themePreview (int index)
{
    static std::map<int, juce::Image> cache;
    if (auto it = cache.find (index); it != cache.end()) return it->second;
    const auto& th = themes::list()[(size_t) index];
    auto C = [&th] (int k) { return juce::Colour (th.c[k]); };
    // image « logicielle » (en mémoire) : une image gardée en cache statique ne doit pas tenir de ressource Direct2D / carte graphique,
    // sinon FL se fige quand il décharge Variant (le pilote graphique attend ses threads pendant le déchargement)
    juce::Image img (juce::Image::ARGB, 44, 26, true, juce::SoftwareImageType());
    juce::Graphics g (img);
    const juce::Rectangle<float> r (1.0f, 1.0f, 42.0f, 24.0f);
    juce::Path clip; clip.addRoundedRectangle (r, 5.0f);
    g.reduceClipRegion (clip);
    g.setGradientFill (juce::ColourGradient (C (1), 0.0f, 0.0f, C (0), 0.0f, 26.0f, false));
    g.fillRect (r);
    const auto nm = juce::String::fromUTF8 (th.name);
    if (nm == "Jungle")
    {
        for (int k = 0; k < 4; ++k)
        {
            juce::Path leaf; leaf.addEllipse (0.0f, -2.5f, 13.0f, 5.0f);
            g.setColour ((k % 2 ? juce::Colour (0xff4fb35a) : juce::Colour (0xff2f8a43)));
            g.fillPath (leaf, juce::AffineTransform::rotation (-0.9f + k * 0.5f).translated (4.0f + k * 9.0f, 22.0f - (k % 2) * 6.0f));
        }
        g.setColour (juce::Colour (0xffe8ff7a)); g.fillEllipse (33.0f, 5.0f, 3.0f, 3.0f);
    }
    else if (nm.endsWith ("sert"))
    {
        g.setColour (juce::Colour (0xffffc04a)); g.fillEllipse (26.0f, 3.0f, 12.0f, 12.0f);
        juce::Path dune; dune.startNewSubPath (0.0f, 26.0f); dune.quadraticTo (14.0f, 12.0f, 28.0f, 20.0f); dune.quadraticTo (36.0f, 16.0f, 44.0f, 19.0f); dune.lineTo (44.0f, 26.0f); dune.closeSubPath();
        g.setColour (juce::Colour (0xffd9a05a)); g.fillPath (dune);
        g.setColour (juce::Colour (0xff4f7a3a)); g.fillRoundedRectangle (9.0f, 8.0f, 3.0f, 12.0f, 1.5f); g.fillRoundedRectangle (5.5f, 11.0f, 3.0f, 5.0f, 1.5f);
    }
    else if (nm == "Palestine")
    {
        g.setColour (juce::Colour (0xff111111)); g.fillRect (0.0f, 0.0f, 44.0f, 9.0f);
        g.setColour (juce::Colours::white); g.fillRect (0.0f, 9.0f, 44.0f, 8.0f);
        g.setColour (juce::Colour (0xff149954)); g.fillRect (0.0f, 17.0f, 44.0f, 9.0f);
        juce::Path t; t.addTriangle (0.0f, 0.0f, 0.0f, 26.0f, 17.0f, 13.0f); g.setColour (juce::Colour (0xffe4312b)); g.fillPath (t);
    }
    else if (nm == juce::String::fromUTF8 ("Brésil"))
    {
        g.setColour (juce::Colour (0xff009c3b)); g.fillRect (r);
        juce::Path d; d.addQuadrilateral (22.0f, 3.0f, 40.0f, 13.0f, 22.0f, 23.0f, 4.0f, 13.0f); g.setColour (juce::Colour (0xffffdf00)); g.fillPath (d);
        g.setColour (juce::Colour (0xff002776)); g.fillEllipse (16.5f, 7.5f, 11.0f, 11.0f);
    }
    else if (nm.startsWith ("La France"))
    {
        g.setColour (juce::Colours::white.withAlpha (0.9f)); g.drawEllipse (16.0f, 7.0f, 12.0f, 12.0f, 2.0f); g.fillRect (21.0f, 3.0f, 2.0f, 20.0f);
    }
    else if (nm.startsWith ("Y2K"))
    {
        for (auto s : { juce::Point<float> (10.0f, 8.0f), juce::Point<float> (32.0f, 16.0f) })
        { juce::Path st; st.addStar (s, 4, 1.2f, 5.0f, 0.0f); g.setColour (C (8)); g.fillPath (st); }
    }
    else
    {
        // motif propre à chaque thème (rappel de son nom)
        auto acc = [&] (int k) { return C (6 + k); };
        if (nm == "FL Studio" || nm == "Ableton Live" || nm == "Logic Pro" || nm == "Bitwig" || nm == "Pro Tools" || nm == "Cubase")
        {
            for (int k = 0; k < 6; ++k)   // petite table de mixage
            {
                const float hgt = 5.0f + (float) ((k * 7 + (int) nm.length()) % 9);
                g.setColour (acc (k % 5)); g.fillRoundedRectangle (16.0f + k * 4.5f, 23.0f - hgt, 3.0f, hgt, 1.0f);
            }
        }
        else if (nm == "Nuit bleue" || nm == "Pastel nuit")
        {
            g.setColour (juce::Colour (0xfff6e7a8)); g.fillEllipse (28.0f, 9.0f, 10.0f, 10.0f);
            g.setColour (C (0)); g.fillEllipse (31.0f, 7.0f, 10.0f, 10.0f);   // croissant de lune
            g.setColour (juce::Colours::white.withAlpha (0.8f)); for (auto s : { juce::Point<float> (10.0f, 17.0f), juce::Point<float> (19.0f, 13.0f), juce::Point<float> (14.0f, 21.0f) }) g.fillEllipse (s.x, s.y, 1.6f, 1.6f);
        }
        else if (nm == "Noir & blanc" || nm == "Blanc pur")
        {
            for (int k = 0; k < 6; ++k) { g.setColour (juce::Colour (0xfff2f2f2)); g.fillRect (8.0f + k * 5.0f, 12.0f, 4.5f, 12.0f); }
            for (int k : { 0, 1, 3, 4 }) { g.setColour (juce::Colour (0xff111111)); g.fillRect (11.0f + k * 5.0f, 12.0f, 3.0f, 7.0f); }   // touches de piano
        }
        else if (nm == "Encre")
        {
            juce::Path drop; drop.startNewSubPath (22.0f, 8.0f); drop.quadraticTo (30.0f, 18.0f, 22.0f, 22.0f); drop.quadraticTo (14.0f, 18.0f, 22.0f, 8.0f);
            g.setColour (acc (0)); g.fillPath (drop);
        }
        else if (nm == "Papier")
        {
            g.setColour (C (3)); for (int k = 0; k < 3; ++k) g.drawHorizontalLine (13 + k * 4, 8.0f, 36.0f);
            g.setColour (acc (2)); g.drawVerticalLine (12, 10.0f, 25.0f);
        }
        else if (nm == "Lavande" || nm == "Menthe")
        {
            g.setColour (juce::Colour (0xff6fa46a)); g.drawLine (22.0f, 24.0f, 22.0f, 9.0f, 1.4f);
            const auto fl = nm == "Lavande" ? juce::Colour (0xff9a7fdc) : juce::Colour (0xff4fae84);
            for (int k = 0; k < 4; ++k) { g.setColour (fl); g.fillEllipse (19.0f + (k % 2) * 3.0f, 9.0f + k * 3.0f, 4.0f, 4.0f); }
        }
        else if (nm == "Rosé" || nm == juce::String::fromUTF8 ("Rosé") || nm.startsWith ("P\xc3\xaa") || nm.startsWith (juce::String::fromUTF8 ("Pêche")))
        {
            g.setColour (acc (0)); g.fillEllipse (17.0f, 10.0f, 12.0f, 12.0f);
            g.setColour (juce::Colour (0xff6fa46a)); g.fillEllipse (26.0f, 9.0f, 6.0f, 3.5f);   // un fruit / une fleur
        }
        else if (nm == "Graphite")
        {
            g.setColour (C (5).withAlpha (0.6f)); for (int k = -2; k < 8; ++k) g.drawLine (k * 6.0f, 26.0f, k * 6.0f + 14.0f, 6.0f, 1.0f);
        }
        else
        {
            juce::Path w; for (int i = 0; i <= 20; ++i) { const float x = 6.0f + i * 1.6f, y = 18.0f + 4.0f * std::sin (i * 0.6f); if (i == 0) w.startNewSubPath (x, y); else w.lineTo (x, y); }
            g.setColour (acc (1)); g.strokePath (w, juce::PathStrokeType (1.6f));   // une onde
        }
    }
    // pastilles des couleurs du thème
    for (int k = 0; k < 4; ++k)
    {
        g.setColour (C (6 + k)); g.fillEllipse (4.0f + k * 7.0f, 4.0f, 6.0f, 6.0f);
    }
    g.setColour (C (3)); g.drawRoundedRectangle (r, 5.0f, 1.0f);
    cache[index] = img;
    return img;
}

void VariantEditor::applyTheme (int index)
{
    proc.themeIndex = index;
    library::settings().setValue ("theme", index);
    library::settings().saveIfNeeded();
    themes::apply (index);
    lnf.refreshColours();
    globalLnf->lnf.refreshColours();
    buildAll();
    sendLookAndFeelChange();
    repaint();
}

void VariantEditor::setLanguage (int lang)
{
    i18n::current().store (lang);
    library::settings().setValue ("language", lang);
    library::settings().saveIfNeeded();
    buildAll();
    repaint();
}

void VariantEditor::setScale (float s)
{
    uiScale = juce::jlimit (0.8f, 2.0f, s);
    {
        const auto ua = screenArea();   // jamais plus grand que l'écran
        uiScale = juce::jmin (uiScale, (float) (ua.getHeight() - 60) / (float) baseH, (float) (ua.getWidth() - 20) / (float) baseW);
    }
    library::settings().setValue ("uiScale", (double) uiScale);
    library::settings().saveIfNeeded();
    setSize ((int) (baseW * uiScale), (int) (baseH * uiScale));
}

void VariantEditor::showSettingsMenu()
{
    juce::PopupMenu m, lang, size, themeM;
    const auto names = i18n::languageNames();
    for (int i = 0; i < names.size(); ++i) lang.addItem (100 + i, names[i], true, i18n::current().load() == i);
    for (int pct : { 90, 100, 115, 125, 140, 160, 180 }) size.addItem (200 + pct, juce::String (pct) + " %", true, std::abs (uiScale * 100 - pct) < 3);
    {
        // thèmes (avant : la liste en haut de la fenêtre) par ordre alphabétique, chacun avec son aperçu
        const auto nms = themes::names();
        std::vector<int> order;
        for (int i = 0; i < nms.size(); ++i) order.push_back (i);
        std::sort (order.begin(), order.end(), [&nms] (int a, int b) { return nms[a].compareNatural (nms[b]) < 0; });
        for (int i : order)
        {
            juce::PopupMenu::Item it (nms[i]);
            it.itemID = 1000 + i;
            it.isTicked = i == proc.themeIndex;
            auto di = std::make_unique<juce::DrawableImage>(); di->setImage (themePreview (i));
            it.image = std::move (di);
            themeM.addItem (std::move (it));
        }
    }
    m.addSubMenu (fr ("Thème"), themeM);
    m.addSubMenu (fr ("Langue"), lang);
    m.addSubMenu (fr ("Taille de l'interface"), size);
    m.addSeparator();
    juce::PopupMenu vis;
    const int vi = SparkleOverlay::intensity();
    vis.addItem (300, fr ("Désactivés"), true, vi == 0);
    vis.addItem (301, fr ("Discrets"), true, vi == 1);
    vis.addItem (302, fr ("Normaux"), true, vi == 2);
    m.addSubMenu (fr ("Effets visuels"), vis);
    m.addItem (4, fr ("Niveau auto : baisse les sons trop forts au chargement"), true, library::settings().getBoolValue ("autoLevel", true));
    m.addItem (5, fr ("Un son chargé au lancement (clic droit sur un son de la banque pour le choisir)"), true, library::settings().getBoolValue ("defaultSoundOn", true));
    m.addItem (6, fr ("Qualité+ : one-shots polis (graves propres, air, dynamique)"), true, library::settings().getBoolValue ("polish", true));
    m.addItem (7, fr ("Effets auto : chaque one-shot arrive avec ses effets (reverb, delay…) s'il en a besoin"), true, library::settings().getBoolValue ("autoFx", true));
    m.addItem (2, fr ("Guide Kick (petit skateur)"), true, buddyOn);
    juce::PopupMenu skins;
    for (int i : sortedSkins()) skins.addItem (400 + i, fr (buddySkins()[(size_t) i].name), true, buddyOn && buddy.getSkin() == i, skinIcon (i));   // choisir un skin rallume Kick s'il était désactivé
    m.addSubMenu (fr ("Skin de Kick"), skins);
    m.addItem (3, fr ("Visite guidée"));
    m.addItem (1, fr ("Raccourcis clavier") + "  (F1)");
    m.addItem (9, fr ("Signaler un bug") + juce::String::fromUTF8 ("…"));
    m.addSeparator();
   #if ! VARIANT_PUBLIC
    m.addItem (8, juce::String::fromUTF8 ("Statistiques de Variant (créateur)…"));
   #endif
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&settingsButton),
        [safe = juce::Component::SafePointer<VariantEditor> (this)] (int r)
        {
            if (safe == nullptr || r == 0) return;
            if (r == 9) { safe->reportPanel.setBounds (safe->root.getLocalBounds()); safe->reportPanel.open(); return; }
            if (r == 8) { safe->statsPanel.setBounds (safe->root.getLocalBounds()); safe->statsPanel.open(); return; }
            if (r >= 1000) { if (r - 1000 != safe->proc.themeIndex) safe->applyTheme (r - 1000); return; }
            if (r == 5) { library::settings().setValue ("defaultSoundOn", ! library::settings().getBoolValue ("defaultSoundOn", true)); library::settings().saveIfNeeded(); return; }
            if (r == 6) { library::settings().setValue ("polish", ! library::settings().getBoolValue ("polish", true)); library::settings().saveIfNeeded(); safe->proc.regenerateVariations (false); return; }
            if (r == 7) { library::settings().setValue ("autoFx", ! library::settings().getBoolValue ("autoFx", true)); library::settings().saveIfNeeded(); return; }
            if (r == 1) { safe->help.setBounds (safe->root.getLocalBounds()); safe->help.setVisible (true); safe->help.toFront (false); }
            else if (r == 2) safe->setBuddyOn (! safe->buddyOn);
            else if (r == 3) safe->startTour();
            else if (r == 4) { library::settings().setValue ("autoLevel", ! library::settings().getBoolValue ("autoLevel", true)); library::settings().saveIfNeeded(); }
            else if (r >= 400 && r < 500) { safe->buddy.setSkin (r - 400); library::settings().setValue ("buddySkin", r - 400); library::settings().saveIfNeeded(); if (! safe->buddyOn) safe->setBuddyOn (true); safe->sayQuote(); safe->muteTicks = 60; }
            else if (r >= 300 && r < 310)
            {
                SparkleOverlay::intensity() = r - 300;
                library::settings().setValue ("fxVisuals2", r - 300);
                library::settings().saveIfNeeded();
            }
            else if (r >= 100 && r < 200) safe->setLanguage (r - 100);
            else if (r >= 200) safe->setScale ((float) (r - 200) / 100.0f);
        });
}

void VariantEditor::showTab (int index)
{
    const int previous = currentTab;
    currentTab = juce::jlimit (0, numPages - 1, index);
    if (previous == 2 && currentTab != 2 && currentTab != 1 && windows[2] == nullptr && proc.isPreviewing()) proc.previewGuide (false);   // on quitte les accords : ils s'arrêtent
    for (int i = 0; i < tabs.size(); ++i)
        tabs[i]->setToggleState (i == currentTab, juce::dontSendNotification);
    if (soundPage == nullptr) return;
    for (int i = 0; i < numPages; ++i)
        if (windows[(size_t) i] == nullptr) pageAt (i)->setVisible (currentTab == i);
    placeholder.setVisible (windows[(size_t) currentTab] != nullptr);
    if (placeholder.isVisible()) placeholder.toFront (false);
    if (currentTab != 0 || windows[0] != nullptr) libraryPanel->setVisible (false);
    updateSynthFocus (previous);
    grabKeyboardFocus();
}

// Page Sound design : en y allant, le synthé s'allume et le clavier le joue (seul, sauf si « Seul » est éteint).
// En repartant, s'il avait été allumé tout seul et que tu n'as rien changé au son, il se rééteint.
static juce::int64 synthPatchSignature (const wt::Engine& e)
{
    juce::int64 h = 1469598103;
    for (int i = 0; i < wt::NumParams; ++i) h = h * 1099511 + (juce::int64) juce::roundToInt (e.patch.get (i) * 10000.0f);
    for (int o = 0; o < 2; ++o) if (auto t = e.getTable (o)) h = h * 31 + t->path.hashCode64();
    return h;
}

void VariantEditor::updateSynthFocus (int previous)
{
    const bool onSynth = currentTab == 5 && windows[5] == nullptr;
    if (onSynth && ! synthPageActive)
    {
        synthPageActive = true;
        if (! proc.wtSynth.enabled.load()) { proc.wtSynth.enabled.store (true); synthAutoOn = true; synthSig = synthPatchSignature (proc.wtSynth); }
        proc.synthFocus.store (library::settings().getBoolValue ("synthSolo", true));
        kbBeforeSynth = proc.kbMode;
        if (proc.kbMode == 0 || proc.kbMode == 3) proc.kbMode = 1;   // le clavier d'ordi joue des notes (pas les chops du Sampler)
    }
    else if (! onSynth && synthPageActive)
    {
        synthPageActive = false;
        proc.synthFocus.store (false);
        if (synthAutoOn && proc.wtSynth.enabled.load() && synthPatchSignature (proc.wtSynth) == synthSig) proc.wtSynth.enabled.store (false);
        synthAutoOn = false;
        if (kbBeforeSynth == 3 && proc.kbMode == 1) proc.kbMode = 3;
    }

    // page Sampler : le clavier d'ordi joue directement les chops (sans cliquer « Jouer au clavier »),
    // et passe avant l'arpège. Le bouton reste là pour le couper (choix gardé).
    const bool onSampler = currentTab == 4 && windows[4] == nullptr;
    if (onSampler && ! samplerPageActive)
    {
        samplerPageActive = true;
        kbBeforeSampler = proc.kbMode;
        if (! library::settings().getBoolValue ("samplerKbOff", false)) { proc.kbMode = 3; grabKeyboardFocus(); }
    }
    else if (! onSampler && samplerPageActive)
    {
        samplerPageActive = false;
        if (proc.kbMode == 3 && kbBeforeSampler != 3) { releaseAllKeys(); proc.kbMode = kbBeforeSampler; }
    }
    juce::ignoreUnused (previous);
}

bool VariantEditor::keyPressed (const juce::KeyPress& k)
{
    const int code = k.getKeyCode();
    const bool cmd = k.getModifiers().isCommandDown();

    if (help.isVisible() && code == juce::KeyPress::escapeKey) { help.setVisible (false); return true; }
    if (code == juce::KeyPress::F1Key) { help.setBounds (root.getLocalBounds()); help.setVisible (! help.isVisible()); help.toFront (false); return true; }

    // banque de sons ouverte : flèches haut / bas = son suivant / précédent (lues par la banque elle-même), pas l'octave
    if (libraryPanel != nullptr && libraryPanel->isVisible() && ! cmd
        && (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey))
        return true;

    // jouer avec le clavier d'ordinateur
    // clavier d'ordinateur -> tranches du slicer (rangée du milieu 1-8, rangée du haut 9-16)
    if (proc.kbMode == 3 && ! cmd)
    {
        const juce::juce_wchar ch = juce::CharacterFunctions::toUpperCase (k.getTextCharacter());
        const bool az = proc.kbLayout == 0;
        const char* top = az ? "AZERTYUIOP" : "QWERTYUIOP";   // tranches 1 à 10
        const char* bot = az ? "WXCVBN" : "ZXCVBN";           // tranches 11 à 16
        const char* mid = az ? "QSDFGHJKLM" : "ASDFGHJKL;";   // tranches 17 à 26
        int s = -1;
        for (int i = 0; i < 10 && s < 0; ++i)
        {
            if (code == (int) top[i] || ch == (juce::juce_wchar) top[i]) s = i;
            else if (i < 6 && (code == (int) bot[i] || ch == (juce::juce_wchar) bot[i])) s = 10 + i;
            else if (code == (int) mid[i] || ch == (juce::juce_wchar) mid[i]) s = 16 + i;
        }
        if (s >= 0) s += proc.sliceBank * slicer::padsPerBank;   // 2e page de pads : le clavier joue la page affichée, dans l'ordre
        if (s >= proc.getSliceCount()) return true;   // touche sans tranche : on l'ignore sans la passer à FL
        if (s >= 0)
        {
            if (kbHeld.count (code) == 0) { proc.playSlice (s, true); kbHeld[code] = { 1000 + s }; kbVk[code] = keyVk (k); }
            return true;
        }
    }

    if (proc.kbMode != 0 && proc.kbMode != 3 && ! cmd)
    {
        // octave : flèches haut / bas
        if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
        {
            proc.kbOctave = juce::jlimit (2, 8, proc.kbOctave + (code == juce::KeyPress::upKey ? 1 : -1));
            chordPage->content.repaint();
            return true;
        }
        if (proc.kbMode == 4)
        {
            // clavier en gamme : chaque touche = la note suivante de la gamme choisie (tonalité + gamme du plugin).
            // Rangées de bas en haut : 2 octaves en dessous, 1 octave en dessous, octave de base (A Z E R… = C5 en C), chiffres = 1 octave au-dessus.
            static const wchar_t* rowsAz[4] = { L"<WXCVBN,;:!", L"QSDFGHJKLMÙ*", L"AZERTYUIOP$", L"&É\"'(-È_ÇÀ)=" };
            static const wchar_t* rowsQw[4] = { L"ZXCVBNM,./", L"ASDFGHJKL;'", L"QWERTYUIOP[]", L"1234567890-=" };
            const auto** rows = proc.kbLayout == 0 ? rowsAz : rowsQw;
            const juce::juce_wchar ch = juce::CharacterFunctions::toUpperCase (k.getTextCharacter());
            const juce::juce_wchar cc = (code > 0 && code < 0x10000) ? juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) code) : 0;
            int row = -1, pos = -1;
            for (int r = 0; r < 4 && row < 0; ++r)
                for (int i = 0; rows[r][i] != 0; ++i)
                    if ((ch != 0 && ch == (juce::juce_wchar) rows[r][i]) || (cc != 0 && cc == (juce::juce_wchar) rows[r][i])) { row = r; pos = i; break; }
            if (row >= 0)
            {
                if (kbHeld.count (code) > 0) return true;   // répétition automatique de la touche
                const auto& steps = theory::scaleAt (proc.getScale()).steps;
                const int n = juce::jmax (1, (int) steps.size());
                const int note = 12 * (proc.kbOctave + row - 2) + proc.getKey() + 12 * (pos / n) + steps[(size_t) (pos % n)];
                std::vector<int> notes;
                if (note >= 0 && note < 128) { notes.push_back (note); proc.keyboardState.noteOn (1, note, 0.8f); }
                kbHeld[code] = notes;
                kbVk[code] = keyVk (k);
                return true;
            }
        }
        // notes comme un piano (façon FL) : rangée du haut = touches blanches (A = C5, Z = D5, E = E5…),
        // rangée du milieu = touches noires entre elles (Q = C#, S = D#, F = F#, G = G#, H = A#… D, J et M n'ont pas de noire),
        // rangée du bas = touches blanches une octave plus bas
        const juce::juce_wchar ch = juce::CharacterFunctions::toUpperCase (k.getTextCharacter());
        const bool az = proc.kbLayout == 0;
        const char* rows[3] = { az ? "AZERTYUIOP" : "QWERTYUIOP", az ? "QSDFGHJKLM" : "ASDFGHJKL;", az ? "WXCVBN,;:!" : "ZXCVBNM,./" };
        int idx = -1, row = -1, pos = -1;
        for (int r = 0, base = 0; r < 3 && idx < 0; base += (int) std::strlen (rows[r]), ++r)
            for (int i = 0; rows[r][i] != 0; ++i)
                if (code == (int) rows[r][i] || ch == (juce::juce_wchar) rows[r][i]) { idx = base + i; row = r; pos = i; break; }
        if (idx >= 0)
        {
            if (kbHeld.count (code) > 0) return true;   // répétition automatique de la touche
            std::vector<int> notes;
            if (proc.kbMode == 1)
            {
                static const int whiteSemi[10] = { 0, 2, 4, 5, 7, 9, 11, 12, 14, 16 };
                static const int blackSemi[10] = { 1, 3, -1, 6, 8, 10, -1, 13, 15, -1 };   // la noire à droite de chaque blanche
                int semi = -100;
                if (pos >= 0 && pos < 10)
                {
                    if (row == 0) semi = whiteSemi[pos];
                    else if (row == 1 && blackSemi[pos] >= 0) semi = blackSemi[pos];
                    else if (row == 2) semi = whiteSemi[pos] - 12;
                }
                if (semi > -100) notes.push_back (proc.kbOctave * 12 + semi);
            }
            else if (row == 0) notes = proc.keyboardChord (pos);                  // accords de la gamme
            else if (row == 1 && pos < 7) notes = proc.keyboardChord (10 + pos); // couleurs hors gamme
            for (int n : notes) if (n >= 0 && n < 128) proc.keyboardState.noteOn (proc.kbMode == 2 ? 2 : 1, n, 0.8f);
            kbHeld[code] = notes;
            kbVk[code] = keyVk (k);
            return true;
        }
    }

    if (proc.isTapChopping() && (code == juce::KeyPress::spaceKey || code == juce::KeyPress::escapeKey))   // « Taper mes chops » : Espace = coupe ici, Échap = arrêter
    {
        if (code == juce::KeyPress::spaceKey) proc.tapChop(); else proc.stopTapChop();
        return true;
    }
    if (code == juce::KeyPress::spaceKey)
    {
        // la progression du plugin ne se joue que dans l'onglet Accords & MIDI ; ailleurs Espace revient à FL Studio
        const bool midiFocus = currentTab == 2 || (windows[2] != nullptr && windows[2]->isActiveWindow()) || (minis[0] != nullptr && minis[0]->isActiveWindow());
        if (midiFocus) { proc.previewGuide (! proc.isPreviewing()); return true; }
        if (currentTab == 4 && ! proc.getSlicePattern().empty()) { proc.previewSlicePattern (! proc.isPreviewing()); return true; }
        if (proc.isPreviewing()) proc.previewGuide (false);
        return false;
    }
    if (cmd && (code == 'Z' || code == 'z') && ! k.getModifiers().isShiftDown()) { if (proc.undoAny()) { repaint(); chordPage->content.selectionChanged(); } return true; }
    if (cmd && ((code == 'Y' || code == 'y') || ((code == 'Z' || code == 'z') && k.getModifiers().isShiftDown()))) { if (proc.redoAny()) { repaint(); chordPage->content.selectionChanged(); } return true; }
    if (code == juce::KeyPress::escapeKey && libraryPanel->isVisible()) { libraryPanel->setVisible (false); return true; }

    if (currentTab == 2 && chordPage->content.handleKey (k)) return true;
    return false;
}

void VariantEditor::trackParamHistory()
{
    const auto& ps = proc.getParameters();
    std::vector<float> now ((size_t) ps.size());
    for (int i = 0; i < ps.size(); ++i) now[(size_t) i] = ps[i]->getValue();
    if (paramSnap.size() != now.size()) { paramSnap = now; return; }
    const double t = juce::Time::getMillisecondCounterHiRes();
    if (now != paramSnap)
    {
        // changement fait par l'utilisateur (souris récente) : pas l'automation de FL
        if (! paramPending && t - touch.last < 1500.0) { paramPending = true; paramBefore = paramSnap; }
        paramChangedAt = t;
        paramSnap = now;
    }
    else if (paramPending && t - paramChangedAt > 450.0 && ! juce::Desktop::getInstance().getMainMouseSource().isDragging())
    {
        paramPending = false;
        auto before = paramBefore, after = now;
        auto apply = [this] (const std::vector<float>& v)
        {
            const auto& pp = proc.getParameters();
            for (int i = 0; i < pp.size() && i < (int) v.size(); ++i)
                if (pp[i]->getValue() != v[(size_t) i]) pp[i]->setValueNotifyingHost (v[(size_t) i]);
            paramSnap.clear();   // ne pas recompter ce changement
        };
        proc.pushHistory ([apply, before] { apply (before); }, [apply, after] { apply (after); });
    }
}

void VariantEditor::timerCallback()
{
    trackParamHistory();
    bgTime += 1.0f / 30.0f;
    if (kickOut != nullptr)
    {
        const auto& h = fxs::host();
        kickOut->buddy.setBeat (h.ppq, h.playing, juce::jmin (1.0f, proc.bassLevel.load() * 3.0f + proc.outLevel.load() * 2.0f));
        kickOut->buddy.tick (1.0f / 30.0f);
    }
    if (SparkleOverlay::intensity() > 0 && (++bgTick % 2) == 0 && isShowing()) root.repaint();   // fond animé (15 images/s)
    {
        // les basses font vibrer légèrement les textes (plus la fenêtre entière : c'était pénible pour cliquer)
        const float b = proc.bassLevel.load();
        proc.bassLevel.store (b * 0.8f);   // retombe même quand le son s'arrête
        const float amp = SparkleOverlay::intensity() == 0 ? 0.0f : juce::jlimit (0.0f, 1.6f * SparkleOverlay::power(), (b - 0.06f) * 4.0f * SparkleOverlay::power());
        const float target = juce::jmax (amp, boomVibe);
        boomVibe *= 0.85f;
        Vibe::phase += 1.7f;
        if (target > 0.05f || bassShaking)
        {
            bassShaking = target > 0.05f;
            Vibe::amount = bassShaking ? target : 0.0f;
            if (isShowing()) root.repaint();
        }
    }
    const int root2 = (int) proc.apvts.getRawParameterValue ("root")->load();
    if (root2 != lastRoot)
    {
        lastRoot = root2;
        keyboard.repaint();
    }

    auto shown = [this] (int i) { return i == currentTab || windows[(size_t) i] != nullptr; };
    if (shown (0)) soundPage->tick();
    if (shown (1)) fxPage->tick();
    if (shown (2)) chordPage->tick();
    if (shown (3)) arpPage->tick();
    if (shown (4)) toolsPage->tick();
    if (shown (5)) synthPage->tick();
    proc.syncHostGuide();   // la progression jouée avec le Play de FL suit les modifications
    updateHint();
    updateBuddy();
    if (libraryPanel->isVisible()) libraryPanel->tick();
    {
        // molette dans FL : on s'inscrit sur la fenêtre de Variant (voir WheelHook.cpp) — la page défile partout, sauf sur les potards
        void* h = getPeer() != nullptr ? getPeer()->getNativeHandle() : nullptr;
        if (h != wheelHwnd)
        {
            if (wheelHwnd != nullptr) wheelhook::remove (wheelHwnd);
            wheelHwnd = h;
            juce::Component::SafePointer<VariantEditor> self (this);
            if (h != nullptr)
                wheelhook::add (h, [self] (juce::Point<int> phys, float dy)
                {
                    if (self == nullptr) return;
                    auto* peer = self->getPeer();
                    if (peer == nullptr) return;
                    const auto logical = juce::Desktop::getInstance().getDisplays().physicalToLogical (phys);
                    juce::MouseWheelDetails w; w.deltaX = 0.0f; w.deltaY = dy; w.isReversed = false; w.isSmooth = false; w.isInertial = false;
                    peer->handleMouseWheel (juce::MouseInputSource::InputSourceType::mouse, peer->globalToLocal (logical.toFloat()), juce::Time::currentTimeMillis(), w);
                });
        }
    }
    {
        // bouton de la banque : nom du preset chargé, sinon du son de la couche choisie (avant : toujours « Banque de sons »)
        juce::String label = proc.presetName;
        if (label.isEmpty()) label = proc.getLayerFile (proc.selectedLayer).getFileNameWithoutExtension();
        if (label.isEmpty()) label = fr ("Banque de sons");
        if (bankButton.getButtonText() != label) { bankButton.setButtonText (label); bankButton.setTooltip (fr ("Banque de sons  ·  ") + label); }
    }
    {
        // message du créateur (envoyé depuis l'écran Statistiques) : Kick l'affiche dans sa fenêtre
        const auto msg = telemetry::popMessage();
        if (msg.isNotEmpty())
        {
            bubble.setVisible (false);
            chat.sayExact (juce::String::fromUTF8 ("Message de @prodbysotam"), msg);
            chat.setVisible (true);
            chat.toFront (false);
            buddy.toFront (false);
        }
    }
    if (! telemetry::hasHandleFlag().load() && ! handleGate.isVisible() && telemetry::takenPending())   // @ déjà pris par un autre PC : on redemande
    {
        handleGate.setBounds (root.getLocalBounds());
        handleGate.setVisible (true);
        handleGate.toFront (true);
    }
    if (telemetry::isBlocked() != blockedScreen.isVisible())   // désactivé / réactivé par le créateur
    {
        blockedScreen.setBounds (root.getLocalBounds());
        blockedScreen.setVisible (telemetry::isBlocked());
        if (blockedScreen.isVisible()) blockedScreen.toFront (false);
    }
    // fenêtre d'un plugin ouvert dans Variant devant : le clavier d'ordinateur joue quand même des notes
    bool hostedFront = false;
    for (auto& h : proc.hostedSlots) if (h.windowActive()) hostedFront = true;
    if (hostedFront && proc.kbMode != 0 && proc.kbMode != 3)
    {
        const char* keys = proc.kbLayout == 0 ? "AZERTYUIOPQSDFGHJKLMWXCVBN" : "QWERTYUIOPASDFGHJKLZXCVBNM";
        for (int i = 0; keys[i] != 0; ++i)
        {
            const int code = (int) keys[i];
            if (juce::KeyPress::isKeyCurrentlyDown (code) && kbHeld.count (code) == 0)
                keyPressed (juce::KeyPress (code, juce::ModifierKeys(), (juce::juce_wchar) keys[i]));
        }
        for (int code : { juce::KeyPress::upKey, juce::KeyPress::downKey })   // flèches = octave
        {
            const bool down = juce::KeyPress::isKeyCurrentlyDown (code);
            if (down && ! arrowWasDown[code == juce::KeyPress::upKey ? 0 : 1]) keyPressed (juce::KeyPress (code));
            arrowWasDown[code == juce::KeyPress::upKey ? 0 : 1] = down;
        }
    }
    if (! kbHeld.empty()) keyStateChanged (false);   // sécurité : aucune note ne reste bloquée
    if (! kbHeld.empty())
    {
        // la fenêtre a perdu le focus (autre fenêtre, FL…) : Windows ne nous enverra pas le relâchement -> on coupe tout
        auto* f = juce::Component::getCurrentlyFocusedComponent();
        auto* peer = f != nullptr ? f->getPeer() : nullptr;
        // (les touches dont on connaît la touche Windows sont relâchées quand on les lâche vraiment, même sans le focus :
        //  on ne coupe que les autres — avant, dans FL, les chops du Sampler s'arrêtaient trop tôt)
        if (! hostedFront && (peer == nullptr || ! peer->isFocused() || ! juce::Process::isForegroundProcess()))
        {
            bool unknown = false;
            for (auto& [code, notes] : kbHeld) { const auto it = kbVk.find (code); if (it == kbVk.end() || it->second == 0) unknown = true; }
            if (unknown) releaseAllKeys();
        }
    }

    // garde le clavier sur le plugin (sauf pendant une saisie de texte)
    if (isShowing() && getPeer() != nullptr && getPeer()->isFocused())
    {
        auto* f = juce::Component::getCurrentlyFocusedComponent();
        const bool typing = dynamic_cast<juce::TextEditor*> (f) != nullptr;
        // la liste de la banque garde le clavier (flèches haut / bas, Entrée)
        const bool inBank = f != nullptr && libraryPanel->isVisible() && libraryPanel->isParentOf (f);
        if (! typing && ! inBank && f != this) grabKeyboardFocus();
    }
}

void VariantEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    soundPage->soundChanged();
}

void VariantEditor::paint (juce::Graphics& g)
{
    g.fillAll (Palette::bg);
}

// Fond animé (tous les thèmes) : grandes nappes de couleur qui dérivent + poussières qui montent.
// Couleurs prises dans le thème (ou le drapeau pour les thèmes à décor). 0 en « effets visuels » = fond fixe.
void VariantEditor::paintAnimatedBackground (juce::Graphics& g)
{
    const int level = SparkleOverlay::intensity();
    if (level == 0) return;
    const auto rb = root.getLocalBounds().toFloat();
    const float t = bgTime;
    std::array<juce::Colour, 4> cols { Palette::mint, Palette::lavender, Palette::blue, Palette::peach };
    if (Palette::decor == 2) cols = { juce::Colour (0xff149954), juce::Colour (0xffe4312b), juce::Colour (0xfff2f0ea), juce::Colour (0xff149954) };
    else if (Palette::decor == 3) cols = { juce::Colour (0xff8e44ff), juce::Colour (0xffe8332f), juce::Colour (0xffb04fd6), juce::Colour (0xff5b2a9e) };
    else if (Palette::decor == 4) cols = { juce::Colour (0xff1aa34a), juce::Colour (0xffffd23f), juce::Colour (0xff2c5fd6), juce::Colour (0xff1aa34a) };
    else if (Palette::decor == 5) cols = { juce::Colour (0xff2f8a43), juce::Colour (0xff5cc8d6), juce::Colour (0xffc6e86a), juce::Colour (0xff1d5e2e) };
    else if (Palette::decor == 6) cols = { juce::Colour (0xffe8a25a), juce::Colour (0xffe8664a), juce::Colour (0xffd9b36a), juce::Colour (0xff6ab0d0) };
    const float strength = SparkleOverlay::power();
    for (int k = 0; k < 4; ++k)
    {
        const float fx = 0.5f + 0.42f * std::sin (t * (0.050f + 0.013f * k) + k * 1.9f);
        const float fy = 0.5f + 0.40f * std::cos (t * (0.041f + 0.011f * k) + k * 2.7f);
        const float r = (0.30f + 0.08f * std::sin (t * 0.09f + k)) * juce::jmax (rb.getWidth(), rb.getHeight());
        const juce::Point<float> c (rb.getX() + fx * rb.getWidth(), rb.getY() + fy * rb.getHeight());
        juce::ColourGradient grad (cols[(size_t) k].withAlpha (0.17f * strength), c.x, c.y, cols[(size_t) k].withAlpha (0.0f), c.x + r, c.y, true);
        g.setGradientFill (grad);
        g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
    }
    // poussières lumineuses qui montent doucement
    juce::Random rr (77);
    const int n = level == 2 ? 120 : 70;
    for (int i = 0; i < n; ++i)
    {
        const float speed = 6.0f + rr.nextFloat() * 14.0f;
        const float x0 = rr.nextFloat() * rb.getWidth();
        const float y = rb.getBottom() - std::fmod (rr.nextFloat() * rb.getHeight() + t * speed, rb.getHeight() + 20.0f);
        const float x = x0 + 14.0f * std::sin (t * 0.4f + (float) i);
        const float s = 1.2f + rr.nextFloat() * 2.4f;
        const float a = (0.18f + 0.20f * (0.5f + 0.5f * std::sin (t * 1.3f + (float) i * 0.7f))) * strength;
        g.setColour ((i % 3 == 0 ? cols[(size_t) (i % 4)] : Palette::text).withAlpha (a));
        g.fillEllipse (x - s * 0.5f, y - s * 0.5f, s, s);
    }
    // reflet qui balaie l'en-tête
    const float sweep = std::fmod (t * 0.08f, 1.6f) - 0.3f;
    juce::ColourGradient sw (juce::Colours::white.withAlpha (0.0f), rb.getWidth() * (sweep - 0.15f), 0.0f,
                             juce::Colours::white.withAlpha (0.0f), rb.getWidth() * (sweep + 0.15f), 0.0f, false);
    sw.addColour (0.5, juce::Colours::white.withAlpha (0.09f * strength));
    g.setGradientFill (sw);
    g.fillRect (headerArea.toFloat());
}

void VariantEditor::paintRoot (juce::Graphics& g)
{
    g.fillAll (Palette::bg);
    paintAnimatedBackground (g);
    // ---- décors de thème (derrière tout le contenu)
    if (Palette::decor >= 2 && Palette::decor <= 4)
    {
        const auto rb = root.getLocalBounds().toFloat();
        if (Palette::decor == 2)   // Palestine : motif de keffieh (filet + feuilles d'olivier) en bandes, couleurs du drapeau
        {
            auto band = [&] (juce::Rectangle<float> b)
            {
                g.setColour (juce::Colour (0xfff2f0ea).withAlpha (0.22f)); g.fillRect (b);
                g.setColour (juce::Colour (0xff111111).withAlpha (0.85f));
                for (float x = b.getX(); x < b.getRight(); x += 10.0f)            // feuilles d'olivier en zigzag
                {
                    juce::Path leaf; leaf.addEllipse (x, b.getCentreY() - 2.5f, 7.0f, 5.0f);
                    leaf.applyTransform (juce::AffineTransform::rotation (((int) (x / 10.0f) % 2 == 0) ? 0.6f : -0.6f, x + 3.5f, b.getCentreY()));
                    g.fillPath (leaf);
                }
            };
            // filet (diagonales croisées) très léger sur tout le fond
            g.setColour (juce::Colours::white.withAlpha (0.055f));
            for (float x = -rb.getHeight(); x < rb.getWidth(); x += 18.0f)
            {
                g.drawLine (x, 0.0f, x + rb.getHeight(), rb.getHeight(), 1.0f);
                g.drawLine (x + rb.getHeight(), 0.0f, x, rb.getHeight(), 1.0f);
            }
            band (rb.withHeight (14.0f));
            band (rb.withTrimmedTop (rb.getHeight() - 12.0f));
            band (rb.withX (rb.getRight() - 12.0f).withWidth (12.0f));
            band (rb.withWidth (12.0f));
            // liseré aux couleurs du drapeau
            const float y = 12.0f;
            const juce::Colour cols[] = { juce::Colour (0xff111111), juce::Colours::white, juce::Colour (0xff149954), juce::Colour (0xffe4312b) };
            for (int k = 0; k < 4; ++k) { g.setColour (cols[k].withAlpha (0.7f)); g.fillRect (rb.getWidth() * k / 4.0f, y, rb.getWidth() / 4.0f, 2.0f); }
            // pastèque discrète en bas à gauche
            juce::Path melon; melon.addPieSegment (24.0f, rb.getBottom() - 70.0f, 46.0f, 46.0f, juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi * 3.0f, 0.0f);
            g.setColour (juce::Colour (0xff149954).withAlpha (0.18f)); g.fillPath (melon);
            juce::Path flesh; flesh.addPieSegment (28.0f, rb.getBottom() - 66.0f, 38.0f, 38.0f, juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi * 3.0f, 0.0f);
            g.setColour (juce::Colour (0xffe4312b).withAlpha (0.18f)); g.fillPath (flesh);
        }
        else if (Palette::decor == 3)   // LFI : dégradé violet et grand logo φ en filigrane
        {
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff8e44ff).withAlpha (0.14f), 0.0f, 0.0f,
                                                     juce::Colour (0xffe8332f).withAlpha (0.06f), rb.getWidth(), rb.getHeight(), false));
            g.fillRect (rb);
            const juce::Point<float> c (rb.getRight() - 150.0f, rb.getBottom() - 210.0f);
            g.setColour (juce::Colour (0xffb04fd6).withAlpha (0.10f));
            g.drawEllipse (c.x - 70.0f, c.y - 70.0f, 140.0f, 140.0f, 18.0f);
            g.fillRect (c.x - 9.0f, c.y - 120.0f, 18.0f, 240.0f);
        }
        else   // Brésil : losange jaune + globe bleu en filigrane, petites étoiles
        {
            const juce::Point<float> c (rb.getCentreX(), rb.getCentreY() + 40.0f);
            juce::Path d; d.addQuadrilateral (c.x, c.y - 210.0f, c.x + 360.0f, c.y, c.x, c.y + 210.0f, c.x - 360.0f, c.y);
            g.setColour (juce::Colour (0xffffd23f).withAlpha (0.06f)); g.fillPath (d);
            g.setColour (juce::Colour (0xff2c5fd6).withAlpha (0.08f)); g.fillEllipse (c.x - 130.0f, c.y - 130.0f, 260.0f, 260.0f);
            juce::Random rr (27);
            for (int k = 0; k < 18; ++k)
            {
                juce::Path st; st.addStar ({ c.x - 110.0f + rr.nextFloat() * 220.0f, c.y - 40.0f + rr.nextFloat() * 120.0f }, 5, 1.5f, 3.5f, 0.0f);
                g.setColour (juce::Colours::white.withAlpha (0.12f)); g.fillPath (st);
            }
        }
    }

    auto h = headerArea;
    if (Palette::y2k)
    {
        // décor Y2K : grille de points, étoiles scintillantes, titre chromé
        const auto rb = root.getLocalBounds().toFloat();
        g.setGradientFill (juce::ColourGradient (Palette::lavender.withAlpha (0.10f), 0.0f, 0.0f,
                                                 Palette::blue.withAlpha (0.06f), rb.getWidth(), rb.getHeight(), false));
        g.fillRect (rb);
        g.setColour (Palette::text.withAlpha (Palette::light ? 0.10f : 0.07f));
        for (float y = 12.0f; y < rb.getHeight(); y += 24.0f)
            for (float x = 12.0f; x < rb.getWidth(); x += 24.0f)
                g.fillEllipse (x - 1.0f, y - 1.0f, 2.0f, 2.0f);
        const float stars[][3] = { { 150, 22, 7 }, { 868, 64, 5 }, { 30, 700, 6 }, { 470, 12, 4 }, { 880, 712, 4 } };
        for (auto& s : stars)
        {
            juce::Path p;
            p.addStar ({ s[0], s[1] }, 4, s[2] * 0.22f, s[2], 0.0f);
            g.setColour ((s[2] > 5 ? Palette::pink : Palette::mint).withAlpha (0.8f));
            g.fillPath (p);
        }
    }
    // logo en fond, tout le long du haut du plugin (à la place du logo à gauche) : grand « VARIANT » + 3 ondes qui bougent
    {
        const auto rb = root.getLocalBounds().toFloat();
        const auto band = juce::Rectangle<float> (0.0f, 0.0f, rb.getWidth(), (float) headerArea.getBottom() + 30.0f);
        const float lvl = juce::jlimit (0.0f, 1.0f, proc.outLevel.load() * 2.5f);
        const juce::Colour wc[] = { Palette::mint, Palette::lavender, Palette::peach };
        for (int k = 0; k < 3; ++k)
        {
            juce::Path w;
            for (int i = 0; i <= 120; ++i)
            {
                const float x = band.getX() + band.getWidth() * i / 120.0f;
                const float y = band.getCentreY() + (k - 1) * 9.0f + std::sin (i * 0.11f + bgTime * (0.9f + 0.3f * k) + k * 1.3f) * (7.0f - k * 1.5f) * (1.0f + lvl * 1.5f);
                if (i == 0) w.startNewSubPath (x, y); else w.lineTo (x, y);
            }
            g.setColour (wc[k].withAlpha (0.24f));
            g.strokePath (w, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        // vinyle qui tourne, à moitié sorti à droite de l'en-tête
        {
            const juce::Point<float> c (band.getRight() - 26.0f, band.getCentreY() + 6.0f);
            const float R = band.getHeight() * 0.95f;
            g.setColour (juce::Colours::black.withAlpha (Palette::light ? 0.10f : 0.35f));
            g.fillEllipse (c.x - R, c.y - R, R * 2.0f, R * 2.0f);
            g.setColour (Palette::text.withAlpha (0.07f));
            for (float rr = R * 0.45f; rr < R; rr += 4.0f) g.drawEllipse (c.x - rr, c.y - rr, rr * 2.0f, rr * 2.0f, 0.8f);
            const float a = bgTime * 1.4f;
            juce::Path shine; shine.addPieSegment (c.x - R, c.y - R, R * 2.0f, R * 2.0f, a, a + 0.5f, 0.45f);
            g.setColour (Palette::text.withAlpha (0.08f)); g.fillPath (shine);
            shine.applyTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::pi, c.x, c.y)); g.fillPath (shine);
            g.setColour (Palette::pink.withAlpha (0.45f)); g.fillEllipse (c.x - R * 0.3f, c.y - R * 0.3f, R * 0.6f, R * 0.6f);
            g.setColour (Palette::bg); g.fillEllipse (c.x - 3.0f, c.y - 3.0f, 6.0f, 6.0f);
        }
        // égaliseur qui danse avec le son, sous le logo
        {
            const int nb = 72;
            const float bw = band.getWidth() / (float) nb;
            for (int i = 0; i < nb; ++i)
            {
                const float u = (float) i / (float) nb;
                const float hgt = 3.0f + (0.35f + 0.65f * lvl) * 14.0f * (0.5f + 0.5f * std::sin (bgTime * (2.0f + 3.0f * u) + i * 0.9f)) * (0.4f + 0.6f * std::sin (u * juce::MathConstants<float>::pi));
                g.setColour ((i % 3 == 0 ? Palette::mint : (i % 3 == 1 ? Palette::lavender : Palette::blue)).withAlpha (0.20f + 0.15f * lvl));
                g.fillRoundedRectangle (band.getX() + i * bw + 1.0f, band.getBottom() - hgt, bw - 2.0f, hgt, 1.0f);
            }
        }
        // le mot VARIANT : rempli + contour, bien plus visible qu'avant
        const float fh = band.getHeight() * 0.95f;
        auto f = uiFont (fh, juce::Font::bold);
        const float natural = juce::GlyphArrangement::getStringWidth (f, "VARIANT");
        f = f.withExtraKerningFactor (juce::jmax (0.0f, (band.getWidth() - 40.0f - natural) / (7.0f * fh)));   // étiré sur toute la largeur
        juce::GlyphArrangement ga;
        ga.addLineOfText (f, "VARIANT", 20.0f, band.getCentreY() + fh * 0.33f);
        juce::Path txt; ga.createPath (txt);
        g.setColour (juce::Colours::black.withAlpha (Palette::light ? 0.06f : 0.30f));
        g.fillPath (txt, juce::AffineTransform::translation (0.0f, 3.0f));
        juce::ColourGradient tg (Palette::mint.withAlpha (0.30f), 0.0f, 0.0f, Palette::peach.withAlpha (0.30f), band.getWidth(), 0.0f, false);
        tg.addColour (0.5, Palette::lavender.withAlpha (0.34f));
        g.setGradientFill (tg);
        g.fillPath (txt);
        // reflet qui traverse les lettres
        const float sx = std::fmod (bgTime * 90.0f, band.getWidth() + 400.0f) - 200.0f;
        juce::ColourGradient sh (juce::Colours::white.withAlpha (0.0f), sx - 120.0f, 0.0f, juce::Colours::white.withAlpha (0.0f), sx + 120.0f, 0.0f, false);
        sh.addColour (0.5, juce::Colours::white.withAlpha (Palette::light ? 0.25f : 0.16f));
        g.setGradientFill (sh);
        g.fillPath (txt);
        g.setColour (Palette::text.withAlpha (Palette::light ? 0.22f : 0.20f));
        g.strokePath (txt, juce::PathStrokeType (1.2f));
    }
    paintThemeScenery (g);


    if (Palette::decor >= 2 && Palette::decor <= 4)
    {
        // emblème du thème à côté du titre
        const juce::Rectangle<float> f ((float) h.getX(), (float) h.getCentreY() - 11.0f, 36.0f, 22.0f);
        juce::Path clip; clip.addRoundedRectangle (f, 4.0f);
        g.saveState();
        g.reduceClipRegion (clip);
        if (Palette::decor == 2)          // drapeau palestinien
        {
            const float b = f.getHeight() / 3.0f;
            g.setColour (juce::Colour (0xff111111)); g.fillRect (f.withHeight (b));
            g.setColour (juce::Colours::white);     g.fillRect (f.withY (f.getY() + b).withHeight (b));
            g.setColour (juce::Colour (0xff149954)); g.fillRect (f.withY (f.getY() + 2 * b).withHeight (b));
            juce::Path tri; tri.addTriangle (f.getX(), f.getY(), f.getX(), f.getBottom(), f.getX() + f.getWidth() * 0.42f, f.getCentreY());
            g.setColour (juce::Colour (0xffe4312b)); g.fillPath (tri);
        }
        else if (Palette::decor == 3)     // symbole phi en dégradé rouge -> violet
        {
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe8332f), f.getX(), f.getY(), juce::Colour (0xffb04fd6), f.getRight(), f.getBottom(), false));
            g.fillRoundedRectangle (f, 4.0f);
            g.setColour (juce::Colours::white);
            g.drawEllipse (f.getCentreX() - 6.0f, f.getCentreY() - 6.0f, 12.0f, 12.0f, 2.0f);
            g.drawLine (f.getCentreX(), f.getY() + 2.0f, f.getCentreX(), f.getBottom() - 2.0f, 2.0f);
        }
        else                              // drapeau brésilien
        {
            g.setColour (juce::Colour (0xff009c3b)); g.fillRect (f);
            juce::Path d;
            d.addQuadrilateral (f.getCentreX(), f.getY() + 2.5f, f.getRight() - 3.0f, f.getCentreY(), f.getCentreX(), f.getBottom() - 2.5f, f.getX() + 3.0f, f.getCentreY());
            g.setColour (juce::Colour (0xffffdf00)); g.fillPath (d);
            g.setColour (juce::Colour (0xff002776)); g.fillEllipse (f.getCentreX() - 5.5f, f.getCentreY() - 5.5f, 11.0f, 11.0f);
            g.setColour (juce::Colours::white); g.drawLine (f.getCentreX() - 5.0f, f.getCentreY() - 0.5f, f.getCentreX() + 5.0f, f.getCentreY() + 1.5f, 1.2f);
        }
        g.restoreState();
    }

    // barre d'aide (comme la hint bar de FL Studio)
    {
        auto hb = hintArea.toFloat();
        g.setColour (Palette::panel.withAlpha (0.7f)); g.fillRoundedRectangle (hb, 4.0f);
        g.setColour (Palette::blue.withAlpha (0.9f)); g.fillRect (hb.withWidth (3.0f).reduced (0.0f, 3.0f));
        g.setColour (hintText.isEmpty() ? Palette::dim.withAlpha (0.6f) : Palette::text.withAlpha (0.9f));
        g.setFont (uiFont (12.5f));
        g.drawText (hintText.isEmpty() ? fr ("Passe la souris sur un réglage pour voir à quoi il sert") : hintText, hintArea.withTrimmedLeft (10), juce::Justification::centredLeft, true);
    }

    // conteneur des onglets (pilule)
    if (! tabs.isEmpty())
    {
        auto pill = tabs.getFirst()->getBounds().getUnion (tabs.getLast()->getBounds()).toFloat().expanded (4.0f);
        g.setColour (Palette::panel.withAlpha (0.55f));   // translucide : le logo VARIANT en fond reste visible
        g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
        g.setColour (Palette::line);
        g.drawRoundedRectangle (pill, pill.getHeight() * 0.5f, 1.0f);
    }
}

juce::Rectangle<int> VariantEditor::screenArea() const
{
    // écran où se trouve la souris (celui de FL), sinon l'écran principal
    const auto& ds = juce::Desktop::getInstance().getDisplays();
    if (auto* d = ds.getDisplayForPoint (juce::Desktop::getMousePosition())) return d->userArea;
    if (auto* d = ds.getPrimaryDisplay()) return d->userArea;
    return { 0, 0, 1920, 1040 };
}

void VariantEditor::resized()
{
    // proportions fixes : l'échelle suit la taille de la fenêtre
    const float s = juce::jmax (0.4f, juce::jmin ((float) getWidth() / (float) baseW, (float) getHeight() / 600.0f));
    // taille choisie à la souris : gardée pour la prochaine ouverture
    if (isShowing() && std::abs (s - uiScale) > 0.01f && (float) getWidth() / (float) baseW > 0.55f)
    {
        uiScale = juce::jlimit (0.6f, 2.0f, (float) getWidth() / (float) baseW);
        library::settings().setValue ("uiScale", (double) uiScale);
    }
    root.setBounds (0, 0, juce::roundToInt ((float) getWidth() / s), juce::roundToInt ((float) getHeight() / s));
    root.setTransform (juce::AffineTransform::translation (shakeX, shakeY).scaled (s));
}

void VariantEditor::layoutRoot()
{
    auto r = root.getLocalBounds().reduced (18, 16);

    headerArea = r.removeFromTop (40);
    auto header = headerArea;
    settingsButton.setBounds (header.removeFromRight (34).withSizeKeepingCentre (34, 32));
    header.removeFromRight (6);
    detachButton.setBounds (header.removeFromRight (34).withSizeKeepingCentre (34, 32));
    header.removeFromRight (6);
    obliterateBtn.setBounds (header.removeFromRight (34).withSizeKeepingCentre (34, 32));
    header.removeFromRight (8);
    {
        auto bank = header.removeFromRight (172).withSizeKeepingCentre (172, 32);
        bankNext.setBounds (bank.removeFromRight (26)); bank.removeFromRight (3);
        bankPrev.setBounds (bank.removeFromLeft (26)); bank.removeFromLeft (3);
        bankButton.setBounds (bank);
    }
    header.removeFromRight (6);
    metroButton.setBounds (header.removeFromRight (34).withSizeKeepingCentre (34, 32));
    header.removeFromRight (6);
    saveButton.setBounds (header.removeFromRight (34).withSizeKeepingCentre (34, 32));
    header.removeFromLeft ((Palette::decor >= 2 && Palette::decor <= 4) ? 48 : 4);   // plus de logo à gauche : il est en fond, tout le long du haut
    header.removeFromRight (4);
    const int tg = 4, nt = tabs.size();
    const int tw = (juce::jmin (header.getWidth(), 680) - (nt - 1) * tg) / juce::jmax (1, nt);
    auto tabRow = header.withSizeKeepingCentre (nt * tw + (nt - 1) * tg, 32);
    for (auto* t : tabs)
    {
        t->setBounds (tabRow.removeFromLeft (tw));
        tabRow.removeFromLeft (tg);
    }

    r.removeFromTop (3);
    hintArea = r.removeFromTop (17);   // barre d'aide façon FL Studio
    r.removeFromTop (4);
    auto keys = r.removeFromBottom (64);
    if (buddyOn)
    {
        auto b = keys.removeFromRight (84);
        buddy.setBounds (b.getX(), b.getBottom() - 105, 84, 105);
        keys.removeFromRight (6);
    }
    keyboard.setBounds (keys);
    keyboard.setKeyWidth ((float) keys.getWidth() / 36.0f);
    r.removeFromBottom (12);
    pageArea = r;

    for (juce::Component* page : { (juce::Component*) soundPage.get(), (juce::Component*) fxPage.get(), (juce::Component*) chordPage.get(),
                                   (juce::Component*) arpPage.get(), (juce::Component*) toolsPage.get(), (juce::Component*) synthPage.get(), (juce::Component*) libraryPanel.get() })
        if (page != nullptr) page->setBounds (r);
    help.setBounds (root.getLocalBounds());
    statsPanel.setBounds (root.getLocalBounds());
    reportPanel.setBounds (root.getLocalBounds());
    handleGate.setBounds (root.getLocalBounds());
    blockedScreen.setBounds (root.getLocalBounds());
    placeholder.setBounds (r);
    chat.setBounds (buddy.getRight() - 400, buddy.getY() - 312, 400, 318);
    tour.setBounds (root.getLocalBounds());
    if (sparkles) sparkles->setBounds (root.getLocalBounds());
}

// Clavier d'ordinateur : renvoie l'index de la touche (blanches 0..9, noires 0..6)
int VariantEditor::keyIndex (const juce::KeyPress& k, bool& black, int& octaveDelta) const
{
    black = false; octaveDelta = 0;
    const int code = k.getKeyCode();
    const juce::juce_wchar ch = juce::CharacterFunctions::toUpperCase (k.getTextCharacter());
    auto is = [&] (juce::juce_wchar c) { return code == (int) c || ch == c; };
    const bool az = proc.kbLayout == 0;
    const char* whites = az ? "QSDFGHJKLM" : "ASDFGHJKL;";
    const char* blacks = az ? "ZETYUOP" : "WETYUOP";
    for (int i = 0; i < 10; ++i) if (is ((juce::juce_wchar) whites[i])) return i;
    for (int i = 0; i < 7; ++i) if (is ((juce::juce_wchar) blacks[i])) { black = true; return i; }
    if (is (az ? 'W' : 'Z')) { octaveDelta = -1; return -1; }
    if (is ('X')) { octaveDelta = 1; return -1; }
    return -1;
}

void VariantEditor::releaseAllKeys()
{
    for (auto& [code, notes] : kbHeld)
        for (int n : notes)
        {
            if (n >= 1000) proc.playSlice (n - 1000, false);
            else proc.keyboardState.noteOff (proc.kbMode == 2 ? 2 : 1, n, 0.0f);
        }
    kbHeld.clear();
    kbVk.clear();
}

bool VariantEditor::keyStateChanged (bool)
{
    // relâchement des touches du clavier d'ordinateur
    // (chiffres et ponctuation : on regarde la vraie touche Windows — JUCE confond par ex. « & » avec la flèche haut)
    for (auto it = kbHeld.begin(); it != kbHeld.end();)
    {
        const auto vk = kbVk.find (it->first);
        const bool down = (vk != kbVk.end() && vk->second != 0) ? vkDown (vk->second) : juce::KeyPress::isKeyCurrentlyDown (it->first);
        if (! down)
        {
            kbVk.erase (it->first);
            for (int n : it->second)
            {
                if (n >= 1000) proc.playSlice (n - 1000, false);
                else proc.keyboardState.noteOff (proc.kbMode == 2 ? 2 : 1, n, 0.0f);
            }
            it = kbHeld.erase (it);
        }
        else ++it;
    }
    return false;
}
//==============================================================================
// Guide "Kick" : explique ce qu'il y a sous la souris
juce::String VariantEditor::pageHelp (int tab) const
{
    static const char* help[] = {
        "Onglet Son : glisse jusqu'à 8 sons (une couche chacun). Chaque note joue une variante un peu différente pour un rendu vivant. "
        "En bas : enveloppe, variations et slide entre les notes.",
        "Onglet Effets : glisse une tuile dans la chaîne (6 places). Chaque effet a des presets et un bouton Hasard.",
        "Accords & MIDI : le piano roll est en haut. Génère une progression, modifie accords et notes, puis glisse le MIDI dans FL. "
        "Les autres réglages sont plus bas en faisant défiler.",
        "Arpège : tiens des notes, il joue en rythme calé sur FL. « Tout au hasard » trouve un motif en un clic.",
        "Sampler : trouve le tempo et la tonalité d'un son, et découpe un sample en tranches jouables avec le slicer.",
        "Synthé : crée ton propre son façon Serum. Choisis une table d'ondes (celles de Serum sur ton PC), règle le filtre et les enveloppes, et clic droit sur un bouton pour le moduler avec un LFO." };
    return fr (help[juce::jlimit (0, numPages - 1, tab)]) + "\n" + fr ("Laisse traîner ta souris sur un bouton, je t'explique. Clique sur moi pour un tips.");
}

void VariantEditor::buddySay (const juce::String& title, const juce::String& text)
{
    if (! buddyOn || text.isEmpty()) return;
    const int w = 300;
    bubble.setText (title, text);
    const int h = juce::jmin (260, bubble.idealHeight (w));
    const auto b = buddy.getBounds();
    bubble.setBounds (b.getRight() - w, b.getY() - h + 6, w, h);
    bubble.setVisible (true);
    bubble.toFront (false);
    buddy.toFront (false);
    buddy.setTalking (true);
    quietTicks = 0;
}

void VariantEditor::updateBuddy()
{
    if (! buddyOn || ! buddy.isShowing()) return;
    if (chat.isVisible() || tour.isRunning()) { bubble.setVisible (false); buddy.setTalking (chat.isVisible()); buddy.tick (1.0f / 30.0f); return; }
    auto ms = juce::Desktop::getInstance().getMainMouseSource();   // (copie : le compilateur du Mac refuse une référence sur un temporaire)
    buddy.setLookTarget (buddy.getLocalPoint (nullptr, ms.getScreenPosition()));
    buddy.tick (1.0f / 30.0f);

    juce::String title, text;
    auto* c = ms.getComponentUnderMouse();
    if (c == &buddy)
    {
        title = fr ("Yo, c'est Kick.");
        text = pageHelp (currentTab);
    }
    else
    {
        for (auto* p = c; p != nullptr; p = p->getParentComponent())
        {
            if (auto* tc = dynamic_cast<juce::TooltipClient*> (p))
            {
                text = tc->getTooltip();
                if (text.isNotEmpty())
                {
                    static const char* intros[] = { "En vrai", "Écoute bien", "Vite fait", "Le truc", "Trust", "Simple", "Ok donc", "Petit tips",
                                                    "Tu hésites ?", "Je t'ai vu", "Allez, je t'aide", "Spoiler", "Laisse-moi deviner", "T'inquiète", "Bon…", "Psst" };
                    if (text != hoverText) introIndex = juce::Random::getSystemRandom().nextInt (16);
                    title = fr (intros[introIndex]);
                    break;
                }
            }
        }
        if (title.length() > 40) title = {};
    }

    // inactif depuis 2 minutes : petite pique (une fois par pause)
    {
        const auto mp = ms.getScreenPosition();
        if (mp.getDistanceFrom (lastMouse) > 2.0f) { lastMouse = mp; idleTicks = 0; idleTeased = false; }
        else if (! idleTeased && ++idleTicks > 30 * 120 && isShowing())
        {
            idleTeased = true;
            static const char* idle[] = { "Allô ? T'es parti faire un café ?", "Page blanche ? Clique « Surprends-moi », je dirai rien à personne.",
                                          "Je m'ennuie… balance un sample dans le slicer, on rigole.", "Pendant que tu réfléchis, moi je fais des pompes. Préviens quand t'as fini.",
                                          "Si t'attends l'inspiration, elle est dans l'onglet Arpège, bouton « Tout au hasard »." };
            if (juce::Random::getSystemRandom().nextBool()) sayQuote();
            else buddySay (fr ("Hé"), fr (idle[juce::Random::getSystemRandom().nextInt (5)]));
            muteTicks = 120;
            return;
        }
    }

    if (muteTicks > 0) { --muteTicks; hoverText = text; hoverTicks = 100; quietTicks = 0; return; }
    if (text != hoverText) { hoverText = text; hoverTicks = 0; }
    else ++hoverTicks;

    if (text.isNotEmpty())
    {
        quietTicks = 0;
        if (hoverTicks == 36) buddySay (title, text);   // ~1,2 s immobile sur un élément : Kick n'intervient que si on hésite
        if (hoverTicks == 300)                          // 10 s à fixer le même bouton : Kick te taquine
        {
            static const char* stare[] = { "Ça fait 10 secondes que tu fixes ce bouton. Clique, il mord pas.", "Tu le regardes comme s'il te devait de l'argent.",
                                           "Je crois qu'il est timide. Fais le premier pas.", "Si tu le fixes encore, il va se mettre à jouer tout seul." };
            buddySay (fr ("Euh…"), fr (stare[juce::Random::getSystemRandom().nextInt (4)]));
        }
    }
    else if (bubble.isVisible() && ++quietTicks > 15)
    {
        bubble.setVisible (false);
        buddy.setTalking (false);
    }
    if (bubble.isVisible() && hoverTicks > 75) buddy.setTalking (false);
}

void VariantEditor::toggleKickOut()
{
    if (kickOut != nullptr) { kickOut = nullptr; return; }
    kickOut = std::make_unique<KickDesktop> (buddy.getSkin());
    kickOut->onClose = [safe = juce::Component::SafePointer<VariantEditor> (this)]
    {
        if (safe != nullptr) juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->kickOut = nullptr; });
    };
}

void VariantEditor::setBuddyOn (bool on)
{
    buddyOn = on;
    library::settings().setValue ("buddy", buddyOn);
    library::settings().saveIfNeeded();
    if (! on && kickOut != nullptr) toggleKickOut();   // Kick sorti sur le bureau : on le rentre aussi
    buddy.setVisible (buddyOn);
    bubble.setVisible (false);
    if (buddyOn) tooltips.reset();
    else tooltips = std::make_unique<juce::TooltipWindow> (this, 500);   // sans Kick : les infobulles classiques
    layoutRoot();
}

// petit portrait de chaque skin (pour les menus)
juce::Image VariantEditor::skinIcon (int s)
{
    static std::map<int, juce::Image> cache;
    auto it = cache.find (s);
    if (it != cache.end()) return it->second;
    Buddy b; b.setSize (40, 64); b.setSkin (s); b.tick (0.01f);
    auto full = b.createComponentSnapshot (b.getLocalBounds(), true, 1.0f);
    const bool cube = juce::String (buddySkins()[(size_t) s].name).startsWith ("Cube");   // le cube est posé en bas
    auto img = juce::SoftwareImageType().convert (full.getClippedImage ({ 4, cube ? 30 : 0, 32, 30 }));   // la tête et les épaules (image en mémoire : voir themePreview)
    cache[s] = img;
    return img;
}

// skins par ordre alphabétique (les numéros restent ceux de la liste)
std::vector<int> VariantEditor::sortedSkins()
{
    std::vector<int> o;
    for (int i = 0; i < (int) buddySkins().size(); ++i) o.push_back (i);
    std::sort (o.begin(), o.end(), [] (int a, int b) { return fr (buddySkins()[(size_t) a].name).compareNatural (fr (buddySkins()[(size_t) b].name)) < 0; });
    return o;
}

void VariantEditor::showSkinMenu()
{
    juce::PopupMenu m;
    m.addItem (100, fr ("Une réplique !"));
    m.addItem (101, kickOut != nullptr ? fr ("Rentrer Kick dans le plugin") : fr ("Sortir Kick sur le bureau (il danse sur le tempo)"));
    m.addItem (102, fr ("Désactiver Kick (tu peux le remettre dans Réglages)"));
    m.addSeparator();
    m.addSectionHeader (fr ("Skin de Kick"));
    for (int i : sortedSkins()) m.addItem (1 + i, fr (buddySkins()[(size_t) i].name), true, buddy.getSkin() == i, skinIcon (i));
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&buddy),
        [safe = juce::Component::SafePointer<VariantEditor> (this)] (int r)
        {
            if (safe == nullptr || r <= 0) return;
            if (r == 100) { safe->sayQuote(); return; }
            if (r == 101) { safe->toggleKickOut(); return; }
            if (r == 102) { safe->setBuddyOn (false); return; }
            safe->buddy.setSkin (r - 1);
            library::settings().setValue ("buddySkin", r - 1);
            library::settings().saveIfNeeded();
            safe->sayQuote();
            safe->muteTicks = 60;
        });
}

void VariantEditor::startTour()
{
    if (soundPage == nullptr) return;
    chat.setVisible (false);
    bubble.setVisible (false);
    for (int i = 0; i < numPages; ++i) if (windows[(size_t) i] != nullptr) reattachPage (i);
    // zone d'une carte (en coordonnées de « content ») : la page défile pour la montrer, puis on la renvoie en coordonnées de l'éditeur
    auto reveal = [this] (juce::Component* content, juce::Rectangle<int> r)
    {
        if (content == nullptr || r.isEmpty()) return juce::Rectangle<int>();
        if (auto* vp = content->findParentComponentOfClass<juce::Viewport>())
        {
            auto* viewed = vp->getViewedComponent();
            const auto inView = viewed != nullptr ? viewed->getLocalArea (content, r) : r;
            const auto pos = vp->getViewPosition();
            if (inView.getY() < pos.y || inView.getBottom() > pos.y + vp->getViewHeight())
                vp->setViewPosition (0, juce::jmax (0, inView.getY() - 30));
        }
        if (! content->isShowing()) return juce::Rectangle<int>();
        return root.getLocalArea (content, r).getIntersection (root.getLocalBounds());
    };
    auto rectOf = [reveal] (juce::Component* c)
    {
        if (c == nullptr || c->getParentComponent() == nullptr) return juce::Rectangle<int>();
        return reveal (c->getParentComponent(), c->getBounds());
    };
    auto& sc = soundPage->content;
    auto& fc = fxPage->content;
    auto& cc = chordPage->content;
    std::vector<TourOverlay::Step> steps {
        { 0, nullptr, fr ("Yo, bienvenue !"), fr ("Moi c'est Kick. Je te fais le tour de tout le plugin, étape par étape. Clique « Passer » quand tu veux.") },
        // ---- onglet Son
        { 0, [rectOf, &sc] { auto r = rectOf (sc.chips.getFirst()); for (auto* c : sc.chips) r = r.getUnion (rectOf (c)); return r; },
          fr ("Les couches"), fr ("Jusqu'à 8 sons joués ensemble, une couche chacun. Clique une couche pour la régler, le bouton rond l'allume ou l'éteint, le S la fait jouer seule.") },
        { 0, [rectOf, &sc] { return rectOf (&sc.waveform); },
          fr ("Ton son"), fr ("Glisse un one-shot ici (ou pioche dans la Bibliothèque). Chaque note joue une variante un peu différente : ça sonne vivant.") },
        { -1, [this] { return bankButton.getBounds().expanded (3); },
          fr ("Banque de sons"), fr ("Tous tes sons et presets rangés par catégories, avec une recherche sur toute la bibliothèque. Clic = écouter, double-clic = charger, flèches = son suivant.") },
        { 0, [rectOf, &sc] { return rectOf (&sc.autoButton); },
          fr ("Auto"), fr ("Choisis 2, 3 ou 4 sons : Variant ajoute des sons qui complètent le tien (corps, grave, brillance), accordés et équilibrés.") },
        { 0, [rectOf, &sc] { return rectOf (&sc.morphBtn).getUnion (rectOf (&sc.morphUndoBtn)).getUnion (rectOf (&sc.morphRedoBtn)); },
          fr ("Métamorphose"), fr ("Transforme ton one-shot en un tout autre son (nappe, cristal, reverse, grains…). Chaque clic donne un résultat différent, et les flèches annulent ou refont.") },
        { 0, [reveal, &sc] { return reveal (&sc, sc.varCard); },
          fr ("Variations"), fr ("Chaque note joue une prise un peu différente, comme un vrai musicien. Réaliste, Texture ou Expérimental, et l'intensité règle à quel point ça change.") },
        { 0, [reveal, &sc] { return reveal (&sc, sc.envCard); },
          fr ("Enveloppe"), fr ("Comment le son démarre et s'arrête : attaque, déclin, maintien, relâche. Tu peux aussi attraper les points du dessin.") },
        { 0, [reveal, &sc] { return reveal (&sc, sc.glideCard); },
          fr ("Slide / Glide"), fr ("Pour les 808 qui glissent : mets Legato et fais se chevaucher deux notes dans FL. Temps = durée du glissement.") },
        { 0, [rectOf, &sc] { return rectOf (sc.sampleCard.get()); },
          fr ("Sample"), fr ("Comme le Sampler de FL : départ, longueur, fondus, inverser, couper le blanc, et les Loop points pour faire tenir un son à l'infini.") },
        { 0, [rectOf, &sc] { return rectOf (&sc.hostedBar); },
          fr ("Plugins dans Variant"), fr ("Charge un VST3 (Serum, Zenology…) dans Variant : il joue tes notes, tes accords et ton arpège, et passe dans mes effets.") },
        { 0, [rectOf, &sc] { return rectOf (&sc.takeCard); },
          fr ("Prise"), fr ("Clique Rec et joue : ce que tu joues est enregistré, puis tu glisses le MIDI ou le WAV (avec les effets) direct dans FL.") },
        { 0, [this] { return keyboard.getBounds(); },
          fr ("Joue"), fr ("Clique sur ce clavier, ou joue depuis le piano roll de FL Studio.") },
        // ---- onglets
        { -1, [this] { return tabs.getFirst()->getBounds().getUnion (tabs.getLast()->getBounds()).expanded (4); },
          fr ("Les onglets"), fr ("Son, Effets, Accords & MIDI, Arpège, Sampler, Synthé : un outil par onglet. Laisse ta souris sur un bouton, je t'explique à quoi il sert.") },
        // ---- Effets
        { 1, [rectOf, &fc] { auto r = rectOf (fc.tiles.getFirst()); for (auto* t : fc.tiles) r = r.getUnion (rectOf (t)); return r; },
          fr ("Les effets"), fr ("21 effets : glisse une tuile dans ta chaîne (6 places). Chaque effet a ses presets, des flèches pour passer au suivant et un bouton Hasard.") },
        { 1, [rectOf, &fc] { return rectOf (&fc.randChain); },
          fr ("Chaîne au hasard"), fr ("Une chaîne d'effets complète dans un style (lo-fi, rêveur, agressif, glitch…) en un clic. Ils existent aussi en plugins séparés pour le mixer de FL.") },
        // ---- Accords & MIDI
        { 2, [reveal, &cc] { return reveal (&cc, cc.genCard); },
          fr ("Générateur d'accords"), fr ("Choisis une ambiance et clique « Générer une progression ». Variété et Hors gamme rendent les accords plus surprenants, « Surprends-moi » tire tout au hasard.") },
        { 2, [rectOf, &cc] { return rectOf (&cc.roll); },
          fr ("Piano roll"), fr ("Modifie les notes comme dans FL : crayon, pinceau, gomme, slide, accord, aimant. Espace pour écouter, Ctrl+Z pour annuler.") },
        { 2, [rectOf, &cc] { return rectOf (&cc.chipChords).getUnion (rectOf (&cc.chipAll)); },
          fr ("Export vers FL"), fr ("Glisse ces pastilles direct dans FL Studio : accords, basse, arpège, ou tout d'un coup.") },
        // ---- Arpège, Sampler, Synthé
        { 3, [this, rectOf] { return rectOf (&arpPage->randAll).getUnion (rectOf (&arpPage->randPattern)); },
          fr ("Arpège"), fr ("Tiens des notes, ça joue en rythme calé sur FL. En panne d'idées ? « Tout au hasard ».") },
        { 4, [this, rectOf] { return rectOf (&toolsPage->content.slicerCard).getIntersection (pageArea); },
          fr ("Sampler & slicer"), fr ("Glisse un sample : tempo, tonalité et découpe en tranches d'un coup. Joue les chops au clavier, enregistre-les et exporte-les.") },
        { 5, [this] { return pageArea.withHeight (juce::jmin (pageArea.getHeight(), 340)); },
          fr ("Synthé"), fr ("Un synthé à tables d'ondes façon Serum : 2 oscillateurs, sub, bruit, filtre, 3 enveloppes, 4 LFO. Clic droit sur un potard pour le moduler, « Hasard » pour un son tout fait.") },
        // ---- en-tête
        { -1, [this] { return metroButton.getBounds().getUnion (saveButton.getBounds()).expanded (3); },
          fr ("Métronome et presets"), fr ("Le métronome suit la lecture du plugin (clic droit pour son son). La disquette sauve ton son en preset, rangé dans ta banque.") },
        { -1, [this] { return detachButton.getBounds().getUnion (obliterateBtn.getBounds()).expanded (3); },
          fr ("Détacher et tout effacer"), fr ("⧉ ouvre la page dans sa propre fenêtre à côté de FL. La bombe remet tout à zéro (deux clics, pas de retour en arrière).") },
        { -1, [this] { return settingsButton.getBounds().expanded (3); },
          fr ("Réglages"), fr ("Thème, langue, taille, effets visuels, raccourcis… et « Signaler un bug » si quelque chose ne marche pas : le créateur le reçoit direct.") },
        { 0, [this] { return buddy.getBounds(); },
          fr ("Et moi"), fr ("Perdu ? Clique sur moi et pose ta question. Clic droit pour changer ma tenue.") },
    };
    tour.setBounds (root.getLocalBounds());
    tour.start (std::move (steps));
    buddy.toFront (false);
}

void VariantEditor::openMini (int kind)
{
    kind = juce::jlimit (0, 2, kind);
    if (auto& m = minis[(size_t) kind]) { m->toFront (true); return; }
    std::unique_ptr<juce::Component> panel;
    int w = 520, h = 190;
    juce::String title;
    switch (kind)
    {
        case 0:  panel = std::make_unique<MiniGenerator> (proc); w = MiniGenerator::logicalW; h = MiniGenerator::logicalH; title = fr ("Générateur"); break;
        case 1:  panel = std::make_unique<MiniExport> (proc);    w = MiniExport::logicalW;    h = MiniExport::logicalH;    title = fr ("Export vers FL"); break;
        default: panel = std::make_unique<MiniPads> (proc);      w = MiniPads::logicalW;      h = MiniPads::logicalH;      title = fr ("Pads du slicer"); break;
    }
    removeFocus (*panel);
    minis[(size_t) kind] = std::make_unique<MiniWindow> (title, std::move (panel), w, h, uiScale, lnf,
        [this] (const juce::KeyPress& k) { return keyPressed (k); },
        [this] { keyStateChanged (false); },
        [safe = juce::Component::SafePointer<VariantEditor> (this), kind]
        { juce::MessageManager::callAsync ([safe, kind] { if (safe != nullptr) safe->minis[(size_t) kind].reset(); }); });
}

void VariantEditor::sayQuote()
{
    if (! buddyOn) return;
    const auto who = fr (buddySkins()[(size_t) buddy.getSkin()].name);
    buddySay (who, buddy.randomQuote());
    muteTicks = 75;
}

void VariantEditor::openPanel (detach::Maker make, const juce::String& title, int w, int h)
{
    auto panel = make();
    if (panel == nullptr) return;
    removeFocus (*panel);
    auto* raw = new MiniWindow (title, std::move (panel), w, h, uiScale, lnf,
        [this] (const juce::KeyPress& k) { return keyPressed (k); },
        [this] { keyStateChanged (false); },
        {});
    panels.emplace_back (raw);
    juce::Component::SafePointer<VariantEditor> safe (this);
    raw->setOnClosed ([safe, raw]
    {
        juce::MessageManager::callAsync ([safe, raw]
        {
            if (safe == nullptr) return;
            auto& v = safe->panels;
            v.erase (std::remove_if (v.begin(), v.end(), [raw] (const std::unique_ptr<MiniWindow>& p) { return p.get() == raw; }), v.end());
        });
    });
}

//==============================================================================
// Barre d'aide façon FL Studio : ce qu'il y a sous la souris (+ la valeur d'un bouton)
void VariantEditor::updateHint()
{
    auto* c = juce::Desktop::getInstance().getMainMouseSource().getComponentUnderMouse();
    juce::String txt;
    for (auto* p = c; p != nullptr && txt.isEmpty(); p = p->getParentComponent())
    {
        if (auto* s = dynamic_cast<juce::Slider*> (p))
        {
            const auto value = s->getTextFromValue (s->getValue());
            const auto tip = s->getTooltip().upToFirstOccurrenceOf ("\n", false, false);
            txt = value + (tip.isNotEmpty() ? juce::String::fromUTF8 ("   ·   ") + tip : juce::String());
        }
        else if (auto* tc = dynamic_cast<juce::TooltipClient*> (p))
            txt = tc->getTooltip().replace ("\n", juce::String::fromUTF8 ("   ·   "));
    }
    if (txt != hintText) { hintText = txt; root.repaint (hintArea); }
}

// Décors des thèmes Jungle et Désert (dans l'en-tête et sur les bords, derrière le contenu)
void VariantEditor::paintThemeScenery (juce::Graphics& g)
{
    if (Palette::decor != 5 && Palette::decor != 6) return;
    const auto rb = root.getLocalBounds().toFloat();
    const float t = bgTime;
    const float hb = (float) headerArea.getBottom() + 30.0f;
    juce::Random rr (Palette::decor * 31);

    auto leaf = [&] (juce::Point<float> at, float len, float angle, juce::Colour c)
    {
        juce::Path p;
        p.startNewSubPath (0.0f, 0.0f);
        p.quadraticTo (len * 0.5f, -len * 0.32f, len, 0.0f);
        p.quadraticTo (len * 0.5f, len * 0.32f, 0.0f, 0.0f);
        p.closeSubPath();
        const auto tr = juce::AffineTransform::rotation (angle).translated (at);
        g.setColour (c); g.fillPath (p, tr);
        juce::Path vein; vein.startNewSubPath (0.0f, 0.0f); vein.lineTo (len * 0.92f, 0.0f);
        g.setColour (c.darker (0.5f).withAlpha (c.getFloatAlpha())); g.strokePath (vein, juce::PathStrokeType (1.0f), tr);
    };

    if (Palette::decor == 5)   // Jungle : lianes qui pendent, grandes feuilles dans les coins, lucioles
    {
        const juce::Colour g1 (0xff2f8a43), g2 (0xff4fb35a), g3 (0xff1d5e2e);
        for (int v = 0; v < 9; ++v)
        {
            const float x0 = rb.getWidth() * (0.04f + 0.115f * v) + rr.nextFloat() * 30.0f;
            const float len = hb * (0.6f + rr.nextFloat() * 0.8f);
            const float sway = std::sin (t * 0.7f + v) * 5.0f;
            juce::Path vine;
            vine.startNewSubPath (x0, -2.0f);
            vine.quadraticTo (x0 + sway * 2.0f, len * 0.5f, x0 + sway, len);
            g.setColour (g3.withAlpha (0.8f));
            g.strokePath (vine, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            for (int k = 1; k <= 4; ++k)
            {
                const float u = k / 4.6f;
                const juce::Point<float> at (x0 + sway * u * 1.4f, len * u);
                leaf (at, 13.0f + rr.nextFloat() * 6.0f, (k % 2 ? 0.5f : 2.6f) + std::sin (t + k + v) * 0.12f, (k % 2 ? g2 : g1).withAlpha (0.75f));
            }
        }
        // grandes feuilles qui débordent des coins bas
        for (int side = 0; side < 2; ++side)
        {
            const juce::Point<float> base (side == 0 ? -10.0f : rb.getWidth() + 10.0f, rb.getHeight() + 8.0f);
            for (int k = 0; k < 6; ++k)
            {
                const float ang = (side == 0 ? -0.35f : juce::MathConstants<float>::pi + 0.35f) + (side == 0 ? -1.0f : 1.0f) * (0.18f * k) + std::sin (t * 0.5f + k) * 0.04f;
                leaf (base, 90.0f + 14.0f * (k % 3), ang, (k % 2 ? g1 : g2).withAlpha (0.42f));
            }
        }
        // lucioles
        for (int i = 0; i < 26; ++i)
        {
            const float x = rr.nextFloat() * rb.getWidth() + 18.0f * std::sin (t * 0.6f + i);
            const float y = rr.nextFloat() * rb.getHeight() + 12.0f * std::cos (t * 0.5f + i * 1.3f);
            const float a = 0.5f + 0.5f * std::sin (t * 2.2f + i * 2.1f);
            g.setColour (juce::Colour (0xffe8ff7a).withAlpha (0.10f * a)); g.fillEllipse (x - 6.0f, y - 6.0f, 12.0f, 12.0f);
            g.setColour (juce::Colour (0xfff4ffb0).withAlpha (0.55f * a)); g.fillEllipse (x - 1.5f, y - 1.5f, 3.0f, 3.0f);
        }
    }
    else   // Désert : soleil couchant, dunes, cactus, chaleur qui ondule
    {
        const juce::Point<float> sun (rb.getWidth() * 0.80f, hb * 0.55f);
        for (int k = 0; k < 12; ++k)   // rayons
        {
            const float a = t * 0.05f + k * juce::MathConstants<float>::twoPi / 12.0f;
            juce::Path ray; ray.addTriangle (0.0f, -3.0f, 0.0f, 3.0f, 90.0f, 0.0f);
            g.setColour (juce::Colour (0xffffc04a).withAlpha (0.10f));
            g.fillPath (ray, juce::AffineTransform::translation (34.0f, 0.0f).rotated (a).translated (sun));
        }
        juce::ColourGradient sg (juce::Colour (0xffffd36a).withAlpha (0.55f), sun.x, sun.y - 30.0f, juce::Colour (0xffe8664a).withAlpha (0.45f), sun.x, sun.y + 30.0f, false);
        g.setGradientFill (sg);
        g.fillEllipse (sun.x - 30.0f, sun.y - 30.0f, 60.0f, 60.0f);
        for (int k = 0; k < 3; ++k)   // bandes du soleil (style rétro)
        {
            g.setColour (Palette::bg.withAlpha (0.55f));
            g.fillRect (sun.x - 32.0f, sun.y + 6.0f + k * 8.0f, 64.0f, 2.0f + k);
        }
        // dunes dans le bas de l'en-tête
        const juce::Colour dc[] = { juce::Colour (0xffd9a05a), juce::Colour (0xffc07a3a), juce::Colour (0xff8a5228) };
        for (int layer = 0; layer < 3; ++layer)
        {
            juce::Path d;
            const float baseY = hb - 18.0f + layer * 8.0f;
            d.startNewSubPath (0.0f, hb + 6.0f);
            for (int i = 0; i <= 60; ++i)
            {
                const float x = rb.getWidth() * i / 60.0f;
                d.lineTo (x, baseY - 10.0f * std::sin (x * (0.006f + layer * 0.003f) + layer * 1.7f) - 4.0f * std::sin (x * 0.02f + layer));
            }
            d.lineTo (rb.getWidth(), hb + 6.0f);
            d.closeSubPath();
            g.setColour (dc[layer].withAlpha (0.30f + layer * 0.08f));
            g.fillPath (d);
        }
        // cactus (silhouettes) sur les dunes
        auto cactus = [&] (float x, float y, float s)
        {
            juce::Path c;
            c.addRoundedRectangle (x - 3.0f * s, y - 30.0f * s, 6.0f * s, 30.0f * s, 3.0f * s);
            c.addRoundedRectangle (x - 12.0f * s, y - 20.0f * s, 4.5f * s, 11.0f * s, 2.2f * s);
            c.addRoundedRectangle (x - 12.0f * s, y - 11.0f * s, 10.0f * s, 4.0f * s, 2.0f * s);
            c.addRoundedRectangle (x + 7.5f * s, y - 25.0f * s, 4.5f * s, 12.0f * s, 2.2f * s);
            c.addRoundedRectangle (x + 2.0f * s, y - 15.0f * s, 10.0f * s, 4.0f * s, 2.0f * s);
            g.setColour (juce::Colour (0xff4f7a3a).withAlpha (0.8f));
            g.fillPath (c);
        };
        cactus (rb.getWidth() * 0.12f, hb - 6.0f, 0.9f);
        cactus (rb.getWidth() * 0.55f, hb - 2.0f, 0.6f);
        cactus (rb.getWidth() * 0.93f, hb - 4.0f, 1.0f);
        // chaleur qui ondule au-dessus des dunes
        for (int k = 0; k < 4; ++k)
        {
            juce::Path w;
            const float y0 = hb - 30.0f - k * 6.0f;
            for (int i = 0; i <= 80; ++i)
            {
                const float x = rb.getWidth() * i / 80.0f;
                const float y = y0 + 1.6f * std::sin (x * 0.05f + t * 2.0f + k);
                if (i == 0) w.startNewSubPath (x, y); else w.lineTo (x, y);
            }
            g.setColour (juce::Colour (0xffffe0a0).withAlpha (0.05f));
            g.strokePath (w, juce::PathStrokeType (1.0f));
        }
    }
}

// flèches autour de « Banque de sons » : son précédent / suivant, le nom s'affiche un instant sur le bouton
void VariantEditor::stepSound (int dir)
{
    if (! proc.stepLayerSound (proc.selectedLayer, dir)) return;
    soundPage->soundChanged();
    const auto f = proc.getLayerFile (proc.selectedLayer);
    bankButton.setButtonText (f.getFileNameWithoutExtension());
    const int ticket = ++bankLabelTicket;
    juce::Component::SafePointer<VariantEditor> self (this);
    juce::ignoreUnused (ticket, self);   // le nom reste affiché (mis à jour par le timer de l'éditeur)
}

//==============================================================================
// Effet seul (Variant Reverb, Variant Delay…) : la carte de l'effet comme dans la chaîne de Variant, rien d'autre
class SingleFxEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SingleFxEditor (VariantProcessor& p) : juce::AudioProcessorEditor (p), proc (p)
    {
        if (telemetry::devEnv ("VARIANT_SNAPSHOT", {}).isNotEmpty())   // outil de dev : image de la fenêtre
            juce::Timer::callAfterDelay (1500, [safe = juce::Component::SafePointer<SingleFxEditor> (this)]
            {
                if (safe == nullptr) return;
                const juce::File dir (telemetry::devEnv ("VARIANT_SNAPSHOT", {}));
                dir.createDirectory();
                auto img = safe->createComponentSnapshot (safe->getLocalBounds(), true, 1.0f);
                juce::FileOutputStream os (dir.getChildFile ("fx.png"));
                if (os.openedOk()) { os.setPosition (0); os.truncate(); juce::PNGImageFormat().writeImageToStream (img, os); }
            });
        auto& st = library::settings();
        i18n::current().store (st.getIntValue ("language", i18n::systemDefault()));
        if (st.containsKey ("theme")) proc.themeIndex = st.getIntValue ("theme", proc.themeIndex);
        themes::apply (proc.themeIndex);
        setLookAndFeel (&lnf);
        buildCard();
        addMouseListener (&automationClicks, true);   // clic droit sur un potard = automation dans FL
        startTimerHz (30);
    }
    ~SingleFxEditor() override
    {
        stopTimer();
        removeMouseListener (&automationClicks);
        card.reset();
        setLookAndFeel (nullptr);
    }

    void paint (juce::Graphics& g) override { g.fillAll (Palette::bg); }
    void resized() override { if (card) card->setBounds (getLocalBounds().reduced (8)); }

private:
    void buildCard()
    {
        card = std::make_unique<SlotCard> (proc, 0);
        card->setSingleMode();
        juce::Component::SafePointer<SingleFxEditor> safe (this);
        card->onChanged = [safe] { juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->buildCard(); }); };
        addAndMakeVisible (*card);
        shownType = proc.getSlotType (0);
        setSize (900, SlotCard::heightFor (shownType) + 16);
        resized();
    }
    void timerCallback() override
    {
        if (proc.getSlotType (0) != shownType) { buildCard(); return; }
        if (card) card->animate (1.0f / 30.0f);
    }

    VariantProcessor& proc;
    juce::SharedResourcePointer<GlobalLookAndFeel> globalLnf;
    VariantLookAndFeel lnf;
    hostmenu::Listener automationClicks;
    std::unique_ptr<SlotCard> card;
    int shownType = -1;
};

juce::AudioProcessorEditor* createSingleFxEditor (VariantProcessor& p) { return new SingleFxEditor (p); }
