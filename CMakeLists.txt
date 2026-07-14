cmake_minimum_required(VERSION 3.24)

if(APPLE AND (NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET OR
              CMAKE_OSX_DEPLOYMENT_TARGET STREQUAL ""))
  # ONNX Runtime 1.22.0's pinned macOS binaries declare 13.3 as their floor.
  set(CMAKE_OSX_DEPLOYMENT_TARGET "13.3" CACHE STRING
    "Minimum macOS deployment target" FORCE)
endif()

project(light_ocr VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
include(GNUInstallDirs)
find_package(Threads REQUIRED)

option(LIGHT_OCR_BUILD_TESTS "Build light-ocr tests" ON)
option(LIGHT_OCR_BUILD_TOOLS "Build validation and benchmark tools" ON)
option(LIGHT_OCR_BUILD_FUZZERS "Build Clang libFuzzer targets" OFF)
option(LIGHT_OCR_BUILD_NODE "Build the Node-API adapter" OFF)
option(LIGHT_OCR_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(LIGHT_OCR_ENABLE_THREAD_SANITIZER "Enable ThreadSanitizer" OFF)
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

# Keep project executables in one predictable directory for both single- and
# multi-configuration generators. This also gives staged ONNX Runtime files a
# stable package location on Windows.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(_light_ocr_configuration DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_light_ocr_configuration}
      "${CMAKE_BINARY_DIR}/bin")
endforeach()

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
  src/detection/db_postprocess.cpp
  src/geometry/geometry.cpp
  src/inference/onnxruntime/backend.cpp
  src/model/model_bundle.cpp
  src/preprocess/image.cpp
  src/preprocess/tensor.cpp
  src/recognition/ctc_decode.cpp
  src/result/result.cpp
  src/util/sha256.cpp
)
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

target_compile_definitions(light_ocr_core PRIVATE LIGHT_OCR_VERSION="${PROJECT_VERSION}")

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
  target_include_directories(light_ocr_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tools)
  light_ocr_stage_onnxruntime(light_ocr_benchmark)

  add_executable(light_ocr_leak_check tools/leak_check/main.cpp tools/common/bundle_files.cpp)
  target_link_libraries(light_ocr_leak_check PRIVATE light_ocr::core nlohmann_json::nlohmann_json)
  target_include_directories(light_ocr_leak_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tools)
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
  target_link_libraries(light_ocr_leak_check PRIVATE Psapi)
endif()

if(LIGHT_OCR_BUILD_TESTS)
  add_executable(light_ocr_unit_tests
    tests/unit/main.cpp
    tests/unit/test_checked_math.cpp
    tests/unit/test_ctc.cpp
    tests/unit/test_detection.cpp
    tests/unit/test_geometry.cpp
    tests/unit/test_image.cpp
    tests/unit/test_model_bundle.cpp
    tests/unit/test_result.cpp
    tests/unit/test_sha256.cpp
  )
  target_link_libraries(light_ocr_unit_tests PRIVATE light_ocr::core nlohmann_json::nlohmann_json
    opencv_core opencv_imgproc)
  target_include_directories(light_ocr_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/tests)
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
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/models/generated/ppocrv6-small-onnx-20260713.1/manifest.json")
    set_tests_properties(light_ocr_integration_tests PROPERTIES
      ENVIRONMENT "LIGHT_OCR_MODEL_BUNDLE=${CMAKE_CURRENT_SOURCE_DIR}/models/generated/ppocrv6-small-onnx-20260713.1")
  endif()
  if(LIGHT_OCR_BUILD_TOOLS AND LIGHT_OCR_ORACLE_PYTHON AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/models/generated/ppocrv6-small-onnx-20260713.1/manifest.json")
    add_test(NAME light_ocr_oracle_compare_tests
      COMMAND "${LIGHT_OCR_ORACLE_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/oracle/test_compare.py")
    add_test(NAME light_ocr_oracle_golden_verify
      COMMAND "${LIGHT_OCR_ORACLE_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/oracle/generate_goldens.py"
        --bundle "${CMAKE_CURRENT_SOURCE_DIR}/models/generated/ppocrv6-small-onnx-20260713.1"
        --fixtures "${CMAKE_CURRENT_SOURCE_DIR}/corpus/fixtures"
        --output "${CMAKE_CURRENT_SOURCE_DIR}/corpus/goldens"
        --verify)
    add_test(NAME light_ocr_ground_truth_verify
      COMMAND "${LIGHT_OCR_ORACLE_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/oracle/verify_ground_truth.py"
        --fixtures "${CMAKE_CURRENT_SOURCE_DIR}/corpus/fixtures"
        --lock "${CMAKE_CURRENT_SOURCE_DIR}/corpus/ground-truth.lock.json")
    add_test(NAME light_ocr_contract_manifest_verify
      COMMAND "${LIGHT_OCR_ORACLE_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/oracle/verify_contracts.py"
        --contracts "${CMAKE_CURRENT_SOURCE_DIR}/corpus/contracts.json"
        --sources-lock "${CMAKE_CURRENT_SOURCE_DIR}/corpus/sources.lock.json")
    add_test(NAME light_ocr_parity_smoke
      COMMAND "${LIGHT_OCR_ORACLE_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/oracle/run_parity.py"
        --native-probe "$<TARGET_FILE:light_ocr_stage_probe>"
        --bundle "${CMAKE_CURRENT_SOURCE_DIR}/models/generated/ppocrv6-small-onnx-20260713.1"
        --fixture "${CMAKE_CURRENT_SOURCE_DIR}/corpus/fixtures/generated-hello-123/fixture.json")
    add_test(NAME light_ocr_parity_corpus
      COMMAND "${LIGHT_OCR_ORACLE_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/oracle/run_corpus.py"
        --native-probe "$<TARGET_FILE:light_ocr_stage_probe>"
        --bundle "${CMAKE_CURRENT_SOURCE_DIR}/models/generated/ppocrv6-small-onnx-20260713.1"
        --fixtures "${CMAKE_CURRENT_SOURCE_DIR}/corpus/fixtures"
        --report-dir "${CMAKE_BINARY_DIR}/reports/parity")
    add_test(NAME light_ocr_quality_baseline
      COMMAND "${LIGHT_OCR_ORACLE_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/oracle/run_quality.py"
        --native-probe "$<TARGET_FILE:light_ocr_stage_probe>"
        --bundle "${CMAKE_CURRENT_SOURCE_DIR}/models/generated/ppocrv6-small-onnx-20260713.1"
        --fixtures "${CMAKE_CURRENT_SOURCE_DIR}/corpus/fixtures"
        --ground-truth-lock "${CMAKE_CURRENT_SOURCE_DIR}/corpus/ground-truth.lock.json"
        --report "${CMAKE_BINARY_DIR}/reports/quality/ppocrv6-small-onnx-20260713.1.json")
    set_tests_properties(light_ocr_oracle_golden_verify light_ocr_ground_truth_verify
      light_ocr_contract_manifest_verify light_ocr_parity_corpus
      light_ocr_quality_baseline
      PROPERTIES LABELS acceptance)
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
  light_ocr_add_fuzzer(light_ocr_fuzz_bundle tests/fuzz/bundle_fuzz.cpp)
  light_ocr_add_fuzzer(light_ocr_fuzz_geometry tests/fuzz/geometry_fuzz.cpp)
  light_ocr_add_fuzzer(light_ocr_fuzz_lifecycle tests/fuzz/lifecycle_fuzz.cpp
    tools/common/bundle_files.cpp)
endif()
