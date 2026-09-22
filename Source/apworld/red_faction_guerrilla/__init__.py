from BaseClasses import Item, ItemClassification, Location, Region
from worlds.AutoWorld import World
from dataclasses import dataclass
from Options import PerGameCommonOptions, Range, Toggle
from .progression_catalog import SECTORS, SECTOR_ITEMS, STORY_SECTORS, ACTIVITY_SECTORS, COUNTED_STORIES, FINAL_STORY
from .public_yaml import install_template_export

install_template_export()

class StoryMissionsRequired(Range):
    """Distinct playable story missions required before Mars Attacks opens.
    Includes Welcome to Mars; excludes Marauder Temple and Mars Attacks itself.
    No sector item is required for the finale. 20 requires every preceding playable mission.
    """
    display_name = "Story missions required for finale"
    range_start = 0
    range_end = 20
    default = 20

class StartWithFastTravel(Toggle):
    """Start with Guerrilla Express. All safehouses are available from the start."""
    display_name = "Start with fast travel"

@dataclass
class RFGOptions(PerGameCommonOptions):
    story_missions_required: StoryMissionsRequired
    start_with_fast_travel: StartWithFastTravel

from worlds.LauncherComponents import Component, Type, components, launch

def launch_rfg_client(*args):
    from .client import run_client
    launch(run_client, name="Red Faction Guerrilla Client", args=args)

components.append(Component("Red Faction Guerrilla Client", func=launch_rfg_client, component_type=Type.CLIENT,
                            supports_uri=True, game_name="Red Faction: Guerrilla Re-Mars-tered"))

GAME_NAME = "Red Faction: Guerrilla Re-Mars-tered"
BASE_ID = 867530000

WEAPON_FAMILIES = {
    "Progressive Jetpack": (BASE_ID + 1011, [0, 0]),
    "Progressive Remote Charges": (BASE_ID + 1001, [2, 2]),
    "Progressive Arc Welder": (BASE_ID + 1002, [3, 3, 4, 5]),
    "Progressive Grinder": (BASE_ID + 1003, [6, 6, 7, 8]),
    "Progressive Proximity Mines": (BASE_ID + 1004, [9, 9, 10]),
    "Progressive Rocket Launcher": (BASE_ID + 1005, [11, 11, 12, 13]),
    "Progressive Thermobaric Rockets": (BASE_ID + 1006, [14, 14]),
    "Progressive Nano Rifle": (BASE_ID + 1007, [15, 15, 16]),
    "Reconstructor": (BASE_ID + 1008, [39]),
    "Progressive Armor": (BASE_ID + 1009, [26, 26]),
    # Unlike the weapon's ownership/upgrades, these values are the actual
    # gameplay carrying limit sent through IPC command 7.
    "Progressive Remote Charge Capacity": (BASE_ID + 1010, [3, 4, 5, 6, 7, 8, 9, 10, 11, 12]),
}

# These seven upgrade-backed weapons have both a confirmed inventory definition
# and a confirmed ownership row. Reconstructor is intentionally excluded because
# it cannot complete the destructive objectives in the playable intro.
STARTING_WEAPONS = {
    "Progressive Remote Charges": {"name": "Remote Charges", "definition": 12, "upgrade": 2},
    "Progressive Arc Welder": {"name": "Arc Welder", "definition": 17, "upgrade": 3},
    "Progressive Grinder": {"name": "Grinder", "definition": 16, "upgrade": 6},
    "Progressive Proximity Mines": {"name": "Proximity Mines", "definition": 11, "upgrade": 9},
    "Progressive Rocket Launcher": {"name": "Rocket Launcher", "definition": 13, "upgrade": 11},
    "Progressive Thermobaric Rockets": {"name": "Thermobaric Rocket Launcher", "definition": 14, "upgrade": 14},
    "Progressive Nano Rifle": {"name": "Nano Rifle", "definition": 15, "upgrade": 15},
}

