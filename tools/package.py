r"""Builds the mod archive "Kick the Door <version>.zip": KickTheDoor.esp, SKSE\Plugins\KickTheDoor.dll, its ini and
translations, Meshes\KickTheDoor\kick.hkx and the door impact sounds. The version is the target's set_version in
xmake.lua (a trailing ".0" is left out of the name).
usage: python package.py [build dir] [output dir]"""

import re
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build"
OUTPUT = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "dist"


def version(target: str) -> str:
    text = (ROOT / "xmake.lua").read_text(encoding="utf-8")
    match = re.search(r'target\("' + re.escape(target) + r'"\)\s*\n\s*set_version\("([^"]+)"\)', text)
    full = match.group(1) if match else "0.0.0"
    return re.sub(r"\.0$", "", full)


def main():
    plugin = BUILD / "windows" / "x64" / "releasedbg"
    res = ROOT / "res"
    files = {
        "KickTheDoor.esp": res / "KickTheDoor.esp",
        "SKSE/Plugins/KickTheDoor.dll": plugin / "KickTheDoor.dll",
        "SKSE/Plugins/KickTheDoor.ini": res / "SKSE" / "Plugins" / "KickTheDoor.ini",
        "Meshes/KickTheDoor/kick.hkx": res / "Meshes" / "KickTheDoor" / "kick.hkx",
    }
    for lang in sorted((res / "SKSE" / "Plugins" / "KickTheDoor" / "lang").glob("*.json")):
        files[f"SKSE/Plugins/KickTheDoor/lang/{lang.name}"] = lang
    for sound in sorted((res / "Sound" / "FX" / "KickTheDoor").glob("*.wav")):
        files[f"Sound/FX/KickTheDoor/{sound.name}"] = sound
    missing = [str(p) for p in files.values() if not p.exists()]
    if missing:
        sys.exit("missing: " + ", ".join(missing))
    OUTPUT.mkdir(parents=True, exist_ok=True)
    archive = OUTPUT / f"Kick the Door {version('KickTheDoor')}.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zf:
        for entry, path in files.items():
            zf.write(path, entry)
    print(f"wrote {archive} ({archive.stat().st_size / 1024:.0f} KB, {len(files)} files)")


if __name__ == "__main__":
    main()
