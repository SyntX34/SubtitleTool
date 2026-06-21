#!/bin/bash
set -e
echo "🔨 Building Subtitle Generator..."
mkdir -p build
cd build
echo "📋 Configuring CMake..."
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_GPU=OFF \
    -DBUILD_TESTS=ON
echo "🛠️  Building..."
make -j$(nproc)
echo "✅ Build complete!"
echo "📁 Executable: build/subtitle_generator"