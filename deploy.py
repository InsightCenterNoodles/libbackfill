#!/usr/bin/env python3
"""
Default install prefix: ~/.local
Optional global install: /opt (not available on macOS)

Requires:
  - cmake
  - clang/clang++
"""

from __future__ import annotations
import argparse
import os
import platform
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

def which_or_die(bin_name: str) -> str:
    path = shutil.which(bin_name)
    if not path:
        die(f"Required tool not found: {bin_name}. Please install it and re-run.")
    return path

def run(cmd: list[str], *, cwd: Path | None = None, env: dict | None = None, verbose: bool = False):
    if verbose:
        print(f"$ {' '.join(cmd)}", flush=True)
    try:
        subprocess.run(cmd, cwd=str(cwd) if cwd else None, env=env, check=True)
    except subprocess.CalledProcessError as e:
        die(f"Command failed (exit {e.returncode}): {' '.join(cmd)}")

def die(msg: str, code: int = 2):
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(code)

def is_writable(path: Path) -> bool:
    # If the path doesn't exist yet, check if its parent is writable
    check_path = path if path.exists() else path.parent
    return os.access(str(check_path), os.W_OK)

def detect_clang(verbose: bool = False) -> tuple[str, str]:
    clang = shutil.which("clang")
    clangxx = shutil.which("clang++")
    if not clang or not clangxx:
        die("clang/clang++ are required but were not found in PATH.")
    # quick sanity: verify versions print without error
    try:
        out_c = subprocess.check_output([clang, "--version"], text=True)
        out_cxx = subprocess.check_output([clangxx, "--version"], text=True)
    except Exception:
        die("Failed to execute clang/clang++ --version.")
    if verbose:
        print(out_c.splitlines()[0])
        print(out_cxx.splitlines()[0])
    return clang, clangxx

def make_env_with_clang(base_env: dict, clang: str, clangxx: str) -> dict:
    env = dict(base_env)
    # Respect existing CC/CXX if they already point to clang, otherwise force clang.
    if env.get("CC", "").find("clang") == -1:
        env["CC"] = clang
    if env.get("CXX", "").find("clang") == -1:
        env["CXX"] = clangxx
    return env

def parse_args() -> argparse.Namespace:
    home_local = str(Path.home() / ".local")
    parser = argparse.ArgumentParser(description="Configure, build, and install libbackfill.")
    parser.add_argument("--dest", choices=["local", "global"], default="local",
                        help="Installation destination: 'local' (~/.local) or 'global' (/opt/local). Default: local.")
    parser.add_argument("--prefix", default=None,
                        help="Override install prefix. If set, --dest is ignored.")
    parser.add_argument("--build-type", default="Release",
                        help="CMAKE_BUILD_TYPE (default: Release).")
    parser.add_argument("--generator", "-G", default=None,
                        help="CMake generator to use (e.g., 'Ninja', 'Unix Makefiles').")
    parser.add_argument("--no-clean", action="store_true",
                        help="Do not delete the temporary build directory (for debugging).")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Print commands as they run.")
    parser.add_argument("--source", default=".",
                        help="Source directory (default: current directory).")
    parser.add_argument("--extra-cmake", nargs="*", default=[],
                        help="Additional -DKEY=VALUE cache entries for CMake. Use like: --extra-cmake -DFEATURE=ON -DFOO=bar")
    return parser.parse_args()

def main():
    args = parse_args()
    sysname = platform.system()  # 'Linux', 'Darwin', 'Windows', etc.

    # decide prefix
    if args.prefix:
        install_prefix = Path(args.prefix).expanduser().resolve()
    else:
        if args.dest == "global":
            if sysname == "Darwin":
                die("Global installation to /opt is disallowed on macOS. Use --dest local or provide --prefix.")
            install_prefix = Path("/opt/local/").resolve()
        else:
            install_prefix = (Path.home() / ".local").resolve()

    # sanity checks
    cmake = which_or_die("cmake")
    clang, clangxx = detect_clang(verbose=args.verbose)

    # if Ninja requested, make sure it exists
    if args.generator and "Ninja" in args.generator:
        which_or_die("ninja")

    # warn about permissions for global installs
    if not is_writable(install_prefix):
        if str(install_prefix).startswith("/opt"):
            print("note: /opt is not writable by the current user. The install step may fail without elevated permissions.", file=sys.stderr)

    src_dir = Path(args.source).resolve()
    if not (src_dir / "CMakeLists.txt").exists():
        die(f"No CMakeLists.txt found in source directory: {src_dir}")

    # temp build dir
    tmp_build_root = Path(tempfile.mkdtemp(prefix="build-"))
    build_dir = tmp_build_root  # single-use build dir in tmp
    if args.verbose:
        print(f"Using build directory: {build_dir}")

    # prepare env (force clang)
    env = make_env_with_clang(os.environ, clang, clangxx)

    # configure
    cmake_config_cmd = [
        cmake, "-S", str(src_dir), "-B", str(build_dir),
        f"-DCMAKE_BUILD_TYPE={args.build_type}",
        f"-DCMAKE_INSTALL_PREFIX={str(install_prefix)}",
        "-DCMAKE_C_COMPILER_LAUNCHER=",  # avoid launcher interfering
        "-DCMAKE_CXX_COMPILER_LAUNCHER=",
    ]
    # Ensure clang is used even if CMake tries to be clever
    cmake_config_cmd += [f"-DCMAKE_C_COMPILER={env['CC']}", f"-DCMAKE_CXX_COMPILER={env['CXX']}"]

    for entry in args.extra_cmake:
        if not entry.startswith("-D"):
            die(f"--extra-cmake entries must look like -DKEY=VALUE. Bad entry: {entry}")
        cmake_config_cmd.append(entry)

    if args.generator:
        cmake_config_cmd += ["-G", args.generator]

    try:
        print("==> Configuring with CMake...")
        run(cmake_config_cmd, env=env, verbose=args.verbose)

        print("==> Building...")
        run([cmake, "--build", str(build_dir), "--parallel"], env=env, verbose=args.verbose)

        print(f"==> Installing to: {install_prefix}")
        run([cmake, "--build", str(build_dir), "--target", "install"], env=env, verbose=args.verbose)

        print("✓ Done.")
    finally:
        if args.no_clean:
            print(f"(build directory preserved at: {build_dir})")
        else:
            try:
                shutil.rmtree(build_dir)
                if args.verbose:
                    print(f"(cleaned build directory: {build_dir})")
            except Exception as e:
                print(f"warning: failed to remove build directory '{build_dir}': {e}", file=sys.stderr)

if __name__ == "__main__":
    main()
