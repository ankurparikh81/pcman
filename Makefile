UNAME_S := $(shell uname -s 2>/dev/null || echo Windows_NT)

CXX ?= g++
CXXFLAGS = -std=c++20 -Wall -Wextra -O2 -Isrc

ifeq ($(UNAME_S),Darwin)
    # macOS (Apple Silicon / Intel)
    CXX = clang++
    CXXFLAGS += -I/opt/homebrew/include -I/usr/local/include
    RAYLIB_STATIC = $(wildcard /opt/homebrew/lib/libraylib.a /usr/local/lib/libraylib.a)
    ifneq ($(strip $(RAYLIB_STATIC)),)
        RAYLIB_LIB = $(firstword $(RAYLIB_STATIC))
    else
        RAYLIB_LIB = -L/opt/homebrew/lib -L/usr/local/lib -lraylib
    endif
    LDFLAGS = $(RAYLIB_LIB) -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo
    TARGET = pcman
else ifeq ($(UNAME_S),Linux)
    # Linux (Debian, Ubuntu, Fedora, Arch, etc.)
    LDFLAGS = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
    TARGET = pcman
else
    # Windows (MinGW / MSYS2)
    LDFLAGS = -lraylib -lopengl32 -lgdi32 -lwinmm
    TARGET = pcman.exe
endif

SRCS = src/main.cpp \
       src/cga_renderer.cpp \
       src/pc_speaker.cpp \
       src/maze.cpp \
       src/entities.cpp \
       src/game.cpp

OBJS = $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $(TARGET) $(LDFLAGS)

test: $(TARGET)
	$(CXX) $(CXXFLAGS) tests/test_pcman.cpp src/maze.cpp src/cga_renderer.cpp src/entities.cpp src/pc_speaker.cpp src/game.cpp -o tests/test_pcman $(LDFLAGS)
	./tests/test_pcman

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) tests/test_pcman pcman.exe

.PHONY: all clean test
