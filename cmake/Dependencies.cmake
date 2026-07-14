include(FetchContent)

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

  light_ocr_archive_url(_opencv_url opencv-4.10.0.tar.gz
    https://codeload.github.com/opencv/opencv/tar.gz/refs/tags/4.10.0)
  set(BUILD_LIST core,imgproc CACHE STRING "" FORCE)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(BUILD_PERF_TESTS OFF CACHE BOOL "" FORCE)
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_ITT OFF CACHE BOOL "" FORCE)
  set(BUILD_opencv_apps OFF CACHE BOOL "" FORCE)
  set(BUILD_JAVA OFF CACHE BOOL "" FORCE)
  set(BUILD_opencv_python_bindings_generator OFF CACHE BOOL "" FORCE)
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

  light_ocr_archive_url(_ort_url microsoft.ml.onnxruntime.1.22.0.nupkg
    https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime/1.22.0/microsoft.ml.onnxruntime.1.22.0.nupkg)
  if(EXISTS "${_ort_url}")
    # CMake 3.31 chooses the extractor for local files from their suffix and
    # does not recognize NuGet's .nupkg suffix. Use a hard-linked .zip alias
    # in the build tree; COPY_ON_ERROR keeps this portable across volumes.
    set(_ort_archive_dir "${CMAKE_BINARY_DIR}/_light_ocr_archives")
    set(_ort_archive_zip "${_ort_archive_dir}/microsoft.ml.onnxruntime.1.22.0.zip")
    file(MAKE_DIRECTORY "${_ort_archive_dir}")
    file(CREATE_LINK "${_ort_url}" "${_ort_archive_zip}" COPY_ON_ERROR)
    set(_ort_url "${_ort_archive_zip}")
  endif()
  FetchContent_Declare(onnxruntime_package
    URL "${_ort_url}"
    # NuGet packages are ZIP archives, but CMake 3.31 still selects the
    # extractor from the download suffix and does not recognize .nupkg.
    DOWNLOAD_NAME microsoft.ml.onnxruntime.1.22.0.zip
    URL_HASH SHA256=d571e63a2329baacb713f441e65ad75284de354db6e1ac435fe4bebbb417986a
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

  FetchContent_MakeAvailable(nlohmann_json clipper opencv onnxruntime_package)

  add_library(light_ocr_clipper STATIC "${clipper_SOURCE_DIR}/src/clipper.cpp")
  add_library(light_ocr::clipper ALIAS light_ocr_clipper)
  target_include_directories(light_ocr_clipper SYSTEM PUBLIC "${clipper_SOURCE_DIR}/src")

  # OpenCV's in-tree targets rely on directory-scoped include paths and do not
  # export them to a parent FetchContent consumer. Add only the two public
  # module include roots used by light-ocr.
  target_include_directories(opencv_core SYSTEM INTERFACE
    "$<BUILD_INTERFACE:${opencv_SOURCE_DIR}/modules/core/include>"
    "$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>")
  target_include_directories(opencv_imgproc SYSTEM INTERFACE
    "$<BUILD_INTERFACE:${opencv_SOURCE_DIR}/modules/imgproc/include>"
    "$<BUILD_INTERFACE:${opencv_SOURCE_DIR}/modules/core/include>"
    "$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>")

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
    set(_ort_runtime_files "${_ort_library}")
  else()
    message(FATAL_ERROR "Unsupported ONNX Runtime target: ${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}")
  endif()

  add_library(light_ocr_onnxruntime SHARED IMPORTED GLOBAL)
  add_library(light_ocr::onnxruntime ALIAS light_ocr_onnxruntime)
  set_target_properties(light_ocr_onnxruntime PROPERTIES
    IMPORTED_LOCATION "${_ort_library}"
    INTERFACE_INCLUDE_DIRECTORIES "${onnxruntime_package_SOURCE_DIR}/build/native/include")
  if(WIN32)
    set_target_properties(light_ocr_onnxruntime PROPERTIES IMPORTED_IMPLIB "${_ort_implib}")
    set(_ort_runtime_files "${_ort_library}")
  elseif(NOT APPLE)
    # The Linux NuGet binary has no DT_SONAME. Link by logical name so the
    # executable records libonnxruntime.so instead of a cache-absolute path.
    set_target_properties(light_ocr_onnxruntime PROPERTIES IMPORTED_NO_SONAME TRUE)
  endif()
  set_property(GLOBAL PROPERTY LIGHT_OCR_ONNXRUNTIME_RUNTIME_FILES
    "${_ort_runtime_files}")
endfunction()
