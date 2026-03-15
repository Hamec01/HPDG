cmake_minimum_required(VERSION 3.22)

project(HPDG VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(JUCE_TARGET_ARCHITECTURE "x86_64")

# Point this to your JUCE checkout if not using FetchContent.
# add_subdirectory("C:/JUCE" JUCE)

add_subdirectory(JUCE)

juce_add_plugin(HPDG
    COMPANY_NAME "BoomBap Labs"
    BUNDLE_ID com.boombaplabs.boombapgenerator
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE BBLB
    PLUGIN_CODE BBG1
    FORMATS VST3 Standalone
    PRODUCT_NAME "HPDG"
)

if (TARGET HPDG_VST3)
    target_compile_definitions(HPDG_VST3 PRIVATE
        JUCE_VST3_CAN_REPLACE_VST2=0
    )
endif()

target_sources(HPDG
    PRIVATE
        Source/Plugin/PluginProcessor.cpp
        Source/Plugin/PluginEditor.cpp
    Source/Core/TrackRegistry.cpp
        Source/Core/PatternProjectSerialization.cpp
        Source/Engine/BoomBapEngine.cpp
    Source/Engine/RapEngine.cpp
    Source/Engine/TrapEngine.cpp
    Source/Engine/DrillEngine.cpp
    Source/Engine/StyleDefaults.cpp
    Source/Engine/GrooveEngine.cpp
    Source/Engine/VelocityEngine.cpp
    Source/Engine/HumanizeEngine.cpp
    Source/Engine/MidiExportEngine.cpp
    Source/Engine/PreviewEngine.cpp
    Source/Engine/SampleLibraryManager.cpp
    Source/Engine/LaneSampleBank.cpp
    Source/Engine/BoomBap/BoomBapPatternLibrary.cpp
    Source/Engine/BoomBap/BoomBapPhrasePlanner.cpp
    Source/Engine/BoomBap/BoomBapStyleProfile.cpp
        Source/Engine/BoomBap/BoomBapKickGenerator.cpp
        Source/Engine/BoomBap/BoomBapSnareGenerator.cpp
        Source/Engine/BoomBap/BoomBapHatGenerator.cpp
    Source/Engine/BoomBap/BoomBapGhostGenerator.cpp
    Source/Engine/BoomBap/BoomBapOpenHatGenerator.cpp
    Source/Engine/BoomBap/BoomBapPercGenerator.cpp
    Source/Engine/Rap/RapStyleProfile.cpp
    Source/Engine/Rap/RapPhrasePlanner.cpp
    Source/Engine/Rap/RapKickGenerator.cpp
    Source/Engine/Rap/RapSnareGenerator.cpp
    Source/Engine/Rap/RapHatGenerator.cpp
    Source/Engine/Trap/TrapStyleProfile.cpp
    Source/Engine/Trap/TrapPhrasePlanner.cpp
    Source/Engine/Trap/TrapKickGenerator.cpp
    Source/Engine/Trap/TrapSnareGenerator.cpp
    Source/Engine/Trap/TrapHatGenerator.cpp
    Source/Engine/Trap/TrapHatFxGenerator.cpp
    Source/Engine/Trap/Trap808Generator.cpp
    Source/Engine/Drill/DrillStyleProfile.cpp
    Source/Engine/Drill/DrillPhrasePlanner.cpp
    Source/Engine/Drill/DrillKickGenerator.cpp
    Source/Engine/Drill/DrillSnareGenerator.cpp
    Source/Engine/Drill/DrillHatGenerator.cpp
    Source/Engine/Drill/DrillHatFxGenerator.cpp
    Source/Engine/Drill/Drill808Generator.cpp
    Source/Services/TemporaryMidiExportService.cpp
        Source/UI/MainHeaderComponent.cpp
        Source/UI/TrackRowComponent.cpp
        Source/UI/TrackListComponent.cpp
        Source/UI/GridEditorComponent.cpp
)

target_include_directories(HPDG
    PRIVATE
        Source
)

target_link_libraries(HPDG
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
