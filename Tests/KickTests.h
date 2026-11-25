#pragma once
// Entraînement / examen de Kick : des dizaines de milliers de questions générées + des questions écrites à la main.
#include "../Source/KickBrain.h"
#include <iostream>
#include <string>

namespace kicktest
{
    struct Result { int total = 0, good = 0; std::vector<std::string> fails; float rate() const { return total > 0 ? (float) good / (float) total : 1.0f; } };

    inline std::string utf8 (const juce::String& s) { return s.toStdString(); }

    inline juce::String phraseKey (const juce::String& s) { auto w = kick::questionWords (s); w.sort (false); return w.joinIntoString (" "); }

    // la fiche j répond aussi à la question si elle a le même nom (doublon voulu) ou la même réponse
    inline bool sameAnswer (int i, int j, const juce::String& name)
    {
        if (i == j) return true;
        if (j < 0) return false;
        const auto& f = kick::faq();
        if (f[(size_t) i].answer == f[(size_t) j].answer) return true;
        juce::StringArray ns; ns.addTokens (f[(size_t) j].names, "|", "");
        const auto k = phraseKey (name);
        for (auto& n : ns) if (phraseKey (n) == k) return true;
        return false;
    }

    inline juce::String typoSwap (const juce::String& w) { const int p = w.length() / 2; return w.substring (0, p - 1) + w.substring (p, p + 1) + w.substring (p - 1, p) + w.substring (p + 1); }
    inline juce::String typoDrop (const juce::String& w) { const int p = w.length() / 2 + 1; return w.substring (0, p) + w.substring (p + 1); }
    inline juce::String typoDouble (const juce::String& w) { const int p = w.length() / 2; return w.substring (0, p) + w.substring (p - 1, p) + w.substring (p); }

    inline Result generated (bool withTypos)
    {
        static const char* templates[] = {
            "{n}", "{n} ?", "c'est quoi {n} ?", "c quoi {n}", "cest koi {n} frr", "à quoi sert {n} ?", "a quoi ca sert {n}", "{n} ça sert à quoi ?",
            "comment marche {n} ?", "comment utiliser {n}", "comment régler {n} ?", "explique moi {n} stp", "il fait quoi {n} ?", "je comprends pas {n}",
            "wsh kick c'est quoi {n}", "dis moi ce que fait {n}", "où est {n} ?", "je trouve pas {n}", "tu peux m'expliquer {n} ?", "what is {n}",
            "how does {n} work", "aide moi avec {n}", "j'ai une question sur {n}", "{n} c'est quoi exactement" };
        static const char* typoTemplates[] = { "{n}", "c'est quoi {n}", "comment marche {n}" };
        Result r;
        const auto& f = kick::faq();
        for (int i = 0; i < (int) f.size(); ++i)
        {
            juce::StringArray ns; ns.addTokens (f[(size_t) i].names, "|", "");
            int used = 0;
            for (auto& n0 : ns)
            {
                const auto n = n0.trim();
                if (kick::questionWords (n).isEmpty() || used >= 5) continue;
                ++used;
                juce::StringArray variants;
                if (! withTypos) variants.add (n);
                else
                {
                    // faute dans le mot le plus long (s'il est assez long)
                    juce::StringArray ws; ws.addTokens (n, " ", "");
                    int longest = -1;
                    for (int w = 0; w < ws.size(); ++w) if (ws[w].length() >= 6 && (longest < 0 || ws[w].length() > ws[longest].length())) longest = w;
                    if (longest < 0) continue;
                    for (int t = 0; t < 3; ++t)
                    {
                        auto c = ws;
                        c.set (longest, t == 0 ? typoSwap (ws[longest]) : (t == 1 ? typoDrop (ws[longest]) : typoDouble (ws[longest])));
                        variants.add (c.joinIntoString (" "));
                    }
                }
                for (auto& v : variants)
                {
                    const int nt = withTypos ? 3 : (int) (sizeof (templates) / sizeof (templates[0]));
                    for (int t = 0; t < nt; ++t)
                    {
                        const auto q = juce::String::fromUTF8 (withTypos ? typoTemplates[t] : templates[t]).replace ("{n}", v);
                        const auto m = kick::find (q);
                        ++r.total;
                        if (m.score >= kick::threshold() && sameAnswer (i, m.index, n)) ++r.good;
                        else if (r.fails.size() < 120)
                            r.fails.push_back (utf8 (q) + "  ->  " + (m.index >= 0 ? utf8 (kick::subjectOf (m.index)) : std::string ("(rien)"))
                                               + " [" + std::to_string (m.score).substr (0, 4) + "] au lieu de " + utf8 (kick::subjectOf (i)));
                    }
                }
            }
        }
        return r;
    }

