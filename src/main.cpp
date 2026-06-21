#include "SubtitleGenerator.hpp"
#include "TimestampFormatter.hpp"
#include "AudioDecoder.hpp"

#include <chrono>
#include <csignal>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

namespace fs = std::filesystem;

static volatile sig_atomic_t g_interrupted = 0;
static void sigint_handler(int) { g_interrupted = 1; }

static void enable_utf8_console() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(h, &mode))
        SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
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
                         (LPBYTE)cpu_name, &size);
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
#else
    {
        std::ifstream f("/proc/cpuinfo");
        std::string line;
        while (std::getline(f, line)) {
            if (line.rfind("model name", 0) == 0) {
                auto pos = line.find(':');
                if (pos != std::string::npos)
                    std::cout << "  CPU     : " << line.substr(pos + 2) << "\n";
                break;
            }
        }
    }
    {
        std::ifstream f("/proc/meminfo");
        std::string line;
        while (std::getline(f, line)) {
            if (line.rfind("MemTotal", 0) == 0) {
                long kb = 0;
                sscanf(line.c_str(), "MemTotal: %ld kB", &kb);
                std::cout << "  RAM     : " << std::fixed << std::setprecision(1)
                          << kb / (1024.0 * 1024.0) << " GB\n";
                break;
            }
        }
    }
#endif
}

struct Options {
    std::string audio_path;
    std::string model_path    = "models/ggml-base.en.bin";
    std::string output_path;
    std::string format        = "srt";
    std::string language      = "auto";
    double      chunk_secs    = 30.0;
    int         n_threads     = 4;
    bool        translate     = false;
    bool        verbose       = false;
    bool        stream        = false;
    float       min_conf      = 0.0f;
    bool        no_filler     = false;
    bool        all_formats   = false;
};

static void printBanner() {
    std::cout <<
"╔══════════════════════════════════════════════════════════════════╗\n"
"║  SubtitleGenerator v2.0  ·  Powered by whisper.cpp              ║\n"
"║  Author : SyntX  |  github.com/SyntX34                          ║\n"
"║  Formats: WAV | MP3 | MP4 | MKV | FLAC | OGG | AAC             ║\n"
"║  Output : SRT | VTT | ASS | JSON | TXT                          ║\n"
"╚══════════════════════════════════════════════════════════════════╝\n\n";
}

static void printUsage(const char* prog) {
    std::cout <<
"USAGE\n"
"  " << prog << " <audio_or_video_file> [OPTIONS]\n\n"
"REQUIRED\n"
"  <file>                  Input file. Supported: "
        << AudioDecoder::supportedFormats() << "\n\n"
"MODEL\n"
"  -m, --model <path>      Whisper model (default: models/ggml-base.en.bin)\n"
"                          Models: ggml-tiny.en.bin  ggml-base.en.bin\n"
"                                  ggml-small.en.bin ggml-medium.en.bin\n\n"
"OUTPUT\n"
"  -o, --output <path>     Output file (default: <input>.<format>)\n"
"  -f, --format <fmt>      srt | vtt | ass | json | txt  (default: srt)\n"
"      --all-formats       Save all formats at once\n\n"
"LANGUAGE\n"
"  -l, --lang <code>       Language code: en, ja, zh, es, fr, de, auto\n"
"      --translate         Translate to English\n\n"
"PROCESSING\n"
"  -t, --threads <n>       Worker threads (default: 4)\n"
"      --chunk <secs>      Audio chunk size in seconds (default: 30)\n"
"      --min-conf <0-1>    Drop segments below this confidence\n"
"      --no-filler         Remove filler words (um, uh, er ...)\n\n"
"MISC\n"
"  -s, --stream            Print subtitles live as generated\n"
"  -v, --verbose           Verbose whisper output\n"
"  -h, --help              Show this message\n\n"
"EXAMPLES\n"
"  " << prog << " movie.mp4\n"
"  " << prog << " movie.mp4 -m models/ggml-medium.en.bin -f srt\n"
"  " << prog << " lecture.mp3 -l auto --translate -o english.srt\n"
"  " << prog << " film.mkv --all-formats --chunk 60\n";
}

