#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Molette de la souris dans FL Studio (voir WheelHook.cpp)
namespace wheelhook
{
    // point à l'écran (pixels physiques) et quantité de molette (comme JUCE : deltaY)
    using Handler = std::function<void (juce::Point<int>, float)>;
    void add (void* nativeWindow, Handler fn);
    void remove (void* nativeWindow);
}
