set (EXAMPLE_SOURCE
  example-Geoid.cpp
)

get_filename_component(EXAMPLE ${EXAMPLE_SOURCE} NAME_WE)

add_executable(${EXAMPLE} ${EXAMPLE_SOURCE})

target_link_libraries(${EXAMPLE}
  ${PROJECT_STATIC_LIBRARIES}
)
