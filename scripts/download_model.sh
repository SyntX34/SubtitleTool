#!/bin/bash
set -e
MODEL_DIR="models"
mkdir -p "$MODEL_DIR"
echo "📥 Downloading Whisper models..."
echo "Available models: tiny, tiny.en, base, base.en, small, small.en, medium, medium.en, large"
echo ""
read -p "Enter model name (default: base.en): " MODEL_NAME
MODEL_NAME=${MODEL_NAME:-base.en}
echo "📥 Downloading $MODEL_NAME model..."
cd "$MODEL_DIR"
wget -q --show-progress "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-$MODEL_NAME.bin"
echo "✅ Model downloaded: $MODEL_DIR/ggml-$MODEL_NAME.bin"