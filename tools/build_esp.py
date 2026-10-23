"""Writes KickTheDoor.esp (ESL flagged, master Skyrim.esm only, no edits to other records):

  SNDR  KTD_DoorImpactSD   a kick or a shout wave slamming into a door (res/Sound/FX/KickTheDoor/door_impact_0N.wav,
                          made by tools/make_sounds.py), positional, set up like the vanilla battering ram impact

  SPEL  KTD_KickSpell      the kick landing on someone (src/Fight.cpp): the vanilla StaggerAttackFFContact effect (hostile,
                          Stagger archetype) at 0.75 like the first word of Unrelenting Force (the DLL passes the menu's
                          stagger strength); cast by the player, so the target and onlookers take it as an attack

  MGEF  KTD_KickDamageEffect  health damage on contact: a copy of the vanilla EnchSilverSwordFFContact data (hostile,
                          detrimental, no resistance, no shader / art / sound) without its undead-only conditions
  SPEL  KTD_KickDamage    the kick's damage, 2 health (fixed); cast by the player, so a kill counts as theirs

The DLL finds them by local form id (0x800, 0x801, 0x803 in KickTheDoor.esp).
usage: python build_esp.py [output.esp]
"""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "res" / "KickTheDoor.esp"
SOUNDS = ROOT / "res" / "Sound" / "FX" / "KickTheDoor"
OWN = 0x01000000  # this plugin (one master)

# Skyrim.esm forms (the same as TRPBatteringRamImpactSD uses)
CATEGORY_SFX = 0x172A1            # AudioCategorySFX (the "Effects" volume slider)
OUTPUT_MONO_2000_VERB = 0xE324B   # SOMMono02000_verb: positional with reverb, heard up to 2000 units
DESCRIPTOR_STANDARD = 0x1EEF540A  # BGSStandardSoundDef

DOOR_IMPACT = 0x800
KICK_SPELL = 0x801
DAMAGE_EFFECT = 0x802
DAMAGE_SPELL = 0x803
KICK_DAMAGE = 2.0  # health a landed kick takes, also from someone a bursting door knocks over
# EnchSilverSwordFFContact's DATA: value modifier on Health, fire and forget, contact, flags hostile | detrimental | no area
DAMAGE_DATA = bytes.fromhex(
    "050820100000000000000000ffffffffffffffff0000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000001800000000000000000000000100000001000000ffffffff000000000000000000000000"
    "00000000000000000000803f000000000000000000000000000000000000000000000000000000000000000000000000")
MENU_DISPLAY = 0x0435A5  # MDOB as the vanilla effects

STAGGER_CONTACT = 0x07809B  # StaggerAttackFFContact (Skyrim.esm)
EITHER_HAND = 0x013F44      # EitherHand equip slot


def sub(typ, data):
    if isinstance(data, str):
        data = data.encode("cp1252") + b"\0"
    return typ.encode() + struct.pack("<H", len(data)) + data


def u32(v):
    return struct.pack("<I", v)


def record(typ, formid, subs, flags=0):
    body = b"".join(subs)
    return typ.encode() + struct.pack("<IIIIHH", len(body), flags, formid, 0, 44, 0) + body


def group(label, contents):
    body = b"".join(contents)
    return b"GRUP" + struct.pack("<I", 24 + len(body)) + label.encode() + struct.pack("<iHHHH", 0, 0, 0, 0, 0) + body


def sndr(local, edid, files, output, priority=128, freq_variance=10, db_variance=2, attenuation_db=2.0, rumble=0x43):
    subs = [sub("EDID", edid), sub("CNAM", u32(DESCRIPTOR_STANDARD)), sub("GNAM", u32(CATEGORY_SFX))]
    subs += [sub("ANAM", f) for f in files]
    subs += [
        sub("ONAM", u32(output)),
        # unknown (1 as the vanilla impacts), looping none, unknown, controller rumble
        sub("LNAM", bytes([1, 0, 0, rumble])),
        # frequency shift, frequency variance, priority, db variance, static attenuation (1/100 dB)
        sub("BNAM", struct.pack("<bbBBH", 0, freq_variance, priority, db_variance, int(attenuation_db * 100))),
    ]
    return record("SNDR", OWN | local, subs)


def spel_kick():
    return record("SPEL", OWN | KICK_SPELL, [
        sub("EDID", "KTD_KickSpell"),
        sub("OBND", b"\0" * 12),
        sub("FULL", "Kick"),
        sub("ETYP", u32(EITHER_HAND)),
        sub("DESC", ""),
        # cost, flags (manual cost), type (spell), charge time, casting (fire and forget), delivery (contact), cast
        # duration, range, half cost perk - as the vanilla gargoyle stagger hits
        sub("SPIT", struct.pack("<IIIfIIffI", 0, 0x1, 0, 0.0, 1, 1, 0.0, 0.0, 0)),
        sub("EFID", u32(STAGGER_CONTACT)),
        sub("EFIT", struct.pack("<fII", 0.75, 0, 0)),
    ])


def mgef_damage():
    assert len(DAMAGE_DATA) == 152
    return record("MGEF", OWN | DAMAGE_EFFECT, [
        sub("EDID", "KTD_KickDamageEffect"),
        sub("FULL", "Kick"),
        sub("MDOB", u32(MENU_DISPLAY)),
        sub("DATA", DAMAGE_DATA),
        sub("SNDD", b""),
        sub("DNAM", ""),
    ])


def spel_damage():
    return record("SPEL", OWN | DAMAGE_SPELL, [
        sub("EDID", "KTD_KickDamage"),
        sub("OBND", b"\0" * 12),
        sub("FULL", "Kick"),
        sub("ETYP", u32(EITHER_HAND)),
        sub("DESC", ""),
        sub("SPIT", struct.pack("<IIIfIIffI", 0, 0x1, 0, 0.0, 1, 1, 0.0, 0.0, 0)),
        sub("EFID", u32(OWN | DAMAGE_EFFECT)),
        sub("EFIT", struct.pack("<fII", KICK_DAMAGE, 0, 0)),
    ])


def main():
    waves = sorted(SOUNDS.glob("door_impact_*.wav"))
    if not waves:
        sys.exit("no sounds: run make_sounds.py first")
    sounds = [sndr(DOOR_IMPACT, "KTD_DoorImpactSD", ["Data\\Sound\\FX\\KickTheDoor\\" + w.name for w in waves], OUTPUT_MONO_2000_VERB)]
    groups = [("SNDR", sounds), ("MGEF", [mgef_damage()]), ("SPEL", [spel_kick(), spel_damage()])]
    body = b"".join(group(label, recs) for label, recs in groups)
    count = sum(len(recs) + 1 for _, recs in groups)
    header = record("TES4", 0, [
        sub("HEDR", struct.pack("<f", 1.71) + struct.pack("<i", count) + u32(DAMAGE_SPELL + 1)),
        sub("CNAM", "W1ter"),
        sub("SNAM", "Kick the Door"),
        sub("MAST", "Skyrim.esm"), sub("DATA", b"\0" * 8),
    ], flags=0x200)  # ESL
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(header + body)
    print(f"wrote {OUT} (1 sound descriptor with {len(waves)} files, 1 effect, 2 spells)")


if __name__ == "__main__":
    main()
