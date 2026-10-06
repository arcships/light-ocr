# NPU SDKs are verified before headers, binaries or release metadata are used.
function(light_ocr_validate_npu_sdk provider directory)
  if(NOT (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
          CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$"))
    message(FATAL_ERROR "NPU SDKs currently target Linux x64 glibc")
  endif()
  if(LIGHT_OCR_TARGET_LIBC STREQUAL "musl")
    message(FATAL_ERROR "NPU SDKs do not support musl")
  endif()
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(Python3_EXECUTABLE "${Python3_EXECUTABLE}" PARENT_SCOPE)
  execute_process(COMMAND "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tools/npu/sdk.py"
    --sdk-dir "${directory}" --provider "${provider}"
    --emit-header "${CMAKE_BINARY_DIR}/generated/${provider}_defaults.hpp"
    RESULT_VARIABLE _result OUTPUT_VARIABLE _output ERROR_VARIABLE _error)
  if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Invalid ${provider} SDK: ${_output}${_error}")
  endif()
  file(READ "${directory}/sdk-manifest.json" _manifest)
  string(JSON _qualification GET "${_manifest}" qualificationId)
  string(JSON _runtime_library GET "${_manifest}" runtimeLibrary)
  string(TOUPPER "${provider}" _upper)
  target_include_directories(light_ocr_core PRIVATE "${CMAKE_BINARY_DIR}/generated")
  target_compile_definitions(light_ocr_core PRIVATE
    LIGHT_OCR_${_upper}_PACKAGE_SDK=1
    LIGHT_OCR_${_upper}_QUALIFICATION_ID="${_qualification}")
  set(LIGHT_OCR_${_upper}_QUALIFICATION_ID "${_qualification}" PARENT_SCOPE)
  # Shipping the backend does not assert hardware qualification. Device
  # reports remain optional; AMD is excluded from automatic selection.
  set(LIGHT_OCR_${_upper}_QUALIFICATION_BUILD OFF PARENT_SCOPE)
  set(LIGHT_OCR_${_upper}_VERIFIED_LIBRARY "${directory}/${_runtime_library}" PARENT_SCOPE)
endfunction()
