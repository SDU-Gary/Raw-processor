cmake_minimum_required(VERSION 3.15)

project(RawProcessor LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(RAWPROC_WITH_LIBRAW "Enable LibRaw-based RAW loading" OFF)
option(RAWPROC_WITH_CIMG "Enable CImg (header-only) if available locally" ON)
option(RAWPROC_WITH_TINYEXR "Enable TinyEXR (header-only) if available locally" ON)
option(RAWPROC_WITH_STB "Enable stb_image_write (header-only) if available locally" ON)

# Helper: platform-specific shared library extension
if(WIN32)
  set(RAWPROC_PLUGIN_EXT ".dll")
elseif(APPLE)
  set(RAWPROC_PLUGIN_EXT ".dylib")
else()
  set(RAWPROC_PLUGIN_EXT ".so")
endif()

add_library(rawproc_core
  src/PluginManager.cpp
  src/ProcessingPipeline.cpp
  src/UnifiedRawData.cpp
  src/PAL/DynamicLibrary.cpp
  src/RawLoader.cpp
  src/ImageExporter.cpp
)

target_include_directories(rawproc_core PUBLIC include src/include)

# For Unix, export symbols for plugins if needed
if(UNIX AND NOT APPLE)
  target_link_libraries(rawproc_core PUBLIC dl)
endif()

add_executable(rawproc_cli apps/main.cpp)

target_link_libraries(rawproc_cli PRIVATE rawproc_core)

# Sample plugin
add_library(denoise_plugin SHARED plugins/denoise/DenoisePlugin.cpp)

target_include_directories(denoise_plugin PUBLIC include)

# Name the factory export consistently across platforms
if(WIN32)
  target_compile_definitions(denoise_plugin PRIVATE RAWPROC_PLUGIN_EXPORTS)
endif()

# Install-like layout (optional)
set(RAWPROC_PLUGIN_DIR "${CMAKE_CURRENT_BINARY_DIR}/plugins")
add_custom_command(TARGET denoise_plugin POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "${RAWPROC_PLUGIN_DIR}"
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:denoise_plugin> "${RAWPROC_PLUGIN_DIR}"
  COMMENT "Copying plugin to runtime plugins folder")

# Additional sample plugins: WhiteBalance, Gamma
add_library(whitebalance_plugin SHARED plugins/whitebalance/WhiteBalancePlugin.cpp)
target_include_directories(whitebalance_plugin PUBLIC include)
if(WIN32)
  target_compile_definitions(whitebalance_plugin PRIVATE RAWPROC_PLUGIN_EXPORTS)
endif()
add_custom_command(TARGET whitebalance_plugin POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "${RAWPROC_PLUGIN_DIR}"
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:whitebalance_plugin> "${RAWPROC_PLUGIN_DIR}"
  COMMENT "Copying WhiteBalance plugin to runtime plugins folder")

add_library(gamma_plugin SHARED plugins/gamma/GammaPlugin.cpp)
target_include_directories(gamma_plugin PUBLIC include)
if(WIN32)
  target_compile_definitions(gamma_plugin PRIVATE RAWPROC_PLUGIN_EXPORTS)
endif()
add_custom_command(TARGET gamma_plugin POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "${RAWPROC_PLUGIN_DIR}"
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:gamma_plugin> "${RAWPROC_PLUGIN_DIR}"
  COMMENT "Copying Gamma plugin to runtime plugins folder")

# Pass runtime plugin dir to CLI
add_compile_definitions(RAWPROC_RUNTIME_PLUGIN_DIR="${RAWPROC_PLUGIN_DIR}")

# --- Optional header-only libraries detection (CImg, stb, TinyEXR) ---
set(RAWPROC_LOCAL_INCLUDE_DIRS
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/include"
)

function(rawproc_check_header _var _relpath)
  set(found FALSE)
  foreach(dir ${RAWPROC_LOCAL_INCLUDE_DIRS})
    if (EXISTS "${dir}/${_relpath}")
      set(found TRUE)
    endif()
  endforeach()
  set(${_var} ${found} PARENT_SCOPE)
endfunction()

rawproc_check_header(HAVE_CIMG "rawproc/CImg.h")
rawproc_check_header(HAVE_STB  "rawproc/stb_image_write.h")
rawproc_check_header(HAVE_TINYEXR "rawproc/tinyexr.h")

if (RAWPROC_WITH_CIMG AND HAVE_CIMG)
  target_compile_definitions(rawproc_core PUBLIC RAWPROC_HAVE_CIMG=1)
endif()

if (RAWPROC_WITH_STB AND HAVE_STB)
  target_compile_definitions(rawproc_core PUBLIC RAWPROC_HAVE_STB=1)
  add_library(rawproc_stb_impl OBJECT src/thirdparty/stb_impl.cpp)
  target_include_directories(rawproc_stb_impl PRIVATE include src/include)
  target_link_libraries(rawproc_core PRIVATE rawproc_stb_impl)
endif()

if (RAWPROC_WITH_TINYEXR AND HAVE_TINYEXR)
  target_compile_definitions(rawproc_core PUBLIC RAWPROC_HAVE_TINYEXR=1)
  add_library(rawproc_tinyexr_impl OBJECT src/thirdparty/tinyexr_impl.cpp)
  target_include_directories(rawproc_tinyexr_impl PRIVATE include src/include)
  # Prefer zlib backend to avoid requiring bundled miniz
  target_compile_definitions(rawproc_tinyexr_impl PRIVATE TINYEXR_USE_MINIZ=0)
  find_package(ZLIB REQUIRED)
  target_link_libraries(rawproc_core PRIVATE rawproc_tinyexr_impl ZLIB::ZLIB)
endif()

# --- LibRaw integration ---
if (RAWPROC_WITH_LIBRAW)
  # vcpkg provides package as `libraw` with targets libraw::raw / libraw::raw_r
  find_package(libraw CONFIG QUIET)
  if (libraw_FOUND)
    message(STATUS "Found libraw via CONFIG (vcpkg)")
    target_compile_definitions(rawproc_core PUBLIC RAWPROC_HAVE_LIBRAW=1)
    target_sources(rawproc_core PRIVATE src/RawLoaderLibRaw.cpp)
    # Prefer thread-safe target if available
    if (TARGET libraw::raw_r)
      target_link_libraries(rawproc_core PRIVATE libraw::raw_r)
    else()
      target_link_libraries(rawproc_core PRIVATE libraw::raw)
    endif()
  else()
    # Fallback to case-sensitive find for distro packages
    find_package(LibRaw QUIET)
    if (LibRaw_FOUND)
      message(STATUS "Found LibRaw (system)")
      target_compile_definitions(rawproc_core PUBLIC RAWPROC_HAVE_LIBRAW=1)
      target_sources(rawproc_core PRIVATE src/RawLoaderLibRaw.cpp)
      target_link_libraries(rawproc_core PRIVATE LibRaw::libraw)
    else()
      message(WARNING "RAWPROC_WITH_LIBRAW=ON but LibRaw not found. Provide a toolchain (e.g., vcpkg) or install libraw-dev.")
    endif()
  endif()
endif()
