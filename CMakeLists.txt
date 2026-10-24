cmake_minimum_required(VERSION 3.21)
project(LiteDAW VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Multimedia)

qt_add_executable(LiteDAW WIN32
    src/main.cpp
    src/Engine.h      src/Engine.cpp
    src/Widgets.h     src/Widgets.cpp
    src/MainWindow.h  src/MainWindow.cpp
)

target_link_libraries(LiteDAW PRIVATE Qt6::Widgets Qt6::Multimedia)

if(MSVC)
    target_compile_options(LiteDAW PRIVATE /O2 /utf-8 /W3)
endif()
