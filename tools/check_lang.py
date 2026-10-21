"""Checks the translations in res/SKSE/Plugins/KickTheDoor/lang against the texts the code translates.

The English texts are the keys: T("..."), F("..."), Id("..."), Hint("..."), Heading("..."), the sidebar pages
(Page{ "..." }), kGlyphHint and the lock level names (Settings.h kLevelNames).
Reports keys missing from a language file, keys the code no longer uses and broken {} placeholders.
usage: python check_lang.py [--keys]   (--keys: print the keys as a json template)
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCES = [ROOT / "src" / n for n in ("Menu.cpp", "Doors.cpp", "Kick.cpp", "Shout.cpp")]
LANG_DIR = ROOT / "res" / "SKSE" / "Plugins" / "KickTheDoor" / "lang"
LANGUAGES = ["french", "german", "italian", "spanish", "polish", "russian", "japanese", "chinese"]
LIT = r'"((?:[^"\\]|\\.)*)"'


def code_keys():
    levels = re.search(r"kLevelNames\{([^}]*)\}", (ROOT / "src" / "Settings.h").read_text(encoding="utf-8"))
    found = re.findall(LIT, levels.group(1))
    for src in SOURCES:
        text = src.read_text(encoding="utf-8")
        for call in ("T", "F", "Id", "Hint", "Heading"):
            found += re.findall(r"\b" + call + r"\(\s*" + LIT, text)
        # T(condition ? "a" : "b")
        for pair in re.findall(r"\bT\([^\n\"]*\?\s*" + LIT + r"\s*:\s*" + LIT, text):
            found += pair
        found += re.findall(r"Page\{\s*" + LIT, text)
        found += re.findall(r"kGlyphHint = " + LIT, text)
    keys = []
    for key in found:
        key = key.encode().decode("unicode_escape")
        if key not in keys:
            keys.append(key)
    return keys


def main():
    keys = code_keys()
    if "--keys" in sys.argv:
        print(json.dumps({k: "" for k in keys}, ensure_ascii=False, indent="\t"))
        return
    ok = True
    for lang in LANGUAGES:
        path = LANG_DIR / f"{lang}.json"
        if not path.exists():
            print(f"{lang}: missing file"); ok = False; continue
        table = json.loads(path.read_text(encoding="utf-8"))
        missing = [k for k in keys if not table.get(k)]
        unused = [k for k in table if k not in keys]
        broken = [k for k, v in table.items() if v and sorted(re.findall(r"\{[^}]*\}", k)) != sorted(re.findall(r"\{[^}]*\}", v))]
        for what, items in (("missing", missing), ("unused", unused), ("broken placeholders", broken)):
            for item in items:
                print(f"{lang}: {what}: {item}"); ok = False
    print(f"{len(keys)} keys, {'all languages complete' if ok else 'problems above'}")


if __name__ == "__main__":
    main()
