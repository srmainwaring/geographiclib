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
if (APPLE AND GEOGRAPHICLIB_PRECISION EQUAL 5)
  if (GEOGRAPHICLIB_STATIC_LIB)
    target_link_libraries (${PROJECT_STATIC_LIBRARIES} ${HIGHPREC_LIBRARIES})
  endif ()
endif ()

if (GEOGRAPHICLIB_STATIC_LIB)
  target_include_directories (${PROJECT_STATIC_LIBRARIES} PUBLIC
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}>/libraries
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>/libraries
    $<INSTALL_INTERFACE:${INCDIR}>/libraries)
endif ()

# Specify where the library is installed, adding it to the export targets
if (LIBDIR)
  install (TARGETS ${PROJECT_ALL_LIBRARIES}
    EXPORT targets
    # A potentially useful option.  However it's only supported in recent
    # versions of cmake (2.8.12 and later?).  So comment out for now.
    # INCLUDES DESTINATION include
    RUNTIME DESTINATION ${DLLDIR}
    LIBRARY DESTINATION ${LIBDIR}
    ARCHIVE DESTINATION ${LIBDIR})
endif ()

if (MSVC AND PACKAGE_DEBUG_LIBS AND LIBDIR)
  if (GEOGRAPHICLIB_STATIC_LIB)
    install (FILES
      "${PROJECT_BINARY_DIR}/lib/Debug/${LIBNAME}${CMAKE_DEBUG_POSTFIX}.lib"
      DESTINATION ${LIBDIR} CONFIGURATIONS Release)
  endif ()
endif ()

# Put the library into a folder in the IDE
set_target_properties (
  ${PROJECT_STATIC_LIBRARIES}
  PROPERTIES FOLDER library)
