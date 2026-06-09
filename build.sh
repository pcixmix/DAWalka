#!/usr/bin/env bash
# =============================================================================
#  DAWalka Installer — runs from inside DAWalka.app
# =============================================================================
#
#  This is the "Install" button handler.  It is intentionally NOT a wrapper
#  around build.sh — the .app contains pre-built DAWalka AU/VST3 bundles, so we
#  can skip the 5-15 min C++ compile and the cmake/git/brew dance that
#  build.sh needs.  All we do here is:
#
#     1. Make sure the system has Python 3.10+ and Xcode CLT (for auval)
#     2. Create the Python venv and install MLX + sentencepiece + aiohttp
#     3. Download the Stable Audio 3 model weights (skipped if cached)
#     4. Copy the bundled DAWalka.component and DAWalka.vst3 into user plug-in folders
#     5. Re-register AU with auval, verify the VST3 bundle, and report the result
#
#  The pre-built plug-ins and python_backend live next to this script:
#     $RES/component/DAWalka.component
#     $RES/vst3/DAWalka.vst3
#     $RES/python_backend/
#  where $RES is DAWalka.app/Contents/Resources/.
#
#  Designed to be re-runnable: every step is a no-op if the artefact
#  already exists.
# =============================================================================
set -uo pipefail

# ─── Self-locate so the script works regardless of CWD ──────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_RES_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"                    # .../Resources
APP_ROOT="$(cd "$APP_RES_DIR/.." && pwd)"                        # .../DAWalka.app
BUNDLED_COMPONENT="$APP_RES_DIR/component/DAWalka.component"
BUNDLED_VST3="$APP_RES_DIR/vst3/DAWalka.vst3"
BUNDLED_PYTHON_BACKEND="$APP_RES_DIR/python_backend"

# Per-machine install state (shared with the developer's build.sh):
VENV_DIR="$HOME/Library/Application Support/DAWalka/venv"
LOG_DIR="$HOME/Library/Application Support/DAWalka"
MODELS_DIR="$LOG_DIR/models"
VENDOR_DIR="$LOG_DIR/python_backend/vendor"
USER_AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
INSTALL_PATH="$USER_AU_DIR/DAWalka.component"
USER_VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
VST3_INSTALL_PATH="$USER_VST3_DIR/DAWalka.vst3"

# Pretty output
if [[ -t 1 ]]; then
    BOLD=$'\033[1m'; DIM=$'\033[2m'; RESET=$'\033[0m'
    RED=$'\033[0;31m'; GREEN=$'\033[0;32m'; YELLOW=$'\033[1;33m'
    BLUE=$'\033[0;34m'; CYAN=$'\033[0;36m'
else
    BOLD=""; DIM=""; RESET=""; RED=""; GREEN=""; YELLOW=""; BLUE=""; CYAN=""
fi

step()  { printf "\n${BLUE}${BOLD}==>${RESET} ${BOLD}%s${RESET}\n" "$1"; }
ok()    { printf "  ${GREEN}\xe2\x9c\x93${RESET} %s\n" "$1"; }
info()  { printf "  ${DIM}%s${RESET}\n" "$1"; }
warn()  { printf "  ${YELLOW}!${RESET} %s\n" "$1"; }
fail()  { printf "\n  ${RED}\xe2\x9c\x97 %s${RESET}\n\n" "$1" >&2; exit 1; }
hr()    { printf "${DIM}%s${RESET}\n" "------------------------------------------------------------"; }

notify() {
    local title="$1" message="$2"
    if command -v osascript >/dev/null 2>&1; then
        osascript -e "display notification \"$message\" with title \"$title\"" \
                  >/dev/null 2>&1 || true
    fi
}

