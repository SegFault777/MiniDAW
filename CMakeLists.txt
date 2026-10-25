cmake_minimum_required(VERSION 3.21)
project(MiniDAW VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Multimedia)

qt_add_executable(MiniDAW WIN32
    src/main.cpp
    src/Engine.h        src/Engine.cpp
    src/Project.cpp
    src/AudioUtil.h     src/AudioUtil.cpp
    src/Theme.h         src/Theme.cpp
    src/Controls.h      src/Controls.cpp
    src/Timeline.h      src/Timeline.cpp
    src/PianoRoll.h     src/PianoRoll.cpp
    src/Panels.h        src/Panels.cpp
    src/MainWindow.h    src/MainWindow.cpp
    src/dsp/Param.h
    src/dsp/Effects.h   src/dsp/Effects.cpp
    src/dsp/Synth.h     src/dsp/Synth.cpp
)

# Windows resource file (application icon and version information).
if(WIN32 AND EXISTS ${CMAKE_SOURCE_DIR}/resources/app.rc)
    target_sources(MiniDAW PRIVATE resources/app.rc)
endif()

target_include_directories(MiniDAW PRIVATE src)
target_compile_definitions(MiniDAW PRIVATE MINIDAW_VERSION="${PROJECT_VERSION}")
target_link_libraries(MiniDAW PRIVATE Qt6::Widgets Qt6::Multimedia)

if(MSVC)
    target_compile_options(MiniDAW PRIVATE /O2 /utf-8 /W3 /permissive-)
endif()
