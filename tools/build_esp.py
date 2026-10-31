"""Writes KickTheDoor.esp (ESL flagged, master Skyrim.esm only, no edits to other records):

  SNDR  KTD_DoorImpactSD   a kick or a shout wave slamming into a door (res/Sound/FX/KickTheDoor/door_impact_0N.wav,
                          made by tools/make_sounds.py), positional, set up like the vanilla battering ram impact
  SNDR  KTD_LockBreakSD    the lock of a door or a chest giving way: the game's own wpn_impact_blade_metal_01.wav (from
                          Skyrim - Sounds.bsa, not shipped), quieter by LOCK_BREAK_ATTENUATION, positional, no rumble

  SPEL  KTD_KickSpell      the kick landing on someone (src/Fight.cpp): the vanilla StaggerAttackFFContact effect (hostile,
                          Stagger archetype) at 0.75 like the first word of Unrelenting Force (the DLL passes the menu's
                          stagger strength); cast by the player, so the target and onlookers take it as an attack

  MGEF  KTD_KickDamageEffect  health damage on contact: a copy of the vanilla EnchSilverSwordFFContact data (hostile,
                          detrimental, no resistance, no shader / art / sound) without its undead-only conditions
  SPEL  KTD_KickDamage    the kick's damage, 2 health (fixed); cast by the player, so a kill counts as theirs

  MGEF  KTD_KickPowerEffect  the Kick power's effect: a script archetype effect with no script (does nothing itself; the
                          DLL kicks when the power is used), on self, no magnitude / duration / area, no hit effect
  SPEL  KTD_KickPower     the Kick lesser power (voice slot, instant, free): only given to the player when [Kick]
                          bKickPower is on; using it (the shout key with it equipped, or a hotkey mod casting it) kicks

The DLL finds them by local form id (0x800, 0x801, 0x803, 0x804, 0x806 in KickTheDoor.esp).
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
LOCK_BREAK = 0x804
POWER_EFFECT = 0x805
POWER = 0x806
VOICE_SLOT = 0x025BEE  # VoiceEquip (lesser powers and shouts)
POWER_DESCRIPTION = "Kick: the door or the chest in front of you, or whoever is in the way."
LOCK_BREAK_FILE = "Data\\Sound\\FX\\WPN\\Impact\\Blade\\Metal\\WPN_Impact_Blade_Metal_01.wav"  # vanilla, in the game's BSA
LOCK_BREAK_ATTENUATION = 9.0  # dB quieter than the recording (it is mastered as loud as a sword hit)
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


def mgef_power():
    data = bytearray(DAMAGE_DATA)
    # no hit event, no duration / magnitude / area, painless, no hit effect; script archetype, no actor value, on self
    struct.pack_into("<I", data, 0, 0x10 | 0x200 | 0x400 | 0x800 | 0x4000000 | 0x8000000)
    struct.pack_into("<I", data, 64, 1)            # archetype: script
    struct.pack_into("<i", data, 68, -1)           # actor value: none
    struct.pack_into("<II", data, 80, 1, 0)        # fire and forget, self
    return record("MGEF", OWN | POWER_EFFECT, [
        sub("EDID", "KTD_KickPowerEffect"),
        sub("FULL", "Kick"),
        sub("MDOB", u32(MENU_DISPLAY)),
        sub("DATA", bytes(data)),
        sub("SNDD", b""),
        sub("DNAM", POWER_DESCRIPTION),
    ])


def spel_power():
    return record("SPEL", OWN | POWER, [
        sub("EDID", "KTD_KickPower"),
        sub("OBND", b"\0" * 12),
        sub("FULL", "Kick"),
        sub("ETYP", u32(VOICE_SLOT)),
        sub("DESC", POWER_DESCRIPTION),
        # cost 0, flags (manual cost), type 3 lesser power, no charge time, fire and forget, self, instant
        sub("SPIT", struct.pack("<IIIfIIffI", 0, 0x1, 3, 0.0, 1, 0, 0.0, 0.0, 0)),
        sub("EFID", u32(OWN | POWER_EFFECT)),
        sub("EFIT", struct.pack("<fII", 0.0, 0, 0)),
    ])


def main():
    waves = sorted(SOUNDS.glob("door_impact_*.wav"))
    if not waves:
        sys.exit("no sounds: run make_sounds.py first")
    sounds = [sndr(DOOR_IMPACT, "KTD_DoorImpactSD", ["Data\\Sound\\FX\\KickTheDoor\\" + w.name for w in waves], OUTPUT_MONO_2000_VERB),
              sndr(LOCK_BREAK, "KTD_LockBreakSD", [LOCK_BREAK_FILE], OUTPUT_MONO_2000_VERB, freq_variance=5, rumble=0,
                   attenuation_db=LOCK_BREAK_ATTENUATION)]
    groups = [("SNDR", sounds), ("MGEF", [mgef_damage(), mgef_power()]), ("SPEL", [spel_kick(), spel_damage(), spel_power()])]
    body = b"".join(group(label, recs) for label, recs in groups)
    count = sum(len(recs) + 1 for _, recs in groups)
    header = record("TES4", 0, [
        sub("HEDR", struct.pack("<f", 1.71) + struct.pack("<i", count) + u32(POWER + 1)),
        sub("CNAM", "W1ter"),
        sub("SNAM", "Kick the Door"),
        sub("MAST", "Skyrim.esm"), sub("DATA", b"\0" * 8),
    ], flags=0x200)  # ESL
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(header + body)
    print(f"wrote {OUT} (2 sound descriptors: {len(waves)} impact files, the vanilla lock break sound; 2 effects, 3 spells)")


if __name__ == "__main__":
    main()
