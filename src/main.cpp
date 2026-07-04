#include "SubtitleGenerator.hpp"
#include "TimestampFormatter.hpp"
#include "AudioDecoder.hpp"
#include "FFmpegHelper.hpp"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <io.h>
#  include <fcntl.h>
#elif defined(__APPLE__)
#  include <sys/sysctl.h>
#  include <sys/types.h>
#endif

namespace fs = std::filesystem;

#ifndef SUBGEN_VERSION
#  define SUBGEN_VERSION "1.1.0"
#endif
#ifndef SUBGEN_GPU_BACKEND
#  define SUBGEN_GPU_BACKEND "CPU"
#endif

static volatile sig_atomic_t g_interrupted = 0;
static void sigint_handler(int) { g_interrupted = 1; }

static void enable_utf8_console() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode))
        SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    _setmode(_fileno(stdout), _O_TEXT);
    _setmode(_fileno(stderr), _O_TEXT);
#endif
}

static void print_hardware_info() {
#ifdef _WIN32
    HKEY hKey;
    char cpu_name[256] = "Unknown CPU";
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
            "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
            0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD size = sizeof(cpu_name);
        RegQueryValueExA(hKey, "ProcessorNameString", nullptr, nullptr,
                         reinterpret_cast<LPBYTE>(cpu_name), &size);
        RegCloseKey(hKey);
    }
    char* p = cpu_name;
    while (*p == ' ') ++p;

    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    GlobalMemoryStatusEx(&mem);
    double ram_gb = mem.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);

    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    DWORD cores = si.dwNumberOfProcessors;

    std::cout << "  CPU     : " << p << "\n";
    std::cout << "  Cores   : " << cores << "\n";
    std::cout << "  RAM     : " << std::fixed << std::setprecision(1)
              << ram_gb << " GB\n";

#elif defined(__APPLE__)
    char cpu_name[256] = "Unknown CPU";
    size_t cpu_len = sizeof(cpu_name);
    if (sysctlbyname("machdep.cpu.brand_string", cpu_name, &cpu_len, nullptr, 0) != 0
        || cpu_name[0] == '\0') {
        cpu_len = sizeof(cpu_name);
        sysctlbyname("hw.model", cpu_name, &cpu_len, nullptr, 0);
    }

    int cores = 0;
    size_t cores_len = sizeof(cores);
    sysctlbyname("hw.ncpu", &cores, &cores_len, nullptr, 0);

    int64_t ram_bytes = 0;
    size_t ram_len = sizeof(ram_bytes);
    sysctlbyname("hw.memsize", &ram_bytes, &ram_len, nullptr, 0);
    double ram_gb = static_cast<double>(ram_bytes) / (1024.0 * 1024.0 * 1024.0);

    std::cout << "  CPU     : " << cpu_name << "\n";
    std::cout << "  Cores   : " << cores << "\n";
    std::cout << "  RAM     : " << std::fixed << std::setprecision(1)
              << ram_gb << " GB\n";

#else // Linux and other POSIX
    {
        std::ifstream f("/proc/cpuinfo");
        std::string line;
        bool found = false;
        while (std::getline(f, line)) {
            if (line.rfind("model name", 0) == 0) {
                auto pos = line.find(':');
                if (pos != std::string::npos) {
                    std::cout << "  CPU     : " << line.substr(pos + 2) << "\n";
                    found = true;
                }
                break;
            }
        }
        if (!found) std::cout << "  CPU     : Unknown\n";
    }
    {
        std::ifstream f("/proc/meminfo");
        std::string line;
        bool found = false;
        while (std::getline(f, line)) {
            if (line.rfind("MemTotal", 0) == 0) {
                long kb = 0;
                sscanf(line.c_str(), "MemTotal: %ld kB", &kb);
                std::cout << "  RAM     : " << std::fixed << std::setprecision(1)
                          << kb / (1024.0 * 1024.0) << " GB\n";
                found = true;
                break;
            }
        }
        if (!found) std::cout << "  RAM     : Unknown\n";
    }
    std::cout << "  Cores   : " << std::thread::hardware_concurrency() << "\n";
