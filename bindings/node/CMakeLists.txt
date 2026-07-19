if(NOT LIGHT_OCR_NODE_INCLUDE_DIR OR
   NOT EXISTS "${LIGHT_OCR_NODE_INCLUDE_DIR}/node_api.h")
  message(FATAL_ERROR
    "LIGHT_OCR_BUILD_NODE requires LIGHT_OCR_NODE_INCLUDE_DIR containing node_api.h")
endif()

add_library(light_ocr_node MODULE
  src/addon.cpp
  src/bundle_loader.cpp
  src/encoded_image.cpp
)

target_include_directories(light_ocr_node PRIVATE
  "${LIGHT_OCR_NODE_INCLUDE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(light_ocr_node PRIVATE
  light_ocr::core
  light_ocr::stb
  Threads::Threads)
target_compile_definitions(light_ocr_node PRIVATE NAPI_VERSION=8)
get_property(_light_ocr_node_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
set_target_properties(light_ocr_node PROPERTIES
  PREFIX ""
  SUFFIX ".node"
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN YES
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
if(_light_ocr_node_multi_config)
  foreach(_light_ocr_configuration IN LISTS CMAKE_CONFIGURATION_TYPES)
    string(TOUPPER "${_light_ocr_configuration}" _light_ocr_configuration_upper)
    set_target_properties(light_ocr_node PROPERTIES
      LIBRARY_OUTPUT_DIRECTORY_${_light_ocr_configuration_upper}
        "${CMAKE_BINARY_DIR}/bin/${_light_ocr_configuration}"
      RUNTIME_OUTPUT_DIRECTORY_${_light_ocr_configuration_upper}
        "${CMAKE_BINARY_DIR}/bin/${_light_ocr_configuration}"
    )
  endforeach()
else()
  foreach(_light_ocr_configuration DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(light_ocr_node PROPERTIES
      LIBRARY_OUTPUT_DIRECTORY_${_light_ocr_configuration} "${CMAKE_BINARY_DIR}/bin"
      RUNTIME_OUTPUT_DIRECTORY_${_light_ocr_configuration} "${CMAKE_BINARY_DIR}/bin"
    )
  endforeach()
endif()

if(MSVC)
  if(NOT LIGHT_OCR_NODE_LIBRARY OR NOT EXISTS "${LIGHT_OCR_NODE_LIBRARY}")
    message(FATAL_ERROR
      "Windows Node-API builds require LIGHT_OCR_NODE_LIBRARY pointing to node.lib")
  endif()
  target_link_libraries(light_ocr_node PRIVATE "${LIGHT_OCR_NODE_LIBRARY}")
  target_compile_options(light_ocr_node PRIVATE /W4 /WX /permissive- /EHsc)
else()
  target_compile_options(light_ocr_node PRIVATE -Wall -Wextra -Wpedantic -Werror)
  if(APPLE)
    target_link_options(light_ocr_node PRIVATE "-undefined" "dynamic_lookup")
    target_link_options(light_ocr_node PRIVATE
      "LINKER:-exported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/exports-macos.txt")
    set_property(TARGET light_ocr_node APPEND PROPERTY LINK_DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/exports-macos.txt")
  elseif(UNIX)
    target_link_options(light_ocr_node PRIVATE
      "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports-linux.map")
    set_property(TARGET light_ocr_node APPEND PROPERTY LINK_DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/exports-linux.map")
  endif()
endif()

light_ocr_stage_onnxruntime(light_ocr_node)

get_property(_light_ocr_node_runtime_files GLOBAL
  PROPERTY LIGHT_OCR_ONNXRUNTIME_RUNTIME_FILES)
if(_light_ocr_node_multi_config)
  set(_light_ocr_node_stage_dir "${CMAKE_BINARY_DIR}/node-runtime/$<CONFIG>")
else()
  set(_light_ocr_node_stage_dir "${CMAKE_BINARY_DIR}/node-runtime")
endif()
set(_light_ocr_node_platform_libc "")
if(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
  set(_light_ocr_node_platform_id "macos-arm64")
  set(_light_ocr_node_platform_os "darwin")
  set(_light_ocr_node_platform_arch "arm64")
elseif(APPLE)
  set(_light_ocr_node_platform_id "macos-x64")
  set(_light_ocr_node_platform_os "darwin")
  set(_light_ocr_node_platform_arch "x86_64")
elseif(WIN32)
  set(_light_ocr_node_platform_id "windows-x64")
  set(_light_ocr_node_platform_os "win32")
  set(_light_ocr_node_platform_arch "x86_64")
else()
  set(_light_ocr_node_platform_id "linux-x64")
  set(_light_ocr_node_platform_os "linux")
  set(_light_ocr_node_platform_arch "x86_64")
  set(_light_ocr_node_platform_libc "glibc")
endif()
if(LIGHT_OCR_HAS_WEBGPU)
  set(_light_ocr_node_runtime_flavor "webgpu")
  set(_light_ocr_node_runtime_version "1.24.4")
  set(_light_ocr_node_runtime_abi "onnxruntime-c-api-24-plugin-ep-0.1")
else()
  set(_light_ocr_node_runtime_flavor "cpu")
  set(_light_ocr_node_runtime_version "1.22.0")
  set(_light_ocr_node_runtime_abi "onnxruntime-c-api-22")
endif()
if(LIGHT_OCR_HAS_WEBGPU AND LIGHT_OCR_WEBGPU_QUALIFICATION_BUILD)
  set(_light_ocr_node_qualification_only 1)
  set(_light_ocr_node_released 0)
else()
  set(_light_ocr_node_qualification_only 0)
  set(_light_ocr_node_released 1)
endif()
target_compile_definitions(light_ocr_node PRIVATE
  LIGHT_OCR_NODE_PLATFORM_ID="${_light_ocr_node_platform_id}"
  LIGHT_OCR_NODE_POLICY_ID="${_light_ocr_node_platform_id}-v1"
  LIGHT_OCR_NODE_POLICY_VERSION=1
  LIGHT_OCR_NODE_RUNTIME_FLAVOR="${_light_ocr_node_runtime_flavor}"
  LIGHT_OCR_NODE_RUNTIME_VERSION="${_light_ocr_node_runtime_version}"
  LIGHT_OCR_NODE_RUNTIME_ABI="${_light_ocr_node_runtime_abi}"
  LIGHT_OCR_NODE_CPU_QUALIFICATION_ID="cpu-baseline-v1"
  LIGHT_OCR_NODE_APPLE_QUALIFICATION_ID="apple-open-macos-v1"
  LIGHT_OCR_NODE_WEBGPU_QUALIFICATION_ID="${LIGHT_OCR_WEBGPU_QUALIFICATION_ID}"
  LIGHT_OCR_NODE_QUALIFICATION_ONLY=${_light_ocr_node_qualification_only}
  LIGHT_OCR_NODE_RELEASED=${_light_ocr_node_released}
  LIGHT_OCR_NODE_HAS_APPLE=$<BOOL:${APPLE}>
  LIGHT_OCR_NODE_HAS_WEBGPU=$<BOOL:${LIGHT_OCR_HAS_WEBGPU}>)
add_custom_command(TARGET light_ocr_node POST_BUILD
  COMMAND "${CMAKE_COMMAND}"
    "-DSTAGE_DIR=${_light_ocr_node_stage_dir}"
    "-DADDON_FILE=$<TARGET_FILE:light_ocr_node>"
    "-DRUNTIME_FILES=${_light_ocr_node_runtime_files}"
    "-DPLATFORM_ID=${_light_ocr_node_platform_id}"
    "-DPLATFORM_OS=${_light_ocr_node_platform_os}"
    "-DPLATFORM_ARCH=${_light_ocr_node_platform_arch}"
    "-DPLATFORM_LIBC=${_light_ocr_node_platform_libc}"
    "-DRUNTIME_FLAVOR=${_light_ocr_node_runtime_flavor}"
    "-DQUALIFICATION_ONLY=${_light_ocr_node_qualification_only}"
    "-DWEBGPU_QUALIFICATION_ID=${LIGHT_OCR_WEBGPU_QUALIFICATION_ID}"
    "-DHAS_COREML=$<BOOL:${APPLE}>"
    -P "${PROJECT_SOURCE_DIR}/cmake/StageNodeRuntime.cmake"
  VERBATIM)

if(LIGHT_OCR_BUILD_TESTS)
  if(NOT LIGHT_OCR_NODE_EXECUTABLE)
    find_program(LIGHT_OCR_NODE_EXECUTABLE NAMES node)
  endif()
  set(_light_ocr_test_bundle
    "${PROJECT_SOURCE_DIR}/models/generated/ppocrv6-small-onnx-20260714.2")
  if(LIGHT_OCR_NODE_EXECUTABLE AND
     EXISTS "${_light_ocr_test_bundle}/manifest.json")
    set(_light_ocr_node_test_environment
      "LIGHT_OCR_NODE_BINARY=${_light_ocr_node_stage_dir}/native/light_ocr_node.node"
      "LIGHT_OCR_RUNTIME_DESCRIPTOR=${_light_ocr_node_stage_dir}/native/runtime-descriptor.json"
      "LIGHT_OCR_MODEL_BUNDLE=${_light_ocr_test_bundle}")
    set(_light_ocr_apple_test_bundle
      "${PROJECT_SOURCE_DIR}/models/generated/ppocrv6-small-native-20260719.1")
    if(APPLE AND EXISTS "${_light_ocr_apple_test_bundle}/manifest.json")
      list(APPEND _light_ocr_node_test_environment
        "LIGHT_OCR_APPLE_MODEL_BUNDLE=${_light_ocr_apple_test_bundle}")
    endif()
    add_test(NAME light_ocr_node_tests
      COMMAND "${LIGHT_OCR_NODE_EXECUTABLE}" --test --test-concurrency=1
        "${CMAKE_CURRENT_SOURCE_DIR}/test/adapter.test.cjs")
    set_tests_properties(light_ocr_node_tests PROPERTIES
      WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
      ENVIRONMENT "${_light_ocr_node_test_environment}"
      TIMEOUT 600
      LABELS "node;integration")
    if(LIGHT_OCR_HAS_WEBGPU)
      add_test(NAME light_ocr_node_webgpu_lifecycle_tests
        COMMAND "${LIGHT_OCR_NODE_EXECUTABLE}" --test --test-concurrency=1
          "--test-name-pattern=loads PP-OCRv6|decodes encoded images concurrently|worker environment teardown"
          "${CMAKE_CURRENT_SOURCE_DIR}/test/adapter.test.cjs")
      set_tests_properties(light_ocr_node_webgpu_lifecycle_tests PROPERTIES
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        ENVIRONMENT "${_light_ocr_node_test_environment}"
        TIMEOUT 120
        LABELS "node;integration;webgpu-contract")
    endif()
  else()
    message(STATUS
      "Skipping Node adapter tests: Node.js or the generated PP-OCRv6 bundle is unavailable")
  endif()
endif()
