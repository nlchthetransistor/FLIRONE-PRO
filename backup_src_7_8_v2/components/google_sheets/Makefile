# =====================================================================
# Makefile for Google Sheets Logger Library
# =====================================================================

CXX := g++
CXXFLAGS := -fPIC -Wall -Wextra -std=c++17 -O2
CFLAGS := -fPIC -Wall -Wextra -O2

LIBDIR := lib
OBJDIR := build

# Source files
SOURCES := google_sheets_logger.cpp
OBJECTS := $(SOURCES:%.cpp=$(OBJDIR)/%.o)

# Library targets
STATIC_LIB := $(LIBDIR)/libgoogle_sheets.a
SHARED_LIB := $(LIBDIR)/libgoogle_sheets.so

# Compiler flags with curl
CURL_CFLAGS := $(shell curl-config --cflags)
CURL_LIBS := $(shell curl-config --libs)

CXXFLAGS += $(CURL_CFLAGS)

.PHONY: all clean static shared

all: static shared

static: $(STATIC_LIB)

shared: $(SHARED_LIB)

$(OBJDIR):
	@mkdir -p $(OBJDIR)

$(LIBDIR):
	@mkdir -p $(LIBDIR)

$(OBJDIR)/%.o: %.cpp | $(OBJDIR)
	@echo "Compiling $<..."
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(STATIC_LIB): $(OBJECTS) | $(LIBDIR)
	@echo "Creating static library: $@"
	@ar rcs $@ $(OBJECTS)
	@echo "✓ Static library created successfully"

$(SHARED_LIB): $(OBJECTS) | $(LIBDIR)
	@echo "Creating shared library: $@"
	@$(CXX) -shared -o $@ $(OBJECTS) $(CURL_LIBS)
	@echo "✓ Shared library created successfully"

clean:
	@echo "Cleaning build files..."
	@rm -rf $(OBJDIR) $(LIBDIR)
	@echo "✓ Build files cleaned"

.SILENT: clean
