# Changelog

## 2.1

(1.5 was released on Nexus as 2.0.)

- No kicks during scenes: while the player is in an OStim Standalone / NG or SexLab scene (their factions; more in [Scenes]
  Factions), and while the game or a mod holds the controls (cutscenes, scripted scenes).
- Key pairs: "Together with" / [Kick] iKey2 (keyboard, mouse) and iGamepadKey2 (gamepad). Hold the kick key or button and
  press the second one (or press both at once) to kick; the one that completes the pair does nothing else meanwhile.
- "Kick only as a lesser power" / [Kick] bKickPower (off by default): Kick is in your Powers and only the power kicks.
  Equipped like any lesser power, the shout key (Z) kicks right away (no casting animation); a hotkey or favorites mod
  casting it kicks too. Turned off, the power is taken away again.
- "Only with a weapon drawn" / [Kick] bOnlyWithWeaponDrawn: kicks only while your weapon or fists are out.
- Beast forms: werewolves and vampire lords no longer kick. A werewolf's power attack breaks the door in front of it; a
  vampire lord breaks it with a power attack or a hostile spell aimed at it (Beast forms section, [Beasts]).

## 1.5

- Kick chests open: look at a locked chest (chests, strongboxes, Dwemer and ruin chests; not wardrobes or bookcases)
  and press the kick key. You are moved in front of it and turned to it, then, the Assassin's Creed way, standing,
  turn a little to the left and drive one powerful kick with the straight right leg into the lock (0.87 s in). Once
  the lock gives (the way a picked lock opens) the chest opens; the rest of the animation plays once you close it.
  Owned chests count as trespassing like doors, and the crack is heard. Locks that need a key never give. "Force
  chest locks" / bForceChests, "Open the chest once it gives" / bOpenForcedChests.
- Chests have their own lock table ("Chest locks" in the menu, [Chests] in the ini): per lock level whether it can be
  forced, the stamina a kick needs (with less you don't kick), the stamina it takes and the kicks it needs (Novice to
  Adept 1, Expert and Master 2 by default; a weakened lock stays weakened, saved with the game). Unrelenting Force
  never opens chests.
- New animation Meshes\KickTheDoor\chest.hkx (1.9 s), played by the plugin like the kick; its "Open" marker sets when
  the chest opens.
- First person kicks: you see your own leg. In first person (without Improved Camera, which shows the whole body
  itself) Meshes\KickTheDoor\kick_1st.hkx and chest_1st.hkx play instead: the leg is the body's own (with its
  armor and boots; drawn for the kick, everything above the waist hidden), the arms go a little out to the sides into
  view. The door kick lifts the foot a little higher there so the hit is in sight; at a chest you look down at the
  lock. Same length and markers as the 3rd person files; with a weapon out the arms keep it (not at chests).
- A lock breaking (door or chest) now sounds like metal giving way instead of the vanilla lockpicking clicks: the
  game's own sword-on-metal hit (WPN_Impact_Blade_Metal_01), 9 dB quieter (KickTheDoor.esp KTD_LockBreakSD).
- Trying to kick without the stamina for it, you pant like when you sprint with none left: the game's own out of
  breath voice line, in your character's voice ("Pant when out of stamina" / [Feedback] bPanting).
- Improved Camera: it draws the whole body in first person itself, so there the first person files' legs play (the
  foot comes into view) with the 3rd person files' upper body and arms; the body is left to Improved Camera.

## 1.4

- New kick for standing and sprinting alike: a high right-leg push kick from a guard (1.33 s, the foot meets the
  target at 0.77 s; based on the Spartan Kick SE animation, mirrored to the right leg and given the plugin's timing
  markers).
- Out of a sprint the run flows into the kick: the animation starts with the knee already coming up (its guard and the
  slow raise are skipped, the foot lands 0.22 s after the press) and the run carries you on from your running speed,
  slowing down evenly until a little after the hit ("Sprint slide": at most 120 units), stopping short of walls and
  people. The game's own sprint ends with the kick, so it doesn't drag you on. The separate sprint
  kick is gone.
- Kick during attacks: the kick key works mid-swing and mid-combo. The attack stops and a shorter, quicker kick plays
  (it starts with the knee coming up, at x1.25, without the long return to standing; the foot lands about 0.3 s after
  the press). After the foot lands, attacking again cuts the kick short and goes straight on with the combo.