DIRECT_ITEMS = {
    # Generic upgrade dispatcher (command 2).
    "Personnel Detector": (BASE_ID + 1017, 2, 17),
    # Radiation protection is granted with the open-overworld policy.
    "Ore Additive": (BASE_ID + 1020, 2, 28),
    "Guerrilla Express": (BASE_ID + 1021, 2, 29),
    "Salvage Collector": (BASE_ID + 1022, 2, 30),
    "Quantum Multiplier": (BASE_ID + 1023, 2, 31),

    # Permanent backpack ownership (command 3).
    "Thrust Backpack": (BASE_ID + 1030, 3, 53),
    "Fleetfoot Backpack": (BASE_ID + 1031, 3, 54),
    "Stealth Backpack": (BASE_ID + 1032, 3, 55),
    "Vision Backpack": (BASE_ID + 1033, 3, 56),
    "Firepower Backpack": (BASE_ID + 1034, 3, 57),
    "Concussion Backpack": (BASE_ID + 1035, 3, 58),
    "Heal Backpack": (BASE_ID + 1036, 3, 59),
    "Rhino Backpack": (BASE_ID + 1037, 3, 60),
    "Tremor Backpack": (BASE_ID + 1038, 3, 61),

    # Re-Mars-tered hammer ownership (command 4).
    "Stonebreaker": (BASE_ID + 1019, 4, 20),
    "Shattermaster": (BASE_ID + 1024, 4, 21),
    "Gold Breaker": (BASE_ID + 1040, 4, 40),
    "Facecrusher": (BASE_ID + 1041, 4, 41),
    "Bronze Crusher": (BASE_ID + 1042, 4, 42),
    "Silver Master": (BASE_ID + 1043, 4, 43),
    "Gold Breaker Alpha": (BASE_ID + 1044, 4, 44),
    "Titanium Hammer": (BASE_ID + 1045, 4, 45),
    "Skull Digger": (BASE_ID + 1046, 4, 46),
    "War Hammer": (BASE_ID + 1047, 4, 47),
    "Battle Axe": (BASE_ID + 1048, 4, 48),
    "Bloody Bat": (BASE_ID + 1049, 4, 49),
    "Stun Baton": (BASE_ID + 1050, 4, 50),
    "Plastic Hammer": (BASE_ID + 1051, 4, 51),
    "Ostrich": (BASE_ID + 1052, 4, 52),

    # Durable weapon-locker registration by weapon-definition index (command 5).
    "EDF Pistol": (BASE_ID + 1060, 5, 3),
    "Assault Rifle": (BASE_ID + 1061, 5, 4),
    "Peacekeeper": (BASE_ID + 1062, 5, 5),
    "Enforcer": (BASE_ID + 1063, 5, 6),
    "Gauss Rifle": (BASE_ID + 1064, 5, 7),
    "Sniper Rifle": (BASE_ID + 1065, 5, 8),
    "Rail Driver": (BASE_ID + 1066, 5, 9),
    "Singularity Bomb": (BASE_ID + 1067, 5, 10),
    "Shotgun": (BASE_ID + 1068, 5, 18),
    "Gutter": (BASE_ID + 1069, 5, 19),

}

SALVAGE_ITEM = "250 Salvage"
ITEMS = {name: item_id for name, (item_id, _) in WEAPON_FAMILIES.items()}
ITEMS.update({name: item_id for name, (item_id, _, _) in DIRECT_ITEMS.items()})
# 1100 remains reserved for historical 100 Salvage receipts.
ITEMS[SALVAGE_ITEM] = BASE_ID + 1101
ITEMS.update(SECTOR_ITEMS)

TRANSPORTER_NUMBERS = (1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 13, 14, 15, 16, 17, 18, 19)
HEAVY_METAL_NUMBERS = (1, 2, 4, 5, 6, 7, 8, 9, 10)

