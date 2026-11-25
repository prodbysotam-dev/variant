#pragma once
// Cerveau de Kick : comprend une question (français, anglais, langage SMS, fautes de frappe)
// et trouve la bonne fiche dans sa base (KickKnowledge.h + KickFxKnowledge.h). Aucune connexion.
#include <juce_core/juce_core.h>
#include <vector>
#include <map>
#include <set>
#include <unordered_map>
#include <cmath>
#include <cstring>
#include <regex>
#include "KickKnowledge.h"
#include "KickFxKnowledge.h"

namespace kick
{
    struct Faq { juce::String names, keys, answer; int tab = -1; bool definition = false; bool generic = false; juce::String answerEn; };

    // version anglaise de la base (Kick parle anglais quand le plugin n'est pas en français)
    struct EnRow { const char* a; const char* b; const char* c; };
    inline const std::unordered_map<std::string, EnRow>& enTable()
    {
        static const std::unordered_map<std::string, EnRow> t {
            #include "tr/kick_en.inc"
        };
        return t;
    }
    inline const EnRow* enRow (const std::string& key) { const auto& t = enTable(); const auto it = t.find (key); return it == t.end() ? nullptr : &it->second; }
    // réponse dans la langue de Kick
    inline juce::String answerOf (const Faq& f, bool french) { return french || f.answerEn.isEmpty() ? f.answer : f.answerEn; }

    //==========================================================================
    // texte -> mots
    inline juce::String normalise (juce::String s)
    {
        s = s.toLowerCase();
        static const char* from[] = { "à", "â", "ä", "á", "é", "è", "ê", "ë", "î", "ï", "í", "ô", "ö", "ó", "ù", "û", "ü", "ú", "ç", "œ", "æ", "ÿ", "ñ",
                                      "’", "'", "`", "&", "→", "↔", "«", "»", "…", "É", "È", "À", "Ç" };
        static const char* to[]   = { "a", "a", "a", "a", "e", "e", "e", "e", "i", "i", "i", "o", "o", "o", "u", "u", "u", "u", "c", "oe", "ae", "y", "n",
                                      " ", " ", " ", " et ", " ", " ", " ", " ", " ", "e", "e", "a", "c" };
        for (int i = 0; i < (int) (sizeof (from) / sizeof (from[0])); ++i) s = s.replace (juce::String::fromUTF8 (from[i]), to[i]);
        juce::String out;
        out.preallocateBytes ((size_t) s.length() + 2);
        for (auto c : s) out << ((c < 128 && juce::CharacterFunctions::isLetterOrDigit (c)) ? juce::String::charToString (c) : juce::String (" "));
        while (out.contains ("  ")) out = out.replace ("  ", " ");
        return out;
    }

    // langage SMS / argot / anglais -> mot de référence (avant les mots vides)
    inline juce::String slang (const juce::String& w)
    {
        static const std::map<juce::String, juce::String> m {
            { "koi", "quoi" }, { "kwa", "quoi" }, { "quoa", "quoi" }, { "koa", "quoi" }, { "cmt", "comment" }, { "cmnt", "comment" }, { "coment", "comment" },
            { "comen", "comment" }, { "commant", "comment" }, { "pk", "pourquoi" }, { "pq", "pourquoi" }, { "pkoi", "pourquoi" }, { "pourkoi", "pourquoi" },
            { "sa", "ca" }, { "ske", "ce" }, { "kel", "quel" }, { "kelle", "quelle" }, { "wsh", "salut" }, { "wesh", "salut" }, { "wech", "salut" },
            { "slt", "salut" }, { "bjr", "salut" }, { "bonjour", "salut" }, { "bonsoir", "salut" }, { "cc", "salut" }, { "coucou", "salut" }, { "yo", "salut" },
            { "hey", "salut" }, { "hello", "salut" }, { "hi", "salut" }, { "mrc", "merci" }, { "mrci", "merci" }, { "thx", "merci" }, { "thanks", "merci" },
            { "ty", "merci" }, { "tt", "tout" }, { "ttes", "toutes" }, { "tjr", "toujours" }, { "tjrs", "toujours" }, { "qd", "quand" }, { "kan", "quand" },
            { "pr", "pour" }, { "ds", "dans" }, { "bcp", "beaucoup" }, { "mm", "meme" }, { "oskour", "aide" }, { "help", "aide" }, { "helpe", "aide" },
            // anglais
            { "attack", "attaque" }, { "release", "relache" }, { "chord", "accord" }, { "chords", "accord" }, { "slice", "slicer" }, { "slices", "slicer" },
            { "load", "charger" }, { "drop", "charger" }, { "import", "importer" }, { "knob", "potard" }, { "knobs", "potard" }, { "button", "bouton" },
            { "sound", "son" }, { "sounds", "son" }, { "effect", "effet" }, { "effects", "effet" }, { "fx", "effet" }, { "scale", "gamme" }, { "key", "tonalite" },
            { "language", "langue" }, { "width", "largeur" }, { "speed", "vitesse" }, { "rate", "vitesse" },
            { "depth", "profondeur" }, { "feedback", "reinjection" }, { "reverse", "inverser" }, { "undo", "annuler" }, { "loop", "boucle" }, { "loops", "boucle" },
            { "record", "enregistrer" }, { "rec", "enregistrer" }, { "recording", "enregistrer" }, { "keyboard", "clavier" }, { "save", "sauver" },
            { "random", "hasard" }, { "aleatoire", "hasard" }, { "randomize", "hasard" }, { "settings", "reglages" }, { "size", "taille" }, { "skins", "skin" },
            { "filter", "filtre" }, { "noise", "bruit" }, { "distortion", "distorsion" },
            { "arpeggio", "arpege" }, { "arpeggiator", "arpege" }, { "arpegiator", "arpege" }, { "arpegiateur", "arpege" }, { "arpeggiateur", "arpege" },
            { "arpegio", "arpege" }, { "arp", "arpege" }, { "arpeges", "arpege" }, { "arpegiateurs", "arpege" }, { "reverbe", "reverb" },
            { "reverberation", "reverb" }, { "rvb", "reverb" }, { "echo", "delay" }, { "echos", "delay" }, { "synth", "synthe" },
            { "synthetiseur", "synthe" }, { "synthes", "synthe" }, { "oneshots", "oneshot" }, { "bpms", "bpm" }, { "tempos", "tempo" },
            { "lfos", "lfo" }, { "eq", "egaliseur" }, { "equaliseur", "egaliseur" }, { "equalizer", "egaliseur" }, { "egaliseurs", "egaliseur" },
            { "tranches", "tranche" }, { "chops", "chop" }, { "velocity", "velocite" }, { "sustain", "maintien" }, { "decay", "declin" },
            { "envelope", "enveloppe" }, { "glide", "glide" }, { "pitch", "pitch" }, { "stereo", "stereo" }, { "presets", "preset" },
        };
        auto it = m.find (w);
        return it != m.end() ? it->second : w;
    }

