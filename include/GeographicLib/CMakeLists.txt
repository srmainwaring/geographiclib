# Install the header files including the generated Config.h (which is in
# the build tree).
set (HEADERS
  Constants.hpp
  Geoid.hpp
  Math.hpp
  Utility.hpp
  )
if (INCDIR)
  install (FILES ${HEADERS} DESTINATION ${INCDIR}/GeographicLib)
  install (FILES ${CMAKE_CURRENT_BINARY_DIR}/Config.h
    DESTINATION ${INCDIR}/GeographicLib)
endif ()
