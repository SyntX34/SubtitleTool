# 🎬 SubtitleGenerator

A high-performance C++ subtitle generator built on [whisper.cpp](https://github.com/ggml-org/whisper.cpp), with automatic GPU acceleration and CPU fallback, multi-format audio/video input via FFmpeg, and built-in translation to English.

[![Build](https://github.com/SyntX34/subtitle-generator/actions/workflows/build.yml/badge.svg)](https://github.com/SyntX34/subtitle-generator/actions/workflows/build.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Platforms](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-informational)](#requirements)
[![Releases](https://img.shields.io/github/v/release/SyntX34/subtitle-generator?include_prereleases)](https://github.com/SyntX34/subtitle-generator/releases)

---

## ✨ Features

- 🚀 **GPU-accelerated, CPU-safe** — uses CUDA on Windows/Linux or Metal on macOS automatically when available, and falls back to a SIMD-optimized CPU path with no extra setup if it isn't.
- 🎯 **Accurate timing** — segment-level timestamps with confidence scores per segment.
- 🌍 **96-language transcription + translation** — transcribe in the spoken language, or translate directly into English (see [Language & Translation](#-language--translation)).
- 📝 **Multiple output formats** — SRT, WebVTT, ASS (Advanced SubStation Alpha), JSON, and plain TXT.
- 🎞️ **Multi-format input via FFmpeg** — WAV natively, plus MP3/MP4/MKV/FLAC/OGG/AAC/MOV/WEBM/and more when built with FFmpeg.
- ⚙️ **Configurable** — adjustable chunking, segment merging/splitting, confidence filtering, filler-word removal.
- 🖥️ **Live hardware + backend report** — prints CPU, core count, RAM, and the active compute backend (CUDA / Metal / CPU) on every run.
- ⏹️ **Graceful Ctrl+C handling** — stop at any time; subtitles produced so far are still saved.
- 📊 **Performance stats** — real-time factor, average confidence, average segment length.

---

## 📦 Requirements

| | Windows | Linux | macOS |
|---|---|---|---|
| Compiler | MSVC 2019+ | GCC 7+ / Clang 6+ | Clang (Xcode CLT) |
| CMake | 3.15+ | 3.15+ | 3.15+ |
| GPU backend | CUDA Toolkit (optional) | CUDA Toolkit (optional) | Metal (built into the OS) |
| FFmpeg (optional, for non-WAV input) | via [vcpkg](https://github.com/microsoft/vcpkg) | `apt install libavformat-dev ...` | `brew install ffmpeg` |

> GPU acceleration is **entirely automatic**. If a CUDA toolkit is present at *build* time, the binary is built with CUDA support; if a compatible NVIDIA GPU is present at *run* time, it's used. If either isn't there, the program runs on CPU without any flags or configuration from you.

---

## 🚀 Quick Start

### 1. Clone with submodules

```bash
git clone --recursive https://github.com/SyntX34/subtitle-generator.git
cd subtitle-generator
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

| Model | Size | Notes |
|---|---|---|
| `tiny.en` | ~75 MB | Fastest, least accurate |
| `base.en` | ~145 MB | Good default for English |
| `small.en` | ~465 MB | Better accuracy, still fast |
| `medium.en` | ~1.5 GB | High accuracy, slower |
| `large-v3` | ~3 GB | Best accuracy, multilingual, needs a GPU for real-time use |

Drop the `.en` suffix (e.g. `base`, `small`) for the multilingual variants if you need non-English transcription or translation.

### 3. Build

```bash
cmake -B build -S .
cmake --build build --config Release
```

CMake auto-detects your platform's GPU backend. To force a CPU-only build (e.g. for a smaller, dependency-free binary):

```bash
cmake -B build -S . -DFORCE_CPU=ON
```

### 4. Run

```bash
./build/subtitle_generator audio.wav -m models/ggml-base.en.bin -o subtitles.srt
```

---

## 🛠️ Usage

```text
subtitle_generator <audio_or_video_file> [OPTIONS]

MODEL
  -m, --model <path>      Whisper model (default: models/ggml-base.en.bin)

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
  -s, --stream            Print subtitles live as they're generated
  -v, --verbose           Verbose whisper output
  -h, --help              Show the full help message
```

---

## 🌍 Language & Translation

`subtitle_generator` separates **what language is spoken** from **what language you want the output in**:

| Flag | Meaning |
|---|---|
| `-l <code>` | The language *spoken in the audio*. Use `auto` to let whisper detect it, or set it explicitly if you already know it — this is faster and more accurate than auto-detection. |
| `--translate` | Translate the transcription *into English*. whisper.cpp can only translate **into** English — it can't translate directly between two non-English languages. |

**Example — Spanish video, English subtitles:**

```bash
subtitle_generator pelicula.mp4 -l es --translate -o subtitles_en.srt
```

**Example — Japanese video, Japanese subtitles (no translation):**

```bash
subtitle_generator anime.mkv -l ja -o subtitles_ja.srt
```

**Example — unknown language, transcribed in its original language:**

```bash
subtitle_generator interview.wav -l auto -o subtitles.srt
```

To translate from a language that isn't English into a *third* language (e.g. Spanish → French), run `subtitle_generator` once to get an English transcript or translation, then run that text through a text translation tool — whisper.cpp's translation path is English-only by design.

See every supported language code:

```bash
subtitle_generator --list-languages
```

---

## 📂 Project Layout

```
subtitle-generator/
├── src/
│   ├── main.cpp                 # CLI entry point, hardware/banner reporting
│   ├── SubtitleGenerator.{hpp,cpp}
│   ├── AudioDecoder.{hpp,cpp}    # WAV (built-in) + FFmpeg (optional)
│   ├── TimestampFormatter.{hpp,cpp}
│   └── dl_windows.cpp            # Windows dynamic-library helpers
├── scripts/
│   ├── download_model.sh
│   └── download_model.ps1
├── third_party/
│   └── whisper.cpp/              # git submodule
├── .github/workflows/build.yml   # Windows + Linux + macOS CI/release
└── CMakeLists.txt
```

---

## 🧩 Building Without FFmpeg

FFmpeg is optional. Without it, only `.wav` input is supported, but the build has zero external dependencies:

```bash
cmake -B build -S . -DUSE_FFMPEG=OFF
```

---

## 📈 Project Activity

<!--
  Static placeholder — replace with a live badge once the repository has
  history to report on, e.g.:
  https://api.star-history.com/svg?repos=SyntX34/subtitle-generator&type=Date
-->

![Repo activity placeholder](https://api.star-history.com/svg?repos=syntx34%2Fsubtitletool&type=Date)

Once the repository is live, this section will embed a real commit/star history chart from [star-history.com](https://star-history.com/).

---

## 🤝 Contributing

Issues and pull requests are welcome. Please run the existing build matrix locally (or check the GitHub Actions results on your PR) before requesting review.

---

## 📜 License

MIT — see [LICENSE](LICENSE).

---

## 👤 Author

**SyntX**

- GitHub: [github.com/SyntX34](https://github.com/SyntX34)
- Steam: [steamcommunity.com/id/SyntX34](https://steamcommunity.com/id/SyntX34)
- Discord: `nh_syntx`
- Discord server: [discord.novazombie.com](https://discord.novazombie.com)
- Instagram: [instagram.com/dfa.nh_syntx](https://instagram.com/dfa.nh_syntx)