banner() {
    printf "${CYAN}${BOLD}"
    cat <<'EOF'

        ____  ____  __      __    _      _  __      __  _   _
       |  _ \/ _  \ \ \    / /   / \    / \ \ \    / / | \ | |
       | | | | | | | \ \  / /   / _ \  / _ \ \ \  / /  |  \| |
       | |_| | |_| |  \ \/ /   / ___ \/ ___ \ \ \/ /   | |\  |
       |____/ \___/    \__/   /_/   \_\/   \_\ \__/    |_| \_|

              AI Audio Generator - one-click installer

EOF
    printf "${RESET}\n"
}

# Run a command, only printing its output on failure (keeps the
# success path quiet, but failures are debuggable).
run_quiet() {
    local log
    log="$(mktemp -t dawalka-install.XXXXXX)"
    if ! "$@" >"$log" 2>&1; then
        printf "\n${RED}--- command failed: $* ---${RESET}\n" >&2
        tail -40 "$log" >&2
        printf "${RED}------------------------------${RESET}\n\n" >&2
        rm -f "$log"
        return 1
    fi
    rm -f "$log"
    return 0
}

run_auval_dawalka_check() {
    local timeout="${1:-20}"
    local log pid elapsed rc
    log="$(mktemp -t dawalka-auval.XXXXXX)"

    if ! command -v auval >/dev/null 2>&1; then
        rm -f "$log"
        return 2
    fi

    auval -a >"$log" 2>/dev/null &
    pid=$!
    elapsed=0
    while kill -0 "$pid" 2>/dev/null && [[ $elapsed -lt $timeout ]]; do
        sleep 1
        elapsed=$((elapsed + 1))
    done

    if kill -0 "$pid" 2>/dev/null; then
        kill "$pid" 2>/dev/null || true
        sleep 1
        kill -9 "$pid" 2>/dev/null || true
        rm -f "$log"
        return 124
    fi

    wait "$pid"
    rc=$?
    if [[ $rc -eq 0 ]] && grep -qi "dawalka" "$log"; then
        rm -f "$log"
        return 0
    fi

    rm -f "$log"
    return 1
}

# Check if a given python interpreter is >= 3.10.  Returns 0 (yes) or 1 (no).
# We can't use a string compare because "3.9" > "3.10" lexicographically.
ver_at_least_310() {
    local py="$1"
    local ver major minor
    ver="$("$py" -c 'import sys;print("%d.%d"%sys.version_info[:2])' 2>/dev/null)" || return 1
    major="${ver%.*}"
    minor="${ver#*.}"
    (( major > 3 || (major == 3 && minor >= 10) ))
}

