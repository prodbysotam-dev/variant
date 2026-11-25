// Molette de la souris dans FL Studio : FL ne transmet pas toujours la molette à la fenêtre du plugin
// (elle n'arrivait que sur certaines zones). Comme JUCE le fait pour le clavier, on attrape le message
// « molette » sur le fil de l'interface, et s'il tombe sur une fenêtre de Variant, on le donne à Variant.
#include "WheelHook.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <vector>

namespace wheelhook
{
    namespace
    {
        struct Entry { void* hwnd; Handler fn; };
        std::vector<Entry>* entries = nullptr;
        HHOOK hook = nullptr;

        LRESULT CALLBACK proc (int code, WPARAM wParam, LPARAM lParam)
        {
            auto& msg = *reinterpret_cast<MSG*> (lParam);
            if (code == HC_ACTION && wParam == PM_REMOVE && msg.message == WM_MOUSEWHEEL && entries != nullptr)
            {
                const HWND under = WindowFromPoint (msg.pt);
                for (auto& e : *entries)
                {
                    const auto h = (HWND) e.hwnd;
                    if (h != nullptr && (under == h || IsChild (h, under)))
                    {
                        const float amount = juce::jlimit (-1000.0f, 1000.0f, 0.5f * (float) (short) HIWORD (msg.wParam));   // comme JUCE
                        const auto fn = e.fn;
                        const POINT pt = msg.pt;
                        msg = {};
                        msg.message = WM_USER;   // message neutralisé : pas traité deux fois
                        if (fn) fn ({ (int) pt.x, (int) pt.y }, amount / 256.0f);
                        return 0;
                    }
                }
            }
            return CallNextHookEx (hook, code, wParam, lParam);
        }
    }

    void add (void* nativeWindow, Handler fn)
    {
        if (nativeWindow == nullptr) return;
        if (entries == nullptr) entries = new std::vector<Entry>();
        remove (nativeWindow);
        if (entries == nullptr) entries = new std::vector<Entry>();
        entries->push_back ({ nativeWindow, std::move (fn) });
        if (hook == nullptr)
            hook = SetWindowsHookExW (WH_GETMESSAGE, proc, (HINSTANCE) juce::Process::getCurrentModuleInstanceHandle(), GetCurrentThreadId());
    }

    void remove (void* nativeWindow)
    {
        if (entries == nullptr) return;
        for (auto it = entries->begin(); it != entries->end();)
            it = it->hwnd == nativeWindow ? entries->erase (it) : it + 1;
        if (entries->empty())
        {
            if (hook != nullptr) { UnhookWindowsHookEx (hook); hook = nullptr; }   // plus aucune fenêtre : on enlève le crochet (FL peut décharger Variant)
            delete entries;
            entries = nullptr;
        }
    }
}
#else
namespace wheelhook
{
    void add (void*, Handler) {}
    void remove (void*) {}
}
#endif
