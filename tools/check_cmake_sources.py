#!/usr/bin/env python3
"""Checks the CMake source lists against the files on disk. Standard library only.

Library: every src/**/*.cpp must be named in CMakeLists.txt, and every name there must exist.
Tests:   every tests/**/*.cpp must be named in tests/CMakeLists.txt (paths relative to tests/), and every name there
         must exist. A test file that no target builds is reported as a failure, because it would never run.

Exit status 0 when both checks pass, 1 otherwise.
"""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8") as f:
        return f.read()


def disk_files(base, pattern):
    return {os.path.relpath(p, base) for p in glob.glob(os.path.join(base, pattern), recursive=True)}


def check_library():
    listed = set(re.findall(r"\b(src/[\w/.\-]+\.cpp)\b", read("CMakeLists.txt")))
    on_disk = disk_files(ROOT, "src/**/*.cpp")
    on_disk = {p for p in on_disk}
    bad = 0
    for p in sorted(listed - on_disk):
        print(f"FAIL library: CMakeLists.txt names {p}, which does not exist")
        bad += 1
    for p in sorted(on_disk - listed):
        print(f"FAIL library: {p} exists but no CMake target builds it")
        bad += 1
    print(f"library: {len(listed)} named, {len(on_disk)} on disk, {bad} problem(s)")
    return bad


def check_tests():
    listed = set(re.findall(r"([\w/.\-]+\.cpp)", read("tests/CMakeLists.txt")))
    on_disk = disk_files(os.path.join(ROOT, "tests"), "**/*.cpp")
    bad = 0
    for p in sorted(listed - on_disk):
        print(f"FAIL tests: tests/CMakeLists.txt names {p}, which does not exist under tests/")
        bad += 1
    for p in sorted(on_disk - listed):
        print(f"FAIL tests: tests/{p} is not built by any test target, so it never runs")
        bad += 1
    print(f"tests: {len(listed)} named, {len(on_disk)} on disk, {bad} problem(s)")
    return bad


def main():
    bad = check_library() + check_tests()
    print("OK" if bad == 0 else f"FAILED: {bad} problem(s)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