# Find a Python >= 3.10 on the system.  macOS's /usr/bin/python3 is
# always 3.9 (or 2.7 on older systems) — useless for our venv — so
# we look at the real install locations: Homebrew on Apple Silicon
# (/opt/homebrew), Homebrew on Intel (/usr/local), and python.org's
# framework (/Library/Frameworks/Python.framework).  Only as a last
# resort do we fall back to whatever `python3` is on PATH, which
# catches the rare case where the user installed Python somewhere
# exotic (pyenv, asdf, custom prefix, …) and added it to PATH.
#
# Forward-compatible: we list python3.10 through python3.15 so
# future Python releases work without a code change.  Each candidate
# is checked with ver_at_least_310, not just for existence, so a
# stale 3.9 install somewhere on the system can't fool us.
#
# When MULTIPLE Python 3.10+ versions are installed (e.g. the user
# has python.org 3.14 AND a leftover Homebrew 3.12), we pick the
# NEWEST one — not the first one in PATH or the first one we happen
# to enumerate.
find_python_310() {
    local prefixes=(
        /opt/homebrew/bin                              # Apple Silicon Homebrew
        /usr/local/bin                                 # Intel Homebrew
        /Library/Frameworks/Python.framework/Versions  # python.org
    )
    local versions=(3.15 3.14 3.13 3.12 3.11 3.10)
    local candidates=()
    for prefix in "${prefixes[@]}"; do
        for v in "${versions[@]}"; do
            local p="$prefix/python${v}"
            [[ -x "$p" ]] && candidates+=("$p")
        done
        # Unversioned python3 in the same dir — Homebrew always
        # creates this as a symlink to the latest installed version.
        local p="$prefix/python3"
        [[ -x "$p" ]] && candidates+=("$p")
        # python.org's framework uses $prefix/3.X/bin/python3, not
        # $prefix/python3.  The wildcard 3.* catches 3.10, 3.11, …
        # 3.99 and any future releases.
        for d in "$prefix"/3.*/bin/python3; do
            [[ -x "$d" ]] && candidates+=("$d")
        done
    done
    # PATH-based fallback (catches pyenv, asdf, custom prefix, etc.)
    for cmd in python3.15 python3.14 python3.13 python3.12 python3.11 python3.10 python3; do
        if command -v "$cmd" >/dev/null 2>&1; then
            candidates+=("$(command -v "$cmd")")
        fi
    done

    # Pick the candidate with the highest reported version that's
    # still >= 3.10.  Encoding "major*1000 + minor" into a single
    # integer lets us compare 3.9 vs 3.10 vs 3.14 (and 4.x) with a
    # plain bash arithmetic compare.  Patch version (3.14.5 vs
    # 3.14.2) is ignored — we don't care about point releases.
    local best="" best_num=0
    for p in "${candidates[@]}"; do
        local ver major minor num
        ver="$("$p" -c 'import sys;print("%d.%d"%sys.version_info[:2])' 2>/dev/null)" || continue
        major="${ver%.*}"
        minor="${ver#*.}"
        if (( major > 3 || (major == 3 && minor >= 10) )); then
            num=$((major * 1000 + minor))
            if (( num > best_num )); then
                best="$p"
                best_num=$num
            fi
        fi
    done

    if [[ -n "$best" ]]; then
        echo "$best"
        return 0
    fi
    return 1
}

# ─── 0. Sanity check the .app bundle itself ────────────────────────────────
if [[ ! -d "$BUNDLED_COMPONENT" ]]; then
    fail "Pre-built component not found at: $BUNDLED_COMPONENT

This usually means the .app was created without first running build.sh
or the install scripts in installer/ are missing.  Re-create the .app
with installer/make_app.sh."
fi
if [[ ! -d "$BUNDLED_VST3" ]]; then
    fail "Pre-built VST3 not found at: $BUNDLED_VST3

This usually means the .app was created without first running build.sh
or the install scripts in installer/ are missing.  Re-create the .app
with installer/make_app.sh."
fi
if [[ ! -d "$BUNDLED_PYTHON_BACKEND" ]]; then
    fail "Python backend not found at: $BUNDLED_PYTHON_BACKEND

Same fix as above - re-create DAWalka.app with installer/make_app.sh."
fi

banner
hr
printf "  ${DIM}App:${RESET}      %s\n" "$APP_ROOT"
printf "  ${DIM}Plugin:${RESET}  %s\n" "$INSTALL_PATH"
printf "  ${DIM}Python:${RESET}  %s\n" "$VENV_DIR"
hr
echo

# ─── 1. System sanity ──────────────────────────────────────────────────────
step "1/5  Checking system requirements"

ARCH="$(uname -m)"
if [[ "$ARCH" != "arm64" ]]; then
    fail "DAWalka requires Apple Silicon (MLX).  Architecture: $ARCH"
fi
ok "Apple Silicon ($ARCH)"

# Xcode Command Line Tools - needed for auval and for any C/C++ toolchain
# the user might want to use later.  We don't auto-install (it requires
# a UI dialog) - we just fail with a clear message.
if ! xcode-select -p >/dev/null 2>&1; then
    fail "Xcode Command Line Tools are not installed.

Open Terminal and run:
    xcode-select --install
When the install finishes, launch DAWalka.app again."
fi
ok "Xcode CLT: $(xcode-select -p)"

