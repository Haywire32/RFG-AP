"""RF:G integration hosted by the standard Archipelago client interface."""
import asyncio
import json
import logging
import re
import time
import uuid
from pathlib import Path
from CommonClient import CommonContext, ClientCommandProcessor, server_loop, get_base_parser, handle_url_arg
import Utils
from MultiServer import mark_raw
from . import GAME_NAME, LOCATIONS, LEGACY_ACTIVITIES
from .catalog import STORY
from .client_state import snapshot, journal_checks, identity
from .client_settings import discover_game_folder
from .rsl_pipe import RslPipe
from .progression_catalog import COUNTED_STORIES, FINAL_STORY, SECTORS
from NetUtils import ClientStatus

logger=logging.getLogger('Client')
ACTIVITIES=LEGACY_ACTIVITIES
ACTIVITY=re.compile(r'AP_(?:DEMO|HOUSE_ARREST|GUERRILLA_RAID|COLLATERAL_DAMAGE|TRANSPORTER|HEAVY_METAL)_CHECK\|([A-Za-z0-9_]+)')

class Commands(ClientCommandProcessor):
    def _cmd_progress(self):
        """Show unlocked sectors and the story requirement for Mars Attacks."""
        state=self.ctx.last_snapshot or self.ctx.current_snapshot
        if not state or 'progression' not in state:
            self.output('Connect a sector-progression seed and RF:G first.'); return
        p=state['progression']
        done=len((self.ctx.local_checks | set(self.ctx.checked_locations)) & COUNTED_STORIES)
        self.output('Open sectors: '+', '.join(s for i,s in enumerate(SECTORS) if p['sectors'] & (1<<i)))
        suffix=' Mount Vogel Sector is also required for this older seed.' if p['version']==1 else ''
        self.output(f'Story missions: {done}/{p["required"]}.'+suffix)
        self.output(self.ctx.game_status)

    def _cmd_status(self):
        """Show whether this client is connected to the game."""
        self.output(self.ctx.game_status)

    @mark_raw
    def _cmd_game_folder(self, folder: str):
        """Set the RF:G installation folder (the folder containing rfg.exe)."""
        path=Path(folder.strip('"'))
        if not (path/'rfg.exe').is_file():
            self.output('That folder does not contain rfg.exe.'); return False
        self.ctx.game_folder=path
        Utils.persistent_store('rfg_archipelago','game_folder',str(path))
        self.ctx.reset_log_reader()
        self.output('RF:G folder updated.'); return True

