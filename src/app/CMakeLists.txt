qt_add_executable(EcuDiagSim main.cpp)

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(EcuDiagSim
    URI EcuDiagSim
    VERSION 1.0
    SOURCES
        AppController.h AppController.cpp
        TraceModel.h TraceModel.cpp
        ScannerViewModel.h ScannerViewModel.cpp
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
target_compile_definitions(EcuDiagSim PRIVATE ECUSIM_VERSION="${PROJECT_VERSION}")
target_link_libraries(EcuDiagSim PRIVATE
    ecusim_core ecusim_options
    Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2
)
if(MSVC)
    # Qt headers pulled into the generated QML cache sources trip "unreachable code" at /W4.
    target_compile_options(EcuDiagSim PRIVATE /wd4702)
endif()
set_target_properties(EcuDiagSim PROPERTIES WIN32_EXECUTABLE TRUE)

install(TARGETS EcuDiagSim RUNTIME DESTINATION bin)
