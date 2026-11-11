cmake_minimum_required(VERSION 3.24)

project(EcuDiagSim VERSION 0.1.0 LANGUAGES CXX) # x-release-please-version

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Installation layout of the packaged app (see src/app/CMakeLists.txt): on Windows and Linux the
# executable sits at the top of the folder, libraries in lib/. macOS installs an .app bundle.
# Plain variables on purpose: they override the cache entries GNUInstallDirs creates (Qt's
# project setup includes it), so the layout is the same in every build folder.
include(GNUInstallDirs)
if(NOT APPLE)
    set(CMAKE_INSTALL_BINDIR ".")
    set(CMAKE_INSTALL_LIBDIR "lib")
endif()

option(ECUSIM_BUILD_APP "Build the Qt Quick application" ON)
option(ECUSIM_BUILD_TESTS "Build the unit and end-to-end tests" ON)
option(ECUSIM_WERROR "Treat compiler warnings as errors" OFF)
option(ECUSIM_SANITIZE "Enable ASan + UBSan (GCC/Clang)" OFF)
option(ECUSIM_UPDATES "In-app updates with Velopack (downloads the Velopack C library)" ON)
# Version shown in the app. Releases use the project version; override it to try an update locally
# (see CONTRIBUTING.md).
set(ECUSIM_VERSION "${PROJECT_VERSION}" CACHE STRING "Version the app reports")

find_package(Qt6 6.8 REQUIRED COMPONENTS Core SerialBus)
qt_standard_project_setup(REQUIRES 6.8)
# Executables go to the build root on every platform. This also keeps the app binary
# (src/app/EcuDiagSim on Linux) from colliding with its QML module folder of the same name.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

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

if(ECUSIM_UPDATES AND (ECUSIM_BUILD_APP OR ECUSIM_BUILD_TESTS))
    include(cmake/Velopack.cmake)
endif()

if(ECUSIM_BUILD_APP)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Gui Qml Quick QuickControls2)
    # Qt 6.8 links Apple's long-deprecated AGL framework, which current macOS SDKs no longer
    # contain ("framework 'AGL' not found"); later Qt versions dropped it. Nothing uses it.
    if(APPLE AND TARGET WrapOpenGL::WrapOpenGL)
        get_target_property(_gl_libs WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
        list(FILTER _gl_libs EXCLUDE REGEX "AGL")
        set_property(TARGET WrapOpenGL::WrapOpenGL PROPERTY INTERFACE_LINK_LIBRARIES "${_gl_libs}")
    endif()
    add_subdirectory(src/app)
endif()

if(ECUSIM_BUILD_TESTS)
    # Qml (no GUI modules) for the view-model tests, so they also build with ECUSIM_BUILD_APP=OFF.
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test Qml)
    enable_testing()
    add_subdirectory(tests)
endif()
