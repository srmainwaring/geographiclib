cmake_minimum_required (VERSION 3.13.0)
project (GeographicLib-examples)

find_package (GeographicLib 2.0 REQUIRED)
include_directories (${GeographicLib_INCLUDE_DIRS})

message("GeographicLib_INCLUDE_DIRS: ${GeographicLib_INCLUDE_DIRS}")
message("GeographicLib_LIBRARIES: ${GeographicLib_LIBRARIES}")
message("GeographicLib_HIGHPREC_LIBRARIES: ${GeographicLib_HIGHPREC_LIBRARIES}")

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set (EXAMPLE_SOURCE
  example-Geoid.cpp
)

get_filename_component(EXAMPLE ${EXAMPLE_SOURCE} NAME_WE)
add_executable(${EXAMPLE} ${EXAMPLE_SOURCE})
target_link_libraries(${EXAMPLE}
  ${GeographicLib_LIBRARIES} ${GeographicLib_HIGHPREC_LIBRARIES})
