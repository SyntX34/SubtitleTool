#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <filesystem>

HINSTANCE dl_load_library(const std::filesystem::path& path) {
    return LoadLibraryW(path.wstring().c_str());
}

void* dl_get_sym(HINSTANCE handle, const char* symbol) {
    return reinterpret_cast<void*>(GetProcAddress(handle, symbol));
}

const char* dl_error(void) {
    static char error_msg[256];
    DWORD error = GetLastError();
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                   error_msg, sizeof(error_msg), NULL);
    return error_msg;
}

#endif