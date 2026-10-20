### Red Faction Guerrilla Archipelago randomizer
This mod is a randomizer for Red Faction Guerrilla Re-Mars-tered edition on steam. Sector unlocks are randomized and the goal is to beat a set amount of story missions to unlock the finale "Mars Attacks".

The mod supports up to 1059 checks.

### Location checks
- Story missions
- Guerrilla actions:
  - Demolition Masters
  - Transporter
  - House Arrest
  - Guerrilla Raid
  - Heavy Metal
  - Collateral Damage
- Shopsanity - all purchases from the upgrade bench are checks
- 300 ore deposits, 54 billboards, 36 radio tags and 419 supply crates as optional checks

### Item rewards
- Weapons and progressive upgrades
- Backpacks progressive upgrades
- Hammer skins
- Salvage
- Character upgrades and unlocks (armor, fast travel, and so on)
- 28 vehicles
- EDF gunship that you can pilot

### Download
[Download can be found here.](https://github.com/Haywire32/RFG-AP/releases)

### How to install
Install Archipelago version 0.6.7

Download the files. Install los.apworld by double-clicking it

Fill out the yaml and make a seed like usual in Archipelago.

Drop "dinput8.dll" into steamapps\common\Red Faction Guerrilla Re-MARS-tered

Open Archipelago Launcher - run "Red Faction Guerrilla Client"

Connect to the Archipelago server and enter slot name, then launch the game normally on Steam.

### Feedback or other questions
I'd love feedback in the [Red Faction Guerilla channel](https://discord.com/channels/731205301247803413/1537482950676058262) in the [Archipelago discord](https://discord.gg/8Z65BR2)

The current version is still unstable and most likely has bugs.

### Disclaimer
I wrote the original mod manually, from version 0.1 through 0.5. I used RSL as the foundation for the original game integration and worked on its C++ code. I used Ghidra to disassemble and reverse engineer the game’s code and identify relevant functions, and Visual Studio to develop and build the mod.

I wrote the initial game hooks, Archipelago client and gear randomization. I also wrote the original shopsanity implementation, with AI later helping to debug and fix it.

ChatGPT/Codex has since helped clean up and restructure the code, rework the hooks and client, and write most of the code for newer features such as the garage and map trackers. Parts of my original implementation have been rewritten or replaced during this work.

I handle item and logic mapping, gameplay design, balancing and in-game testing, with feedback from players. I spend many hours testing, adjusting and retesting changes before release.

I review the code, but I’m not claiming to be an expert in every part of it. The source is public for anyone to inspect. I’m responsible for what I release, supporting players and fixing bugs.