    // synonymes -> mot de référence, puis racine (charger / chargé / charge -> charg)
    inline juce::String canon (const juce::String& word)
    {
        static const std::map<juce::String, juce::String> syn {
            { "importer", "charger" }, { "ajouter", "charger" }, { "ajoute", "charger" }, { "inserer", "charger" }, { "deposer", "charger" }, { "ouvrir", "charger" },
            { "accords", "accord" }, { "boucles", "boucle" }, { "effets", "effet" },
            { "exporter", "export" }, { "exportation", "export" }, { "exporte", "export" },
            { "sample", "son" }, { "samples", "son" }, { "sons", "son" }, { "sonore", "son" },
            { "bpm", "tempo" }, { "gamme", "tonalite" }, { "tonalites", "tonalite" }, { "gammes", "tonalite" },
            { "decouper", "slicer" }, { "decoupe", "slicer" }, { "tranche", "slicer" }, { "slicer", "slicer" },
            { "enregistrer", "enregistrement" }, { "capturer", "enregistrement" },
            { "ralentir", "halftime" }, { "ralenti", "halftime" }, { "grossbeat", "gross" },
            { "bonhomme", "kick" }, { "personnage", "kick" }, { "mascotte", "kick" },
            { "probleme", "bug" }, { "souci", "bug" }, { "plante", "bug" }, { "crash", "bug" }, { "beug", "bug" }, { "bugs", "bug" },
            { "notes", "note" }, { "potards", "potard" }, { "boutons", "bouton" },
            { "basses", "basse" }, { "graves", "grave" }, { "aigus", "aigu" }, { "aigue", "aigu" }, { "aigues", "aigu" },
            { "variante", "variantx" }, { "variantes", "variantx" },
        };
        auto it = syn.find (word);
        juce::String w = it != syn.end() ? it->second : word;
        static const char* endings[] = { "ements", "ement", "ations", "ation", "ions", "ent", "ez", "er", "ee", "es", "e", "s" };
        for (auto* e : endings)
            if (w.length() - (int) std::strlen (e) >= 4 && w.endsWith (e)) { w = w.dropLastCharacters ((int) std::strlen (e)); break; }
        return w;
    }

    inline const std::set<juce::String>& stopWords()
    {
        static const std::set<juce::String> stop {
            "le", "la", "les", "de", "du", "des", "un", "une", "mon", "ma", "mes", "ta", "tes", "sa", "ses", "leur", "leurs", "notre", "votre",
            "c", "ce", "cet", "cette", "ces", "ceci", "cela", "ca", "est", "sont", "suis", "es", "etre", "ete", "quoi", "comment", "pourquoi", "quand",
            "quel", "quelle", "quels", "quelles", "lequel", "laquelle", "ou", "qui", "que", "qu", "dont", "si", "faire", "fais", "fait", "font", "faut",
            "je", "j", "tu", "t", "il", "elle", "on", "nous", "vous", "ils", "elles", "me", "m", "moi", "toi", "lui", "se", "s", "y", "a", "l", "d", "n", "ne",
            "veux", "veut", "voudrais", "voulais", "aimerais", "peux", "peut", "pouvoir", "pourrais", "pourrait", "sais", "sait", "savoir", "sert", "sers",
            "servent", "servir", "utiliser", "utilise", "utilises", "fonctionne", "fonctionner", "marche", "regler", "regle", "reglez", "changer",
            "change", "modifier", "modifie", "mettre", "met", "mets", "avoir", "ai", "as", "avec", "dans", "sur", "sous", "en", "et", "au", "aux", "pour",
            "par", "plus", "moins", "pas", "point", "comme", "tres", "bien", "aussi", "alors", "donc", "mais", "car", "the", "an", "how",
            "do", "does", "to", "my", "is", "what", "can", "i", "of", "in", "it", "this", "that", "where", "why", "explique", "expliquer", "expliques",
            "with", "for", "and", "from", "combien", "question", "questions", "exactement", "demander", "demande", "precisement", "bref", "svp",
            "dis", "dire", "montre", "montrer", "stp", "sil", "plait", "please", "pls", "plz", "frr", "frere", "frero", "bro", "mec",
            "khey", "kho", "poto", "wallah", "jsp", "jpp", "ptn", "putain", "merde", "zebi", "genre", "truc", "machin", "chose", "ya", "yen",
            "jveux", "jveu", "chui", "chuis", "jsuis", "cest", "cques", "ske", "beaucoup", "tout", "toutes", "tous", "meme", "toujours", "jamais",
            "hein", "ok", "okay", "bon", "ben", "bah", "euh", "hmm", "voila", "ici", "deja", "encore", "juste", "vraiment",
            "trouve", "trouver", "comprends", "comprend", "comprendre", "capte", "connais", "va", "vas", "allez", "aller",
            "petit", "petite", "grand", "nouveau", "chez", "vers", "entre", "apres", "avant", "work", "works", "use", "does", "are", "you", "your",
            "which", "when", "there", "make", "get", "want", "would", "like", "we", "be", "on", "at", "by", "or", "so", "about", "did", "have", "has", "need", "should", "could", "a", "am", "was", "will", "them", "its", "into", "just", "some", "any", "tell", "explain", "show", "know",
        };
        return stop;
    }

