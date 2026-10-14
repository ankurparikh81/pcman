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

# WebAssembly / Emscripten configuration
EMSDK ?= $(HOME)/emsdk
EMCC ?= $(shell which emcc 2>/dev/null || echo $(EMSDK)/upstream/emscripten/emcc)
EMCXX ?= $(shell which em++ 2>/dev/null || echo $(EMSDK)/upstream/emscripten/em++)
RAYLIB_WASM ?= $(HOME)/raylib-wasm
WEB_OUT_DIR = build-web
WEB_TARGET = $(WEB_OUT_DIR)/index.html
WEB_FLAGS = -std=c++20 -Wall -Wextra -O2 -Isrc -I$(RAYLIB_WASM)/src -DPLATFORM_WEB \
            -s USE_GLFW=3 -s TOTAL_STACK=64MB \
            --shell-file web/shell.html

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $(TARGET) $(LDFLAGS)

web: $(WEB_TARGET)

$(WEB_TARGET): $(SRCS) web/shell.html
	@mkdir -p $(WEB_OUT_DIR)
	$(EMCXX) $(WEB_FLAGS) $(SRCS) $(RAYLIB_WASM)/src/libraylib.a -o $(WEB_TARGET)
	@echo "WebAssembly build complete: $(WEB_TARGET)"

test: $(TARGET)
	$(CXX) $(CXXFLAGS) tests/test_pcman.cpp src/maze.cpp src/cga_renderer.cpp src/entities.cpp src/pc_speaker.cpp src/game.cpp -o tests/test_pcman $(LDFLAGS)
	./tests/test_pcman

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) tests/test_pcman pcman.exe

clean-web:
	rm -rf $(WEB_OUT_DIR) build-cmake-web

compdb compile_commands.json:
	@python3 scripts/gen_compdb.py

.PHONY: all clean clean-web test web compdb compile_commands.json

