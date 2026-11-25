cmake_minimum_required(VERSION 3.22)

# Mac : un seul binaire pour les Mac Apple (M1…) et Intel, macOS 10.13 minimum (ignoré sur Windows)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")

project (Variant VERSION 3.28.0)

# version publique (celle qu on envoie aux gens) : sans les outils du créateur (statistiques, bannir, messages)
option(VARIANT_PUBLIC "Version publique sans outils createur" OFF)

# formats : VST3 (FL, Ableton, Cubase, Bitwig…) partout, AU en plus sur Mac (Logic Pro, GarageBand)
if(APPLE)
    set(VARIANT_FORMATS AU VST3 Standalone)
    set(VARIANT_FX_FORMATS AU VST3)
else()
    set(VARIANT_FORMATS VST3 Standalone)
    set(VARIANT_FX_FORMATS VST3)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE est téléchargé automatiquement au premier "cmake -B build"
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Variant
    COMPANY_NAME                "Variant Audio"
    PRODUCT_NAME                "Variant"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    PLUGIN_MANUFACTURER_CODE    Vrnt
    PLUGIN_CODE                 Vos1
    VST3_CATEGORIES             Instrument Sampler
    BUNDLE_ID                   com.variantaudio.variant
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
    FORMATS                     ${VARIANT_FORMATS})

target_sources(Variant PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/WheelHook.cpp)

target_compile_definitions(Variant PUBLIC
    JUCE_WEB_BROWSER=0
    VARIANT_PUBLIC=$<BOOL:${VARIANT_PUBLIC}>
    JUCE_PLUGINHOST_VST3=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_MP3AUDIOFORMAT=1)

if(MSVC)
    target_compile_options(Variant PRIVATE /utf-8)
endif()
if(APPLE)
    target_compile_definitions(Variant PUBLIC JUCE_PLUGINHOST_AU=1)   # les Audio Units s'ouvrent dans Variant
endif()

# Tests des moteurs (programme console)
juce_add_console_app(VariantTests PRODUCT_NAME "VariantTests")
target_sources(VariantTests PRIVATE Tests/EngineTests.cpp)
target_compile_definitions(VariantTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
if(MSVC)
    target_compile_options(VariantTests PRIVATE /utf-8)
endif()
target_link_libraries(VariantTests PRIVATE juce::juce_audio_basics juce::juce_audio_formats juce::juce_audio_processors juce::juce_dsp juce::juce_core juce::juce_recommended_config_flags)

target_link_libraries(Variant
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Un plugin d'effet par effet de Variant (pour le mixer de FL) : même code, mode « effet seul »
function(variant_single_fx target product code type)
    juce_add_plugin(${target}
        COMPANY_NAME                "Variant Audio"
        PRODUCT_NAME                "${product}"
        IS_SYNTH                    FALSE
        NEEDS_MIDI_INPUT            TRUE
        NEEDS_MIDI_OUTPUT           FALSE
        IS_MIDI_EFFECT              FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        COPY_PLUGIN_AFTER_BUILD     FALSE
        PLUGIN_MANUFACTURER_CODE    Vrnt
        PLUGIN_CODE                 ${code}
        VST3_CATEGORIES             Fx
        BUNDLE_ID                   com.variantaudio.${code}
        FORMATS                     ${VARIANT_FX_FORMATS})
    target_sources(${target} PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/WheelHook.cpp)
    target_compile_definitions(${target} PUBLIC
        VARIANT_FX_ONLY=1 VARIANT_SINGLE_FX=${type} VARIANT_PUBLIC=$<BOOL:${VARIANT_PUBLIC}>
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_USE_MP3AUDIOFORMAT=1)
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8 /MP)
    endif()
    target_link_libraries(${target}
        PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_audio_processors juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
endfunction()

variant_single_fx(VSaturation "Variant Saturation" Vsat 1)
variant_single_fx(VFilter     "Variant Filtre"     Vflt 2)
variant_single_fx(VOtt        "Variant Compressor" Vott 3)
variant_single_fx(VEq         "Variant EQ"         Veq1 4)
variant_single_fx(VChorus     "Variant Chorus"     Vchr 5)
variant_single_fx(VFlanger    "Variant Flanger"    Vfln 6)
variant_single_fx(VPhaser     "Variant Phaser"     Vphs 7)
variant_single_fx(VTremolo    "Variant Tremolo"    Vtrm 8)
variant_single_fx(VLofi       "Variant Lo-fi"      Vlof 9)
variant_single_fx(VPortal     "Variant Grain"      Vprt 10)
variant_single_fx(VCrystal    "Variant Givre"      Vcry 11)
variant_single_fx(VDelay      "Variant Delay"      Vdly 12)
variant_single_fx(VReverb     "Variant Reverb"     Vrvb 13)
variant_single_fx(VStereo     "Variant Stereo"     Vste 14)
variant_single_fx(VHalftime   "Variant Ralenti"    Vhlf 15)
variant_single_fx(VGross      "Variant Shape"      Vgrs 16)
variant_single_fx(VGlitch     "Variant Glitch"     Vglt 17)
variant_single_fx(VChroma     "Variant Prisme"     Vchm 18)
variant_single_fx(VNappe      "Variant Nappe"      Vnap 19)
variant_single_fx(VLiquide    "Variant Wobble"     Vliq 20)
variant_single_fx(VBruit      "Variant Bruit"      Vbrt 21)

# Installateur Windows : « Installer Variant.exe » (les plugins sont collés à la fin par tools/make_installer.ps1)
if(WIN32)
    juce_add_gui_app(VariantInstaller PRODUCT_NAME "Installer Variant" COMPANY_NAME "Variant Audio")
    target_sources(VariantInstaller PRIVATE Installer/InstallerApp.cpp)
    target_compile_definitions(VariantInstaller PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_APPLICATION_NAME_STRING="Installer Variant" JUCE_APPLICATION_VERSION_STRING="1.0")
    target_compile_options(VariantInstaller PRIVATE /utf-8)
    # demande les droits administrateur au lancement (écrire dans Program Files)
    target_link_options(VariantInstaller PRIVATE "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
    target_link_libraries(VariantInstaller PRIVATE juce::juce_gui_basics juce::juce_recommended_config_flags)
endif()

# Outil de test : charge les plugins compilés comme un logiciel de musique (son + fenêtre)
juce_add_console_app(VariantHostCheck PRODUCT_NAME "VariantHostCheck")
target_sources(VariantHostCheck PRIVATE tools/HostCheck.cpp)
target_compile_definitions(VariantHostCheck PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=$<BOOL:${APPLE}>)
if(MSVC)
    target_compile_options(VariantHostCheck PRIVATE /utf-8)
endif()
target_link_libraries(VariantHostCheck PRIVATE juce::juce_audio_processors juce::juce_audio_utils juce::juce_gui_basics juce::juce_recommended_config_flags)
