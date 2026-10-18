"""Prepare the targeting source overlay used by Windows MSBuild projects.

The core reconstruction tree is intentionally immutable from the targeting
layer's point of view.  CMake already builds from ``overlay.py`` output, but
the Visual Studio projects historically compiled the reconstructed sources
directly.  That left generated targeting/item helper definitions out of the
Windows link.

Multiple core projects may build in parallel from Hengband.sln.  They share a
single overlay tree, so this wrapper serializes regeneration and skips work
when neither the reconstructed source metadata nor the targeting generators
changed.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time


SOURCE_DIRS = ("src", "tangband/src", "gensoband/src", "frox/src", "silq/src",
               "umoria/patched-src", "rogue36/patched-src", "rogue54/patched-src", "rogue2/patched-src",
               "toband/src")
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hpp", ".inl", ".inc"}


def _fingerprint(sources: Path, tool_dir: Path) -> str:
    digest = hashlib.sha256()

    prepared = sources / ".prepared.json"
    if prepared.is_file():
        digest.update(b"prepared\0")
        digest.update(prepared.read_bytes())

    # Metadata is sufficient here: the reconstruction tree is generated data,
    # while size + nanosecond mtime also catches an intentional local edit.
    for relative_root in SOURCE_DIRS:
        root = sources / relative_root
        if not root.is_dir():
            continue
        for path in sorted(root.rglob("*")):
            if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
                continue
            stat = path.stat()
            rel = path.relative_to(sources).as_posix().encode("utf-8")
            digest.update(rel)
            digest.update(b"\0")
            digest.update(str(stat.st_size).encode("ascii"))
            digest.update(b":")
            digest.update(str(stat.st_mtime_ns).encode("ascii"))
            digest.update(b"\n")

    for path in sorted(tool_dir.glob("*.py")):
        digest.update(path.name.encode("utf-8"))
        digest.update(b"\0")
        digest.update(path.read_bytes())
        digest.update(b"\n")

    return digest.hexdigest()


class _FileLock:
    def __init__(self, path: Path):
        self.path = path
        self.handle = None

    def __enter__(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.handle = self.path.open("a+b")
        self.handle.seek(0, os.SEEK_END)
        if self.handle.tell() == 0:
            self.handle.write(b"0")
            self.handle.flush()
        self.handle.seek(0)
        if os.name == "nt":
            import msvcrt
            while True:
                try:
                    msvcrt.locking(self.handle.fileno(), msvcrt.LK_NBLCK, 1)
                    break
                except OSError:
                    time.sleep(0.1)
        else:
            import fcntl

            fcntl.flock(self.handle.fileno(), fcntl.LOCK_EX)
        return self

    def __exit__(self, exc_type, exc, tb):
        assert self.handle is not None
        self.handle.seek(0)
        if os.name == "nt":
            import msvcrt

            msvcrt.locking(self.handle.fileno(), msvcrt.LK_UNLCK, 1)
        else:
            import fcntl

            fcntl.flock(self.handle.fileno(), fcntl.LOCK_UN)
        self.handle.close()
        self.handle = None


def prepare(sources: Path, output: Path) -> bool:
    sources = sources.resolve()
    output = output.resolve()
    tool_dir = Path(__file__).resolve().parent
    overlay = tool_dir / "overlay.py"
    if not (sources / ".prepared.json").is_file():
        raise SystemExit(f"Prepared core source marker is missing: {sources / '.prepared.json'}")

    lock_path = output.parent / f".{output.name}.lock"
    stamp_path = output / ".windows-overlay-stamp.json"
    with _FileLock(lock_path):
        fingerprint = _fingerprint(sources, tool_dir)
        if stamp_path.is_file() and (output / "manifest.json").is_file():
            try:
                stamp = json.loads(stamp_path.read_text(encoding="utf-8"))
            except (OSError, ValueError):
                stamp = {}
            if stamp.get("fingerprint") == fingerprint:
                print(f"Windows targeting overlay is up to date: {output}")
                return False

        output.mkdir(parents=True, exist_ok=True)
        subprocess.run(
            [
                sys.executable,
                str(overlay),
                "--sources",
                str(sources),
                "--output",
                str(output),
            ],
            check=True,
            stdout=subprocess.DEVNULL,
        )
        stamp = {
            "version": 1,
            "fingerprint": _fingerprint(sources, tool_dir),
            "sources": str(sources),
        }
        temp = stamp_path.with_suffix(".tmp")
        temp.write_text(json.dumps(stamp, indent=2) + "\n", encoding="utf-8")
        os.replace(temp, stamp_path)
        print(f"Prepared Windows targeting overlay: {output}")
        return True


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sources", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    prepare(args.sources, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
