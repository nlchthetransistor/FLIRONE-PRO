CXX      := g++
CXXFLAGS := -O2 -Wall -Wextra -std=c++17 -pthread \
            -I/usr/include/libusb-1.0 \
            $(shell pkg-config --cflags opencv4)

LDLIBS   := $(shell pkg-config --libs opencv4) \
            -lusb-1.0 -ljpeg -lm

TARGET := flirone
SRC    := flirone.cpp dht12.cpp
OBJ    := $(SRC:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJ)
