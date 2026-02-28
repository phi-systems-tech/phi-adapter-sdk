cmake_minimum_required(VERSION 3.21)

project(phi-adapter-sdk
    VERSION 0.1.0
    DESCRIPTION "Phi adapter SDK (contract + Linux runtime)"
    LANGUAGES CXX
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

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

add_library(phi_adapter_sdk STATIC
    src/runtime.cpp
    src/sidecar.cpp
    src/linux/uds_epoll_transport.cpp
)
add_library(phi::adapter-sdk ALIAS phi_adapter_sdk)

set_target_properties(phi_adapter_sdk PROPERTIES
    EXPORT_NAME adapter-sdk
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

add_executable(phi_adapter_sidecar_example
    examples/phi-adapter-sidecar/main.cpp
)

target_link_libraries(phi_adapter_sidecar_example
    PRIVATE
        phi::adapter-sdk
)

install(
    DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)

install(
    TARGETS
        phi_adapter_contract
        phi_adapter_sdk
    EXPORT phi-adapter-sdkTargets
)

install(
    EXPORT phi-adapter-sdkTargets
    FILE phi-adapter-sdkTargets.cmake
    NAMESPACE phi::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/phi-adapter-sdk
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/phi-adapter-sdkConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/phi-adapter-sdkConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/phi-adapter-sdk
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
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/phi-adapter-sdk
)
