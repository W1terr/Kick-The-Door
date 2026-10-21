# Changelog

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
