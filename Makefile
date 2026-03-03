# gpsfix - Simple GPS status monitoring tool for gpsd
# Makefile for native and cross-compilation

# Compiler (can be overridden for cross-compilation)
CXX ?= g++

# Target architecture detection
HOST_OS ?= $(shell uname | tr '[:upper:]' '[:lower:]')
ARCH ?= $(shell uname -m)

# Compiler flags
FLAGS := -std=gnu++23 -Wall -Wno-write-strings -Wno-unknown-pragmas -Wshadow
FLAGS += -fvisibility-inlines-hidden -Wno-psabi
FLAGS += -fexceptions  # Required for CLI11


# Libraries
LIBS := -lgps

# Directories
SRC_DIR := src
BUILD_DIR := build
BIN_DIR := bin

# Files
TARGET := $(BIN_DIR)/gpsfix
SOURCE := $(SRC_DIR)/gpsfix.cpp

# Default target
.PHONY: all
all: $(TARGET)

# Create directories
$(BUILD_DIR) $(BIN_DIR):
	mkdir -p $@

# Build target
$(TARGET): $(SOURCE) | $(BIN_DIR)
	$(CXX) -o $@ $(FLAGS) $(CXXFLAGS) $< $(LIBS)

# Clean build artifacts
.PHONY: clean
clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

# Install to system (default: /usr/local/bin)
PREFIX ?= /usr/local
.PHONY: install
install: $(TARGET)
	install -d $(PREFIX)/bin
	install -m 755 $(TARGET) $(PREFIX)/bin/gpsfix

# Uninstall
.PHONY: uninstall
uninstall:
	rm -f $(PREFIX)/bin/gpsfix

# Display build configuration
.PHONY: info
info:
	@echo "CXX:      $(CXX)"
	@echo "CXXFLAGS: $(CXXFLAGS)"
	@echo "LIBS:     $(LIBS)"
	@echo "ARCH:     $(ARCH)"
	@echo "HOST_OS:  $(HOST_OS)"
	@echo "TARGET:   $(TARGET)"

# Help
.PHONY: help
help:
	@echo "gpsfix Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all       - Build gpsfix (default)"
	@echo "  clean     - Remove build artifacts"
	@echo "  install   - Install to PREFIX (default: /usr/local)"
	@echo "  uninstall - Remove installed binary"
	@echo "  info      - Display build configuration"
	@echo "  help      - Show this help"
	@echo ""
	@echo "Variables:"
	@echo "  CXX       - C++ compiler (default: g++)"
	@echo "  PREFIX    - Installation prefix (default: /usr/local)"
	@echo "  CXXFLAGS  - Additional compiler flags"
	@echo ""
	@echo "Cross-compilation example:"
	@echo "  make CXX=aarch64-linux-gnu-g++"