class RFGContext(CommonContext):
    game=GAME_NAME
    items_handling=7
    want_slot_data=True
    command_processor=Commands

    def __init__(self, address=None, password=None, game_folder=None):
        super().__init__(address,password)
        self.game_folder=Path(game_folder) if game_folder else None
        self.slot_data=None
        self.history_ready=False
        self.pipe=RslPipe()
        self.last_snapshot=None
        self.current_snapshot=None
        self.game_status='Game not connected'
        self.last_send=0
        self.last_notice=None
        self.local_checks=set()
        self.session=None
        self.goal_reported=False
        self.hud_events=[]
        self.hud_nonce=uuid.uuid4().hex
        self.hud_sequence=0
        self.hud_session=None
        self.finale_notices=set()
        self.reset_log_reader()

    def reset_log_reader(self):
        self.log_offset=0
        self.log_session=None
        self.log_partial=''

    def reset_server_state(self):
        super().reset_server_state()
        # CommonClient may resend cached checks during Connected, before our
        # callback runs. Recover same-run checks from its journal after binding
        # the new RoomInfo instead of carrying those caches across connections.
        self.seed_name=None
        self.slot_data=None
        self.history_ready=False
        self.locations_checked.clear()
        self.locations_scouted.clear()
        self.checked_locations.clear()
        self.missing_locations.clear()
        self.finished_game=False
        self.local_checks.clear()
        self.session=None
        self.last_snapshot=None
        self.current_snapshot=None
        self.goal_reported=False
        self.hud_events.clear()
        self.hud_session=None
        self.finale_notices.clear()
        self.reset_log_reader()

    async def server_auth(self, password_requested=False):
        if password_requested and not self.password: await super().server_auth(password_requested)
        await self.get_username()
        await self.send_connect()

    def make_gui(self):
        class RFGManager(super().make_gui()):
            base_title='Archipelago Red Faction Guerrilla Client'
        return RFGManager

    def set_game_status(self, text):
        if text==self.game_status: return
        self.game_status=text
        # Keep game connectivity visible even while the AP server is connected.
        if getattr(self,'ui',None):
            self.ui.base_title='Archipelago Red Faction Guerrilla Client - '+text
            self.ui.title=self.ui.base_title

    def on_package(self, cmd, args):
        if cmd=='RoomInfo':
            seed=args.get('seed_name')
            if not isinstance(seed,str) or not seed.strip():
                self.history_ready=False
                raise ValueError('Server did not supply a valid seed name; progress synchronization is blocked')
            if self.seed_name and self.seed_name!=seed:
                self.history_ready=False
                raise ValueError('Server changed seeds during a connection; reconnect before synchronizing progress')
            self.seed_name=seed
        elif cmd=='Connected':
            session=identity(self.seed_name,self.team,self.slot)
            logger.info('RF:G seed %s, team %s, slot %s; progress journal %s',self.seed_name,self.team,self.slot,session)
            if self.checked_locations:
                logger.info('This server slot already has %s completed checks. Starting a new campaign does not reset this room or its received items.',len(self.checked_locations))
                if FINAL_STORY in self.checked_locations:
                    logger.warning('This room already has the final mission checked. Use a fresh room and a new campaign to test progression from the start.')
            connection_identity=(self.seed_name,self.team,self.slot)
            if self.hud_session!=connection_identity:
                self.hud_events.clear()
                self.hud_session=connection_identity
            self.slot_data=args['slot_data']
            self.pipe.select('RFGArchipelago', fallback='RSLMainPipe' if self.slot_data.get('progression_protocol')!=2 else None)
            self.history_ready=False
            self.last_snapshot=None
            self.goal_reported=False
            asyncio.create_task(self.send_msgs([{'cmd':'Sync'}, {'cmd':'Get','keys':['rfg_shopsanity_sync_barrier']}]))
        elif cmd=='Retrieved' and 'rfg_shopsanity_sync_barrier' in args['keys']:
            self.history_ready=True

    def hud_notice(self, text):
        # Limit native text length, strip control characters and game markup.
        text=''.join(' ' if ord(c)<32 or c in '[]' else c for c in text)[:180]
        self.hud_sequence+=1
        self.hud_events.append(dict(id=f'{self.hud_nonce}:{self.hud_sequence}',text=text))
        if len(self.hud_events)>256: self.hud_events.pop(0)

    def on_print_json(self, args):
        super().on_print_json(args)
        if args.get('type')!='ItemSend' or self.slot is None: return
        item=args.get('item')
        if hasattr(item,'_asdict'): item=item._asdict()
        elif isinstance(item,(list,tuple)): item=dict(zip(('item','location','player','flags'),item))
        if not isinstance(item,dict): return
        sender,receiver=item['player'],args['receiving']
        if self.slot not in (sender,receiver): return
        name=self.item_names.lookup_in_slot(item['item'],receiver)
        if sender==receiver==self.slot:
            self.hud_notice(f'You found {name}')
        elif receiver==self.slot:
            self.hud_notice(f'You received {name} from {self.player_names[sender]}')
        else:
            self.hud_notice(f'You sent {name} to {self.player_names[receiver]}')

    def queue_finale_notice(self, state):
        progression=state.get('progression')
        if not progression: return
        stories=set(progression['stories']) | self.local_checks
        if FINAL_STORY in stories: return
        if progression.get('version',1)==1 and not progression['sectors'] & (1<<6): return
        if len(stories & COUNTED_STORIES)<progression['required']: return
        session=state['session']
        if session in self.finale_notices: return
        # Stable ID lets the running game deduplicate this even if the client
        # is restarted. Ownership and this notice travel in the same snapshot.
        self.hud_events.append(dict(id='finale-unlocked:'+session,
                                   text='Final mission unlocked: Mars Attacks! Look for its mission marker on the map.'))
        self.finale_notices.add(session)

    def read_checks(self, session):
        for folder in (self.game_folder/'RFGArchipelago'/'Shopsanity',self.game_folder/'RSL'/'AP'/'Shopsanity'):
            journal=folder/(session+'.json')
            if journal.exists(): self.local_checks |= journal_checks(json.loads(journal.read_text()),session)
        # Activities still use existing native log markers. Only accept markers
        # after a READY line binds this log session to the connected seed/slot.
        folder='RFGArchipelago' if self.slot_data.get('progression_protocol')==2 else 'RSL'
        path=self.game_folder/folder/'Logs'/'General Log.log'
        if not path.exists(): return
        if path.stat().st_size<self.log_offset: self.reset_log_reader()
        with path.open('rb') as f:
            f.seek(self.log_offset); chunk=f.read(1024*1024); self.log_offset=f.tell()
        lines=(self.log_partial+chunk.decode('utf-8',errors='replace')).split('\n')
        self.log_partial=lines.pop()
        for line in lines:
            ready=re.search(r'AP SHOP V2 READY: session=([0-9a-f]{64})',line)
            if ready: self.log_session=ready[1]
            if self.log_session!=session: continue
            found=ACTIVITY.search(line)
            if not self.slot_data.get('progression_protocol') and found and found[1].lower() in ACTIVITIES:
                self.local_checks.add(ACTIVITIES[found[1].lower()])
            # Compatibility with the installed shopsanity-v2 DLL, until the
            # native story journal candidate is installed on a game restart.
            if not self.slot_data.get('progression_protocol'):
                found=re.search(r'AP_STORY_CHECK\|(.+?)\.str2_pc',line)
                if found and found[1] in STORY: self.local_checks.add(STORY[found[1]])

    async def report_goal(self):
        if not self.seed_name or self.slot is None or not self.slot_data or not self.history_ready: return
        if self.slot_data.get('progression_protocol') in (1,2) and not self.goal_reported and FINAL_STORY in (self.local_checks | set(self.checked_locations)):
            await self.send_msgs([{'cmd':'StatusUpdate','status':ClientStatus.CLIENT_GOAL}])
            self.goal_reported=True
            if FINAL_STORY in self.local_checks:
                logger.info('Mars Attacks completed. Archipelago goal reached!')
            else:
                logger.info('Archipelago goal restored from this room\'s existing final-mission check.')

    async def poll_game(self):
        if self.slot is None or not self.history_ready or not self.game_folder: return
        session=identity(self.seed_name,self.team,self.slot)
        if self.session!=session:
            self.session=session; self.local_checks.clear(); self.reset_log_reader()
        # Completion is durable on disk. Report it even if the game is closed,
        # its pipe is broken, or an ownership update cannot be applied.
        self.read_checks(session)
        valid=self.local_checks & (set(self.missing_locations) | set(self.checked_locations))
        await self.check_locations(valid)
        self.locations_checked |= valid
        await self.report_goal()
        state=snapshot(self.seed_name,self.team,self.slot,self.slot_data,
                       [i.item for i in self.items_received],self.missing_locations,self.checked_locations)
        self.current_snapshot=state
        self.queue_finale_notice(state)
        batch=self.hud_events[:32]
        if batch: state['notices']=batch
        if state!=self.last_snapshot or time.monotonic()-self.last_send>10:
            await asyncio.to_thread(self.pipe.send,state)
            del self.hud_events[:len(batch)]
            if self.last_snapshot is None: logger.info('Connected to RF:G. Ownership synchronized.')
            self.last_snapshot=state; self.last_send=time.monotonic()
        self.set_game_status('Game connected')
        self.last_notice=None

    async def watch_game(self):
        while not self.exit_event.is_set():
            try:
                await self.poll_game()
            except Exception as ex:
                notice=str(ex)
                if notice!=self.last_notice: logger.info('Waiting for RF:G: %s',notice); self.last_notice=notice
                self.last_snapshot=None
                self.set_game_status('Game not connected - saved checks still synchronize')
            await asyncio.sleep(1)
        self.pipe.close()

