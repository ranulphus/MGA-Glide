#!/usr/bin/env python3
"""Extract local test fixtures from the game archives (never committed).

  $FIXTURES_DIR/ovl/gta.ovl     GTA's retail GLIDE2X.OVL (Voodoo Graphics)
  $FIXTURES_DIR/ovl/sr.ovl      Screamer Rally's retail glide2x.ovl
  $FIXTURES_DIR/games/gta/...   GTA ECTS build tree
  $FIXTURES_DIR/games/sr/...    Screamer Rally tree
"""
import hashlib
import os
import sys
import zipfile

GAMES = os.environ.get("GAMES_DIR", os.path.expanduser("~/DOSGAMES"))
FIX = os.environ.get("FIXTURES_DIR", os.path.expanduser("~/.cache/mga-glide/fixtures"))

SOURCES = {
    "gta": ("grand-theft-auto.zip", "GTAECTS/", "GTAECTS/GTADOS/GLIDE2X.OVL"),
    "sr": ("Screamer-Rally_DOS_EN.zip", "screamer_rally/sr/", "screamer_rally/sr/glide2x.ovl"),
}


def main():
    os.makedirs(os.path.join(FIX, "ovl"), exist_ok=True)
    for key, (zname, prefix, ovl) in SOURCES.items():
        zpath = os.path.join(GAMES, zname)
        if not os.path.exists(zpath):
            print("fixtures: %s missing, skipping %s" % (zpath, key))
            continue
        z = zipfile.ZipFile(zpath)
        data = z.read(ovl)
        out = os.path.join(FIX, "ovl", key + ".ovl")
        open(out, "wb").write(data)
        print("fixtures: %s sha256=%s" % (out, hashlib.sha256(data).hexdigest()[:16]))
        gdir = os.path.join(FIX, "games", key)
        if not os.path.isdir(gdir):
            for info in z.infolist():
                if info.filename.startswith(prefix) and not info.is_dir():
                    rel = info.filename[len(prefix):]
                    dst = os.path.join(gdir, rel)
                    os.makedirs(os.path.dirname(dst), exist_ok=True)
                    open(dst, "wb").write(z.read(info))
            print("fixtures: unpacked %s" % gdir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
