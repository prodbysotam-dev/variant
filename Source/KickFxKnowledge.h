#pragma once
// Base de connaissances de Kick, partie 2 : chaque effet et chacun de ses potards.

namespace kick
{
    // un effet : son nom (tel qu'affiché), d'autres noms, sa présentation
    struct FxInfo { const char* name; const char* aliases; const char* keys; const char* about; };
    // un potard d'effet
    struct FxKnob { const char* fx; const char* knob; const char* aliases; const char* text; };

    inline const std::vector<FxInfo>& fxInfos()
    {
        static const std::vector<FxInfo> f {
            { "Saturation", "saturateur|distorsion|disto|drive", "chaleur analogique lampe transfo console bande fuzz crunch",
              "La Saturation ajoute de la chaleur analogique (Lampe, Transfo, Console, Bande ou Fuzz), suréchantillonnée pour rester propre. Parfait pour qu'une 808 ou un piano s'entende mieux." },
            { "Filtre", "filter|filtre qui bouge|wah", "passe bas passe haut bande ladder resonance balayage mouvement",
              "Le Filtre coupe des fréquences (Passe-bas, Passe-haut, Passe-bande, Coupe-bande, Ladder 24 dB) et peut bouger tout seul en rythme avec « Mouvement » et « Vitesse »." },
            { "Compressor", "ott|pression|compresseur|compression|multibande", "compression dense brillant multibande ecrase remonte 3 bandes",
              "Compressor compresse vers le haut et vers le bas sur 3 bandes : son dense et brillant en un geste. Monte « Profondeur » et c'est réglé." },
            { "Égaliseur", "eq|egaliseur|equaliseur|parametrique", "points courbe bande frequence cloche plateau coupe pente",
              "L'Égaliseur est paramétrique comme celui de FL : glisse les points sur la courbe (fréquence et gain), molette = largeur, clic droit = type (cloche, plateau, coupe, encoche) et pente (12 à 48 dB/oct), double-clic = couper la bande." },
            { "Chorus", "chorus|dimension", "epaisseur dimension double large voix",
              "Le Chorus épaissit le son façon Dimension (modes I, II, I + II, Ensemble) : comme si plusieurs instruments jouaient ensemble." },
            { "Flanger", "flanger|effet avion|jet", "avion jet through zero balayage",
              "Le Flanger fait l'effet avion (balayage métallique), avec un mode through-zero pour un son plus profond." },
            { "Phaser", "phaser|phase", "tourbillon analogique etages",
              "Le Phaser fait tourbillonner le son (2 à 12 étages), façon pédale analogique." },
            { "Trémolo", "tremolo|auto pan|autopan", "pulsation volume pan rythme",
              "Le Trémolo fait pulser le volume en rythme, ou balade le son gauche / droite (Auto-pan)." },
            { "Lo-fi", "lofi|lo fi|bitcrusher|crusher|cassette", "bande usee grain numerique bits wow flutter vintage",
              "Le Lo-fi abîme le son exprès : bits et échantillonnage réduits (grain numérique), Wow et Flutter (bande usée)." },
            { "Grain", "portal|nuage|granulaire|grains", "granulaire grains nuages echos texture",
              "Grain découpe le son en grains et en fait des nuages et des échos granulaires. Super pour des textures." },
            { "Givre", "cristal|echos inverses|crystal", "echos inverses transposes octave scintille",
              "Givre fait des échos inversés et transposés (octaves, quintes) : un son qui scintille." },
            { "Delay", "echo|écho|delai", "echo bande tempo repetition ping pong",
              "Le Delay est un écho à bande calé sur le tempo de FL, avec ping-pong, usure de bande, diffusion et ducking." },
            { "Réverb", "reverb|reverbe|réverbération|reverberation", "espace salle profond queue shimmer",
              "La Réverb met le son dans un espace profond et vivant, avec un Shimmer qui ajoute des octaves brillantes et des temps calables sur le tempo." },
            { "Stéréo", "stereo|largeur|elargisseur|imager", "largeur placement haas mono balance",
              "Stéréo règle la largeur et le placement : Largeur, effet Haas, Graves mono et Balance." },
            { "Ralenti", "half time|halftime|demi vitesse", "ralentir groove moitie vitesse",
              "Ralenti ralentit le groove sans ralentir le projet : il rejoue le son à moitié vitesse (ou 2/3, 3/4, 1/4) sur une boucle calée sur FL." },
            { "Shape", "decoupe|temps et volume|gross beat|grossbeat|shaper|sidechain", "gross beat halftime reverse stutter tape stop volume sidechain slot",
              "Shape rejoue le passé (halftime, reverse, stutter, tape stop…) et sculpte le volume (sidechain, gate…). Choisis un slot temps et un slot volume, ou dessine les tiens dans l'éditeur (onglet TEMPS / VOLUME)." },
            { "Glitch", "detraqueur|digitalis|stutter", "glitch grille pistes pas repete reverse tape stop pitch crush gate",
              "Glitch : 6 pistes d'effets (Répète, Reverse, Tape stop, Pitch, Crush, Gate) sur 16 pas. Clique ou glisse sur les cases pour dessiner ton motif. Boutons Vider, Hasard et Contretemps sous la grille." },
            { "Prisme", "chroma|harmoniseur|xynth|chroma", "reaccorde gamme frequences note juste resonance",
              "Prisme réaccorde n'importe quel son sur ta gamme : les fréquences hors gamme vont sur la note juste la plus proche. En « Notes jouées (MIDI) », il suit les accords que tu joues." },
            { "Nappe", "pad|nappe", "pad gel montee chœur choeur espace ambient",
              "Nappe transforme n'importe quel one-shot en pad : montée douce, gel, chœur et grande réverb, 6 styles." },
            { "Wobble", "remous|liquide|liquid|shaperbox liquid", "courbe dessiner flanger chorus phaser mouvement",
              "Wobble : dessine une courbe (1 à 4 mesures, calée sur FL) qui fait bouger un flanger, un chorus ou un phaser. Clic = point, glisser = déplacer, clic droit = supprimer." },
            { "Bruit", "noise|souffle|vinyle", "souffle blanc rose vinyle bande radio craquements",
              "Bruit ajoute du souffle (blanc, rose, vinyle, bande, radio) qui suit le son ou pulse en rythme, avec des craquements de vinyle." },
        };
        return f;
    }