#endif

    std::cout << "  Backend : " << SUBGEN_GPU_BACKEND
#if defined(SUBGEN_HAVE_CUDA) || defined(SUBGEN_HAVE_METAL) || defined(SUBGEN_HAVE_VULKAN)
              << " (GPU build -- falls back to CPU automatically if no "
                 "compatible GPU is detected at runtime)"
#else
              << " (CPU-only build)"
#endif
              << "\n";
}

struct Options {
    std::string audio_path;
    std::string model_path     = "models/ggml-base.en.bin";
    std::string output_path;
    std::string format         = "srt";
    std::string language       = "auto";
    double      chunk_secs     = 30.0;
    int         n_threads      = 4;
    bool        translate      = false;
    bool        verbose        = false;
    bool        stream         = false;
    float       min_conf       = 0.0f;
    bool        no_filler      = false;
    bool        all_formats    = false;
    bool        force_cpu      = false;
    std::string ffmpeg_path    = "";
};

static void printBanner() {
    std::cout <<
"+======================================================================+\n"
"|  SubtitleGenerator v" SUBGEN_VERSION "  -  powered by whisper.cpp                |\n"
"|  Author : SyntX  |  github.com/SyntX34                              |\n"
"|  Formats: WAV | MP3 | MP4 | MKV | FLAC | OGG | AAC | ...            |\n"
"|  Output : SRT | VTT | ASS | JSON | TXT                              |\n"
"+======================================================================+\n\n";
}

static void printUsage(const char* prog) {
    std::cout <<
"USAGE\n"
"  " << prog << " <audio_or_video_file> [OPTIONS]\n\n"
"REQUIRED\n"
"  <file>                  Input file. Supported: "
        << AudioDecoder::supportedFormats() << "\n\n"
"MODEL\n"
"  -m, --model <name>      Model name or path (default: base.en)\n"
"                          Examples: base.en, small, medium, ...\n"
"                          Or use full path: models/ggml-base.en.bin\n"
"      --list-models       List all available models with sizes\n\n"
"OUTPUT\n"
"  -o, --output <path>     Output file (default: <input>.<format>)\n"
"  -f, --format <fmt>      srt | vtt | ass | json | txt  (default: srt)\n"
"      --all-formats       Save all formats at once\n\n"
"LANGUAGE & TRANSLATION\n"
"  -l, --lang <code>       Source language spoken in the audio (default: auto)\n"
"                          Run with --list-languages to see every code.\n"
"      --translate         Translate the transcription into English.\n"
"                          Combine with -l to make it faster and more\n"
"                          accurate, e.g. Spanish audio to English subs:\n"
"                            -l es --translate\n"
"                          (whisper.cpp can only translate INTO English;\n"
"                          it cannot translate between two non-English\n"
"                          languages.)\n"
"      --list-languages    Print every supported language code and exit\n\n"
"PROCESSING\n"
"  -t, --threads <n>       Worker threads (default: 4)\n"
"      --chunk <secs>      Audio chunk size in seconds (default: 30)\n"
"      --min-conf <0-1>    Drop segments below this confidence\n"
"      --no-filler         Remove filler words (um, uh, er, ...)\n\n"
"MISC\n"
"  -s, --stream            Print subtitles live as they're generated\n"
"  -v, --verbose           Verbose whisper output\n"
"      --ffmpeg-path <dir> Path to FFmpeg binaries (e.g. D:\\FFMPEG\\bin)\n"
"      --cpu, --force-cpu  Use CPU only, even if compiled with GPU support\n"
"  -h, --help              Show this message\n\n"
"EXAMPLES\n"
"  " << prog << " movie.mp4\n"
"  " << prog << " movie.mp4 -m models/ggml-medium.en.bin -f srt\n"
"  " << prog << " lecture.mp3 -l es --translate -o english.srt\n"
"  " << prog << " film.mkv --all-formats --chunk 60\n\n"
"Press Ctrl+C at any time to stop early; the subtitles generated so far\n"
"are still saved.\n";
}