    struct QA { const char* q; const char* expect; };

    // questions de vrais utilisateurs (formulées n'importe comment) -> un bout de la bonne réponse
    inline const std::vector<QA>& realQuestions()
    {
        static const std::vector<QA> qa {
            // démarrer / sons
            { "comment je met un son dans le plugin", "Glisse un son" }, { "how do i load a sound", "Glisse un son" }, { "jarrive pas a charger mon sample", "Glisse un son" },
            { "comment importer un wav", "Glisse un son" }, { "ou sont les saxophones", "saxos" }, { "y a des pianos ?", "pianos" },
            { "comment changer de son avec les fleches", "flèches haut / bas" }, { "les fleches marchent pas dans la banque", "flèches" },
            { "ou je mets mes propres sons", "Documents\\Variant Bibliothèque" }, { "c quoi le nom entre parenthese dans les sons", "source entre parenthèses" },
            { "je veux un son au lancement", "Un son chargé au lancement" }, { "jsuis perdu", "Pas de panique" }, { "par ou je commence", "visite" },
            { "comment marche le plugin", "visite" }, { "c'est quoi les onglets", "6 onglets" },
            // couches
            { "comment superposer plusieurs sons", "4 couches" }, { "combien de couches je peux mettre", "4 couches" },
            { "comment eteindre une couche", "bouton rond" }, { "le bouton auto il fait quoi", "sons complémentaires" },
            { "comment retirer un son d'une couche", "Clic droit sur une couche" }, { "a quoi sert naturel", "décalages de timing" },
            { "c'est quoi la repartition des couches", "Superposées" },
            // variations
            { "c'est quoi les variations", "variante un peu différente" }, { "round robin ?", "variante" }, { "a quoi sert intensite", "s'éloignent" },
            { "combien de variantes je dois mettre", "8 à 16" }, { "le style de variation experimental", "Expérimental" },
            // enveloppe
            { "c'est quoi l'attaque", "montée" }, { "comment rendre le son moins sec au debut", "Attaque" }, { "le release sert a quoi", "extinction" },
            { "c'est quoi le sustain", "niveau tenu" }, { "decay ?", "Déclin" }, { "c'est quoi une adsr", "enveloppe" },
            { "mon son se coupe quand je lache la touche", "Jouer en entier" }, { "comment faire que le son aille jusquau bout", "Jouer en entier" },
            { "c'est quoi la velocite", "force" }, { "vel timbre ca fait quoi", "sombres" }, { "a quoi sert le potard tenue", "maximum avant" },
            // couche
            { "comment monter le son d'une octave", "+12" }, { "transposer de 12", "demi-tons" }, { "mon sample sonne faux", "Note racine" },
            { "c'est quoi la note racine", "joué tel quel" }, { "ma 808 change de note", "C5" }, { "a quoi sert la finesse", "cents" },
            { "accordage lie ?", "décale aussi les autres" }, { "comment mettre le son a gauche", "Pan" }, { "le son est trop fort", "Volume" },
            { "comment baisser le volume general", "Sortie" },
            // slide
            { "faire un slide 808", "glisse" }, { "comment faire glisser ma 808", "Legato" }, { "c'est quoi le glide", "Glide" },
            { "le temps du slide", "glisser jusqu'à sa hauteur" }, { "slide d entree", "démarre ce nombre de demi-tons" }, { "portamento", "Glide" },
            // sample
            { "comment enlever le blanc au debut de mon mp3", "Couper le blanc" }, { "y a un silence avant mon son", "Couper le blanc" },
            { "smp start", "où le son commence" }, { "comment mettre le son a l'envers", "à l'envers" }, { "fade in", "montée douce" },
            { "normaliser mon son", "niveau maximum" }, { "deux couches s'annulent le son est creux", "Polarité" },
            { "comment faire tenir un one shot a l infini", "Loop points" }, { "c'est quoi le ping pong de la boucle", "aller-retour" },
            { "je peux charger un sf2 ?", ".sf2" }, { "je peux lire mes presets serum", "Serum" },
            // boucles
            { "ma boucle d'arpege est pas calee", "Boucle" }, { "stretch ca sert a quoi", "tempo de FL" }, { "c'est quoi bpm boucle", "tempo d'origine" },
            // clavier
            { "comment jouer avec mon clavier dordi", "Clavier" }, { "clavier en gamme ?", "notes de la gamme" },
            { "les touches de mon clavier jouent rien", "clique d'abord dans le plugin" }, { "changer d'octave au clavier", "flèches" },
            { "jai un clavier midi comment je fais", "Channel rack" },
            // hébergés / prise
            { "je peux mettre serum dans variant ?", "Plugins dans Variant" }, { "comment charger un vst dans variant", "VST3" },
            { "comment enregistrer ce que je joue en wav", "PRISE" }, { "la prise dure combien de temps max", "4 minutes" },
            // effets
            { "comment ajouter un effet", "glisse une tuile" }, { "combien d'effets max", "6 places" }, { "les effets changent tout seul quand je change de son", "Effets auto" },
            { "chaine au hasard", "chaîne cohérente" }, { "le bouton hasard d'un effet", "toujours musical" }, { "comment bypass un effet", "contourne" },
            { "changer l'ordre des effets", "flèches" }, { "c'est quoi le mix", "dosage" }, { "dry wet", "dosage" }, { "le delay suit le tempo ?", "BPM de FL" },
            { "variant reverb dans le mixer", "plugins séparés" },
            { "c'est quoi la reverb", "espace" }, { "reverbe", "espace" }, { "a quoi sert le predelay de la reverb", "attente avant" },
            { "amorti reverb", "coupe les aigus" }, { "shimmer ?", "octaves brillantes" }, { "durée de la réverb", "queue" },
            { "c'est quoi le delay", "écho" }, { "feedback du delay", "combien d'échos" }, { "ducking", "baisse les échos" }, { "usure du delay", "bande" },
            { "ping pong delay", "rebondissent" }, { "c'est quoi la saturation", "chaleur" }, { "drive de la saturation", "quantité de saturation" },
            { "modele saturation", "Lampe" }, { "c'est quoi l'ott", "3 bandes" }, { "profondeur ott", "potard principal" }, { "ecrase ott", "sons forts" },
            { "c'est quoi un egaliseur", "paramétrique" }, { "comment utiliser l'eq", "points" }, { "c'est quoi le chorus", "épaissit" },
            { "flanger", "avion" }, { "through zero", "zéro" }, { "phaser etages", "étages" }, { "tremolo", "pulser" }, { "auto pan", "gauche / droite" },
            { "lofi", "abîme" }, { "c'est quoi wow et flutter", "Lo-fi" }, { "bits du lofi", "résolution" }, { "portal", "grains" }, { "cristal", "inversés" },
            { "c'est quoi le haas", "quelques ms" }, { "graves mono", "mono" }, { "halftime", "moitié vitesse" }, { "gross beat", "Gross Beat" },
            { "sidechain", "Sidechain" }, { "faire un effet pompe", "Pompe" }, { "glitch", "Digitalis" }, { "chroma", "réaccorde" },
            { "harmoniseur", "réaccorde" }, { "nappe", "pad" }, { "liquide", "courbe" }, { "bruit de vinyle", "vinyle" }, { "craquements", "craquements" },
            { "a quoi sert la vitesse", "plusieurs effets" }, { "la vitesse du chorus", "ondulation" }, { "resonance du filtre", "303" },
            { "c'est quoi le feedback", "plusieurs effets" }, { "largeur stereo", "Largeur" }, { "comment elargir le son", "Largeur" },
            // accords
            { "comment generer des accords", "Générer une progression" }, { "je veux des accords plus originaux", "Variété" }, { "accords hors gamme", "Hors gamme" },
            { "couleur jazzy", "9e" }, { "accords jazz", "9e" }, { "les ambiances du generateur", "Swagg" }, { "surprends moi", "au hasard" },
            { "le bouton j'aime", "te plaît" }, { "mes gouts", "aimées" }, { "harmoniser ma melodie", "Accords autour de mes notes" },
            { "comment modifier une note dans le piano roll", "piano roll" }, { "c'est quoi le pinceau", "peindre" }, { "comment effacer une note", "effacer" },
            { "notes slide dans le piano roll", "triangle" }, { "c'est quoi l'aimant", "où se posent les notes" }, { "quantize", "aimant" },
            { "importer un fichier midi", ".mid" }, { "comment exporter en midi vers fl", "pastilles MIDI" }, { "comment exporter en midddi", "pastilles MIDI" },
            { "ou vont les midi exportes", "Documents\\Variant MIDI" }, { "play de fl", "SA progression" }, { "espace ne lance pas fl", "Espace" },
            { "comment changer la gamme", "gamme" }, { "c'est quoi une gamme", "notes qui sonnent bien" }, { "c'est quoi le voicing", "Drop 2" },
            { "renversement", "en bas" }, { "strum", "guitare" }, { "comment jouer des accords avec un seul doigt", "accord complet" },
            { "accords tristes", "Mineur" }, { "quelle gamme pour la drill", "Phrygien" }, { "metronome", "métronome" },
            // arpège
            { "comment activer l'arpegiateur", "arpège" }, { "arp", "Arpège" }, { "c'est quoi le latch", "lâches les touches" },
            { "vitesse de l'arp", "1/16" }, { "les modes de l'arpege", "Montant" }, { "octaves arp", "octaves" }, { "c'est quoi le swing", "groove" },
            { "probabilite", "chance que chaque pas joue" }, { "ratchet", "roulement" }, { "comment faire des hi hat rolls", "roulement" },
            { "coups ratchet", "2 à 4" }, { "decalage du motif", "plus loin" }, { "rampe", "vélocité monte" }, { "accent", "temps" },
            { "basse sur le premier pas", "octave en dessous" }, { "les barres de l'arpege", "force" }, { "liberte", "improvise" },
            { "plus d'options de l'arpege", "Ratchet" }, { "presets d'arpege", "prêts à jouer" }, { "l'arpege marche pas sur mon son", "seq / arp" },
            { "tout au hasard", "tirés au hasard" }, { "durée des notes de l'arp", "staccato" },
            // sampler
            { "comment découper un sample", "découpé tout seul" }, { "faire des chops", "découpé tout seul" }, { "c'est quoi le bpm de mon son", "tempo et la tonalit" },
            { "le bpm est faux dans le sampler", "tape le bon tempo" }, { "comment changer le bpm du sample", "Entrée" }, { "enregistrer mes chops", "Rec" },
            { "jouer les chops au clavier", "A Z E R T Y" }, { "les pads", "pads" }, { "banque b des pads", "17-32" }, { "c'est quoi la sensibilite", "attaques faibles" },
            { "j'ai trop de tranches", "Sensibilité" }, { "decoupe fine", "serrées" }, { "1er chop", "C0" }, { "caler sur fl", "accélère ou ralentit" },
            { "couper au relachement", "s'arrête quand la note s'arrête" }, { "une tranche a la fois", "MPC" }, { "volumes egalises", "même volume" },
            { "couleur des chops", "sombre" }, { "attaque des chops", "punchy" }, { "comment regler le volume d'un seul chop", "Clic droit sur un pad" },
            { "mettre un chop a l'envers", "Reverse" }, { "taper mes chops", "Espace" }, { "faire mes propres chops", "Mes chops" },
            { "deplacer un repere", "transitoires" }, { "choisir une partie du sample", "bande du haut" }, { "appliquer au plugin", "tonalité" },
            { "exporter mes chops en wav", ".wav" }, { "arpeger les tranches", "une seule note" },
            // synthé
            { "c'est quoi le synthe", "tables d'ondes" }, { "wavetable", "tables d'ondes" }, { "comment allumer le synthe", "mêmes notes" },
            { "c'est quoi l'unisson", "voix superposées" }, { "detune", "écart" }, { "c'est quoi le cutoff", "coupure" }, { "resonance", "coupure" },
            { "c'est quoi un lfo", "onde lente" }, { "comment synchroniser le lfo au tempo", "Sync" }, { "matrice de modulation", "source" },
            { "les macros", "plusieurs réglages" }, { "mode mono", "une note à la fois" }, { "warp", "tord" }, { "position dans la table", "timbre" },
            { "sauver mon son de synthe", "Enregistrer" }, { "tables de serum", "Serum" }, { "sub", "grave" },
            // réglages / interface
            { "je veux changer la langue", "Langue" }, { "mettre le plugin en anglais", "Langue" }, { "changer le theme", "thème" }, { "agrandir le plugin", "coin" },
            { "l'interface vibre", "Effets visuels" }, { "enlever les particules", "Effets visuels" }, { "les raccourcis clavier", "F1" },
            { "comment annuler", "Ctrl+Z" }, { "ctrl z", "Ctrl+Z" }, { "la bombe en haut", "zéro" }, { "tout reset", "zéro" },
            { "detacher la page", "propre fenêtre" }, { "la molette marche pas", "molette" }, { "remettre un potard par defaut", "Double-clic" },
            { "automatiser un potard dans fl", "automation" }, { "sauver un preset", "Mes presets" }, { "mes reglages sont perdus", ".flp" },
            // Kick
            { "salut kick", "Pose ta question" }, { "wsh", "Pose ta question" }, { "merci", "kickflip" }, { "t'es qui", "Kick" },
            { "changer la tenue de kick", "personnage" }, { "changer de skin", "personnage" }, { "kick a disparu", "Guide Kick" },
            { "comment remettre kick", "Guide Kick" }, { "comment desactiver kick", "Désactiver Kick" }, { "kick sur le bureau", "bureau" },
            { "raconte une blague", "frigo" }, { "t'es une ia ?", "hors ligne" }, { "une replique", "réplique" },
            // licence / install / problèmes
            { "mon @ est deja utilise", "libérer" }, { "j'ai change de pc", "libérer" }, { "c'est quoi le truc instagram", "licence" },
            { "le plugin est bloqué", "désactivé" }, { "qui a cree variant", "prodbysotam" }, { "fl trouve pas variant", "Manage plugins" },
            { "ca marche sur mac ?", "Mac" }, { "ca marche sur ableton", "Ableton" }, { "ca rame", "buffer" }, { "il y a pas de son", "Pas de son" },
            { "j'entends rien", "Pas de son" }, { "fl joue rien", "Channel rack" }, { "le plugin a crash", "plante" },
            // prod
            { "comment faire une 808", "808" }, { "comment avoir un son plus gros", "superpose" }, { "faire un son lofi", "Lo-fi" }, { "faire un pad", "Nappe" },
            { "mon son est trop brillant", "passe-bas" }, { "trop de basses", "coupe-bas" }, { "ça sature", "sature" }, { "plus de graves", "graves" },
            // --- série 2 : potards d'effets, posés comme on parle
            { "le potard drive de la saturation il fait quoi", "quantité de saturation" }, { "dureté saturation", "écrêtage" }, { "coupe bas de la saturation", "graves avant" },
            { "c'est quoi le ton sur la saturation", "couleur après" }, { "sortie de la saturation", "compenser" }, { "le modele fuzz", "Fuzz" },
            { "type de filtre", "Passe-bas" }, { "frequence du filtre", "coupure" }, { "mouvement du filtre", "bouge toute seule" }, { "vitesse du filtre", "tempo" },
            { "drive du filtre", "grain" }, { "ott vitesse", "Rapide" }, { "remonte ott", "faibles" }, { "graves ott", "bande grave" }, { "aigus de l'ott", "aiguë" },
            { "mode du chorus", "Ensemble" }, { "profondeur du chorus", "amplitude" }, { "retard chorus", "temps de base" }, { "reinjection flanger", "métallique" },
            { "stereo flanger", "large" }, { "nombre d'etages du phaser", "creux" }, { "centre du phaser", "fréquences" }, { "forme du tremolo", "pulsation" },
            { "synchro tremolo", "tempo" }, { "profondeur tremolo", "volume baisse" }, { "echantillonnage lofi", "SP-1200" }, { "wow lofi", "cassette" },
            { "flutter", "bande usée" }, { "grains portal", "10 à 500 ms" }, { "densite portal", "combien de grains" }, { "dispersion", "hasard dans le passé" },
            { "hauteurs portal", "transposés" }, { "hauteur cristal", "+12" }, { "temps cristal", "1,5 s" }, { "temps du delay", "1/8" },
            { "diffusion delay", "flous" }, { "ton du delay", "sombres" }, { "espace reverb", "taille de l'espace" }, { "modulation reverb", "onduler" },
            { "pre-delai tempo", "tempo" }, { "duree tempo reverb", "mesures" }, { "largeur de l'effet stereo", "200 %" }, { "balance stereo", "gauche ou à droite" },
            { "vitesse halftime", "1/2" }, { "mode du halftime", "Garde la hauteur" }, { "wobble", "onduler" }, { "variation halftime", "casse la routine" },
            { "slot temps", "Tape stop" }, { "slot volume", "Sidechain" }, { "cycle temps et volume", "longueur du motif" }, { "lissage glitch", "clics" },
            { "grille du glitch", "1/16" }, { "motif glitch", "Bégaiement" }, { "crush glitch", "Crush" }, { "gate glitch", "coupe le son" },
            { "correction chroma", "notes justes" }, { "focus chroma", "précision" }, { "mode chroma", "latence" }, { "decalage chroma", "avant le recalage" },
            { "style nappe", "Chœur" }, { "gel de la nappe", "infinie" }, { "montee nappe", "3 s" }, { "octave nappe", "+1" },
            { "type liquide", "flanger" }, { "longueur liquide", "mesures" }, { "courbe liquide", "forme de courbe" }, { "type de bruit", "vinyle" },
            { "suit le son", "monte et descend" }, { "rythme du bruit", "pulser" }, { "volume du bruit", "-60" },
            // --- série 2 : synthé
            { "c'est quoi le blend", "voix d'unisson" }, { "phase aleatoire", "hasard à chaque note" }, { "le sub du synthe", "grave" },
            { "les types de filtre du synthe", "Peigne" }, { "suivi clavier du filtre", "aiguës" }, { "env 2", "matrice" }, { "lfo sync", "tempo" },
            { "lfo par note", "repart" }, { "formes du lfo", "Sinus" }, { "polyphonie", "notes en même temps" }, { "glide synthe", "glissent" },
            { "init synthe", "scie" }, { "hasard synthe", "au hasard" }, { "deposer un wav dans le synthe", ".wav" }, { "drive synthe", "avant le filtre" },
            // --- série 2 : son / sampler / divers
            { "c'est quoi la courbe du slide", "régulier" }, { "glide toujours", "Toujours" }, { "le legato des leads", "30 ms" }, { "delai de l'enveloppe", "attente" },
            { "courbe attaque", "petits ronds" }, { "debut de la boucle", "repères verts" }, { "fondu de boucle", "jonction" }, { "polarite", "polarité" },
            { "ordre des couches", "Grave → aigu" }, { "boucles en accord", "Note la plus grave" }, { "nouvelles variations", "nouvelle série" },
            { "retard couche", "quelques ms" }, { "fenetre du plugin heberge", "interface" }, { "effacer la prise", "recommencer" },
            { "mode de decoupe", "Transitoires" }, { "decoupe au tempo", "Au tempo" }, { "finesse sampler", "cent" }, { "stretch sampler", "sans changer de hauteur" },
            { "zone a decouper", "bande du haut" }, { "zoom sampler", "Zoom zone" }, { "relache des chops", "doucement" }, { "velocite des chops", "même volume" },
            { "dans une couche", "couche" }, { "vers le piano roll", "piano roll" }, { "choke", "MPC" }, { "jouer les tranches", "au lieu des couches" },
            { "ambiance trap", "Trap" }, { "richesse des accords", "triades" }, { "registre des accords", "Grave" }, { "nombre de mesures", "8 mesures" },
            { "changer un seul accord", "seulement" }, { "outil couper", "deux" }, { "selectionner plusieurs notes", "rectangle" }, { "zoom piano roll", "Ctrl" },
            { "revenir aux accords", "annule" }, { "midi sans chevauchement", "s'arrête quand le suivant" }, { "gammes exotiques", "Hirajoshi" },
            { "synchro de la gamme entre plusieurs variant", "suivent" }, { "accord fixe", "Accord fixe" }, { "humanisation des accords", "imprécisions" },
            { "source de l'arpege", "Tranches du slicer" }, { "velocite arp", "Fixe" }, { "repartir du debut a chaque note", "relance" },
            { "direction des octaves", "Vers le haut" }, { "contraindre a la gamme", "ramène" }, { "pourquoi l'arpege marche pas", "seq / arp" },
            { "theme palestine", "Palestine" }, { "niveau auto", "trop forts" }, { "qualite+", "polis" }, { "taper une valeur", "Taper une valeur" },
            { "kick danse", "tempo" }, { "liberer mon @", "libérer" }, { "les stats", "installations" }, { "nouvelle version", "créateur" },
            { "comment marche la molette", "défiler" }, { "ca marche sur logic", "Logic" }, { "son plus large", "Largeur" }, { "son vintage", "lo-fi" },
            { "l'arp joue trop de notes", "Probabilité" }, { "rendre l'arp plus vivant", "Probabilité" },
        };
        return qa;
    }

