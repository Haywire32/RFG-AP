"""Pure RF:G state conversion; paid locations never imply received ownership."""
import hashlib
from collections import Counter
from .catalog import SHOP
from .destruction_catalog import TARGET_IDS
from .collectible_catalog import COLLECTIBLE_IDS
from .vehicle_catalog import VEHICLES
from . import LOCATIONS, ACTIVITY_INTERNALS
from .progression_catalog import COUNTED_STORIES, FINAL_STORY

BASES = {2:12,3:17,6:16,9:11,11:13,14:14,15:15,25:4}

def identity(seed, team, slot):
    if not isinstance(seed,str) or not seed.strip():
        raise ValueError('Waiting for a valid Archipelago seed name; no progress will be synchronized')
    if type(team) is not int or team<0 or type(slot) is not int or slot<1:
        raise ValueError('Waiting for a valid Archipelago team and slot')
    return hashlib.sha256(f"RFG-Shopsanity-v2\n{seed}\n{team}\n{slot}".encode()).hexdigest()

def snapshot(seed, team, slot, data, items, missing, checked):
    owned, weapons, enabled, paid = [0]*62, [False]*96, [0]*62, [0]*62
    start = data['starting_weapon']
    row, definition = start['upgrade'], start['definition']
    if type(row) is not int or type(definition) is not int or not (
        BASES.get(row) == definition or
        (row == -1 and definition in (3,4,5,6,7,8,9,10,18,19)) or
        (row == 39 and definition == -1)):
        raise ValueError('Invalid starting weapon')
    if row >= 0: owned[row] = 1
    if definition >= 0: weapons[definition] = True
    owned[1] = data.get('remote_charge_base_capacity', 2)-2
    progression = data.get('progression_protocol')
    if progression not in (None, 1, 2): raise ValueError('Unsupported progression protocol')
    sectors = 1
    requirement = data.get('story_missions_required', 20)
    if type(requirement) is not int or not 0<=requirement<=20: raise ValueError('Invalid story requirement')
    counts, salvage = Counter(), {}
    vehicles, ammo = set(), [0]*96
    recharge, power = 0, 0
    for index, item in enumerate(items):
        sector=data.get('sector_items',{}).get(str(item))
        if sector is not None:
            if type(sector) is not int or not 1<=sector<=6: raise ValueError('Invalid sector item')
            sectors |= 1<<sector
            continue
        vehicle=data.get('vehicle_items',{}).get(str(item))
        if vehicle is not None:
            if vehicle not in VEHICLES.values(): raise ValueError('Invalid vehicle item')
            vehicles.add(vehicle)
            continue
        backpack=data.get('backpack_upgrade_items',{}).get(str(item))
        if backpack is not None:
            if backpack == 'Progressive Backpack Recharge': recharge=min(5,recharge+1)
            elif backpack == 'Progressive Backpack Power': power=min(5,power+1)
            else: raise ValueError('Invalid backpack upgrade')
            continue
        family=data['weapon_sequences'].get(str(item))
        if family:
            copy=counts[item]; counts[item]+=1
            # Extra copies (including AP console grants) cannot upgrade a
            # completed family further or stop unrelated checks from syncing.
            if copy>=len(family['indices']): continue
            value=family['indices'][copy]
            if data.get('protocol_version',0)>=6 and 103 <= value <= 118:
                ammo[value-100]=min(5,ammo[value-100]+1)
                continue
            if data.get('protocol_version',0)>=6 and 203 <= value <= 218:
                command, value = 5, value-200
            else:
                command=7 if family['name']=='Progressive Remote Charge Capacity' else 5 if family['name']=='Gauss Rifle Registry Test' else 2
        else:
            direct=data['direct_items'].get(str(item))
            if direct is None: raise ValueError(f'Unknown RF:G item {item}')
            command, value=direct['command'],direct['value']
        if command==1:
            if not 0<value<=30000: raise ValueError('Invalid salvage receipt')
            salvage[str(index)]=value
        elif command in (2,3,4):
            if not 0<=value<62: raise ValueError('Invalid upgrade')
            owned[value]=owned[value]+1 if command==2 and family else 1
        elif command==5:
            if not 0<=value<96: raise ValueError('Invalid weapon')
            weapons[value]=True
        elif command==7: owned[1]=max(owned[1],value-2)
        else: raise ValueError(f'Unknown item command {command}')
    if progression: owned[18]=1 # open overworld, including irradiated roads
    for row,definition in BASES.items():
        if weapons[definition]: owned[row]=max(1,owned[row])
        if owned[row]: weapons[definition]=True
    for row,value in enumerate(owned):
        maximum=10 if row==1 else 2 if row in (0,2,3,6,9,11,14,15,26) else 1
        if not 0<=value<=maximum: raise ValueError(f'Invalid ownership level at row {row}')
    for (row,level),location in SHOP.items():
        if location in missing or location in checked: enabled[row]|=1<<level
        if location in checked: paid[row]|=1<<level
    result = dict(protocol=3 if progression else 2,session=identity(seed,team,slot),start_definition=start['definition'],
                start_row=start['upgrade'],enabled=enabled,checked=paid,owned=owned,weapons=weapons,salvage=salvage)
    if data.get('protocol_version',0)>=6:
        collectibles=data.get('collectible_checks',[])
        if not isinstance(collectibles,list) or any(type(i) is not int or i not in COLLECTIBLE_IDS for i in collectibles):
            raise ValueError('Invalid collectible catalog')
        result.update(features_version=6, vehicles=sorted(vehicles), ammo=ammo,
                      backpack_recharge=recharge, backpack_power=power,
                      shop_tiers=True,
                      collectible_checks=sorted(set(collectibles) & (set(missing)|set(checked))))
    spawn_costs=data.get('vehicle_spawn_costs',False)
    purchase=data.get('gunship_purchase',False)
    purchase_cost=data.get('gunship_purchase_cost',1000)
    if type(spawn_costs) is not bool or type(purchase) is not bool or type(purchase_cost) is not int or not 0<=purchase_cost<=30000:
        raise ValueError('Invalid garage settings')
    result.update(vehicle_spawn_costs=spawn_costs, gunship_purchase=purchase, gunship_purchase_cost=purchase_cost)
    targets=data.get('destruction_checks',[])
    if not isinstance(targets,list) or any(type(t) is not int or t not in TARGET_IDS for t in targets):
        raise ValueError('Invalid destruction catalog')
    if targets and not progression: raise ValueError('Destruction checks require sector progression')
    result['destruction_checks']=sorted(set(targets) & (set(missing) | set(checked)))
    result['checked_targets']=sorted(set(targets) & set(checked))
    visibility=data.get('shop_reward_visibility',True)
    if type(visibility) is not bool: raise ValueError('Invalid shop reward visibility')
    result['shop_reward_visibility']=visibility
    if progression:
        result['progression'] = dict(version=progression,sectors=sectors,required=requirement,
                                     stories=sorted(set(checked) & (COUNTED_STORIES | {867530217, FINAL_STORY})))
    return result

