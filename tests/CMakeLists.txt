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

# The UI's view models, built from the app sources and driven without QML or any GUI module.
if(TARGET Qt6::Qml)
    set(app_dir "${CMAKE_SOURCE_DIR}/src/app")
    qt_add_executable(tst_app tst_app.cpp
        ${app_dir}/AppController.h ${app_dir}/AppController.cpp
        ${app_dir}/TraceModel.h ${app_dir}/TraceModel.cpp
        ${app_dir}/ScannerViewModel.h ${app_dir}/ScannerViewModel.cpp)
    target_include_directories(tst_app PRIVATE ${app_dir})
    target_link_libraries(tst_app PRIVATE ecusim_core ecusim_options Qt6::Qml Qt6::Test)
    add_test(NAME tst_app COMMAND tst_app)
    set_tests_properties(tst_app PROPERTIES TIMEOUT 300)
endif()
