cmake_minimum_required(VERSION 3.10)
project(filterexamples LANGUAGES CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(FFMPEG_PREFIX_PATH "/usr/local/Cellar/ffmpeg/6.0/")

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(ALL_FFMPEG_LIBS 
    libavutil.a 
    libavdevice.a 
    libavcodec.a
    libavformat.a
    libavfilter.a 
    libswscale.a
    libswresample.a
)

set(AUDIO_MD5_EXAMPLE "audiomd5")
add_executable(${AUDIO_MD5_EXAMPLE} audiomd5.cpp)
target_include_directories(${AUDIO_MD5_EXAMPLE} PUBLIC ${FFMPEG_PREFIX_PATH}/include/)
target_link_directories(${AUDIO_MD5_EXAMPLE} PUBLIC ${FFMPEG_PREFIX_PATH}/lib/)
target_link_libraries(${AUDIO_MD5_EXAMPLE} PUBLIC ${ALL_FFMPEG_LIBS})


set(RESAMPLE_EXAMPLE "resamplefilter")
add_executable(${RESAMPLE_EXAMPLE} resamplefilter.cpp)
target_include_directories(${RESAMPLE_EXAMPLE} PUBLIC ${FFMPEG_PREFIX_PATH}/include/)
target_link_directories(${RESAMPLE_EXAMPLE} PUBLIC ${FFMPEG_PREFIX_PATH}/lib/)
target_link_libraries(${RESAMPLE_EXAMPLE} PUBLIC ${ALL_FFMPEG_LIBS})


set(ASCIIVIDEO_EXAMPLE "filterVideo")
add_executable(${ASCIIVIDEO_EXAMPLE} filterVideo.cpp)
target_include_directories(${ASCIIVIDEO_EXAMPLE} PUBLIC ${FFMPEG_PREFIX_PATH}/include/)
target_link_directories(${ASCIIVIDEO_EXAMPLE} PUBLIC ${FFMPEG_PREFIX_PATH}/lib/)
target_link_libraries(${ASCIIVIDEO_EXAMPLE} PUBLIC ${ALL_FFMPEG_LIBS})


set(IMAGEFILTER_EXAMPLE "filterImage")
add_executable(${IMAGEFILTER_EXAMPLE} filterImage.cpp)
target_include_directories(${IMAGEFILTER_EXAMPLE} PUBLIC ${FFMPEG_PREFIX_PATH}/include/)
target_link_directories(${IMAGEFILTER_EXAMPLE} PUBLIC ${FFMPEG_PREFIX_PATH}/lib/)
target_link_libraries(${IMAGEFILTER_EXAMPLE} PUBLIC ${ALL_FFMPEG_LIBS})


set(OVERLAYFILTER_EXAMPLE "overlayFilter")
add_executable(${OVERLAYFILTER_EXAMPLE} overlayFilter.cpp)
target_include_directories(${OVERLAYFILTER_EXAMPLE} PUBLIC ${FFMPEG_PREFIX_PATH}/include/)
target_link_directories(${OVERLAYFILTER_EXAMPLE} PUBLIC ${FFMPEG_PREFIX_PATH}/lib/)
target_link_libraries(${OVERLAYFILTER_EXAMPLE} PUBLIC ${ALL_FFMPEG_LIBS})


set(TEXTFILTER_EXAMPLE "textFrameScroll")
add_executable(${TEXTFILTER_EXAMPLE} textFrameScroll.cpp)
target_include_directories(${TEXTFILTER_EXAMPLE} PUBLIC ${FFMPEG_PREFIX_PATH}/include/)
target_link_directories(${TEXTFILTER_EXAMPLE} PUBLIC ${FFMPEG_PREFIX_PATH}/lib/)
target_link_libraries(${TEXTFILTER_EXAMPLE} PUBLIC ${ALL_FFMPEG_LIBS})


set(TEXTSCROLL_EXAMPLE "textScroll")
add_executable(${TEXTSCROLL_EXAMPLE} textScroll.cpp)
target_include_directories(${TEXTSCROLL_EXAMPLE} PUBLIC ${FFMPEG_PREFIX_PATH}/include/)
target_link_directories(${TEXTSCROLL_EXAMPLE} PUBLIC ${FFMPEG_PREFIX_PATH}/lib/)
target_link_libraries(${TEXTSCROLL_EXAMPLE} PUBLIC ${ALL_FFMPEG_LIBS})



set(OVERLAYIMAGEVIDEO_EXAMPLE "overlayFilterVideo")
add_executable(${OVERLAYIMAGEVIDEO_EXAMPLE} overlayFilterVideo.cpp)
target_include_directories(${OVERLAYIMAGEVIDEO_EXAMPLE} PUBLIC ${FFMPEG_PREFIX_PATH}/include/)
target_link_directories(${OVERLAYIMAGEVIDEO_EXAMPLE} PUBLIC ${FFMPEG_PREFIX_PATH}/lib/)
target_link_libraries(${OVERLAYIMAGEVIDEO_EXAMPLE} PUBLIC ${ALL_FFMPEG_LIBS})


set(CLASSTEXTSCROLL "classTextScroll")
add_executable(${CLASSTEXTSCROLL} classTextScroll.cpp)
target_include_directories(${CLASSTEXTSCROLL} PUBLIC ${FFMPEG_PREFIX_PATH}/include/)
target_link_directories(${CLASSTEXTSCROLL} PUBLIC ${FFMPEG_PREFIX_PATH}/lib/)
target_link_libraries(${CLASSTEXTSCROLL} PUBLIC ${ALL_FFMPEG_LIBS})