static void printLanguages() {
    auto langs = SubtitleGenerator::supportedLanguages();
    std::cout << "Supported language codes (" << langs.size() << "):\n\n";
    size_t col = 0;
    for (const auto& pair : langs) {
        std::cout << "  " << std::left << std::setw(4) << pair.first
                  << std::setw(20) << pair.second;
        if (++col % 3 == 0) std::cout << "\n";
    }
    if (col % 3 != 0) std::cout << "\n";
    std::cout << "\nUse 'auto' to let whisper detect the spoken language "
                 "automatically.\n";
}

// ── Model registry ───────────────────────────────────────────────

struct ModelInfo {
    const char* name;        // passed to -m, e.g. "base.en"
    const char* filename;    // actual file, e.g. "ggml-base.en.bin"
    const char* description; // human-readable summary
    const char* size_str;    // download size
    bool  multilingual;      // false = English-only (smaller/faster)
};

static const ModelInfo s_models[] = {
    {"tiny.en",     "ggml-tiny.en.bin",     "Fastest, English-only",     "75 MB",   false},
    {"tiny",        "ggml-tiny.bin",        "Fastest, 100 languages",    "75 MB",   true},
    {"base.en",     "ggml-base.en.bin",     "Default, English-only",     "145 MB",  false},
    {"base",        "ggml-base.bin",         "Default, 100 languages",   "145 MB",  true},
    {"small.en",    "ggml-small.en.bin",    "Good accuracy, English",    "465 MB",  false},
    {"small",       "ggml-small.bin",        "Good accuracy, multi",     "465 MB",  true},
    {"medium.en",   "ggml-medium.en.bin",   "High accuracy, English",   "1.5 GB",  false},
    {"medium",      "ggml-medium.bin",       "High accuracy, multi",    "1.5 GB",  true},
    {"large-v3",    "ggml-large-v3.bin",     "Best accuracy, multi",     "3.0 GB",  true},
    {"large-v3-turbo", "ggml-large-v3-turbo.bin", "Fast large, multi",   "1.5 GB",  true},
    {nullptr, nullptr, nullptr, nullptr, false}
};

static const ModelInfo* findKnownModel(const std::string& name) {
    for (int i = 0; s_models[i].name; ++i) {
        if (name == s_models[i].name) return &s_models[i];
    }
    return nullptr;
}

// ── Local model scanning ─────────────────────────────────────────

struct LocalModel {
    std::string name;        // display name (e.g. "base.en" or custom filename)
    std::string filepath;    // full path to .bin file
    bool        is_known;    // true if it matches a built-in model
    std::string size_str;    // human-readable file size
    std::string description; // from built-in registry or auto-generated
};

/// Scan models/ directory and return all found .bin files.
static std::vector<LocalModel> scanLocalModels() {
    std::vector<LocalModel> models;
    fs::path modelsDir = "models";
    if (!fs::exists(modelsDir)) return models;

    for (const auto& entry : fs::directory_iterator(modelsDir)) {
        if (entry.path().extension() != ".bin") continue;
        if (!fs::is_regular_file(entry)) continue;

        std::string filename = entry.path().filename().string();
        std::string name;
        bool is_known = false;
        std::string desc;
        std::string size_str;

        // Check if this file matches a known model
        // Known files have format "ggml-<name>.bin"
        if (filename.rfind("ggml-", 0) == 0 && filename.size() > 9) {
            std::string stripped = filename.substr(5);  // remove "ggml-"
            if (stripped.size() >= 4 && stripped.rfind(".bin") == stripped.size() - 4) {
                name = stripped.substr(0, stripped.size() - 4);
            } else {
                name = stripped;
            }

            if (const ModelInfo* m = findKnownModel(name)) {
                is_known = true;
                desc = m->description;
                size_str = m->size_str;
            }
        }

        if (name.empty()) {
            name = filename;
            // Remove .bin extension for display
            if (name.size() >= 4 && name.rfind(".bin") == name.size() - 4)
                name = name.substr(0, name.size() - 4);
        }

        if (!is_known) {
            desc = "Custom model";
            // Get file size
            std::error_code ec;
            auto fsize = fs::file_size(entry, ec);
            if (!ec && fsize > 0) {
                if (fsize < 1024 * 1024)
                    size_str = std::to_string(fsize / 1024) + " KB";
                else if (fsize < 1024 * 1024 * 1024)
                    size_str = std::to_string(fsize / (1024 * 1024)) + " MB";
                else
                    size_str = std::to_string(fsize / (1024 * 1024 * 1024)) + " GB";
            } else {
                size_str = "?";
            }
        }

        models.push_back({name, entry.path().string(), is_known, size_str, desc});
    }

    // Sort: known models first, then alphabetically
    std::sort(models.begin(), models.end(), [](const LocalModel& a, const LocalModel& b) {
        if (a.is_known != b.is_known) return a.is_known > b.is_known;
        return a.name < b.name;
    });

    return models;
}

