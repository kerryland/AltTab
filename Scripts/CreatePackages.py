"""Build AltTab via compile.cmd and bundle the redistributables into zip files.

Unlike the old devenv-based implementation this does not hardcode any Visual
Studio path and it produces .zip archives. Building is delegated to
compile.cmd, which locates MSBuild.exe and the newest C++ PlatformToolset
automatically. Only the Python standard library is required.

Usage:
  python CreatePackages.py                     # build Release + ReleaseNoLogger, zip both
  python CreatePackages.py --configs Release    # build and zip only Release
  python CreatePackages.py --skip-build         # zip the already-built exes only
  python CreatePackages.py --rebuild            # clean, rebuild, then zip
  python CreatePackages.py --update-version     # bump build number, then build + zip
"""

import argparse
import os
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

import VersionHelper

ROOT_DIR = Path(__file__).resolve().parent.parent
COMPILE_CMD = ROOT_DIR / "compile.cmd"
RELEASES_DIR = ROOT_DIR / "Releases"
X64_DIR = ROOT_DIR / "x64"
INTERMEDIATE_DIR = ROOT_DIR / "IntermediateFiles" / "AltTab"

# Files shipped in every package, relative to ROOT_DIR.
PACKAGE_FILES = ["AltTab.chm", "ReadMe.txt", "ReleaseNotes.txt"]


def exe_for(config):
    return X64_DIR / config / "AltTab.exe"


def intermediate_dir_for(config):
    return INTERMEDIATE_DIR / "x64" / config


def clean_config(config):
    """Delete a configuration's build outputs so the next build is a full one."""
    paths = [exe_for(config).parent, intermediate_dir_for(config)]
    for path in paths:
        if path.exists():
            try:
                shutil.rmtree(path)
                print(f"Cleaned {path}")
            except OSError as error:
                print(f"Warning: could not clean {path}: {error}", file=sys.stderr)


def build_configs(configs, rebuild):
    """Build the requested configurations by delegating to compile.cmd."""
    if rebuild:
        for config in configs:
            clean_config(config)

    # compile.cmd takes no argument for Release (its default); pass the config
    # name for every other configuration.
    for config in configs:
        args = [] if config == "Release" else [config]
        command = ["cmd.exe", "/c", str(COMPILE_CMD)] + args
        print(f"\n===== Building {config} | x64 =====\n")
        result = subprocess.run(
            command,
            cwd=str(ROOT_DIR),
            stdin=subprocess.DEVNULL,  # keep compile.cmd's pause() from hanging us
        )
        if result.returncode != 0:
            print(f"[ERROR] Build failed for {config} | x64.", file=sys.stderr)
            return False
    return True


def create_zip(output_filename, files_to_pack):
    """Create a zip archive containing the given files (flat file names)."""
    with zipfile.ZipFile(output_filename, "w", zipfile.ZIP_DEFLATED) as archive:
        for file_path in files_to_pack:
            if os.path.exists(file_path):
                archive.write(file_path, os.path.basename(file_path))
            else:
                print(f"Warning: File '{file_path}' not found. Skipping.")
    return output_filename


def create_package(config):
    """Zip one configuration's redistribution files into Releases\\."""
    version = VersionHelper.get_current_version()
    config_label = "ReleaseLogger" if config == "Release" else config
    output_filename = RELEASES_DIR / f"AltTab_{version}_{config_label}_x64.zip"

    files_to_pack = [str(exe_for(config))]
    files_to_pack += [str(ROOT_DIR / name) for name in PACKAGE_FILES]

    create_zip(output_filename, files_to_pack)
    print(f"Package '{output_filename}' created successfully.")

    if config == "ReleaseNoLogger":
        # Additional copy without the configuration suffix.
        copy_to = RELEASES_DIR / f"AltTab_{version}_x64.zip"
        shutil.copy2(output_filename, copy_to)
        print(f"Copied '{output_filename}' to '{copy_to}'.")


def parse_arguments():
    """Parse command line arguments."""
    parser = argparse.ArgumentParser(
        description="Build AltTab (via compile.cmd) and create zip packages.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""Examples:
  # Build and package all configurations
  python CreatePackages.py

  # Build and package only the Release configuration
  python CreatePackages.py --configs Release

  # Clean, rebuild and package specific configurations
  python CreatePackages.py --configs Release ReleaseNoLogger --build-action rebuild

  # Do not build, only create zips from existing outputs
  python CreatePackages.py --skip-build
""",
    )

    parser.add_argument(
        "--update-version",
        action="store_true",
        help="Bump the build number in VersionInfo.txt before building",
    )

    parser.add_argument(
        "--generate-version-header",
        action="store_true",
        help="Regenerate source/version.h from VersionInfo.txt before building",
    )

    parser.add_argument(
        "--configs",
        nargs="+",
        default=["Release", "ReleaseNoLogger"],
        choices=["Release", "ReleaseNoLogger"],
        help="Configuration(s) to build and package (default: Release ReleaseNoLogger)",
    )

    parser.add_argument(
        "--build-action",
        choices=["build", "rebuild", "clean"],
        default="build",
        help="build: incremental (default) | rebuild: clean then build | clean: clean only",
    )

    parser.add_argument(
        "--skip-build",
        action="store_true",
        help="Skip building/cleaning and only create packages from existing outputs",
    )

    return parser.parse_args()


if __name__ == "__main__":
    args = parse_arguments()

    print(f"Current Version: {VersionHelper.get_current_version()}\n")

    if args.update_version:
        VersionHelper.update_version_info()
    elif args.generate_version_header:
        VersionHelper.create_version_header()

    if args.skip_build:
        print("Skipping build (--skip-build).")
    elif args.build_action == "clean":
        for config in args.configs:
            clean_config(config)
        print("Clean finished. Nothing was packaged.")
        sys.exit(0)
    elif not build_configs(args.configs, args.build_action == "rebuild"):
        sys.exit(1)

    RELEASES_DIR.mkdir(exist_ok=True)
    for config in args.configs:
        create_package(config)

    print("\nDone.")