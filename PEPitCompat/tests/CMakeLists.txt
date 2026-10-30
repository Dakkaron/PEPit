# PEPitCompat — unit tests (Catch2, run natively; see TESTING.md in the app repo)
#
# The test binary links the same pepit_core library as the app (minus the app
# entry point shim/main.cpp), so it exercises the real, unmodified firmware
# code against the Linux shims.
#
# Headless: tests must run without a display — CI and ctest set
# SDL_VIDEODRIVER=dummy.

# The amalgamated .cpp provides its own main() unless CATCH_AMALGAMATED_CUSTOM_MAIN
# is defined — we provide our own (catch_main.cpp, via CATCH_CONFIG_RUNNER).
set_source_files_properties(${CMAKE_SOURCE_DIR}/third_party/Catch2/catch_amalgamated.cpp
    PROPERTIES COMPILE_DEFINITIONS "CATCH_AMALGAMATED_CUSTOM_MAIN")

add_executable(pepit_tests
    catch_main.cpp
    test_ini_parsing.cpp
    test_config_parsing.cpp
    test_pressure_sensor.cpp
    test_execution_log.cpp
    test_game_selection.cpp
    test_time_handling.cpp
    test_matrix.cpp
    ${CMAKE_SOURCE_DIR}/third_party/Catch2/catch_amalgamated.cpp
)

target_include_directories(pepit_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/third_party/Catch2
)

target_link_libraries(pepit_tests PRIVATE pepit_core)

# ============================================================
# CTest integration (enable_testing() is called in the top-level CMakeLists)
# ============================================================

add_test(NAME unit_tests COMMAND pepit_tests)

set_tests_properties(unit_tests PROPERTIES
    # Build-local data dir — tests must never write into the repo's data/
    # (a running app instance may have it open).
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;PEPIT_DATA_DIR=${CMAKE_BINARY_DIR}/test_data"
    TIMEOUT 120
)
