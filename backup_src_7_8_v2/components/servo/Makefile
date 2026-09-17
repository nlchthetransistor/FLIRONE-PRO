# Makefile for Servo Motor Control Library
# Builds static and shared libraries using pigpio for PWM control

CC = gcc
CFLAGS = -Wall -Wextra -O2 -fPIC -std=c99
AR = ar
RANLIB = ranlib

# Directories
SRCDIR = .
OBJDIR = build
LIBDIR = lib

# Files
SOURCES = servo.c
HEADERS = servo.h
OBJECTS = $(SOURCES:%.c=$(OBJDIR)/%.o)
STATIC_LIB = $(LIBDIR)/libservo.a
SHARED_LIB = $(LIBDIR)/libservo.so

# Library linking
LIBS = -lpigpiod_if2 -lrt -lpthread -lm

# Default target
.PHONY: all lib_static lib_shared clean help

all: lib_static lib_shared

# Create directories if they don't exist
$(OBJDIR):
	@mkdir -p $(OBJDIR)

$(LIBDIR):
	@mkdir -p $(LIBDIR)

# Compile object files
$(OBJDIR)/%.o: $(SRCDIR)/%.c $(HEADERS) | $(OBJDIR)
	@echo "Compiling $<..."
	@$(CC) $(CFLAGS) -c $< -o $@

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
	@$(CC) -shared -fPIC -o $@ $^ $(LIBS)
	@echo "✓ Shared library created successfully"

# Installation
.PHONY: install uninstall

install: $(STATIC_LIB) $(SHARED_LIB)
	@echo "Installing Servo library..."
	@mkdir -p /usr/local/include
	@mkdir -p /usr/local/lib
	@cp $(HEADERS) /usr/local/include/
	@cp $(STATIC_LIB) /usr/local/lib/
	@cp $(SHARED_LIB) /usr/local/lib/
	@ldconfig
	@echo "✓ Installation completed successfully"

uninstall:
	@echo "Uninstalling Servo library..."
	@rm -f /usr/local/include/servo.h
	@rm -f /usr/local/lib/libservo.a
	@rm -f /usr/local/lib/libservo.so
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
	@echo "Servo Library - Make Targets"
	@echo "============================"
	@echo "all         - Build both static and shared libraries (default)"
	@echo "lib_static  - Build static library (libservo.a)"
	@echo "lib_shared  - Build shared library (libservo.so)"
	@echo "install     - Install library system-wide (requires sudo)"
	@echo "uninstall   - Uninstall library (requires sudo)"
	@echo "clean       - Remove build files"
	@echo "distclean   - Remove all generated files"
	@echo "help        - Show this help message"
	@echo ""
	@echo "Requirements:"
	@echo "  - pigpio library: sudo apt-get install pigpio python3-pigpio"
	@echo "  - gcc compiler"

# Display build info
.PHONY: info

info:
	@echo "Servo Library Build Information"
	@echo "==============================="
	@echo "Compiler:   $(CC)"
	@echo "Flags:      $(CFLAGS)"
	@echo "Libraries:  $(LIBS)"
	@echo "Sources:    $(SOURCES)"
	@echo "Headers:    $(HEADERS)"
	@echo "Objects:    $(OBJECTS)"
	@echo "Static:     $(STATIC_LIB)"
	@echo "Shared:     $(SHARED_LIB)"
