# Flirone v2 - Web UI Edition
CXX = g++
CC = gcc
CXXFLAGS = -O2 -Wall -std=c++17 \
    -I. -I./components/dht12 -I./components/servo -I./components/google_sheets \
    -I/usr/include/libusb-1.0 \
    -pthread

LDFLAGS = -L./components/dht12/lib -L./components/servo/lib -L./components/google_sheets/lib \
    -Wl,-rpath,./components/dht12/lib:./components/servo/lib:./components/google_sheets/lib

# Libraries (NO OpenCV!)
LIBS = -lusb-1.0 -ljpeg -lwebsockets -lm -pthread

# Component libraries
COMPONENT_LIBS = -ldht12 -lservo -lgoogle_sheets -lpigpiod_if2 -lrt -lpthread -lcurl

TARGET = flirone
SRCS = main.cpp config.cpp thermal_proc.cpp usb_thermal.cpp web_server.cpp
OBJS = $(SRCS:.cpp=.o)

COMPONENTS_DIR = components

.PHONY: all components clean distclean help

all: components $(TARGET)
	@echo ""
	@echo "✓ Build completed. Run with: sudo ./flirone [config.json]"
	@echo "  Then open http://<rpi-ip>:8080 in browser"

components:
	@$(MAKE) -C $(COMPONENTS_DIR) all

%.o: %.cpp %.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Some files might not have a matching .h file, let's just make it simpler:
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $(TARGET) $(OBJS) $(LIBS) $(COMPONENT_LIBS)

clean:
	@$(MAKE) -C $(COMPONENTS_DIR) clean
	rm -f $(OBJS) $(TARGET)

distclean: clean
	@$(MAKE) -C $(COMPONENTS_DIR) distclean

help:
	@echo "FLIRONE v2 Build System"
	@echo "======================="
	@echo "  make all      - Build everything"
	@echo "  make clean    - Clean build files"
	@echo "  make distclean - Clean everything"
	@echo ""
	@echo "Dependencies: libusb-1.0-dev libjpeg-dev libwebsockets-dev nlohmann-json3-dev libcurl4-openssl-dev"
	@echo "Install:  sudo apt install libusb-1.0-0-dev libjpeg-dev libwebsockets-dev nlohmann-json3-dev libcurl4-openssl-dev"
