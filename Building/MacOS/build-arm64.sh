#!/bin/bash
# Simple ARM64-only build for Apple Silicon Macs

set -e  # Exit on any error

QT_ARM64="/opt/homebrew/opt/qt@5/bin"  # Homebrew Qt5 location
BUILD_DIR="build-arm64"

# Check if we're on Apple Silicon
if [[ $(uname -m) != "arm64" ]]; then
    echo "Warning: This script is intended for Apple Silicon Macs (arm64)"
    echo "Current architecture: $(uname -m)"
    echo "Use existing build scripts for Intel Macs"
    exit 1
fi

# Check if Qt is available
if [[ ! -x "$QT_ARM64/qmake" ]]; then
    echo "Error: Qt 5 not found at $QT_ARM64"
    echo "Please install Qt 5 with: brew install qt@5"
    exit 1
fi

echo "Building OSCAR for Apple Silicon (ARM64)..."
echo "Qt version: $($QT_ARM64/qmake --version | head -1)"
echo "Architecture: $(file $QT_ARM64/qmake | grep -o 'arm64')"

# Create build directory
mkdir -p ${BUILD_DIR}
cd ${BUILD_DIR}

# Configure with qmake
echo "Configuring build..."
${QT_ARM64}/qmake ../oscar/oscar.pro -spec macx-clang

# Build with all available cores
echo "Building with $(sysctl -n hw.logicalcpu) cores..."
make -j$(sysctl -n hw.logicalcpu)

# Verify the build
if [[ -f "OSCAR.app/Contents/MacOS/OSCAR" ]]; then
    echo ""
    echo "✅ ARM64 build complete!"
    echo "📁 Application: ${BUILD_DIR}/OSCAR.app"
    echo "🔧 Architecture: $(file OSCAR.app/Contents/MacOS/OSCAR | grep -o 'arm64')"
    echo ""
    echo "To run OSCAR:"
    echo "  ./build-arm64/OSCAR.app/Contents/MacOS/OSCAR"
    echo ""
    echo "To create DMG installer:"
    echo "  cd build-arm64 && ../Building/MacOS/create_dmg OSCAR OSCAR.app"
else
    echo "❌ Build failed - OSCAR binary not found"
    exit 1
fi