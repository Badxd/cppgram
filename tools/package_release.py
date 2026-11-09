#!/usr/bin/env python3
"""Builds the source release archive. Standard library only.

    python3 tools/package_release.py --output dist/cppgram-0.1.0-source.zip

What goes in: every tracked-style source file under the repository root, except the excluded paths below.
What the archive contains: one top-level folder named cppgram-<VERSION>, the sources, and SHA256SUMS.txt for every file in
that folder (SHA256SUMS.txt itself is not listed, since it cannot record its own hash).

Reproducibility: entries are sorted, use a fixed timestamp (SOURCE_DATE_EPOCH, or the mtime of VERSION if unset), and
deflate at level 9. Two runs on the same tree produce the same entries. The zip's own SHA-256 is printed. It is not stored
in the archive, since a zip cannot record its own hash.

The script refuses to write an archive that lacks a required file or contains an excluded one.
"""
import argparse
import fnmatch
import hashlib
import os
import stat
import sys
import time
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

EXCLUDED_DIRS = {"build", ".git", "__pycache__", "dist", "doc", ".cache", ".vscode", ".idea", ".ccls-cache"}
EXCLUDED_DIR_PATTERNS = ["build-*"]
EXCLUDED_FILE_PATTERNS = [
    "*.session", "*.session-journal", "*.session-wal", "*.session-shm", "*.db", "*.db-*", "*.sqlite", "*.sqlite3",
    "*.log", "*.rss", "*.o", "*.obj", "*.a", "*.so", "*.so.*", "*.dylib", "*.dll", "*.exe", "*.tmp", "*~", "*.orig",
    "*.rej", "*.swp", "*.pyc", "*.zip", "*.tar", "*.tar.gz", "*.gz", "*.core", "*.profraw", "*.gcda", "*.gcno",
    "*.pem", "*.key", "*.p12", "*.pfx", "compile_commands.json", ".DS_Store", "Thumbs.db",
    ".env", ".env.*",
]
ALLOWED_FILES = {".env.example"}

REQUIRED = [
    "VERSION", "CHANGELOG.md", "RELEASE_NOTES.md", "README.md", "CMakeLists.txt", "Makefile", "LICENSE", "NOTICE",
    "FINAL_REPORT.md", "IMPLEMENTATION_STATUS.md", "PARITY_MATRIX.md", "KNOWN_LIMITATIONS.md", "VPS_TESTING_GUIDE.md",
    "TEST_RESULTS.md", "BUILD_RESULTS.md", "schema/SHA256SUMS.txt", "schema/README.md",
    "include/cppgram/client.h", "include/cppgram/mtproto/schema/api.h", "src/mtproto/schema/api_impl_0.cpp",
    "tests/CMakeLists.txt", "examples/echobot/src/main.cpp", "tools/check_schema.sh", "tools/package_release.py",
    "tools/verify_release.sh", ".github/workflows/ci.yml", "cmake/cppgramConfig.cmake.in",
]


def excluded_dir(name):
    return name in EXCLUDED_DIRS or any(fnmatch.fnmatch(name, p) for p in EXCLUDED_DIR_PATTERNS)


def excluded_file(name):
    if name in ALLOWED_FILES:
        return False
    return any(fnmatch.fnmatch(name, p) for p in EXCLUDED_FILE_PATTERNS)


def collect(root):
    files = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = sorted(d for d in dirnames if not excluded_dir(d))
        for name in sorted(filenames):
            full = os.path.join(dirpath, name)
            rel = os.path.relpath(full, root).replace(os.sep, "/")
            if rel == "SHA256SUMS.txt":  # generated into the archive; a stale copy in the tree is never packaged
                continue
            if excluded_file(name):
                continue
            if os.path.islink(full):
                continue
            files.append(rel)
    return sorted(files)


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", required=True, help="path of the zip to write")
    parser.add_argument("--root", default=ROOT, help="repository root (default: this checkout)")
    args = parser.parse_args()

    root = os.path.abspath(args.root)
    with open(os.path.join(root, "VERSION"), encoding="utf-8") as f:
        version = f.read().strip()
    top = f"cppgram-{version}"

    files = collect(root)
    present = set(files)
    missing = [r for r in REQUIRED if r not in present]
    if missing:
        print("refusing to package; required files are missing:", *missing, sep="\n  ", file=sys.stderr)
        return 1

    epoch = os.environ.get("SOURCE_DATE_EPOCH")
    stamp = int(epoch) if epoch else int(os.stat(os.path.join(root, "VERSION")).st_mtime)
    date_time = time.gmtime(stamp)[:6]

    sums = []
    for rel in files:
        sums.append(f"{sha256(os.path.join(root, rel))}  {rel}")
    sums_text = "\n".join(sums) + "\n"

    out_dir = os.path.dirname(os.path.abspath(args.output))
    os.makedirs(out_dir, exist_ok=True)
    tmp_path = args.output + ".tmp"
    entries = [(f"{top}/{rel}", os.path.join(root, rel)) for rel in files]
    with zipfile.ZipFile(tmp_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for arcname, src in entries:
            info = zipfile.ZipInfo(arcname, date_time=date_time)
            mode = os.stat(src).st_mode
            executable = bool(mode & stat.S_IXUSR)
            info.external_attr = ((0o755 if executable else 0o644) | stat.S_IFREG) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            with open(src, "rb") as f:
                zf.writestr(info, f.read(), compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
        info = zipfile.ZipInfo(f"{top}/SHA256SUMS.txt", date_time=date_time)
        info.external_attr = (0o644 | stat.S_IFREG) << 16
        zf.writestr(info, sums_text.encode("utf-8"), compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
    os.replace(tmp_path, args.output)

    with zipfile.ZipFile(args.output) as zf:
        leaked = []
        for n in zf.namelist():
            parts = n.split("/")[1:]
            if any(excluded_dir(part) for part in parts[:-1]):
                leaked.append(n)
            elif parts[-1] != "SHA256SUMS.txt" and excluded_file(parts[-1]):
                leaked.append(n)
    if leaked:
        os.remove(args.output)
        print("refusing: excluded paths reached the archive:", *leaked[:20], sep="\n  ", file=sys.stderr)
        return 1

    size = os.path.getsize(args.output)
    print(f"archive: {args.output}")
    print(f"top-level folder: {top}/")
    print(f"files: {len(files)} (plus SHA256SUMS.txt)")
    print(f"bytes: {size}")
    print(f"sha256: {sha256(args.output)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