STORY_MISSIONS = (
    ("Welcome To Mars", "Tutorial.str2_pc"),
    ("Better Red Than Dead", "Intro 1.str2_pc"),
    ("Ambush", "Intro 2.str2_pc"),
    ("Start Your Engines", "We know where you are.str2_pc"),
    ("Rallying Point", "Friends, Martians, Countrymen.str2_pc"),
    ("Industrial Revolution", "Walker, Martian Ranger.str2_pc"),
    ("Ultor Echo", "PartyTime.str2_pc"),
    ("Ashes To Ashes...", "Death From Above.str2_pc"),
    ("Emergency Response", "Refugee Truck.str2_pc"),
    ("Catch And Release", "Highway To Hell.str2_pc"),
    ("Air Traffic Control", "Start Your Engines.str2_pc"),
    ("Access Denied", "Traffic Jam.str2_pc"),
    ("Blitzkrieg", "Tank Attack.str2_pc"),
    ("The Guns Of Tharsis", "Guns of Tharsis.str2_pc"),
    ("Death By Committee", "Death By Committee.str2_pc"),
    ("The Dogs Of War", "Sniper Hunter.str2_pc"),
    ("Hammer Of The Gods", "Save the Guerrilla Camp.str2_pc"),
    ("Marauder Temple", "Marauder Temple.str2_pc"),
    ("Emergency Broadcast System", "Emergency Broadcast System.str2_pc"),
    ("Manual Override", "Ants Vs Magnifying Glass.str2_pc"),
    ("Guerrillas At The Gates", "Assault the EDF Central Command.str2_pc"),
    ("Mars Attacks", "Final Mission.str2_pc"),
)

LOCATIONS = {}
LOCATIONS.update({
    f"Demolition Master: ACT_JE_DM_{number:02d}": BASE_ID + number
    for number in range(1, 17)
})
LOCATIONS.update({
    f"Story Mission: {public_name}": BASE_ID + 200 + index
    for index, (public_name, _) in enumerate(STORY_MISSIONS)
})
LOCATIONS.update({
    f"House Arrest: je_ha_{number:02d}": BASE_ID + 49 + number
    for number in range(1, 16)
})
LOCATIONS.update({
    f"Guerrilla Raid: je_ro_{number:02d}": BASE_ID + 69 + number
    for number in range(1, 12)
})
LOCATIONS.update({
    f"Collateral Damage: je_rs_{number:02d}": BASE_ID + 89 + number
    for number in range(1, 8)
})
LOCATIONS.update({
    f"Transporter: je_td_{number:02d}": BASE_ID + 100 + index
    for index, number in enumerate(TRANSPORTER_NUMBERS)
})
LOCATIONS.update({
    f"Heavy Metal: je_cu_{number:02d}": BASE_ID + 120 + index
    for index, number in enumerate(HEAVY_METAL_NUMBERS)
})
LOCATIONS.update({
    "Shop: 9th Remote Charge": BASE_ID + 300,
    "Shop: 10th Remote Charge": BASE_ID + 301,
    "Shop: 11th Remote Charge": BASE_ID + 302,
    "Shop: 12th Remote Charge": BASE_ID + 303,
    "Shop: Arc Welder": BASE_ID + 304,
    "Shop: Arc Welder Ammo": BASE_ID + 305,
    "Shop: Arc Welder 3rd Arc": BASE_ID + 306,
    "Shop: Smart Arc Welder": BASE_ID + 307,
    "Shop: 3rd Remote Charge": BASE_ID + 308,
    "Shop: 4th Remote Charge": BASE_ID + 309,
    "Shop: 5th Remote Charge": BASE_ID + 310,
    "Shop: 6th Remote Charge": BASE_ID + 311,
    "Shop: 7th Remote Charge": BASE_ID + 312,
    "Shop: 8th Remote Charge": BASE_ID + 313,
    "Shop: Remote Charge Ammo": BASE_ID + 315,
    "Shop: Grinder": BASE_ID + 316,
    "Shop: Grinder Ammo": BASE_ID + 317,
    "Shop: Fast Grinder": BASE_ID + 318,
    "Shop: Explosive Grinder Discs": BASE_ID + 319,
    "Shop: Proximity Mines": BASE_ID + 320,
    "Shop: Proximity Mine Ammo": BASE_ID + 321,
    "Shop: Smart Proximity Mines": BASE_ID + 322,
    "Shop: Rocket Launcher": BASE_ID + 323,
    "Shop: Rocket Ammo": BASE_ID + 324,
    "Shop: Multi-Rockets": BASE_ID + 325,
    "Shop: Heat-Seeking Rockets": BASE_ID + 326,
    "Shop: Thermobaric Rockets": BASE_ID + 327,
    "Shop: Thermobaric Ammo": BASE_ID + 328,
    "Shop: Nano Rifle": BASE_ID + 329,
    "Shop: Nano Rifle Ammo": BASE_ID + 330,
    "Shop: Nano Enhancer": BASE_ID + 331,
    "Shop: Personnel Detector": BASE_ID + 332,
    "Shop: Radiation Shielding": BASE_ID + 333,
    "Shop: Stonebreaker": BASE_ID + 334,
    "Shop: Shattermaster": BASE_ID + 335,
    "Shop: Level 1 Armor": BASE_ID + 336,
    "Shop: Level 2 Armor": BASE_ID + 337,
    "Shop: Ore Additive": BASE_ID + 338,
    "Shop: Guerrilla Express": BASE_ID + 339,
    "Shop: Salvage Collector": BASE_ID + 340,
    "Shop: Quantum Multiplier": BASE_ID + 341,
    "Shop: Reconstructor": BASE_ID + 342,
    "Shop: Gold Breaker": BASE_ID + 343,
    "Shop: Facecrusher": BASE_ID + 344,
    "Shop: Bronze Crusher": BASE_ID + 345,
    "Shop: Silver Master": BASE_ID + 346,
    "Shop: Gold Breaker Alpha": BASE_ID + 347,
    "Shop: Titanium Hammer": BASE_ID + 348,
    "Shop: Skull Digger": BASE_ID + 349,
    "Shop: War Hammer": BASE_ID + 350,
    "Shop: Battle Axe": BASE_ID + 351,
    "Shop: Bloody Bat": BASE_ID + 352,
    "Shop: Stun Baton": BASE_ID + 353,
    "Shop: Plastic Hammer": BASE_ID + 354,
    "Shop: Ostrich": BASE_ID + 355,
    "Shop: Thrust Backpack": BASE_ID + 356,
    "Shop: Fleetfoot Backpack": BASE_ID + 357,
    "Shop: Stealth Backpack": BASE_ID + 358,
    "Shop: Vision Backpack": BASE_ID + 359,
    "Shop: Firepower Backpack": BASE_ID + 360,
    "Shop: Concussion Backpack": BASE_ID + 361,
    "Shop: Heal Backpack": BASE_ID + 362,
    "Shop: Rhino Backpack": BASE_ID + 363,
    "Shop: Tremor Backpack": BASE_ID + 364,
})


