include(FetchContent)
include(${CMAKE_CURRENT_LIST_DIR}/WebGpuRuntime.cmake)

function(light_ocr_stage_onnxruntime target)
  get_property(_runtime_files GLOBAL PROPERTY LIGHT_OCR_ONNXRUNTIME_RUNTIME_FILES)
  if(NOT _runtime_files)
    message(FATAL_ERROR "ONNX Runtime files are not configured")
  endif()
  foreach(_runtime_file IN LISTS _runtime_files)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_runtime_file}" "$<TARGET_FILE_DIR:${target}>"
      VERBATIM)
  endforeach()
  if(APPLE)
    set_property(TARGET ${target} APPEND PROPERTY BUILD_RPATH "@loader_path")
  elseif(UNIX)
    set_property(TARGET ${target} APPEND PROPERTY BUILD_RPATH "\$ORIGIN")
  endif()
endfunction()

function(light_ocr_archive_url out_var filename remote_url)
  if(LIGHT_OCR_DEPENDENCY_CACHE_DIR AND EXISTS "${LIGHT_OCR_DEPENDENCY_CACHE_DIR}/${filename}")
    set(${out_var} "${LIGHT_OCR_DEPENDENCY_CACHE_DIR}/${filename}" PARENT_SCOPE)
  else()
    set(${out_var} "${remote_url}" PARENT_SCOPE)
  endif()
endfunction()

