cmake_minimum_required(VERSION 3.22)

project(HPDG VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(JUCE_TARGET_ARCHITECTURE "x86_64")

include(CTest)

# Point this to your JUCE checkout if not using FetchContent.
# add_subdirectory("C:/JUCE" JUCE)

add_subdirectory(JUCE)

juce_add_binary_data(HPDG_FontAssets
    SOURCES
        Assets/Fonts/Neucha.ttf
        Assets/Fonts/OFL.txt
)

juce_add_binary_data(HPDG_ImageAssets
    NAMESPACE ImageBinaryData
    HEADER_NAME ImageAssets.h
    SOURCES
        Assets/Images/Logo.png
)

juce_add_plugin(HPDG
    COMPANY_NAME "HamloProd"
    BUNDLE_ID com.boombaplabs.boombapgenerator
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE BBLB
    PLUGIN_CODE BBG1
    FORMATS Standalone VST3
    PRODUCT_NAME "HPDG"
)

if (TARGET HPDG_VST3)
    target_compile_definitions(HPDG_VST3 PRIVATE
        JUCE_VST3_CAN_REPLACE_VST2=0
    )
endif()

# The sample library sits next to the plugin binary (SampleLibraryManager looks for a
# "Samples" folder beside the executable), so ship it inside the built bundle / app folder.
foreach(hpdg_format_target HPDG_VST3 HPDG_Standalone)
    if (TARGET ${hpdg_format_target})
        add_custom_command(TARGET ${hpdg_format_target} POST_BUILD
            # Replace (not merge): a sample removed from Samples/ must not linger in the bundle.
            COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_FILE_DIR:${hpdg_format_target}>/Samples"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/Samples"
                "$<TARGET_FILE_DIR:${hpdg_format_target}>/Samples"
            VERBATIM)
    endif()
endforeach()

target_sources(HPDG
    PRIVATE
        Source/Plugin/PluginProcessor.cpp
        Source/Plugin/PluginEditor.cpp
        Source/Audio/DrumCompressorBlock.cpp
        Source/Audio/MonstaFxBlock.cpp
    Source/Analysis/SampleAnalyzer.cpp
    Source/Analysis/SampleSourceAudio.cpp
    Source/Analysis/DrumBreakTranscriber.cpp
    Source/Analysis/SampleHarmonyAnalyzer.cpp
    Source/Analysis/SampleLabelReader.cpp
    Source/Analysis/SampleLineTranscriber.cpp
    Source/Analysis/FeatureExtractor.cpp
    Source/Analysis/SampleApplyWeights.cpp
    Source/Core/TrackRegistry.cpp
    Source/Core/RuntimeLaneLifecycle.cpp
        Source/Core/PatternProjectSerialization.cpp
        Source/Engine/BoomBapEngine.cpp
    Source/Engine/DrillEngine.cpp
    Source/Engine/DnBEngine.cpp
    Source/Engine/DnB/DnBStyleProfile.cpp
    Source/Engine/DnB/DnBGrammar.cpp
    Source/Engine/DnB/DnBScorer.cpp
    Source/Engine/DnB/DnBBass.cpp
    Source/Engine/TechnoEngine.cpp
    Source/Engine/Techno/TechnoStyleProfile.cpp
    Source/Engine/Techno/TechnoGrammar.cpp
    Source/Engine/Techno/TechnoBass.cpp
    Source/Engine/Drill/Drill808Generator.cpp
    Source/Engine/Drill/DrillHatFxGenerator.cpp
    Source/Engine/Drill/DrillHatGenerator.cpp
    Source/Engine/Drill/DrillKickGenerator.cpp
    Source/Engine/Drill/DrillPatternValidator.cpp
    Source/Engine/RapEngine.cpp
    Source/Engine/TrapEngine.cpp
    Source/Engine/Drill/DrillPhrasePlanner.cpp
    Source/Engine/Drill/DrillSnareGenerator.cpp
    Source/Engine/StyleDefaults.cpp
    Source/Engine/StyleDefinitionLoader.cpp
    Source/Engine/StyleDefinitionResolver.cpp
    Source/Engine/StyleInfluence.cpp
    Source/Engine/GenerationModel/PatternFeatureVector.cpp
    Source/Engine/GenerationModel/CandidateSelectionEngine.cpp
    Source/Engine/GenerationModel/StyleTargetModel.cpp
    Source/Engine/MidiExportEngine.cpp
    Source/Engine/PreviewEngine.cpp
    Source/Engine/SampleLibraryManager.cpp
    Source/Engine/LaneSampleBank.cpp
    Source/Engine/ExtractPatternBuilder.cpp
    Source/Engine/SampleGuideAccents.cpp
    Source/Engine/SampleBassFollower.cpp
    Source/Engine/SampleBassLineComposer.cpp
    Source/Engine/PatternBlendEngine.cpp
    Source/Engine/SubstyleRuleEnforcer.cpp
    Source/Engine/BoomBap/BoomBapStyleProfile.cpp
    Source/Engine/BoomBap/BoomBapClassicAlgebraGenerator.cpp
    Source/Engine/BoomBap/BoomBapBassGenerator.cpp
    Source/Engine/BoomBap/BoomBapClassicPatternScorer.cpp
    Source/Engine/BoomBap/BoomBapGenerationContext.cpp
    Source/Engine/Rap/RapStyleProfile.cpp
    Source/Engine/Rap/RapPhrasePlanner.cpp
    Source/Engine/Rap/RapKickGenerator.cpp
    Source/Engine/Rap/RapSnareGenerator.cpp
    Source/Engine/Rap/RapHatGenerator.cpp
    Source/Engine/Trap/TrapStyleProfile.cpp
    Source/Engine/Trap/TrapTempoContext.cpp
    Source/Engine/Trap/TrapAlgebraEngine.cpp
    Source/Engine/Trap/TrapQualityScorer.cpp
    Source/Services/StyleLabReferenceService.cpp
    Source/Services/StyleLabReferenceBrowserService.cpp
    Source/Services/MidiImportService.cpp
    Source/Services/TemporaryMidiExportService.cpp
        Source/UI/MainHeaderComponent.cpp
        Source/UI/SketchDrawing.cpp
        Source/UI/SketchFonts.cpp
        Source/UI/SketchLookAndFeel.cpp
        Source/UI/SampleAnalysisPanelComponent.cpp
        Source/UI/StyleLabInlineComponent.cpp
        Source/UI/StyleLabComponent.cpp
        Source/UI/StyleLabReferenceBrowserComponent.cpp
        Source/UI/TrackRowComponent.cpp
        Source/UI/TrackListComponent.cpp
        Source/UI/SoundModuleComponent.cpp
        Source/UI/Sub808PianoRollComponent.cpp
        Source/UI/GridEditActions.cpp
        Source/UI/GridEditorComponent.cpp
        Source/UI/Vst3GridLiteComponent.cpp
        Source/UI/SampleBreakStripComponent.cpp
        Source/UI/SampleTrimEditorComponent.cpp
)

target_include_directories(HPDG
    PRIVATE
        Source
)

target_link_libraries(HPDG
    PRIVATE
        HPDG_FontAssets
        HPDG_ImageAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

add_executable(HPDG_CoreTests
    Tests/CoreStage0Tests.cpp
    Source/Core/TrackRegistry.cpp
    Source/Core/RuntimeLaneLifecycle.cpp
    Source/Core/PatternProjectSerialization.cpp
    Source/Analysis/SampleApplyWeights.cpp
    Source/Engine/GenerationModel/PatternFeatureVector.cpp
    Source/Engine/GenerationModel/CandidateSelectionEngine.cpp
    Source/Engine/GenerationModel/StyleTargetModel.cpp
    Source/Engine/BoomBapEngine.cpp
    Source/Engine/DrillEngine.cpp
    Source/Engine/DnBEngine.cpp
    Source/Engine/DnB/DnBStyleProfile.cpp
    Source/Engine/DnB/DnBGrammar.cpp
    Source/Engine/DnB/DnBScorer.cpp
    Source/Engine/DnB/DnBBass.cpp
    Source/Engine/TechnoEngine.cpp
    Source/Engine/Techno/TechnoStyleProfile.cpp
    Source/Engine/Techno/TechnoGrammar.cpp
    Source/Engine/Techno/TechnoBass.cpp
    Source/Engine/Drill/Drill808Generator.cpp
    Source/Engine/Drill/DrillHatFxGenerator.cpp
    Source/Engine/Drill/DrillHatGenerator.cpp
    Source/Engine/Drill/DrillKickGenerator.cpp
    Source/Engine/Drill/DrillPatternValidator.cpp
    Source/Engine/Drill/DrillPhrasePlanner.cpp
    Source/Engine/Drill/DrillSnareGenerator.cpp
    Source/Engine/SampleLibraryManager.cpp
    Source/Engine/LaneSampleBank.cpp
    Source/Engine/MidiExportEngine.cpp
    Source/Engine/ExtractPatternBuilder.cpp
    Source/Engine/PatternBlendEngine.cpp
    Source/Engine/SubstyleRuleEnforcer.cpp
    Source/Engine/StyleDefaults.cpp
    Source/Engine/StyleDefinitionLoader.cpp
    Source/Engine/StyleDefinitionResolver.cpp
    Source/Engine/StyleInfluence.cpp
    Source/Engine/BoomBap/BoomBapStyleProfile.cpp
    Source/Engine/BoomBap/BoomBapClassicAlgebraGenerator.cpp
    Source/Engine/BoomBap/BoomBapBassGenerator.cpp
    Source/Engine/BoomBap/BoomBapClassicPatternScorer.cpp
    Source/Engine/BoomBap/BoomBapGenerationContext.cpp
    Source/Engine/RapEngine.cpp
    Source/Engine/Rap/RapStyleProfile.cpp
    Source/Engine/Rap/RapPhrasePlanner.cpp
    Source/Engine/Rap/RapKickGenerator.cpp
    Source/Engine/Rap/RapSnareGenerator.cpp
    Source/Engine/Rap/RapHatGenerator.cpp
    Source/Engine/TrapEngine.cpp
    Source/Engine/Trap/TrapStyleProfile.cpp
    Source/Engine/Trap/TrapTempoContext.cpp
    Source/Engine/Trap/TrapAlgebraEngine.cpp
    Source/Engine/Trap/TrapQualityScorer.cpp
    Source/Services/TemporaryMidiExportService.cpp
    Source/Services/StyleLabReferenceService.cpp
    Source/Services/StyleLabReferenceBrowserService.cpp
    Source/UI/GridEditActions.cpp
    Source/UI/GridEditorComponent.cpp
)

target_include_directories(HPDG_CoreTests
    PRIVATE
        Source
)

target_link_libraries(HPDG_CoreTests
    PRIVATE
        juce::juce_audio_formats
        juce::juce_data_structures
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

add_test(NAME HPDG_CoreTests COMMAND HPDG_CoreTests)

# Temporary regression safety net for the TIMING GRID V2 (NoteEvent tick-native) migration.
# See Tests/TimingMigrationBaselineDump.cpp for how to use it. Safe to delete once the migration
# is verified and merged.
add_executable(HPDG_TimingBaselineDump
    Tests/TimingMigrationBaselineDump.cpp
    Source/Core/TrackRegistry.cpp
    Source/Core/RuntimeLaneLifecycle.cpp
    Source/Core/PatternProjectSerialization.cpp
    Source/Analysis/SampleApplyWeights.cpp
    Source/Engine/GenerationModel/PatternFeatureVector.cpp
    Source/Engine/GenerationModel/CandidateSelectionEngine.cpp
    Source/Engine/GenerationModel/StyleTargetModel.cpp
    Source/Engine/BoomBapEngine.cpp
    Source/Engine/DrillEngine.cpp
    Source/Engine/DnBEngine.cpp
    Source/Engine/DnB/DnBStyleProfile.cpp
    Source/Engine/DnB/DnBGrammar.cpp
    Source/Engine/DnB/DnBScorer.cpp
    Source/Engine/DnB/DnBBass.cpp
    Source/Engine/TechnoEngine.cpp
    Source/Engine/Techno/TechnoStyleProfile.cpp
    Source/Engine/Techno/TechnoGrammar.cpp
    Source/Engine/Techno/TechnoBass.cpp
    Source/Engine/Drill/Drill808Generator.cpp
    Source/Engine/Drill/DrillHatFxGenerator.cpp
    Source/Engine/Drill/DrillHatGenerator.cpp
    Source/Engine/Drill/DrillKickGenerator.cpp
    Source/Engine/Drill/DrillPatternValidator.cpp
    Source/Engine/Drill/DrillPhrasePlanner.cpp
    Source/Engine/Drill/DrillSnareGenerator.cpp
    Source/Engine/SampleLibraryManager.cpp
    Source/Engine/LaneSampleBank.cpp
    Source/Engine/MidiExportEngine.cpp
    Source/Engine/ExtractPatternBuilder.cpp
    Source/Engine/PatternBlendEngine.cpp
    Source/Engine/SubstyleRuleEnforcer.cpp
    Source/Engine/StyleDefaults.cpp
    Source/Engine/StyleDefinitionLoader.cpp
    Source/Engine/StyleDefinitionResolver.cpp
    Source/Engine/StyleInfluence.cpp
    Source/Engine/BoomBap/BoomBapStyleProfile.cpp
    Source/Engine/BoomBap/BoomBapClassicAlgebraGenerator.cpp
    Source/Engine/BoomBap/BoomBapBassGenerator.cpp
    Source/Engine/BoomBap/BoomBapClassicPatternScorer.cpp
    Source/Engine/BoomBap/BoomBapGenerationContext.cpp
    Source/Engine/RapEngine.cpp
    Source/Engine/Rap/RapStyleProfile.cpp
    Source/Engine/Rap/RapPhrasePlanner.cpp
    Source/Engine/Rap/RapKickGenerator.cpp
    Source/Engine/Rap/RapSnareGenerator.cpp
    Source/Engine/Rap/RapHatGenerator.cpp
    Source/Engine/TrapEngine.cpp
    Source/Engine/Trap/TrapStyleProfile.cpp
    Source/Engine/Trap/TrapTempoContext.cpp
    Source/Engine/Trap/TrapAlgebraEngine.cpp
    Source/Engine/Trap/TrapQualityScorer.cpp
    Source/Services/TemporaryMidiExportService.cpp
    Source/Services/StyleLabReferenceService.cpp
    Source/Services/StyleLabReferenceBrowserService.cpp
)

target_include_directories(HPDG_TimingBaselineDump
    PRIVATE
        Source
)

target_link_libraries(HPDG_TimingBaselineDump
    PRIVATE
        juce::juce_audio_formats
        juce::juce_data_structures
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

add_executable(HPDG_BoomBapBatchAudit
    Tests/BoomBapBatchAudit.cpp
    Source/Engine/GenerationModel/PatternFeatureVector.cpp
    Source/Engine/GenerationModel/CandidateSelectionEngine.cpp
    Source/Engine/GenerationModel/StyleTargetModel.cpp
    Source/Engine/BoomBap/BoomBapClassicAlgebraGenerator.cpp
    Source/Engine/BoomBap/BoomBapBassGenerator.cpp
    Source/Engine/BoomBap/BoomBapClassicPatternScorer.cpp
    Source/Engine/BoomBap/BoomBapGenerationContext.cpp
    Source/Engine/BoomBap/BoomBapStyleProfile.cpp
)
target_include_directories(HPDG_BoomBapBatchAudit PRIVATE Source)
target_link_libraries(HPDG_BoomBapBatchAudit PRIVATE juce::juce_core PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

add_executable(HPDG_LaneBoundaryTests
    Tests/LaneBoundaryRegressionTests.cpp
)

target_link_libraries(HPDG_LaneBoundaryTests
    PRIVATE
        HPDG
        juce::juce_audio_formats
        juce::juce_data_structures
        juce::juce_gui_basics
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

add_test(NAME HPDG_LaneBoundaryTests COMMAND HPDG_LaneBoundaryTests)

# Scorer validation + candidate-count benchmark (roadmap sections 8 / 11).
add_executable(HPDG_ScorerAudit
    Tests/ScorerAudit.cpp
)

target_link_libraries(HPDG_ScorerAudit
    PRIVATE
        HPDG
        juce::juce_audio_formats
        juce::juce_data_structures
        juce::juce_gui_basics
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# Generation quality lab (docs/ROADMAP_UPDATE.md, Phase 0/1): batch metrics over the real
# product path for every genre / substyle; baselines live in docs/audit/baseline/.
add_executable(HPDG_GenerationQualityLab
    Tests/GenerationQualityLab.cpp
)

target_link_libraries(HPDG_GenerationQualityLab
    PRIVATE
        HPDG
        juce::juce_audio_formats
        juce::juce_data_structures
        juce::juce_gui_basics
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

add_executable(HPDG_TrackSemanticsTests
    Tests/TrackSemanticsTests.cpp
)

add_test(NAME HPDG_TrackSemanticsTests COMMAND HPDG_TrackSemanticsTests)

# Drum-break transcription bench: analyze real breaks (report + MIDI) or score synthetic ones.
add_executable(HPDG_BreakLab
    Tests/BreakTranscriptionLab.cpp
    Source/Analysis/DrumBreakTranscriber.cpp
    Source/Analysis/SampleHarmonyAnalyzer.cpp
    Source/Analysis/SampleLabelReader.cpp
    Source/Analysis/SampleLineTranscriber.cpp
)

target_include_directories(HPDG_BreakLab PRIVATE Source)

target_link_libraries(HPDG_BreakLab
    PRIVATE
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# Sample source (waveform peaks, transients, tempo grid, suggested fragment) and the trim editor.
add_executable(HPDG_SampleTrimTests
    Tests/SampleTrimTests.cpp
    Source/Analysis/SampleSourceAudio.cpp
    Source/UI/SampleTrimEditorComponent.cpp
)

target_include_directories(HPDG_SampleTrimTests PRIVATE Source)

target_compile_definitions(HPDG_SampleTrimTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

target_link_libraries(HPDG_SampleTrimTests
    PRIVATE
        juce::juce_audio_formats
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

add_test(NAME HPDG_SampleTrimTests COMMAND HPDG_SampleTrimTests)
