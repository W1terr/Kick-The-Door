# Kick animation packs

Kick the Door plays its kick animation itself, on the player's skeleton: no Nemesis, Pandora, FNIS or OAR. Anyone can
add their own kick (or several) as an **animation pack**, a plain folder with loose files:

```
Data\Meshes\KickTheDoor\Animations\
    Default\                 the mod's own kick (priority 0)
        kick.hkx
        config.json
    Sprint\                  the mod's sprint kick (priority 0, condition sprinting)
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
- Start close to a standing pose: the animation blends in over 0.08 s. After its last frame it blends back to what
  the game animates over 0.15 s (`blendOut`), so it can end mid-move (a leg still coming down).
- The player gets their controls back once it has blended out.
- Annotations (Blender timeline / pose markers) set the timing:
  - `Impact` (or `HitFrame`): the foot meets the door. Stamina, the lock and the door are handled here.
  - `Swing`: the whoosh sound.
  - `StepStart` and `StepEnd`: the plugin moves the player towards the door (or back, if they stand too close)
    between these, until they stand `standOff` units from it. Put them where a foot steps, so the move doesn't
    look like sliding. The player also turns to face the door by `StepEnd`.
  - Without them: impact at 42 % of the length, swing at 60 % of the impact, the step from 12 % to 70 % of the impact.

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
	"stepStart": 0.1,              // seconds: override StepStart / StepEnd (both or neither)
	"stepEnd": 0.4,
	"standOff": 74,                // units from the door the player is moved to: the foot reaches it at Impact (default 55)
	"blendIn": 0.08,               // seconds (default 0.08)
	"blendOut": 0.4,               // seconds after the last frame (default 0.15)
	"endStep": 10,                 // units the player moves forward while blending out (default 0): an animation
	                               // that steps forward keeps that foot in place instead of sliding it back
	"maxStep": 50,                 // the step towards the target goes this far at most (default 50)
	"lunge": 0,                    // with nobody to aim at, the player is carried this far forward (default 0;
	                               // the sprint kick: 45, the run's momentum)
	"conditions": {                // leave one out = either way
		"sex": "female",           // "male" or "female"
		"weaponDrawn": false,
		"sneaking": true,
		"lockedDoor": true,        // false = only for doors that aren't locked (and kicks at anything else)
		"sprinting": true,         // the player sprinted into the kick
		"races": ["KhajiitRace", "Skyrim.esm|0x13740"]  // editor ids, or "Plugin.esp|0xID" (always works)
	}
}
```

## Which pack plays

For each kick, every pack whose conditions fit is a candidate; the highest priority wins, and among the same priority
the pack with more conditions (so the sprint kick beats the plain one while sprinting). Packs equal in both are mixed:
one animation is picked at random from all of their files. A pack without conditions always fits, so the
mod's own kick (priority 0) plays when nothing else does.

Players can also pick one pack for every kick with `sAnimation = <folder name>` in `SKSE\Plugins\KickTheDoor.ini`
(`auto` = by priority and conditions).

## Checking a pack

The menu's **Status** page lists every pack found: name, author, priority, how many animations loaded, its conditions,
and any file that didn't load and why (a broken config.json, a 32 bit file...). `SKSE\KickTheDoor.log` has the same, plus
each file's length and Swing / Impact times. With "Log details" on, the log also says which animation each kick picked.

Packs are read when the game starts: restart the game after changing them.
