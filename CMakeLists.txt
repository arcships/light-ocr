cmake_minimum_required(VERSION 3.24)

if(APPLE AND (NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET OR
              CMAKE_OSX_DEPLOYMENT_TARGET STREQUAL ""))
  # ONNX Runtime 1.22.0's pinned macOS binaries declare 13.3 as their floor.
  set(CMAKE_OSX_DEPLOYMENT_TARGET "13.3" CACHE STRING
    "Minimum macOS deployment target" FORCE)
endif()

project(light_ocr VERSION 0.3.0 LANGUAGES CXX)

if(APPLE)
  enable_language(OBJCXX)
endif()

include(CTest)
include(GNUInstallDirs)
find_package(Threads REQUIRED)

option(LIGHT_OCR_BUILD_TESTS "Build light-ocr tests" ON)
option(LIGHT_OCR_BUILD_TOOLS "Build validation and benchmark tools" ON)
option(LIGHT_OCR_BUILD_FUZZERS "Build Clang libFuzzer targets" OFF)
option(LIGHT_OCR_BUILD_NODE "Build the Node-API adapter" OFF)
option(LIGHT_OCR_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(LIGHT_OCR_ENABLE_THREAD_SANITIZER "Enable ThreadSanitizer" OFF)
option(LIGHT_OCR_PARITY_LIVE_ORACLE
  "Compare the native probe with the pinned oracle running on this machine" OFF)
set(LIGHT_OCR_DEPENDENCY_CACHE_DIR "" CACHE PATH "Directory containing verified dependency archives")
set(LIGHT_OCR_ORACLE_PYTHON "" CACHE FILEPATH "Pinned Python interpreter for parity tests")
set(LIGHT_OCR_NODE_INCLUDE_DIR "" CACHE PATH "Directory containing node_api.h")
set(LIGHT_OCR_NODE_LIBRARY "" CACHE FILEPATH "Windows Node import library")
set(LIGHT_OCR_NODE_EXECUTABLE "" CACHE FILEPATH "Node.js executable used for adapter tests")

if(LIGHT_OCR_ENABLE_SANITIZERS AND LIGHT_OCR_ENABLE_THREAD_SANITIZER)
  message(FATAL_ERROR "Address/Undefined sanitizers and ThreadSanitizer require separate builds")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Dependencies)
light_ocr_configure_dependencies()

# Keep single-configuration executables in one predictable directory and
# isolate multi-configuration outputs so concurrent Debug/Release builds cannot
# overwrite each other's binaries or staged ONNX Runtime files.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
get_property(_light_ocr_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(_light_ocr_multi_config)
  foreach(_light_ocr_configuration IN LISTS CMAKE_CONFIGURATION_TYPES)
    string(TOUPPER "${_light_ocr_configuration}" _light_ocr_configuration_upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_light_ocr_configuration_upper}
        "${CMAKE_BINARY_DIR}/bin/${_light_ocr_configuration}")
  endforeach()
else()
  foreach(_light_ocr_configuration DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_light_ocr_configuration}
        "${CMAKE_BINARY_DIR}/bin")
  endforeach()
endif()

