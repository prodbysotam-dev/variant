@echo off
REM Compile le plugin Variant (VST3 + version autonome + tests)
REM Prérequis : Visual Studio 2022 Build Tools (C++) et CMake
REM Le dossier de build est court (limite de 260 caractères de MSBuild).
cd /d "%~dp0"
set B=%LOCALAPPDATA%\VariantBuild
REM force la recompilation des sources (le suivi des en-têtes ne marche pas avec un chemin long)
powershell -NoProfile -Command "Get-ChildItem Source\*.cpp, Tests\*.cpp | ForEach-Object { $_.LastWriteTime = Get-Date }"
cmake -S . -B "%B%" -G "Visual Studio 17 2022" -A x64 || goto :error
cmake --build "%B%" --config Release --target VariantTests Variant_VST3 Variant_Standalone || goto :error
"%B%\VariantTests_artefacts\Release\VariantTests.exe" || goto :error
echo.
echo OK : %B%\Variant_artefacts\Release\VST3\Variant.vst3
echo Copie ce dossier dans C:\Program Files\Common Files\VST3 puis rescanne les plugins dans FL Studio.
goto :eof
:error
echo La compilation ou les tests ont echoue.
exit /b 1
