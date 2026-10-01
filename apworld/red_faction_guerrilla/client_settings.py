"""Find a local Steam installation without bundling a launcher or game files."""
import os
import re
import hashlib
from pathlib import Path

def startup_problem(folder):
    """Read only the selected installation's bounded startup log."""
    if not folder:
        return None
    try:
        with (Path(folder)/'RFGArchipelago'/'Logs'/'General Log.log').open('rb') as log:
            text=log.read(16384).decode('utf-8',errors='replace')
    except OSError:
        return None
    if 'Unsupported executable' in text:
        return ('The game log reports an unsupported rfg.exe; the mod did not start its connection. '
                'Run /diagnose and share the result. If using Steam, verify the game files and restart the game.')
    return None

def game_diagnostics(folder):
    if not folder:
        return 'No game folder selected. Use /game_folder with the folder containing rfg.exe.'
    try:
        with (Path(folder)/'rfg.exe').open('rb') as exe:
            digest=hashlib.file_digest(exe,'sha256').hexdigest().upper()
    except OSError as error:
        return f'Cannot read the selected rfg.exe: {error}'
    problem=startup_problem(folder)
    return f'rfg.exe SHA256: {digest}\n'+(problem or 'No unsupported-executable error found in the game startup log.')

def steam_game_folders(steam_roots):
    libraries=[]
    for root in steam_roots:
        root=Path(root)
        libraries.append(root)
        listing=root/'steamapps'/'libraryfolders.vdf'
        try:
            text=listing.read_text(encoding='utf-8',errors='replace')
        except OSError:
            continue
        libraries.extend(Path(value.replace('\\\\','\\'))
                         for value in re.findall(r'"path"\s*"([^"]+)"',text))
    found=[]
    for library in dict.fromkeys(libraries):
        manifest=library/'steamapps'/'appmanifest_667720.acf'
        try:
            text=manifest.read_text(encoding='utf-8',errors='replace')
        except OSError:
            continue
        match=re.search(r'"installdir"\s*"([^"]+)"',text)
        if match:
            candidate=library/'steamapps'/'common'/match[1]
            if (candidate/'rfg.exe').is_file(): found.append(candidate)
    return found

def discover_game_folder(saved=None):
    if saved and (Path(saved)/'rfg.exe').is_file(): return Path(saved)
    roots=[]
    if os.name=='nt':
        import winreg
        for hive,key,value in ((winreg.HKEY_CURRENT_USER,r'Software\Valve\Steam','SteamPath'),
                               (winreg.HKEY_LOCAL_MACHINE,r'SOFTWARE\WOW6432Node\Valve\Steam','InstallPath')):
            try:
                with winreg.OpenKey(hive,key) as handle: roots.append(Path(winreg.QueryValueEx(handle,value)[0]))
            except OSError: pass
    for variable in ('ProgramFiles(x86)','ProgramFiles'):
        if os.environ.get(variable): roots.append(Path(os.environ[variable])/'Steam')
    found=steam_game_folders(roots)
    # Multiple installations are ambiguous: let /game_folder select one.
    return found[0] if len(set(found))==1 else None