    inline const std::vector<FxKnob>& fxKnobs()
    {
        static const std::vector<FxKnob> k {
            // Saturation
            { "Saturation", "Modèle", "type de saturation", "le type de saturation : Lampe (douce), Transfo (gras dans les graves), Console (subtil), Bande (compressé, vintage) ou Fuzz (sale)." },
            { "Saturation", "Drive", "gain|quantite", "la quantité de saturation : plus haut = plus chaud, puis plus sale." },
            { "Saturation", "Ton", "brillance|aigus", "la couleur après la saturation : à gauche plus sombre, à droite plus brillant." },
            { "Saturation", "Coupe-bas", "coupe bas|low cut", "enlève des graves avant la saturation pour qu'elle ne bave pas dans les basses." },
            { "Saturation", "Dureté", "durete|agressivite", "la dureté de l'écrêtage : bas = rond, haut = agressif." },
            { "Saturation", "Sortie", "volume", "le volume en sortie de la saturation (pour compenser le Drive)." },
            // Filtre
            { "Filtre", "Type", "type de filtre", "le type de filtre : Passe-bas (garde les graves), Passe-haut (garde les aigus), Passe-bande, Coupe-bande ou Ladder 24 dB (gras, façon Moog)." },
            { "Filtre", "Fréquence", "frequence|cutoff|coupure", "la fréquence de coupure : où le filtre commence à couper." },
            { "Filtre", "Résonance", "resonance", "accentue la zone autour de la coupure : haut = son qui siffle, façon acid 303." },
            { "Filtre", "Drive", "saturation", "sature le son dans le filtre, pour plus de grain." },
            { "Filtre", "Mouvement", "modulation|balayage", "à quel point la fréquence bouge toute seule en rythme (0 = fixe)." },
            { "Filtre", "Vitesse", "rate", "la vitesse du mouvement, calée sur le tempo de FL." },
            // OTT
            { "Compressor", "Profondeur", "depth|quantite", "la quantité de Compressor : c'est le potard principal." },
            { "Compressor", "Vitesse", "temps|attaque relache", "la vitesse de la compression : Rapide (plus agressif), Normale ou Lente (plus doux)." },
            { "Compressor", "Écrase (haut)", "ecrase|haut|compression haut", "calme les sons forts (compression vers le bas)." },
            { "Compressor", "Remonte (bas)", "remonte|bas|compression bas", "fait ressortir les détails faibles (compression vers le haut)." },
            { "Compressor", "Graves", "bande grave", "le niveau de la bande grave (-12 à +12 dB)." },
            { "Compressor", "Médiums", "mediums|bande medium", "le niveau de la bande médium." },
            { "Compressor", "Aigus", "bande aigue", "le niveau de la bande aiguë." },
            { "Compressor", "Sortie", "volume", "le volume en sortie de Compressor." },
            // Égaliseur
            { "Égaliseur", "Sortie", "volume", "le volume après l'égaliseur." },
            // Chorus
            { "Chorus", "Mode", "type", "le caractère : I, II, I + II (façon Dimension) ou Ensemble (plus riche)." },
            { "Chorus", "Vitesse", "rate", "la vitesse de l'ondulation." },
            { "Chorus", "Profondeur", "depth", "l'amplitude de l'ondulation : plus haut = plus de mouvement." },
            { "Chorus", "Ton", "brillance", "la couleur du son traité (plus sombre ou plus brillant)." },
            { "Chorus", "Largeur", "stereo", "l'écartement stéréo du chorus." },
            { "Chorus", "Retard", "delay", "le temps de base du chorus : court = serré, long = plus doublé." },
            // Flanger
            { "Flanger", "Vitesse", "rate", "la vitesse du balayage." },
            { "Flanger", "Profondeur", "depth", "l'amplitude du balayage." },
            { "Flanger", "Réinjection", "feedback|reinjection", "renvoie le son dans le flanger : plus haut = plus métallique." },
            { "Flanger", "Retard", "delay", "le temps de base : change la note métallique du flanger." },
            { "Flanger", "Stéréo", "stereo|largeur", "décale gauche et droite pour un flanger plus large." },
            { "Flanger", "Through-zero", "through zero|tz", "passe par zéro : le balayage se croise avec le son direct, effet plus creusé." },
            // Phaser
            { "Phaser", "Étages", "etages|stages", "le nombre d'étages (2 à 12) : plus = plus de creux, son plus marqué." },
            { "Phaser", "Vitesse", "rate", "la vitesse du tourbillon." },
            { "Phaser", "Profondeur", "depth", "l'amplitude du tourbillon." },
            { "Phaser", "Réinjection", "feedback|reinjection", "renforce les creux : plus haut = plus résonant." },
            { "Phaser", "Centre", "frequence", "la zone de fréquences où le phaser travaille." },
            { "Phaser", "Stéréo", "stereo|largeur", "décale gauche et droite pour un phaser plus large." },
            // Trémolo
            { "Trémolo", "Vitesse", "rate", "la vitesse de la pulsation." },
            { "Trémolo", "Profondeur", "depth", "à quel point le volume baisse à chaque pulsation." },
            { "Trémolo", "Forme", "onde|shape", "la forme de la pulsation (douce ou hachée)." },
            { "Trémolo", "Auto-pan", "auto pan|pan", "fait pulser gauche / droite au lieu du volume." },
            { "Trémolo", "Synchro", "sync|tempo", "cale la pulsation sur le tempo de FL." },
            // Lo-fi
            { "Lo-fi", "Bits", "bitcrush|resolution", "réduit la résolution : moins de bits = son plus granuleux, façon vieux sampler." },
            { "Lo-fi", "Échantillon.", "echantillonnage|sample rate|downsample", "réduit la fréquence d'échantillonnage : son plus métallique, aliasing façon SP-1200." },
            { "Lo-fi", "Wow", "wow|pleurage", "fait onduler la hauteur lentement, comme une cassette qui tourne mal." },
            { "Lo-fi", "Flutter", "flutter|scintillement", "petites variations rapides de hauteur, comme une bande usée." },
            { "Lo-fi", "Ton", "brillance|sombre", "la couleur : à gauche plus étouffé (vieux), à droite plus clair." },
            { "Lo-fi", "Saturation", "drive", "un peu de saturation de bande." },
            // Portal
            { "Grain", "Grains", "taille des grains", "la taille des grains (10 à 500 ms) : petits = texture, grands = échos." },
            { "Grain", "Densité", "densite", "combien de grains en même temps." },
            { "Grain", "Dispersion", "dispersion|hasard", "à quel point les grains sont pris au hasard dans le passé." },
            { "Grain", "Hauteurs", "pitch|transposition", "les grains sont transposés (octaves, quintes) : plus haut = plus de hauteurs différentes." },
            { "Grain", "Inversion", "reverse|envers", "la part de grains joués à l'envers." },
            { "Grain", "Réinjection", "feedback|reinjection", "renvoie les grains dans Grain : nuages qui durent plus longtemps." },
            // Cristal
            { "Givre", "Temps", "temps", "le temps des échos (40 ms à 1,5 s)." },
            { "Givre", "Hauteur", "pitch|transposition", "la transposition des échos : -12, -7, -5, 0, +5, +7, +12, +19 ou +24 demi-tons." },
            { "Givre", "Réinjection", "feedback|reinjection", "combien d'échos se répètent." },
            { "Givre", "Inversion", "reverse|envers", "la part d'échos inversés." },
            { "Givre", "Largeur", "stereo", "l'écartement stéréo des échos." },
            { "Givre", "Ton", "filtre|brillance", "coupe les aigus des échos (plus sombre à gauche)." },
            // Delay
            { "Delay", "Temps", "temps|time", "le temps entre les échos, calé sur le tempo (1/4, 1/8, 1/8 pointé…)." },
            { "Delay", "Réinjection", "feedback|reinjection|repetitions", "combien d'échos : bas = 1 ou 2 répétitions, haut = ça répète longtemps." },
            { "Delay", "Ping-pong", "ping pong|gauche droite", "les échos rebondissent gauche / droite." },
            { "Delay", "Ton", "filtre|brillance", "la couleur des échos : à gauche plus sombres (ils se fondent derrière)." },
            { "Delay", "Usure", "bande|wow|vieux", "l'usure de la bande : les échos ondulent et s'abîment, façon delay à bande." },
            { "Delay", "Diffusion", "diffus|flou", "rend les échos plus flous, entre delay et réverb." },
            { "Delay", "Ducking", "ducking|sidechain", "baisse les échos pendant que le son joue, et les laisse revenir dans les trous : un delay qui ne mange pas ton son." },
            // Réverb
            { "Réverb", "Durée", "longueur|decay|taille", "la longueur de la queue de réverb." },
            { "Réverb", "Pré-délai", "predelay|pre delay", "l'attente avant que la réverb arrive : le son direct reste net devant." },
            { "Réverb", "Amorti", "damping|amortissement", "coupe les aigus de la queue : haut = réverb plus sombre et douce." },
            { "Réverb", "Modulation", "mouvement", "fait onduler la queue pour une réverb plus vivante, moins métallique." },
            { "Réverb", "Shimmer", "shimmer|octave brillante", "ajoute des octaves brillantes dans la queue : son céleste." },
            { "Réverb", "Espace", "taille|piece", "la taille de l'espace (petite pièce → grande salle)." },
            { "Réverb", "Pré-délai tempo", "predelay tempo", "cale le pré-délai sur le tempo de FL." },
            { "Réverb", "Durée tempo", "duree tempo", "cale la durée de la queue sur le tempo : Libre, 1/2 mesure, 1, 2 ou 4 mesures." },
            // Stéréo
            { "Stéréo", "Largeur", "stereo|large", "la largeur : 0 % = mono, 100 % = normal, 200 % = très large." },
            { "Stéréo", "Haas", "haas|decalage", "décale un côté de quelques ms : élargit un son mono." },
            { "Stéréo", "Graves mono", "basses mono|mono bass", "met les graves en mono sous la fréquence choisie : basses solides même en élargissant." },
            { "Stéréo", "Balance", "pan|gauche droite", "déplace le son à gauche ou à droite." },
            // Halftime
            { "Ralenti", "Vitesse", "ratio|moitie", "la vitesse de relecture : 1/2, 2/3, 3/4, 1/4…" },
            { "Ralenti", "Boucle", "longueur|taille", "la longueur de la boucle relue." },
            { "Ralenti", "Mode", "bande|garde la hauteur", "Bande (ralentit et descend en grave, comme une cassette) ou Garde la hauteur." },
            { "Ralenti", "Fondu", "fade|clic", "le fondu aux jonctions de boucle (enlève les clics)." },
            { "Ralenti", "Inversion", "reverse|envers", "la chance qu'un passage soit joué à l'envers." },
            { "Ralenti", "Couleur", "filtre|brillance", "coupe les aigus du son ralenti." },
            { "Ralenti", "Wobble", "wobble|ondulation", "fait onduler la hauteur, façon bande." },
            { "Ralenti", "Variation", "hasard", "casse la routine : la boucle change un peu à chaque fois." },
            // Temps & Volume
            { "Shape", "Slot temps", "slot temps|temps", "le motif de temps : Normal, Halftime, Reverse, Stutter 1/4, 1/8, 1/16, Tape stop, Tape start, Rewind, Triolets, Scratch… ou Perso (dessiné)." },
            { "Shape", "Slot volume", "slot volume|volume", "le motif de volume : Sidechain, Pompe douce, Gate 1/2, Trance gate, Rampes, Stutter, Syncopé… ou Perso." },
            { "Shape", "Mix temps", "dosage temps", "le dosage de l'effet de temps." },
            { "Shape", "Mix volume", "dosage volume", "le dosage de l'effet de volume." },
            { "Shape", "Cycle", "longueur|mesures", "la longueur du motif (en temps ou mesures)." },
            { "Shape", "Lissage", "clic|smooth", "adoucit les changements brusques (enlève les clics)." },
            { "Shape", "Filtre", "filtre", "le motif de volume pilote aussi un filtre." },
            { "Shape", "Pan", "pan", "le motif de volume pilote aussi le pan." },
            // Glitch
            { "Glitch", "Motif", "preset|pattern", "le motif de la grille : Bégaiement, IDM, Reverse fin, Tape stop fin, Hachoir, Vocal chop… ou Perso." },
            { "Glitch", "Grille", "division|pas", "la taille d'un pas de la grille (1/8, 1/16, 1/32…)." },
            { "Glitch", "Répétition", "repetition|stutter taille", "la taille du morceau répété par la piste Répète." },
            { "Glitch", "Pitch", "hauteur|transpo", "la transposition de la piste Pitch (-12 à +12 demi-tons)." },
            { "Glitch", "Crush", "bitcrush|destroy", "l'intensité de la piste Crush." },
            { "Glitch", "Gate", "coupe|silence", "l'intensité de la piste Gate (coupe le son)." },
            { "Glitch", "Lissage", "clic|smooth", "adoucit les coupures pour éviter les clics." },
            { "Glitch", "Hasard", "aleatoire|surprise", "ajoute des surprises au hasard dans le motif." },
            // Chroma
            { "Prisme", "Tonalité", "key|tonique", "la tonalité de recalage (ou celle du plugin)." },
            { "Prisme", "Gamme", "scale", "la gamme de recalage : Gamme du plugin, Majeur, Mineur, Pentas, Dorien, accords, Quintes, Notes jouées (MIDI)…" },
            { "Prisme", "Couleur", "resonance|chant", "à quel point les notes de la gamme résonnent (0 à 200 %) : plus haut = son plus chantant." },
            { "Prisme", "Correction", "force|recalage", "la force du recalage : 100 % = tout va sur les notes justes." },
            { "Prisme", "Focus", "precision", "la précision : haut = notes très nettes, bas = plus doux et naturel." },
            { "Prisme", "Décalage", "pre shift|transpo", "décale le son avant le recalage (-12 à +12 demi-tons)." },
            { "Prisme", "Mode", "qualite|latence", "Zéro latence, Normal ou Haute qualité (plus lourd)." },
            { "Prisme", "Brillance", "aigus", "plus sombre ou plus brillant après le recalage." },
            // Nappe
            { "Nappe", "Style", "type", "le style : Nappe douce, Cordes, Chœur, Glace (shimmer), Sombre ou Granulaire." },
            { "Nappe", "Taille", "espace", "la taille de l'espace autour du pad." },
            { "Nappe", "Montée", "attaque|fade in", "le temps de montée du pad (30 ms à 3 s)." },
            { "Nappe", "Gel", "freeze|figer", "fige le son pour qu'il tienne comme une note infinie." },
            { "Nappe", "Brillance", "aigus|sombre", "plus sombre ou plus brillant." },
            { "Nappe", "Mouvement", "modulation", "fait bouger le pad lentement." },
            { "Nappe", "Octave", "octave", "ajoute une octave : -1, aucune, +1, ou -1 et +1." },
            { "Nappe", "Largeur", "stereo", "l'écartement stéréo du pad." },
            // Liquide
            { "Wobble", "Type", "flanger chorus phaser", "l'effet que la courbe fait bouger : flanger, chorus ou phaser." },
            { "Wobble", "Longueur", "mesures|cycle", "la longueur de la courbe : 1, 2 ou 4 mesures, calée sur FL." },
            { "Wobble", "Centre", "frequence", "le point central de l'effet." },
            { "Wobble", "Profondeur", "depth", "l'amplitude du mouvement." },
            { "Wobble", "Stéréo", "stereo|largeur", "décale gauche et droite." },
            { "Wobble", "Feedback", "reinjection", "renforce l'effet : plus métallique." },
            { "Wobble", "Lissage", "smooth", "adoucit la courbe." },
            { "Wobble", "Courbe", "forme|preset", "une forme de courbe toute prête (ou dessine la tienne)." },
            // Bruit
            { "Bruit", "Type", "blanc rose vinyle bande radio", "le type de bruit : blanc, rose, vinyle, bande ou radio." },
            { "Bruit", "Volume", "niveau", "le volume du bruit (-60 à -6 dB)." },
            { "Bruit", "Ton", "brillance|sombre", "plus sombre ou plus brillant." },
            { "Bruit", "Suit le son", "suit|envelope|follow", "le bruit monte et descend avec ton son (au lieu d'être constant)." },
            { "Bruit", "Largeur", "stereo", "l'écartement stéréo du bruit." },
            { "Bruit", "Craquements", "crackle|vinyle", "les craquements de vinyle." },
            { "Bruit", "Rythme", "pulse|tempo", "fait pulser le bruit en rythme." },
        };
        return k;
    }
}
