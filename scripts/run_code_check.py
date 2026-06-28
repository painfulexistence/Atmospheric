#!/usr/bin/env python3
"""
Code quality script for Atmospheric.

Default mode  : run clang-tidy (fix) then clang-format (fix in-place).
--check mode  : run clang-format --dry-run only; exits non-zero if any file
                would be reformatted.  Used by CI (no build required).
"""
import argparse
import glob
import multiprocessing
import os
import shutil
import subprocess
import sys


SEARCH_DIRS = [
    'Engine/src',
    'Engine/include',
    'Engine/frontends',
    'Examples',
]


def find_source_files(root_dir):
    extensions = ('*.cpp', '*.hpp', '*.c', '*.h', '*.mm', '*.m')
    files = []
    for search_dir in SEARCH_DIRS:
        dir_path = os.path.join(root_dir, search_dir)
        if not os.path.exists(dir_path):
            continue
        for ext in extensions:
            files.extend(glob.glob(os.path.join(dir_path, '**', ext), recursive=True))
    return files


def run_clang_tidy(project_root, source_files):
    print("========================================")
    print("      Running run-clang-tidy            ")
    print("========================================")

    clang_tidy_exe = shutil.which('run-clang-tidy')
    if not clang_tidy_exe:
        brew_path = '/opt/homebrew/opt/llvm/bin/run-clang-tidy'
        if os.path.exists(brew_path):
            clang_tidy_exe = brew_path
        else:
            print("run-clang-tidy not found. Install llvm (e.g. brew install llvm).")
            sys.exit(1)

    compile_commands = os.path.join(project_root, 'build', 'compile_commands.json')
    if not os.path.exists(compile_commands):
        print("Error: build/compile_commands.json not found.")
        print("Run CMake first: cmake --preset ninja-global-vcpkg -DCMAKE_EXPORT_COMPILE_COMMANDS=ON")
        sys.exit(1)

    cores = multiprocessing.cpu_count()
    cmd = [clang_tidy_exe, '-p', 'build', '-fix', '-j', str(cores)] + source_files

    print(f"Executing: {' '.join(cmd)}")
    result = subprocess.run(cmd, check=False)
    if result.returncode == 0:
        print("run-clang-tidy finished successfully.\n")
    else:
        print(f"run-clang-tidy finished with warnings/errors (exit {result.returncode}).\n")


def run_clang_format(source_files, check_only=False):
    print("========================================")
    print("      Running clang-format              ")
    print("========================================")

    clang_format_exe = shutil.which('clang-format') or shutil.which('clang-format-18')
    if not clang_format_exe:
        if check_only:
            print("Error: clang-format not found.")
            sys.exit(1)
        print("Warning: clang-format not found. Skipping.")
        return

    if check_only:
        # --dry-run --Werror: exit non-zero if any file would change
        failed = []
        for f in source_files:
            r = subprocess.run(
                [clang_format_exe, '--dry-run', '--Werror', f],
                capture_output=True, text=True
            )
            if r.returncode != 0:
                failed.append(f)
                sys.stdout.write(r.stderr)

        if failed:
            print(f"\n{len(failed)} file(s) need reformatting:")
            for f in failed:
                print(f"  {f}")
            print("\nRun  python3 scripts/run_code_check.py  to fix in-place.")
            sys.exit(1)
        print(f"All {len(source_files)} files are correctly formatted.\n")
    else:
        print(f"Formatting {len(source_files)} files in-place...")
        chunk_size = 50
        for i in range(0, len(source_files), chunk_size):
            subprocess.run([clang_format_exe, '-i'] + source_files[i:i + chunk_size], check=True)
        print("clang-format finished successfully.\n")


def main():
    parser = argparse.ArgumentParser(description="Atmospheric code quality checks.")
    parser.add_argument(
        '--check',
        action='store_true',
        help='Format-check only (clang-format --dry-run). No tidy, no file modifications. Used by CI.',
    )
    args = parser.parse_args()

    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    os.chdir(project_root)

    source_files = find_source_files(project_root)
    if not source_files:
        print("No source files found!")
        sys.exit(0)

    print(f"Found {len(source_files)} source files.\n")

    if args.check:
        run_clang_format(source_files, check_only=True)
        print("Format check passed.")
    else:
        run_clang_tidy(project_root, source_files)
        run_clang_format(source_files, check_only=False)
        print("All checks and fixes completed successfully!")


if __name__ == '__main__':
    main()
