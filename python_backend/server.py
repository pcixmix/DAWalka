#!/usr/bin/env python3
"""
DAWalka backend — local HTTP service that hosts Stable Audio 3 inference
for the DAWalka AU plugin.

The plugin talks to this service over 127.0.0.1:47823 ("DWLK" on a phone
keypad).  The service runs in its own process so the AU host audio thread
is never blocked, regardless of how long inference takes.

Endpoints
---------
GET  /health                → liveness probe
GET  /models                → catalogue + installed state
POST /load/{model_id}       → load a model into memory (unloads previous)
POST /generate              → enqueue a generation job
GET  /job/{id}              → poll a job (status / progress / result)
POST /cancel                → cancel a job
POST /shutdown              → graceful shutdown

Inference
---------
Pure-MLX backend (Apple Silicon, no PyTorch at runtime) using the vendored
copy of Stability AI's official sa3_mlx package — see
https://github.com/Stability-AI/stable-audio-3/tree/main/optimized/mlx

Weights come from the non-gated `stabilityai/stable-audio-3-optimized`
HuggingFace repo (pre-converted MLX .npz files).  No HF token is required
for downloads.
"""
from __future__ import annotations

import argparse
import asyncio
import gc
import json
import math
import logging
import os
import sys
import threading
import time
import traceback
import uuid
import wave
from dataclasses import dataclass
from http import HTTPStatus
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional, Tuple

import numpy as np
import soundfile as sf
from aiohttp import web

# ─── Vendor path setup ─────────────────────────────────────────────────────
ROOT = Path(__file__).resolve().parent
SA3_MLX = ROOT / "vendor" / "sa3_mlx"
if not SA3_MLX.is_dir():
    sys.exit(f"vendor/sa3_mlx missing at {SA3_MLX}")
sys.path.insert(0, str(SA3_MLX))

# Stable Audio 3 — pure MLX (no PyTorch)
from models.defs.sa3_pipeline import (  # noqa: E402
    apply_prompt_padding,
    build_pingpong_schedule,
    load_conditioner_from_npz,
    patched_decode,
    sample_flow_pingpong,
)
from models.defs.t5gemma_mlx import T5Gemma  # noqa: E402

SAMPLE_RATE = 44100
SAMPLES_PER_LATENT = 4096  # PatchedPretransform downsample × SAME 16× expansion

log = logging.getLogger("dawalka")


def _init_mlx_stream():
    """Pre-warm the MLX Metal stream on the current thread.

    Called once at server startup, on the dedicated single-thread
    executor that will handle ALL subsequent MLX work (model load
    and inference).  By binding the stream to this thread before the
    model is ever loaded, we guarantee the same stream is used for
    every GPU operation, which MLX requires.

    See https://github.com/ml-explore/mlx-lm/issues/1181 — without
    this, switching threads between load() and generate() causes
    `RuntimeError: There is no Stream(gpu, 0) in current thread.`
    """
    import mlx.core as mx
    stream = mx.new_stream(mx.gpu)
    mx.set_default_stream(stream)
    # Touch the GPU so the stream is fully active (creates the
    # underlying Metal command queue lazily).
    a = mx.array([0.0])
    mx.eval(a)


# ─── Catalogue (must stay in sync with C++ ModelManager::getCatalogue()) ──
# Maps each model_id to the set of local weight files that must be present
# for the model to be considered "installed".  Files live in
# `models_dir` (~/Library/Application Support/DAWalka/models/) on disk.
# For our sa3_mlx inference, sm-music and sm-sfx share the SAME-S codec
# and all three share the T5Gemma text encoder.
HF_REPO_ID = "stabilityai/stable-audio-3-optimized"

# Per-model runtime config: which DiT module / npz / decoder to load.
#   - dit:        "sm"  → models.defs.dit_mlx  (sm-music, sm-sfx)
#                 "md"  → models.defs.dit_mlx_medium  (medium)
#   - dit_npz:    DiT weights filename in HF_REPO_ID under "MLX/"
#   - decoder:    "same-s" or "same-l" — must match the DiT family
#   - dec_npz:    decoder weights filename in HF_REPO_ID under "MLX/"
#   - enc_npz:    SAME encoder weights — required for audio-to-audio mode
#                 (loads lazily on first /a2a request so T2A users don't pay
#                  the ~215 MB / ~1.7 GB memory hit)
MODEL_RUNTIME: Dict[str, Dict[str, str]] = {
    "sa3-sm-music": {
        "dit":     "sm",
        "dit_npz": "MLX/dit_sm-music_f16.npz",
        "decoder": "same-s",
        "dec_npz": "MLX/same_s_decoder_f32.npz",
        "enc_npz": "MLX/same_s_encoder_f32.npz",
    },
    "sa3-sm-sfx": {
        "dit":     "sm",
        "dit_npz": "MLX/dit_sm-sfx_f16.npz",
        "decoder": "same-s",
        "dec_npz": "MLX/same_s_decoder_f32.npz",   # shared with sm-music
        "enc_npz": "MLX/same_s_encoder_f32.npz",   # shared with sm-music
    },
    "sa3-medium": {
        "dit":     "md",
        "dit_npz": "MLX/dit_medium_f16.npz",
        "decoder": "same-l",
        "dec_npz": "MLX/same_l_decoder_f32.npz",
        "enc_npz": "MLX/same_l_encoder_f32.npz",
    },
}
T5GEMMA_NPZ = "MLX/t5gemma_f16.npz"  # shared, downloaded once

