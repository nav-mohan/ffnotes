cmake_minimum_required(VERSION 3.10)
project(PCM2AAC LANGUAGES CXX C)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(FFMPEG_PREFIX_PATH "/usr/local/Cellar/ffmpeg/6.0/")

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(
    CPP_SOURCE_FILES
    main.cpp
)

add_executable(${PROJECT_NAME} ${CPP_SOURCE_FILES})

target_include_directories(
    ${PROJECT_NAME} 
    PUBLIC 
    ${FFMPEG_PREFIX_PATH}/include/
)
target_link_directories(
    ${PROJECT_NAME} PUBLIC
    ${FFMPEG_PREFIX_PATH}/lib/
)

target_link_libraries(
    ${PROJECT_NAME} PUBLIC 
    libavdevice.a 
    libavcodec.a
    libavformat.a
    libavutil.a 
    libavfilter.a 
    libswscale.a
    libswresample.a
    )
