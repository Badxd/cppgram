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

.PHONY: all configure reconfigure build test install clean tl-generate live live-offline-check \
	checks source-check version-check format-check format check-examples api-check schema-check configure-asan configure-tsan \
	package verify-release

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

# ---- Release and quality checks. None of these contacts Telegram. ----

# Every fast local check that does not need a full build: source lists, version, formatting, examples syntax.
checks: source-check version-check format-check api-check check-examples

# CMake source lists name every file, and every test file is built by a target.
source-check:
	python3 tools/check_cmake_sources.py

# VERSION, client.h app_version and the changelog heading agree.
version-check:
	bash tools/check_version.sh

# docs/API_INDEX.md matches client.h, and every indexed member is defined.
api-check:
	python3 tools/api_index.py --check

# Checks only the files in tools/format_allowlist.txt, with clang-format-18. The rest of the tree is not reformatted.
format-check:
	bash tools/format_check.sh

# Rewrites the allowlisted files with clang-format-18. Only run this on purpose; it changes files.
format:
	grep -v '^#' tools/format_allowlist.txt | grep -v '^$' | xargs clang-format-18 -i

# Syntax-checks every example against the public headers. Does not link.
check-examples:
	bash tools/check_examples.sh

# Regenerates the TL code, then checks that the checked-in files, the layer and schema/SHA256SUMS.txt match.
schema-check: configure
	cmake --build $(BUILD_DIR) --target tl-check

# Sanitizer build trees. Same flags as the CI sanitizer jobs. Configure only; build them with cmake --build.
configure-asan:
	cmake -G "$(GENERATOR)" -S . -B build/asan -DCMAKE_BUILD_TYPE=None -DENABLE_TESTS=ON \
		"-DCMAKE_CXX_FLAGS=-O0 -g0 -fsanitize=address,undefined -fno-omit-frame-pointer" \
		"-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined" \
		"-DCMAKE_SHARED_LINKER_FLAGS=-fsanitize=address,undefined"

configure-tsan:
	cmake -G "$(GENERATOR)" -S . -B build/tsan -DCMAKE_BUILD_TYPE=None -DENABLE_TESTS=ON \
		"-DCMAKE_CXX_FLAGS=-O1 -g0 -fsanitize=thread -fno-omit-frame-pointer" \
		"-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread" \
		"-DCMAKE_SHARED_LINKER_FLAGS=-fsanitize=thread"

# Source archive for release. Output name and contents are set in tools/package_release.py.
RELEASE_ZIP ?= dist/cppgram-$(shell tr -d '[:space:]' < VERSION)-source.zip
package:
	python3 tools/package_release.py --output $(RELEASE_ZIP)

# Verifies a release archive: zip integrity, and every SHA256SUMS.txt entry inside it.
verify-release:
	bash tools/verify_release.sh $(RELEASE_ZIP)