CATALOGUE: List[Dict[str, Any]] = [
    {
        "id": "sa3-sm-music",
        "name": "Stable Audio 3 Small Music",
        "description": "Compact music / loop generator. ~1.9 GB. ~10x realtime on M1 8 GB.",
        "files": [
            "dit_sm-music_f16.npz",
            "same_s_decoder_f32.npz",
            "same_s_encoder_f32.npz",
            "t5gemma_f16.npz",
        ],
        "min_memory_gb": 8,
        "steps_default": 8,
    },
    {
        "id": "sa3-sm-sfx",
        "name": "Stable Audio 3 Small SFX",
        "description": "Sound effects & foley generator. ~1.9 GB. ~10x realtime on M1 8 GB.",
        "files": [
            "dit_sm-sfx_f16.npz",
            "same_s_decoder_f32.npz",
            "same_s_encoder_f32.npz",
            "t5gemma_f16.npz",
        ],
        "min_memory_gb": 8,
        "steps_default": 8,
    },
    {
        "id": "sa3-medium",
        "name": "Stable Audio 3 Medium",
        "description": "Higher-fidelity music + SFX. ~6.9 GB. ~2x realtime on M1 8 GB.",
        "files": [
            "dit_medium_f16.npz",
            "same_l_decoder_f32.npz",
            "same_l_encoder_f32.npz",
            "t5gemma_f16.npz",
        ],
        "min_memory_gb": 16,
        "steps_default": 8,
    },
]


# ─── Job bookkeeping ─────────────────────────────────────────────────────
@dataclass
class Job:
    id: str
    request: Dict[str, Any]
    status: str = "queued"           # queued | running | done | error | cancelled
    progress: float = 0.0
    error: str = ""
    audio_path: str = ""
    duration: float = 0.0
    sample_rate: int = SAMPLE_RATE
    channels: int = 2
    model: str = ""
    seed: int = 0
    started_at: float = 0.0
    finished_at: float = 0.0
    generation_time_ms: float = 0.0
    # "t2a" or "a2a" — picked at job submission, drives the worker
    # dispatch in _process().
    kind: str = "t2a"
    # Per-job output dir.  Empty means "use the worker's default".
    # A2A jobs use this to write into a separate folder from T2A.
    output_dir: str = ""

    def __post_init__(self):
        # Pull the per-job sample rate out of the request payload so the
        # WAV file is written at whatever the plugin asked for (44100
        # or 48000).  Clamp to the two supported values; anything else
        # falls back to the project-wide default.
        try:
            sr = int(self.request.get("sample_rate", SAMPLE_RATE))
            if sr in (44100, 48000):
                self.sample_rate = sr
        except (TypeError, ValueError):
            pass
        # Pull the per-job output dir (if the caller specified one)
        out = self.request.get("output_dir", "")
        if isinstance(out, str) and out:
            self.output_dir = out


# ─── Weights helper ──────────────────────────────────────────────────────
def ensure_weights(models_dir: Path, files: List[str]) -> List[Path]:
    """
    Download any missing weights from `stabilityai/stable-audio-3-optimized`
    into `models_dir` and return absolute paths.  Non-gated repo — no token
    needed, but logged-in users get higher rate limits.
    """
    from huggingface_hub import hf_hub_download

    models_dir.mkdir(parents=True, exist_ok=True)
    out: List[Path] = []
    for rel in files:
        local = models_dir / Path(rel).name
        if local.exists() and local.stat().st_size > 0:
            out.append(local)
            continue
        log.info("Downloading %s from %s", rel, HF_REPO_ID)
        path = hf_hub_download(
            repo_id=HF_REPO_ID,
            filename=rel,
            local_dir=str(models_dir),
        )
        out.append(Path(path))
    return out


