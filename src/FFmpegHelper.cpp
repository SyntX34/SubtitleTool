#include "FFmpegHelper.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

namespace FFmpegHelper {

static bool s_available = false;

// ── Common install directories ────────────────────────────────────

static std::vector<std::string> commonDirs() {
    std::vector<std::string> dirs;

#ifdef _WIN32
    // The directory containing the executable
    char exePath[MAX_PATH];
    if (GetModuleFileNameA(nullptr, exePath, MAX_PATH) > 0) {
        std::string exe(exePath);
        auto pos = exe.find_last_of("\\/");
        if (pos != std::string::npos)
            dirs.push_back(exe.substr(0, pos));
    }
    // Common FFmpeg install locations
    dirs.push_back("D:\\FFMPEG\\bin");
    dirs.push_back("D:\\FFmpeg\\bin");
    dirs.push_back("C:\\FFmpeg\\bin");
    dirs.push_back("C:\\Program Files\\FFmpeg\\bin");
    dirs.push_back("C:\\Program Files (x86)\\FFmpeg\\bin");
    dirs.push_back("C:\\tools\\ffmpeg\\bin");      // chocolatey
    dirs.push_back("C:\\msys64\\mingw64\\bin");     // msys2/mingw
    dirs.push_back("C:\\msys64\\ucrt64\\bin");
#elif defined(__APPLE__)
    dirs.push_back("/opt/homebrew/lib");
    dirs.push_back("/usr/local/lib");
    dirs.push_back("/opt/homebrew/opt/ffmpeg/lib");
    dirs.push_back("/usr/local/opt/ffmpeg/lib");
#else
    dirs.push_back("/usr/local/lib");
    dirs.push_back("/usr/lib");
    dirs.push_back("/usr/lib/x86_64-linux-gnu");
    dirs.push_back("/usr/lib/aarch64-linux-gnu");
#endif

    return dirs;
}

// ── On Windows: modify PATH to include FFmpeg directories ─────────

#ifdef _WIN32

static bool dirExists(const std::string& dir) {
    DWORD attr = GetFileAttributesA(dir.c_str());
    return attr != INVALID_FILE_ATTRIBUTES &&
           (attr & FILE_ATTRIBUTE_DIRECTORY);
}

void setupSearchPath(const std::string& extraPath) {
    std::vector<std::string> dirs = commonDirs();

    // Add user-specified path first (highest priority)
    if (!extraPath.empty()) {
        // If extraPath points to a directory containing bin/, add bin/
        std::string binPath = extraPath + "\\bin";
        if (dirExists(binPath))
            dirs.insert(dirs.begin(), binPath);
        else if (dirExists(extraPath))
            dirs.insert(dirs.begin(), extraPath);
    }

    std::string currentPath;
    char* envPath = nullptr;
    size_t envLen = 0;
    if (_dupenv_s(&envPath, &envLen, "PATH") == 0 && envPath) {
        currentPath = envPath;
        free(envPath);
    }

    for (const auto& dir : dirs) {
        if (!dirExists(dir)) continue;

        // Check if this directory has FFmpeg DLLs
        std::string checkPath = dir + "\\avformat-*.dll";
        WIN32_FIND_DATAA ffd;
        HANDLE hFind = FindFirstFileA(checkPath.c_str(), &ffd);
        if (hFind == INVALID_HANDLE_VALUE) continue;
        FindClose(hFind);

        // Add to PATH
        currentPath = dir + ";" + currentPath;
    }

    if (!currentPath.empty())
        SetEnvironmentVariableA("PATH", currentPath.c_str());
}

#endif // _WIN32

// ── On non-Windows: try to dlopen FFmpeg libraries ────────────────

#ifndef _WIN32

static bool tryLoad(const std::vector<std::string>& names) {
    for (const auto& name : names) {
        void* h = dlopen(name.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (h) {
            dlclose(h);
            return true;
        }
    }
    return false;
}

#endif

// ── Public API ────────────────────────────────────────────────────

bool init(const std::string& extraPath) {
    if (s_available) return true;

#ifdef _WIN32
    // On Windows with /DELAYLOAD, we add FFmpeg directories to PATH
    setupSearchPath(extraPath);

    // Verify that at least one FFmpeg DLL can be loaded
    // (just try loading and immediately freeing one)
    const char* testNames[] = {
        "avformat-61.dll", "avformat-60.dll", "avformat-59.dll",
        "avformat-58.dll", "avformat.dll",
        nullptr
    };
    for (int i = 0; testNames[i]; ++i) {
        HMODULE h = LoadLibraryA(testNames[i]);
        if (h) {
            FreeLibrary(h);
            s_available = true;
            return true;
        }
    }
#else
    // On non-Windows, try to find and pre-load the libraries
    // First check common dirs
    for (const auto& dir : commonDirs()) {
        std::string base = dir + "/";
        bool ok = true;
        for (const auto& libs : {
            std::vector<std::string>{
                "libavformat.so.61", "libavformat.so.60",
                "libavformat.so.59", "libavformat.so.58",
                "libavformat.so"},
            std::vector<std::string>{
                "libavcodec.so.61", "libavcodec.so.60",
                "libavcodec.so.59", "libavcodec.so.58",
                "libavcodec.so"},
            std::vector<std::string>{
                "libavutil.so.59", "libavutil.so.58",
                "libavutil.so.57", "libavutil.so.56",
                "libavutil.so"},
            std::vector<std::string>{
                "libswresample.so.5", "libswresample.so.4",
                "libswresample.so.3", "libswresample.so"}
        }) {
            bool found = false;
            for (const auto& lib : libs) {
                void* h = dlopen((base + lib).c_str(), RTLD_NOW | RTLD_LOCAL);
                if (h) { dlclose(h); found = true; break; }
            }
            if (!found) { ok = false; break; }
        }
        if (ok) {
            s_available = true;
            return true;
        }
    }
#endif

    return false;
}

bool available() {
    return s_available;
}

} // namespace FFmpegHelper
