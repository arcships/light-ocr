cmake_minimum_required(VERSION 3.26)
project(light_ocr_aie_runtime LANGUAGES C CXX)
if(NOT LIGHT_OCR_IREE_SOURCE OR NOT LIGHT_OCR_AMD_AIE_SOURCE)
  message(FATAL_ERROR "Pinned IREE and AMD AIE source paths are required")
endif()
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_STANDARD 17)
set(IREE_BUILD_COMPILER OFF CACHE BOOL "" FORCE)
set(IREE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(IREE_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(IREE_BUILD_PYTHON_BINDINGS OFF CACHE BOOL "" FORCE)
set(IREE_HAL_DRIVER_DEFAULTS OFF CACHE BOOL "" FORCE)
set(IREE_HAL_DRIVER_LOCAL_SYNC ON CACHE BOOL "" FORCE)
set(IREE_HAL_DRIVER_LOCAL_TASK OFF CACHE BOOL "" FORCE)
set(IREE_EXTERNAL_HAL_DRIVERS amdxdna CACHE STRING "" FORCE)
set(IREE_CMAKE_PLUGIN_PATHS "${LIGHT_OCR_AMD_AIE_SOURCE}" CACHE STRING "" FORCE)
set(ENABLE_AMDXDNA_CTS_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_AMDXDNA_AIE_EXECUTABLE_TESTS OFF CACHE BOOL "" FORCE)
set(IREE_ENABLE_ASSERTIONS OFF CACHE BOOL "" FORCE)
set(IREE_ERROR_ON_MISSING_SUBMODULES OFF CACHE BOOL "" FORCE)
add_subdirectory("${LIGHT_OCR_IREE_SOURCE}" iree)
add_library(light_ocr_amdaie SHARED runtime.cc)
target_include_directories(light_ocr_amdaie PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../../src")
target_link_libraries(light_ocr_amdaie PRIVATE
  iree::base iree::hal
  iree::hal::drivers::local_sync::registration
  iree-amd-aie::driver::amdxdna::registration
  iree::io::formats::parser_registry iree::io::parameter_index_provider
  iree::modules::hal iree::modules::io::parameters iree::vm::bytecode::module)
target_compile_options(light_ocr_amdaie PRIVATE -fvisibility=hidden -ffunction-sections -fdata-sections)
target_link_options(light_ocr_amdaie PRIVATE
  "-Wl,--gc-sections" "-Wl,--exclude-libs,ALL" "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports.map")
set_target_properties(light_ocr_amdaie PROPERTIES VERSION 1.0.0 SOVERSION 1)
add_executable(merge-aie-parameters merge_parameters.cc)
target_link_libraries(merge-aie-parameters PRIVATE iree::base
  iree::io::formats::irpa iree::io::formats::parser_registry)
