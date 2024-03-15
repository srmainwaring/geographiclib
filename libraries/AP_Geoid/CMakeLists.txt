# Build the library...

# Include all the .cpp files in the library.
set (SOURCES
  Geoid.cpp
  Math.cpp
  Utility.cpp
)

set (HEADERS
  ${PROJECT_BINARY_DIR}/libraries/AP_Geoid/Config.h
  Constants.hpp
  Geoid.hpp
  Math.hpp
  Utility.hpp
)

# Define the library.
if (GEOGRAPHICLIB_STATIC_LIB)
  add_library (${PROJECT_STATIC_LIBRARIES} STATIC ${SOURCES} ${HEADERS})
  add_library (${PROJECT_NAME}::${PROJECT_STATIC_LIBRARIES}
    ALIAS ${PROJECT_STATIC_LIBRARIES})
endif ()

add_library (${PROJECT_INTERFACE_LIBRARIES} INTERFACE)
add_library (${PROJECT_NAME}::${PROJECT_INTERFACE_LIBRARIES}
  ALIAS ${PROJECT_INTERFACE_LIBRARIES})
target_link_libraries (${PROJECT_INTERFACE_LIBRARIES}
  INTERFACE ${PROJECT_LIBRARIES})

# Set the version number on the library
set_target_properties (
  ${PROJECT_STATIC_LIBRARIES} PROPERTIES
  VERSION "${LIBVERSION_BUILD}" SOVERSION "${LIBVERSION_API}"
  OUTPUT_NAME ${LIBNAME})

if (GEOGRAPHICLIB_STATIC_LIB)
  target_include_directories (${PROJECT_STATIC_LIBRARIES} PUBLIC
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}>/libraries
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>/libraries
    $<INSTALL_INTERFACE:${INCDIR}>/libraries)
endif ()

# Put the library into a folder in the IDE
set_target_properties (
  ${PROJECT_STATIC_LIBRARIES}
  PROPERTIES FOLDER library)
