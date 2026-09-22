"""Use the authored RF:G YAML in Archipelago's standard template generator."""
from functools import wraps
from importlib import resources
from pathlib import Path

TEMPLATE_NAME = 'Red Faction Guerrilla.yaml'
GAME_NAME = 'Red Faction: Guerrilla Re-Mars-tered'


def template_bytes():
    return resources.files(__package__).joinpath(TEMPLATE_NAME).read_bytes()


def install_template_export():
    # AP 0.6.7 has no per-world authored-template hook. Keep its normal generator
    # and replace only the RF:G default it just created. Imports never write files;
    # user player YAMLs, other games and presets are outside this replacement.
    import Options
    from Utils import get_file_safe_name

    original = Options.generate_yaml_templates
    if getattr(original, '_rfg_authored_template', False):
        return

    @wraps(original)
    def generate(target_folder, *args, **kwargs):
        result = original(target_folder, *args, **kwargs)
        destination = Path(target_folder) / (get_file_safe_name(GAME_NAME) + '.yaml')
        if destination.is_file():
            destination.write_bytes(template_bytes())
        return result

    generate._rfg_authored_template = True
    Options.generate_yaml_templates = generate
