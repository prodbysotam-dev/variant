#pragma once

// Traductions ajoutées en v3.28 : tout ce qui manquait, dans les 8 langues (avec le russe),
// et le russe des premières traductions (Translations.h).
// Colonnes : anglais, espagnol, allemand, italien, portugais, japonais, chinois, russe.
namespace i18n
{
    inline const std::unordered_map<std::string, Row>& moreTable()
    {
        static const std::unordered_map<std::string, Row> t {
#include "tr/more.inc"
        };
        return t;
    }

    inline const std::unordered_map<std::string, const char*>& ruTable()
    {
        static const std::unordered_map<std::string, const char*> t {
#include "tr/ru.inc"
        };
        return t;
    }
}
