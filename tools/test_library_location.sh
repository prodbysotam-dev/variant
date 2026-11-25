#!/bin/bash
# Vérifie que Variant retrouve sa bibliothèque où qu'elle soit (PC et Mac).
# Utilisation : tools/test_library_location.sh <chemin de VariantTests>
set -u
T="$1"; R=$(mktemp -d); fails=0
mklib() { mkdir -p "$1/Variant Keys" "$1/Variant Pad"; printf 'RIFF' > "$1/Variant Keys/Test Key.wav"; printf 'RIFF' > "$1/Variant Pad/Test Pad.wav"; touch "$1/.variant-cree"; }
check() {   # $1 = profil, $2 = attendu, $3 = nom du cas
    prof="$(basename "$1")"
    got=$(VARIANT_TEST_PROFILE="$1" VARIANT_LIBTEST=1 "$T" | sed -n 's/^BIBLIOTHEQUE=//p' | tr -d '\r')
    got="${got//\\//}"; got="${got#*/$prof/}"          # chemin à l'intérieur du faux profil
    want="${2//\\//}"; want="${want#*/$prof/}"
    if [ -n "$got" ] && [ "$got" = "$want" ]; then echo "ok   : $3 ($got)"; else echo "ECHEC: $3 (trouve « $got », attendu « $want »)"; fails=$((fails+1)); fi
}
P1="$R/p1"; mklib "$P1/Documents/Variant Bibliothèque @prodbysotam"; check "$P1" "$P1/Documents/Variant Bibliothèque @prodbysotam" "dans Documents"
P2="$R/p2"; mklib "$P2/Disque D/Projets/Musique/Mes sons Variant"; check "$P2" "$P2/Disque D/Projets/Musique/Mes sons Variant" "renommée et rangée ailleurs (sans indice)"
P3="$R/p3"; L3="$P3/X/a/b/c/d/e/f/Variant Bibliothèque @prodbysotam"; mklib "$L3"; mkdir -p "$P3/AppData/Variant"; H="$L3"; command -v cygpath >/dev/null && H=$(cygpath -w "$L3"); printf "%s" "$H" > "$P3/AppData/Variant/library.txt"
check "$P3" "$L3" "très loin, notée par l'installateur"
P4="$R/p4"; mklib "$P4/Desktop/Variant PC/Variant Bibliothèque @prodbysotam"; check "$P4" "$P4/Desktop/Variant PC/Variant Bibliothèque @prodbysotam" "laissée dans le dossier décompressé"
rm -rf "$R"
echo "$fails echec(s)"; exit $fails