def journal_checks(journal, session):
    if journal['version']!=2 or journal['session']!=session: raise ValueError('Journal session mismatch')
    masks=journal['purchased']
    if len(masks)!=62: raise ValueError('Invalid purchase journal')
    result={location for (row,level),location in SHOP.items() if masks[row] & (1<<level)}
    stories=journal.get('stories',[])
    if any(not 867530200<=n<=867530221 for n in stories): raise ValueError('Invalid story check')
    activities=journal.get('activities',[])
    allowed=set(ACTIVITY_INTERNALS)
    if any(n not in allowed for n in activities): raise ValueError('Invalid activity check')
    targets=journal.get('destroyed_targets',[])
    if any(type(t) is not int or t not in TARGET_IDS for t in targets): raise ValueError('Invalid destroyed target')
    completed=journal.get('destruction_checks',[])
    if any(type(t) is not int or t not in targets for t in completed): raise ValueError('Invalid destruction check')
    collected=journal.get('collected_objects',[])
    collectibles=journal.get('collectible_checks',[])
    if any(type(t) is not int or t not in COLLECTIBLE_IDS for t in collected): raise ValueError('Invalid collectible history')
    if any(type(t) is not int or t not in collected for t in collectibles): raise ValueError('Invalid collectible check')
    return result | set(stories) | set(activities) | set(completed) | set(collectibles)
