#!/bin/bash
# Installe Variant sur ce Mac : double-clique ce fichier (la première fois : clic droit > Ouvrir).
# - repère les logiciels de musique installés (Logic, GarageBand, Ableton, FL Studio, Cubase, Studio One, REAPER, Bitwig…)
# - plugins AU (.component) -> ~/Library/Audio/Plug-Ins/Components   (Logic Pro, GarageBand, Ableton, Studio One…)
# - plugins VST3 (.vst3)    -> ~/Library/Audio/Plug-Ins/VST3         (Ableton, FL Studio, Cubase, REAPER, Bitwig…)
#   + un dossier de plugins perso si tu en as un
# - Variant.app             -> /Applications (version autonome)
# - la bibliothèque de sons livrée à côté -> ~/Documents (ou ailleurs) ; son emplacement est noté pour Variant
# Tests : --silent (aucune question), VARIANT_LIB_DEST=<dossier> pour choisir où va la bibliothèque.
cd "$(dirname "$0")"
set -e
SILENT=0; [ "${1:-}" = "--silent" ] && SILENT=1
COMP="$HOME/Library/Audio/Plug-Ins/Components"
VST3="$HOME/Library/Audio/Plug-Ins/VST3"
mkdir -p "$COMP" "$VST3"

echo "=== Variant - @prodbysotam ==="
echo "Logiciels de musique trouvés :"
found=0
for a in /Applications/*.app /Applications/*/*.app; do
    [ -e "$a" ] || continue
    n=$(basename "$a" .app)
    case "$n" in
        "Pro Tools"*) echo "  ✖ $n : n'ouvre que les plugins AAX, Variant ne peut pas y être utilisé"; found=1 ;;
        Logic*|GarageBand*) echo "  ✔ $n : version AU"; found=1 ;;
        "Ableton Live"*|"FL Studio"*|Cubase*|Nuendo*|"Studio One"*|REAPER*|"Bitwig Studio"*|Reason*|"Studio Pro"*) echo "  ✔ $n : version VST3 (ou AU)"; found=1 ;;
    esac
done
[ $found = 1 ] || echo "  (aucun trouvé : Variant va dans les dossiers standard, lus par tous les logiciels)"

# anciens effets renommés (sinon ils apparaissent en double) : mis de côté, pas supprimés
OLD="$HOME/Library/Audio/Plug-Ins/Variant anciens effets"
for n in "Variant OTT" "Variant Pression" "Variant Portal" "Variant Nuage" "Variant Crystal" "Variant Halftime" "Variant Gross" \
         "Variant Decoupe" "Variant Detraqueur" "Variant Chroma" "Variant Liquide" "Variant Remous"; do
    for d in "$COMP/$n.component" "$VST3/$n.vst3"; do
        if [ -e "$d" ]; then mkdir -p "$OLD"; rm -rf "$OLD/$(basename "$d")"; mv "$d" "$OLD/"; fi
    done
done

EXTRA=""
if [ $SILENT = 0 ]; then
    echo ""
    echo "Les plugins vont dans les dossiers standard (lus par tous les logiciels)."
    read -r -p "Tu as un dossier VST3 perso en plus ? Glisse-le ici puis Entrée (sinon juste Entrée) : " EXTRA || true
    EXTRA="${EXTRA%\'}"; EXTRA="${EXTRA#\'}"; EXTRA="$(echo "$EXTRA" | sed 's/\ / /g; s/[[:space:]]*$//')"
fi

n=0
for p in *.component; do [ -e "$p" ] || continue; rm -rf "$COMP/$p"; cp -R "$p" "$COMP/"; xattr -cr "$COMP/$p" 2>/dev/null || true; n=$((n+1)); done
for p in *.vst3; do
    [ -e "$p" ] || continue
    rm -rf "$VST3/$p"; cp -R "$p" "$VST3/"; xattr -cr "$VST3/$p" 2>/dev/null || true; n=$((n+1))
    if [ -n "$EXTRA" ] && [ -d "$EXTRA" ]; then rm -rf "$EXTRA/$p"; cp -R "$p" "$EXTRA/"; xattr -cr "$EXTRA/$p" 2>/dev/null || true; fi
done
if [ -d Variant.app ]; then
    rm -rf "/Applications/Variant.app" 2>/dev/null || true
    cp -R Variant.app /Applications/ 2>/dev/null && xattr -cr /Applications/Variant.app 2>/dev/null || true
fi
echo "$n plugins installés."

# la bibliothèque : « Variant Bibliothèque… » à côté de l'installateur (ou dans le dossier au-dessus)
SRC=""
for d in ./Variant\ Biblioth* ../Variant\ Biblioth*; do [ -d "$d" ] && SRC="$d" && break; done
if [ -n "$SRC" ]; then
    DEST_PARENT="${VARIANT_LIB_DEST:-$HOME/Documents}"
    DST="$DEST_PARENT/$(basename "$SRC")"
    echo "Copie de la bibliothèque dans $DST (environ 3 Go, quelques minutes)…"
    mkdir -p "$DST"
    rsync -a "$SRC/" "$DST/"
    xattr -cr "$DST" 2>/dev/null || true
    mkdir -p "$HOME/Library/Variant"
    printf '%s' "$DST" > "$HOME/Library/Variant/library.txt"   # Variant lit ce fichier pour trouver ses sons
    echo "Bibliothèque copiée."
else
    echo "(Bibliothèque pas trouvée à côté de l'installateur : Variant la cherchera tout seul sur ton Mac.)"
fi

killall -9 AudioComponentRegistrar 2>/dev/null || true   # Logic relit la liste des AU
echo ""
echo "Variant est installé."
echo "  Logic Pro / GarageBand : Instrument > AU Instruments > Variant Audio > Variant."
echo "  Ableton : Réglages > Plug-ins > « Use VST3 Plug-in System Folders » puis Rescan."
echo "  FL Studio : Options > Manage plugins > Find more plugins."
echo ""
[ $SILENT = 1 ] || read -n 1 -s -r -p "Appuie sur une touche pour fermer."
