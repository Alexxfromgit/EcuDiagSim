qt_add_library(ecusim_core STATIC
    can/VirtualCanBus.h can/VirtualCanBus.cpp
    dbc/DbcDatabase.h dbc/DbcDatabase.cpp
    vehicle/VehicleModel.h vehicle/VehicleModel.cpp
    sensors/Sensor.h sensors/Sensor.cpp
    diag/DtcManager.h diag/DtcManager.cpp
    diag/DtcCatalog.h diag/DtcCatalog.cpp
    diag/IsoTp.h diag/IsoTp.cpp
    diag/UdsDefs.h diag/UdsDefs.cpp
    diag/DiagnosticServer.h diag/DiagnosticServer.cpp
    ecu/Ecu.h ecu/Ecu.cpp
    ecu/Ecus.h ecu/Ecm.cpp ecu/Tcm.cpp ecu/AbsEcu.cpp ecu/Bcm.cpp ecu/Ipc.cpp
    tester/DiagnosticTester.h tester/DiagnosticTester.cpp
    tester/Decode.h tester/Decode.cpp
    trace/TraceRecorder.h trace/TraceRecorder.cpp
    sim/Simulation.h sim/Simulation.cpp
    sim/ScenarioRunner.h sim/ScenarioRunner.cpp
)

target_include_directories(ecusim_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ecusim_core PUBLIC Qt6::Core Qt6::SerialBus PRIVATE ecusim_options)

# The CAN database and the bundled lesson scenarios travel inside the library.
file(GLOB ECUSIM_SCENARIOS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/resources/scenarios/*.json")
qt_add_resources(ecusim_core "ecusim_data"
    PREFIX "/ecusim"
    BASE "${CMAKE_SOURCE_DIR}/resources"
    FILES
        "${CMAKE_SOURCE_DIR}/resources/dbc/vehicle.dbc"
        ${ECUSIM_SCENARIOS}
)
