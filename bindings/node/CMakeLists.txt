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
)
target_link_libraries(light_ocr_node PRIVATE
  light_ocr::core
  light_ocr::stb
  Threads::Threads)
target_compile_definitions(light_ocr_node PRIVATE NAPI_VERSION=8)
set_target_properties(light_ocr_node PROPERTIES
  PREFIX ""
  SUFFIX ".node"
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN YES
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

foreach(_light_ocr_configuration DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set_target_properties(light_ocr_node PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY_${_light_ocr_configuration} "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_${_light_ocr_configuration} "${CMAKE_BINARY_DIR}/bin"
  )
endforeach()

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

if(LIGHT_OCR_BUILD_TESTS)
  if(NOT LIGHT_OCR_NODE_EXECUTABLE)
    find_program(LIGHT_OCR_NODE_EXECUTABLE NAMES node)
  endif()
  set(_light_ocr_test_bundle
    "${PROJECT_SOURCE_DIR}/models/generated/ppocrv6-small-onnx-20260714.2")
  if(LIGHT_OCR_NODE_EXECUTABLE AND
     EXISTS "${_light_ocr_test_bundle}/manifest.json")
    add_test(NAME light_ocr_node_tests
      COMMAND "${LIGHT_OCR_NODE_EXECUTABLE}" --test --test-concurrency=1
        "${CMAKE_CURRENT_SOURCE_DIR}/test/adapter.test.cjs")
    set_tests_properties(light_ocr_node_tests PROPERTIES
      WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
      ENVIRONMENT
        "LIGHT_OCR_NODE_BINARY=$<TARGET_FILE:light_ocr_node>;LIGHT_OCR_MODEL_BUNDLE=${_light_ocr_test_bundle}"
      LABELS "node;integration")
  else()
    message(STATUS
      "Skipping Node adapter tests: Node.js or the generated PP-OCRv6 bundle is unavailable")
  endif()
endif()
