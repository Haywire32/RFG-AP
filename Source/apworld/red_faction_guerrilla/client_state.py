"""Pure RF:G state conversion; paid locations never imply received ownership."""
import hashlib
from collections import Counter
from .catalog import SHOP
from . import LOCATIONS
from .progression_catalog import COUNTED_STORIES, FINAL_STORY

BASES = {2:12,3:17,6:16,9:11,11:13,14:14,15:15,25:4}

def identity(seed, team, slot):
    return hashlib.sha256(f"RFG-Shopsanity-v2\n{seed}\n{team}\n{slot}".encode()).hexdigest()

def snapshot(seed, team, slot, data, items, missing, checked):
    owned, weapons, enabled, paid = [0]*62, [False]*96, [0]*62, [0]*62
    start = data['starting_weapon']
    row, definition = start['upgrade'], start['definition']
    if BASES.get(row) != definition:
        raise ValueError('Invalid starting weapon')
    owned[row], weapons[definition] = 1, True
    owned[1] = data.get('remote_charge_base_capacity', 2)-2
    progression = data.get('progression_protocol')
    if progression not in (None, 1, 2): raise ValueError('Unsupported progression protocol')
    sectors = 1
    requirement = data.get('story_missions_required', 20)
    if type(requirement) is not int or not 0<=requirement<=20: raise ValueError('Invalid story requirement')
    counts, salvage = Counter(), {}
    for index, item in enumerate(items):
        sector=data.get('sector_items',{}).get(str(item))
        if sector is not None:
            if type(sector) is not int or not 1<=sector<=6: raise ValueError('Invalid sector item')
            sectors |= 1<<sector
            continue
        family=data['weapon_sequences'].get(str(item))
        if family:
            copy=counts[item]; counts[item]+=1
            # Extra copies (including AP console grants) cannot upgrade a
            # completed family further or stop unrelated checks from syncing.
            if copy>=len(family['indices']): continue
            value=family['indices'][copy]
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
    allowed={id for name,id in LOCATIONS.items() if not name.startswith(('Shop:', 'Story Mission:'))}
    if any(n not in allowed for n in activities): raise ValueError('Invalid activity check')
    return result | set(stories) | set(activities)
