cmake_minimum_required(VERSION 3.22)
project(Lisztmachine VERSION 0.4.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# The composer: plain C++, no JUCE, so it is testable and deterministic on its own.
add_library(liszt_core STATIC
    src/core/Composer.cpp
    src/core/Chorale.cpp
    src/core/Yours.cpp)
target_include_directories(liszt_core PUBLIC src/core)

# The engine: plugin hosting, event player, mixer. Shared by the app and the renderer.
add_library(liszt_engine STATIC
    src/engine/Engine.cpp)
target_include_directories(liszt_engine PUBLIC src/engine)
target_link_libraries(liszt_engine
    PUBLIC liszt_core
           juce::juce_audio_utils
           juce::juce_audio_processors
    PUBLIC juce::juce_recommended_config_flags
           juce::juce_recommended_warning_flags)
target_compile_definitions(liszt_engine PUBLIC
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

juce_add_gui_app(Lisztmachine
    PRODUCT_NAME "Lisztmachine"
    COMPANY_NAME "Kerem Altaylar"
    BUNDLE_ID "com.keremaltaylar.lisztmachine")
target_sources(Lisztmachine PRIVATE src/app/Main.cpp)
target_link_libraries(Lisztmachine PRIVATE liszt_engine)
if(APPLE) # the palette travels inside the .app
    add_custom_command(TARGET Lisztmachine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/sounds
                "$<TARGET_BUNDLE_CONTENT_DIR:Lisztmachine>/Resources/sounds")
endif()

# Offline renderer: N minutes of the concert to a WAV, as fast as the CPU allows.
juce_add_console_app(liszt_render PRODUCT_NAME "liszt_render")
target_sources(liszt_render PRIVATE src/render/RenderMain.cpp)
target_link_libraries(liszt_render PRIVATE liszt_engine juce::juce_dsp)

add_executable(liszt_tests tests/CoreTests.cpp)
target_link_libraries(liszt_tests PRIVATE liszt_core)
