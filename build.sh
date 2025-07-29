#!/bin/bash

set -e
INSTALL_DIR="$(pwd)/build"
BASE_DIR="$(pwd)"
OMT_INCLUDE="$BASE_DIR/OMT/include"
OMT_LIB="$BASE_DIR/OMT/lib"

ARCH=$(uname -m)  # Detect current machine arch: arm64 or x86_64

function build_ffmpeg() {
  local target_arch=$1
  local prefix_dir=$2

  echo "Building FFmpeg for architecture: $target_arch"

  CC="clang -arch $target_arch"
  ./configure --prefix="$prefix_dir" --disable-shared --enable-static --enable-libomt \
    --extra-cflags="-I${OMT_INCLUDE}" --extra-ldflags="-L${OMT_LIB}" --arch="$target_arch" --cc="$CC" --enable-cross-compile

  make -j$(sysctl -n hw.ncpu) install
}





function make_universal() {
  echo "Creating universal binaries..."

  mkdir -p "$INSTALL_DIR/universal/bin"

  # Example for ffmpeg and ffprobe binaries; extend as needed:
  lipo -create "$INSTALL_DIR/arm64/bin/ffmpeg" "$INSTALL_DIR/x86_64/bin/ffmpeg" -output "$INSTALL_DIR/universal/bin/ffmpeg"
  lipo -create "$INSTALL_DIR/arm64/bin/ffprobe" "$INSTALL_DIR/x86_64/bin/ffprobe" -output "$INSTALL_DIR/universal/bin/ffprobe"

  echo "Universal binaries created at $INSTALL_DIR/universal/bin"
}

function run_install_name_tool() {
  # Replace install_name_tool commands below with your actual usage
  echo "Running install_name_tool for $ARCH..."

  # Example command (adjust paths and libraries as needed):
  #install_name_tool -change @rpath/libomt.dylib @executable_path/../lib/libomt.dylib "$INSTALL_DIR/$ARCH/bin/ffmpeg"
  install_name_tool -add_rpath @executable_path/resources  "$INSTALL_DIR/$ARCH/bin/ffmpeg"

  echo "install_name_tool step completed."
}

# Main logic to handle input parameter
PARAM=$1

case "$PARAM" in
  "")
    echo "Building for current architecture only: $ARCH"
    build_ffmpeg "$ARCH" "$INSTALL_DIR/$ARCH"
        run_install_name_tool

    ;;

  "universal")
    echo "Building universal binary (arm64 + x86_64)..."

    # Build for arm64
    build_ffmpeg "arm64" "$INSTALL_DIR/arm64"

    # Build for x86_64
    build_ffmpeg "x86_64" "$INSTALL_DIR/x86_64"

    # Create universal binaries
    make_universal
        run_install_name_tool

    ;;

  "quick")
    echo "Quick rebuild for current arch and install_name_tool"
    build_ffmpeg "$ARCH" "$INSTALL_DIR/$ARCH"
    run_install_name_tool
    ;;

  *)
    echo "Unknown option: $PARAM"
    echo "Usage: $0 [universal|quick]"
    exit 1
    ;;
esac
