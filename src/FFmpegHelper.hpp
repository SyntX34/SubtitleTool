#pragma once

#include <string>

/// Runtime FFmpeg detection helper.
///
/// On Windows with MSVC, the binary is compiled with /DELAYLOAD for
/// FFmpeg DLLs, so they are loaded lazily on first function call.
/// This helper adds common install paths (D:\FFMPEG\bin, etc.) to
/// the DLL search order before any FFmpeg function is called.
///
/// On other platforms where /DELAYLOAD is unavailable, the helper
/// uses dlopen / dlsym to dynamically load FFmpeg at runtime and
/// provides an available() check.
namespace FFmpegHelper
{

    /// Call at startup. Searches common FFmpeg install directories and
    /// makes them findable. extraPath can be set via --ffmpeg-path.
    /// Returns true if FFmpeg seems available.
    bool init(const std::string &extraPath = "");

    /// True if FFmpeg libraries were found.
    bool available();

}

#ifdef _WIN32
// Windows: Call SearchPathSetup before calling any FFmpeg code.
// This modifies the DLL search path so delay-loaded FFmpeg DLLs
// are found even when not in the exe directory.
namespace FFmpegHelper
{
    /// Add common directories   extraPath to the DLL search path.
    /// Call once at startup.
    void setupSearchPath(const std::string &extraPath = "");
}
#endif