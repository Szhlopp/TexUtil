#!/usr/bin/env python3
"""Fetch the pinned optional Filament SDK. Uses only Python's standard library."""
import argparse
import hashlib
import platform
import shutil
import tarfile
import urllib.request
from pathlib import Path

VERSION = "1.76.1"
RELEASES = {
    "mac": "37f74c9e67e37b1c47dc12859de23829fb6d6f936fd2de00a13e59b9a4685703",
    "linux": "958752e9753d0cdf29503a62ae6b5e23af7710aec0a41bf738d8b73d80bda1e1",
    "arm-linux": "56eeed7bab2eb7a7f3e70dd6c77a6cdceaff376efa58ecd18c2e21fd0b02fa7a",
    "windows": "07971b211f3fde7e6c4252fd6985ec7cf77c7ac991b157d293f52a8ca374a206",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=Path("build/model-deps"))
    args = parser.parse_args()
    system = platform.system()
    suffix = {"Darwin": "mac", "Windows": "windows", "Linux": "linux"}.get(system)
    if suffix is None:
        parser.error("No pinned Filament SDK for this platform")
    if suffix == "linux" and platform.machine().lower() in ("aarch64", "arm64"):
        suffix = "arm-linux"
    destination = args.out.resolve()
    sdk = destination / "filament"
    if sdk.exists():
        parser.error(f"{sdk} already exists; use it or choose a new --out directory")
    destination.mkdir(parents=True, exist_ok=True)
    archive = destination / f"filament-v{VERSION}-{suffix}.tgz"
    url = f"https://github.com/google/filament/releases/download/v{VERSION}/{archive.name}"
    print(f"Downloading {url}", flush=True)
    with urllib.request.urlopen(url) as response, archive.open("wb") as output:
        shutil.copyfileobj(response, output)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != RELEASES[suffix]:
        raise RuntimeError("SDK SHA-256 mismatch; refusing extraction")
    # Official SDK consists of directories and regular files. Preserve tool modes.
    with tarfile.open(archive) as package:
        for member in package.getmembers():
            target = (destination / member.name).resolve()
            if not target.is_relative_to(sdk) or not (member.isdir() or member.isfile()):
                raise RuntimeError(f"Unexpected archive entry: {member.name}")
        for member in package.getmembers():
            target = destination / member.name
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                with package.extractfile(member) as source, target.open("wb") as output:
                    shutil.copyfileobj(source, output)
                target.chmod(member.mode & 0o777)
    print(f"SDK ready: {sdk}")
    print(f'Configure with -DTEXUTIL_FILAMENT_ROOT="{sdk}"')


if __name__ == "__main__":
    main()