# Python 3.10+.  macOS's /usr/bin/python3 is 3.9 and useless for our
# venv, so we explicitly search Homebrew (both arches) and python.org
# before giving up.  See find_python_310 above for the search order.
if ! PYTHON="$(find_python_310)"; then
    fail "Python 3.10+ not found.

macOS ships only Python 3.9 in /usr/bin, which is not enough.
Install Python 3.10+ in one of these ways:

  Homebrew (recommended):
      brew install python@3.11

  python.org:
      https://www.python.org/downloads/macos/

After installing, launch DAWalka.app again."
fi
# From here on, use $PYTHON instead of bare 'python3'.  We also put
# its directory at the front of PATH so that any subprocesses
# (e.g. pip, the python backend) pick up the right interpreter.
export PATH="$(dirname "$PYTHON"):$PATH"
PY_VERSION="$("$PYTHON" -c 'import sys;print("%d.%d"%sys.version_info[:2])')"
ok "Python $PY_VERSION — $PYTHON"

# Disk space - models are 6.7 GB, venv is ~1.5 GB, be generous.
FREE_GB="$(df -g "$HOME" | awk 'NR==2 {print $4}')"
if [[ -n "$FREE_GB" && "$FREE_GB" -lt 10 ]]; then
    fail "Low disk space: ${FREE_GB} GB free, need at least 10 GB"
fi
ok "Free: ${FREE_GB} GB"

# ─── 2. Python venv + deps ─────────────────────────────────────────────────
step "2/5  Creating Python venv"
mkdir -p "$(dirname "$VENV_DIR")"

# Check if existing venv uses compatible Python version (>= 3.10)
venv_needs_recreate=0
if [[ -d "$VENV_DIR" && -x "$VENV_DIR/bin/python3" ]]; then
    VENV_PY_VERSION="$("$VENV_DIR/bin/python3" -c 'import sys;print("%d.%d"%sys.version_info[:2])' 2>/dev/null)"
    if [[ -n "$VENV_PY_VERSION" ]]; then
        VENV_MAJOR="${VENV_PY_VERSION%.*}"
        VENV_MINOR="${VENV_PY_VERSION#*.}"
        if (( VENV_MAJOR < 3 || (VENV_MAJOR == 3 && VENV_MINOR < 10) )); then
            warn "Existing venv uses Python $VENV_PY_VERSION (< 3.10), will recreate"
            venv_needs_recreate=1
        fi
    else
        warn "Cannot determine venv Python version, will recreate"
        venv_needs_recreate=1
    fi
else
    venv_needs_recreate=1
fi

if [[ $venv_needs_recreate -eq 1 ]]; then
    rm -rf "$VENV_DIR"
    if ! "$PYTHON" -m venv "$VENV_DIR"; then
        fail "Failed to create venv.  Check that $PYTHON works."
    fi
    ok "venv created with Python $PY_VERSION"
else
    ok "venv already exists with compatible Python $VENV_PY_VERSION"
fi

VENV_PY="$VENV_DIR/bin/python3"
if [[ ! -x "$VENV_PY" ]]; then
    fail "venv python not found: $VENV_PY"
fi
ok "Activated $("$VENV_PY" -V 2>&1)"

if ! run_quiet "$VENV_PY" -m pip install --upgrade pip wheel setuptools; then
    fail "Failed to upgrade pip"
fi
ok "pip upgraded"

step "3/5  Installing Python dependencies"

if [[ ! -f "$BUNDLED_PYTHON_BACKEND/requirements.txt" ]]; then
    fail "Not found: $BUNDLED_PYTHON_BACKEND/requirements.txt"
fi

if ! run_quiet "$VENV_PY" -m pip install -r "$BUNDLED_PYTHON_BACKEND/requirements.txt"; then
    fail "Failed to install base packages"
fi
ok "Base packages (aiohttp, numpy, soundfile, scipy, huggingface_hub, requests)"

