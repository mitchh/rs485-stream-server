#!/bin/bash

# RS485 Stream Server Development Script
# Usage: ./dev.sh [command]
# Commands: compile, upload, logs, clean, validate, run, watch

set -e

DEVICE_CONFIG="test-device.yaml"
DEVICE_IP="192.168.30.22"  # Your production device for testing
DEVICE_NAME="easytouch-bridge-esp8266"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log() {
    echo -e "${BLUE}[$(date +'%H:%M:%S')]${NC} $1"
}

success() {
    echo -e "${GREEN}✅ $1${NC}"
}

error() {
    echo -e "${RED}❌ $1${NC}"
}

warn() {
    echo -e "${YELLOW}⚠️  $1${NC}"
}

case "${1:-help}" in
    "compile"|"c")
        log "Compiling $DEVICE_CONFIG..."
        if esphome compile $DEVICE_CONFIG; then
            success "Compilation successful!"
        else
            error "Compilation failed!"
            exit 1
        fi
        ;;

    "upload"|"u")
        log "Uploading to device..."
        if [ -n "$DEVICE_IP" ]; then
            log "Using OTA upload to $DEVICE_IP"
            esphome upload $DEVICE_CONFIG --device $DEVICE_IP
        else
            log "Using USB upload (make sure device is connected)"
            esphome upload $DEVICE_CONFIG
        fi
        ;;

    "logs"|"l")
        log "Showing device logs..."
        if [ -n "$DEVICE_IP" ]; then
            esphome logs $DEVICE_CONFIG --device $DEVICE_IP
        else
            esphome logs $DEVICE_CONFIG
        fi
        ;;

    "run"|"r")
        log "Compile and upload in one step..."
        if [ -n "$DEVICE_IP" ]; then
            esphome run $DEVICE_CONFIG --device $DEVICE_IP
        else
            esphome run $DEVICE_CONFIG
        fi
        ;;

    "clean")
        log "Cleaning build files..."
        esphome clean $DEVICE_CONFIG
        success "Build files cleaned"
        ;;

    "validate"|"v")
        log "Validating component..."
        if ./.venv/bin/python -c "import esphome.codegen as cg; import esphome.config_validation as cv; print('ESPHome imports successful')"; then
            success "ESPHome imports successful"
        else
            error "ESPHome import validation failed"
            exit 1
        fi

        log "Validating YAML config..."
        if esphome config $DEVICE_CONFIG > /dev/null; then
            success "YAML config validation successful"
        else
            error "YAML config validation failed"
            exit 1
        fi
        ;;

    "format"|"f")
        log "Formatting code..."

        if command -v clang-format &> /dev/null; then
            find components -name "*.cpp" -o -name "*.h" | xargs clang-format -i
            success "C++ code formatted"
        else
            warn "clang-format not found, skipping C++ formatting"
        fi

        if [ -f .venv/bin/black ]; then
            .venv/bin/black components/
            success "Python code formatted"
        elif command -v black &> /dev/null; then
            black components/
            success "Python code formatted"
        else
            warn "black not found, skipping Python formatting"
        fi
        ;;

    "watch"|"w")
        log "Watching for changes and auto-compiling..."
        log "Watching: components/ and $DEVICE_CONFIG"
        warn "Press Ctrl+C to stop"

        # Use fswatch if available (brew install fswatch)
        if command -v fswatch &> /dev/null; then
            fswatch -o components/ $DEVICE_CONFIG | while read; do
                log "Changes detected, recompiling..."
                if esphome compile $DEVICE_CONFIG; then
                    success "Auto-compilation successful!"
                else
                    error "Auto-compilation failed!"
                fi
                echo "---"
            done
        else
            warn "Install fswatch for file watching: brew install fswatch"
            echo "Falling back to manual compilation mode"
            echo "Run: ./dev.sh compile"
        fi
        ;;

    "quick"|"q")
        log "Quick development cycle: format → validate → compile"

        # Format code
        if command -v clang-format &> /dev/null; then
            find components -name "*.cpp" -o -name "*.h" | xargs clang-format -i
        fi

        # Validate
        if ! ./.venv/bin/python -c "import esphome.codegen as cg; import esphome.config_validation as cv" 2>/dev/null; then
            error "ESPHome import validation failed"
            exit 1
        fi

        # Compile
        if esphome compile $DEVICE_CONFIG; then
            success "Quick cycle completed successfully!"
        else
            error "Quick cycle failed at compilation"
            exit 1
        fi
        ;;

    "deploy"|"d")
        log "Full deploy: format → validate → compile → upload"

        # Quick validation and compile
        ./dev.sh quick

        # Upload
        log "Uploading to device..."
        if [ -n "$DEVICE_IP" ]; then
            esphome upload $DEVICE_CONFIG --device $DEVICE_IP
            success "Deployed via OTA to $DEVICE_IP"
        else
            esphome upload $DEVICE_CONFIG
            success "Deployed via USB"
        fi
        ;;

    "test"|"t")
        log "Running test connection to RS485 server..."
        if [ -n "$DEVICE_IP" ]; then
            log "Testing connection to $DEVICE_IP:6683..."
            if nc -z $DEVICE_IP 6683 2>/dev/null; then
                success "RS485 stream server is responding on port 6683"
            else
                error "Cannot connect to RS485 stream server on $DEVICE_IP:6683"
            fi
        else
            warn "Set DEVICE_IP in script to test connection"
        fi
        ;;

    "help"|*)
        echo -e "${BLUE}RS485 Stream Server Development Commands:${NC}"
        echo "  ${GREEN}compile, c${NC}    - Compile the firmware"
        echo "  ${GREEN}upload, u${NC}     - Upload firmware to device"
        echo "  ${GREEN}logs, l${NC}       - Show device logs"
        echo "  ${GREEN}run, r${NC}        - Compile and upload in one step"
        echo "  ${GREEN}clean${NC}         - Clean build files"
        echo "  ${GREEN}validate, v${NC}   - Validate component and config"
        echo "  ${GREEN}format, f${NC}     - Format C++ and Python code"
        echo "  ${GREEN}watch, w${NC}      - Watch files and auto-compile"
        echo "  ${GREEN}quick, q${NC}      - Quick dev cycle (format→validate→compile)"
        echo "  ${GREEN}deploy, d${NC}     - Full deploy (quick + upload)"
        echo "  ${GREEN}test, t${NC}       - Test connection to RS485 server"
        echo ""
        echo -e "${YELLOW}Configuration:${NC}"
        echo "  Set DEVICE_IP in this script for faster OTA uploads"
        echo "  Current device: $DEVICE_NAME"
        echo "  Config file: $DEVICE_CONFIG"
        ;;
esac
