# 🎬 Subtitle Generator

A high-performance, C++ subtitle generator using Whisper.cpp for real-time speech-to-subtitle transcription.

## Features

- 🚀 **Blazing fast** - Optimized C++ with SIMD acceleration
- 🎯 **Accurate timing** - Word-level timestamps
- 📝 **Multiple formats** - SRT, WebVTT, JSON
- ⚙️ **Configurable** - Adjustable segment length, merging, and splitting
- 🔧 **Post-processing** - Smart merging and splitting of segments
- 📊 **Performance stats** - Real-time speed metrics

## Requirements

- CMake 3.15+
- C++17 compiler (GCC 7+, Clang 6+, MSVC 2019+)
- Whisper.cpp (automatically downloaded)

## Quick Start

### 1. Clone with submodules

```bash
git clone --recursive https://github.com/yourusername/subtitle-generator.git
cd subtitle-generator
```

### 2. Download the model
```
./scripts/download_model.sh
```

### 3. Run
```bash
./build/subtitle_generator audio.wav -m models/ggml-base.en.bin -o subtitles.srt
```

## Usage
```bash
./subtitle_generator <audio.wav> [options]

Options:
  -m, --model <path>      Whisper model path (default: models/ggml-base.en.bin)
  -o, --output <path>     Output file path (default: subtitles.srt)
  -f, --format <format>   Output format: srt, vtt, json (default: srt)
  -l, --lang <lang>       Language code (default: en)
  -v, --verbose           Show verbose output
  -h, --help              Show help message
```