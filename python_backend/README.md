# DAWalka — Python backend

This directory contains the local AI service that the **DAWalka** Audio
Unit plugin talks to.  Everything runs **on-device** — no cloud, no
network calls beyond the first model download.

## Why a separate process?

The plugin's audio thread (and the host's main thread) must never block on
inference.  By spawning this backend as its own Python process we get:

* zero risk of starving the audio thread (no GIL contention, no UI hitches)
* ability to use Python-only inference frameworks (MLX, PyTorch)
* easy recovery if inference crashes (the plugin re-spawns the process)

## Layout

```
python_backend/
├── server.py            # aiohttp service with all REST endpoints
├── stable_audio_backend.py
├── requirements.txt     # Python dependencies
├── setup.sh             # creates a venv, installs MLX / PyTorch / stable-audio-tools
└── vendor/              # (created by setup.sh) stable-audio-tools source
```

## First-time setup

```bash
cd python_backend
./setup.sh
```

This creates `venv/`, installs:

* `aiohttp` (HTTP server)
* `numpy`, `soundfile` (audio I/O)
* `mlx` + `mlx-metal` (Apple Silicon) — preferred
* `torch` + `torchaudio` — fallback
* the **stable-audio-tools** repository (vendored from
  https://github.com/Stability-AI/stable-audio-tools)

## Running standalone

```bash
source venv/bin/activate
python server.py \
    --models "$HOME/Library/Application Support/DAWalka/models" \
    --outputs "$HOME/Library/Application Support/DAWalka/output" \
    --verbose
```

The plugin will detect and launch the same script automatically the first
time you press **Generate** — you only need the manual command above for
debugging.

## REST API

| Method | Path                 | Description |
|--------|----------------------|-------------|
| GET    | `/health`            | Liveness + backend name |
| GET    | `/models`            | Catalogue + installed flag |
| POST   | `/load/{model_id}`   | Load a model into memory |
| POST   | `/generate`          | Submit a generation job |
| GET    | `/job/{id}`          | Poll job status / result |
| POST   | `/cancel`            | Cancel a queued / running job |
| POST   | `/shutdown`          | Graceful shutdown |

## Backend priority

1. **MLX**  — Apple Silicon, Metal-backed, fastest path.
2. **PyTorch** — CPU (or CUDA on Linux / Intel Macs).  Slower but
   functional.

The backend is auto-selected at startup; the plugin reads the choice via
`/health` and displays the active backend in its status label.
