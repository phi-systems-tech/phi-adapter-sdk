# phi-adapter-sdk tests: plain C++ binaries registered with ctest.
# Run automatically by dh_auto_test during Debian package builds.
find_package(Threads REQUIRED)

add_executable(sdk_runtime_tests runtime_tests.cpp)
target_link_libraries(sdk_runtime_tests PRIVATE phi::adapter-sdk Threads::Threads)

add_executable(sdk_protocol_tests protocol_tests.cpp)
target_link_libraries(sdk_protocol_tests PRIVATE phi::adapter-sdk Threads::Threads)

# Golden-wire contract tests: byte-exact outbound payloads against checked-in
# fixtures; canonical inbound request fixtures decoded and asserted.
# Regenerate outbound fixtures with: PHI_GOLDEN_UPDATE=1 ./sdk_golden_wire_tests
add_executable(sdk_golden_wire_tests golden_wire_tests.cpp)
target_link_libraries(sdk_golden_wire_tests PRIVATE phi::adapter-sdk Threads::Threads)
target_compile_definitions(sdk_golden_wire_tests
    PRIVATE PHI_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")

add_test(NAME sdk_runtime_tests COMMAND sdk_runtime_tests)
add_test(NAME sdk_protocol_tests COMMAND sdk_protocol_tests)
add_test(NAME sdk_golden_wire_tests COMMAND sdk_golden_wire_tests)
set_tests_properties(sdk_runtime_tests sdk_protocol_tests sdk_golden_wire_tests
    PROPERTIES TIMEOUT 120)