static bool parse(int argc, char* argv[], Options& opt) {
    if (argc < 2) { printUsage(argv[0]); return false; }
    opt.audio_path = argv[1];
    if (opt.audio_path == "-h" || opt.audio_path == "--help") {
        printUsage(argv[0]); return false;
    }
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << a << "\n"; exit(1);
            }
            return argv[++i];
        };
        if      (a == "-h" || a == "--help")      { printUsage(argv[0]); return false; }
        else if (a == "-m" || a == "--model")     { opt.model_path  = next(); }
        else if (a == "-o" || a == "--output")    { opt.output_path = next(); }
        else if (a == "-f" || a == "--format")    { opt.format      = next(); }
        else if (a == "-l" || a == "--lang")      { opt.language    = next(); }
        else if (a == "-t" || a == "--threads")   { opt.n_threads   = std::stoi(next()); }
        else if (a == "--chunk")                  { opt.chunk_secs  = std::stod(next()); }
        else if (a == "--min-conf")               { opt.min_conf    = std::stof(next()); }
        else if (a == "--translate")              { opt.translate   = true; }
        else if (a == "--no-filler")              { opt.no_filler   = true; }
        else if (a == "--all-formats")            { opt.all_formats = true; }
        else if (a == "-s" || a == "--stream")    { opt.stream      = true; }
        else if (a == "-v" || a == "--verbose")   { opt.verbose     = true; }
        else { std::cerr << "Unknown option: " << a << "\n"; return false; }
    }

    static const char* valid[] = {"srt","vtt","ass","json","txt", nullptr};
    bool ok = false;
    for (int i = 0; valid[i]; ++i)
        if (opt.format == valid[i]) { ok = true; break; }
    if (!ok) { std::cerr << "Invalid format: " << opt.format << "\n"; return false; }

    if (opt.output_path.empty()) {
        fs::path p(opt.audio_path);
        opt.output_path = p.stem().string() + "." + opt.format;
    }
    return true;
}

static void drawProgress(double cur, double total) {
    if (total <= 0) return;
    int pct    = std::min(100, static_cast<int>(cur / total * 100));
    int filled = pct / 2;  // 50-char bar
    std::cout << "\r[";
    for (int i = 0; i < 50; ++i)
        std::cout << (i < filled ? '#' : '-');
    std::cout << "] " << std::setw(3) << pct << "%  "
              << TimestampFormatter::formatDuration(cur) << " / "
              << TimestampFormatter::formatDuration(total) << "   "
              << std::flush;
    if (pct >= 100) std::cout << "\n";
}

int main(int argc, char* argv[]) {
    enable_utf8_console();

    signal(SIGINT,  sigint_handler);
    signal(SIGTERM, sigint_handler);

    printBanner();

    Options opt;
    if (!parse(argc, argv, opt)) return 1;

    std::cout << "System info:\n";
    print_hardware_info();
    std::cout << "\n";

    if (!fs::exists(opt.audio_path)) {
        std::cerr << "[ERROR] Input file not found: " << opt.audio_path << "\n";
        return 1;
    }
    if (!fs::exists(opt.model_path)) {
        std::cerr << "[ERROR] Model not found: " << opt.model_path << "\n";
        std::cerr << "  Download: https://huggingface.co/ggerganov/whisper.cpp\n";
        return 1;
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
        cfg.merge_short_segments   = true;
        cfg.max_segment_duration   = 5.0;

        SubtitleGenerator gen(cfg);

        gen.setProgressCallback([](double cur, double total) {
            drawProgress(cur, total);
        });

        gen.setInterruptFlag(&g_interrupted);

        if (opt.stream) {
            gen.setChunkCallback([](const std::vector<Subtitle>& subs) {
                for (const auto& s : subs)
                    std::cout << "  [" << TimestampFormatter::formatSRT(s.start_time)
                              << "] " << s.text << "\n";
            });
        }

        std::cout << "Input   : " << opt.audio_path  << "\n"
                  << "Model   : " << opt.model_path  << "\n"
                  << "Language: " << opt.language     << "\n"
                  << "Threads : " << opt.n_threads    << "\n"
                  << "Chunk   : " << opt.chunk_secs   << "s\n"
                  << "Format  : " << (opt.all_formats ? "all" : opt.format) << "\n\n";

        gen.generate(opt.audio_path);

        if (g_interrupted) {
            std::cout << "\n[INFO] Interrupted by user. Saving partial results...\n";
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
            for (auto& f : {"srt","vtt","json","ass","txt"}) saveAs(f);
        } else {
            saveAs(opt.format);
        }

        auto stats = gen.getStats();
        std::cout <<
"\n+-- Summary --------------------------------------------------+\n"
"| Segments   : " << stats.total_segments << "\n"
"| Duration   : " << TimestampFormatter::formatDuration(stats.total_duration) << "\n"
"| Processed  : " << std::fixed << std::setprecision(1) << stats.processing_time << "s\n"
"| Speed      : " << std::setprecision(1) << stats.realtime_factor << "x real-time\n"
"| Language   : " << stats.detected_language << "\n"
"| Avg conf.  : " << std::setprecision(2) << stats.average_confidence << "\n"
"| Avg seg.   : " << std::setprecision(2) << stats.average_segment_length << "s\n"
"+-------------------------------------------------------------+\n"
"\nDone!\n";

    } catch (const std::exception& e) {
        std::cerr << "\n[ERROR] " << e.what() << "\n";
        return 1;
    }
    return 0;
}