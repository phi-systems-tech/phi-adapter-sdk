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

# The Qt-free execution backend: one thread, one loop, a bounded stop.
add_executable(sdk_loop_execution_backend_tests loop_execution_backend_tests.cpp)
target_link_libraries(sdk_loop_execution_backend_tests PRIVATE phi::adapter-sdk Threads::Threads)

# HTTP digest, pinned against the worked examples in RFC 2617 and RFC 7616.
add_executable(sdk_http_auth_tests http_auth_tests.cpp)
target_link_libraries(sdk_http_auth_tests PRIVATE phi::adapter-sdk)

# HTTP/1.1 framing: the request that goes out, and a response arriving in
# whatever pieces the network chose.
add_executable(sdk_http_message_tests http_message_tests.cpp)
target_link_libraries(sdk_http_message_tests PRIVATE phi::adapter-sdk)

# The client, against a server that recomputes the digest rather than checking
# that a header is present.
add_executable(sdk_http_client_tests http_client_tests.cpp)
target_link_libraries(sdk_http_client_tests PRIVATE phi::adapter-sdk Threads::Threads)

# Composite channel values and the field that stands for them.
add_executable(sdk_channel_value_tests channel_value_tests.cpp)
target_link_libraries(sdk_channel_value_tests PRIVATE phi::adapter-contract)

# Vocabulary rather than runtime: the contract alone, no Qt and no event loop,
# which is the point of it living there.
add_executable(sdk_tlsconfig_tests tlsconfig_tests.cpp)
target_link_libraries(sdk_tlsconfig_tests PRIVATE phi::adapter-contract)

add_test(NAME sdk_tlsconfig_tests COMMAND sdk_tlsconfig_tests)
add_test(NAME sdk_channel_value_tests COMMAND sdk_channel_value_tests)
add_test(NAME sdk_http_auth_tests COMMAND sdk_http_auth_tests)
add_test(NAME sdk_http_message_tests COMMAND sdk_http_message_tests)
add_test(NAME sdk_http_client_tests COMMAND sdk_http_client_tests)
add_test(NAME sdk_loop_execution_backend_tests COMMAND sdk_loop_execution_backend_tests)
add_test(NAME sdk_runtime_tests COMMAND sdk_runtime_tests)
add_test(NAME sdk_protocol_tests COMMAND sdk_protocol_tests)
add_test(NAME sdk_golden_wire_tests COMMAND sdk_golden_wire_tests)
set_tests_properties(sdk_runtime_tests sdk_protocol_tests sdk_golden_wire_tests
    sdk_loop_execution_backend_tests sdk_http_client_tests
    PROPERTIES TIMEOUT 120)
