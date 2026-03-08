#----------------------------------------------------------------
# Generated CMake target import file for configuration "None".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "phi::adapter-sdk-qt" for configuration "None"
set_property(TARGET phi::adapter-sdk-qt APPEND PROPERTY IMPORTED_CONFIGURATIONS NONE)
set_target_properties(phi::adapter-sdk-qt PROPERTIES
  IMPORTED_LINK_DEPENDENT_LIBRARIES_NONE "Qt6::Core;phi::adapter-sdk"
  IMPORTED_LOCATION_NONE "${_IMPORT_PREFIX}/lib/aarch64-linux-gnu/libphi_adapter_sdk_qt.so.0.1.0"
  IMPORTED_SONAME_NONE "libphi_adapter_sdk_qt.so.0"
  )

list(APPEND _cmake_import_check_targets phi::adapter-sdk-qt )
list(APPEND _cmake_import_check_files_for_phi::adapter-sdk-qt "${_IMPORT_PREFIX}/lib/aarch64-linux-gnu/libphi_adapter_sdk_qt.so.0.1.0" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
