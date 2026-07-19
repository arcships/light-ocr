function(light_ocr_configure_webgpu_runtime)
  if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The WebGPU runtime flavor requires a 64-bit target")
  elseif(WIN32 AND
         CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
    set(_platform "windows-x64")
    set(_backend "D3D12")
    set(_link_path "lib/onnxruntime.lib")
    set(_core_path "lib/onnxruntime.dll")
    set(_provider_path "lib/onnxruntime_providers_webgpu.dll")
    set(_expected_runtime_files
      "lib/onnxruntime.dll"
      "lib/onnxruntime_providers_webgpu.dll"
      "lib/dxcompiler.dll"
      "lib/dxil.dll")
    set(_expected_file_count 18)
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
         CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
    set(_platform "linux-x64")
    set(_backend "Vulkan")
    set(_link_path "lib/libonnxruntime.so.1")
    set(_core_path "lib/libonnxruntime.so.1")
    set(_provider_path "lib/libonnxruntime_providers_webgpu.so")
    set(_expected_runtime_files
      "lib/libonnxruntime.so.1"
      "lib/libonnxruntime_providers_webgpu.so")
    set(_expected_file_count 15)
    set(LIGHT_OCR_TARGET_LIBC "" CACHE STRING
      "Target libc contract for WebGPU cross-compiles (must be glibc)")
    if(LIGHT_OCR_TARGET_LIBC)
      if(NOT LIGHT_OCR_TARGET_LIBC STREQUAL "glibc")
        message(FATAL_ERROR
          "The Linux WebGPU runtime flavor requires LIGHT_OCR_TARGET_LIBC=glibc")
      endif()
    else()
      include(CheckCXXSourceCompiles)
      set(_saved_try_compile_target_type "${CMAKE_TRY_COMPILE_TARGET_TYPE}")
      set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
      unset(_target_is_glibc CACHE)
      check_cxx_source_compiles(
        "#include <features.h>\n#ifndef __GLIBC__\n#error not glibc\n#endif\nint main() { return 0; }"
        _target_is_glibc)
      set(CMAKE_TRY_COMPILE_TARGET_TYPE "${_saved_try_compile_target_type}")
      if(NOT _target_is_glibc)
        message(FATAL_ERROR
          "The Linux WebGPU runtime requires glibc; cross-compiles must set LIGHT_OCR_TARGET_LIBC=glibc in the verified toolchain")
      endif()
    endif()
  else()
    message(FATAL_ERROR
      "The WebGPU runtime supports Linux x64 glibc and Windows x64, got ${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}")
  endif()

  if(NOT LIGHT_OCR_WEBGPU_SDK_DIR)
    message(FATAL_ERROR
      "LIGHT_OCR_WEBGPU_SDK_DIR is required for the WebGPU runtime flavor")
  endif()
  cmake_path(ABSOLUTE_PATH LIGHT_OCR_WEBGPU_SDK_DIR NORMALIZE
    OUTPUT_VARIABLE _sdk)
  set(_manifest "${_sdk}/artifact-manifest.json")
  if(NOT EXISTS "${_manifest}" OR IS_SYMLINK "${_manifest}" OR
     IS_DIRECTORY "${_manifest}")
    message(FATAL_ERROR
      "WebGPU artifact manifest must be a regular file: ${_manifest}")
  endif()
  file(READ "${_manifest}" _json)
  string(JSON _schema ERROR_VARIABLE _json_error GET "${_json}" schemaVersion)
  if(_json_error)
    message(FATAL_ERROR "WebGPU SDK manifest is invalid JSON: ${_json_error}")
  endif()

  string(JSON _contract GET "${_json}" contractId)
  string(JSON _manifest_platform GET "${_json}" platform id)
  string(JSON _runtime_flavor GET "${_json}" runtime flavor)
  string(JSON _runtime_kind GET "${_json}" runtime kind)
  string(JSON _runtime_version GET "${_json}" runtime version)
  string(JSON _runtime_abi GET "${_json}" runtime abi)
  string(JSON _provider_name GET "${_json}" runtime providerName)
  string(JSON _provider_version GET "${_json}" runtime providerVersion)
  string(JSON _registration_name GET "${_json}" runtime registrationName)
  string(JSON _manifest_backend GET "${_json}" runtime graphicsBackend)
  string(JSON _manifest_link GET "${_json}" artifacts linkLibrary)
  string(JSON _manifest_provider GET "${_json}" artifacts providerLibrary)
  string(JSON _qualification_id GET "${_json}" qualification evidenceId)
  string(JSON _qualification_status GET "${_json}" qualification status)
  string(JSON _provider_gate GET "${_json}" qualification providerGatePassed)
  string(JSON _artifact_qualified GET "${_json}" qualification productionArtifactQualified)
  string(JSON _layout GET "${_json}" sessionOptions preferredLayout)
  string(JSON _graph_capture GET "${_json}" sessionOptions enableGraphCapture)
  string(JSON _validation GET "${_json}" sessionOptions validationMode)
  string(JSON _power GET "${_json}" sessionOptions powerPreference)
  string(JSON _device_id GET "${_json}" sessionOptions deviceIdSupported)
  if(NOT _schema EQUAL 2 OR
     NOT _contract STREQUAL "native-webgpu-plugin-0.1.0-ort-1.24.4-v1" OR
     NOT _manifest_platform STREQUAL _platform OR
     NOT _runtime_flavor STREQUAL "webgpu" OR
     NOT _runtime_kind STREQUAL "onnxruntime-plugin-webgpu" OR
     NOT _runtime_version STREQUAL "1.24.4" OR
     NOT _runtime_abi STREQUAL "onnxruntime-c-api-24-plugin-ep-0.1" OR
     NOT _provider_name STREQUAL "WebGpuExecutionProvider" OR
     NOT _provider_version STREQUAL "0.1.0" OR
     NOT _registration_name STREQUAL "light-ocr-webgpu" OR
     NOT _manifest_backend STREQUAL _backend OR
     NOT _manifest_link STREQUAL _link_path OR
     NOT _manifest_provider STREQUAL _provider_path OR
     NOT _qualification_id MATCHES "^[A-Za-z0-9._-]+$" OR
     NOT _layout STREQUAL "NHWC" OR
     NOT _graph_capture STREQUAL "0" OR
     NOT _validation STREQUAL "basic" OR
     NOT _power STREQUAL "high-performance" OR _device_id)
    message(FATAL_ERROR
      "WebGPU SDK identity does not match the locked ORT 1.24.4 / WebGPU Plugin EP 0.1.0 contract")
  endif()

  string(JSON _package_count LENGTH "${_json}" packages)
  if(NOT _package_count EQUAL 2)
    message(FATAL_ERROR "WebGPU SDK package provenance is incomplete")
  endif()
  set(_package_ids "Microsoft.ML.OnnxRuntime" "Microsoft.ML.OnnxRuntime.EP.WebGpu")
  set(_package_versions "1.24.4" "0.1.0")
  set(_package_sha512
    "f5dd415dfcafcb3a7461f10a08f0337ea22c1ba8f8af81316daabf7496075add181aecb0de3cabebebdb9f5da3afbe507480aaf34b76e1189409088ccc5c2eac"
    "d048cfb4a687d82547338cdf36649c95dfac0a254a752e2e53b5f2faeccfacf4e7b2ed3125e03e5f58bc1c23c6c7cbe356fa513c99b3ad217f491ac1c80bb92a")
  foreach(_package_index RANGE 0 1)
    string(JSON _package_id GET "${_json}" packages ${_package_index} id)
    string(JSON _package_version GET "${_json}" packages ${_package_index} version)
    string(JSON _package_hash GET "${_json}" packages ${_package_index} sha512)
    list(GET _package_ids ${_package_index} _expected_package_id)
    list(GET _package_versions ${_package_index} _expected_package_version)
    list(GET _package_sha512 ${_package_index} _expected_package_hash)
    if(NOT _package_id STREQUAL _expected_package_id OR
       NOT _package_version STREQUAL _expected_package_version OR
       NOT _package_hash STREQUAL _expected_package_hash)
      message(FATAL_ERROR "WebGPU SDK package provenance is invalid")
    endif()
  endforeach()

  set(_expected_headers
    onnxruntime_c_api.h
    onnxruntime_cxx_api.h
    onnxruntime_cxx_inline.h
    onnxruntime_env_config_keys.h
    onnxruntime_ep_c_api.h
    onnxruntime_ep_device_ep_metadata_keys.h
    onnxruntime_float16.h
    onnxruntime_run_options_config_keys.h
    onnxruntime_session_options_config_keys.h)
  foreach(_header IN LISTS _expected_headers)
    if(NOT EXISTS "${_sdk}/include/${_header}" OR
       IS_SYMLINK "${_sdk}/include/${_header}" OR
       IS_DIRECTORY "${_sdk}/include/${_header}")
      message(FATAL_ERROR
        "WebGPU SDK header is missing or not regular: ${_header}")
    endif()
  endforeach()

  string(JSON _file_count LENGTH "${_json}" artifacts files)
  if(NOT _file_count EQUAL _expected_file_count)
    message(FATAL_ERROR "WebGPU SDK file inventory is incomplete")
  endif()
  set(_declared_files "")
  math(EXPR _last_file "${_file_count} - 1")
  foreach(_file_index RANGE 0 ${_last_file})
    string(JSON _file_path GET "${_json}" artifacts files ${_file_index} path)
    string(JSON _file_bytes GET "${_json}" artifacts files ${_file_index} bytes)
    string(JSON _file_sha GET "${_json}" artifacts files ${_file_index} sha256)
    if(NOT _file_path MATCHES "^(include|lib|licenses)/[A-Za-z0-9_.-]+$")
      message(FATAL_ERROR "WebGPU SDK file path is unsafe: ${_file_path}")
    endif()
    list(FIND _declared_files "${_file_path}" _duplicate_index)
    if(NOT _duplicate_index EQUAL -1)
      message(FATAL_ERROR "WebGPU SDK file inventory contains a duplicate path")
    endif()
    list(APPEND _declared_files "${_file_path}")
    set(_file "${_sdk}/${_file_path}")
    if(NOT EXISTS "${_file}" OR IS_SYMLINK "${_file}" OR IS_DIRECTORY "${_file}")
      message(FATAL_ERROR
        "WebGPU SDK artifact must be a regular file: ${_file_path}")
    endif()
    file(SIZE "${_file}" _actual_file_bytes)
    file(SHA256 "${_file}" _actual_file_sha)
    if(NOT _actual_file_bytes EQUAL _file_bytes OR
       NOT _actual_file_sha STREQUAL _file_sha)
      message(FATAL_ERROR
        "WebGPU SDK artifact identity mismatch: ${_file_path}")
    endif()
  endforeach()
  file(GLOB_RECURSE _actual_files LIST_DIRECTORIES FALSE RELATIVE "${_sdk}" "${_sdk}/*")
  list(REMOVE_ITEM _actual_files "artifact-manifest.json")
  list(SORT _actual_files)
  list(SORT _declared_files)
  if(NOT _actual_files STREQUAL _declared_files)
    message(FATAL_ERROR "WebGPU SDK contains undeclared or missing files")
  endif()

  string(JSON _runtime_count LENGTH "${_json}" artifacts runtimeFiles)
  list(LENGTH _expected_runtime_files _expected_runtime_count)
  if(NOT _runtime_count EQUAL _expected_runtime_count)
    message(FATAL_ERROR "WebGPU SDK runtime file inventory is invalid")
  endif()
  set(_runtime_files "")
  math(EXPR _last_runtime "${_runtime_count} - 1")
  foreach(_runtime_index RANGE 0 ${_last_runtime})
    string(JSON _runtime_path GET "${_json}" artifacts runtimeFiles ${_runtime_index})
    list(GET _expected_runtime_files ${_runtime_index} _expected_runtime_path)
    if(NOT _runtime_path STREQUAL _expected_runtime_path)
      message(FATAL_ERROR "WebGPU SDK runtime file identity or order is invalid")
    endif()
    list(APPEND _runtime_files "${_sdk}/${_runtime_path}")
  endforeach()

  if(LIGHT_OCR_WEBGPU_QUALIFICATION_BUILD)
    if(NOT _qualification_status STREQUAL "development-pending-device-validation" OR
       _provider_gate OR _artifact_qualified)
      message(FATAL_ERROR
        "WebGPU qualification SDK must remain pending and unqualified")
    endif()
  else()
    string(JSON _artifact_set_sha GET "${_json}" artifacts artifactSetSha256)
    string(JSON _linux_qualified_sha GET "${_json}"
      qualification qualifiedArtifactSetSha256 linux-x64)
    string(JSON _windows_qualified_sha GET "${_json}"
      qualification qualifiedArtifactSetSha256 windows-x64)
    string(JSON _linux_report_sha GET "${_json}"
      qualification qualificationReportSha256 linux-x64)
    string(JSON _windows_report_sha GET "${_json}"
      qualification qualificationReportSha256 windows-x64)
    string(JSON _qualified_sha GET "${_json}"
      qualification qualifiedArtifactSetSha256 ${_platform})
    if(NOT _qualification_status STREQUAL "production-qualified" OR
       NOT _provider_gate OR NOT _artifact_qualified OR
       NOT _linux_qualified_sha MATCHES "^[0-9a-f]{64}$" OR
       NOT _windows_qualified_sha MATCHES "^[0-9a-f]{64}$" OR
       NOT _linux_report_sha MATCHES "^[0-9a-f]{64}$" OR
       NOT _windows_report_sha MATCHES "^[0-9a-f]{64}$" OR
       NOT _qualified_sha STREQUAL _artifact_set_sha)
      message(FATAL_ERROR
        "WebGPU release SDK requires accepted Linux and Windows Provider Gates bound to this artifact set")
    endif()
  endif()

  set(_include "${_sdk}/include")
  set(_core "${_sdk}/${_core_path}")
  set(_link "${_sdk}/${_link_path}")
  set(_provider "${_sdk}/${_provider_path}")
  set(LIGHT_OCR_WEBGPU_PLATFORM "${_platform}" PARENT_SCOPE)
  set(LIGHT_OCR_WEBGPU_BACKEND "${_backend}" PARENT_SCOPE)
  set(LIGHT_OCR_WEBGPU_PROVIDER_LIBRARY "${_provider}" PARENT_SCOPE)
  set(LIGHT_OCR_WEBGPU_QUALIFICATION_ID_VALUE "${_qualification_id}" PARENT_SCOPE)
  set(LIGHT_OCR_WEBGPU_ORT_INCLUDE "${_include}" PARENT_SCOPE)
  set(LIGHT_OCR_WEBGPU_ORT_LIBRARY "${_core}" PARENT_SCOPE)
  set(LIGHT_OCR_WEBGPU_ORT_LINK_LIBRARY "${_link}" PARENT_SCOPE)
  set(LIGHT_OCR_WEBGPU_RUNTIME_FILES "${_runtime_files}" PARENT_SCOPE)
endfunction()

if(CMAKE_SCRIPT_MODE_FILE AND LIGHT_OCR_WEBGPU_VALIDATE_ONLY)
  light_ocr_configure_webgpu_runtime()
endif()