# ─── Inference backend ───────────────────────────────────────────────────
class InferenceBackend:
    """
    Pure-MLX inference using Stability AI's sa3_mlx package.
    T5Gemma is loaded once and reused; DiT + decoder are reloaded on
    model switch.  All weights stay in memory between requests.
    """

    def __init__(self, models_dir: Path):
        self.models_dir = models_dir
        self.current_model_id: Optional[str] = None
        self.dtype = None     # mx.float16 for DiT, mx.float32 for decoder
        self.enc: Optional[T5Gemma] = None
        self.dit = None
        self.decoder = None
        self.dec_chunk_fn: Optional[Callable] = None
        self.dec_chunk = 0
        self.dec_ovl = 0
        self.cond_padding = None
        self.cond_secs = None
        # SAME encoder — loaded lazily on first /a2a request, kept resident
        # for subsequent A2A jobs.  Cleared in _unload_dit_locked() so model
        # switches release it.
        self.same_encoder = None
        self.same_encoder_kind: Optional[str] = None   # "same-s" or "same-l"
        self.same_encoder_pad_modulo = 0
        self.backend_name = "none"
        self._lock = threading.Lock()
        self._loading = False

    # ── backend selection ────────────────────────────────────────────────
    def initialize(self):
        try:
            import mlx.core as mx  # noqa: F401
            self.dtype = mx.float16
            self.backend_name = "mlx"
            log.info("MLX backend ready (Apple Silicon)")
        except Exception as e:
            log.error("MLX import failed: %s", e)
            log.error("Install with: pip install mlx mlx-metal")
            self.backend_name = "none"

    # ── model loading ────────────────────────────────────────────────────
    def load(self, model_id: str) -> bool:
        if model_id == self.current_model_id and self.dit is not None:
            return True
        if model_id not in MODEL_RUNTIME:
            log.error("Unknown model_id: %s", model_id)
            return False
        with self._lock:
            self._loading = True
            try:
                cfg = MODEL_RUNTIME[model_id]

                # Make sure all required weights are on disk
                files_for_model = [
                    cfg["dit_npz"],
                    cfg["dec_npz"],
                    T5GEMMA_NPZ,
                ]
                paths = ensure_weights(self.models_dir, files_for_model)
                path_by_hf = {p.name: p for p in paths}
                # ensure_weights returns paths keyed by basename; resolve
                # back to the actual file we asked for.
                def _resolve(hf_filename: str) -> Path:
                    base = Path(hf_filename).name
                    p = path_by_hf.get(base)
                    if p is None:
                        # Fall back: walk the dir for a match
                        for cand in self.models_dir.rglob(Path(hf_filename).name):
                            return cand
                        raise FileNotFoundError(hf_filename)
                    return p

                dit_path = _resolve(cfg["dit_npz"])
                dec_path = _resolve(cfg["dec_npz"])
                t5_path  = _resolve(T5GEMMA_NPZ)

                # 1. Load T5Gemma (once)
                if self.enc is None:
                    log.info("Loading T5Gemma text encoder from %s", t5_path)
                    self.enc = T5Gemma.from_npz(str(t5_path))

                # 2. Free previous DiT + decoder
                self._unload_dit_locked()

                # 3. Load conditioner (baked into the DiT npz under "cond." prefix)
                log.info("Loading conditioner from %s", dit_path)
                import mlx.core as mx
                self.cond_padding, self.cond_secs = load_conditioner_from_npz(
                    str(dit_path), prefix="cond."
                )
                # Cast to compute dtype so we don't dcast inside the loop
                self.cond_padding = mx.array(self.cond_padding.astype(self.dtype))

                # 4. Load DiT
                log.info("Loading DiT (%s) from %s", cfg["dit"], dit_path)
                if cfg["dit"] == "sm":
                    from models.defs.dit_mlx import load_dit
                else:
                    from models.defs.dit_mlx_medium import load_dit
                self.dit = load_dit(str(dit_path), T_lat=1, dtype=self.dtype, compile_=False)

                # 5. Load decoder
                log.info("Loading decoder (%s) from %s", cfg["decoder"], dec_path)
                if cfg["decoder"] == "same-s":
                    from models.defs.same_s_decoder import load_model
                    self.dec_chunk, self.dec_ovl = 8, 2
                else:
                    from models.defs.same_l_decoder import load_model
                    self.dec_chunk, self.dec_ovl = 128, 8
                self.decoder = load_model(weights_path=str(dec_path), dtype=mx.float32, compile_=False)
                self.dec_chunk_fn = getattr(
                    __import__(f"models.defs.same_{'s' if cfg['decoder']=='same-s' else 'l'}_decoder",
                                fromlist=['decode_chunked']),
                    "decode_chunked",
                )

                self.current_model_id = model_id
                log.info("Loaded model %s", model_id)
                return True
            except Exception:
                log.exception("Failed to load model %s", model_id)
                self._unload_dit_locked()
                self.current_model_id = None
                return False
            finally:
                self._loading = False

    def _unload_dit_locked(self):
        for attr in ("dit", "decoder", "cond_padding", "cond_secs",
                     "dec_chunk_fn", "same_encoder", "same_encoder_kind",
                     "same_encoder_pad_modulo"):
            if getattr(self, attr, None) is not None:
                try:
                    delattr(self, attr)
                except Exception:
                    pass
                setattr(self, attr, None)
        try:
            import mlx.core as mx
            if hasattr(mx, "clear_cache"):
                mx.clear_cache()
            elif hasattr(mx, "metal") and hasattr(mx.metal, "clear_cache"):
                mx.metal.clear_cache()
        except Exception:
            pass
        gc.collect()

    def _ensure_encoder_for_a2a(self, model_id: str) -> None:
        """Lazy-load the SAME-S or SAME-L encoder for the active model.

        A2A needs to encode the input audio into latents via the SAME
        encoder that matches the decoder the DiT is paired with.  We
        keep the encoder resident across A2A jobs and only free it
        when the user switches to a different model.

        Raises RuntimeError if the encoder weights aren't on disk.
        """
        if model_id not in MODEL_RUNTIME:
            raise RuntimeError(f"Unknown model_id: {model_id}")
        cfg = MODEL_RUNTIME[model_id]
        decoder_kind = cfg["decoder"]
        if self.same_encoder is not None and self.same_encoder_kind == decoder_kind:
            return  # already loaded

        # Free any previously-loaded (wrong-kind) encoder
        self.same_encoder = None
        self.same_encoder_kind = None
        self.same_encoder_pad_modulo = 0

        enc_npz = cfg.get("enc_npz")
        if not enc_npz:
            raise RuntimeError(f"Model '{model_id}' has no encoder configured")

        enc_path = self.models_dir / Path(enc_npz).name
        if not enc_path.exists():
            # Try to download (idempotent — no-op if already there)
            from huggingface_hub import hf_hub_download
            log.info("Downloading %s from %s", enc_npz, HF_REPO_ID)
            p = hf_hub_download(repo_id=HF_REPO_ID, filename=enc_npz,
                                local_dir=str(self.models_dir))
            enc_path = Path(p)

        import mlx.core as mx
        if decoder_kind == "same-s":
            from models.defs.same_s_encoder import load_model
            self.same_encoder_pad_modulo = 32
        elif decoder_kind == "same-l":
            from models.defs.same_l_encoder import load_model
            self.same_encoder_pad_modulo = 16
        else:
            raise RuntimeError(f"Unknown decoder kind: {decoder_kind}")
        log.info("Loading SAME encoder (%s) from %s", decoder_kind, enc_path)
        self.same_encoder = load_model(weights_path=str(enc_path),
                                       dtype=mx.float32, compile_=False)
        self.same_encoder_kind = decoder_kind
        log.info("SAME encoder ready (%s, %d MB)",
                 decoder_kind,
                 enc_path.stat().st_size // (1024 * 1024) if enc_path.exists() else 0)

    # ── generation ───────────────────────────────────────────────────────
    def generate(
        self,
        prompt: str,
        seconds_total: float,
        seed: int,
        steps: int,
        progress_cb: Callable[[float], None],
    ) -> np.ndarray:
        if self.dit is None or self.enc is None:
            raise RuntimeError("No model loaded")
        import mlx.core as mx

        if seconds_total <= 0.0:
            seconds_total = 1.0
        steps = max(1, int(steps))

        # 1. Encode text
        progress_cb(0.02)
        embeds, mask = self.enc.encode([prompt], max_len=256)
        mx.eval(embeds, mask)
        progress_cb(0.05)

        # 2. Build conditioning
        embeds = embeds.astype(self.dtype)
        embeds_padded = apply_prompt_padding(embeds, mask, self.cond_padding)
        seconds_embed = self.cond_secs(seconds_total).astype(self.dtype)
        cross_attn = mx.concatenate([embeds_padded, seconds_embed], axis=1)
        global_cond = seconds_embed[:, 0, :]
        mx.eval(cross_attn, global_cond)
        progress_cb(0.10)

        # 3. Latent time axis
        T_lat = max(1, math.ceil(seconds_total * SAMPLE_RATE / SAMPLES_PER_LATENT))
        # Pad for SAME-S encoder/decoder
        if self.dec_chunk and (self.dec_chunk + 2 * self.dec_ovl) > 0:
            # SAME-S needs T_lat even (T_aud = T_lat * 16)
            if T_lat % 2 != 0 and self.current_model_id in ("sa3-sm-music", "sa3-sm-sfx"):
                T_lat += 1
        progress_cb(0.12)

        # 4. Build sampling schedule
        sigma_max = 1.0
        sigmas = build_pingpong_schedule(steps, sigma_max=sigma_max, use_logsnr_shift=True)

        # 5. Noise
        key = mx.random.key(seed)
        noise = mx.random.normal((1, 256, T_lat), dtype=self.dtype, key=key)
        mx.eval(noise)

        # 6. DiT forward closure
        def model_fn(x, t):
            return self.dit(x, t, cross_attn, global_cond, local_add_cond=None)

        # 7. Sample (pingpong)
        def on_step(i: int, total: int):
            # 12% reserved for setup, 70% for sampling, 18% for decode/write
            progress_cb(0.12 + 0.70 * (i / max(total, 1)))

        t_sample_start = time.time()
        latents = sample_flow_pingpong(
            model_fn, noise, sigmas, seed=seed + 1,
            paste_back=None, on_step=on_step,
        )
        mx.eval(latents)
        progress_cb(0.82)
        log.info("DiT sample: %.1f s for %d steps (%.1f s/step)",
                 time.time() - t_sample_start, steps,
                 (time.time() - t_sample_start) / max(steps, 1))

        # 8. Decode latents → audio patches
        latents_fp32 = latents.astype(mx.float32)
        kernel = self.dec_chunk + 2 * self.dec_ovl
        if T_lat > kernel:
            patches = self.dec_chunk_fn(self.decoder, latents_fp32,
                                        self.dec_chunk, self.dec_ovl)
        elif T_lat % 2 == 0:
            patches = self.decoder(latents_fp32)
        else:
            patches = self.dec_chunk_fn(self.decoder, latents_fp32, 2, 2)
        mx.eval(patches)
        progress_cb(0.95)

        # 9. Unpatch → (1, 2, T)
        audio = patched_decode(patches, patch_size=256, channels=2)
        mx.eval(audio)
        audio_np = np.array(audio.astype(mx.float32))[0]  # (2, T)

        # 10. Trim to requested length
        target_samples = int(round(seconds_total * SAMPLE_RATE))
        if audio_np.shape[-1] > target_samples:
            audio_np = audio_np[..., :target_samples]
        progress_cb(1.0)
        return audio_np

    def sample_rate(self) -> int:
        return SAMPLE_RATE

    # ── audio-to-audio ──────────────────────────────────────────────────
    @staticmethod
    def _read_wav_to_44100_stereo(path: str) -> np.ndarray:
        """Read any WAV/AIFF/FLAC into a (2, T) float32 array at 44.1 kHz.

        Uses soundfile (handles 16/24/32-bit, mono, any sr) and falls
        back to scipy resample_poly if the source rate isn't 44100.
        Mono input is duplicated to stereo; multi-channel is downmixed
        to the first 2 channels.
        """
        try:
            audio, sr = sf.read(path, always_2d=True)
        except Exception as e:
            raise RuntimeError(f"Failed to read audio: {path} ({e})")
        # audio shape: (T, C) — convert to (C, T)
        audio = audio.T.astype(np.float32)
        if audio.shape[0] > 2:
            audio = audio[:2]
        if audio.shape[0] == 1:
            audio = np.concatenate([audio, audio], axis=0)
        if sr != SAMPLE_RATE:
            from scipy.signal import resample_poly
            # integer-ratio resample for the common cases
            from math import gcd
            g = gcd(sr, SAMPLE_RATE)
            up, down = SAMPLE_RATE // g, sr // g
            audio = np.stack([
                resample_poly(audio[c].astype(np.float64), up, down).astype(np.float32)
                for c in range(audio.shape[0])
            ])
        return audio

    def generate_a2a(
        self,
        prompt: str,
        init_audio_path: str,
        init_noise_level: float,
        seconds_total: float,
        seed: int,
        steps: int,
        progress_cb: Callable[[float], None],
    ) -> np.ndarray:
        """Audio-to-audio: regenerate the input file in the style of `prompt`.

        Pipeline (mirrors sa3_mlx.py --init-audio):
            1. Load input audio (any format) → resample to 44100 stereo
            2. Patch + SAME-encode to latents
            3. Encode text → cross_attn + global_cond
            4. Build sigma schedule where σmax = init_noise_level
            5. Mix latents with noise:
                   noise = init_latents * (1 - σmax) + pure_noise * σmax
            6. DiT pingpong sample → latents
            7. Decode → audio (1, 2, T)
            8. Trim to seconds_total
        """
        if self.dit is None or self.enc is None:
            raise RuntimeError("No model loaded")
        if not (0.01 < init_noise_level <= 1.5):
            raise ValueError(
                f"init_noise_level must be in (0.01, 1.5]; got {init_noise_level}"
            )
        import mlx.core as mx

        if seconds_total <= 0.0:
            seconds_total = 1.0
        steps = max(1, int(steps))

        # 0. Make sure the right SAME encoder is resident
        progress_cb(0.01)
        self._ensure_encoder_for_a2a(self.current_model_id)

        # 1. Read input audio
        progress_cb(0.03)
        audio_in = self._read_wav_to_44100_stereo(init_audio_path)
        # Pad/trim to seconds_total, ALIGNED to patch_size (256).
        # The SAME encoder reshapes audio to (B, C, L, 256) where
        # L = T // 256; if T is not a multiple of 256 the reshape
        # fails with "cannot reshape array of size N into shape
        # (1, C, L, 256)".  Round UP so we never silently drop audio
        # (we may add up to 255 zero samples).
        patch_size = 256
        target_samples = int(round(seconds_total * SAMPLE_RATE))
        target_samples = ((target_samples + patch_size - 1)
                          // patch_size) * patch_size
        if audio_in.shape[-1] >= target_samples:
            audio_in = audio_in[:, :target_samples]
        else:
            pad = target_samples - audio_in.shape[-1]
            audio_in = np.pad(audio_in, ((0, 0), (0, pad)), mode="constant")
        # Defensive: ensure exact multiple of 256 (in case float
        # rounding produced something like 350,948.0001).
        T = audio_in.shape[-1]
        T_aligned = (T // patch_size) * patch_size
        if T_aligned != T:
            audio_in = audio_in[:, :T_aligned]
        audio_in = audio_in[None, ...]  # (1, 2, T)
        progress_cb(0.07)

        # 2. Patch + encode → init latents
        T_lat = max(1, math.ceil(seconds_total * SAMPLE_RATE / SAMPLES_PER_LATENT))
        if self.same_encoder_kind == "same-s" and T_lat % 2 != 0:
            T_lat += 1

        patch_size = 256
        B, C, T = audio_in.shape
        L = T // patch_size
        # rearrange("b c (l h) -> b (c h) l", h=patch_size)
        patches = audio_in.reshape(B, C, L, patch_size).transpose(0, 1, 3, 2)
        patches = patches.reshape(B, C * patch_size, L).astype(np.float32)
        # Sanity: same encoder requires T_audio_patches % pad_modulo == 0
        if patches.shape[-1] % self.same_encoder_pad_modulo != 0:
            # Pad to next multiple of pad_modulo
            extra = self.same_encoder_pad_modulo - (patches.shape[-1] % self.same_encoder_pad_modulo)
            patches = np.pad(patches, ((0, 0), (0, 0), (0, extra)), mode="constant")
        progress_cb(0.12)
        init_latents = self.same_encoder(mx.array(patches))
        mx.eval(init_latents)
        init_latents = init_latents.astype(self.dtype)
        progress_cb(0.18)

        # Free the encoder if the user is doing a single A2A — we can re-load
        # on the next A2A request (encoder is small relative to DiT).
        # Actually, we keep it loaded — the user is more likely to do
        # another A2A than not.  _unload_dit_locked() handles cleanup on
        # model switch.

        # 3. Encode text → cross_attn / global_cond
        embeds, mask = self.enc.encode([prompt], max_len=256)
        mx.eval(embeds, mask)
        embeds = embeds.astype(self.dtype)
        embeds_padded = apply_prompt_padding(embeds, mask, self.cond_padding)
        seconds_embed = self.cond_secs(seconds_total).astype(self.dtype)
        cross_attn = mx.concatenate([embeds_padded, seconds_embed], axis=1)
        global_cond = seconds_embed[:, 0, :]
        mx.eval(cross_attn, global_cond)
        progress_cb(0.22)

        # 4. Sigma schedule — σmax is the user's init_noise_level
        sigmas = build_pingpong_schedule(steps, sigma_max=init_noise_level,
                                         use_logsnr_shift=True)

        # 5. Mix init with noise (rf_denoiser formula: noise = init*(1-σ) + pure*σ)
        key = mx.random.key(seed)
        pure_noise = mx.random.normal((1, 256, T_lat), dtype=self.dtype, key=key)
        noise = init_latents * (1.0 - init_noise_level) + pure_noise * init_noise_level
        mx.eval(noise)
        progress_cb(0.25)

        # 6. Sample
        def model_fn(x, t):
            return self.dit(x, t, cross_attn, global_cond, local_add_cond=None)

        def on_step(i, total):
            progress_cb(0.25 + 0.60 * (i / max(total, 1)))

        t_sample_start = time.time()
        latents = sample_flow_pingpong(
            model_fn, noise, sigmas, seed=seed + 1,
            paste_back=None, on_step=on_step,
        )
        mx.eval(latents)
        progress_cb(0.85)
        log.info("DiT A2A sample: %.1f s for %d steps (%.1f s/step, σmax=%.2f)",
                 time.time() - t_sample_start, steps,
                 (time.time() - t_sample_start) / max(steps, 1),
                 init_noise_level)

        # 7. Decode (same as T2A)
        latents_fp32 = latents.astype(mx.float32)
        kernel = self.dec_chunk + 2 * self.dec_ovl
        if T_lat > kernel:
            patches_out = self.dec_chunk_fn(self.decoder, latents_fp32,
                                            self.dec_chunk, self.dec_ovl)
        elif T_lat % 2 == 0:
            patches_out = self.decoder(latents_fp32)
        else:
            patches_out = self.dec_chunk_fn(self.decoder, latents_fp32, 2, 2)
        mx.eval(patches_out)
        progress_cb(0.95)

        # 8. Unpatch → (1, 2, T)
        audio = patched_decode(patches_out, patch_size=256, channels=2)
        mx.eval(audio)
        audio_np = np.array(audio.astype(mx.float32))[0]

        # 9. Trim to exactly seconds_total (must match the aligned
        #    target_samples from step 1 so input and output have the
        #    same length in samples).
        target_samples = int(round(seconds_total * SAMPLE_RATE))
        target_samples = ((target_samples + 256 - 1) // 256) * 256
        if audio_np.shape[-1] > target_samples:
            audio_np = audio_np[..., :target_samples]
        progress_cb(1.0)
        return audio_np


# ─── Generation worker ──────────────────────────────────────────────────
class GenerationWorker:
    """
    Owns a single InferenceBackend instance and a single background thread.
    Jobs are submitted through ``enqueue`` and processed in FIFO order.
    """

    def __init__(self, backend: InferenceBackend, output_dir: Path):
        self.backend = backend
        self.output_dir = output_dir
        self.output_dir.mkdir(parents=True, exist_ok=True)
        self.jobs: Dict[str, Job] = {}
        self.jobs_lock = threading.Lock()
        self.queue: asyncio.Queue = asyncio.Queue()
        self._thread: Optional[threading.Thread] = None
        self._stop = threading.Event()

    def start(self):
        self._thread = threading.Thread(target=self._run, name="DAWalka-Worker", daemon=True)
        self._thread.start()

    def stop(self):
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=3.0)

    def submit(self, job: Job):
        with self.jobs_lock:
            self.jobs[job.id] = job
        try:
            loop = asyncio.get_event_loop()
        except RuntimeError:
            loop = asyncio.new_event_loop()
            asyncio.set_event_loop(loop)
        loop.call_soon_threadsafe(self.queue.put_nowait, job.id)
        return job.id

    def get(self, job_id: str) -> Optional[Job]:
        with self.jobs_lock:
            return self.jobs.get(job_id)

    def cancel(self, job_id: str):
        with self.jobs_lock:
            j = self.jobs.get(job_id)
            if j is not None and j.status in ("queued", "running"):
                j.status = "cancelled"
                j.finished_at = time.time()
                j.error = "Cancelled by user"

    def _run(self):
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        loop.run_until_complete(self._main())

    async def _main(self):
        loop = asyncio.get_running_loop()
        # Dedicated single-thread executor for all MLX work.  MLX ties
        # its Metal stream to the *thread* that first touches a GPU
        # array — see https://github.com/ml-explore/mlx-lm/issues/1181.
        # If model loading and inference run on different threads (e.g.
        # the default ThreadPoolExecutor reuses different workers),
        # inference fails with `There is no Stream(gpu, 0) in current
        # thread.`  Pinning both load() and generate() to ONE thread
        # eliminates the entire class of bugs.
        import concurrent.futures
        self._mlx_executor = concurrent.futures.ThreadPoolExecutor(
            max_workers=1, thread_name_prefix="DAWalka-MLX"
        )
        # Pre-warm the stream on the MLX thread so the first inference
        # doesn't race with stream creation.
        await loop.run_in_executor(self._mlx_executor, _init_mlx_stream)
        while not self._stop.is_set():
            try:
                job_id = await asyncio.wait_for(self.queue.get(), timeout=0.5)
            except asyncio.TimeoutError:
                continue
            await self._process(job_id)

    async def _process(self, job_id: str):
        with self.jobs_lock:
            job = self.jobs.get(job_id)
            if job is None or job.status == "cancelled":
                return
            job.status = "running"
            job.started_at = time.time()

        request = job.request
        kind = getattr(job, "kind", "t2a")
        try:
            model_id = request.get("model", "sa3-sm-music")
            if model_id not in MODEL_RUNTIME:
                raise ValueError(f"Unknown model '{model_id}'; available: {list(MODEL_RUNTIME)}")
            # Load and generate on the SAME single-thread executor so
            # the MLX stream persists across both calls.
            loop = asyncio.get_event_loop()
            ok = await loop.run_in_executor(
                self._mlx_executor, self.backend.load, model_id
            )
            if not ok:
                raise RuntimeError(f"Failed to load model '{model_id}'")

            def progress_cb(p: float):
                with self.jobs_lock:
                    job.progress = max(0.0, min(1.0, p))

            steps = int(request.get("steps", 8))
            seed = int(request.get("seed", -1))
            if seed < 0:
                seed = int(time.time()) & 0xFFFF

            if kind == "a2a":
                # ─── A2A: verbatim prompt, init from input file ─────────
                # The user-facing prompt is sent as-is — no "BPM/key/
                # seamless" suffix.  Output length matches the input.
                prompt = (request.get("prompt", "") or "").replace("\n", " ").strip()
                init_audio_path = request.get("init_audio", "")
                if not init_audio_path or not Path(init_audio_path).exists():
                    raise FileNotFoundError(
                        f"Input audio not found: {init_audio_path!r}"
                    )
                init_noise_level = float(request.get("init_noise_level", 0.7))
                # duration comes from the input file (caller measured it)
                duration = float(request.get("duration", 0.0))
                if duration <= 0.0:
                    raise ValueError("A2A duration must be > 0 (input file is silent or unreadable)")

                audio = await loop.run_in_executor(
                    self._mlx_executor,
                    lambda: self.backend.generate_a2a(
                        prompt, init_audio_path, init_noise_level,
                        duration, seed, steps, progress_cb,
                    ),
                )
            else:
                # ─── T2A: standard prompt, no init audio ──────────────
                prompt = PromptBuilder.build(
                    request.get("prompt", ""),
                    int(request.get("bpm", 120)),
                    request.get("key_root", 0),
                    request.get("key_mode", "major"),
                    bool(request.get("seamless_loop", True)),
                    include_key=bool(request.get("include_key", True)),
                    include_bpm=bool(request.get("include_bpm", True)),
                )
                duration = float(request.get("duration", 8.0))

                audio = await loop.run_in_executor(
                    self._mlx_executor,
                    lambda: self.backend.generate(prompt, duration, seed, steps, progress_cb),
                )

            # Always stereo at 44.1 kHz (model output rate)
            if audio.ndim == 1:
                audio = np.stack([audio, audio])
            if audio.shape[0] == 1:
                audio = np.concatenate([audio, audio], axis=0)
            audio = audio[:2]

            # Per-job output dir (A2A uses this; T2A leaves it empty and
            # falls back to the worker's default).
            job_out_dir = Path(job.output_dir) if job.output_dir else self.output_dir
            job_out_dir.mkdir(parents=True, exist_ok=True)

            # A2A can suggest a meaningful filename like
            # "loop_bass__a2a__15-30-45.wav" — falls back to {job.id}.wav
            # for T2A and for A2A jobs that don't supply one.
            suggested = request.get("output_filename", "") if kind == "a2a" else ""
            # Sanitize: only allow alnum, dash, underscore, dot; cap length
            safe = "".join(c for c in suggested if c.isalnum() or c in "-_.")[:120]
            if not safe or not safe.lower().endswith(".wav"):
                safe = f"{job.id}.wav"
            out_path = job_out_dir / safe
            # If a file with the same name already exists, suffix it
            # with a counter so we never silently overwrite history.
            if out_path.exists():
                stem, dot, ext = safe.rpartition(".")
                i = 2
                while True:
                    candidate = job_out_dir / f"{stem}_{i}.{ext}" if dot else job_out_dir / f"{safe}_{i}"
                    if not candidate.exists():
                        out_path = candidate
                        break
                    i += 1
                    if i > 999:
                        # Last-resort fallback
                        out_path = job_out_dir / f"{job.id}.wav"
                        break

            # The MLX model always generates at 44100 Hz (hard-coded in
            # the vendored sa3_mlx package).  If the user picked 48 kHz
            # in the plugin, we polyphase-resample the audio so the WAV
            # actually plays at the right speed/pitch and the loop
            # points stay on the bar grid.  Ratio 48000/44100 reduces to
            # 160/147 — using integer resample_poly keeps length exact.
            if job.sample_rate != SAMPLE_RATE:
                from scipy.signal import resample_poly
                # Resample each channel with the same filter for phase
                # alignment; the model's internal bar-aligned loop
                # points stay at the same fractional position after
                # resampling, so looped bars still hit the grid.
                resampled = np.stack([
                    resample_poly(audio[ch].astype(np.float64),
                                  job.sample_rate, SAMPLE_RATE).astype(np.float32)
                    for ch in range(audio.shape[0])
                ])
                audio = resampled

            sf.write(str(out_path), audio.T, job.sample_rate, subtype="FLOAT")

            with self.jobs_lock:
                job.status = "done"
                job.audio_path = str(out_path)
                job.duration = duration
                job.sample_rate = job.sample_rate
                job.channels = 2
                job.model = model_id
                job.seed = seed
                job.generation_time_ms = (time.time() - job.started_at) * 1000.0
                job.finished_at = time.time()
                job.progress = 1.0

        except Exception as e:
            log.exception("Job %s failed", job_id)
            with self.jobs_lock:
                job.status = "error"
                job.error = str(e)
                job.finished_at = time.time()


# ─── Prompt builder ──────────────────────────────────────────────────────
class PromptBuilder:
    @staticmethod
    def build(prompt: str, bpm: int, root: int, mode: str, seamless: bool,
              include_key: bool = True, include_bpm: bool = True) -> str:
        names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
        parts = []
        clean = (prompt or "").replace("\n", " ").strip()
        if clean:
            parts.append(clean)
        # BPM / KEY are the two tags that push the model toward
        # tonal, rhythmic output.  When the user is using a music
        # model for sound effects or untextured sounds, they can
        # opt out of either or both via the editor's "Include KEY /
        # BPM in prompt" toggles.  bpm / root / mode are still
        # received so the duration slider and host-tempo sync keep
        # working — only the prompt-side annotation is suppressed.
        if include_bpm:
            parts.append(f"{int(bpm)} BPM")
        if include_key:
            parts.append(f"{names[int(root) % 12]} {mode.capitalize()}")
        if seamless:
            parts.append("seamless loop")
        parts.append("high quality recording")
        parts.append("professional mix")
        return ", ".join(parts)


# ─── HTTP server ────────────────────────────────────────────────────────
class HttpServer:
    def __init__(self, models_dir: Path, output_dir: Path, host: str, port: int):
        self.models_dir = models_dir
        self.output_dir = output_dir
        self.host = host
        self.port = port

        self.backend = InferenceBackend(models_dir)
        self.worker = GenerationWorker(self.backend, output_dir)
        self.app = web.Application()
        self._add_routes()

    def _add_routes(self):
        r = self.app.router
        r.add_get ("/health",             self.handle_health)
        r.add_get ("/models",             self.handle_models)
        r.add_post("/load/{model_id}",    self.handle_load)
        r.add_post("/generate",           self.handle_generate)
        r.add_post("/a2a",                self.handle_a2a)
        r.add_get ("/job/{job_id}",       self.handle_job)
        r.add_post("/cancel",             self.handle_cancel)
        r.add_post("/shutdown",           self.handle_shutdown)

    # ── handlers ─────────────────────────────────────────────────────────
    async def handle_health(self, _):
        return web.json_response(
            {
                "ok": True,
                "backend": self.backend.backend_name,
                "model": self.backend.current_model_id,
                "platform": "apple-silicon" if sys.platform == "darwin" else "other",
            }
        )

    async def handle_models(self, _):
        out = []
        for entry in CATALOGUE:
            installed = all((self.models_dir / f).exists() for f in entry["files"])
            out.append(
                {
                    **entry,
                    "installed": installed,
                    "path": str(self.models_dir),
                }
            )
        return web.json_response({"models": out})

    async def handle_load(self, req: web.Request):
        model_id = req.match_info["model_id"]
        # Use the dedicated MLX executor (same thread as inference) so
        # the loaded model shares one stream with generate().  See the
        # explanation in _main() above.
        executor = self.worker._mlx_executor
        ok = await asyncio.get_event_loop().run_in_executor(
            executor, self.backend.load, model_id
        )
        return web.json_response({"ok": ok, "model": self.backend.current_model_id})

    async def handle_generate(self, req: web.Request):
        try:
            payload = await req.json()
        except Exception:
            return web.json_response({"ok": False, "error": "invalid JSON"}, status=400)

        job = Job(id=str(uuid.uuid4()), request=payload, kind="t2a")
        self.worker.submit(job)
        return web.json_response({"ok": True, "job_id": job.id, "status": "queued"})

    async def handle_a2a(self, req: web.Request):
        """Audio-to-audio: regenerate `init_audio` in the style of `prompt`.

        Expected JSON body:
            {
                "prompt":          str,    # text prompt (verbatim, no suffix)
                "init_audio":      str,    # absolute path to the input WAV
                "init_noise_level": float, # 0.0..1.0 (σmax); 0.7 is typical
                "duration":        float,  # seconds; matches the input file
                "model":           str,    # "sa3-sm-music" | "sa3-sm-sfx" | "sa3-medium"
                "sample_rate":     int,    # 44100 or 48000 — written into the WAV
                "steps":           int,    # default 8
                "seed":            int,    # -1 = random
            }
        """
        try:
            payload = await req.json()
        except Exception:
            return web.json_response({"ok": False, "error": "invalid JSON"}, status=400)

        # Minimal validation — let the worker raise richer errors
        if not payload.get("init_audio"):
            return web.json_response(
                {"ok": False, "error": "init_audio path is required"},
                status=400,
            )
        if not Path(payload["init_audio"]).exists():
            return web.json_response(
                {"ok": False, "error": f"init_audio not found: {payload['init_audio']}"},
                status=400,
            )
        if "duration" not in payload or float(payload.get("duration", 0)) <= 0.0:
            return web.json_response(
                {"ok": False, "error": "duration must be > 0 (caller measures the input file)"},
                status=400,
            )
        inl = float(payload.get("init_noise_level", 0.7))
        if not (0.01 <= inl <= 1.5):
            return web.json_response(
                {"ok": False, "error": f"init_noise_level must be in [0.01, 1.5]; got {inl}"},
                status=400,
            )

        job = Job(id=str(uuid.uuid4()), request=payload, kind="a2a")
        self.worker.submit(job)
        return web.json_response({"ok": True, "job_id": job.id, "status": "queued"})

    async def handle_job(self, req: web.Request):
        job_id = req.match_info["job_id"]
        job = self.worker.get(job_id)
        if job is None:
            return web.json_response({"ok": False, "error": "unknown job"}, status=404)
        return web.json_response(
            {
                "ok": job.status == "done",
                "status": job.status,
                "progress": job.progress,
                "error": job.error,
                "audio": job.audio_path,
                "duration": job.duration,
                "sample_rate": job.sample_rate,
                "channels": job.channels,
                "model": job.model,
                "seed": job.seed,
                "generation_time_ms": job.generation_time_ms,
            }
        )

    async def handle_cancel(self, req: web.Request):
        try:
            payload = await req.json()
        except Exception:
            return web.json_response({"ok": False, "error": "invalid JSON"}, status=400)
        job_id = payload if isinstance(payload, str) else payload.get("job_id", "")
        self.worker.cancel(job_id)
        return web.json_response({"ok": True})

    async def handle_shutdown(self, _):
        asyncio.get_event_loop().call_later(0.5, lambda: os._exit(0))
        return web.json_response({"ok": True})

    # ── lifecycle ────────────────────────────────────────────────────────
    def start(self):
        self.backend.initialize()
        self.worker.start()
        log.info("DAWalka backend listening on http://%s:%d", self.host, self.port)
        web.run_app(self.app, host=self.host, port=self.port, access_log=None, print=None)

    def stop(self):
        self.worker.stop()


# ─── Entry point ─────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(description="DAWalka — Stable Audio 3 MLX backend")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=47823)
    parser.add_argument(
        "--models",
        default=str(Path.home() / "Library/Application Support/DAWalka/models"),
    )
    parser.add_argument(
        "--outputs",
        default=str(Path.home() / "Documents/DAWalka/T2A"),
    )
    parser.add_argument("--log-file", default=None)
    parser.add_argument("--verbose", "-v", action="count", default=0)
    args = parser.parse_args()

    handlers: list = [logging.StreamHandler()]
    if args.log_file:
        handlers.append(logging.FileHandler(args.log_file, mode="a"))

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s [%(levelname).1s] %(name)s | %(message)s",
        handlers=handlers,
    )
    log.info("DAWalka backend starting")
    log.info("models dir: %s", args.models)
    log.info("output dir: %s", args.outputs)

    server = HttpServer(
        Path(args.models),
        Path(args.outputs),
        args.host,
        args.port,
    )
    server.start()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        log.info("Shutting down")
    except Exception:
        traceback.print_exc()
        sys.exit(1)
