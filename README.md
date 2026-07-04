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
# 1. Run it — if no models are found, it'll show the list and let you download
./subtitle_generator video.mp4

# After downloading, re-run to start transcription:
./subtitle_generator video.mp4
```

> Downloads show live progress from aria2c, curl, or wget. See [Model download](#-downloading-models) below.
> If you hit GPU issues, pass `--cpu`. See [Troubleshooting](#-troubleshooting).

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

**The easiest way:** just run the tool — if no models are found, it shows the full list and prompts you to type model names to download:

```
$ subtitle_generator video.mp4

[INFO] No model files found in the 'models/' directory.

Available models:
  #  Name                 Size      Description
  --------------------------------------------------
  1  tiny.en              75 MB     Fastest, English-only (download)
  2  tiny                 75 MB     Fastest, multilingual (download)
  3  base.en              145 MB    Default, English-only (download)
  ...

Enter model name(s) to download (space-separated), or just press Enter
> base.en small

========== Downloading base.en ==========
  from: https://huggingface.co/.../ggml-base.en.bin
  to  : models/ggml-base.en.bin
[#b6c452 145MiB/145MiB(100%) CN:1 DL:11MiB ETA:0s]   ← live progress!

  >> Done! base.en saved to models/

========== Downloading small ==========
  ...

=========================================================
  2 model(s) downloaded successfully!
  Re-run the tool to start transcription.
=========================================================
```

**Or use the built-in downloader directly:**

```bash
# Download a single model
./subtitle_generator --download-model base.en

# Download multiple models at once
./subtitle_generator --download-model base.en small medium

# See all available models
./subtitle_generator --list-models
```

**Or use the download scripts:**

```bash
# Linux / macOS
./scripts/download_model.sh base.en

# Windows (PowerShell)
.\scripts\download_model.ps1 -Model base.en
```

**Available models:**

| Model | Size | Description |
|---|---|---|
| `tiny.en` | ~75 MB | Fastest, English-only |
| `tiny` | ~75 MB | Fastest, multilingual |
| `base.en` | ~145 MB | **Default**, English-only |
| `base` | ~145 MB | Default, multilingual |
| `small.en` | ~465 MB | Good accuracy, English |
| `small` | ~465 MB | Good accuracy, multilingual |
| `medium.en` | ~1.5 GB | High accuracy, English |
| `medium` | ~1.5 GB | High accuracy, multilingual |
| `large-v3` | ~3.0 GB | Best accuracy, multilingual |
| `large-v3-turbo` | ~1.5 GB | Fast large model, multilingual |

Each model is downloaded into the `models/` folder as a `.bin` file. The tool picks up any `.bin` file you drop there automatically.

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
  -m, --model <name>      Model name, index, or path (default: base.en)
                          Examples: -m base.en, -m small, -m 1, -m 3
                          Use by index: -m 1 (see --list-models for numbers)
                          Or full path: -m models/ggml-base.en.bin
      --list-models       List all available models with sizes and indices
      --download-model    Download one or more models and exit
                          Usage: --download-model <name1> [name2 ...]
                          Example: --download-model base.en small medium

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
# Basic usage — auto-detects model, prompts if none found
subtitle_generator movie.mp4

# After downloading, re-run with the same command:
subtitle_generator movie.mp4 -l en --translate -o subtitles.srt

# Select model by name
subtitle_generator movie.mp4 -m medium

# Select model by index (see --list-models for the list)
subtitle_generator movie.mp4 -m 3

# Download models without transcribing
subtitle_generator --download-model base.en small medium

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

The tool automatically handles model selection:

- **No models at all?** Shows all downloadable models with sizes, prompts you to type model names to download, shows live download progress, then tells you to **re-run** to start transcription.
- **Single model?** Uses it automatically.
- **Multiple models?** Shows an interactive picker with numbered list.
- **Select by index:** `-m 1`, `-m 2`, etc. (run `--list-models` to see the numbered list)
- **Non-downloaded model?** Offers to download it, shows progress, then tells you to re-run.

```bash
# Auto-resolve model name (no path needed):
subtitle_generator audio.mp4 -m base.en

# Select by index:
subtitle_generator audio.mp4 -m 3

# Download missing models on the fly (without an audio file):
subtitle_generator --download-model small.en medium

# Or specify a full path:
subtitle_generator audio.mp4 -m models/ggml-base.en.bin

# List downloadable models with sizes and indices:
subtitle_generator --list-models
```

> ⚠️ **Important**: When no models are found and you download them, the tool exits with a "Re-run" message instead of auto-continuing. This lets you download multiple models and switch between them freely.

If the `models/` directory has .bin files but the one you requested isn't there, the tool will list what it found and prompt you to pick one.

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
