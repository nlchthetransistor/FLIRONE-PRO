# Makefile for Components Libraries
# Master Makefile to build DHT12 and Servo libraries

.PHONY: all dht12 servo google_sheets clean distclean install uninstall help \
        dht12_static dht12_shared servo_static servo_shared google_sheets_static google_sheets_shared \
        dht12_clean servo_clean google_sheets_clean

# Default target
all: dht12 servo google_sheets

# ==============================================================================
# DHT12 Component
# ==============================================================================

dht12: dht12_static dht12_shared
	@echo ""
	@echo "✓ DHT12 library built successfully"

dht12_static:
	@echo "Building DHT12 static library..."
	@$(MAKE) -C dht12 lib_static

dht12_shared:
	@echo "Building DHT12 shared library..."
	@$(MAKE) -C dht12 lib_shared

dht12_clean:
	@echo "Cleaning DHT12 build files..."
	@$(MAKE) -C dht12 clean

# ==============================================================================
# Servo Component
# ==============================================================================

servo: servo_static servo_shared
	@echo ""
	@echo "✓ Servo library built successfully"

servo_static:
	@echo "Building Servo static library..."
	@$(MAKE) -C servo lib_static

servo_shared:
	@echo "Building Servo shared library..."
	@$(MAKE) -C servo lib_shared

servo_clean:
	@echo "Cleaning Servo build files..."
	@$(MAKE) -C servo clean

# ==============================================================================
# Google Sheets Logger Component
# ==============================================================================

google_sheets: google_sheets_static google_sheets_shared
	@echo ""
	@echo "✓ Google Sheets library built successfully"

google_sheets_static:
	@echo "Building Google Sheets static library..."
	@$(MAKE) -C google_sheets static

google_sheets_shared:
	@echo "Building Google Sheets shared library..."
	@$(MAKE) -C google_sheets shared

google_sheets_clean:
	@echo "Cleaning Google Sheets build files..."
	@$(MAKE) -C google_sheets clean

# ==============================================================================
# Installation
# ==============================================================================

.PHONY: install_dht12 install_servo install_all uninstall_dht12 uninstall_servo

install_dht12:
	@echo "Installing DHT12 library..."
	@$(MAKE) -C dht12 install

install_servo:
	@echo "Installing Servo library..."
	@$(MAKE) -C servo install

install_all: install_dht12 install_servo
	@echo ""
	@echo "✓ All libraries installed successfully"

install: install_all

uninstall_dht12:
	@echo "Uninstalling DHT12 library..."
	@$(MAKE) -C dht12 uninstall

uninstall_servo:
	@echo "Uninstalling Servo library..."
	@$(MAKE) -C servo uninstall

uninstall_all: uninstall_dht12 uninstall_servo
	@echo ""
	@echo "✓ All libraries uninstalled successfully"

uninstall: uninstall_all

# ==============================================================================
# Cleaning
# ==============================================================================

clean: dht12_clean servo_clean google_sheets_clean
	@echo ""
	@echo "✓ Build files cleaned"

distclean:
	@echo "Removing all generated files..."
	@$(MAKE) -C dht12 distclean
	@$(MAKE) -C servo distclean
	@$(MAKE) -C google_sheets clean
	@echo ""
	@echo "✓ All generated files removed"

# ==============================================================================
# Information & Help
# ==============================================================================

.PHONY: info status

info:
	@echo "Components Build System Information"
	@echo "===================================="
	@echo ""
	@echo "Available Components:"
	@echo "  1. DHT12  - Temperature/Humidity sensor library (C++)"
	@echo "  2. Servo  - Servo motor control library (C)"
	@echo ""
	@echo "Build Directories:"
	@echo "  dht12/build/   - Object files for DHT12"
	@echo "  dht12/lib/     - DHT12 libraries"
	@echo "  servo/build/   - Object files for Servo"
	@echo "  servo/lib/     - Servo libraries"
	@echo ""
	@echo "Use 'make help' for detailed build targets"

status:
	@echo "Component Build Status"
	@echo "======================"
	@echo ""
	@echo "DHT12 Library Status:"
	@test -f dht12/lib/libdht12.a && echo "  ✓ Static library:  dht12/lib/libdht12.a" || echo "  ✗ Static library:  Not built"
	@test -f dht12/lib/libdht12.so && echo "  ✓ Shared library:  dht12/lib/libdht12.so" || echo "  ✗ Shared library:  Not built"
	@echo ""
	@echo "Servo Library Status:"
	@test -f servo/lib/libservo.a && echo "  ✓ Static library:  servo/lib/libservo.a" || echo "  ✗ Static library:  Not built"
	@test -f servo/lib/libservo.so && echo "  ✓ Shared library:  servo/lib/libservo.so" || echo "  ✗ Shared library:  Not built"

help:
	@echo "Components Master Makefile"
	@echo "=========================="
	@echo ""
	@echo "MAIN TARGETS:"
	@echo "  all                 - Build all components (default)"
	@echo "  clean               - Remove all build files"
	@echo "  distclean           - Remove all generated files"
	@echo ""
	@echo "DHT12 TARGETS:"
	@echo "  dht12               - Build DHT12 (static + shared)"
	@echo "  dht12_static        - Build DHT12 static library"
	@echo "  dht12_shared        - Build DHT12 shared library"
	@echo "  dht12_clean         - Clean DHT12 build files"
	@echo ""
	@echo "SERVO TARGETS:"
	@echo "  servo               - Build Servo (static + shared)"
	@echo "  servo_static        - Build Servo static library"
	@echo "  servo_shared        - Build Servo shared library"
	@echo "  servo_clean         - Clean Servo build files"
	@echo ""
	@echo "INSTALLATION TARGETS:"
	@echo "  install             - Install all libraries system-wide (requires sudo)"
	@echo "  install_all         - Same as 'install'"
	@echo "  install_dht12       - Install DHT12 library only"
	@echo "  install_servo       - Install Servo library only"
	@echo "  uninstall           - Uninstall all libraries (requires sudo)"
	@echo "  uninstall_all       - Same as 'uninstall'"
	@echo "  uninstall_dht12     - Uninstall DHT12 library only"
	@echo "  uninstall_servo     - Uninstall Servo library only"
	@echo ""
	@echo "INFORMATION TARGETS:"
	@echo "  info                - Show build system information"
	@echo "  status              - Show library build status"
	@echo "  help                - Show this help message"
	@echo ""
	@echo "EXAMPLES:"
	@echo "  make all            - Build all libraries"
	@echo "  make dht12          - Build only DHT12 library"
	@echo "  make servo_static   - Build only Servo static library"
	@echo "  make install        - Install all libraries (requires sudo)"
	@echo "  make clean          - Clean all build files"
	@echo "  make status         - Check what has been built"

# Set default target if none specified
.DEFAULT_GOAL := help
