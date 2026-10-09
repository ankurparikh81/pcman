#!/usr/bin/env python3
import json
import os
import subprocess

def main():
    cwd = os.path.abspath(os.getcwd())
    src_files = [
        "src/main.cpp",
        "src/cga_renderer.cpp",
        "src/pc_speaker.cpp",
        "src/maze.cpp",
        "src/entities.cpp",
        "src/game.cpp",
        "tests/test_pcman.cpp",
    ]

    try:
        lines = subprocess.check_output(["make", "-n", "-B"]).decode().splitlines()
    except Exception:
        lines = []

    compile_prefix = ""
    for line in lines:
        if " -c " in line:
            parts = line.split()
            idx = parts.index("-c")
            compile_prefix = " ".join(parts[:idx])
            break

    if not compile_prefix:
        compile_prefix = "clang++ -std=c++20 -Wall -Wextra -O2 -Isrc -I/opt/homebrew/include -I/usr/local/include"

    entries = []
    for src in src_files:
        obj = src.rsplit(".", 1)[0] + ".o"
        cmd = f"{compile_prefix} -c {src} -o {obj}"
        entries.append({
            "directory": cwd,
            "command": cmd,
            "file": os.path.join(cwd, src),
            "output": os.path.join(cwd, obj)
        })

    out_path = os.path.join(cwd, "compile_commands.json")
    with open(out_path, "w") as f:
        json.dump(entries, f, indent=2)

    print(f"Generated {out_path} with {len(entries)} entries.")

if __name__ == "__main__":
    main()
