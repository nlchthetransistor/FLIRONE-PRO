# Flirone - Main Application with Components
# This Makefile builds the main flirone application along with component libraries

CXX = g++
CC = gcc
CXXFLAGS = -O2 -Wall -std=c++17 -I. -I./components/dht12 -I./components/servo -I./components/google_sheets -I/usr/include/libusb-1.0 -I/usr/include/opencv4 -pthread
LDFLAGS = -L./components/dht12/lib -L./components/servo/lib -L./components/google_sheets/lib -Wl,-rpath,./components/dht12/lib:./components/servo/lib:./components/google_sheets/lib

# OpenCV and other libraries
LIBS = -lopencv_stitching -lopencv_alphamat -lopencv_aruco -lopencv_barcode -lopencv_bgsegm -lopencv_bioinspired -lopencv_ccalib -lopencv_cvv -lopencv_dnn_objdetect -lopencv_dnn_superres -lopencv_dpm -lopencv_face -lopencv_freetype -lopencv_fuzzy -lopencv_hdf -lopencv_hfs -lopencv_img_hash -lopencv_intensity_transform -lopencv_line_descriptor -lopencv_mcc -lopencv_quality -lopencv_rapid -lopencv_reg -lopencv_rgbd -lopencv_saliency -lopencv_shape -lopencv_stereo -lopencv_structured_light -lopencv_phase_unwrapping -lopencv_superres -lopencv_optflow -lopencv_surface_matching -lopencv_tracking -lopencv_highgui -lopencv_datasets -lopencv_text -lopencv_plot -lopencv_ml -lopencv_videostab -lopencv_videoio -lopencv_viz -lopencv_wechat_qrcode -lopencv_ximgproc -lopencv_video -lopencv_xobjdetect -lopencv_objdetect -lopencv_calib3d -lopencv_imgcodecs -lopencv_features2d -lopencv_dnn -lopencv_flann -lopencv_xphoto -lopencv_photo -lopencv_imgproc -lopencv_core -lusb-1.0 -ljpeg -lm -pthread

# Component libraries
COMPONENT_LIBS = -ldht12 -lservo -lgoogle_sheets -lpigpiod_if2 -lrt -lpthread

TARGET = flirone
SRC = flirone.cpp
COMPONENTS_DIR = components

# ============================================================================
# Main Targets
# ============================================================================

.PHONY: all components flirone clean distclean help status \
        build_flirone build_components install uninstall

all: build_components flirone
	@echo ""
	@echo "✓ Build completed successfully"

components: 
	@echo "Building component libraries..."
	@$(MAKE) -C $(COMPONENTS_DIR) all

build_components: components

$(TARGET): build_components $(SRC)
	@echo "Building main application: $(TARGET)"
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $(TARGET) $(SRC) $(LIBS) $(COMPONENT_LIBS)
	@echo "✓ Flirone application built successfully"

flirone: $(TARGET)

build_flirone: flirone

# ============================================================================
# Component-specific Targets
# ============================================================================

.PHONY: dht12 servo dht12_clean servo_clean

dht12:
	@$(MAKE) -C $(COMPONENTS_DIR) dht12

servo:
	@$(MAKE) -C $(COMPONENTS_DIR) servo

google_sheets:
	@$(MAKE) -C $(COMPONENTS_DIR) google_sheets

dht12_clean:
	@$(MAKE) -C $(COMPONENTS_DIR) dht12_clean

servo_clean:
	@$(MAKE) -C $(COMPONENTS_DIR) servo_clean

google_sheets_clean:
	@$(MAKE) -C $(COMPONENTS_DIR) google_sheets_clean

# ============================================================================
# Clean Targets
# ============================================================================

clean: dht12_clean servo_clean google_sheets_clean
	@echo "Cleaning build files..."
	@rm -f $(TARGET)
	@echo "✓ Clean completed"

distclean:
	@echo "Removing all generated files..."
	@$(MAKE) -C $(COMPONENTS_DIR) distclean
	@rm -f $(TARGET)
	@echo "✓ All generated files removed"

# ============================================================================
# Installation Targets
# ============================================================================

.PHONY: install install_components uninstall uninstall_components

install: all install_components
	@echo ""
	@echo "✓ Installation completed"

install_components:
	@echo "Installing component libraries..."
	@$(MAKE) -C $(COMPONENTS_DIR) install

uninstall: uninstall_components
	@echo ""
	@echo "✓ Uninstallation completed"

uninstall_components:
	@echo "Uninstalling component libraries..."
	@$(MAKE) -C $(COMPONENTS_DIR) uninstall

# ============================================================================
# Information & Help
# ============================================================================

.PHONY: info status help

info:
	@echo "Flirone Build System Information"
	@echo "================================"
	@echo ""
	@echo "Main Application:"
	@echo "  Target:     $(TARGET)"
	@echo "  Source:     $(SRC)"
	@echo "  Compiler:   $(CXX)"
	@echo ""
	@echo "Component Libraries:"
	@echo "  DHT12:      Temperature/Humidity sensor (C++)"
	@echo "  Servo:      Motor control with PWM (C)"
	@echo ""
	@echo "Build Directories:"
	@echo "  components/dht12/lib/    - DHT12 libraries"
	@echo "  components/servo/lib/    - Servo libraries"
	@echo ""
	@echo "Use 'make help' for detailed targets"

status:
	@echo "Build Status"
	@echo "============"
	@echo ""
	@echo "Main Application:"
	@test -f $(TARGET) && echo "  ✓ $(TARGET) built" || echo "  ✗ $(TARGET) not built"
	@echo ""
	@echo "Component Status:"
	@$(MAKE) -C $(COMPONENTS_DIR) status

help:
	@echo "Flirone Master Makefile"
	@echo "======================"
	@echo ""
	@echo "MAIN TARGETS:"
	@echo "  all                 - Build everything (default)"
	@echo "  flirone             - Build main application"
	@echo "  components          - Build all component libraries"
	@echo "  clean               - Remove build files"
	@echo "  distclean           - Remove all generated files"
	@echo ""
	@echo "COMPONENT TARGETS:"
	@echo "  dht12               - Build DHT12 library"
	@echo "  servo               - Build Servo library"
	@echo "  dht12_clean         - Clean DHT12 build files"
	@echo "  servo_clean         - Clean Servo build files"
	@echo ""
	@echo "INSTALLATION TARGETS (require sudo):"
	@echo "  install             - Build and install everything"
	@echo "  install_components  - Install component libraries"
	@echo "  uninstall           - Uninstall components"
	@echo "  uninstall_components - Uninstall component libraries"
	@echo ""
	@echo "INFORMATION TARGETS:"
	@echo "  info                - Show build system information"
	@echo "  status              - Show current build status"
	@echo "  help                - Show this help message"
	@echo ""
	@echo "EXAMPLES:"
	@echo "  make all            - Build everything"
	@echo "  make flirone        - Build just the application"
	@echo "  make dht12          - Build just DHT12 library"
	@echo "  make clean          - Clean all build files"
	@echo "  make status         - Check build status"
