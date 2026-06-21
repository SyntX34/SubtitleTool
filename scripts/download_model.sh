#!/usr/bin/env bash
#
# download_model.sh — fetch a whisper.cpp GGML model.
#
# Usage:
#   ./scripts/download_model.sh [model_name]
#
# Examples:
#   ./scripts/download_model.sh            # downloads ggml-base.en.bin
#   ./scripts/download_model.sh small.en
#   ./scripts/download_model.sh medium
#
# Models are downloaded from the official whisper.cpp Hugging Face
# repository and placed in ./models/.

set -euo pipefail

MODEL="${1:-base.en}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODELS_DIR="${SCRIPT_DIR}/../models"
BASE_URL="https://huggingface.co/ggerganov/whisper.cpp/resolve/main"

mkdir -p "${MODELS_DIR}"

FILENAME="ggml-${MODEL}.bin"
DEST="${MODELS_DIR}/${FILENAME}"
URL="${BASE_URL}/${FILENAME}"

if [ -f "${DEST}" ]; then
    echo "Model already exists: ${DEST}"
    exit 0
fi

echo "Downloading ${FILENAME} ..."
echo "  from: ${URL}"
echo "  to  : ${DEST}"
echo

if command -v curl >/dev/null 2>&1; then
    curl -L --fail --progress-bar -o "${DEST}.part" "${URL}"
elif command -v wget >/dev/null 2>&1; then
    wget --show-progress -O "${DEST}.part" "${URL}"
else
    echo "Error: need curl or wget to download the model." >&2
    exit 1
fi

mv "${DEST}.part" "${DEST}"
echo
echo "Done. Saved to ${DEST}"
echo
echo "Available model names: tiny.en tiny base.en base small.en small \\"
echo "                        medium.en medium large-v3"
