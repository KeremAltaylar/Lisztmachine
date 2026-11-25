cmake_minimum_required(VERSION 3.22)
project(Lisztmachine VERSION 0.13.0 LANGUAGES C CXX)

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

# sfizz (BSD-2): the SFZ sampler that plays the sampled instruments (double bass, cello, guitars,
# piano, sax, erhu, winds...). Its release archive carries its submodules.
set(SFIZZ_SHARED OFF CACHE BOOL "" FORCE)
set(SFIZZ_RENDER OFF CACHE BOOL "" FORCE)
set(SFIZZ_JACK OFF CACHE BOOL "" FORCE)
set(ENABLE_LTO OFF CACHE BOOL "" FORCE)
set(SFIZZ_GIT_SUBMODULE_CHECK OFF CACHE BOOL "" FORCE)
# On Apple silicon sfizz names the processor after CMAKE_OSX_ARCHITECTURES ("arm64"), takes it for 32-bit ARM and adds
# -mfpu=neon -mfloat-abi=hard, which Apple clang rejects (Kerem's Mac, 2026-11-14). Named aarch64, it adds nothing.
if(APPLE AND CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
    set(PROJECT_SYSTEM_PROCESSOR "aarch64" CACHE STRING "" FORCE)
endif()
# Apple clang 17 makes sfizz's atomic_queue (`Base::template do_pop_any(...)` without <>) an error by default; the
# code is right, only the style check is new. Off for the third-party code built below (sfizz and its parts).
if(APPLE)
    add_compile_options(-Wno-missing-template-arg-list-after-template-kw -Wno-nan-infinity-disabled)
endif()
FetchContent_Declare(sfizz
    URL https://github.com/sfztools/sfizz/releases/download/1.2.3/sfizz-1.2.3.tar.gz
    URL_HASH SHA256=a9339eac7620d7f0f6b44bdfe860680fab73e66efad4b5f15b21198dd9436822)
FetchContent_MakeAvailable(sfizz)
# And whatever sfizz (or its abseil) still gave its targets for 32-bit ARM is taken off them: Apple clang 17 refuses
# -mfpu / -mfloat-abi on arm64 as errors (older ones only warned, so the October Mac builds passed).
if(APPLE)
    set(LISZT_STRIPPED 0)
    function(liszt_strip_arm32 dir)
        get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
        foreach(t ${targets})
            get_target_property(type ${t} TYPE)
            if(NOT type STREQUAL "INTERFACE_LIBRARY")
                set_property(TARGET ${t} PROPERTY CXX_STANDARD 17) # sfizz is written for C++17 (C++20 breaks its opcode tables), as on Windows
            endif()
            foreach(prop COMPILE_OPTIONS INTERFACE_COMPILE_OPTIONS)
                get_target_property(opts ${t} ${prop})
                if(opts)
                    list(LENGTH opts before)
                    list(FILTER opts EXCLUDE REGEX "^-mfpu=|^-mfloat-abi=")
                    list(LENGTH opts after)
                    if(NOT before EQUAL after)
                        set_property(TARGET ${t} PROPERTY ${prop} "${opts}")
                        math(EXPR n "${LISZT_STRIPPED} + 1")
                        set(LISZT_STRIPPED ${n} PARENT_SCOPE)
                        set(LISZT_STRIPPED ${n})
                    endif()
                endif()
            endforeach()
        endforeach()
        get_property(subs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
        foreach(s ${subs})
            liszt_strip_arm32("${s}")
            set(LISZT_STRIPPED ${LISZT_STRIPPED} PARENT_SCOPE)
        endforeach()
    endfunction()
    liszt_strip_arm32("${sfizz_SOURCE_DIR}")
    message(STATUS "Lisztmachine: 32-bit ARM flags removed from ${LISZT_STRIPPED} sfizz targets")
endif()
# sfizz asks MSVC for the static runtime; everything else here uses the DLL one, so it is set back,
# and it builds as C++17, as written (C++20 breaks its opcode tables).
if(MSVC)
    function(liszt_dynamic_runtime dir)
        get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
        foreach(t ${targets})
            get_target_property(type ${t} TYPE)
            if(NOT type STREQUAL "INTERFACE_LIBRARY")
                set_property(TARGET ${t} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
                set_property(TARGET ${t} PROPERTY CXX_STANDARD 17) # it is written for C++17
            endif()
        endforeach()
        get_property(subs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
        foreach(s ${subs})
            liszt_dynamic_runtime("${s}")
        endforeach()
    endfunction()
    liszt_dynamic_runtime("${sfizz_SOURCE_DIR}")
endif()

# The composer: plain C++, no JUCE, so it is testable and deterministic on its own.
add_library(liszt_core STATIC
    src/core/Composer.cpp
    src/core/Genres.cpp
    src/core/Improv.cpp)
target_include_directories(liszt_core PUBLIC src/core)

# The engine: plugin hosting, event player, mixer. Shared by the app and the renderer.
add_library(liszt_engine STATIC
    src/engine/Engine.cpp)
target_include_directories(liszt_engine PUBLIC src/engine)
target_link_libraries(liszt_engine
    PRIVATE sfizz::sfizz
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
    BUNDLE_ID "com.keremaltaylar.lisztmachine"
    MICROPHONE_PERMISSION_ENABLED TRUE # input 1: the kick that drives the visuals' drum-pad effect
    MICROPHONE_PERMISSION_TEXT "Lisztmachine listens to input 1 (the kick drum) to drive the visuals.")
target_sources(Lisztmachine PRIVATE src/app/Main.cpp)
# the code's version, shown on the visuals page (2026-11-18: which build is the Mac running?), read at each configure
execute_process(COMMAND git -C "${CMAKE_SOURCE_DIR}" describe --tags --always OUTPUT_VARIABLE LISZT_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
target_compile_definitions(Lisztmachine PRIVATE LISZT_COMMIT="${LISZT_COMMIT}")
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
