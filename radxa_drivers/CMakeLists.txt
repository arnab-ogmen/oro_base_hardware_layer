cmake_minimum_required(VERSION 3.15)
project(radxa_drivers VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -Wpedantic -Werror=return-type)

find_package(PkgConfig REQUIRED)
pkg_check_modules(ZMQ REQUIRED libzmq)
pkg_check_modules(GST REQUIRED gstreamer-1.0 gstreamer-app-1.0)
find_package(nlohmann_json REQUIRED)
find_package(spdlog REQUIRED)

find_program(FLATC_EXECUTABLE NAMES flatc)
if(NOT FLATC_EXECUTABLE)
    message(FATAL_ERROR "flatc compiler not found! Please install flatbuffers-compiler")
endif()

# FlatBuffers schema generation
set(FBS_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/schemas/video_frame.fbs
    ${CMAKE_CURRENT_SOURCE_DIR}/schemas/audio_frame.fbs
)
set(FBS_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${FBS_OUT_DIR})

set(FBS_HEADERS
    ${FBS_OUT_DIR}/video_frame_generated.h
    ${FBS_OUT_DIR}/audio_frame_generated.h
)

add_custom_command(
    OUTPUT ${FBS_HEADERS}
    COMMAND ${FLATC_EXECUTABLE} -c -o ${FBS_OUT_DIR} ${FBS_SRCS}
    DEPENDS ${FBS_SRCS}
    COMMENT "Generating C++ headers for FlatBuffers schemas"
)

add_custom_target(generate_fbs DEPENDS ${FBS_HEADERS})

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../oro_base_input_layer/include
    ${ZMQ_INCLUDE_DIRS}
    ${GST_INCLUDE_DIRS}
    ${FBS_OUT_DIR}
)

# Existing radxa_drivers library
add_library(radxa_drivers SHARED
    src/radxa_services.cpp
    src/radxa_drivers_node.cpp
)

target_link_libraries(radxa_drivers
    PRIVATE
    ${ZMQ_LIBRARIES}
)

# --- Media Stack ---

# Video Ingestor
add_executable(video_ingestor
    src/media/video_ingestor/main.cpp
    src/media/video_ingestor/video_pipeline.cpp
)
add_dependencies(video_ingestor generate_fbs)
target_link_libraries(video_ingestor
    PRIVATE
    ${ZMQ_LIBRARIES}
    ${GST_LIBRARIES}
    spdlog::spdlog
    flatbuffers
    nlohmann_json::nlohmann_json
)
target_link_directories(video_ingestor PRIVATE ${ZMQ_LIBRARY_DIRS} ${GST_LIBRARY_DIRS})

# Audio Ingestor
add_executable(audio_ingestor
    src/media/audio_ingestor/main.cpp
    src/media/audio_ingestor/audio_pipeline.cpp
)
add_dependencies(audio_ingestor generate_fbs)
target_link_libraries(audio_ingestor
    PRIVATE
    ${ZMQ_LIBRARIES}
    ${GST_LIBRARIES}
    spdlog::spdlog
    flatbuffers
    nlohmann_json::nlohmann_json
)
target_link_directories(audio_ingestor PRIVATE ${ZMQ_LIBRARY_DIRS} ${GST_LIBRARY_DIRS})

# Sync Verifier
add_executable(sync_verifier
    src/media/sync_verifier/main.cpp
)
add_dependencies(sync_verifier generate_fbs)
target_link_libraries(sync_verifier
    PRIVATE
    ${ZMQ_LIBRARIES}
    spdlog::spdlog
    flatbuffers
)
target_link_directories(sync_verifier PRIVATE ${ZMQ_LIBRARY_DIRS})

# Stream Player (Full round-trip validation: ZMQ SUB → FlatBuffers → GStreamer playback)
add_executable(stream_player
    src/media/stream_player/main.cpp
)
add_dependencies(stream_player generate_fbs)
target_link_libraries(stream_player
    PRIVATE
    ${ZMQ_LIBRARIES}
    ${GST_LIBRARIES}
    spdlog::spdlog
    flatbuffers
)
target_link_directories(stream_player PRIVATE ${ZMQ_LIBRARY_DIRS} ${GST_LIBRARY_DIRS})


# Installation rules
install(TARGETS radxa_drivers video_ingestor audio_ingestor stream_player
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY ../include/ DESTINATION include/oro/hardware)