# Pre-flight import check.  Catches the case where requirements.txt
# evolved (new dep added) and the venv was created before that, so
# pip install -r above is a no-op (everything already installed)
# but the new code path in _hf_download.py needs the new dep.
# Without this, the download loop crashes 8 times with
# "ModuleNotFoundError" and the user has no clue why.
for mod in requests huggingface_hub sentencepiece; do
    if ! "$VENV_PY" -c "import $mod" 2>/dev/null; then
        fail "Python module '$mod' is not installed in the venv.

  This can happen if the venv was created before '$mod' was added
  to requirements.txt.  Delete the venv and re-run the installer:
      rm -rf '$VENV_DIR'
  Then click Install again."
    fi
done
ok "All Python modules available"

# Check MLX version and upgrade if needed (require >= 0.30)
MLX_VERSION="$("$VENV_PY" -c 'import mlx.core; print(mlx.core.__version__)' 2>/dev/null)"
if [[ -n "$MLX_VERSION" ]]; then
    MLX_MAJOR="${MLX_VERSION%%.*}"
    MLX_MINOR_PATCH="${MLX_VERSION#*.}"
    MLX_MINOR="${MLX_MINOR_PATCH%%.*}"
    if (( MLX_MAJOR == 0 && MLX_MINOR < 30 )); then
        warn "MLX version $MLX_VERSION < 0.30, upgrading..."
        if ! run_quiet "$VENV_PY" -m pip install --upgrade 'mlx>=0.30'; then
            fail "Failed to upgrade MLX"
        fi
        ok "MLX upgraded to $("$VENV_PY" -c 'import mlx.core; print(mlx.core.__version__)' 2>/dev/null)"
    else
        ok "MLX $MLX_VERSION already installed (>= 0.30)"
    fi
else
    warn "Installing MLX (Apple Silicon Metal acceleration)..."
    if ! run_quiet "$VENV_PY" -m pip install 'mlx>=0.30'; then
        fail "MLX is required on Apple Silicon"
    fi
    ok "MLX installed: $("$VENV_PY" -c 'import mlx.core; print(mlx.core.__version__)' 2>/dev/null)"
fi

# Upgrade sentencepiece if needed (require >= 0.2.0)
SP_VERSION="$("$VENV_PY" -c 'import sentencepiece; print(sentencepiece.__version__)' 2>/dev/null)"
if [[ -n "$SP_VERSION" ]]; then
    SP_MAJOR="${SP_VERSION%%.*}"
    SP_MINOR_PATCH="${SP_VERSION#*.}"
    SP_MINOR="${SP_MINOR_PATCH%%.*}"
    if (( SP_MAJOR == 0 && SP_MINOR < 2 )); then
        warn "sentencepiece version $SP_VERSION < 0.2.0, upgrading..."
        if ! run_quiet "$VENV_PY" -m pip install --upgrade 'sentencepiece>=0.2.0'; then
            fail "Failed to upgrade sentencepiece"
        fi
        ok "sentencepiece upgraded to $("$VENV_PY" -c 'import sentencepiece; print(sentencepiece.__version__)' 2>/dev/null)"
    else
        ok "sentencepiece $SP_VERSION already installed (>= 0.2.0)"
    fi
else
    warn "Installing sentencepiece..."
    if ! run_quiet "$VENV_PY" -m pip install 'sentencepiece>=0.2.0'; then
        fail "Failed to install sentencepiece"
    fi
    ok "sentencepiece installed"
fi

# ─── 4. Models (skipped if all 8 .npz already exist) ──────────────────────
step "4/5  Downloading Stable Audio 3 MLX weights (if not present)"
mkdir -p "$MODELS_DIR"

# Optional HF auth (only for rate limit bumps)
if [[ -n "${HF_TOKEN:-}" ]]; then
    "$VENV_PY" -c "from huggingface_hub import login; login(token='${HF_TOKEN}', add_to_git_credential=False)" 2>/dev/null \
        && ok "HuggingFace: authenticated with HF_TOKEN" \
        || warn "HF_TOKEN was not accepted"