if(NOT MSVC)
  if(LIGHT_OCR_ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
  elseif(LIGHT_OCR_ENABLE_THREAD_SANITIZER)
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
  endif()
endif()

add_library(light_ocr_core STATIC
  src/core/engine.cpp
  src/core/engine_factory.hpp
  src/detection/db_postprocess.cpp
  src/detection/tiled.cpp
  src/geometry/geometry.cpp
  src/inference/onnxruntime/backend.cpp
  src/inference/selection.hpp
  src/model/model_bundle.cpp
  src/preprocess/image.cpp
  src/preprocess/tensor.cpp
  src/recognition/ctc_decode.cpp
  src/result/result.cpp
  src/util/sha256.cpp
)
if(APPLE)
  target_sources(light_ocr_core PRIVATE src/inference/coreml/backend.mm)
endif()
add_library(light_ocr::core ALIAS light_ocr_core)

target_include_directories(light_ocr_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(light_ocr_core
  PRIVATE
    light_ocr::onnxruntime
    nlohmann_json::nlohmann_json
    opencv_core
    opencv_imgproc
    light_ocr::clipper
    Threads::Threads
)

if(APPLE)
  find_library(LIGHT_OCR_COREML_FRAMEWORK CoreML REQUIRED)
  find_library(LIGHT_OCR_FOUNDATION_FRAMEWORK Foundation REQUIRED)
  target_link_libraries(light_ocr_core PRIVATE
    "${LIGHT_OCR_COREML_FRAMEWORK}"
    "${LIGHT_OCR_FOUNDATION_FRAMEWORK}")
  target_compile_options(light_ocr_core PRIVATE
    $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
  target_compile_definitions(light_ocr_core PRIVATE LIGHT_OCR_HAS_COREML=1)
endif()

target_compile_definitions(light_ocr_core PRIVATE LIGHT_OCR_VERSION="${PROJECT_VERSION}")
if(LIGHT_OCR_HAS_WEBGPU)
  target_compile_definitions(light_ocr_core PRIVATE LIGHT_OCR_HAS_WEBGPU=1)
  if(LIGHT_OCR_WEBGPU_QUALIFICATION_BUILD)
    target_compile_definitions(light_ocr_core PRIVATE
      LIGHT_OCR_WEBGPU_QUALIFICATION_BUILD=1)
  endif()
  if(UNIX AND NOT APPLE)
    target_link_libraries(light_ocr_core PRIVATE ${CMAKE_DL_LIBS})
  endif()
endif()

if(MSVC)
  target_compile_options(light_ocr_core PRIVATE /W4 /WX /permissive- /EHsc /fp:strict)
else()
  target_compile_options(light_ocr_core PRIVATE -Wall -Wextra -Wpedantic -Werror
    -ffp-contract=off)
endif()

if(LIGHT_OCR_BUILD_NODE)
  add_subdirectory(bindings/node)
endif()

if(LIGHT_OCR_BUILD_TOOLS)
  add_executable(light_ocr_validate tools/validate/main.cpp tools/common/bundle_files.cpp)
  target_link_libraries(light_ocr_validate PRIVATE light_ocr::core nlohmann_json::nlohmann_json)
  target_include_directories(light_ocr_validate PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tools)
  light_ocr_stage_onnxruntime(light_ocr_validate)

  add_executable(light_ocr_benchmark tools/benchmark/main.cpp tools/common/bundle_files.cpp)
  target_link_libraries(light_ocr_benchmark PRIVATE light_ocr::core nlohmann_json::nlohmann_json)
  target_include_directories(light_ocr_benchmark PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/tools)
  light_ocr_stage_onnxruntime(light_ocr_benchmark)

  add_executable(light_ocr_memory_gate tools/memory_gate/main.cpp
    tools/common/bundle_files.cpp)
  target_link_libraries(light_ocr_memory_gate PRIVATE light_ocr::core
    nlohmann_json::nlohmann_json opencv_core opencv_imgproc)
  target_include_directories(light_ocr_memory_gate PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/tools)
  light_ocr_stage_onnxruntime(light_ocr_memory_gate)

  add_executable(light_ocr_leak_check tools/leak_check/main.cpp tools/common/bundle_files.cpp)
  target_link_libraries(light_ocr_leak_check PRIVATE light_ocr::core nlohmann_json::nlohmann_json)
  target_include_directories(light_ocr_leak_check PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/tools)
  light_ocr_stage_onnxruntime(light_ocr_leak_check)

  add_executable(light_ocr_stage_probe tools/stage_probe/main.cpp tools/common/bundle_files.cpp)
  target_link_libraries(light_ocr_stage_probe PRIVATE light_ocr::core light_ocr::onnxruntime
    nlohmann_json::nlohmann_json opencv_core opencv_imgproc)
  target_include_directories(light_ocr_stage_probe PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/tools)
  light_ocr_stage_onnxruntime(light_ocr_stage_probe)
endif()

if(WIN32 AND LIGHT_OCR_BUILD_TOOLS)
  target_link_libraries(light_ocr_benchmark PRIVATE Psapi)
  target_link_libraries(light_ocr_memory_gate PRIVATE Psapi)
  target_link_libraries(light_ocr_leak_check PRIVATE Psapi)
endif()

if(LIGHT_OCR_BUILD_TESTS)
  add_executable(light_ocr_unit_tests
    tests/unit/main.cpp
    tests/unit/test_checked_math.cpp
    tests/unit/test_ctc.cpp
    tests/unit/test_detection.cpp
    tests/unit/test_tiled.cpp
    tests/unit/test_geometry.cpp
    tests/unit/test_image.cpp
    tests/unit/test_model_bundle.cpp
    tests/unit/test_result.cpp
    tests/unit/test_selection.cpp
    tests/unit/test_sha256.cpp
  )
  target_link_libraries(light_ocr_unit_tests PRIVATE light_ocr::core nlohmann_json::nlohmann_json
    opencv_core opencv_imgproc)
  target_include_directories(light_ocr_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  if(LIGHT_OCR_HAS_WEBGPU AND LIGHT_OCR_WEBGPU_QUALIFICATION_BUILD)
    target_compile_definitions(light_ocr_unit_tests PRIVATE
      LIGHT_OCR_WEBGPU_QUALIFICATION_BUILD=1)
  endif()
  light_ocr_stage_onnxruntime(light_ocr_unit_tests)
  add_test(NAME light_ocr_unit_tests COMMAND light_ocr_unit_tests)

  add_executable(light_ocr_integration_tests tests/integration/main.cpp tools/common/bundle_files.cpp)
  target_link_libraries(light_ocr_integration_tests PRIVATE light_ocr::core light_ocr::onnxruntime
    nlohmann_json::nlohmann_json opencv_core opencv_imgproc Threads::Threads)
  target_include_directories(light_ocr_integration_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/tools)
  light_ocr_stage_onnxruntime(light_ocr_integration_tests)
  add_test(NAME light_ocr_integration_tests COMMAND light_ocr_integration_tests)
  set_tests_properties(light_ocr_integration_tests PROPERTIES SKIP_RETURN_CODE 77)
  if(APPLE)
    add_executable(light_ocr_apple_integration_tests
      tests/integration/apple.cpp tools/common/bundle_files.cpp)
    target_link_libraries(light_ocr_apple_integration_tests PRIVATE
      light_ocr::core nlohmann_json::nlohmann_json opencv_core opencv_imgproc
      Threads::Threads)
    target_include_directories(light_ocr_apple_integration_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/tools)
    light_ocr_stage_onnxruntime(light_ocr_apple_integration_tests)
    add_test(NAME light_ocr_apple_integration_tests
      COMMAND light_ocr_apple_integration_tests)
    set_tests_properties(light_ocr_apple_integration_tests PROPERTIES
      SKIP_RETURN_CODE 77 LABELS "acceptance;apple" TIMEOUT 180)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/models/generated/ppocrv6-small-native-20260719.1/manifest.json")
      set_tests_properties(light_ocr_apple_integration_tests PROPERTIES
        ENVIRONMENT
          "LIGHT_OCR_APPLE_MODEL_BUNDLE=${CMAKE_CURRENT_SOURCE_DIR}/models/generated/ppocrv6-small-native-20260719.1;LIGHT_OCR_APPLE_TEST_PIXELS=${CMAKE_CURRENT_SOURCE_DIR}/corpus/fixtures/generated-hello-123/pixels.bin")
    endif()
  endif()
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/models/generated/ppocrv6-small-onnx-20260714.2/manifest.json")
    set_tests_properties(light_ocr_integration_tests PROPERTIES
      ENVIRONMENT "LIGHT_OCR_MODEL_BUNDLE=${CMAKE_CURRENT_SOURCE_DIR}/models/generated/ppocrv6-small-onnx-20260714.2")
    if(LIGHT_OCR_BUILD_TOOLS)
      add_test(NAME light_ocr_tiled_memory_blank_2048
        COMMAND "$<TARGET_FILE:light_ocr_memory_gate>"
          --bundle "${CMAKE_CURRENT_SOURCE_DIR}/models/generated/ppocrv6-small-onnx-20260714.2"
          --pixels "${CMAKE_CURRENT_SOURCE_DIR}/corpus/fixtures/generated-blank/pixels.bin"
          --width 320 --height 160 --stride 960 --format bgr8
          --target-width 2048 --target-height 2048 --profile tiled_v1
          --maximum-peak-bytes 1073741824 --maximum-boxes 0
          --report "${CMAKE_BINARY_DIR}/reports/tiled/blank-2048.json")
      add_test(NAME light_ocr_tiled_memory_dense_2048
        COMMAND "$<TARGET_FILE:light_ocr_memory_gate>"
          --bundle "${CMAKE_CURRENT_SOURCE_DIR}/models/generated/ppocrv6-small-onnx-20260714.2"
          --pixels "${CMAKE_CURRENT_SOURCE_DIR}/corpus/fixtures/paddleocr-xfund-form/pixels.bin"
          --width 1488 --height 2105 --stride 4464 --format bgr8
          --target-width 2048 --target-height 2048 --profile tiled_v1
          --maximum-peak-bytes 1073741824 --minimum-boxes 100
          --report "${CMAKE_BINARY_DIR}/reports/tiled/dense-2048.json")
      set_tests_properties(light_ocr_tiled_memory_blank_2048
        light_ocr_tiled_memory_dense_2048 PROPERTIES
        LABELS "acceptance;memory;tiled" TIMEOUT 120)
    endif()
    if(LIGHT_OCR_BUILD_TOOLS AND APPLE AND
       CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
      add_test(NAME light_ocr_memory_blank_2048
        COMMAND "$<TARGET_FILE:light_ocr_memory_gate>"
          --bundle "${CMAKE_CURRENT_SOURCE_DIR}/models/generated/ppocrv6-small-onnx-20260714.2"
          --pixels "${CMAKE_CURRENT_SOURCE_DIR}/corpus/fixtures/generated-blank/pixels.bin"
          --width 320 --height 160 --stride 960 --format bgr8
          --target-width 2048 --target-height 2048
          --maximum-peak-bytes 402653184 --maximum-boxes 0
          --report "${CMAKE_BINARY_DIR}/reports/memory/blank-2048.json")
      add_test(NAME light_ocr_memory_dense_2048
        COMMAND "$<TARGET_FILE:light_ocr_memory_gate>"
          --bundle "${CMAKE_CURRENT_SOURCE_DIR}/models/generated/ppocrv6-small-onnx-20260714.2"
          --pixels "${CMAKE_CURRENT_SOURCE_DIR}/corpus/fixtures/paddleocr-xfund-form/pixels.bin"
          --width 1488 --height 2105 --stride 4464 --format bgr8
          --target-width 2048 --target-height 2048
          --maximum-peak-bytes 671088640 --minimum-boxes 100
          --report "${CMAKE_BINARY_DIR}/reports/memory/dense-2048.json")
      set_tests_properties(light_ocr_memory_blank_2048 light_ocr_memory_dense_2048
        PROPERTIES LABELS "acceptance;memory" TIMEOUT 120)
    endif()
  endif()
  if(LIGHT_OCR_BUILD_TOOLS AND LIGHT_OCR_ORACLE_PYTHON AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/models/generated/ppocrv6-small-onnx-20260714.2/manifest.json")
    set(_light_ocr_parity_oracle_arguments)
    if(LIGHT_OCR_PARITY_LIVE_ORACLE)
      list(APPEND _light_ocr_parity_oracle_arguments --live-oracle)
    endif()
    add_test(NAME light_ocr_oracle_compare_tests
      COMMAND "${LIGHT_OCR_ORACLE_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/oracle/test_compare.py")
    add_test(NAME light_ocr_oracle_golden_lock_verify
      COMMAND "${LIGHT_OCR_ORACLE_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/oracle/generate_goldens.py"
        --bundle "${CMAKE_CURRENT_SOURCE_DIR}/models/generated/ppocrv6-small-onnx-20260714.2"
        --fixtures "${CMAKE_CURRENT_SOURCE_DIR}/corpus/fixtures"
        --output "${CMAKE_CURRENT_SOURCE_DIR}/corpus/goldens"
        --profile upstream_exact
        --verify-lock-only)
    add_test(NAME light_ocr_oracle_golden_verify
      COMMAND "${LIGHT_OCR_ORACLE_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/oracle/generate_goldens.py"
        --bundle "${CMAKE_CURRENT_SOURCE_DIR}/models/generated/ppocrv6-small-onnx-20260714.2"
        --fixtures "${CMAKE_CURRENT_SOURCE_DIR}/corpus/fixtures"
        --output "${CMAKE_CURRENT_SOURCE_DIR}/corpus/goldens"
        --profile upstream_exact
        --verify)
    add_test(NAME light_ocr_oracle_bounded_golden_lock_verify
      COMMAND "${LIGHT_OCR_ORACLE_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/oracle/generate_goldens.py"
        --bundle "${CMAKE_CURRENT_SOURCE_DIR}/models/generated/ppocrv6-small-onnx-20260714.2"
        --fixtures "${CMAKE_CURRENT_SOURCE_DIR}/corpus/fixtures"
        --output "${CMAKE_CURRENT_SOURCE_DIR}/corpus/goldens-bounded"
        --profile bounded_default
        --verify-lock-only)
    add_test(NAME light_ocr_oracle_bounded_golden_verify
      COMMAND "${LIGHT_OCR_ORACLE_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/oracle/generate_goldens.py"
        --bundle "${CMAKE_CURRENT_SOURCE_DIR}/models/generated/ppocrv6-small-onnx-20260714.2"
        --fixtures "${CMAKE_CURRENT_SOURCE_DIR}/corpus/fixtures"
        --output "${CMAKE_CURRENT_SOURCE_DIR}/corpus/goldens-bounded"
        --profile bounded_default
        --verify)
    add_test(NAME light_ocr_ground_truth_verify
      COMMAND "${LIGHT_OCR_ORACLE_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/oracle/verify_ground_truth.py"
        --fixtures "${CMAKE_CURRENT_SOURCE_DIR}/corpus/fixtures"
        --lock "${CMAKE_CURRENT_SOURCE_DIR}/corpus/ground-truth.lock.json")
    add_test(NAME light_ocr_tiled_ground_truth_verify
      COMMAND "${LIGHT_OCR_ORACLE_PYTHON}"
        "${CMAKE_CURRENT_SOURCE_DIR}/oracle/tiled_ground_truth.py")
    add_test(NAME light_ocr_tiled_golden_lock_verify
      COMMAND "${LIGHT_OCR_ORACLE_PYTHON}"
        "${CMAKE_CURRENT_SOURCE_DIR}/oracle/generate_tiled_goldens.py"
        --bundle "${CMAKE_CURRENT_SOURCE_DIR}/models/generated/ppocrv6-small-onnx-20260714.2"
        --verify-lock-only)
    add_test(NAME light_ocr_contract_manifest_verify
      COMMAND "${LIGHT_OCR_ORACLE_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/oracle/verify_contracts.py"
        --contracts "${CMAKE_CURRENT_SOURCE_DIR}/corpus/contracts.json"
        --sources-lock "${CMAKE_CURRENT_SOURCE_DIR}/corpus/sources.lock.json")
    add_test(NAME light_ocr_parity_smoke
      COMMAND "${LIGHT_OCR_ORACLE_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/oracle/run_parity.py"
        --native-probe "$<TARGET_FILE:light_ocr_stage_probe>"
        --bundle "${CMAKE_CURRENT_SOURCE_DIR}/models/generated/ppocrv6-small-onnx-20260714.2"
        --fixture "${CMAKE_CURRENT_SOURCE_DIR}/corpus/fixtures/generated-hello-123/fixture.json"
        --profile upstream_exact
        ${_light_ocr_parity_oracle_arguments})
    add_test(NAME light_ocr_parity_corpus
      COMMAND "${LIGHT_OCR_ORACLE_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/oracle/run_corpus.py"
        --native-probe "$<TARGET_FILE:light_ocr_stage_probe>"
        --bundle "${CMAKE_CURRENT_SOURCE_DIR}/models/generated/ppocrv6-small-onnx-20260714.2"
        --fixtures "${CMAKE_CURRENT_SOURCE_DIR}/corpus/fixtures"
        --report-dir "${CMAKE_BINARY_DIR}/reports/parity"
        --profile upstream_exact
        ${_light_ocr_parity_oracle_arguments})
    add_test(NAME light_ocr_parity_bounded_smoke
      COMMAND "${LIGHT_OCR_ORACLE_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/oracle/run_parity.py"
        --native-probe "$<TARGET_FILE:light_ocr_stage_probe>"
        --bundle "${CMAKE_CURRENT_SOURCE_DIR}/models/generated/ppocrv6-small-onnx-20260714.2"
        --fixture "${CMAKE_CURRENT_SOURCE_DIR}/corpus/fixtures/generated-hello-123/fixture.json"
        --profile bounded_default
        ${_light_ocr_parity_oracle_arguments})
    add_test(NAME light_ocr_parity_bounded_corpus
      COMMAND "${LIGHT_OCR_ORACLE_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/oracle/run_corpus.py"
        --native-probe "$<TARGET_FILE:light_ocr_stage_probe>"
        --bundle "${CMAKE_CURRENT_SOURCE_DIR}/models/generated/ppocrv6-small-onnx-20260714.2"
        --fixtures "${CMAKE_CURRENT_SOURCE_DIR}/corpus/fixtures"
        --report-dir "${CMAKE_BINARY_DIR}/reports/parity-bounded"
        --profile bounded_default
        ${_light_ocr_parity_oracle_arguments})
    add_test(NAME light_ocr_quality_baseline
      COMMAND "${LIGHT_OCR_ORACLE_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/oracle/run_quality.py"
        --native-probe "$<TARGET_FILE:light_ocr_stage_probe>"
        --bundle "${CMAKE_CURRENT_SOURCE_DIR}/models/generated/ppocrv6-small-onnx-20260714.2"
        --fixtures "${CMAKE_CURRENT_SOURCE_DIR}/corpus/fixtures"
        --ground-truth-lock "${CMAKE_CURRENT_SOURCE_DIR}/corpus/ground-truth.lock.json"
        --profile bounded_default
        --report "${CMAKE_BINARY_DIR}/reports/quality/ppocrv6-small-onnx-20260714.2.json")
    add_test(NAME light_ocr_tiled_parity_corpus
      COMMAND "${LIGHT_OCR_ORACLE_PYTHON}"
        "${CMAKE_CURRENT_SOURCE_DIR}/oracle/run_tiled_corpus.py"
        --native-probe "$<TARGET_FILE:light_ocr_stage_probe>"
        --bundle "${CMAKE_CURRENT_SOURCE_DIR}/models/generated/ppocrv6-small-onnx-20260714.2"
        --report-dir "${CMAKE_BINARY_DIR}/reports/tiled/parity")
    add_test(NAME light_ocr_tiled_quality
      COMMAND "${LIGHT_OCR_ORACLE_PYTHON}"
        "${CMAKE_CURRENT_SOURCE_DIR}/oracle/run_tiled_quality.py"
        --native-validate "$<TARGET_FILE:light_ocr_validate>"
        --bundle "${CMAKE_CURRENT_SOURCE_DIR}/models/generated/ppocrv6-small-onnx-20260714.2"
        --repetitions 1
        --report "${CMAKE_BINARY_DIR}/reports/tiled/quality.json")
    set_tests_properties(light_ocr_oracle_golden_lock_verify
      light_ocr_oracle_bounded_golden_lock_verify light_ocr_ground_truth_verify
      light_ocr_tiled_ground_truth_verify light_ocr_tiled_golden_lock_verify
      light_ocr_contract_manifest_verify light_ocr_parity_corpus
      light_ocr_parity_bounded_corpus
      light_ocr_quality_baseline light_ocr_tiled_parity_corpus
      light_ocr_tiled_quality
      PROPERTIES LABELS acceptance)
    set_tests_properties(light_ocr_tiled_parity_corpus PROPERTIES TIMEOUT 600)
    set_tests_properties(light_ocr_tiled_quality PROPERTIES TIMEOUT 300)
    set_tests_properties(light_ocr_oracle_golden_verify
      light_ocr_oracle_bounded_golden_verify PROPERTIES LABELS canonical-oracle)
  endif()
endif()

if(LIGHT_OCR_BUILD_FUZZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "LIGHT_OCR_BUILD_FUZZERS requires Clang or AppleClang")
  endif()
  if(NOT LIGHT_OCR_ENABLE_SANITIZERS)
    message(FATAL_ERROR "LIGHT_OCR_BUILD_FUZZERS requires LIGHT_OCR_ENABLE_SANITIZERS=ON")
  endif()

  include(CheckCXXSourceCompiles)
  set(_light_ocr_required_flags "${CMAKE_REQUIRED_FLAGS}")
  set(_light_ocr_required_link_options "${CMAKE_REQUIRED_LINK_OPTIONS}")
  set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -fsanitize=fuzzer")
  set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=fuzzer")
  check_cxx_source_compiles(
    "#include <cstddef>\nextern \"C\" int LLVMFuzzerTestOneInput(const unsigned char*, std::size_t) { return 0; }"
    LIGHT_OCR_HAS_LIBFUZZER)
  set(CMAKE_REQUIRED_FLAGS "${_light_ocr_required_flags}")
  set(CMAKE_REQUIRED_LINK_OPTIONS "${_light_ocr_required_link_options}")

  if(LIGHT_OCR_HAS_LIBFUZZER)
    message(STATUS "Fuzz engine: libFuzzer")
  else()
    message(WARNING
      "The selected Clang toolchain has no libFuzzer runtime; using the deterministic "
      "standalone smoke driver. Use a complete LLVM toolchain for coverage-guided fuzzing.")
  endif()

  function(light_ocr_add_fuzzer target source)
    if(LIGHT_OCR_HAS_LIBFUZZER)
      add_executable(${target} ${source} ${ARGN})
    else()
      add_executable(${target} ${source} ${ARGN} tests/fuzz/standalone_main.cpp)
    endif()
    target_link_libraries(${target} PRIVATE light_ocr::core opencv_core opencv_imgproc)
    target_include_directories(${target} PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/tools)
    if(LIGHT_OCR_HAS_LIBFUZZER)
      target_compile_options(${target} PRIVATE -fsanitize=fuzzer)
      target_link_options(${target} PRIVATE -fsanitize=fuzzer,address,undefined)
    endif()
    light_ocr_stage_onnxruntime(${target})
  endfunction()

  light_ocr_add_fuzzer(light_ocr_fuzz_image tests/fuzz/image_fuzz.cpp)
  light_ocr_add_fuzzer(light_ocr_fuzz_encoded_image
    tests/fuzz/encoded_image_fuzz.cpp
    bindings/node/src/encoded_image.cpp)
  target_link_libraries(light_ocr_fuzz_encoded_image PRIVATE light_ocr::stb)
  target_include_directories(light_ocr_fuzz_encoded_image PRIVATE bindings/node/src)
  light_ocr_add_fuzzer(light_ocr_fuzz_bundle tests/fuzz/bundle_fuzz.cpp)
  light_ocr_add_fuzzer(light_ocr_fuzz_geometry tests/fuzz/geometry_fuzz.cpp)
  light_ocr_add_fuzzer(light_ocr_fuzz_lifecycle tests/fuzz/lifecycle_fuzz.cpp
    tools/common/bundle_files.cpp)
endif()
