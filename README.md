# Kick the Door

SKSE plugin for Skyrim SE / AE: kick doors open, or blast them open with Unrelenting Force. Lockpicking stays as it is.

- **Kick**: look at a door and hold **G** (changeable, gamepad too). Your character turns to the door, steps in and kicks it. Unlocked doors fly open with one kick; locked ones need more stamina the harder the lock is. Novice to Adept locks break with one kick, Expert and Master locks take two (a weakened door stays weakened, saved with the game).
- **Unrelenting Force**: the first door the shout's wave reaches bursts open; a locked one only if the shout had enough words for its lock (Fus / Fus Ro / Fus Ro Dah per lock level).
- The door bursts open faster than usual and away from you, whichever way it normally opens. Whoever stands right behind it is knocked over. A kicked-in load door takes you through.
- Own kick animation (played by the plugin, no Nemesis / Pandora / OAR needed) and door impact sound.
- Never forced: locks that need a key, quest doors, barred doors (door replacers like Glorious Doors of Skyrim whose doors unbar themselves before they swing), display cases, hatches and trap triggers. Each lock level can be switched off.
- Breaking an owned lock in front of witnesses is trespassing, as picking it is.
- Menu and messages in English, French, German, Italian, Spanish, Polish, Russian, Japanese and Chinese.

Settings: SKSE Menu Framework > Kick the Door, or `SKSE\Plugins\KickTheDoor.ini`.

Requires SKSE and Address Library for SKSE Plugins; SKSE Menu Framework 3 for the in-game menu. KickTheDoor.esp is ESL flagged and only holds the impact sound.
