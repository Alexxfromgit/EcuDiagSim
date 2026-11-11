# Velopack (https://velopack.io): installers, portable builds and in-app updates.
#
# Provides the imported target `velopack` (C/C++ library, shared) and the helper
# ecusim_copy_velopack(<target>) that puts the runtime library next to an executable in the
# build tree. Configure with -DECUSIM_UPDATES=OFF to build without it (no download needed).

set(ECUSIM_VELOPACK_VERSION 1.2.161)
set(ECUSIM_VELOPACK_SHA256 31de8f2cf05a47bd5328a9f98b0c41aec1582156f6075f1058b26941f7f17c20)

include(FetchContent)
FetchContent_Declare(velopack_libc
    URL "https://github.com/velopack/velopack/releases/download/${ECUSIM_VELOPACK_VERSION}/velopack_libc_${ECUSIM_VELOPACK_VERSION}.zip"
    URL_HASH SHA256=${ECUSIM_VELOPACK_SHA256}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(velopack_libc) # no CMakeLists.txt inside: this only downloads and unpacks
set(_vpk_src "${velopack_libc_SOURCE_DIR}")
set(_vpk_dir "${CMAKE_BINARY_DIR}/velopack")
file(MAKE_DIRECTORY "${_vpk_dir}")

add_library(velopack SHARED IMPORTED GLOBAL)
target_include_directories(velopack INTERFACE "${_vpk_src}/include")
target_compile_definitions(velopack INTERFACE ECUSIM_HAS_VELOPACK)

if(WIN32)
    if(CMAKE_CXX_COMPILER_ARCHITECTURE_ID MATCHES "ARM64")
        set(_vpk_name velopack_libc_win_arm64_msvc)
    else()
        set(_vpk_name velopack_libc_win_x64_msvc)
    endif()
    # The import library refers to "velopack_libc.dll", so the DLL has to carry that name.
    file(COPY_FILE "${_vpk_src}/lib/${_vpk_name}.dll" "${_vpk_dir}/velopack_libc.dll" ONLY_IF_DIFFERENT)
    set_target_properties(velopack PROPERTIES
        IMPORTED_LOCATION "${_vpk_dir}/velopack_libc.dll"
        IMPORTED_IMPLIB "${_vpk_src}/lib/${_vpk_name}.dll.lib")
elseif(APPLE)
    # The shipped dylib (universal arm64 + x86_64) carries the absolute path of Velopack's build
    # machine as its install name. Give it an @rpath name so the app finds it inside its bundle,
    # and re-sign it ad hoc: Apple silicon refuses to load a modified library with a broken signature.
    set(_vpk_lib "${_vpk_dir}/libvelopack_libc.dylib")
    if(NOT EXISTS "${_vpk_lib}" OR "${_vpk_src}/lib/velopack_libc_osx.dylib" IS_NEWER_THAN "${_vpk_lib}")
        file(COPY_FILE "${_vpk_src}/lib/velopack_libc_osx.dylib" "${_vpk_lib}")
        execute_process(COMMAND install_name_tool -id "@rpath/libvelopack_libc.dylib" "${_vpk_lib}"
                        COMMAND_ERROR_IS_FATAL ANY)
        execute_process(COMMAND codesign --force --sign - "${_vpk_lib}" COMMAND_ERROR_IS_FATAL ANY)
    endif()
    set_target_properties(velopack PROPERTIES
        IMPORTED_LOCATION "${_vpk_lib}"
        IMPORTED_SONAME "@rpath/libvelopack_libc.dylib")
else()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(_vpk_name velopack_libc_linux_arm64_gnu)
    else()
        set(_vpk_name velopack_libc_linux_x64_gnu)
    endif()
    # The .so has no SONAME. Under a lib*.so name and with IMPORTED_NO_SONAME, CMake links it as
    # -lvelopack_libc, so the executable records "libvelopack_libc.so" instead of a build path.
    set(_vpk_lib "${_vpk_dir}/libvelopack_libc.so")
    file(COPY_FILE "${_vpk_src}/lib/${_vpk_name}.so" "${_vpk_lib}" ONLY_IF_DIFFERENT)
    set_target_properties(velopack PROPERTIES
        IMPORTED_LOCATION "${_vpk_lib}"
        IMPORTED_NO_SONAME TRUE)
endif()

# Windows looks for DLLs next to the executable; Linux and macOS find the library through the
# build RPATH that CMake adds for it.
function(ecusim_copy_velopack target)
    if(WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:velopack> $<TARGET_FILE_DIR:${target}>
            VERBATIM)
    endif()
endfunction()
