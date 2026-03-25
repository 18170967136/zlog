# Dynamic configuration examples

# Set include directories to find zlog.h
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../src)

add_executable(simple_demo simple_demo.c)
target_link_libraries(simple_demo PRIVATE zlog pthread)

add_executable(dynamic_demo dynamic_demo.c)
target_link_libraries(dynamic_demo PRIVATE zlog pthread)
