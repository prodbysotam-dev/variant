#pragma once

#include <juce_core/juce_core.h>
#include <unordered_map>
#include <string>
#include <array>
#include <atomic>
#include <set>
#include <mutex>

// Traductions : chaque texte de l'interface est écrit en français dans le code,
// puis traduit à l'affichage selon la langue choisie.
namespace i18n
{
    enum Lang { FR = 0, EN, ES, DE, IT, PT, JA, ZH, RU, NumLangs };

    inline std::atomic<int>& current() { static std::atomic<int> l { FR }; return l; }

    inline juce::StringArray languageNames()
    {
        return { juce::String::fromUTF8 ("Français"), "English", juce::String::fromUTF8 ("Español"), "Deutsch", "Italiano",
                 juce::String::fromUTF8 ("Português"), juce::String::fromUTF8 ("日本語"), juce::String::fromUTF8 ("中文"), juce::String::fromUTF8 ("Русский") };
    }

    // Langue du système (au premier lancement)
    inline int systemDefault()
    {
        const auto code = juce::SystemStats::getUserLanguage().toLowerCase().substring (0, 2);
        if (code == "fr") return FR;
        if (code == "es") return ES;
        if (code == "de") return DE;
        if (code == "it") return IT;
        if (code == "pt") return PT;
        if (code == "ja") return JA;
        if (code == "zh") return ZH;
        if (code == "ru") return RU;
        return EN;
    }

    using Row = std::array<const char*, NumLangs - 1>;   // EN, ES, DE, IT, PT, JA, ZH, RU
    inline const std::unordered_map<std::string, Row>& table();       // défini dans Translations.h (premières traductions)
    inline const std::unordered_map<std::string, Row>& moreTable();   // défini dans Translations2.h (le reste, avec le russe)
    inline const std::unordered_map<std::string, const char*>& ruTable();   // russe des premières traductions

    // outil de dev : liste des textes affichés sans traduction
    inline std::atomic<bool>& recording() { static std::atomic<bool> r { false }; return r; }
    inline std::mutex& missLock() { static std::mutex m; return m; }
    inline std::set<std::string>& missed() { static std::set<std::string> s; return s; }

    inline juce::String tr (const char* utf8)
    {
        const int l = current().load();
        if (l == FR || utf8 == nullptr || *utf8 == 0) return juce::String::fromUTF8 (utf8);
        const char* found = nullptr;
        const char* english = nullptr;
        const auto& m = moreTable();
        if (const auto it = m.find (utf8); it != m.end())
        {
            found = it->second[(size_t) (l - 1)];
            english = it->second[0];
        }
        if (found == nullptr || *found == 0)
        {
            const auto& t = table();
            if (const auto it = t.find (utf8); it != t.end())
            {
                if (l == RU) { const auto& r = ruTable(); if (const auto rt = r.find (utf8); rt != r.end()) found = rt->second; }
                else found = it->second[(size_t) (l - 1)];
                if (english == nullptr || *english == 0) english = it->second[0];
            }
        }
        if (found != nullptr && *found != 0) return juce::String::fromUTF8 (found);
        if (recording().load())
        {
            // un texte avec des lettres, sans traduction dans cette langue
            const auto s = juce::String::fromUTF8 (utf8);
            if (s.containsAnyOf ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"))
            {
                const std::lock_guard<std::mutex> g (missLock());
                missed().insert (utf8);
            }
        }
        if (english != nullptr && *english != 0) return juce::String::fromUTF8 (english);   // repli sur l'anglais
        return juce::String::fromUTF8 (utf8);
    }

    // version anglaise d'un texte (Kick parle anglais dans toutes les langues sauf le français)
    inline juce::String english (const char* utf8)
    {
        if (utf8 == nullptr || *utf8 == 0) return {};
        const auto& m = moreTable();
        if (const auto it = m.find (utf8); it != m.end() && it->second[0] != nullptr && *it->second[0] != 0) return juce::String::fromUTF8 (it->second[0]);
        const auto& t = table();
        if (const auto it = t.find (utf8); it != t.end() && it->second[0] != nullptr && *it->second[0] != 0) return juce::String::fromUTF8 (it->second[0]);
        return juce::String::fromUTF8 (utf8);
    }
    // langue de Kick : le français en français, l'anglais partout ailleurs
    inline juce::String kickLang (const char* utf8) { return current().load() == FR ? juce::String::fromUTF8 (utf8) : english (utf8); }
}

inline juce::String fr (const char* utf8)          { return i18n::tr (utf8); }
inline juce::String fr (const juce::String& s)     { return i18n::tr (s.toRawUTF8()); }

#include "Translations.h"
#include "Translations2.h"
