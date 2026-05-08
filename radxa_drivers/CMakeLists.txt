cmake_minimum_required(VERSION 3.15)
project(radxa_drivers VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -Wpedantic -Werror=return-type)

find_package(PkgConfig REQUIRED)
pkg_check_modules(ZMQ REQUIRED libzmq)

# Find nlohmann_json (assuming it's installed or available in system)
# If it's not found as a package, we'll assume it's just a header available via include_directories if needed
# But standard is to find it.

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../oro_base_input_layer/include
    ${ZMQ_INCLUDE_DIRS}
)

add_library(radxa_drivers SHARED
    src/radxa_services.cpp
    src/radxa_drivers_node.cpp
)

target_link_libraries(radxa_drivers
    PRIVATE
    ${ZMQ_LIBRARIES}
)

# Installation rules
install(TARGETS radxa_drivers
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY ../include/ DESTINATION include/oro/hardware)
