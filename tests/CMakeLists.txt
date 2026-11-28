function(ecusim_add_test name)
    qt_add_executable(${name} ${name}.cpp TestUtil.h)
    target_link_libraries(${name} PRIVATE ecusim_core ecusim_options Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT 300)
endfunction()

ecusim_add_test(tst_dbc)
ecusim_add_test(tst_isotp)
ecusim_add_test(tst_dtcmanager)
ecusim_add_test(tst_uds)
ecusim_add_test(tst_obd)
ecusim_add_test(tst_vehicle)
ecusim_add_test(tst_trace)
ecusim_add_test(tst_scenarios)

# Optional developer tooling only; the app and its builds never need Node/npm.
find_program(ECUSIM_NODE_EXECUTABLE node)
if(ECUSIM_NODE_EXECUTABLE)
    add_test(NAME tst_vehicle_viewer_state
        COMMAND "${ECUSIM_NODE_EXECUTABLE}" --test "${CMAKE_CURRENT_SOURCE_DIR}/vehicle-viewer.test.mjs")
endif()
# The bundled lessons point at their documentation; the test checks those files exist.
target_compile_definitions(tst_scenarios PRIVATE ECUSIM_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# The UI's view models, built from the app sources and driven without QML or any GUI module.
if(TARGET Qt6::Qml)
    set(app_dir "${CMAKE_SOURCE_DIR}/src/app")
    qt_add_executable(tst_app tst_app.cpp
        ${app_dir}/AppController.h ${app_dir}/AppController.cpp
        ${app_dir}/TraceModel.h ${app_dir}/TraceModel.cpp
        ${app_dir}/ScannerViewModel.h ${app_dir}/ScannerViewModel.cpp
        ${app_dir}/UpdateService.h ${app_dir}/UpdateService.cpp)
    target_include_directories(tst_app PRIVATE ${app_dir})
    target_compile_definitions(tst_app PRIVATE ECUSIM_VERSION="${ECUSIM_VERSION}")
    target_link_libraries(tst_app PRIVATE ecusim_core ecusim_options Qt6::Qml Qt6::Test)
    if(TARGET velopack)
        target_link_libraries(tst_app PRIVATE velopack)
        ecusim_copy_velopack(tst_app)
    endif()
    add_test(NAME tst_app COMMAND tst_app)
    set_tests_properties(tst_app PROPERTIES TIMEOUT 300)
    if(TARGET Qt6::Quick AND TARGET Qt6::QuickControls2)
        qt_add_executable(tst_vehicle_workspace tst_vehicle_workspace.cpp
            ${app_dir}/AppController.h ${app_dir}/AppController.cpp
            ${app_dir}/TraceModel.h ${app_dir}/TraceModel.cpp
            ${app_dir}/ScannerViewModel.h ${app_dir}/ScannerViewModel.cpp
            ${app_dir}/UpdateService.h ${app_dir}/UpdateService.cpp)
        target_include_directories(tst_vehicle_workspace PRIVATE ${app_dir})
        target_compile_definitions(tst_vehicle_workspace PRIVATE ECUSIM_VERSION="${ECUSIM_VERSION}"
            ECUSIM_QML_SOURCE_DIR="${app_dir}/qml")
        target_link_libraries(tst_vehicle_workspace PRIVATE ecusim_core ecusim_options
            Qt6::Quick Qt6::QuickControls2 Qt6::Test)
        if(TARGET velopack)
            target_link_libraries(tst_vehicle_workspace PRIVATE velopack)
            ecusim_copy_velopack(tst_vehicle_workspace)
        endif()
        add_test(NAME tst_vehicle_workspace COMMAND tst_vehicle_workspace)
        set_tests_properties(tst_vehicle_workspace PROPERTIES TIMEOUT 60
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    endif()
endif()
