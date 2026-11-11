cmake_minimum_required(VERSION 3.24)

project(EcuDiagSim VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ECUSIM_BUILD_APP "Build the Qt Quick application" ON)
option(ECUSIM_BUILD_TESTS "Build the unit and end-to-end tests" ON)
option(ECUSIM_WERROR "Treat compiler warnings as errors" OFF)
option(ECUSIM_SANITIZE "Enable ASan + UBSan (GCC/Clang)" OFF)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core SerialBus)
qt_standard_project_setup(REQUIRES 6.8)

# Shared warning / sanitizer settings for our own targets.
add_library(ecusim_options INTERFACE)
if(MSVC)
    target_compile_options(ecusim_options INTERFACE /W4 /permissive- /utf-8)
    if(ECUSIM_WERROR)
        target_compile_options(ecusim_options INTERFACE /WX)
    endif()
else()
    target_compile_options(ecusim_options INTERFACE -Wall -Wextra -Wpedantic)
    if(ECUSIM_WERROR)
        target_compile_options(ecusim_options INTERFACE -Werror)
    endif()
    if(ECUSIM_SANITIZE)
        target_compile_options(ecusim_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(ecusim_options INTERFACE -fsanitize=address,undefined)
    endif()
endif()

add_subdirectory(src/core)

if(ECUSIM_BUILD_APP)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Gui Qml Quick QuickControls2)
    add_subdirectory(src/app)
endif()

if(ECUSIM_BUILD_TESTS)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
    enable_testing()
    add_subdirectory(tests)
endif()
