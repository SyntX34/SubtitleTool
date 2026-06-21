cmake_minimum_required(VERSION 3.15)
project(SubtitleGenerator VERSION 2.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)
option(USE_FFMPEG "Enable FFmpeg for multi-format audio decoding" ON)

if(WIN32)
    add_definitions(-DWHISPER_NO_OPENMP)
    add_definitions(-D_USE_MATH_DEFINES)
    add_definitions(-DWHISPER_VERSION="1.7.0")
    add_definitions(-DGGML_VERSION="0.1.0")
    add_definitions(-DGGML_COMMIT="unknown")
    add_definitions(-DGGML_USE_CPU)
    add_definitions(-DGGML_USE_CPU_ONLY)
    add_definitions(-D_WIN32_WINNT=0x0601)
    add_definitions(-DGGML_BUILTIN_BACKENDS)
endif()

if(MSVC)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    add_definitions(-D_XOPEN_SOURCE=600)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(WHISPER_DIR    "third_party/whisper.cpp")
set(WHISPER_SRC    "${WHISPER_DIR}/src")
set(WHISPER_INC    "${WHISPER_DIR}/include")
set(GGML_DIR       "${WHISPER_DIR}/ggml")
set(GGML_SRC       "${GGML_DIR}/src")
set(GGML_CPU_DIR   "${GGML_SRC}/ggml-cpu")

include_directories(
    src
    ${WHISPER_INC}
    ${WHISPER_SRC}
    ${GGML_DIR}/include
    ${GGML_SRC}
    ${GGML_CPU_DIR}
    ${GGML_CPU_DIR}/arch/x86
)

set(GGML_C_SOURCES
    ${GGML_SRC}/ggml.c
    ${GGML_SRC}/ggml-alloc.c
    ${GGML_SRC}/ggml-quants.c
)

set(GGML_CPP_SOURCES
    ${GGML_SRC}/ggml-backend.cpp
    ${GGML_SRC}/ggml-backend-reg.cpp
    ${GGML_SRC}/ggml-opt.cpp
    ${GGML_SRC}/ggml-threading.cpp
    ${GGML_SRC}/ggml.cpp
    ${GGML_SRC}/gguf.cpp
)

foreach(_extra
        ${GGML_SRC}/ggml-backend-meta.cpp
        ${GGML_SRC}/ggml-aarch64.cpp
        ${GGML_SRC}/ggml-cpu-aarch64.cpp)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_extra}")
        list(APPEND GGML_CPP_SOURCES ${_extra})
    endif()
endforeach()

set(GGML_CPU_SOURCES
    ${GGML_CPU_DIR}/ggml-cpu.c
    ${GGML_CPU_DIR}/ggml-cpu.cpp
    ${GGML_CPU_DIR}/ops.cpp
    ${GGML_CPU_DIR}/quants.c
    ${GGML_CPU_DIR}/repack.cpp
    ${GGML_CPU_DIR}/vec.cpp
    ${GGML_CPU_DIR}/binary-ops.cpp
    ${GGML_CPU_DIR}/unary-ops.cpp
    ${GGML_CPU_DIR}/traits.cpp
    ${GGML_CPU_DIR}/hbm.cpp
    ${GGML_CPU_DIR}/arch/x86/quants.c
    ${GGML_CPU_DIR}/arch/x86/repack.cpp
    ${GGML_CPU_DIR}/arch/x86/cpu-feats.cpp
)

set(WHISPER_SOURCES
    ${WHISPER_SRC}/whisper.cpp
)

set(APP_SOURCES
    src/main.cpp
    src/SubtitleGenerator.cpp
    src/TimestampFormatter.cpp
    src/AudioDecoder.cpp
)

if(WIN32)
    list(APPEND APP_SOURCES src/dl_windows.cpp)
endif()

set(ALL_SOURCES
    ${APP_SOURCES}
    ${WHISPER_SOURCES}
    ${GGML_C_SOURCES}
    ${GGML_CPP_SOURCES}
    ${GGML_CPU_SOURCES}
)

add_executable(subtitle_generator ${ALL_SOURCES})

if(WIN32)
    target_link_libraries(subtitle_generator PRIVATE winmm ws2_32)
endif()

if(MSVC)
    target_compile_options(subtitle_generator PRIVATE
        /W0 /wd4996 /wd4267 /wd4244 /wd4099
        /D_CRT_SECURE_NO_WARNINGS
        /F 8388608
    )
    target_link_options(subtitle_generator PRIVATE /LARGEADDRESSAWARE)
endif()

if(MINGW)
    target_compile_options(subtitle_generator PRIVATE -w -Wno-deprecated-declarations)
endif()

if(USE_FFMPEG)
    if(NOT DEFINED FFMPEG_ROOT AND DEFINED ENV{FFMPEG_ROOT})
        set(FFMPEG_ROOT "$ENV{FFMPEG_ROOT}")
    endif()

    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(FFMPEG QUIET libavformat libavcodec libavutil libswresample)
    endif()

    if(NOT FFMPEG_FOUND AND DEFINED FFMPEG_ROOT)
        find_path(FFMPEG_INCLUDE_DIR libavformat/avformat.h
            PATHS "${FFMPEG_ROOT}/include" NO_DEFAULT_PATH)
        find_library(FFMPEG_avformat  NAMES avformat  PATHS "${FFMPEG_ROOT}/lib" NO_DEFAULT_PATH)
        find_library(FFMPEG_avcodec   NAMES avcodec   PATHS "${FFMPEG_ROOT}/lib" NO_DEFAULT_PATH)
        find_library(FFMPEG_avutil    NAMES avutil    PATHS "${FFMPEG_ROOT}/lib" NO_DEFAULT_PATH)
        find_library(FFMPEG_swresample NAMES swresample PATHS "${FFMPEG_ROOT}/lib" NO_DEFAULT_PATH)

        if(FFMPEG_INCLUDE_DIR AND FFMPEG_avformat)
            set(FFMPEG_FOUND TRUE)
            set(FFMPEG_INCLUDE_DIRS ${FFMPEG_INCLUDE_DIR})
            set(FFMPEG_LIBRARIES ${FFMPEG_avformat} ${FFMPEG_avcodec} ${FFMPEG_avutil} ${FFMPEG_swresample})
        endif()
    endif()

    if(FFMPEG_FOUND)
        message(STATUS "FFmpeg found — multi-format audio decoding enabled (mp3/mp4/mkv/aac/flac...)")
        target_include_directories(subtitle_generator PRIVATE ${FFMPEG_INCLUDE_DIRS})
        target_link_libraries(subtitle_generator PRIVATE ${FFMPEG_LIBRARIES})
        target_compile_definitions(subtitle_generator PRIVATE HAVE_FFMPEG=1)
    else()
        message(STATUS "FFmpeg NOT found — only WAV input supported.")
        message(STATUS "  Windows: cmake .. -DFFMPEG_ROOT=D:/FFMPEG")
        message(STATUS "  Linux  : sudo apt install libavformat-dev libavcodec-dev libswresample-dev libavutil-dev")
    endif()
endif()

message(STATUS "=== SubtitleGenerator Build ===")
message(STATUS "  App sources    : ${APP_SOURCES}")
message(STATUS "  GGML C++       : ${GGML_CPP_SOURCES}")
message(STATUS "  Build type     : ${CMAKE_BUILD_TYPE}")
message(STATUS "  FFmpeg enabled : ${FFMPEG_FOUND}")