/// Build the full list of models for display/selection.
/// First: local .bin files found in models/.
/// Then: downloadable models not yet present locally.
struct SelectableModel {
    int         index;       // displayed selection number
    std::string name;        // short name for -m flag
    std::string path;        // file path (empty = downloadable only)
    std::string size_str;
    std::string description;
    bool        downloaded;  // true if on disk
};

static std::vector<SelectableModel> collectModels() {
    std::vector<SelectableModel> result;
    auto local = scanLocalModels();

    // Add local models first
    for (size_t i = 0; i < local.size(); ++i) {
        result.push_back({
            (int)i + 1,
            local[i].name,
            local[i].filepath,
            local[i].size_str,
            local[i].description + (local[i].is_known ? "" : " (custom)"),
            true
        });
    }

    // Add downloadable models not already present locally
    int idx = (int)result.size() + 1;
    for (int i = 0; s_models[i].name; ++i) {
        bool alreadyLocal = false;
        for (const auto& local : local) {
            if (local.name == s_models[i].name) { alreadyLocal = true; break; }
        }
        if (!alreadyLocal) {
            result.push_back({
                idx++,
                s_models[i].name,
                "",   // no local path
                s_models[i].size_str,
                std::string(s_models[i].description) + " (download)",
                false
            });
        }
    }

    return result;
}

/// Print the model selection list.
static void printModelMenu(const std::vector<SelectableModel>& all, const std::string& defaultName = "base.en") {
    std::cout << "\nAvailable models:\n\n";
    std::cout << "  #  Name                 Size      Description\n";
    std::cout << "  --------------------------------------------------\n";

    // Count how many are downloaded
    int downloaded = 0;
    for (const auto& m : all) if (m.downloaded) ++downloaded;

    if (downloaded > 0) {
        std::cout << "  -- Local models (in models/ folder) --\n";
        for (const auto& m : all) {
            if (!m.downloaded) continue;
            bool isDefault = (m.name == defaultName);
            std::cout << "  " << std::setw(2) << m.index << "  "
                      << std::left << std::setw(17) << m.name
                      << std::setw(10) << m.size_str
                      << m.description;
            if (isDefault) std::cout << "  (default)";
            std::cout << "\n";
        }
    }

    if ((int)all.size() > downloaded) {
        std::cout << "  -- Downloadable models --\n";
        for (const auto& m : all) {
            if (m.downloaded) continue;
            std::cout << "  " << std::setw(2) << m.index << "  "
                      << std::left << std::setw(17) << m.name
                      << std::setw(10) << m.size_str
                      << m.description << "\n";
        }
    }

    std::cout << "\n  You can select a model by:\n";
    std::cout << "    -m <name>     e.g. -m base.en\n";
    std::cout << "    -m <number>   e.g. -m 1\n";
    std::cout << "    Just press Enter to use the default.\n";
    std::cout << "\n  Download models: https://huggingface.co/ggerganov/whisper.cpp/tree/main\n";
}

