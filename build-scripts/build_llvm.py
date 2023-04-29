#!/usr/bin/env python3
#
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#

import argparse
import os
import pathlib
import shlex
import shutil
import subprocess
import sys


def clone_llvm(dst_dir, llvm_repo, llvm_branch):
    llvm_dir = dst_dir.joinpath("llvm").resolve()

    if not llvm_dir.exists():
        print(f"Clone llvm to {llvm_dir} ...")
        GIT_CLONE_CMD = f"git clone --depth 1 --branch {llvm_branch} {llvm_repo} llvm"
        subprocess.check_output(shlex.split(GIT_CLONE_CMD), cwd=dst_dir)
    else:
        print(f"There is an LLVM local repo in {llvm_dir}, clean and keep using it")

    return llvm_dir


def build_llvm(llvm_dir, backends, projects):
    LLVM_COMPILE_OPTIONS = [
        '-DCMAKE_BUILD_TYPE:STRING="Debug"',
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        "-DLLVM_APPEND_VC_REV:BOOL=ON",
        "-DLLVM_BUILD_BENCHMARKS:BOOL=OFF",
        "-DLLVM_BUILD_DOCS:BOOL=OFF",
        "-DLLVM_BUILD_EXAMPLES:BOOL=OFF",
        "-DLLVM_BUILD_LLVM_DYLIB:BOOL=OFF",
        "-DLLVM_BUILD_TESTS:BOOL=OFF",
        "-DLLVM_CCACHE_BUILD:BOOL=OFF",
        "-DLLVM_ENABLE_BINDINGS:BOOL=OFF",
        "-DLLVM_ENABLE_IDE:BOOL=OFF",
        "-DLLVM_ENABLE_TERMINFO:BOOL=OFF",
        "-DLLVM_ENABLE_ZLIB:BOOL=OFF",
        "-DLLVM_INCLUDE_BENCHMARKS:BOOL=OFF",
        "-DLLVM_INCLUDE_DOCS:BOOL=OFF",
        "-DLLVM_INCLUDE_EXAMPLES:BOOL=OFF",
        "-DLLVM_INCLUDE_UTILS:BOOL=OFF",
        "-DLLVM_INCLUDE_TESTS:BOOL=OFF",
        "-DLLVM_BUILD_TESTS:BOOL=OFF",
        "-DLLVM_OPTIMIZED_TABLEGEN:BOOL=ON",
    ]

    LLVM_TARGETS_TO_BUILD = [
        '-DLLVM_TARGETS_TO_BUILD:STRING="' + ";".join(backends) + '"'
        if backends
        else '-DLLVM_TARGETS_TO_BUILD:STRING="X86"'
    ]

    LLVM_PROJECTS_TO_BUILD = [
        '-DLLVM_ENABLE_PROJECTS:STRING="' + ";".join(projects) + '"' if projects else ""
    ]

    # lldb project requires libxml2
    LLVM_LIBXML2_OPTION = [
        "-DLLVM_ENABLE_LIBXML2:BOOL=" + ("ON" if "lldb" in projects else "OFF")
    ]

    if not llvm_dir.exists():
        raise Exception(f"{llvm_dir} doesn't exist")

    build_dir = llvm_dir.joinpath("build").resolve()
    build_dir.mkdir(exist_ok=True)

    lib_llvm_core_library = build_dir.joinpath("lib/libLLVMCore.a").resolve()
    if lib_llvm_core_library.exists():
        print(f"Please remove {build_dir} manually and try again")
        return build_dir

    compile_options = " ".join(
        LLVM_COMPILE_OPTIONS
        + LLVM_LIBXML2_OPTION
        + LLVM_TARGETS_TO_BUILD
        + LLVM_PROJECTS_TO_BUILD
    )

    CONFIG_CMD = f"cmake {compile_options} ../llvm"
    print(f"{CONFIG_CMD}")
    subprocess.check_call(shlex.split(CONFIG_CMD), cwd=build_dir)

    BUILD_CMD = f"cmake --build . --target package --parallel {os.cpu_count()}"
    subprocess.check_call(shlex.split(BUILD_CMD), cwd=build_dir)

    return build_dir


def repackage_llvm(llvm_dir):
    build_dir = llvm_dir.joinpath("./build").resolve()

    packs = [f for f in build_dir.glob("LLVM-16*.tar.gz")]
    if len(packs) > 1:
        raise Exception("Find more than one LLVM-16*.tar.gz")

    if not packs:
        return

    llvm_package = packs[0].name
    # mv build/LLVM-16.0.0*.gz .
    shutil.move(str(build_dir.joinpath(llvm_package).resolve()), str(llvm_dir))
    # rm -r build
    shutil.rmtree(str(build_dir))
    # mkdir build
    build_dir.mkdir()
    # tar xf ./LLVM-16.0.0-*.tar.gz --strip-components=1 --directory=build
    CMD = f"tar xf {llvm_dir.joinpath(llvm_package).resolve()} --strip-components=1 --directory={build_dir}"
    subprocess.check_call(shlex.split(CMD), cwd=llvm_dir)


def main():
    parser = argparse.ArgumentParser(description="build necessary LLVM libraries")
    parser.add_argument(
        "--platform",
        type=str,
        default="linux",
        choices=["linux"],
        help="identify current platform",
    )
    parser.add_argument(
        "--arch",
        nargs="+",
        type=str,
        choices=[
            "RISCV",
            "X86",
        ],
        help="identify LLVM supported backends, separate by space, like '--arch ARM Mips X86'",
    )
    parser.add_argument(
        "--project",
        nargs="+",
        type=str,
        default="",
        choices=["clang", "lldb"],
        help="identify extra LLVM projects, separate by space, like '--project clang lldb'",
    )
    options = parser.parse_args()

    print(f"options={options}")

    platform = options.platform

    print(f"========== Build LLVM for {platform} ==========\n")

    current_file = pathlib.Path(__file__)
    if current_file.is_symlink():
        current_file = pathlib.Path(os.readlink(current_file))

    current_dir = current_file.parent.resolve()
    deps_dir = current_dir.joinpath("../runtime/deps").resolve()

    try:
        print(f"==================== CLONE LLVM ====================")
        llvm_info = {"repo": "https://github.com/llvm/llvm-project.git",
            "branch": "release/16.x"}
        llvm_dir = clone_llvm(deps_dir, llvm_info["repo"], llvm_info["branch"])

        print()
        print(f"==================== BUILD LLVM ====================")
        build_llvm(llvm_dir, options.arch, options.project)

        print()
        print(f"==================== PACKAGE LLVM ====================")
        repackage_llvm(llvm_dir)

        print()
        return True
    except subprocess.CalledProcessError:
        return False


if __name__ == "__main__":
    sys.exit(0 if main() else 1)
