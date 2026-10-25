BUILD_TYPE ?= Release
BUILD_DIR ?= build/$(BUILD_TYPE)
INSTALL_PREFIX ?= build/install
NPROC ?= $(shell nproc 2>/dev/null || echo 1)

.PHONY: all configure build test install clean tl-generate

all: build

configure:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DENABLE_TESTS=ON

build: configure
	cmake --build $(BUILD_DIR) --parallel $(NPROC)

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

install: build
	cmake --install $(BUILD_DIR) --prefix $(INSTALL_PREFIX)

clean:
	rm -rf build

# Regenerates include/cppgram/mtproto/schema/api.h and src/mtproto/schema/*.cpp from schema/*.tl (C++ generator).
tl-generate: configure
	cmake --build $(BUILD_DIR) --target tl-generate