/// Show an interactive model picker and return the chosen path.
/// Returns empty string if user cancelled.
static std::string interactiveModelPicker(const std::vector<SelectableModel>& all) {
    printModelMenu(all);

    std::cout << "\n  Select model [1]: ";
    std::cout.flush();

    std::string input;
    std::getline(std::cin, input);

    // Default: use first local model (or first downloadable)
    if (input.empty()) {
        return all.empty() ? "" : all[0].path;
    }

    // Try as index
    char* end = nullptr;
    long num = std::strtol(input.c_str(), &end, 10);
    if (end && *end == '\0' && num >= 1 && num <= (long)all.size()) {
        return all[num - 1].path;
    }

    // Try as name
    for (const auto& m : all) {
        if (m.name == input) return m.path;
    }

    std::cerr << "  Invalid selection. Using default.\n";
    return all.empty() ? "" : all[0].path;
}

/// Print the downloadable models list (for --list-models).
static void printModels(const std::string& defaultModel = "base.en") {
    auto all = collectModels();
    printModelMenu(all, defaultModel);
    std::cout << "\n  Download scripts:\n";
    std::cout << "    Linux/macOS: ./scripts/download_model.sh <name>\n";
    std::cout << "    Windows:     .\\scripts\\download_model.ps1 -Model <name>\n";
}

static bool parse(int argc, char* argv[], Options& opt) {
    if (argc < 2) { printUsage(argv[0]); return false; }

    std::string first = argv[1];
    if (first == "-h" || first == "--help") { printUsage(argv[0]); return false; }
    if (first == "--list-languages") { printLanguages(); return false; }
    if (first == "--list-models")    { printModels(); return false; }

    opt.audio_path = argv[1];

    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << a << "\n";
                std::exit(1);
            }
            return argv[++i];
        };
        if      (a == "-h" || a == "--help")     { printUsage(argv[0]); return false; }
        else if (a == "--list-languages")        { printLanguages(); return false; }
        else if (a == "--list-models")           { printModels(); return false; }
        else if (a == "-m" || a == "--model")    { opt.model_path  = next(); }
        else if (a == "-o" || a == "--output")   { opt.output_path = next(); }
        else if (a == "-f" || a == "--format")   { opt.format      = next(); }
        else if (a == "-l" || a == "--lang")     { opt.language    = next(); }
        else if (a == "-t" || a == "--threads")  { opt.n_threads   = std::stoi(next()); }
        else if (a == "--chunk")                 { opt.chunk_secs  = std::stod(next()); }
        else if (a == "--min-conf")              { opt.min_conf    = std::stof(next()); }
        else if (a == "--translate")             { opt.translate   = true; }
        else if (a == "--no-filler")             { opt.no_filler   = true; }
        else if (a == "--all-formats")           { opt.all_formats = true; }
        else if (a == "-s" || a == "--stream")   { opt.stream      = true; }
        else if (a == "-v" || a == "--verbose")  { opt.verbose     = true; }
        else if (a == "--ffmpeg-path")            { opt.ffmpeg_path = next(); }
        else if (a == "--cpu" || a == "--force-cpu") { opt.force_cpu = true; }
        else { std::cerr << "Unknown option: " << a << "\n"; return false; }
    }

    static const char* valid_formats[] = {"srt", "vtt", "ass", "json", "txt", nullptr};
    bool format_ok = false;
    for (int i = 0; valid_formats[i]; ++i)
        if (opt.format == valid_formats[i]) { format_ok = true; break; }
    if (!format_ok) {
        std::cerr << "Invalid format: " << opt.format
                  << " (expected one of: srt, vtt, ass, json, txt)\n";
        return false;
    }

    if (opt.language != "auto" && !SubtitleGenerator::isValidLanguageCode(opt.language)) {
        std::cerr << "Unknown language code: " << opt.language
                  << "\nRun with --list-languages to see all supported codes.\n";
        return false;
    }

    // Resolve model selection
    // -m <number>   → select by index from combined model list
    // -m <name>     → resolve known name to models/ggml-<name>.bin
    // Full path     → use as-is
    {
        // Check if it's a known model name first
        if (const ModelInfo* m = findKnownModel(opt.model_path)) {
            opt.model_path = std::string("models/") + m->filename;
        }
        // Otherwise check if it could be an index number
        else if (!opt.model_path.empty()) {
            char* end = nullptr;
            long idx = std::strtol(opt.model_path.c_str(), &end, 10);
            if (end && *end == '\0' && idx >= 1) {
                auto all = collectModels();
                if (idx <= (long)all.size()) {
                    const auto& selected = all[idx - 1];
                    if (selected.downloaded) {
                        opt.model_path = selected.path;
                    } else {
                        // Selected model isn't downloaded yet
                        std::cerr << "[ERROR] Model '" << selected.name
                                  << "' is not downloaded.\n";
                        std::cerr << "  Download it first:\n";
                        std::cerr << "    ./scripts/download_model.sh " << selected.name << "\n";
                        std::cerr << "    or: .\\scripts\\download_model.ps1 -Model " << selected.name << "\n";
                        return false;
                    }
                }
            }
        }
    }

    if (opt.output_path.empty()) {
        fs::path p(opt.audio_path);
        opt.output_path = p.stem().string() + "." + opt.format;
    }
    return true;
}

