# Kick animation packs

Kick the Door plays its kick animation itself, on the player's skeleton: no Nemesis, Pandora, FNIS or OAR. Anyone can
add their own kick (or several) as an **animation pack**, a plain folder with loose files:

```
Data\Meshes\KickTheDoor\Animations\
    Default\                 the mod's own kick (priority 0)
        kick.hkx
        config.json
    My Kicks\                your pack: any folder name
        front_kick.hkx       one or more .hkx files: one of them is picked at random for each kick
        spinning_kick.hkx
        config.json          optional
```

A pack mod only needs that folder, so it installs next to Kick the Door in any mod manager without overwriting
anything. Overwriting `Default\kick.hkx` works too, if you'd rather just replace the kick.

## The animation file

- A normal Skyrim **SE** animation (64 bit Havok packfile, `.hkx`), spline compressed like the game's own or
  uncompressed. Skyrim LE (32 bit) files are refused. Blender with the
  io_hkx_animation add-on exports them.
- Made on the vanilla character skeleton (`meshes\actors\character\character assets\skeleton.hkx`), standing in
  place. The root, camera, weapon and magic nodes are ignored: the plugin turns the player to the door and steps them
  in. Bone translations are applied as offsets from the first frame, so the player's own proportions stay.
- Start and end close to a standing pose: the animation blends in over 0.08 s and out over the last 0.15 s.
- Its length is the kick's length; the player gets their controls back when it ends.
- Annotations (Blender pose markers with io_hkx_animation) set the timing:
  - `Impact` (or `HitFrame`): the foot meets the door. Stamina, the lock and the door are handled here.
  - `Swing`: the whoosh sound.
  - Without them: impact at 42 % of the length, swing at 60 % of the impact.

## config.json

Everything is optional. `//` comments are allowed.

```json
{
	"name": "My Kicks",            // shown in the menu (default: the folder name)
	"author": "Me",
	"priority": 10,                // higher wins (default 1; the mod's own kick is 0)
	"keepArmsWithWeapon": true,    // with a weapon drawn the arms keep the weapon stance (default true)
	"impact": 0.4,                 // seconds, for every file of the pack: overrides the Impact annotation
	"swing": 0.25,                 // seconds: overrides the Swing annotation
	"conditions": {                // leave one out = either way
		"sex": "female",           // "male" or "female"
		"weaponDrawn": false,
		"sneaking": true,
		"lockedDoor": true,        // false = only for doors that aren't locked
		"races": ["KhajiitRace", "Skyrim.esm|0x13740"]  // editor ids, or "Plugin.esp|0xID" (always works)
	}
}
```

## Which pack plays

For each kick, every pack whose conditions fit is a candidate; the highest priority wins. Packs with the same priority
are mixed: one animation is picked at random from all of their files. A pack without conditions always fits, so the
mod's own kick (priority 0) plays when nothing else does.

Players can also pick one pack for every kick: SKSE Menu Framework > Kick the Door > Settings > Kick animation, or
`sAnimation = <folder name>` in `SKSE\Plugins\KickTheDoor.ini` (`auto` = by priority and conditions).

## Checking a pack

The menu's **Status** page lists every pack found: name, author, priority, how many animations loaded, its conditions,
and any file that didn't load and why (a broken config.json, a 32 bit file...). `SKSE\KickTheDoor.log` has the same, plus
each file's length and Swing / Impact times. With "Log details" on, the log also says which animation each kick picked.

Packs are read when the game starts: restart the game after changing them.
