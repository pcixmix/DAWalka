# =============================================================================
#  DAWalka — top-level convenience Makefile
#
#  Usage:
#    make            — configure + build all targets + package DAWalka.app
#    make build      — same as above (explicit alias)
#    make configure  — cmake configure only (first-time JUCE fetch)
#    make compile    — cmake --build only (no reconfigure, no repackage)
#    make package    — (re)package DAWalka.app from build/ artefacts
#    make install    — run the installer (venv + models + deploy plugins)
#    make all        — build + package + install in one shot
#    make clean      — remove build/ directory
#    make distclean  — remove build/ AND DAWalka.app
#    make help       — show this message
#
#  Prerequisites: cmake, clang (Xcode CLT), Python 3.10+
# =============================================================================

BUILD_DIR  := build
BUILD_TYPE := Release
NPROC      := $(shell sysctl -n hw.logicalcpu 2>/dev/null || echo 4)

# Installer app lives at the project root after `make package`
APP_BUNDLE := DAWalka.app
# install.sh lives inside the packaged .app
INSTALL_SCRIPT := $(APP_BUNDLE)/Contents/Resources/Scripts/install.sh

.PHONY: all build configure compile package install clean distclean help

# ─── Default target ───────────────────────────────────────────────────────────
build: configure compile package

# ─── Full pipeline ────────────────────────────────────────────────────────────
all: build install

# ─── Step 1: CMake configure ─────────────────────────────────────────────────
# Runs only when CMakeCache.txt is absent (first time) or when CMakeLists.txt
# changes.  Re-running configure on an existing build dir is safe — CMake is
# idempotent — but skipping it keeps incremental rebuilds fast.
configure: $(BUILD_DIR)/CMakeCache.txt

$(BUILD_DIR)/CMakeCache.txt: CMakeLists.txt
	cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

# ─── Step 2: Compile ─────────────────────────────────────────────────────────
# Depends on configure so `make compile` alone still works on a fresh tree.
compile: $(BUILD_DIR)/CMakeCache.txt
	cmake --build $(BUILD_DIR) --parallel $(NPROC)

# ─── Step 3: Package ─────────────────────────────────────────────────────────
# Runs make_app.sh.  Passes --rebuild so repeated `make package` always
# produces a fresh bundle from the latest build/ artefacts.
package: $(BUILD_DIR)/Standalone/DAWalka.app
	./installer/make_app.sh --rebuild

$(BUILD_DIR)/Standalone/DAWalka.app: compile
	@# Satisfied by the compile step via CMake's post-build copy.
	@# This target exists only so `package` re-runs when the standalone changes.

# ─── Step 4: Install ─────────────────────────────────────────────────────────
# Runs the embedded installer script (Python venv + model download + plugin copy).
# Requires the .app to exist; run `make build` or `make package` first.
install:
	@if [ ! -f "$(INSTALL_SCRIPT)" ]; then \
		echo "ERROR: $(APP_BUNDLE) not found — run 'make build' first." >&2; \
		exit 1; \
	fi
	bash "$(INSTALL_SCRIPT)" --standalone

# ─── Housekeeping ─────────────────────────────────────────────────────────────
clean:
	rm -rf $(BUILD_DIR)

distclean: clean
	rm -rf $(APP_BUNDLE)

# ─── Help ─────────────────────────────────────────────────────────────────────
help:
	@echo ""
	@echo "  make              configure + compile + package DAWalka.app"
	@echo "  make all          build + install (full pipeline)"
	@echo "  make configure    cmake configure only"
	@echo "  make compile      cmake --build only"
	@echo "  make package      (re)package DAWalka.app from build/ artefacts"
	@echo "  make install      deploy plugins, venv, and models"
	@echo "  make clean        remove build/"
	@echo "  make distclean    remove build/ and DAWalka.app"
	@echo ""
	@echo "  Build type: $(BUILD_TYPE)  (override: make BUILD_TYPE=Debug)"
	@echo ""
