cmake_minimum_required(VERSION 3.21)

project(phi-adapter-sdk
    VERSION 0.13.3
    DESCRIPTION "Phi adapter SDK (contract + Linux runtime)"
    LANGUAGES CXX
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# The loop execution backend is built on phi-runtime's EpollLoop. It is a hard
# dependency rather than an option because it is what replaces the Qt backend:
# an adapter that cannot arm a timer has to bring its own event loop, and that
# is how libQt6Core got onto the box in the first place.
find_package(phi-runtime CONFIG REQUIRED)

# MD5 for HTTP digest authentication, and TLS for both HTTP clients. OpenSSL
# rather than libcurl: Debian's libcurl pulls librtmp and with it the GnuTLS
# chain the licence rule exists to keep out - which is the reasoning phi-core's
# own client was written under, and it is that client's TLS that lives here now.
find_package(OpenSSL REQUIRED)

set(PHI_ADAPTER_SDK_CMAKE_INSTALL_DIR "lib/cmake/phi-adapter-sdk")

add_library(phi_adapter_contract INTERFACE)
add_library(phi::adapter-contract ALIAS phi_adapter_contract)

set_target_properties(phi_adapter_contract PROPERTIES
    EXPORT_NAME adapter-contract
)

target_compile_features(phi_adapter_contract INTERFACE cxx_std_20)

target_include_directories(phi_adapter_contract
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

add_library(phi_adapter_sdk SHARED
    src/runtime.cpp
    src/sidecar.cpp
    src/sidecar_main.cpp
    src/button_presses.cpp
    src/http_auth.cpp
    src/http_client.cpp
    src/http_fetch.cpp
    src/http_message.cpp
    src/net_tls.cpp
    src/loop_execution_backend.cpp
    src/linux/uds_epoll_transport.cpp
)
add_library(phi::adapter-sdk ALIAS phi_adapter_sdk)

set_target_properties(phi_adapter_sdk PROPERTIES
    EXPORT_NAME adapter-sdk
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

target_compile_features(phi_adapter_sdk PUBLIC cxx_std_20)

target_include_directories(phi_adapter_sdk
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(phi_adapter_sdk
    PUBLIC
        phi::adapter-contract
        # Public: an adapter that takes the loop backend also writes
        # Loop::current()->timerEvery(...), so it needs the headers.
        phi::runtime
    PRIVATE
        OpenSSL::SSL
        OpenSSL::Crypto
)

target_compile_definitions(phi_adapter_sdk
    PRIVATE
        _GNU_SOURCE
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(phi_adapter_sdk PUBLIC PHI_ADAPTER_SDK_LINUX=1)
else()
    message(FATAL_ERROR "phi_adapter_sdk runtime currently supports Linux only")
endif()



option(PHI_ADAPTER_SDK_BUILD_TESTS "Build phi-adapter-sdk tests" ${PROJECT_IS_TOP_LEVEL})
if(PHI_ADAPTER_SDK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

add_executable(phi_adapter_sidecar_example
    examples/phi-adapter-sidecar/main.cpp
)

target_link_libraries(phi_adapter_sidecar_example
    PRIVATE
        phi::adapter-sdk
)

install(
    DIRECTORY include/phi/adapter/v1
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/phi/adapter
    FILES_MATCHING
        PATTERN "*.h"
)
install(
    DIRECTORY include/phi/adapter/net
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/phi/adapter
    FILES_MATCHING
        PATTERN "*.h"
)
install(
    DIRECTORY include/phi/adapter/testing
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/phi/adapter
    FILES_MATCHING
        PATTERN "*.h"
)
install(
    DIRECTORY include/phi/adapter/sdk
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/phi/adapter
    FILES_MATCHING
        PATTERN "*.h"
        PATTERN "qt" EXCLUDE
)

set(PHI_ADAPTER_SDK_INSTALL_TARGETS
    phi_adapter_contract
    phi_adapter_sdk
)

install(
    TARGETS ${PHI_ADAPTER_SDK_INSTALL_TARGETS}
    EXPORT phi-adapter-sdkTargets
)

install(
    EXPORT phi-adapter-sdkTargets
    FILE phi-adapter-sdkTargets.cmake
    NAMESPACE phi::
    DESTINATION ${PHI_ADAPTER_SDK_CMAKE_INSTALL_DIR}
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/phi-adapter-sdkConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/phi-adapter-sdkConfig.cmake
    INSTALL_DESTINATION ${PHI_ADAPTER_SDK_CMAKE_INSTALL_DIR}
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/phi-adapter-sdkConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/phi-adapter-sdkConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/phi-adapter-sdkConfigVersion.cmake
    DESTINATION ${PHI_ADAPTER_SDK_CMAKE_INSTALL_DIR}
)
