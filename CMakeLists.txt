cmake_minimum_required (VERSION 3.13.0)
project (GeographicLib)

# Version information
set (PROJECT_VERSION_MAJOR 2)
set (PROJECT_VERSION_MINOR 3)
set (PROJECT_VERSION_PATCH 0)
set (PROJECT_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")
if (PROJECT_VERSION_PATCH GREATER 0)
  set (PROJECT_VERSION "${PROJECT_VERSION}.${PROJECT_VERSION_PATCH}")
endif ()

# The library version tracks the numbering given by libtool in the
# autoconf set up.
set (LIBVERSION_API 26)
set (LIBVERSION_BUILD 26.0.0)
string (TOLOWER ${PROJECT_NAME} PROJECT_NAME_LOWER)
string (TOUPPER ${PROJECT_NAME} PROJECT_NAME_UPPER)

# User-settable variables

# (1) Where to look for data files.  Various classes look in the geoids,
# gravity, magnetic, subdirectories of ${GEOGRAPHICLIB_DATA}.
set (GEOGRAPHICLIB_DATA
  "/usr/local/share/${PROJECT_NAME}"
  CACHE PATH "Location for data for GeographicLib")

# (5) Set the default "real" precision.  This should probably be left
# at 2 (double).
set (GEOGRAPHICLIB_PRECISION 2 CACHE STRING
  "Precision: 1 = float, 2 = double")
set_property (CACHE GEOGRAPHICLIB_PRECISION PROPERTY STRINGS 1 2)

set (GEOGRAPHICLIB_STATIC_LIB ON)
set (PROJECT_STATIC_LIBRARIES GeographicLib_STATIC)

set (PROJECT_LIBRARIES ${PROJECT_STATIC_LIBRARIES})
set (PROJECT_DEFINITIONS ${PROJECT_STATIC_DEFINITIONS})

set (PROJECT_INTERFACE_LIBRARIES GeographicLib)
set (PROJECT_ALL_LIBRARIES
  ${PROJECT_STATIC_LIBRARIES}
  ${PROJECT_INTERFACE_LIBRARIES})

set (LIBNAME "GeographicLib")

include (CheckTypeSize)
check_type_size ("long double" LONG_DOUBLE BUILTIN_TYPES_ONLY)
check_type_size ("double" DOUBLE BUILTIN_TYPES_ONLY)
if (HAVE_LONG_DOUBLE AND (LONG_DOUBLE GREATER DOUBLE))
  set (GEOGRAPHICLIB_HAVE_LONG_DOUBLE TRUE)
else ()
  set (GEOGRAPHICLIB_HAVE_LONG_DOUBLE FALSE)
endif ()
include (TestBigEndian)
test_big_endian (GEOGRAPHICLIB_WORDS_BIGENDIAN)

# We require C++11
set (CMAKE_CXX_STANDARD 11)
set (CMAKE_CXX_STANDARD_REQUIRED ON)

# Make the compiler more picky.
include (CheckCXXCompilerFlag)
set (FLOAT_CONVERSION_FLAG "-Wfloat-conversion")
check_cxx_compiler_flag (${FLOAT_CONVERSION_FLAG} FLOAT_CONVERSION)
if (NOT FLOAT_CONVERSION)
  set (FLOAT_CONVERSION_FLAG)
endif ()
set (CMAKE_CXX_FLAGS
  "${CMAKE_CXX_FLAGS} -Wall -Wextra ${FLOAT_CONVERSION_FLAG}")
if (CONVERT_WARNINGS_TO_ERRORS)
  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror")
endif ()

# Tell Intel compiler to do arithmetic accurately.  This is needed to
# stop the compiler from ignoring parentheses in expressions like
# (a + b) + c and from simplifying 0.0 + x to x (which is wrong if
# x = -0.0).
if (CMAKE_CXX_COMPILER_ID STREQUAL "Intel")
  set (CMAKE_CXX_FLAGS
    "${CMAKE_CXX_FLAGS} -fp-model precise -diag-disable=11074,11076")
endif ()

set (HIGHPREC_LIBRARIES)
if (GEOGRAPHICLIB_PRECISION EQUAL 1)
  message (WARNING "Compiling with floats which results in poor accuracy")
elseif (GEOGRAPHICLIB_PRECISION EQUAL 2)
  # This is the default
endif ()

# Create a Config.h to expose system information to the compiler
configure_file (
  libraries/AP_Geoid/Config.h.in
  libraries/AP_Geoid/Config.h
@ONLY)

# The list of subdirectories to process
add_subdirectory (libraries/AP_Geoid)
add_subdirectory (libraries/AP_Geoid/examples)
