# 🎬 SubtitleGenerator

A high-performance C++ subtitle generator built on [whisper.cpp](https://github.com/ggml-org/whisper.cpp), with automatic GPU acceleration, CPU fallback, multi-format input via FFmpeg, and built-in translation to English.

[![Build](https://github.com/SyntX34/SubtitleTool/actions/workflows/build.yml/badge.svg)](https://github.com/SyntX34/SubtitleTool/actions/workflows/build.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Platforms](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-informational)](#requirements)
[![Releases](https://img.shields.io/github/v/release/SyntX34/SubtitleTool?include_prereleases)](https://github.com/SyntX34/SubtitleTool/releases)
[![Stars](https://img.shields.io/github/stars/SyntX34/SubtitleTool?style=social)](https://github.com/SyntX34/SubtitleTool/stargazers)

---

## ✨ Features

- 🚀 **GPU-accelerated with CPU fallback** — uses Vulkan/CUDA on Windows/Linux or Metal on macOS automatically when available, with CPU fallback if no compatible GPU is found.
- 🎯 **Live subtitle preview** — see transcribed text in real-time alongside the progress bar as audio is processed.
- 🌍 **100-language transcription + translation** — transcribe in the spoken language, or translate **into** English.
- 📝 **Multiple output formats** — SRT, WebVTT, ASS, JSON, and plain TXT.
- 🎞️ **Multi-format input** — WAV (built-in), plus MP3/MP4/MKV/FLAC/OGG/AAC/MOV/AVI/WEBM and more via FFmpeg.
- ⚙️ **Configurable** — adjustable chunking, segment merging/splitting, confidence filtering, filler-word removal.
- ⏹️ **Graceful Ctrl+C** — stop at any time; subtitles produced so far are still saved.
- 📊 **Performance stats** — real-time factor, average confidence, average segment length.

---

## 📦 Download & Run

Grab the latest binary from the [Releases page](https://github.com/SyntX34/SubtitleTool/releases). Each zip contains:

| Platform | File | GPU backend |
|---|---|---|
| Windows | `subtitle_generator-windows-x64.zip` | Vulkan |
| Linux | `subtitle_generator-linux-x64.zip` | Vulkan |
| macOS | `subtitle_generator-macos-arm64.zip` | Metal |

### Quick start

```bash
# 1. Download a model
./scripts/download_model.sh base.en

# 2. Run it
./subtitle_generator video.mp4 --cpu
```

> If you hit GPU issues (crashes or hangs during model loading), pass `--cpu` to run entirely on CPU. See [Troubleshooting](#-troubleshooting) below.

---

## 🚀 Building from Source

### 1. Clone with submodules

```bash
git clone --recursive https://github.com/SyntX34/SubtitleTool.git
cd SubtitleTool
```

Already cloned without `--recursive`?
```bash
git submodule update --init --recursive
```

### 2. Download a model

```bash
# Linux / macOS
./scripts/download_model.sh base.en

# Windows (PowerShell)
.\scripts\download_model.ps1 -Model base.en
```

Run `--list-models` to see all available models:

| Model | Size | Description |
|---|---|---|
| `tiny.en` | ~75 MB | Fastest, English-only |
| `tiny` | ~75 MB | Fastest, multilingual |
| `base.en` | ~145 MB | Default, English-only |
| `base` | ~145 MB | Default, multilingual |
| `small.en` | ~465 MB | Good accuracy, English |
| `small` | ~465 MB | Good accuracy, multilingual |
| `medium.en` | ~1.5 GB | High accuracy, English |
| `medium` | ~1.5 GB | High accuracy, multilingual |
| `large-v3` | ~3.0 GB | Best accuracy, multilingual |
| `large-v3-turbo` | ~1.5 GB | Fast large model, multilingual |

### 3. Build

```bash
cmake -B build -S .
cmake --build build --config Release
```

The build result is at `build/subtitle_generator` (Linux/macOS) or `build\Release\subtitle_generator.exe` (Windows).

#### Build options

| Option | Default | Description |
|---|---|---|
| `-DUSE_GPU=ON` | ON | Enable GPU acceleration |
| `-DFORCE_CPU=ON` | OFF | Force CPU-only build |
| `-DUSE_FFMPEG=ON` | ON | Enable FFmpeg for multi-format input |

### 4. Run

```bash
# On Linux/macOS
./build/subtitle_generator audio.mp4 -o subtitles.srt

# On Windows
.\build\Release\subtitle_generator.exe audio.mp4 -o subtitles.srt
```

---

## 🛠️ Usage

```text
subtitle_generator <audio_or_video_file> [OPTIONS]

MODEL
  -m, --model <name>      Model name or path (default: base.en)
                          Examples: base.en, small, medium, ...
                          Or use full path: models/ggml-base.en.bin
      --list-models       List all available models with sizes

OUTPUT
  -o, --output <path>     Output file (default: <input>.<format>)
  -f, --format <fmt>      srt | vtt | ass | json | txt  (default: srt)
      --all-formats       Save all formats at once

LANGUAGE & TRANSLATION
  -l, --lang <code>       Source language spoken in the audio (default: auto)
      --translate         Translate the transcription into English
      --list-languages    Print every supported language code and exit

PROCESSING
  -t, --threads <n>       Worker threads (default: 4)
      --chunk <secs>      Audio chunk size in seconds (default: 30)
      --min-conf <0-1>    Drop segments below this confidence
      --no-filler         Remove filler words (um, uh, er, ...)

MISC
  -s, --stream            Print subtitles live with timestamps
  -v, --verbose           Verbose whisper output
      --cpu, --force-cpu  Use CPU only (disable GPU)
      --ffmpeg-path <dir> Path to FFmpeg binaries (e.g. D:\FFMPEG\bin)
  -h, --help              Show this message
```

### Examples

```bash
# Basic usage — English-only, default model
subtitle_generator movie.mp4

# Spanish to English subtitles
subtitle_generator pelicula.mp4 -l es --translate

# High accuracy, live stream view, CPU-only
subtitle_generator lecture.mp4 -m medium -s --cpu

# All formats at once, 60-second chunks
subtitle_generator podcast.mkv --all-formats --chunk 60

# Specify FFmpeg path if auto-detection fails
subtitle_generator video.mp4 --ffmpeg-path D:\FFMPEG\bin
```

---

## 🌍 Language & Translation

| Flag | Meaning |
|---|---|
| `-l <code>` | Language *spoken in the audio*. Use `auto` for automatic detection, or set it explicitly for faster/more accurate results. |
| `--translate` | Translate the transcription **into** English. |

```bash
# Spanish video → English subtitles
subtitle_generator pelicula.mp4 -l es --translate -o english.srt

# Japanese video → Japanese subtitles
subtitle_generator anime.mkv -l ja -o subtitles_ja.srt

# Unknown language → same-language transcript
subtitle_generator interview.wav -l auto -o transcript.srt
```

> whisper.cpp only translates **into** English. To translate Spanish→French, generate English subtitles first then use a text translation tool.

---

## ❗ Troubleshooting

### GPU crashes or hangs on startup

If the program crashes right after detecting your GPU (you see `ggml_vulkan:` or `ggml_cuda:` lines and then nothing), use `--cpu`:

```bash
subtitle_generator video.mp4 --cpu
```

This bypasses the GPU backend entirely and runs on CPU. The GPU-aware build is still used — GPU is just disabled at runtime. Future updates may fix the underlying GPU compatibility issue.

### FFmpeg not found

The tool automatically searches common FFmpeg install locations:
- **Windows**: `D:\FFMPEG\bin`, `C:\FFmpeg\bin`, `C:\Program Files\FFmpeg\bin`, PATH
- **Linux**: `/usr/lib`, `/usr/local/lib`, ldconfig paths
- **macOS**: `/opt/homebrew/lib`, `/usr/local/lib`

If your FFmpeg is in a non-standard location:

```bash
subtitle_generator video.mp4 --ffmpeg-path D:\my\ffmpeg\bin
```

When built with MSVC, FFmpeg DLLs are loaded lazily (`/DELAYLOAD`) so the binary can start even without FFmpeg present. Only WAV files work without FFmpeg.

### Model not found

```bash
# Auto-resolve model name (no path needed):
subtitle_generator audio.mp4 -m base.en

# Or specify a full path:
subtitle_generator audio.mp4 -m models/ggml-base.en.bin

# List downloadable models:
subtitle_generator --list-models

# Download the default model:
./scripts/download_model.sh base.en
```

If the `models/` directory has .bin files but the one you requested isn't there, the tool will list what it found and suggest the correct download command.

---

## 📂 Project Layout

```
SubtitleTool/
├── src/
│   ├── main.cpp                  # CLI entry point
│   ├── SubtitleGenerator.{hpp,cpp}
│   ├── AudioDecoder.{hpp,cpp}     # WAV + FFmpeg decoding
│   ├── FFmpegHelper.{hpp,cpp}     # Runtime FFmpeg detection
│   ├── TimestampFormatter.{hpp,cpp}
│   └── dl_windows.cpp             # Windows helpers
├── scripts/
│   ├── download_model.sh
│   └── download_model.ps1
├── third_party/
│   └── whisper.cpp/               # git submodule
├── .github/workflows/build.yml
└── CMakeLists.txt
```

---

## 📜 License

MIT — see [LICENSE](LICENSE).

## 👤 Author

**SyntX** — [github.com/SyntX34](https://github.com/SyntX34)
