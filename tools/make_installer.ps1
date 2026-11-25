# Fabrique « Installer Variant.exe » : l'installateur + un zip des plugins VST3 collé à la fin.
# Utilisation : powershell -File tools\make_installer.ps1 -Build <dossier de build> -Out <dossier de sortie>
#   -OnlyVariant : seulement Variant (sans les effets séparés)
param([string]$Build = "$env:LOCALAPPDATA\VariantBuild", [string]$Out = ".", [switch]$OnlyVariant)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem

$exe = Join-Path $Build "VariantInstaller_artefacts\Release\Installer Variant.exe"
if (-not (Test-Path -LiteralPath $exe)) { throw "Installateur pas compilé : $exe" }

# les .vst3 aux noms actuels (Variant + effets séparés listés dans CMakeLists.txt) ; un effet pas encore compilé est sauté
$cm = Get-Content -Raw -Encoding UTF8 (Join-Path $PSScriptRoot "..\CMakeLists.txt")
$bundles = @()
$main = Join-Path $Build "Variant_artefacts\Release\VST3\Variant.vst3"
if (Test-Path -LiteralPath $main) { $bundles += Get-Item -LiteralPath $main } else { throw "Variant pas compilé : $main" }
foreach ($m in [regex]::Matches($cm, 'variant_single_fx\(\s*(\w+)\s+"([^"]+)"')) {
    if ($OnlyVariant) { break }
    $p = Join-Path $Build ($m.Groups[1].Value + "_artefacts\Release\VST3\" + $m.Groups[2].Value + ".vst3")
    if (Test-Path -LiteralPath $p) { $bundles += Get-Item -LiteralPath $p } else { Write-Host "  (pas compilé, sauté : $($m.Groups[2].Value))" }
}

$tmp = Join-Path ([IO.Path]::GetTempPath()) ("variant_payload_" + [guid]::NewGuid().ToString("N") + ".zip")
$zip = [IO.Compression.ZipFile]::Open($tmp, "Create")
foreach ($b in $bundles) {
    foreach ($f in Get-ChildItem -LiteralPath $b.FullName -Recurse -File) {
        $rel = $b.Name + "/" + $f.FullName.Substring($b.FullName.Length + 1).Replace("\", "/")
        [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $f.FullName, $rel, "Optimal")
    }
    Write-Host "  + $($b.Name)"
}
$zip.Dispose()

New-Item -ItemType Directory -Force $Out | Out-Null
$dest = Join-Path $Out "Installer Variant.exe"
$o = [IO.File]::Create($dest)
$a = [IO.File]::ReadAllBytes($exe); $o.Write($a, 0, $a.Length)
$z = [IO.File]::ReadAllBytes($tmp); $o.Write($z, 0, $z.Length)
$o.Write([BitConverter]::GetBytes([long]$z.Length), 0, 8)
$mg = [Text.Encoding]::ASCII.GetBytes("VARIANTZ"); $o.Write($mg, 0, 8)
$o.Close()
Remove-Item $tmp
Write-Host "OK : $dest ($([math]::Round((Get-Item -LiteralPath $dest).Length / 1MB, 1)) Mo, $($bundles.Count) plugins)"