    inline Result real()
    {
        Result r;
        for (auto& x : realQuestions())
        {
            const auto m = kick::find (juce::String::fromUTF8 (x.q));
            ++r.total;
            const bool ok = m.index >= 0 && m.score >= kick::guessThreshold()
                            && kick::faq()[(size_t) m.index].answer.contains (juce::String::fromUTF8 (x.expect));
            if (ok) ++r.good;
            else r.fails.push_back (std::string (x.q) + "  ->  " + (m.index >= 0 ? utf8 (kick::subjectOf (m.index)) : std::string ("(rien)"))
                                    + " [" + std::to_string (m.score).substr (0, 4) + "] (attendu : " + x.expect + ")");
        }
        return r;
    }

    // questions hors sujet : Kick doit dire qu'il n'a pas compris
    inline Result nonsense()
    {
        static const char* qs[] = { "quelle est la capitale de l'allemagne", "qsdfqsdfqsdf", "le chat de ma voisine est roux", "combien coute une baguette",
                                    "qui a gagne la coupe du monde 2018", "je mange des pates ce soir", "zzzzzz", "blablabla blibli", "ma mere m'appelle",
                                    "il fait beau aujourd'hui", "recette du couscous", "1234567" };
        Result r;
        for (auto* q : qs)
        {
            const auto m = kick::find (juce::String::fromUTF8 (q));
            ++r.total;
            if (m.score < kick::guessThreshold()) ++r.good;
            else r.fails.push_back (std::string (q) + "  ->  " + utf8 (kick::subjectOf (m.index)) + " [" + std::to_string (m.score).substr (0, 4) + "]");
        }
        return r;
    }

    inline void print (const char* title, const Result& r, int maxFails = 60)
    {
        std::cout << "   Kick / " << title << " : " << r.good << " / " << r.total << " (" << juce::roundToInt (r.rate() * 1000.0f) / 10.0f << " %)\n";
        for (int i = 0; i < (int) r.fails.size() && i < maxFails; ++i) std::cout << "      x " << r.fails[(size_t) i] << "\n";
    }
}
