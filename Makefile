BUILD_TYPE ?= Release
BUILD_DIR ?= build/$(BUILD_TYPE)
LIVE_BUILD_DIR ?= build/live
INSTALL_PREFIX ?= build/install
NPROC ?= $(shell nproc 2>/dev/null || echo 1)
# Ninja is faster than the Makefile generator for incremental builds. Used when it is installed.
GENERATOR ?= $(if $(shell command -v ninja 2>/dev/null),Ninja,Unix Makefiles)
# Extra cmake -D flags for `configure`, for example: make configure CMAKE_CONFIG_FLAGS='-DCPPGRAM_UNITY_BUILD=ON'
CMAKE_CONFIG_FLAGS ?=
# Test parallelism. Left at 1 because some suites bind local ports; raise it on a machine you have checked.
CTEST_PARALLEL_LEVEL ?= 1
CMAKE_CACHE := $(BUILD_DIR)/CMakeCache.txt

.PHONY: all configure reconfigure build test install clean tl-generate live live-offline-check

all: build

# Configure once per build directory. CMake regenerates its own build files when CMakeLists.txt changes, so this
# rule does not run cmake again on every build.
$(CMAKE_CACHE):
	cmake -G "$(GENERATOR)" -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DENABLE_TESTS=ON $(CMAKE_CONFIG_FLAGS)

configure: $(CMAKE_CACHE)

reconfigure:
	rm -f $(CMAKE_CACHE)
	$(MAKE) configure

build: configure
	cmake --build $(BUILD_DIR) --parallel $(NPROC)

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure --parallel $(CTEST_PARALLEL_LEVEL)

install: build
	cmake --install $(BUILD_DIR) --prefix $(INSTALL_PREFIX)

clean:
	rm -rf build

# Regenerates include/cppgram/mtproto/schema/api.h and src/mtproto/schema/*.cpp from schema/*.tl (C++ generator).
tl-generate: configure
	cmake --build $(BUILD_DIR) --target tl-generate

# Builds only the live binaries (cppgram_live_check and cppgram_live_tests) in their own directory, so the library
# and test suite are not rebuilt for a live run. Nothing is run here; see LIVE_CHECK.md.
live:
	cmake -G "$(GENERATOR)" -S . -B $(LIVE_BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DBUILD_LIVE_CHECK=ON -DBUILD_LIVE_TESTS=ON
	cmake --build $(LIVE_BUILD_DIR) --target cppgram_live_check cppgram_live_tests --parallel $(NPROC)

# Offline live-test checks: no network, no credentials. Every Telegram test must SKIP or report NOT_SUPPORTED.
live-offline-check: live
	bash tools/check_live_tests_offline.sh $(LIVE_BUILD_DIR)/cppgram_live_tests
