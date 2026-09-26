#!/usr/bin/env python3
"""Fail if any tracked file looks like 3dfx material (PRD D12).

Checks every file in `git ls-files`: known retail OVL hashes, LE DLLs whose
module name is glide2x (only build outputs may be that), and copyright
banners that appear in 3dfx headers and binaries.
"""
import hashlib
import subprocess
import sys

KNOWN_RETAIL = {
    "e077ab1c5809f7ee",  # GTA GLIDE2X.OVL (Voodoo Graphics)
    "6981a7bd77445be7",  # Screamer Rally glide2x.ovl / hotcgl/voodoo.ovl
}
BANNERS = [b"Copyright (c) 1995, 3Dfx", b"Copyright (c) 1996, 3Dfx", b"Copyright (c) 1997, 3Dfx",
           b"3Dfx Interactive, Inc.", b"UNPUBLISHED PROPRIETARY", b"3DFX GLIDE Source Code"]
ALLOW = {"tools/ci/no_3dfx.py"}


def main():
    files = subprocess.run(["git", "ls-files"], check=True, stdout=subprocess.PIPE, text=True).stdout.split()
    bad = []
    for f in files:
        if f in ALLOW:
            continue
        try:
            data = open(f, "rb").read()
        except OSError:
            continue
        h = hashlib.sha256(data).hexdigest()[:16]
        if h in KNOWN_RETAIL:
            bad.append("%s: retail OVL" % f)
        if data[:2] == b"MZ" and b"glide2x" in data[:1 << 16]:
            bad.append("%s: LE DLL named glide2x committed" % f)
        for b in BANNERS if not f.endswith(".md") else []:
            if b.lower() in data.lower():
                bad.append("%s: contains %r" % (f, b.decode()))
    for b in bad:
        print("no-3dfx:", b)
    print("no-3dfx: %d files checked, %d problems" % (len(files), len(bad)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
