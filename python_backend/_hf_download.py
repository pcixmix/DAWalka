#!/usr/bin/env python3
# =============================================================================
#  _hf_download.py - stream a file from HuggingFace with a progress bar
# =============================================================================
#
#  This replaces the inline `python -c "..."` block in installer/install.sh
#  that previously used `hf_hub_download` + `tail -n 1`.  Two bugs in that
#  approach:
#
#    1. hf_hub_download uses tqdm.auto internally.  In DAWalka.app stderr
#       is a pipe (NSPipe) not a tty, so tqdm goes silent.  The user sees
#       "скачиваю..." and then nothing for minutes while a 1.6 GB file
#       downloads.  Looks like a hang.
#
#    2. `2>&1 | tail -n 1` is racy - the last line is whatever
#       huggingface_hub wrote to stderr last (often a warning or fetch
#       progress line), not the `OK`/`ERR` line we print.  The script
#       then marks the file as failed even though it actually downloaded.
#
#  This script fixes both:
#    - Streams one progress line per 0.5 s to stderr in pipe-safe format
#      (no carriage returns, no cursor moves - just `\\n`-terminated lines
#      that show up nicely in NSTextView)
#    - Writes the final status to a file (path in $DAWALKA_STATUS_FILE)
#      so the bash wrapper reads it deterministically
#
#  Bonus: resume support via HTTP Range, total size from HEAD request.
# =============================================================================
import os
import sys
import time
import requests
from huggingface_hub import hf_hub_url


def _fmt(n: float) -> str:
    """Format bytes as human-readable size."""
    for unit in ("B", "KB", "MB", "GB", "TB"):
        if n < 1024:
            return f"{n:.1f}{unit}"
        n /= 1024
    return f"{n:.1f}PB"


def _err(msg: str) -> int:
    """Print error to stderr and write ERR to status file.  Returns 1."""
    print(f"  [ERROR] {msg[:400]}", file=sys.stderr, flush=True)
    sf = os.environ.get("DAWALKA_STATUS_FILE", "")
    if sf:
        try:
            with open(sf, "w") as f:
                f.write(f"ERR {msg[:400]}")
        except OSError:
            pass
    return 1


def download(repo_id: str, filename: str, local_dir: str,
             hf_token: str = "") -> int:
    base = os.path.basename(filename)
    out_path = os.path.join(local_dir, base)
    part_path = out_path + ".part"

    # If we already have a complete file, nothing to do.
    if os.path.exists(out_path) and os.path.getsize(out_path) > 0:
        size = os.path.getsize(out_path)
        print(f"  [{base}] already present ({_fmt(size)})",
              file=sys.stderr, flush=True)
        sf = os.environ.get("DAWALKA_STATUS_FILE", "")
        if sf:
            with open(sf, "w") as f:
                f.write(f"OK {size}")
        return 0

    os.makedirs(local_dir, exist_ok=True)

    # Resolve the actual download URL (handles redirects internally).
    try:
        url = hf_hub_url(repo_id=repo_id, filename=filename)
    except Exception as e:
        return _err(f"hf_hub_url failed: {e}")

    headers = {}
    if hf_token:
        headers["Authorization"] = f"Bearer {hf_token}"

    # HEAD to learn the total size and confirm reachability.
    try:
        head = requests.head(url, headers=headers, allow_redirects=True,
                             timeout=30)
        head.raise_for_status()
        total = int(head.headers.get("content-length", 0))
    except Exception as e:
        return _err(f"HEAD failed: {e}")

    # Resume from a previous partial download if we have one.
    pos = os.path.getsize(part_path) if os.path.exists(part_path) else 0
    if pos > 0 and pos < total:
        headers["Range"] = f"bytes={pos}-"
        print(f"  [{base}] resuming from {_fmt(pos)} of {_fmt(total)}",
              file=sys.stderr, flush=True)
    elif pos >= total:
        # Part file is bigger than expected (server changed?) - redo
        os.remove(part_path)
        pos = 0

    # Stream the file.  Use a 60s idle timeout (per chunk), 30 min total.
    t0 = time.time()
    last_print = 0.0
    downloaded = pos
    try:
        with requests.get(url, headers=headers, stream=True,
                          allow_redirects=True, timeout=60) as r:
            r.raise_for_status()
            # 206 = partial content (resume).  Anything else = full file.
            mode = "ab" if r.status_code == 206 else "wb"
            if mode == "wb":
                pos = 0
                downloaded = 0

            with open(part_path, mode) as f:
                for chunk in r.iter_content(chunk_size=1024 * 1024):
                    if not chunk:
                        continue
                    f.write(chunk)
                    downloaded += len(chunk)

                    now = time.time()
                    if now - last_print >= 0.5:
                        pct = (downloaded / total * 100) if total else 0
                        elapsed = now - t0
                        speed = (downloaded - pos) / elapsed if elapsed > 0 else 0
                        eta = (total - downloaded) / speed if speed > 0 else 0
                        eta_s = f" ETA {int(eta)}s" if eta < 9999 else ""
                        print(
                            f"  [{base}] {pct:5.1f}%  "
                            f"{_fmt(downloaded)}/{_fmt(total)}  "
                            f"({_fmt(speed)}/s{eta_s})",
                            file=sys.stderr, flush=True,
                        )
                        last_print = now

                    # 30 min total safety cap
                    if now - t0 > 1800:
                        return _err("download exceeded 30 min wall-clock cap")
    except requests.exceptions.Timeout as e:
        return _err(f"timeout: {e}")
    except requests.exceptions.HTTPError as e:
        return _err(f"HTTP {e.response.status_code}: {e.response.reason}")
    except requests.exceptions.RequestException as e:
        return _err(f"network: {e}")
    except Exception as e:
        return _err(f"unexpected: {type(e).__name__}: {e}")

    # Final line - 100% summary
    elapsed = time.time() - t0
    speed = (downloaded - pos) / elapsed if elapsed > 0 else 0
    print(
        f"  [{base}] 100.0%  {_fmt(downloaded)} in {elapsed:.1f}s "
        f"({_fmt(speed)}/s)",
        file=sys.stderr, flush=True,
    )

    # Atomic move .part -> final
    os.rename(part_path, out_path)

    sf = os.environ.get("DAWALKA_STATUS_FILE", "")
    if sf:
        with open(sf, "w") as f:
            f.write(f"OK {downloaded}")
    return 0


if __name__ == "__main__":
    import argparse
    p = argparse.ArgumentParser()
    p.add_argument("--repo", required=True, help="HF repo id, e.g. owner/name")
    p.add_argument("--file", required=True, help="filename inside the repo")
    p.add_argument("--out-dir", required=True, help="destination directory")
    p.add_argument("--token", default="", help="optional HF token")
    args = p.parse_args()
    sys.exit(download(args.repo, args.file, args.out_dir, args.token))
