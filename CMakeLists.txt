cmake_minimum_required(VERSION 3.15)
project(SubtitleGenerator VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

option(USE_FFMPEG "Enable FFmpeg for multi-format audio decoding" ON)
option(USE_GPU     "Allow GPU acceleration (CUDA on Windows/Linux, Metal on macOS)" ON)
option(FORCE_CPU   "Force CPU-only build even if a GPU backend is detected" OFF)

set(SUBGEN_GPU_BACKEND "CPU")

if(USE_GPU AND NOT FORCE_CPU)
    # --- Metal (macOS) ---
    if(APPLE)
        set(GGML_METAL ON CACHE BOOL "" FORCE)
        set(SUBGEN_GPU_BACKEND "Metal")
        message(STATUS "Metal enabled -- building with Apple GPU support")
    endif()

    # --- CUDA (NVIDIA GPUs on Windows/Linux) ---
    if(WIN32 OR (UNIX AND NOT APPLE))
        find_package(CUDAToolkit QUIET)
        if(CUDAToolkit_FOUND)
            set(GGML_CUDA ON CACHE BOOL "" FORCE)
            set(SUBGEN_GPU_BACKEND "CUDA")
            message(STATUS "CUDA toolkit found -- building with CUDA support")
        else()
            include(CheckLanguage)
            check_language(CUDA)
            if(CMAKE_CUDA_COMPILER)
                enable_language(CUDA)
                set(GGML_CUDA ON CACHE BOOL "" FORCE)
                set(SUBGEN_GPU_BACKEND "CUDA")
                message(STATUS "CUDA toolkit found (fallback) -- building with CUDA support")
            endif()
        endif()
    endif()

    # --- Vulkan (cross-platform GPU: NVIDIA / AMD / Intel) ---
    # Vulkan is the most universal GPU backend — works on all modern GPUs
    # across Windows and Linux. It picks the best available GPU at runtime.
    if(WIN32 OR (UNIX AND NOT APPLE))
        find_package(Vulkan QUIET COMPONENTS glslc)
        if(Vulkan_FOUND AND Vulkan_glslc_FOUND)
            set(GGML_VULKAN ON CACHE BOOL "" FORCE)
            if(SUBGEN_GPU_BACKEND STREQUAL "CPU")
                set(SUBGEN_GPU_BACKEND "Vulkan")
            else()
                # already has CUDA, so we describe the full picture
                set(SUBGEN_GPU_BACKEND "${SUBGEN_GPU_BACKEND}+Vulkan")
            endif()
            message(STATUS "Vulkan SDK found -- building with Vulkan GPU support")
        else()
            message(STATUS "No Vulkan SDK found. "
                            "Install Vulkan SDK from https://vulkan.lunarg.com/ "
                            "to enable cross-platform GPU acceleration.")
        endif()
    endif()

    if(SUBGEN_GPU_BACKEND STREQUAL "CPU")
        message(STATUS "No GPU backend available -- building CPU-only. "
                        "(Install CUDA toolkit for NVIDIA GPUs or "
                        "Vulkan SDK for any GPU to enable acceleration.)")
    endif()
else()
    message(STATUS "GPU acceleration disabled (USE_GPU=OFF or FORCE_CPU=ON).")
endif()

message(STATUS "=== SubtitleGenerator GPU backend(s): ${SUBGEN_GPU_BACKEND} ===")

if(WIN32)
    add_definitions(-D_USE_MATH_DEFINES)
    add_definitions(-D_WIN32_WINNT=0x0601)
    add_definitions(-DNOMINMAX)
endif()

if(MSVC)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(WHISPER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/whisper.cpp")

if(NOT EXISTS "${WHISPER_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "whisper.cpp submodule not found at '${WHISPER_DIR}'.\n"
        "Clone with submodules:\n"
        "  git clone --recursive https://github.com/SyntX34/subtitle-generator.git\n"
        "or, if already cloned:\n"
        "  git submodule update --init --recursive")
endif()

set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER   OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS      OFF CACHE BOOL "" FORCE)

add_subdirectory(${WHISPER_DIR} whisper_build EXCLUDE_FROM_ALL)

set(APP_SOURCES
    src/main.cpp
    src/SubtitleGenerator.cpp
    src/TimestampFormatter.cpp
    src/AudioDecoder.cpp
)

if(WIN32)
    list(APPEND APP_SOURCES src/dl_windows.cpp)
endif()

add_executable(subtitle_generator ${APP_SOURCES})

target_include_directories(subtitle_generator PRIVATE
    src
    ${WHISPER_DIR}/include
    ${WHISPER_DIR}/ggml/include
)

target_link_libraries(subtitle_generator PRIVATE
    whisper
    ggml
    ggml-base
    ggml-cpu
)

if(APPLE)
    if(TARGET ggml-metal)
        target_link_libraries(subtitle_generator PRIVATE ggml-metal)
    endif()
    find_library(FOUNDATION_LIB Foundation REQUIRED)
    target_link_libraries(subtitle_generator PRIVATE ${FOUNDATION_LIB})
endif()

if(WIN32 OR (UNIX AND NOT APPLE))
    if(TARGET ggml-vulkan)
        target_link_libraries(subtitle_generator PRIVATE ggml-vulkan)
        message(STATUS "ggml-vulkan target found -- Vulkan GPU backend linked")
    endif()
endif()

if(WIN32)
    target_link_libraries(subtitle_generator PRIVATE winmm ws2_32)
endif()

# Pass GPU backend info as compile definitions to the source code.
# These drive GPU detection at runtime via SUBGEN_HAVE_CUDA / SUBGEN_HAVE_METAL / SUBGEN_HAVE_VULKAN.
target_compile_definitions(subtitle_generator PRIVATE
    SUBGEN_GPU_BACKEND="${SUBGEN_GPU_BACKEND}"
)
if(GGML_CUDA)
    target_compile_definitions(subtitle_generator PRIVATE SUBGEN_HAVE_CUDA=1)
endif()
if(GGML_METAL)
    target_compile_definitions(subtitle_generator PRIVATE SUBGEN_HAVE_METAL=1)
endif()
if(GGML_VULKAN)
    target_compile_definitions(subtitle_generator PRIVATE SUBGEN_HAVE_VULKAN=1)
endif()

if(MSVC)
    target_compile_options(subtitle_generator PRIVATE
        /utf-8                
        /W3 /wd4996 /wd4267 /wd4244 /wd4099
        /D_CRT_SECURE_NO_WARNINGS
    )
    target_link_options(subtitle_generator PRIVATE /LARGEADDRESSAWARE)
elseif(MINGW)
    target_compile_options(subtitle_generator PRIVATE
        -finput-charset=UTF-8 -fexec-charset=UTF-8
        -Wall -Wno-deprecated-declarations
    )
else()
    target_compile_options(subtitle_generator PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

if(APPLE)
    find_library(FOUNDATION_LIB Foundation REQUIRED)
    target_link_libraries(subtitle_generator PRIVATE ${FOUNDATION_LIB})
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
        find_library(FFMPEG_avformat_LIB   NAMES avformat   PATHS "${FFMPEG_ROOT}/lib" NO_DEFAULT_PATH)
        find_library(FFMPEG_avcodec_LIB    NAMES avcodec    PATHS "${FFMPEG_ROOT}/lib" NO_DEFAULT_PATH)
        find_library(FFMPEG_avutil_LIB     NAMES avutil     PATHS "${FFMPEG_ROOT}/lib" NO_DEFAULT_PATH)
        find_library(FFMPEG_swresample_LIB NAMES swresample PATHS "${FFMPEG_ROOT}/lib" NO_DEFAULT_PATH)

        if(FFMPEG_INCLUDE_DIR AND FFMPEG_avformat_LIB AND FFMPEG_avcodec_LIB
           AND FFMPEG_avutil_LIB AND FFMPEG_swresample_LIB)
            set(FFMPEG_FOUND TRUE)
            set(FFMPEG_INCLUDE_DIRS ${FFMPEG_INCLUDE_DIR})
            set(FFMPEG_LIBRARIES
                ${FFMPEG_avformat_LIB} ${FFMPEG_avcodec_LIB}
                ${FFMPEG_avutil_LIB}   ${FFMPEG_swresample_LIB})
            if(WIN32 AND EXISTS "${FFMPEG_ROOT}/bin")
                file(GLOB FFMPEG_RUNTIME_DLLS "${FFMPEG_ROOT}/bin/*.dll")
            endif()
        endif()
    endif()

    if(NOT FFMPEG_FOUND AND APPLE)
        find_program(BREW_EXECUTABLE brew)
        if(BREW_EXECUTABLE)
            execute_process(
                COMMAND ${BREW_EXECUTABLE} --prefix ffmpeg
                OUTPUT_VARIABLE FFMPEG_BREW_PREFIX
                OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE FFMPEG_BREW_RESULT
            )
        endif()

        if(FFMPEG_BREW_RESULT EQUAL 0 AND FFMPEG_BREW_PREFIX)
            find_path(FFMPEG_INCLUDE_DIR libavformat/avformat.h
                PATHS "${FFMPEG_BREW_PREFIX}/include" NO_DEFAULT_PATH)
            find_library(FFMPEG_avformat_LIB   NAMES avformat   PATHS "${FFMPEG_BREW_PREFIX}/lib" NO_DEFAULT_PATH)
            find_library(FFMPEG_avcodec_LIB    NAMES avcodec    PATHS "${FFMPEG_BREW_PREFIX}/lib" NO_DEFAULT_PATH)
            find_library(FFMPEG_avutil_LIB     NAMES avutil     PATHS "${FFMPEG_BREW_PREFIX}/lib" NO_DEFAULT_PATH)
            find_library(FFMPEG_swresample_LIB NAMES swresample PATHS "${FFMPEG_BREW_PREFIX}/lib" NO_DEFAULT_PATH)

            if(FFMPEG_INCLUDE_DIR AND FFMPEG_avformat_LIB AND FFMPEG_avcodec_LIB
               AND FFMPEG_avutil_LIB AND FFMPEG_swresample_LIB)
                set(FFMPEG_FOUND TRUE)
                set(FFMPEG_INCLUDE_DIRS ${FFMPEG_INCLUDE_DIR})
                set(FFMPEG_LIBRARIES
                    ${FFMPEG_avformat_LIB} ${FFMPEG_avcodec_LIB}
                    ${FFMPEG_avutil_LIB}   ${FFMPEG_swresample_LIB})
            endif()
        endif()

        if(NOT FFMPEG_FOUND)
            foreach(_brew_prefix /opt/homebrew /usr/local)
                find_path(FFMPEG_INCLUDE_DIR libavformat/avformat.h
                    PATHS "${_brew_prefix}/include" NO_DEFAULT_PATH)
                find_library(FFMPEG_avformat_LIB   NAMES avformat   PATHS "${_brew_prefix}/lib" NO_DEFAULT_PATH)
                find_library(FFMPEG_avcodec_LIB    NAMES avcodec    PATHS "${_brew_prefix}/lib" NO_DEFAULT_PATH)
                find_library(FFMPEG_avutil_LIB     NAMES avutil     PATHS "${_brew_prefix}/lib" NO_DEFAULT_PATH)
                find_library(FFMPEG_swresample_LIB NAMES swresample PATHS "${_brew_prefix}/lib" NO_DEFAULT_PATH)

                if(FFMPEG_INCLUDE_DIR AND FFMPEG_avformat_LIB AND FFMPEG_avcodec_LIB
                   AND FFMPEG_avutil_LIB AND FFMPEG_swresample_LIB)
                    set(FFMPEG_FOUND TRUE)
                    set(FFMPEG_INCLUDE_DIRS ${FFMPEG_INCLUDE_DIR})
                    set(FFMPEG_LIBRARIES
                        ${FFMPEG_avformat_LIB} ${FFMPEG_avcodec_LIB}
                        ${FFMPEG_avutil_LIB}   ${FFMPEG_swresample_LIB})
                    break()
                endif()
                unset(FFMPEG_INCLUDE_DIR CACHE)
                unset(FFMPEG_avformat_LIB CACHE)
                unset(FFMPEG_avcodec_LIB CACHE)
                unset(FFMPEG_avutil_LIB CACHE)
                unset(FFMPEG_swresample_LIB CACHE)
            endforeach()
        endif()
    endif()

    if(FFMPEG_FOUND)
        message(STATUS "FFmpeg found -- multi-format audio decoding enabled (mp3/mp4/mkv/aac/flac/...)")
        target_include_directories(subtitle_generator PRIVATE ${FFMPEG_INCLUDE_DIRS})
        target_link_directories(subtitle_generator PRIVATE ${FFMPEG_LIBRARY_DIRS})
        target_link_libraries(subtitle_generator PRIVATE ${FFMPEG_LIBRARIES})
        target_compile_definitions(subtitle_generator PRIVATE HAVE_FFMPEG=1)
    else()
        message(STATUS "FFmpeg NOT found -- only WAV input will be supported.")
        message(STATUS "  Windows : download a prebuilt shared package from "
                        "https://www.gyan.dev/ffmpeg/builds/ and pass "
                        "-DFFMPEG_ROOT=C:/path/to/ffmpeg (see .github/workflows/build.yml)")
        message(STATUS "  macOS   : brew install pkg-config ffmpeg")
        message(STATUS "  Linux   : sudo apt install libavformat-dev libavcodec-dev libswresample-dev libavutil-dev")
    endif()
endif()

install(TARGETS subtitle_generator RUNTIME DESTINATION .)
if(WIN32)
    install(FILES $<TARGET_RUNTIME_DLLS:subtitle_generator>
            DESTINATION . OPTIONAL)

    if(DEFINED FFMPEG_RUNTIME_DLLS)
        install(FILES ${FFMPEG_RUNTIME_DLLS} DESTINATION .)
        add_custom_command(TARGET subtitle_generator POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${FFMPEG_RUNTIME_DLLS}
                $<TARGET_FILE_DIR:subtitle_generator>
            COMMENT "Copying FFmpeg DLLs next to subtitle_generator.exe")
    endif()
endif()

message(STATUS "=== SubtitleGenerator Build Summary ===")
message(STATUS "  Build type     : ${CMAKE_BUILD_TYPE}")
message(STATUS "  GPU backend    : ${SUBGEN_GPU_BACKEND}")
message(STATUS "  FFmpeg enabled : ${FFMPEG_FOUND}")