- The same quicker kick plays whenever a weapon (or fists, or a spell) is out, attacking or not: in a fight every kick
  is the short one. Sprinting into it, the sprint kick plays. "Kick
  during attacks" / bDuringAttacks. Animation packs: condition "attacking", config comboStart / comboSpeed / comboEnd.
- A kick that lands deals 2 damage (fixed), and so does a forced door to whoever it throws over; the player is the one
  who hurt them (KickTheDoor.esp KTD_KickDamage).
- Forced doors swing open at x1.9 by default (was x1.5).
- One kick animation, Meshes\KickTheDoor\kick.hkx, with its timing built in: the animation packs (Animations\<pack>
  folders, config.json, conditions, sAnimation) and ANIMATIONS.md are gone, and so is the pack list on the Status page.
- The mod no longer touches the camera: no switch to third person for the kick ("In first person" is gone), no camera
  shakes, the player isn't turned to face a door (the step still goes towards it) and switching the view during a
  kick isn't blocked.

## 1.3

- Kick anything: the kick key now kicks on a press (Hold for 0 by default), and away from doors the kick goes straight
  where you face, no turning to anyone or pulling you towards them; whoever the foot reaches in front gets it. It costs
  40 stamina ("Stamina cost") and staggers them like the first word of Unrelenting Force ("Stagger strength"); they
  take it as an attack (a hostile spell the player casts, KickTheDoor.esp KTD_KickSpell), so combat and assault work
  as with any hit.
- Sprint kicks: kicked while you sprint, people in light armor or none fall over ("Sprint kicks knock people down",
  "Knockdown force"); heavy armor, big creatures and those who can't fall are only staggered. Own animation: from the
  running stride straight into the kick with a hop, the run carrying you on (pack Sprint).
- The standing kick is faster: 0.87 s, the foot meets the target at 0.53 s.
- The "Kick animation" choice is gone from the menu (sAnimation in the ini still picks a pack for every kick).
- Doors that can't be forced (key, barred, quest) take the kick with a thud now, instead of no kick.
- Kicks per lock level can be changed (Locks table / i<Level>Kicks, 1 - 5), unlocked doors too.
- Who a kick meets: the nearest body in front (measured to the near side of their body, big creatures are wider and
  taller), in the kick's height band (not someone on a ledge above or down the stairs) and with nothing solid between.
- A door straight ahead is found even when the camera looks past it (3rd person). Door wings are looked up once per
  loaded door instead of on every check.
- Animation packs: condition "sprinting", the more specific pack wins among equal priorities, config maxStep / lunge.

## 1.2

- New kick, heavier (1.07 s, impact at 0.67 s): a light step forward, the knee comes up heavy and a little slow, a hard snap
  into the door with the hips behind it, the foot holds on the door as the hit jolts back, then the leg comes a little
  down and the body settles.
- Fixed: at the stockade gate (and other doors whose bounds hold more than the door) the character sometimes turned
  their back to it and it swung open the wrong way. The door's plane now goes through its hinges.
- Fixed: the wings of doors whose model lies on its side in the file (Solitude jail door) weren't recognized (the
  knob was taken for the wing), so they didn't swing away from the hit. Wings are found where the door stands.
- Animation packs: StepStart / StepEnd markers (when the player is moved to the door, also backwards when too
  close), config.json standOff, blendIn, blendOut (now after the last frame) and endStep.

## 1.1

- Kick animation packs: every folder in Meshes\KickTheDoor\Animations is a pack with one or more .hkx files (one picked
  at random per kick) and an optional config.json (name, author, priority, conditions: sex, weapon drawn, sneaking,
  locked door, races; timing; arms with a weapon drawn). The highest priority pack that fits plays; "Kick animation"
  in the menu (sAnimation) picks one for every kick. The Status page lists the packs and their problems. Guide:
  ANIMATIONS.md.
- The mod's own kick moved to Meshes\KickTheDoor\Animations\Default.

## 1.0

- First release: kick doors open (hold key, stamina per lock level, two kicks for Expert and Master locks, unlocked
  doors with one kick), Unrelenting Force breaching (words per lock level), own kick animation and door impact sound,
  doors burst open faster and away from the hit, knockdown behind the door, crime and noise, quest / key / barred doors
  left alone, SKSE Menu Framework settings, 9 languages.