    // mots « faibles » : comptent peu dès que la question a d'autres mots
    inline const std::set<juce::String>& weakWords()
    {
        static const std::set<juce::String> w { "salut", "merci", "kick", "aide", "plugin", "variant", "potard", "bouton", "reglag", "parametr", "option",
                                                "effet", "fonction", "outil", "page", "onglet", "menu", "note", "fl", "son" };
        return w;
    }
    // politesse : compte peu dès qu'il y a n'importe quel autre mot
    inline bool isSocial (const juce::String& w) { return w == "salut" || w == "merci" || w == "kick" || w == "aide"; }

    // la question demande une définition ("c'est quoi", "ça veut dire quoi"…)
    inline bool asksDefinition (const juce::String& q)
    {
        const auto nq = " " + normalise (q) + " ";
        for (auto* cue : { " c est quoi ", " c quoi ", " cest quoi ", " c koi ", " cest koi ", " c est koi ", " qu est ce ", " definition ",
                           " what is ", " what s ", " veut dire ", " signifie ", " kesako ", " ces quoi ", " c est quoi " })
            if (nq.contains (cue)) return true;
        return false;
    }

    inline int editDistance (const juce::String& a, const juce::String& b, int limit);

    struct Tok { juce::String surf, can; };
    inline bool knownSurface (const juce::String& s);

    // querySide : c'est la question de l'utilisateur (pas la base) -> on corrige aussi l'anglais mal tapé
    inline std::vector<Tok> tokens (const juce::String& q, bool querySide = false)
    {
        auto nq = " " + normalise (q.replace ("@", " arobase ")) + " ";
        // expressions courantes -> mots-clés (avant les mots vides)
        static const char* phrases[][2] = {
            { " pas de son ", " silence " }, { " aucun son ", " silence " }, { " entends rien ", " silence " }, { " entend rien ", " silence " },
            { " ne marche pas ", " bug " }, { " marche pas ", " bug " }, { " fonctionne pas ", " bug " },
            { " a quoi sert ", " " }, { " a quoi servent ", " " }, { " c est quoi ", " " }, { " ca sert a quoi ", " " }, { " sert a quoi ", " " },
            { " one shot ", " oneshot " }, { " one shots ", " oneshot " }, { " piano roll ", " pianoroll " }, { " fl studio ", " fl " },
            { " ping pong ", " pingpong " }, { " lo fi ", " lofi " }, { " half time ", " halftime " }, { " gross beat ", " gross " },
            { " hi hat ", " hihat " }, { " tape stop ", " tapestop " }, { " pitch bend ", " pitchbend " }, { " round robin ", " roundrobin " },
            { " through zero ", " throughzero " }, { " dry wet ", " mix " }, { " sound font ", " soundfont " },
            { " auto pan ", " autopan " }, { " pre delai ", " predelai " }, { " pre delay ", " predelai " }, { " coupe bas ", " coupebas " },
            { " low cut ", " coupebas " }, { " temps et volume ", " gross " }, { " wave table ", " wavetable " }, { " table d ondes ", " wavetable " },
            { " table d onde ", " wavetable " }, { " tables d ondes ", " wavetable " }, { " plus d options ", " plusdoptions " },
            { " tout au hasard ", " toutauhasard " }, { " key tracking ", " suiviclavier " }, { " suivi clavier ", " suiviclavier " },
            { " graves mono ", " gravesmono " }, { " mono bass ", " gravesmono " }, { " basses mono ", " gravesmono " },
            { " t es qui ", " kick " }, { " qui es tu ", " kick " }, { " tu es qui ", " kick " }, { " t es qui toi ", " kick " }, { " ca va ", " cava " },
            { " ca sature ", " satureprobleme " }, { " ca satur ", " satureprobleme " }, { " mon son sature ", " satureprobleme " }, { " le son sature ", " satureprobleme " },
            { " ca clip ", " satureprobleme " }, { " ca gresille ", " satureprobleme " }, { " son sature ", " satureprobleme " },
            { " deja utilise ", " dejautilise " }, { " deja pris ", " dejautilise " }, { " in english ", " anglais " },
        };
        for (auto& p : phrases) nq = nq.replace (p[0], p[1]);
        juce::StringArray raw;
        raw.addTokens (nq, " ", "");
        raw.removeEmptyStrings();
        std::vector<Tok> out;
        for (auto& x0 : raw)
        {
            auto x = slang (x0);
            if (querySide && x == x0 && x0.length() >= 5 && stopWords().count (x0) == 0 && ! knownSurface (x0))
            {
                // anglais / SMS mal tapé ("atatck", "revese") : une lettre près, seulement si le mot n'existe pas tel quel
                static const std::vector<std::pair<juce::String, juce::String>> longSlang = []
                {
                    std::vector<std::pair<juce::String, juce::String>> v;
                    for (auto* w : { "attack", "release", "chords", "slices", "sounds", "effects", "language", "settings", "keyboard", "arpeggio",
                                     "arpeggiator", "reverberation", "synthetiseur", "recording", "randomize", "distortion", "equalizer", "feedback",
                                     "reverse", "velocity", "sustain", "envelope", "presets", "aleatoire" })
                        v.push_back ({ w, slang (w) });
                    return v;
                }();
                for (auto& [k, val] : longSlang)
                    if (std::abs (k.length() - x0.length()) <= 1 && editDistance (x0, k, 1) <= 1) { x = val; break; }
            }
            if (x.length() < 2 && ! x.containsOnly ("0123456789")) continue;
            if (stopWords().count (x) != 0) continue;
            const auto c = canon (x);
            bool dup = false;
            for (auto& t : out) if (t.can == c) dup = true;
            if (! dup) out.push_back ({ x, c });
        }
        return out;
    }