fi

REPO="stabilityai/stable-audio-3-optimized"
declare -a FILES=(
    "MLX/dit_sm-music_f16.npz"
    "MLX/dit_sm-sfx_f16.npz"
    "MLX/dit_medium_f16.npz"
    "MLX/same_s_decoder_f32.npz"
    "MLX/same_l_decoder_f32.npz"
    "MLX/same_s_encoder_f32.npz"
    "MLX/same_l_encoder_f32.npz"
    "MLX/t5gemma_f16.npz"
)

total=${#FILES[@]}
i=0
downloaded=0
skipped=0
for hf_filename in "${FILES[@]}"; do
    i=$((i+1))
    base=$(basename "$hf_filename")
    out="$MODELS_DIR/$base"
    if [[ -s "$out" ]]; then
        info "  [$i/$total] $base -- already downloaded"
        skipped=$((skipped+1))
        continue
    fi
    info "  [$i/$total] $base -- downloading..."
    # Use a status file instead of `2>&1 | tail -n 1` (which is racy
    # and races with stderr output from huggingface_hub).
    status_file="$(mktemp -t dawalka-dl.XXXXXX)"
    export DAWALKA_STATUS_FILE="$status_file"
    "$VENV_PY" "$BUNDLED_PYTHON_BACKEND/_hf_download.py" \
        --repo "$REPO" \
        --file "$hf_filename" \
        --out-dir "$MODELS_DIR" \
        ${HF_TOKEN:+--token "$HF_TOKEN"}
    result_line="$(cat "$status_file" 2>/dev/null || echo ERR no_status)"
    rm -f "$status_file"
    unset DAWALKA_STATUS_FILE
    if [[ "$result_line" == OK* ]]; then
        ok "  [$i/$total] $base"
        downloaded=$((downloaded+1))
    else
        warn "  [$i/$total] $base -- $result_line"
        info "  Re-run to retry the download"
    fi
done
ok "Downloaded $downloaded, already had $skipped. Total: $(du -sh "$MODELS_DIR" 2>/dev/null | cut -f1 || echo ?)"

# Vendored sa3_mlx inference code (cloned from Stability-AI/stable-audio-3).
# The .app doesn't bundle it because it's not needed at install time
# (only the weights are).  Clone it into the shared per-machine vendor
# directory the way build.sh does.
mkdir -p "$VENDOR_DIR"
if [[ ! -d "$VENDOR_DIR/sa3_mlx/models/defs" ]]; then
    info "vendor/sa3_mlx missing - cloning..."
    rm -rf /tmp/sa3_clone
    if ! git clone --depth 1 --filter=blob:none --sparse https://github.com/Stability-AI/stable-audio-3.git /tmp/sa3_clone 2>&1 | tail -3; then
        fail "Failed to clone stabilityai/stable-audio-3 (check your internet connection)"
    fi
    (cd /tmp/sa3_clone && git sparse-checkout set optimized/mlx && cp -R optimized/mlx "$VENDOR_DIR/sa3_mlx")
    rm -rf /tmp/sa3_clone
    rm -rf "$VENDOR_DIR/sa3_mlx/ableton" \
           "$VENDOR_DIR/sa3_mlx/benchmark.sh" \
           "$VENDOR_DIR/sa3_mlx/bootstrap.sh" \
           "$VENDOR_DIR/sa3_mlx/install.sh" \
           "$VENDOR_DIR/sa3_mlx/sa3"
    rm -f "$VENDOR_DIR/sa3_mlx/scripts/install.py" \
          "$VENDOR_DIR/sa3_mlx/scripts/test_all_configs.py" \
          "$VENDOR_DIR/sa3_mlx/scripts/benchmark.py" \
          "$VENDOR_DIR/sa3_mlx/scripts/examples.py"
    ok "vendor/sa3_mlx deployed"
else
    ok "vendor/sa3_mlx already in place"
fi

# ─── 5. Install the pre-built plug-ins ─────────────────────────────────────
step "5/5  Installing AU and VST3 plug-ins"
mkdir -p "$USER_AU_DIR"
mkdir -p "$USER_VST3_DIR"

# Stop any running backend so the .component isn't locked
pkill -9 -f "DAWalka.*server.py" 2>/dev/null && ok "Backend stopped" || info "Backend not running"
killall -9 "DAWalka Launcher" 2>/dev/null || true
sleep 1

# Also remove the old launcher if it was used (e.g. from a previous
# dev build).  The .app's install path uses the .component's embedded
# launcher, so the standalone one is no longer needed.
rm -rf "$HOME/Applications/DAWalka Launcher.app" 2>/dev/null || true

rm -rf "$INSTALL_PATH"
rm -rf "$VST3_INSTALL_PATH"
if ! cp -R "$BUNDLED_COMPONENT" "$USER_AU_DIR/"; then
    fail "Failed to copy AU plugin to $USER_AU_DIR"
fi
if ! cp -R "$BUNDLED_VST3" "$USER_VST3_DIR/"; then
    fail "Failed to copy VST3 plugin to $USER_VST3_DIR"
fi
if command -v codesign >/dev/null 2>&1; then
    codesign --force --deep --sign - "$INSTALL_PATH" >/dev/null 2>&1 \
        && ok "AU ad-hoc signature refreshed" \
        || warn "Could not refresh AU ad-hoc signature"
    codesign --force --deep --sign - "$VST3_INSTALL_PATH" >/dev/null 2>&1 \
        && ok "VST3 ad-hoc signature refreshed" \
        || warn "Could not refresh VST3 ad-hoc signature"
fi
ok "AU plugin installed: $INSTALL_PATH"
ok "VST3 plugin installed: $VST3_INSTALL_PATH"

# Re-register Audio Units
killall -9 audiounitservicecrasher 2>/dev/null || true
if command -v auval >/dev/null 2>&1; then
    if run_auval_dawalka_check 20; then
        ok "auval confirmed registration"
    else
        case $? in
            124)
                warn "auval took longer than 20 seconds - skipping so the installer can finish"
                ;;
            *)
                warn "auval did not find DAWalka. Restart your DAW, or run:"
                warn "    auval -a | grep -i dawalka"
                ;;
        esac
    fi
