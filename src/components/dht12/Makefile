# Makefile for DHT12 Temperature/Humidity Sensor Library
# Builds static and shared libraries for DHT12 I2C sensor

CXX = g++
CXXFLAGS = -Wall -Wextra -O2 -fPIC -std=c++17
AR = ar
RANLIB = ranlib

# Directories
SRCDIR = .
OBJDIR = build
LIBDIR = lib

# Files
SOURCES = dht12.cpp
HEADERS = dht12.hpp
OBJECTS = $(SOURCES:%.cpp=$(OBJDIR)/%.o)
STATIC_LIB = $(LIBDIR)/libdht12.a
SHARED_LIB = $(LIBDIR)/libdht12.so

# Default target
.PHONY: all lib_static lib_shared clean help

all: lib_static lib_shared

# Create directories if they don't exist
$(OBJDIR):
	@mkdir -p $(OBJDIR)

$(LIBDIR):
	@mkdir -p $(LIBDIR)

# Compile object files
$(OBJDIR)/%.o: $(SRCDIR)/%.cpp $(HEADERS) | $(OBJDIR)
	@echo "Compiling $<..."
	@$(CXX) $(CXXFLAGS) -c $< -o $@

# Build static library
lib_static: $(STATIC_LIB)

$(STATIC_LIB): $(OBJECTS) | $(LIBDIR)
	@echo "Creating static library: $@"
	@$(AR) rcs $@ $^
	@$(RANLIB) $@
	@echo "✓ Static library created successfully"

# Build shared library
lib_shared: $(SHARED_LIB)

$(SHARED_LIB): $(OBJECTS) | $(LIBDIR)
	@echo "Creating shared library: $@"
	@$(CXX) -shared -fPIC -o $@ $^
	@echo "✓ Shared library created successfully"

# Installation
.PHONY: install uninstall

install: $(STATIC_LIB) $(SHARED_LIB)
	@echo "Installing DHT12 library..."
	@mkdir -p /usr/local/include
	@mkdir -p /usr/local/lib
	@cp $(HEADERS) /usr/local/include/
	@cp $(STATIC_LIB) /usr/local/lib/
	@cp $(SHARED_LIB) /usr/local/lib/
	@ldconfig
	@echo "✓ Installation completed successfully"

uninstall:
	@echo "Uninstalling DHT12 library..."
	@rm -f /usr/local/include/dht12.hpp
	@rm -f /usr/local/lib/libdht12.a
	@rm -f /usr/local/lib/libdht12.so
	@ldconfig
	@echo "✓ Uninstallation completed"

# Clean
.PHONY: clean distclean

clean:
	@echo "Cleaning build files..."
	@rm -rf $(OBJDIR)
	@echo "✓ Build files cleaned"

distclean: clean
	@echo "Removing library files..."
	@rm -rf $(LIBDIR)
	@echo "✓ All generated files removed"

# Help
help:
	@echo "DHT12 Library - Make Targets"
	@echo "============================"
	@echo "all         - Build both static and shared libraries (default)"
	@echo "lib_static  - Build static library (libdht12.a)"
	@echo "lib_shared  - Build shared library (libdht12.so)"
	@echo "install     - Install library system-wide (requires sudo)"
	@echo "uninstall   - Uninstall library (requires sudo)"
	@echo "clean       - Remove build files"
	@echo "distclean   - Remove all generated files"
	@echo "help        - Show this help message"

# Display build info
.PHONY: info

info:
	@echo "DHT12 Library Build Information"
	@echo "==============================="
	@echo "Compiler:   $(CXX)"
	@echo "Flags:      $(CXXFLAGS)"
	@echo "Sources:    $(SOURCES)"
	@echo "Headers:    $(HEADERS)"
	@echo "Objects:    $(OBJECTS)"
	@echo "Static:     $(STATIC_LIB)"
	@echo "Shared:     $(SHARED_LIB)"