async def main(args):
    saved=Utils.persistent_load().get('rfg_archipelago',{}).get('game_folder')
    folder=args.game_folder or discover_game_folder(saved)
    ctx=RFGContext(args.connect,args.password,folder)
    ctx.auth=args.name
    ctx.server_task=asyncio.create_task(server_loop(ctx),name='RF:G server')
    watcher=asyncio.create_task(ctx.watch_game(),name='RF:G game connection')
    if not getattr(args,'nogui',False) and Utils.gui_enabled: ctx.run_gui()
    ctx.run_cli()
    logger.info('RF:G client: received items, sent checks, chat and hints use the standard Archipelago interface.')
    if ctx.game_folder is None: logger.info('Set the game folder with /game_folder followed by the folder containing rfg.exe.')
    else: logger.info('RF:G folder: %s',ctx.game_folder)
    await ctx.exit_event.wait()
    await watcher
    await ctx.shutdown()

def run_client(*args):
    Utils.init_logging('RFGClient',exception_logger='Client')
    parser=get_base_parser('Red Faction: Guerrilla Re-Mars-tered Archipelago client')
    parser.add_argument('--name',default=None)
    parser.add_argument('--game-folder',default=None)
    parser.add_argument('url',nargs='?')
    options=handle_url_arg(parser.parse_args(args),parser)
    asyncio.run(main(options))