function(light_ocr_configure_dependencies)
  # These values are consumed outside this function by the core and Node
  # staging. Reset them on every configure so a reused CPU build tree cannot
  # retain WebGPU capability or provider paths.
  set(LIGHT_OCR_HAS_WEBGPU FALSE CACHE INTERNAL
    "WebGPU runtime flavor is configured" FORCE)
  set(LIGHT_OCR_WEBGPU_QUALIFICATION_ID "" CACHE INTERNAL
    "WebGPU runtime qualification identity" FORCE)
  set_property(GLOBAL PROPERTY LIGHT_OCR_WEBGPU_PROVIDER_LIBRARY "")
  set(FETCHCONTENT_QUIET OFF)

  light_ocr_archive_url(_json_url json-3.11.3.tar.gz
    https://codeload.github.com/nlohmann/json/tar.gz/refs/tags/v3.11.3)
  set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
  set(JSON_Install OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(nlohmann_json
    URL "${_json_url}"
    URL_HASH SHA256=0d8ef5af7f9794e3263480193c491549b2ba6cc74bb018906202ada498a79406
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

  light_ocr_archive_url(_clipper_url pyclipper-1.3.0.post6.tar.gz
    https://codeload.github.com/fonttools/pyclipper/tar.gz/refs/tags/1.3.0.post6)
  FetchContent_Declare(clipper
    URL "${_clipper_url}"
    URL_HASH SHA256=2be14496a1609fa8602d9d3672c83ee95d5ef44a08b765a60e65b93a68882ff6
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

  light_ocr_archive_url(_stb_url stb-31c1ad374564.tar.gz
    https://codeload.github.com/nothings/stb/tar.gz/31c1ad37456438565541f4919958214b6e762fb4)
  FetchContent_Declare(stb
    URL "${_stb_url}"
    URL_HASH SHA256=e4e3bba9c572a4a4148373a914d88ea0f0d11de8cc2c66739926e7eca0223319
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

  light_ocr_archive_url(_opencv_url opencv-4.10.0.tar.gz
    https://codeload.github.com/opencv/opencv/tar.gz/refs/tags/4.10.0)
  set(BUILD_LIST core,imgproc CACHE STRING "" FORCE)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  # Keep every static dependency on the same dynamic MSVC runtime as the
  # project and the pinned ONNX Runtime DLL. OpenCV otherwise defaults to /MT
  # for static builds, which cannot be linked into the project's /MD tools.
  set(BUILD_WITH_STATIC_CRT OFF CACHE BOOL "" FORCE)
  set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(BUILD_PERF_TESTS OFF CACHE BOOL "" FORCE)
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_ITT OFF CACHE BOOL "" FORCE)
  set(BUILD_opencv_apps OFF CACHE BOOL "" FORCE)
  set(BUILD_JAVA OFF CACHE BOOL "" FORCE)
  set(BUILD_opencv_python_bindings_generator OFF CACHE BOOL "" FORCE)
  # Core controls inference parallelism through EngineOptions. The built-in
  # pthread parallel_for pool is unnecessary here and races under TSan in
  # OpenCV 4.10. Disable runtime-discovered parallel plugins for the same
  # deterministic, current-directory-independent behavior.
  set(PARALLEL_ENABLE_PLUGINS OFF CACHE BOOL "" FORCE)
  set(WITH_PTHREADS_PF OFF CACHE BOOL "" FORCE)
  set(WITH_OPENMP OFF CACHE BOOL "" FORCE)
  set(WITH_TBB OFF CACHE BOOL "" FORCE)
  set(WITH_1394 OFF CACHE BOOL "" FORCE)
  set(WITH_ADE OFF CACHE BOOL "" FORCE)
  set(WITH_AVFOUNDATION OFF CACHE BOOL "" FORCE)
  set(WITH_EIGEN OFF CACHE BOOL "" FORCE)
  set(WITH_FFMPEG OFF CACHE BOOL "" FORCE)
  set(WITH_GSTREAMER OFF CACHE BOOL "" FORCE)
  set(WITH_GTK OFF CACHE BOOL "" FORCE)
  set(WITH_IPP OFF CACHE BOOL "" FORCE)
  set(WITH_JASPER OFF CACHE BOOL "" FORCE)
  set(WITH_JPEG OFF CACHE BOOL "" FORCE)
  set(WITH_ITT OFF CACHE BOOL "" FORCE)
  set(WITH_LAPACK OFF CACHE BOOL "" FORCE)
  set(WITH_OPENCL OFF CACHE BOOL "" FORCE)
  set(WITH_OPENEXR OFF CACHE BOOL "" FORCE)
  set(WITH_OPENJPEG OFF CACHE BOOL "" FORCE)
  set(WITH_OBSENSOR OFF CACHE BOOL "" FORCE)
  set(WITH_PNG OFF CACHE BOOL "" FORCE)
  set(WITH_PROTOBUF OFF CACHE BOOL "" FORCE)
  set(WITH_FLATBUFFERS OFF CACHE BOOL "" FORCE)
  set(WITH_TIFF OFF CACHE BOOL "" FORCE)
  set(WITH_VTK OFF CACHE BOOL "" FORCE)
  set(WITH_WEBP OFF CACHE BOOL "" FORCE)
  set(CV_TRACE OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(opencv
    URL "${_opencv_url}"
    URL_HASH SHA256=b2171af5be6b26f7a06b1229948bbb2bdaa74fcf5cd097e0af6378fce50a6eb9
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

  if(NOT DEFINED LIGHT_OCR_ONNXRUNTIME_FLAVOR OR
     LIGHT_OCR_ONNXRUNTIME_FLAVOR STREQUAL "")
    set(LIGHT_OCR_ONNXRUNTIME_FLAVOR "cpu")
  endif()
  if(NOT LIGHT_OCR_ONNXRUNTIME_FLAVOR MATCHES "^(cpu|webgpu)$")
    message(FATAL_ERROR
      "LIGHT_OCR_ONNXRUNTIME_FLAVOR must be cpu or webgpu, got: ${LIGHT_OCR_ONNXRUNTIME_FLAVOR}")
  endif()
  if(LIGHT_OCR_ONNXRUNTIME_FLAVOR STREQUAL "cpu")
    set(LIGHT_OCR_WEBGPU_QUALIFICATION_BUILD FALSE CACHE BOOL
      "Build a qualification-only WebGPU runtime" FORCE)
  elseif(NOT DEFINED LIGHT_OCR_WEBGPU_QUALIFICATION_BUILD)
    set(LIGHT_OCR_WEBGPU_QUALIFICATION_BUILD FALSE CACHE BOOL
      "Build a qualification-only WebGPU runtime")
  endif()

  if(LIGHT_OCR_ONNXRUNTIME_FLAVOR STREQUAL "cpu")
    light_ocr_archive_url(_ort_url microsoft.ml.onnxruntime.1.22.0.nupkg
      https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime/1.22.0/microsoft.ml.onnxruntime.1.22.0.nupkg)
    if(EXISTS "${_ort_url}")
      # CMake 3.31 chooses the extractor for local files from their suffix and
      # does not recognize NuGet's .nupkg suffix. Use a hard-linked .zip alias.
      set(_ort_archive_dir "${CMAKE_BINARY_DIR}/_light_ocr_archives")
      set(_ort_archive_zip "${_ort_archive_dir}/microsoft.ml.onnxruntime.1.22.0.zip")
      file(MAKE_DIRECTORY "${_ort_archive_dir}")
      file(CREATE_LINK "${_ort_url}" "${_ort_archive_zip}" COPY_ON_ERROR)
      set(_ort_url "${_ort_archive_zip}")
    endif()
    FetchContent_Declare(onnxruntime_package
      URL "${_ort_url}"
      DOWNLOAD_NAME microsoft.ml.onnxruntime.1.22.0.zip
      URL_HASH SHA256=d571e63a2329baacb713f441e65ad75284de354db6e1ac435fe4bebbb417986a
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(nlohmann_json clipper opencv onnxruntime_package)
  else()
    FetchContent_MakeAvailable(nlohmann_json clipper opencv)
  endif()

  if(LIGHT_OCR_BUILD_NODE OR LIGHT_OCR_BUILD_FUZZERS)
    FetchContent_MakeAvailable(stb)
    add_library(light_ocr_stb INTERFACE)
    add_library(light_ocr::stb ALIAS light_ocr_stb)
    target_include_directories(light_ocr_stb SYSTEM INTERFACE "${stb_SOURCE_DIR}")
  endif()

  add_library(light_ocr_clipper STATIC "${clipper_SOURCE_DIR}/src/clipper.cpp")
  add_library(light_ocr::clipper ALIAS light_ocr_clipper)
  target_include_directories(light_ocr_clipper SYSTEM PUBLIC "${clipper_SOURCE_DIR}/src")

  # OpenCV's in-tree targets rely on directory-scoped include paths and do not
  # export them to a parent FetchContent consumer.
  target_include_directories(opencv_core SYSTEM INTERFACE
    "$<BUILD_INTERFACE:${opencv_SOURCE_DIR}/modules/core/include>"
    "$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>")
  target_include_directories(opencv_imgproc SYSTEM INTERFACE
    "$<BUILD_INTERFACE:${opencv_SOURCE_DIR}/modules/imgproc/include>"
    "$<BUILD_INTERFACE:${opencv_SOURCE_DIR}/modules/core/include>"
    "$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>")

  if(LIGHT_OCR_ONNXRUNTIME_FLAVOR STREQUAL "webgpu")
    light_ocr_configure_webgpu_runtime()
    set(_ort_include "${LIGHT_OCR_WEBGPU_ORT_INCLUDE}")
    set(_ort_library "${LIGHT_OCR_WEBGPU_ORT_LIBRARY}")
    set(_ort_link_library "${LIGHT_OCR_WEBGPU_ORT_LINK_LIBRARY}")
    set(_ort_runtime_files "${LIGHT_OCR_WEBGPU_RUNTIME_FILES}")
    set(_webgpu_provider_library "${LIGHT_OCR_WEBGPU_PROVIDER_LIBRARY}")
    set(_webgpu_qualification_id "${LIGHT_OCR_WEBGPU_QUALIFICATION_ID_VALUE}")
    if(WIN32)
      set(_ort_implib "${_ort_link_library}")
    endif()
  else()
    set(_ort_include "${onnxruntime_package_SOURCE_DIR}/build/native/include")
    if(WIN32)
      set(_ort_runtime_dir "runtimes/win-x64/native")
      set(_ort_library "${onnxruntime_package_SOURCE_DIR}/${_ort_runtime_dir}/onnxruntime.dll")
      set(_ort_implib "${onnxruntime_package_SOURCE_DIR}/${_ort_runtime_dir}/onnxruntime.lib")
    elseif(APPLE)
      if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
        set(_ort_runtime_dir "runtimes/osx-arm64/native")
      else()
        set(_ort_runtime_dir "runtimes/osx-x64/native")
      endif()
      set(_ort_library "${onnxruntime_package_SOURCE_DIR}/${_ort_runtime_dir}/libonnxruntime.dylib")
      set(_ort_versioned_library
        "${onnxruntime_package_SOURCE_DIR}/${_ort_runtime_dir}/libonnxruntime.1.22.0.dylib")
      if(NOT EXISTS "${_ort_versioned_library}")
        file(CREATE_LINK "libonnxruntime.dylib" "${_ort_versioned_library}" SYMBOLIC)
      endif()
      set(_ort_runtime_files "${_ort_library}" "${_ort_versioned_library}")
    elseif(UNIX AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
      set(_ort_runtime_dir "runtimes/linux-x64/native")
      set(_ort_library "${onnxruntime_package_SOURCE_DIR}/${_ort_runtime_dir}/libonnxruntime.so")
      set(_ort_soname_library
        "${onnxruntime_package_SOURCE_DIR}/${_ort_runtime_dir}/libonnxruntime.so.1")
      if(NOT EXISTS "${_ort_soname_library}")
        file(CREATE_LINK "libonnxruntime.so" "${_ort_soname_library}" SYMBOLIC)
      endif()
      set(_ort_runtime_files "${_ort_library}" "${_ort_soname_library}")
    else()
      message(FATAL_ERROR
        "Unsupported ONNX Runtime target: ${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}")
    endif()
  endif()

  add_library(light_ocr_onnxruntime SHARED IMPORTED GLOBAL)
  add_library(light_ocr::onnxruntime ALIAS light_ocr_onnxruntime)
  set_target_properties(light_ocr_onnxruntime PROPERTIES
    IMPORTED_LOCATION "${_ort_library}"
    INTERFACE_INCLUDE_DIRECTORIES "${_ort_include}")
  if(WIN32)
    set_target_properties(light_ocr_onnxruntime PROPERTIES IMPORTED_IMPLIB "${_ort_implib}")
  elseif(NOT APPLE AND NOT LIGHT_OCR_ONNXRUNTIME_FLAVOR STREQUAL "webgpu")
    set_target_properties(light_ocr_onnxruntime PROPERTIES IMPORTED_NO_SONAME TRUE)
  endif()
  if(LIGHT_OCR_ONNXRUNTIME_FLAVOR STREQUAL "webgpu")
    target_compile_definitions(light_ocr_onnxruntime INTERFACE LIGHT_OCR_HAS_WEBGPU=1)
    set(LIGHT_OCR_HAS_WEBGPU TRUE CACHE INTERNAL
      "WebGPU runtime flavor is configured" FORCE)
    set(LIGHT_OCR_WEBGPU_QUALIFICATION_ID "${_webgpu_qualification_id}"
      CACHE INTERNAL "WebGPU runtime qualification identity" FORCE)
    set_property(GLOBAL PROPERTY LIGHT_OCR_WEBGPU_PROVIDER_LIBRARY
      "${_webgpu_provider_library}")
  elseif(WIN32)
    set(_ort_runtime_files "${_ort_library}")
  endif()
  set_property(GLOBAL PROPERTY LIGHT_OCR_ONNXRUNTIME_RUNTIME_FILES
    "${_ort_runtime_files}")
endfunction()
