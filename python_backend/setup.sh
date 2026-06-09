#!/usr/bin/env bash
# Setup helper for the DAWalka Python backend.
#
# Creates a virtual environment and installs the inference dependencies
# for the pure-MLX Stable Audio 3 backend.  Run from inside the
# `python_backend/` directory:
#
#     ./setup.sh
#
# On Apple Silicon it also installs MLX (Metal acceleration).  No
# PyTorch is needed at runtime.
set -euo pipefail

cd "$(dirname "$0")"

VENV="${VENV:-./venv}"
PYTHON_BIN="${PYTHON_BIN:-python3}"

# Find Python >= 3.10
find_python_310() {
    local best="" best_num=0
    for cmd in python3.15 python3.14 python3.13 python3.12 python3.11 python3.10 python3; do
        if command -v "$cmd" >/dev/null 2>&1; then
            local p ver major minor num
            p="$(command -v "$cmd")"
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
        fi
    done
    if [[ -n "$best" ]]; then echo "$best"; return 0; fi
    return 1
}

# Resolve the best available Python
RESOLVED_PYTHON=""
if [[ "$PYTHON_BIN" == "python3" ]] || ! command -v "$PYTHON_BIN" >/dev/null 2>&1; then
    if RESOLVED_PYTHON="$(find_python_310)"; then
        echo "Found Python >= 3.10: $RESOLVED_PYTHON"
        PYTHON_BIN="$RESOLVED_PYTHON"
    else
        echo "ERROR: Python >= 3.10 not found. Install via: brew install python@3.12" >&2
        exit 1
    fi
else
    # Check if the explicitly provided PYTHON_BIN is >= 3.10
    ver="$("$PYTHON_BIN" -c 'import sys;print("%d.%d"%sys.version_info[:2])' 2>/dev/null)" || true
    if [[ -n "$ver" ]]; then
        major="${ver%.*}"
        minor="${ver#*.}"
        if (( major < 3 || (major == 3 && minor < 10) )); then
            echo "WARNING: $PYTHON_BIN is Python $ver (< 3.10), searching for a newer version..." >&2
            if RESOLVED_PYTHON="$(find_python_310)"; then
                echo "Using $RESOLVED_PYTHON instead"
                PYTHON_BIN="$RESOLVED_PYTHON"
            else
                echo "ERROR: No Python >= 3.10 found. Install via: brew install python@3.12" >&2
                exit 1
            fi
        fi
    fi
fi

PY_VERSION="$("$PYTHON_BIN" -c 'import sys;print("%d.%d"%sys.version_info[:2])')"
echo "Using Python $PY_VERSION — $PYTHON_BIN"

# Check if existing venv uses compatible Python
venv_needs_recreate=0
if [[ -d "$VENV" && -x "$VENV/bin/python3" ]]; then
    VENV_PY_VER="$("$VENV/bin/python3" -c 'import sys;print("%d.%d"%sys.version_info[:2])' 2>/dev/null)"
    if [[ -n "$VENV_PY_VER" ]]; then
        V_MAJOR="${VENV_PY_VER%.*}"
        V_MINOR="${VENV_PY_VER#*.}"
        if (( V_MAJOR < 3 || (V_MAJOR == 3 && V_MINOR < 10) )); then
            echo "Existing venv uses Python $VENV_PY_VER (< 3.10), recreating..."
            venv_needs_recreate=1
        fi
    else
        echo "Cannot determine venv Python version, recreating..."
        venv_needs_recreate=1
    fi
else
    venv_needs_recreate=1
fi

if [[ $venv_needs_recreate -eq 1 ]]; then
    rm -rf "$VENV"
    echo "Creating virtualenv at $VENV using $PYTHON_BIN"
    "$PYTHON_BIN" -m venv "$VENV"
else
    echo "Existing venv uses compatible Python $VENV_PY_VER, keeping it"
fi

# shellcheck disable=SC1091
source "$VENV/bin/activate"

echo "Upgrading pip / wheel"
pip install --upgrade pip wheel 2>&1 | tail -3

echo "Installing base requirements"
pip install -r requirements.txt 2>&1 | tail -5

# Apple Silicon -> MLX with version check
if [[ "$(uname -s)" == "Darwin" && "$(uname -m)" == "arm64" ]]; then
    echo "Apple Silicon detected"
    # Check existing MLX version
    MLX_VER="$("$VENV/bin/python3" -c 'import mlx.core; print(mlx.core.__version__)' 2>/dev/null || true)"
    if [[ -n "$MLX_VER" ]]; then
        MLX_MAJ="${MLX_VER%%.*}"
        MLX_MINP="${MLX_VER#*.}"
        MLX_MIN="${MLX_MINP%%.*}"
        if (( MLX_MAJ == 0 && MLX_MIN < 30 )); then
            echo "MLX $MLX_VER < 0.30, upgrading..."
            pip install --upgrade "mlx>=0.30" 2>&1 | tail -5
        else
            echo "MLX $MLX_VER already installed (>= 0.30)"
        fi
    else
        echo "Installing MLX..."
        if pip install "mlx>=0.30" 2>&1 | tee /tmp/mlx-install.log | tail -8; then
            if "$VENV/bin/python3" -c "import mlx.core" 2>/dev/null; then
                echo "mlx import OK"
            else
                echo "mlx install completed but import FAILED; reinstalling verbose"
                pip install --force-reinstall "mlx>=0.30" 2>&1 | tail -20
            fi
        else
            echo "   (mlx install failed; inference requires Apple Silicon)"
            tail -30 /tmp/mlx-install.log
            exit 1
        fi
    fi
else
    echo "WARNING: this backend requires Apple Silicon (MLX).  No MLX installed."
fi

echo
echo "DAWalka backend environment ready."
echo "  Activate with: source $VENV/bin/activate"
echo "  Run with:      $VENV/bin/python3 server.py --models \"$HOME/Library/Application Support/DAWalka/models\""
