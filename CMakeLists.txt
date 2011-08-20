# CMake script for building imageworsener.
#
# TODO: For Windows builds especially, this is a work in progress.
# It sort of works, but has a lot of small problems. It's better to use the
# prebuilt Visual Studio 2008 project files, if possible.
#

cmake_minimum_required (VERSION 2.6)
project (imagew C)

if(WIN32 AND (NOT CYGWIN))
  set(IW_WINDOWS 1)
else()
  set(IW_WINDOWS 0)
endif()

set(CMAKE_BUILD_TYPE Release)

set(ROOT "${CMAKE_HOME_DIRECTORY}")
set(IW_LIBDIRNAME ${CMAKE_BUILD_TYPE})

set(SRC ${ROOT}/src)

include_directories(${SRC})

if(IW_WINDOWS)
  # Use Unicode. (Why isn't this CMake's default, and why isn't there a
  # high-level way to do it?)
  add_definitions(-D_UNICODE -DUNICODE)

  # Use static C runtime. (Why does CMake make this so difficult?)
  foreach(flag_var CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE
       CMAKE_C_FLAGS_MINSIZEREL CMAKE_C_FLAGS_RELWITHDEBINFO)
    if(${flag_var} MATCHES "/MD")
      string(REGEX REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
    endif()
  endforeach()
endif()

if(IW_WINDOWS)
  set(CMAKE_PREFIX_PATH ${IW_LIBDIRNAME})
  set(CMAKE_INCLUDE_PATH ${CMAKE_INCLUDE_PATH}
   ${ROOT}/../zlib
   ${ROOT}/../libpng
   ${ROOT}/../jpeg)
  set(CMAKE_LIBRARY_PATH ${CMAKE_LIBRARY_PATH}
   ${ROOT}/../zlib
   ${ROOT}/../libpng
   ${ROOT}/../jpeg)
endif()

set(IW_SUPPORT_JPEG 0)
set(IW_SUPPORT_PNG 0)
set(IW_SUPPORT_WEBP 0)

find_package(JPEG)
if(JPEG_FOUND)
  set(IW_SUPPORT_JPEG 1)
endif()

find_package(PNG)
if(PNG_FOUND)
  set(IW_SUPPORT_PNG 1)
endif()

##### Ugly hacks.
##### I guess I don't use the right conventions for these libraries.
if(IW_WINDOWS AND (NOT JPEG_FOUND))
  if(EXISTS "${ROOT}/../jpeg/${IW_LIBDIRNAME}/libjpeg.lib")
    set(JPEG_FOUND 1)
    set(JPEG_INCLUDE_DIR "${ROOT}/../jpeg")
    set(JPEG_LIBRARIES "${ROOT}/../jpeg/${IW_LIBDIRNAME}/libjpeg.lib")
    message(STATUS "Found JPEG: ${JPEG_LIBRARIES}")
  endif()
endif()

if(IW_WINDOWS AND (NOT ZLIB_FOUND))
  if(EXISTS "${ROOT}/../zlib/${IW_LIBDIRNAME}/zlib.lib")
    set(ZLIB_FOUND 1)
    set(ZLIB_INCLUDE_DIR "${ROOT}/../zlib")
    set(ZLIB_LIBRARIES "${ROOT}/../zlib/${IW_LIBDIRNAME}/zlib.lib")
    message(STATUS "Found ZLIB: ${ZLIB_LIBRARIES}")
  endif()
endif()

if(IW_WINDOWS AND ZLIB_FOUND AND (NOT PNG_FOUND))
  if(EXISTS "${ROOT}/../libpng/${IW_LIBDIRNAME}/libpng.lib")
    set(PNG_FOUND 1)
    set(PNG_INCLUDE_DIR "${ROOT}/../libpng" "${ZLIB_INCLUDE_DIR}")
    set(PNG_LIBRARIES "${ROOT}/../libpng/${IW_LIBDIRNAME}/libpng.lib")
    message(STATUS "Found PNG: ${PNG_LIBRARIES}")
  endif()
endif()
#####

##### Lame attempt to detect (non-installed) libwebp
set(LIBWEBP_DIR "${ROOT}/../libwebp")
if(IW_WINDOWS)
  set(LIBWEBP_LIBFILE "${LIBWEBP_DIR}/${IW_LIBDIRNAME}/libwebp.lib")
else()
  set(LIBWEBP_LIBFILE "${LIBWEBP_DIR}/src/libwebp.a")
endif()

if(EXISTS "${LIBWEBP_LIBFILE}")
  set(IW_SUPPORT_WEBP 1)
  set(WEBP_INCLUDE_DIR "${LIBWEBP_DIR}/src")
  set(WEBP_LIBRARIES "${LIBWEBP_LIBFILE}")
  message(STATUS "Found WebP: ${WEBP_LIBRARIES}")
endif()
#####

add_definitions(-DIW_SUPPORT_JPEG=${IW_SUPPORT_JPEG})
add_definitions(-DIW_SUPPORT_PNG=${IW_SUPPORT_PNG})
add_definitions(-DIW_SUPPORT_WEBP=${IW_SUPPORT_WEBP})

add_library(imageworsener STATIC
 ${SRC}/imagew-api.c
 ${SRC}/imagew-main.c
 ${SRC}/imagew-opt.c
 ${SRC}/imagew-resize.c
 ${SRC}/imagew-util.c
 ${SRC}/imagew-bmp.c
 ${SRC}/imagew-gif.c
 ${SRC}/imagew-jpeg.c
 ${SRC}/imagew-miff.c
 ${SRC}/imagew-png.c
 ${SRC}/imagew-tiff.c
 ${SRC}/imagew-webp.c)

add_executable(imagew ${SRC}/imagew-cmd.c)

target_link_libraries(imagew imageworsener)

if(IW_SUPPORT_WEBP)
  include_directories(${WEBP_INCLUDE_DIR})
  target_link_libraries(imagew ${WEBP_LIBRARIES})
endif()
if(IW_SUPPORT_PNG)
  include_directories(${PNG_INCLUDE_DIR})
  target_link_libraries(imagew ${PNG_LIBRARIES})
endif()
if(IW_SUPPORT_JPEG)
  include_directories(${JPEG_INCLUDE_DIR})
  target_link_libraries(imagew ${JPEG_LIBRARIES})
endif()

if(NOT IW_WINDOWS)
  target_link_libraries(imagew m)
endif()
