qt_add_executable(EcuDiagSim main.cpp)

# Application icon: Windows resource, macOS bundle icon, and the window icon at run time.
set(icons_dir "${CMAKE_SOURCE_DIR}/resources/icons")
if(WIN32)
    target_sources(EcuDiagSim PRIVATE app.rc)
elseif(APPLE)
    set_source_files_properties("${icons_dir}/ecudiagsim.icns" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(EcuDiagSim PRIVATE "${icons_dir}/ecudiagsim.icns")
endif()
qt_add_resources(EcuDiagSim "app_icon" PREFIX "/ecusim" BASE "${CMAKE_SOURCE_DIR}/resources"
    FILES "${icons_dir}/ecudiagsim.png")

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(EcuDiagSim
    URI EcuDiagSim
    VERSION 1.0
    SOURCES
        AppController.h AppController.cpp
        TraceModel.h TraceModel.cpp
        ScannerViewModel.h ScannerViewModel.cpp
        UpdateService.h UpdateService.cpp
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/Panel.qml
        qml/Chip.qml
        qml/SimBar.qml
        qml/Gauge.qml
        qml/Lamp.qml
        qml/Cluster.qml
        qml/DriverControls.qml
        qml/VehicleView.qml
        qml/FaultsPanel.qml
        qml/TracePanel.qml
        qml/ScannerPanel.qml
        qml/ScenariosPanel.qml
)

target_include_directories(EcuDiagSim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(EcuDiagSim PRIVATE ECUSIM_VERSION="${ECUSIM_VERSION}")
if(TARGET velopack)
    target_link_libraries(EcuDiagSim PRIVATE velopack)
    ecusim_copy_velopack(EcuDiagSim)
endif()
target_link_libraries(EcuDiagSim PRIVATE
    ecusim_core ecusim_options
    Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2
)
if(MSVC)
    # Qt headers pulled into the generated QML cache sources trip "unreachable code" at /W4.
    target_compile_options(EcuDiagSim PRIVATE /wd4702)
endif()
set_target_properties(EcuDiagSim PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "ECU Diagnostics Simulator"
    MACOSX_BUNDLE_GUI_IDENTIFIER io.github.alexxfromgit.ecudiagsim
    MACOSX_BUNDLE_BUNDLE_VERSION "${ECUSIM_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${ECUSIM_VERSION}"
    MACOSX_BUNDLE_ICON_FILE ecudiagsim.icns
    MACOSX_BUNDLE_COPYRIGHT "© 2026 Alexander Rubtsov, MIT License")

# --- Installation: a self-contained folder (macOS: an .app bundle) that the installers pack ---
# `cmake --install <build> --prefix <dir>` puts the app, Qt (libraries, plugins, QML modules),
# the Velopack library and the C++ runtime there. Layout: Windows and Linux keep the executable
# at the top with lib/, plugins/ and qml/ next to it.
install(TARGETS EcuDiagSim
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(TARGET velopack)
    if(WIN32)
        install(FILES $<TARGET_FILE:velopack> DESTINATION ${CMAKE_INSTALL_BINDIR})
    elseif(APPLE)
        install(FILES $<TARGET_FILE:velopack> DESTINATION EcuDiagSim.app/Contents/Frameworks)
    else()
        install(FILES $<TARGET_FILE:velopack> DESTINATION ${CMAKE_INSTALL_LIBDIR})
    endif()
endif()
if(APPLE)
    set_target_properties(EcuDiagSim PROPERTIES INSTALL_RPATH "@executable_path/../Frameworks")
elseif(UNIX)
    set_target_properties(EcuDiagSim PROPERTIES INSTALL_RPATH "$ORIGIN/${CMAKE_INSTALL_LIBDIR}")
endif()
if(WIN32)
    # MSVC runtime DLLs next to the executable, so the app runs without the VC++ redistributable.
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ${CMAKE_INSTALL_BINDIR})
    set(CMAKE_INSTALL_UCRT_LIBRARIES FALSE)
    include(InstallRequiredSystemLibraries)
endif()

if(APPLE)
    # Cocoa, plus the headless platform for automated checks of the packaged app.
    qt_import_plugins(EcuDiagSim INCLUDE_BY_TYPE platforms
        Qt6::QCocoaIntegrationPlugin Qt6::QOffscreenIntegrationPlugin)
elseif(UNIX)
    # X11 (also under Wayland through XWayland) and the headless platform for automated checks.
    qt_import_plugins(EcuDiagSim INCLUDE_BY_TYPE platforms
        Qt6::QXcbIntegrationPlugin Qt6::QOffscreenIntegrationPlugin)
endif()
if(WIN32)
    # Qt Quick uses Direct3D 11 here, which has its own software fallback: no need for the
    # software OpenGL renderer or the shader compilers (about 35 MB). The C++ runtime comes from
    # InstallRequiredSystemLibraries above, as DLLs instead of the vc_redist installer.
    set(ECUSIM_DEPLOY_TOOL_OPTIONS
        --no-opengl-sw --no-system-d3d-compiler --no-system-dxc-compiler --no-compiler-runtime)
endif()
qt_generate_deploy_qml_app_script(
    TARGET EcuDiagSim
    OUTPUT_SCRIPT deploy_script
    NO_TRANSLATIONS
    DEPLOY_TOOL_OPTIONS ${ECUSIM_DEPLOY_TOOL_OPTIONS})
install(SCRIPT ${deploy_script})
