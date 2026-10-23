# Kick the Door

SKSE plugin for Skyrim SE / AE: kick doors open, or blast them open with Unrelenting Force, and kick anyone in your way. Lockpicking stays as it is.

- **Kick**: look at a door and press **G** (changeable, gamepad too). Your character steps in and kicks it. Unlocked doors fly open with one kick; locked ones need more stamina the harder the lock is. Novice to Adept locks break with one kick, Expert and Master locks take two by default - stamina, kicks and shout words are set per lock level (a weakened door stays weakened, saved with the game).
- **Kick people**: press the key anywhere else and you kick straight where you face (40 stamina). Whoever the foot reaches takes 2 damage, staggers like from the first word of Unrelenting Force and takes it as an attack. Sprint into the kick and people in light armor or none go down; heavy armor and big creatures only stagger. The kick also works mid-swing or mid-combo: the attack stops, a quicker kick goes in, and attacking after the hit carries the combo on. With a weapon out every kick is that quicker one.
- **Unrelenting Force**: the first door the shout's wave reaches bursts open; a locked one only if the shout had enough words for its lock (Fus / Fus Ro / Fus Ro Dah per lock level).
- The door bursts open faster than usual and away from you, whichever way it normally opens. Whoever stands right behind it is knocked over (2 damage). A kicked-in load door takes you through.
- Kick animations played by the plugin itself, no Nemesis / Pandora / OAR needed: a high right-leg push kick from a guard (based on the Spartan Kick SE animation), and an own door impact sound.
- Never forced: locks that need a key, quest doors, barred doors (door replacers like Glorious Doors of Skyrim whose doors unbar themselves before they swing), display cases, hatches and trap triggers. Each lock level can be switched off.
- Breaking an owned lock in front of witnesses is trespassing, as picking it is.
- Menu and messages in English, French, German, Italian, Spanish, Polish, Russian, Japanese and Chinese.

Settings: SKSE Menu Framework > Kick the Door, or `SKSE\Plugins\KickTheDoor.ini`.

Requires SKSE and Address Library for SKSE Plugins; SKSE Menu Framework 3 for the in-game menu. KickTheDoor.esp is ESL flagged and only holds the impact sound and the kick's hit and damage (no edits to other records).
