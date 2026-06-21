cmake_minimum_required(VERSION 3.15)
project(SubtitleGenerator VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(USE_FFMPEG "Enable FFmpeg for multi-format audio decoding" ON)
option(USE_GPU     "Allow GPU acceleration (CUDA on Windows/Linux, Metal on macOS)" ON)
option(FORCE_CPU   "Force CPU-only build even if a GPU backend is detected" OFF)

# ---------------------------------------------------------------------------
# GPU backend auto-detection
#
# We don't hand-pick GGML source files (that approach silently bit-rots as
# upstream whisper.cpp evolves). Instead we build whisper.cpp's own CMake
# project as a subdirectory and let it decide which GGML backends to
# compile in. We just toggle the right backend option for the host OS:
#
#   Windows / Linux + NVIDIA toolkit available -> GGML_CUDA
#   macOS                                      -> GGML_METAL (default on
#                                                  Apple platforms anyway)
#   anything else / FORCE_CPU                  -> CPU backend only
#
# At runtime, whisper_init_from_file_with_params() with use_gpu=true will
# automatically fall back to CPU if no compatible GPU is actually present,
# so a CUDA-enabled binary still runs fine on a machine with no NVIDIA GPU.
# ---------------------------------------------------------------------------
set(SUBGEN_GPU_BACKEND "CPU")

if(USE_GPU AND NOT FORCE_CPU)
    if(APPLE)
        set(GGML_METAL ON CACHE BOOL "" FORCE)
        set(SUBGEN_GPU_BACKEND "Metal")
    elseif(WIN32 OR (UNIX AND NOT APPLE))
        # Only request CUDA if the CUDA toolkit is actually present at
        # configure time, otherwise leave it off so CPU-only machines
        # (CI runners, laptops with no NVIDIA GPU) still build cleanly.
        include(CheckLanguage)
        check_language(CUDA)
        if(CMAKE_CUDA_COMPILER)
            enable_language(CUDA)
            set(GGML_CUDA ON CACHE BOOL "" FORCE)
            set(SUBGEN_GPU_BACKEND "CUDA")
        else()
            message(STATUS "No CUDA toolkit found at configure time -- building CPU-only. "
                            "(GPU is still used automatically at runtime if you later "
                            "rebuild on a machine with the CUDA toolkit installed.)")
        endif()
    endif()
else()
    message(STATUS "GPU acceleration disabled (USE_GPU=OFF or FORCE_CPU=ON).")
endif()

message(STATUS "=== SubtitleGenerator GPU backend: ${SUBGEN_GPU_BACKEND} ===")

# ---------------------------------------------------------------------------
# Platform / compiler definitions
# ---------------------------------------------------------------------------
if(WIN32)
    add_definitions(-D_USE_MATH_DEFINES)
    add_definitions(-D_WIN32_WINNT=0x0601)
    add_definitions(-DNOMINMAX)
endif()

if(MSVC)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ---------------------------------------------------------------------------
# whisper.cpp (vendored as a git submodule under third_party/whisper.cpp)
# ---------------------------------------------------------------------------
set(WHISPER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/whisper.cpp")

if(NOT EXISTS "${WHISPER_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "whisper.cpp submodule not found at '${WHISPER_DIR}'.\n"
        "Clone with submodules:\n"
        "  git clone --recursive https://github.com/SyntX34/subtitle-generator.git\n"
        "or, if already cloned:\n"
        "  git submodule update --init --recursive")
endif()

# whisper.cpp builds its own examples/tests by default; we only want the
# library.
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER   OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS      OFF CACHE BOOL "" FORCE)

add_subdirectory(${WHISPER_DIR} whisper_build EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# Application sources
# ---------------------------------------------------------------------------
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

target_link_libraries(subtitle_generator PRIVATE whisper)

target_compile_definitions(subtitle_generator PRIVATE
    SUBGEN_VERSION="2.0.0"
    SUBGEN_GPU_BACKEND="${SUBGEN_GPU_BACKEND}"
)

if(SUBGEN_GPU_BACKEND STREQUAL "CUDA")
    target_compile_definitions(subtitle_generator PRIVATE SUBGEN_HAVE_CUDA=1)
elseif(SUBGEN_GPU_BACKEND STREQUAL "Metal")
    target_compile_definitions(subtitle_generator PRIVATE SUBGEN_HAVE_METAL=1)
endif()

if(WIN32)
    target_link_libraries(subtitle_generator PRIVATE winmm ws2_32)
endif()

if(MSVC)
    target_compile_options(subtitle_generator PRIVATE
        /utf-8                 # source AND execution character set = UTF-8
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

# ---------------------------------------------------------------------------
# FFmpeg (optional, multi-format audio/video decoding)
# ---------------------------------------------------------------------------
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
        find_library(FFMPEG_avformat   NAMES avformat   PATHS "${FFMPEG_ROOT}/lib" NO_DEFAULT_PATH)
        find_library(FFMPEG_avcodec    NAMES avcodec    PATHS "${FFMPEG_ROOT}/lib" NO_DEFAULT_PATH)
        find_library(FFMPEG_avutil     NAMES avutil     PATHS "${FFMPEG_ROOT}/lib" NO_DEFAULT_PATH)
        find_library(FFMPEG_swresample NAMES swresample PATHS "${FFMPEG_ROOT}/lib" NO_DEFAULT_PATH)

        if(FFMPEG_INCLUDE_DIR AND FFMPEG_avformat AND FFMPEG_avcodec AND FFMPEG_avutil AND FFMPEG_swresample)
            set(FFMPEG_FOUND TRUE)
            set(FFMPEG_INCLUDE_DIRS ${FFMPEG_INCLUDE_DIR})
            set(FFMPEG_LIBRARIES ${FFMPEG_avformat} ${FFMPEG_avcodec} ${FFMPEG_avutil} ${FFMPEG_swresample})
        endif()
    endif()

    # macOS Homebrew fallback (Apple Silicon and Intel prefixes)
    if(NOT FFMPEG_FOUND AND APPLE)
        foreach(_brew_prefix /opt/homebrew /usr/local)
            if(EXISTS "${_brew_prefix}/include/libavformat/avformat.h")
                set(FFMPEG_INCLUDE_DIRS "${_brew_prefix}/include")
                set(FFMPEG_LIBRARIES
                    "${_brew_prefix}/lib/libavformat.dylib"
                    "${_brew_prefix}/lib/libavcodec.dylib"
                    "${_brew_prefix}/lib/libavutil.dylib"
                    "${_brew_prefix}/lib/libswresample.dylib")
                set(FFMPEG_FOUND TRUE)
                break()
            endif()
        endforeach()
    endif()

    if(FFMPEG_FOUND)
        message(STATUS "FFmpeg found -- multi-format audio decoding enabled (mp3/mp4/mkv/aac/flac/...)")
        target_include_directories(subtitle_generator PRIVATE ${FFMPEG_INCLUDE_DIRS})
        target_link_libraries(subtitle_generator PRIVATE ${FFMPEG_LIBRARIES})
        target_compile_definitions(subtitle_generator PRIVATE HAVE_FFMPEG=1)
    else()
        message(STATUS "FFmpeg NOT found -- only WAV input will be supported.")
        message(STATUS "  Windows : install via vcpkg (vcpkg install ffmpeg) or set -DFFMPEG_ROOT=C:/ffmpeg")
        message(STATUS "  macOS   : brew install ffmpeg")
        message(STATUS "  Linux   : sudo apt install libavformat-dev libavcodec-dev libswresample-dev libavutil-dev")
    endif()
endif()

# ---------------------------------------------------------------------------
# Install rules (used by the release packaging step in CI)
# ---------------------------------------------------------------------------
install(TARGETS subtitle_generator RUNTIME DESTINATION .)
if(WIN32)
    # Pull in CUDA/whisper DLLs that get built alongside a shared whisper
    # build, if BUILD_SHARED_LIBS was overridden by the toolchain.
    install(FILES $<TARGET_RUNTIME_DLLS:subtitle_generator>
            DESTINATION . OPTIONAL)
endif()

message(STATUS "=== SubtitleGenerator Build Summary ===")
message(STATUS "  Build type     : ${CMAKE_BUILD_TYPE}")
message(STATUS "  GPU backend    : ${SUBGEN_GPU_BACKEND}")
message(STATUS "  FFmpeg enabled : ${FFMPEG_FOUND}")