else
    info "auval unavailable - skipping"
fi

if [[ -d "$VST3_INSTALL_PATH/Contents/Resources/python_backend" ]]; then
    ok "VST3 bundle contains python_backend"
else
    warn "VST3 bundle installed, but python_backend is missing"
fi

# ─── Summary ───────────────────────────────────────────────────────────────
hr
echo
printf "  ${GREEN}${BOLD}\xe2\x9c\x93 DAWalka is ready to use!${RESET}\n"
hr
echo
printf "  ${BOLD}AU plugin:${RESET}   %s\n" "$INSTALL_PATH"
printf "  ${BOLD}VST3 plugin:${RESET} %s\n" "$VST3_INSTALL_PATH"
printf "  ${BOLD}Python venv:${RESET} %s\n" "$VENV_DIR"
printf "  ${BOLD}Models:${RESET}      %s (%s)\n" "$MODELS_DIR" "$(du -sh "$MODELS_DIR" 2>/dev/null | cut -f1 || echo ?)"
echo
printf "  ${BOLD}Next steps:${RESET}\n"
printf "    1. Open ${BOLD}Logic, Reaper, Bitwig, Ableton, or another AU/VST3 host${RESET}\n"
printf "    2. Create an instrument/generator track\n"
printf "    3. Pick ${BOLD}DAWalka${RESET} from the AU or VST3 list\n"
printf "    4. Type a prompt and press ${BOLD}GENERATE${RESET}\n"
echo
hr
echo

notify "DAWalka" "Installation complete \xe2\x9c\x85"
exit 0