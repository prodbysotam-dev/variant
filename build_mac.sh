#!/bin/bash
# Compile Variant pour Mac : AU (Logic Pro, GarageBand), VST3 (Ableton, FL Mac, Cubase, Bitwig…), version autonome
# et les 21 effets séparés (AU + VST3). Binaire universel : Mac Apple (M1, M2…) et Mac Intel.
# Version publique (sans les outils du créateur). Prérequis : Xcode et CMake (brew install cmake).
# Utilisation :  ./build_mac.sh            -> compile + tests + paquet dans dist/
#                ./build_mac.sh install    -> pareil, puis installe les plugins sur ce Mac
set -euo pipefail
cd "$(dirname "$0")"
B=build-mac
cmake -S . -B "$B" -G Xcode -DVARIANT_PUBLIC=ON -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=10.13

FX=$(grep -o 'variant_single_fx(V[A-Za-z]*' CMakeLists.txt | sed 's/variant_single_fx(//')
TARGETS="VariantTests VariantHostCheck Variant_AU Variant_VST3 Variant_Standalone"
for t in $FX; do TARGETS="$TARGETS ${t}_AU ${t}_VST3"; done

# journal complet dans build-mac/build.log ; en cas d'erreur, les erreurs sont affichées en clair à la fin
mkdir -p "$B"
for t in $TARGETS; do
    echo "== $t"
    if ! cmake --build "$B" --config Release --target "$t" -- -quiet >> "$B/build.log" 2>&1; then
        echo "=== ERREURS DE COMPILATION ($t) ==="
        sed -e $'s/\x1b\[[0-9;]*m//g' "$B/build.log" | grep -E -B2 -A8 "(error|fatal error):" | head -300
        exit 1
    fi
done
"$B/VariantTests_artefacts/Release/VariantTests"

rm -rf dist && mkdir -p "dist/Variant Plugins Mac"
D="dist/Variant Plugins Mac"
A="$B/Variant_artefacts/Release"
cp -R "$A/AU/Variant.component" "$A/VST3/Variant.vst3" "$A/Standalone/Variant.app" "$D/"
for t in $FX; do
    cp -R "$B/${t}_artefacts/Release/AU/"*.component "$D/"
    cp -R "$B/${t}_artefacts/Release/VST3/"*.vst3 "$D/"
done
# symboles de débogage retirés : plugins beaucoup plus légers (le paquet doit rester petit)
find "$D" -type f -perm -u+x -path "*/Contents/MacOS/*" -exec strip -x {} \; 2>/dev/null || true
# signature « ad hoc » : obligatoire pour qu'un Mac Apple (M1…) accepte de charger les plugins
for p in "$D"/*.component "$D"/*.vst3 "$D"/*.app; do codesign --force --deep --sign - "$p"; done
cp "mac/Installer Variant.command" "$D/"
chmod +x "$D/Installer Variant.command"
cp "$B/VariantHostCheck_artefacts/Release/VariantHostCheck" "$B/VariantTests_artefacts/Release/VariantTests" dist/
(cd dist && ditto -c -k --keepParent "Variant Plugins Mac" "Variant Plugins Mac.zip")
echo "OK : dist/Variant Plugins Mac.zip ($(ls "$D" | wc -l) éléments)"

if [ "${1:-}" = "install" ]; then
    "$D/Installer Variant.command"
fi