    inline juce::StringArray questionWords (const juce::String& q)
    {
        juce::StringArray out;
        for (auto& t : tokens (q)) out.add (t.can);
        return out;
    }

    //==========================================================================
    // fautes de frappe : distance d'édition (avec inversion de 2 lettres voisines)
    inline int editDistance (const juce::String& a, const juce::String& b, int limit)
    {
        const int n = a.length(), m = b.length();
        if (std::abs (n - m) > limit) return limit + 1;
        std::vector<int> pp ((size_t) m + 1), prev ((size_t) m + 1), cur ((size_t) m + 1);
        for (int j = 0; j <= m; ++j) prev[(size_t) j] = j;
        for (int i = 1; i <= n; ++i)
        {
            cur[0] = i;
            int rowMin = cur[0];
            for (int j = 1; j <= m; ++j)
            {
                const int cost = a[i - 1] == b[j - 1] ? 0 : 1;
                int v = juce::jmin (prev[(size_t) j] + 1, cur[(size_t) j - 1] + 1, prev[(size_t) j - 1] + cost);
                if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) v = juce::jmin (v, pp[(size_t) j - 2] + 1);
                cur[(size_t) j] = v;
                rowMin = juce::jmin (rowMin, v);
            }
            if (rowMin > limit) return limit + 1;
            std::swap (pp, prev);
            std::swap (prev, cur);
        }
        return prev[(size_t) m];
    }

    // à quel point un mot de la question ressemble à un mot de la base (0 = pas du tout, 1 = pareil)
    inline float wordMatch (const juce::String& w, const juce::String& k)
    {
        if (w == k) return 1.0f;
        const int lw = w.length(), lk = k.length();
        if (lk >= 4 && w.startsWith (k)) return 0.85f;
        if (lw >= 5 && k.startsWith (w) && lk - lw <= 2) return 0.8f;
        const int shortest = juce::jmin (lw, lk);
        if (shortest >= 4 && (w[0] == k[0] || shortest >= 6))
        {
            const int lim = shortest >= 8 ? 2 : 1;
            const int d = editDistance (w, k, lim);
            if (d <= lim) return shortest <= 4 ? 0.45f : (shortest == 5 ? 0.62f : (d == 1 ? 0.72f : 0.6f));
        }
        return 0.0f;
    }

    //==========================================================================
    // la base complète : fiches écrites + effets + potards d'effets + fiches générales des noms de potards
    inline const std::vector<Faq>& faq()
    {
        static const std::vector<Faq> all = []
        {
            std::vector<Faq> v;
            const auto U = [] (const char* s) { return juce::String::fromUTF8 (s); };
            // version anglaise (tr/kick_en.inc) : mots-clés anglais ajoutés, réponse anglaise à côté
            auto enFx = [&U] (const juce::String& fxName) { auto* r = enRow ("fx:" + fxName.toStdString()); return r != nullptr ? U (r->a) : fxName; };
            auto enKnob = [] (const FxKnob& k) { return enRow (std::string ("knob:") + k.fx + ">" + k.knob); };
            for (auto& e : knowledge())
            {
                Faq f { U (e.names), U (e.keys), U (e.answer), e.tab };
                if (auto* r = enRow (e.names)) { f.keys << " " << U (r->a); f.answerEn = U (r->b); }
                else f.answerEn = f.answer;
                v.push_back (f);
            }

            // noms déjà pris par une fiche : pas de fiche générale en double
            std::set<juce::String> taken;
            auto phraseKey = [] (const juce::String& s) { return questionWords (s).joinIntoString (" "); };
            for (auto& f : v) { juce::StringArray ns; ns.addTokens (f.names, "|", ""); for (auto& n : ns) taken.insert (phraseKey (n)); }

            for (auto& fx : fxInfos())
            {
                const auto name = juce::String::fromUTF8 (fx.name);
                juce::String knobs, knobsEn;
                for (auto& k : fxKnobs()) if (juce::String::fromUTF8 (k.fx) == name)
                {
                    knobs << (knobs.isEmpty() ? "" : ", ") << juce::String::fromUTF8 (k.knob);
                    auto* r = enKnob (k);
                    knobsEn << (knobsEn.isEmpty() ? "" : ", ") << (r != nullptr ? U (r->a) : U (k.knob));
                }
                juce::String ans = juce::String::fromUTF8 (fx.about);
                if (knobs.isNotEmpty()) ans << "\n" << juce::String::fromUTF8 ("Ses potards : ") << knobs << juce::String::fromUTF8 (". Il est dans l'onglet Effets (et dans les Effets rapides).");
                Faq f { name + "|effet " + name + "|" + juce::String::fromUTF8 (fx.aliases), juce::String::fromUTF8 (fx.keys) + " effet", ans, 1 };
                if (auto* r = enRow (std::string ("fx:") + fx.name))
                {
                    f.names << "|" << U (r->a) << "|" << U (r->a) << " effect";
                    f.keys << " " << U (r->b) << " effect";
                    f.answerEn = U (r->c);
                    if (knobsEn.isNotEmpty()) f.answerEn << "\nIts knobs: " << knobsEn << ". It's in the Effects tab (and in Quick effects).";
                }
                else f.answerEn = ans;
                v.push_back (f);
                taken.insert (phraseKey (name));
                juce::StringArray al; al.addTokens (juce::String::fromUTF8 (fx.aliases), "|", "");
                for (auto& a : al) taken.insert (phraseKey (a));
            }

            // potards d'effets, rangés par nom pour les fiches générales
            std::map<juce::String, std::vector<const FxKnob*>> byName;
            for (auto& k : fxKnobs()) byName[phraseKey (juce::String::fromUTF8 (k.knob))].push_back (&k);

            for (auto& k : fxKnobs())
            {
                const auto fxName = juce::String::fromUTF8 (k.fx), knob = juce::String::fromUTF8 (k.knob);
                juce::String fxAlt;
                for (auto& fx : fxInfos()) if (fxName == juce::String::fromUTF8 (fx.name)) fxAlt = juce::String::fromUTF8 (fx.aliases).upToFirstOccurrenceOf ("|", false, false);
                juce::String names = knob + " " + fxName + "|" + knob + " de l'effet " + fxName;
                if (fxAlt.isNotEmpty() && phraseKey (fxAlt) != phraseKey (fxName)) names << "|" << knob << " " << fxAlt;
                juce::StringArray al; al.addTokens (juce::String::fromUTF8 (k.aliases), "|", "");
                for (auto& a : al) names << "|" << a << " " << fxName;
                const auto key = phraseKey (knob);
                if (byName[key].size() == 1 && taken.count (key) == 0) names << "|" << knob;   // nom unique : il suffit
                Faq f { names, juce::String::fromUTF8 (k.aliases).replace ("|", " ") + " " + fxName,
                        juce::String::fromUTF8 ("« ") + knob + juce::String::fromUTF8 (" » de l'effet ") + fxName + " : " + juce::String::fromUTF8 (k.text), 1 };
                if (auto* r = enKnob (k))
                {
                    const auto fe = enFx (fxName), ke = U (r->a);
                    f.names << "|" << ke << " " << fe;
                    f.keys << " " << U (r->b) << " " << fe;
                    f.answerEn = juce::String::fromUTF8 ("« ") + ke + juce::String::fromUTF8 (" » in the ") + fe + " effect: " + U (r->c);
                }
                else f.answerEn = f.answer;
                v.push_back (f);
            }

            // fiche générale pour un nom de potard qu'on trouve dans plusieurs effets
            for (auto& [key, list] : byName)
            {
                if (list.size() < 2 || taken.count (key) != 0) continue;
                const auto knob = juce::String::fromUTF8 (list.front()->knob);
                juce::String ans = juce::String::fromUTF8 ("« ") + knob + juce::String::fromUTF8 (" » existe dans plusieurs effets :");
                juce::String others, othersEn;
                const auto* r0 = enKnob (*list.front());
                juce::String ansEn = juce::String::fromUTF8 ("« ") + (r0 != nullptr ? U (r0->a) : knob) + juce::String::fromUTF8 (" » exists in several effects:");
                for (size_t i = 0; i < list.size(); ++i)
                {
                    const auto* ri = enKnob (*list[i]);
                    if (i < 4)
                    {
                        ans << "\n" << juce::String::fromUTF8 ("• ") << juce::String::fromUTF8 (list[i]->fx) << " : " << juce::String::fromUTF8 (list[i]->text);
                        ansEn << "\n" << juce::String::fromUTF8 ("• ") << enFx (U (list[i]->fx)) << ": " << (ri != nullptr ? U (ri->c) : U (list[i]->text));
                    }
                    else
                    {
                        others << (others.isEmpty() ? "" : ", ") << juce::String::fromUTF8 (list[i]->fx);
                        othersEn << (othersEn.isEmpty() ? "" : ", ") << enFx (U (list[i]->fx));
                    }
                }
                if (others.isNotEmpty()) { ans << "\n" << "Et aussi : " << others << "."; ansEn << "\nAnd also: " << othersEn << "."; }
                // alias partagés par au moins 2 effets (ex. feedback) : ils mènent aussi ici
                std::map<juce::String, int> aliasCount;
                for (auto* k : list) { juce::StringArray al; al.addTokens (juce::String::fromUTF8 (k->aliases), "|", ""); for (auto& a : al) ++aliasCount[a]; }
                juce::String names = knob;
                for (auto& [a, c] : aliasCount) if (c >= 2 && taken.count (phraseKey (a)) == 0 && byName.count (phraseKey (a)) == 0) names << "|" << a;
                juce::String keysEn;
                for (auto* k : list) if (auto* rk = enKnob (*k)) keysEn << " " << U (rk->a) << " " << U (rk->b);
                Faq g { names, "potard effet knob" + keysEn, ans, 1 };
                g.answerEn = ansEn;
                g.generic = true;
                v.push_back (g);
            }
            for (auto& f : v) f.definition = normalise (f.names.upToFirstOccurrenceOf ("|", false, false)).trim().startsWith ("c est quoi");
            return v;
        }();
        return all;
    }

    //==========================================================================
    // index : vocabulaire commun, poids par fiche, rareté des mots
    struct Index
    {
        juce::StringArray vocab;
        std::map<juce::String, int> vocabId;
        std::vector<std::pair<juce::String, int>> surfaces;   // mot tel qu'écrit -> sa racine (pour les fautes de frappe)
        std::vector<float> idf;
        struct E { std::unordered_map<int, float> w; std::vector<std::vector<int>> names; };
        std::vector<E> entries;
    };

    inline int vocabIndex (Index& ix, const juce::String& w)
    {
        auto it = ix.vocabId.find (w);
        if (it != ix.vocabId.end()) return it->second;
        const int id = ix.vocab.size();
        ix.vocab.add (w);
        ix.vocabId[w] = id;
        return id;
    }

    inline const Index& index()
    {
        static const Index ix = []
        {
            Index x;
            std::set<juce::String> seenSurf;
            const auto& f = faq();
            x.entries.resize (f.size());
            for (size_t i = 0; i < f.size(); ++i)
            {
                auto& e = x.entries[i];
                auto put = [&] (const Tok& t, float w)
                {
                    const int id = vocabIndex (x, t.can);
                    auto it = e.w.find (id);
                    if (it == e.w.end() || it->second < w) e.w[id] = w;
                    if (t.surf.length() >= 4 && seenSurf.insert (t.surf).second) x.surfaces.push_back ({ t.surf, id });
                    return id;
                };
                juce::StringArray ns; ns.addTokens (f[i].names, "|", "");
                for (auto& n : ns)
                {
                    std::vector<int> ph;
                    for (auto& t : tokens (n)) ph.push_back (put (t, 3.0f));
                    if (! ph.empty()) e.names.push_back (ph);
                }
                for (auto& t : tokens (f[i].keys)) put (t, 1.5f);
                for (auto& t : tokens (f[i].answer)) put (t, 0.45f);
                if (f[i].answerEn != f[i].answer) for (auto& t : tokens (f[i].answerEn)) put (t, 0.45f);
            }
            std::vector<int> df ((size_t) x.vocab.size(), 0);
            for (auto& e : x.entries) for (auto& [id, w] : e.w) if (w >= 1.0f) ++df[(size_t) id];
            const float n = (float) f.size();
            x.idf.resize ((size_t) x.vocab.size());
            for (size_t id = 0; id < df.size(); ++id)
                x.idf[id] = juce::jlimit (0.4f, 1.0f, std::log ((n + 1.0f) / ((float) df[id] + 0.5f)) / std::log (n + 1.0f) + 0.15f);
            return x;
        }();
        return ix;
    }

    // mots de la base qui ressemblent à un mot de la question (mémorisé)
    inline std::vector<std::pair<int, float>> matchesFor (const Tok& t)
    {
        static std::map<juce::String, std::vector<std::pair<int, float>>> cache;
        static juce::CriticalSection lock;
        const juce::ScopedLock sl (lock);
        const auto key = t.surf + "|" + t.can;
        auto it = cache.find (key);
        if (it != cache.end()) return it->second;
        const auto& ix = index();
        std::map<int, float> best;
        for (int id = 0; id < ix.vocab.size(); ++id)
        {
            const float m = wordMatch (t.can, ix.vocab[id]);
            if (m > 0.0f) best[id] = m;
        }
        // faute de frappe sur le mot entier (avant la racine) : "lanecment" -> "lancement"
        const int ls = t.surf.length();
        if (ls >= 4)
            for (auto& [sv, id] : ix.surfaces)
            {
                const int shortest = juce::jmin (ls, sv.length());
                if (std::abs (ls - sv.length()) > 2 || shortest < 4 || sv == t.surf) continue;
                if (sv[0] != t.surf[0] && shortest < 6) continue;
                const int lim = shortest >= 8 ? 2 : 1;
                const int d = editDistance (t.surf, sv, lim);
                if (d <= lim)
                {
                    const float m = shortest <= 4 ? 0.45f : (shortest == 5 ? 0.62f : (d == 1 ? 0.72f : 0.6f));
                    if (best[id] < m) best[id] = m;
                }
            }
        std::vector<std::pair<int, float>> r (best.begin(), best.end());
        if (cache.size() > 20000) cache.clear();
        cache[key] = r;
        return r;
    }

    struct Match { int index = -1; float score = 0.0f; int second = -1; float secondScore = 0.0f; };

    inline float threshold() { return 1.6f; }
    inline float guessThreshold() { return threshold() * 0.75f; }   // en dessous : « j'ai pas capté »

    // le mot existe-t-il tel quel dans la base ?
    inline bool knownSurface (const juce::String& s)
    {
        static const std::set<juce::String> known = []
        {
            std::set<juce::String> k;
            for (auto& [sv, id] : index().surfaces) k.insert (sv);
            for (auto& v : index().vocab) k.insert (v);
            return k;
        }();
        return known.count (s) != 0;
    }

    // mot vide mal tapé ("commnt", "mette") : on l'ignore
    inline bool nearStopWord (const juce::String& s)
    {
        if (s.length() < 5 || knownSurface (s)) return false;
        for (auto& w : stopWords())
            if (w.length() >= 5 && std::abs (w.length() - s.length()) <= 1 && editDistance (s, w, 1) <= 1) return true;
        return false;
    }

    // trouve la meilleure fiche. favourTab : sujet de la question d'avant (questions de suite) ; currentTab : page affichée
    inline Match find (const juce::String& question, int favourTab = -2, int currentTab = -2, std::vector<std::pair<float, int>>* all = nullptr)
    {
        Match m;
        std::vector<Tok> toks;
        for (auto& t : tokens (question, true))
        {
            bool looksKnown = false;
            for (auto& h : matchesFor (t)) if (h.second >= 0.6f) looksKnown = true;
            if (looksKnown || ! nearStopWord (t.surf)) toks.push_back (t);
        }
        if (toks.empty()) return m;
        const auto& ix = index();
        const auto& f = faq();
        const bool def = asksDefinition (question);

        // poids de chaque mot de la question : rareté (celle du mot tapé), politesse, mots faibles
        struct QW { std::vector<std::pair<int, float>> hits; float weak, idf; bool longWord; };
        std::vector<QW> q;
        bool hasStrong = false, hasNonSocial = false;
        for (auto& t : toks) { if (weakWords().count (t.can) == 0) hasStrong = true; if (! isSocial (t.can)) hasNonSocial = true; }
        int meaningful = 0;
        for (auto& t : toks)
        {
            float weak = 1.0f;
            if (isSocial (t.can) && hasNonSocial) weak = 0.25f;
            else if (hasStrong && weakWords().count (t.can) != 0) weak = 0.3f;
            if (weak >= 1.0f) ++meaningful;
            QW x { matchesFor (t), weak, 0.0f, t.surf.length() >= 5 };
            if (x.hits.empty()) continue;
            float top = 0.0f;
            for (auto& h : x.hits) top = juce::jmax (top, h.second);
            for (auto& h : x.hits) if (h.second >= top - 0.001f) x.idf = juce::jmax (x.idf, ix.idf[(size_t) h.first]);
            q.push_back (std::move (x));
        }
        if (q.empty()) return m;
        int nonSocial = 0;
        for (auto& t : toks) if (! isSocial (t.can)) ++nonSocial;
        meaningful = juce::jmax (1, meaningful);

        struct Got { int id; float fm, idf, weak; };
        std::vector<float> scores (ix.entries.size(), 0.0f);
        for (size_t i = 0; i < ix.entries.size(); ++i)
        {
            const auto& e = ix.entries[i];
            float s = 0.0f;
            int covered = 0;
            std::vector<Got> got;   // mots de la fiche touchés
            for (auto& x : q)
            {
                float best = 0.0f; int bestId = -1; float bestF = 0.0f;
                for (auto& [id, fm] : x.hits)
                {
                    auto it = e.w.find (id);
                    if (it == e.w.end()) continue;
                    const float v = fm * it->second;
                    if (v > best) { best = v; bestId = id; bestF = fm; }
                }
                if (bestId >= 0)
                {
                    s += best * x.idf * x.weak;
                    got.push_back ({ bestId, bestF, x.idf, x.weak });
                    if (x.weak >= 1.0f && best >= 0.9f && (bestF >= 0.8f || (bestF >= 0.6f && x.longWord))) ++covered;
                }
            }
            if (s <= 0.0f) continue;
            // bonus si un nom entier de la fiche est dans la question (plus le nom est long, plus c'est précis)
            float bonus = 0.0f;
            bool exactName = false;   // la question n'est QUE ce nom (pour les fiches « c'est quoi … »)
            for (auto& ph : e.names)
            {
                int hit = 0; float idfSum = 0.0f, minF = 1.0f, sumF = 0.0f, effN = 0.0f;
                for (int id : ph)
                    for (auto& g : got)
                        if (g.id == id && g.fm >= 0.6f) { ++hit; idfSum += g.idf * g.weak; effN += g.weak; sumF += g.fm; minF = juce::jmin (minF, g.fm); break; }
                const int n = (int) ph.size();
                float b = 0.0f;
                if (hit == n)
                {
                    b = (1.2f + 1.1f * juce::jmax (0.0f, effN - 1.0f)) * (idfSum / juce::jmax (0.25f, effN)) * (sumF / (float) n);
                    if (minF >= 0.95f && nonSocial <= n) exactName = true;
                }
                else if (n >= 2 && hit > 0) b = 0.2f * (float) hit;
                bonus = juce::jmax (bonus, b);
            }
            s += bonus;
            s *= 0.2f + 0.8f * juce::jmin (1.0f, (float) covered / (float) meaningful);
            if (f[i].definition) s += (def && exactName) ? 1.0f : -0.3f;
            if (favourTab > -2 && f[i].tab == favourTab) s += 0.6f;
            if (currentTab >= 0 && f[i].tab == currentTab) s += 0.25f;
            if (all != nullptr) all->push_back ({ s, (int) i });
            scores[i] = s;
            if (s > m.score) { m.second = m.index; m.secondScore = m.score; m.score = s; m.index = (int) i; }
            else if (s > m.secondScore) { m.secondScore = s; m.second = (int) i; }
        }
        // potard du même nom sur une autre page ("Vitesse", "Attaque", "Transpo"…) : on prend celui de la page affichée,
        // si un de ses noms contient tous les mots importants de la question
        if (m.index >= 0 && currentTab >= 0 && f[(size_t) m.index].tab != currentTab)
        {
            juce::StringArray key;
            for (auto& t : toks) if (! isSocial (t.can) && weakWords().count (t.can) == 0) key.add (t.can);
            if (! key.isEmpty())
            {
                int pick = -1;
                for (size_t i = 0; i < f.size(); ++i)
                    if (f[i].tab == currentTab && scores[i] >= m.score * 0.5f && (pick < 0 || scores[i] > scores[(size_t) pick]))
                    {
                        juce::StringArray ns; ns.addTokens (f[i].names, "|", "");
                        for (auto& n : ns)
                        {
                            const auto nw = questionWords (n);
                            bool all = true;
                            for (auto& k : key) if (! nw.contains (k)) all = false;
                            if (all) { pick = (int) i; break; }
                        }
                    }
                if (pick >= 0) { m.second = m.index; m.secondScore = m.score; m.index = pick; m.score = juce::jmax (scores[(size_t) pick], threshold()); }
            }
        }
        return m;
    }

    // pour régler Kick : les 6 meilleures fiches et leur score
    inline juce::String explain (const juce::String& question)
    {
        juce::String out;
        out << "mots :";
        for (auto& t : tokens (question, true)) out << " " << t.surf << "/" << t.can << (nearStopWord (t.surf) ? "(ignore)" : "");
        out << (asksDefinition (question) ? "  [definition]" : "") << "\n";
        std::vector<std::pair<float, int>> all;
        const auto& f = faq();
        find (question, -2, -2, &all);
        std::sort (all.begin(), all.end(), [] (auto& a, auto& b) { return a.first > b.first; });
        for (size_t k = 0; k < all.size() && k < 6; ++k)
            out << "  " << juce::String (all[k].first, 2) << "  " << f[(size_t) all[k].second].names.upToFirstOccurrenceOf ("|", false, false) << "\n";
        return out;
    }


    // score d'une explication de bouton (titre + texte), sur la même échelle que find()
    inline float scoreHint (const juce::String& question, const juce::String& title, const juce::String& text)
    {
        const auto words = questionWords (question);
        if (words.isEmpty()) return 0.0f;
        const auto tw = questionWords (title), xw = questionWords (text);
        bool hasStrong = false;
        for (auto& w : words) if (weakWords().count (w) == 0) hasStrong = true;
        float s = 0.0f; int titleHits = 0, covered = 0, meaningful = 0;
        for (auto& w : words)
        {
            const float weak = (hasStrong && weakWords().count (w) != 0) ? 0.3f : 1.0f;
            if (weak >= 1.0f) ++meaningful;
            float best = 0.0f; bool inTitle = false;
            for (auto& k : tw) { const float v = wordMatch (w, k) * 3.0f; if (v > best) { best = v; inTitle = true; } }
            for (auto& k : xw) { const float v = wordMatch (w, k) * 1.0f; if (v > best) { best = v; inTitle = false; } }
            if (best > 0.0f) { s += best * weak * 0.8f; if (inTitle) ++titleHits; if (weak >= 1.0f) ++covered; }
        }
        if (tw.size() > 0 && titleHits >= tw.size()) s += 1.2f + 1.1f * (float) (tw.size() - 1);
        return s * (0.55f + 0.45f * juce::jmin (1.0f, (float) covered / (float) juce::jmax (1, meaningful)));
    }

    // ancienne interface
    inline int bestMatch (const juce::String& question, float* scoreOut = nullptr, int* secondOut = nullptr, float* secondScore = nullptr, int favourTab = -2)
    {
        const auto m = find (question, favourTab);
        if (scoreOut != nullptr) *scoreOut = m.score;
        if (secondOut != nullptr) *secondOut = m.second;
        if (secondScore != nullptr) *secondScore = m.secondScore;
        return m.score >= threshold() * 0.5f ? m.index : -1;
    }

    // le sujet d'une fiche (son 1er nom), pour les titres
    inline juce::String subjectOf (int i)
    {
        if (! juce::isPositiveAndBelow (i, (int) faq().size())) return {};
        auto s = faq()[(size_t) i].names.upToFirstOccurrenceOf ("|", false, false).trim();
        return s.substring (0, 1).toUpperCase() + s.substring (1);
    }
}