// Live subtitle display — shows the latest transcribed text
// alongside the progress bar.
static std::string g_lastSubtitleText;

static void drawProgress(double cur, double total) {
    if (total <= 0) return;
    int pct    = std::min(100, static_cast<int>(cur / total * 100));
    int filled = pct / 2;  // 50-char bar
    // Build progress bar line with live subtitle text
    std::cout << "\r[";
    for (int i = 0; i < 50; ++i)
        std::cout << (i < filled ? '#' : '-');
    std::cout << "] " << std::setw(3) << pct << "%  "
              << TimestampFormatter::formatDuration(cur) << " / "
              << TimestampFormatter::formatDuration(total);

    // Show latest subtitle text if available
    if (!g_lastSubtitleText.empty() && cur < total) {
        // Truncate to ~60 chars to keep the line manageable
        std::string snippet = g_lastSubtitleText;
        if (snippet.size() > 60) {
            snippet.resize(57);
            snippet += "...";
        }
        std::cout << "  |  " << snippet;
    }

    // Erase to end of line (\033[K) so shorter lines don't leave
    // visible remnants when they follow longer lines
    std::cout << "\033[K" << std::flush;
    if (pct >= 100) std::cout << "\n";
}

int main(int argc, char* argv[]) {
    enable_utf8_console();

    signal(SIGINT,  sigint_handler);
    signal(SIGTERM, sigint_handler);

    printBanner();

    Options opt;
    if (!parse(argc, argv, opt)) return 1;

    // Initialize FFmpeg runtime detection (must be AFTER option parsing
    // so --ffmpeg-path is available)
    FFmpegHelper::init(opt.ffmpeg_path);

    std::cout << "System info:\n";
    print_hardware_info();
    std::cout << "\n";

    if (!fs::exists(opt.audio_path)) {
        std::cerr << "[ERROR] Input file not found: " << opt.audio_path << "\n";
        return 1;
    }

    // Interactive model selection
    // If no -m was given and no models found, show the download list
    // If no -m was given and multiple models exist, show a picker
    if (!fs::exists(opt.model_path)) {
        auto all = collectModels();
        int localCount = 0;
        for (const auto& m : all) if (m.downloaded) ++localCount;

        if (localCount == 0) {
            // No models at all — show download list
            std::cerr << "[ERROR] No model files found in the 'models/' directory.\n\n";
            printModelMenu(all);
            std::cerr << "\n  Quick download:\n";
            std::cerr << "    Linux/macOS: ./scripts/download_model.sh base.en\n";
            std::cerr << "    Windows:     .\\scripts\\download_model.ps1 -Model base.en\n";
            std::cerr << "    Manual:      https://huggingface.co/ggerganov/whisper.cpp/tree/main\n";
            return 1;
        }

        // Local models exist — show picker
        if (localCount == 1) {
            // Only one local model — use it directly
            for (const auto& m : all) {
                if (m.downloaded) {
                    opt.model_path = m.path;
                    std::cout << "  Using model: " << m.name << "\n";
                    break;
                }
            }
        } else {
            // Multiple local models — interactive picker
            std::string picked = interactiveModelPicker(all);
            if (picked.empty()) {
                std::cerr << "  No model selected. Exiting.\n";
                return 1;
            }
            opt.model_path = picked;
        }

        if (!fs::exists(opt.model_path)) {
            std::cerr << "[ERROR] Specified model still not found: " << opt.model_path << "\n";
            return 1;
        }
    }

    try {
        SubtitleConfig cfg;
        cfg.model_path             = opt.model_path;
        cfg.language               = opt.language;
        cfg.translate              = opt.translate;
        cfg.n_threads              = opt.n_threads;
        cfg.chunk_duration_seconds = opt.chunk_secs;
        cfg.chunk_overlap_seconds  = 1.0;
        cfg.min_confidence         = opt.min_conf;
        cfg.remove_filler_words    = opt.no_filler;
        cfg.print_special          = opt.verbose;
        cfg.force_cpu              = opt.force_cpu;
        cfg.merge_short_segments   = true;
        cfg.max_segment_duration   = 5.0;

        SubtitleGenerator gen(cfg);

        gen.setProgressCallback([](double cur, double total) {
            drawProgress(cur, total);
        });

        gen.setInterruptFlag(&g_interrupted);

        // Always capture subtitles for the live progress display.
        // The --stream flag additionally prints them with timestamps.
        const bool doStream = opt.stream;
        gen.setChunkCallback([doStream](const std::vector<Subtitle>& subs) {
            for (const auto& s : subs) {
                g_lastSubtitleText = s.text;
                if (doStream) {
                    std::cout << "\n  [" << TimestampFormatter::formatSRT(s.start_time)
                              << "] " << s.text << "\n";
                }
            }
        });

        std::string lang_desc = opt.language;
        if (opt.translate) lang_desc += " -> en (translate)";

        std::cout << "Input    : " << opt.audio_path  << "\n"
                  << "Model    : " << opt.model_path  << "\n"
                  << "Language : " << lang_desc        << "\n"
                  << "Threads  : " << opt.n_threads    << "\n"
                  << "Chunk    : " << opt.chunk_secs   << "s\n"
                  << "Format   : " << (opt.all_formats ? "all" : opt.format) << "\n\n";

        gen.generate(opt.audio_path);

        if (g_interrupted) {
            std::cout << "\n[INFO] Interrupted by user (Ctrl+C). "
                         "Saving the subtitles generated so far...\n";
        }

        auto saveAs = [&](const std::string& fmt) {
            fs::path base = opt.output_path;
            std::string fpath = base.replace_extension("." + fmt).string();
            if      (fmt == "srt")  gen.saveSRT (fpath);
            else if (fmt == "vtt")  gen.saveVTT (fpath);
            else if (fmt == "json") gen.saveJSON(fpath);
            else if (fmt == "ass")  gen.saveASS (fpath);
            else if (fmt == "txt")  gen.saveTXT (fpath);
        };

        if (opt.all_formats) {
            for (const char* f : {"srt", "vtt", "json", "ass", "txt"}) saveAs(f);
        } else {
            saveAs(opt.format);
        }

        auto stats = gen.getStats();
        std::cout <<
"\n+-- Summary -----------------------------------------------------+\n"
"| Segments    : " << stats.total_segments << "\n"
"| Duration    : " << TimestampFormatter::formatDuration(stats.total_duration) << "\n"
"| Processed   : " << std::fixed << std::setprecision(1) << stats.processing_time << "s\n"
"| Speed       : " << std::setprecision(1) << stats.realtime_factor << "x real-time\n"
"| Compute     : " << stats.compute_device << "\n"
"| Language    : " << stats.detected_language << "\n"
"| Avg conf.   : " << std::setprecision(2) << stats.average_confidence << "\n"
"| Avg seg.    : " << std::setprecision(2) << stats.average_segment_length << "s\n"
"| Interrupted : " << (stats.interrupted ? "yes" : "no") << "\n"
"+------------------------------------------------------------------+\n"
"\n" << (stats.interrupted ? "Done (partial -- interrupted by user)." : "Done!") << "\n";

    } catch (const std::exception& e) {
        std::cerr << "\n[ERROR] " << e.what() << "\n";
        return 1;
    }
    return 0;
}
