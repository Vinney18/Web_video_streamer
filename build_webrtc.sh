#!/bin/bash

# WebRTC Video Streamer Build Script
# This script automates the build process for the WebRTC implementation

set -e  # Exit on error

echo "========================================="
echo "WebRTC Video Streamer - Build Script"
echo "========================================="
echo ""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${GREEN}[✓]${NC} $1"
}

print_error() {
    echo -e "${RED}[✗]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[!]${NC} $1"
}

# Check if running in dev container
if [ ! -d "/vcpkg" ]; then
    print_error "vcpkg not found at /vcpkg"
    print_warning "Are you running inside the dev container?"
    exit 1
fi

# Step 1: Install libdatachannel
echo "Step 1: Checking libdatachannel installation..."
if [ ! -d "/vcpkg/installed/x64-linux/include/rtc" ]; then
    print_warning "libdatachannel not installed, installing now..."
    cd /vcpkg
    ./vcpkg install libdatachannel
    print_status "libdatachannel installed successfully"
else
    print_status "libdatachannel already installed"
fi

# Step 2: Navigate to project directory
echo ""
echo "Step 2: Navigating to project directory..."
PROJECT_DIR="/home/vineet/vineet/i2v_projects/web-video-streamer/streamer"
if [ ! -d "$PROJECT_DIR" ]; then
    print_error "Project directory not found: $PROJECT_DIR"
    exit 1
fi
cd "$PROJECT_DIR"
print_status "In directory: $(pwd)"

# Step 3: Create build directory
echo ""
echo "Step 3: Setting up build directory..."
mkdir -p build
cd build
print_status "Build directory ready"

# Step 4: Run CMake
echo ""
echo "Step 4: Configuring with CMake..."
cmake .. -DCMAKE_TOOLCHAIN_FILE=/vcpkg/scripts/buildsystems/vcpkg.cmake

if [ $? -eq 0 ]; then
    print_status "CMake configuration successful"
else
    print_error "CMake configuration failed"
    exit 1
fi

# Step 5: Build
echo ""
echo "Step 5: Building project..."
CPU_CORES=$(nproc)
print_warning "Building with $CPU_CORES cores..."
make -j$CPU_CORES

if [ $? -eq 0 ]; then
    print_status "Build successful!"
else
    print_error "Build failed"
    exit 1
fi

# Step 6: Verify binary
echo ""
echo "Step 6: Verifying binary..."
if [ -f "./streamer" ]; then
    print_status "Binary created: ./streamer"

    # Get file size
    FILE_SIZE=$(du -h ./streamer | cut -f1)
    echo "   Size: $FILE_SIZE"

    # Check if executable
    if [ -x "./streamer" ]; then
        print_status "Binary is executable"
    else
        print_warning "Binary is not executable, setting permissions..."
        chmod +x ./streamer
    fi
else
    print_error "Binary not found!"
    exit 1
fi

# Step 7: Summary
echo ""
echo "========================================="
echo "Build Summary"
echo "========================================="
print_status "All steps completed successfully!"
echo ""
echo "Build artifacts:"
echo "  Binary: $PROJECT_DIR/build/streamer"
echo "  Size: $FILE_SIZE"
echo ""
echo "Next steps:"
echo "  1. Run the server:"
echo "     cd $PROJECT_DIR/build"
echo "     ./streamer"
echo ""
echo "  2. Serve the client (in new terminal):"
echo "     cd /home/vineet/vineet/i2v_projects/web-video-streamer/client"
echo "     python3 -m http.server 8080"
echo ""
echo "  3. Open browser:"
echo "     http://localhost:8080/webrtc-player.html"
echo ""
echo "For detailed documentation, see:"
echo "  - QUICK_START.md"
echo "  - WEBRTC_MIGRATION.md"
echo "  - IMPLEMENTATION_SUMMARY.md"
echo ""
print_status "Happy streaming! 🎥"
echo "========================================="
