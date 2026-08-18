# phi-adapter-sdk tests: plain C++ binaries registered with ctest.
# Run automatically by dh_auto_test during Debian package builds.
find_package(Threads REQUIRED)

add_executable(sdk_runtime_tests runtime_tests.cpp)
target_link_libraries(sdk_runtime_tests PRIVATE phi::adapter-sdk Threads::Threads)

add_executable(sdk_protocol_tests protocol_tests.cpp)
target_link_libraries(sdk_protocol_tests PRIVATE phi::adapter-sdk Threads::Threads)

add_test(NAME sdk_runtime_tests COMMAND sdk_runtime_tests)
add_test(NAME sdk_protocol_tests COMMAND sdk_protocol_tests)
set_tests_properties(sdk_runtime_tests sdk_protocol_tests PROPERTIES TIMEOUT 120)
