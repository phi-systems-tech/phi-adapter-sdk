cmake_minimum_required(VERSION 3.21)

project(phi-adapter-sdk
    VERSION 0.1.0
    DESCRIPTION "Phi adapter SDK (contract + Linux runtime)"
    LANGUAGES CXX
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(PHI_ADAPTER_SDK_BUILD_QT_HELPERS
    "Build optional Qt helper target phi::adapter-sdk-qt"
    ON
)

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

if(PHI_ADAPTER_SDK_BUILD_QT_HELPERS)
    find_package(Qt6 QUIET COMPONENTS Core)
    if(Qt6Core_FOUND)
        add_library(phi_adapter_sdk_qt SHARED
            src/qt/instance_execution_backend_qt.cpp
        )
        add_library(phi::adapter-sdk-qt ALIAS phi_adapter_sdk_qt)

        set_target_properties(phi_adapter_sdk_qt PROPERTIES
            EXPORT_NAME adapter-sdk-qt
            VERSION ${PROJECT_VERSION}
            SOVERSION ${PROJECT_VERSION_MAJOR}
        )

        target_compile_features(phi_adapter_sdk_qt PUBLIC cxx_std_20)

        target_include_directories(phi_adapter_sdk_qt
            PUBLIC
                $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        )

        target_link_libraries(phi_adapter_sdk_qt
            PRIVATE
                Qt6::Core
                phi::adapter-sdk
        )
    else()
        message(STATUS "Qt6::Core not found, skipping optional target phi::adapter-sdk-qt")
    endif()
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
    DIRECTORY include/phi/adapter/sdk
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/phi/adapter
    FILES_MATCHING
        PATTERN "*.h"
        PATTERN "qt" EXCLUDE
)

if(TARGET phi_adapter_sdk_qt)
    install(
        DIRECTORY include/phi/adapter/sdk/qt
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/phi/adapter/sdk
        FILES_MATCHING
            PATTERN "*.h"
    )
endif()

set(PHI_ADAPTER_SDK_INSTALL_TARGETS
    phi_adapter_contract
    phi_adapter_sdk
)

install(
    TARGETS ${PHI_ADAPTER_SDK_INSTALL_TARGETS}
    EXPORT phi-adapter-sdkTargets
)

if(TARGET phi_adapter_sdk_qt)
    install(
        TARGETS phi_adapter_sdk_qt
        EXPORT phi-adapter-sdk-qtTargets
    )
endif()

install(
    EXPORT phi-adapter-sdkTargets
    FILE phi-adapter-sdkTargets.cmake
    NAMESPACE phi::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/phi-adapter-sdk
)

if(TARGET phi_adapter_sdk_qt)
    install(
        EXPORT phi-adapter-sdk-qtTargets
        FILE phi-adapter-sdk-qtTargets.cmake
        NAMESPACE phi::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/phi-adapter-sdk-qt
    )
endif()

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

if(TARGET phi_adapter_sdk_qt)
    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/phi-adapter-sdk-qtConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/phi-adapter-sdk-qtConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/phi-adapter-sdk-qt
    )

    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/phi-adapter-sdk-qtConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    install(
        FILES
            ${CMAKE_CURRENT_BINARY_DIR}/phi-adapter-sdk-qtConfig.cmake
            ${CMAKE_CURRENT_BINARY_DIR}/phi-adapter-sdk-qtConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/phi-adapter-sdk-qt
    )
endif()