class RFGItem(Item):
    game = GAME_NAME


class RFGLocation(Location):
    game = GAME_NAME


# Stable numeric IDs remain the protocol identities; presentation uses sector
# numbering so archive codes never have to be read by the player.
LEGACY_ACTIVITIES = {name.split(': ',1)[1].lower():id for name,id in LOCATIONS.items()
                     if not name.startswith(('Shop:', 'Story Mission:'))}
ACTIVITY_INTERNALS = {id:name.removeprefix('act_') for name,id in LEGACY_ACTIVITIES.items()}
activity_numbers = {}
for old_name,id in list(LOCATIONS.items()):
    if id not in ACTIVITY_INTERNALS: continue
    family=old_name.split(': ',1)[0]
    sector=ACTIVITY_SECTORS[ACTIVITY_INTERNALS[id]]
    key=(family,sector)
    activity_numbers[key]=activity_numbers.get(key,0)+1
    del LOCATIONS[old_name]
    LOCATIONS[f'{family} - {sector} {activity_numbers[key]}']=id
LOCATIONS['Shop: Jetpack']=BASE_ID+365
LOCATIONS['Shop: Jetpack Recharge']=BASE_ID+366

class RFGWorld(World):
    """Sector access and a configurable story-count finale."""

    game = GAME_NAME
    item_name_to_id = ITEMS
    location_name_to_id = LOCATIONS
    options_dataclass = RFGOptions
    options: RFGOptions

    def generate_early(self) -> None:
        self.starting_family = self.random.choice(tuple(STARTING_WEAPONS))
        self.starting_weapon = STARTING_WEAPONS[self.starting_family]
        if self.options.start_with_fast_travel:
            self.multiworld.push_precollected(self.create_item('Guerrilla Express'))
        self.weapon_sequences = {}
        for name, (item_id, source_sequence) in WEAPON_FAMILIES.items():
            if name == "Progressive Remote Charge Capacity":
                sequence = list(source_sequence)
            elif name == "Progressive Arc Welder":
                sequence = list(source_sequence)
            else:
                base = source_sequence[0]
                later = list(source_sequence[1:])
                self.random.shuffle(later)
                sequence = [base, *later]
            if name == self.starting_family:
                sequence = sequence[1:]
            self.weapon_sequences[str(item_id)] = {
                "name": name,
                "indices": sequence,
            }

    def create_regions(self) -> None:
        menu = Region("Menu", self.player, self.multiworld)
        regions = {sector: Region(sector, self.player, self.multiworld) for sector in SECTORS}
        finale = Region("Finale", self.player, self.multiworld)
        self.multiworld.regions.extend([menu, *regions.values(), finale])
        # The ending has its own entrance: owning the Mount Vogel activity key
        # must never be an extra requirement after reaching the story target.
        menu.connect(finale)
        for sector, region in regions.items():
            menu.connect(region, rule=(lambda state: True) if sector == "Parker" else
                         (lambda state, item=sector+" Sector": state.has(item, self.player)))
        for name, address in LOCATIONS.items():
            if name.startswith("Shop:"):
                sector = "Parker"  # Every upgrade table offers the same paid checks.
            elif name.startswith("Story Mission:"):
                sector = STORY_SECTORS[address]
            else:
                internal = ACTIVITY_INTERNALS[address]
                sector = ACTIVITY_SECTORS[internal]
            region = finale if address == FINAL_STORY else regions[sector]
            region.locations.append(RFGLocation(self.player, name, address, region))
            if address in COUNTED_STORIES:
                event = RFGLocation(self.player, "Completed: "+name, None, region)
                event.place_locked_item(RFGItem("Story Mission Completed", ItemClassification.progression, None, self.player))
                region.locations.append(event)
        victory = RFGLocation(self.player, "Defeat the EDF", None, finale)
        victory.place_locked_item(RFGItem("Victory", ItemClassification.progression, None, self.player))
        finale.locations.append(victory)

    def create_items(self) -> None:
        real_item_count = 0
        for name, (item_id, _) in WEAPON_FAMILIES.items():
            sequence = self.weapon_sequences[str(item_id)]["indices"]
            for _ in sequence:
                self.multiworld.itempool.append(self.create_item(name))
                real_item_count += 1
        for name in DIRECT_ITEMS:
            if name=='Guerrilla Express' and self.options.start_with_fast_travel: continue
            self.multiworld.itempool.append(self.create_item(name))
            real_item_count += 1
        for name in SECTOR_ITEMS:
            self.multiworld.itempool.append(self.create_item(name))
            real_item_count += 1
        for _ in range(len(LOCATIONS) - real_item_count):
            self.multiworld.itempool.append(self.create_item(SALVAGE_ITEM))

    def create_item(self, name: str) -> RFGItem:
        classification = (
            ItemClassification.filler if name == SALVAGE_ITEM
            else ItemClassification.progression
        )
        return RFGItem(name, classification, ITEMS[name], self.player)

    def set_rules(self) -> None:
        # Sector access must never depend on paying for shop checks.
        for location in self.get_locations():
            if location.name.startswith('Shop:'):
                location.item_rule=lambda item: item.name not in SECTOR_ITEMS
        requirement = self.options.story_missions_required.value
        finale_rule = lambda state: state.has("Story Mission Completed", self.player, requirement)
        self.get_location("Story Mission: Mars Attacks").access_rule = finale_rule
        self.get_location("Defeat the EDF").access_rule = finale_rule
        self.multiworld.completion_condition[self.player] = lambda state: state.has("Victory", self.player)

    def fill_slot_data(self) -> dict:
        return {
            "protocol_version": 5,
            "shopsanity_protocol": 2,
            "progression_protocol": 2,
            "story_missions_required": self.options.story_missions_required.value,
            "start_with_fast_travel": bool(self.options.start_with_fast_travel),
            "sector_items": {str(item): SECTORS.index(name.removesuffix(" Sector")) for name,item in SECTOR_ITEMS.items()},
            # Gameplay capacity comes from the seed and received items only.
            "remote_charge_base_capacity": 2,
            "starting_weapon": self.starting_weapon,
            "weapon_sequences": self.weapon_sequences,
            "direct_items": {
                **{
                    str(item_id): {
                        "name": name,
                        "command": command,
                        "value": value,
                    }
                    for name, (item_id, command, value) in DIRECT_ITEMS.items()
                },
                str(ITEMS[SALVAGE_ITEM]): {
                    "name": SALVAGE_ITEM, "command": 1, "value": 250
                },
            },
        }

