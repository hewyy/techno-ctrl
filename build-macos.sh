#!/usr/bin/env bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIRECTORY="$PROJECT_ROOT/build-macos"
MAC_ARCHITECTURE="$(uname -m)"
LOGICAL_CPUS="$(sysctl -n hw.logicalcpu 2>/dev/null || echo 4)"
PLUGIN_NAME="Live Pattern Sequencer MVP.vst3"
PLUGIN_PATH="$BUILD_DIRECTORY/LivePatternSequencer_artefacts/Release/VST3/$PLUGIN_NAME"
INSTALL_DIRECTORY="$HOME/Library/Audio/Plug-Ins/VST3"
INSTALL_PATH="$INSTALL_DIRECTORY/$PLUGIN_NAME"

for required_command in cmake git c++ codesign; do
    if ! command -v "$required_command" >/dev/null 2>&1; then
        echo "Missing required command: $required_command"
        echo "See README-MACOS.md for installation instructions."
        exit 1
    fi
done

echo "Configuring for macOS architecture: $MAC_ARCHITECTURE"

cmake \
    -S "$PROJECT_ROOT" \
    -B "$BUILD_DIRECTORY" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="$MAC_ARCHITECTURE"

cmake \
    --build "$BUILD_DIRECTORY" \
    --config Release \
    --parallel "$LOGICAL_CPUS"

ctest \
    --test-dir "$BUILD_DIRECTORY" \
    -C Release \
    --output-on-failure

if [[ ! -d "$PLUGIN_PATH" ]]; then
    echo "Build completed, but the VST3 bundle was not found at:"
    echo "$PLUGIN_PATH"
    exit 1
fi

mkdir -p "$INSTALL_DIRECTORY"
/usr/bin/ditto "$PLUGIN_PATH" "$INSTALL_PATH"
/usr/bin/codesign --force --deep --sign - --timestamp=none "$INSTALL_PATH"

echo
echo "Build, tests, installation, and local signing completed."
echo "Installed plugin:"
echo "$INSTALL_PATH"
echo
echo "Open REAPER and run Options -> Preferences -> Plug-ins -> VST -> Re-scan."