//==============================================================================
// Ton « entre potes » : Kick parle comme un pote qui t'écrit (pas de virgules, contractions, une petite vanne de temps en temps).
// Seulement en français (les autres langues gardent la traduction telle quelle).
namespace kick
{
    inline juce::String potes (const juce::String& text, juce::Random& rng, bool tease = true)
    {
        if (text.isEmpty()) return text;
        std::string s = text.toStdString();
        auto rep = [&s] (const char* re, const char* by) { s = std::regex_replace (s, std::regex (re), by); };
        // tournures parlées
        rep (R"(\bIl y a\b)", "Y a");          rep (R"(\bil y a\b)", "y a");
        rep (R"(\bIl faut\b)", "Faut");        rep (R"(\bil faut\b)", "faut");
        rep (R"(\bTu as\b)", "T'as");          rep (R"(\btu as\b)", "t'as");
        rep (R"(\bTu es\b)", "T'es");          rep (R"(\btu es\b)", "t'es");
        rep (R"(\bCe n'est pas\b)", "C'est pas"); rep (R"(\bce n'est pas\b)", "c'est pas");
        rep (R"(\bCela\b)", "\xc3\x87" "a");     rep (R"(\bcela\b)", "\xc3\xa7" "a");
        rep (R"(\bne ([^\s,.;:]+) (pas|plus|jamais|rien)\b)", "$1 $2");
        rep (R"(\bn'([^\s,.;:]+) (pas|plus|jamais|rien)\b)", "$1 $2");
        // ponctuation de pote : pas de virgules, une phrase par ligne
        rep (R"( ?; )", " ");
        rep (R"(, )", " ");
        rep (R"( : )", " ");
        rep (R"(\bex\. )", "ex ");
        rep (R"(\.\s+(?=\S))", "\n");
        rep (R"(\.$)", "");
        rep (R"(\.\n)", "\n");
        auto out = juce::String::fromUTF8 (s.c_str());
        if (tease && rng.nextFloat() < 0.35f)
        {
            static const char* vannes[] = {
                "trql c'était pas sorcier", "t'aurais pu trouver tout seul hein", "allez au taf maintenant", "même mon skate aurait su",
                "tu vois quand tu veux", "et après tu diras que c'est compliqué", "va faire des hits maintenant", "je te facture la prochaine question",
                "t'inquiète je dirai à personne que t'as demandé", "c'est cadeau mon reuf", "note-le cette fois", "la prochaine fois tu lis le plugin avant de me déranger",
                "fais pas genre t'avais compris", "allez bouge maintenant ça va te sortir une prod de fou" };
            out << "\n" << juce::String::fromUTF8 (vannes[rng.nextInt ((int) (sizeof (vannes) / sizeof (vannes[0])))]);
        }
        return out;
    }
}
