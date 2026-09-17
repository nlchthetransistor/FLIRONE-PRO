CXX = g++
CXXFLAGS = -O2 -Wall -I. -I/usr/include/libusb-1.0 $(shell pkg-config --cflags opencv4)
LDFLAGS = $(shell pkg-config --libs opencv4) -lusb-1.0 -ljpeg -lm -pthread

TARGET = flirone
SRC = flirone.cpp dht12.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)
