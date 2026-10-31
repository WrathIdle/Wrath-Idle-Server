"""Check class presets against the installed Wrath talent tables without running the game."""
import argparse
import re
import struct
from pathlib import Path


def dbc(path):
    data = path.read_bytes()
    magic, count, fields, width, strings = struct.unpack_from('<4s4I', data)
    assert magic == b'WDBC' and width == fields * 4, f'Unexpected DBC format: {path.name}'
    assert len(data) == 20 + count * width + strings, f'Truncated DBC: {path.name}'
    return [struct.unpack_from(f'<{fields}I', data, 20 + i * width) for i in range(count)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dbc', type=Path, required=True)
    parser.add_argument('--config', type=Path, required=True)
    args = parser.parse_args()
    server = Path(__file__).resolve().parents[3]
    policy = (server / 'modules/mod-wrathidle/src/IdleClassPolicy.h').read_text()
    presets = re.findall(r'\{(\d+), "([^"]+)", "[^"]+", "([^"]+)"', policy)
    assert len(presets) == 10, 'All ten playable classes need a preset'
    # Only read the two talent configuration families; never emit database settings.
    options = dict(re.findall(r'^\s*(Playerbots\.PremadeSpec(?:Name|Link)\.[\d.]+)\s*=\s*(.*?)\s*$',
                              args.config.read_text(), re.M))
    tabs = {row[0]: row for row in dbc(args.dbc / 'TalentTab.dbc')}
    talents = dbc(args.dbc / 'Talent.dbc')
    for cls, name, template in presets:
        cls = int(cls)
        key = next((key for key, value in options.items()
                    if key.startswith(f'Playerbots.PremadeSpecName.{cls}.') and value.strip('"') == template), None)
        assert key, f'{name}: configured template is missing'
        spec = key.rsplit('.', 1)[1]
        link = options[f'Playerbots.PremadeSpecLink.{cls}.{spec}.80'].strip('"')
        planned = {}
        order = []
        for tab, ranks in enumerate(link.split('-')):
            entries = sorted((talent for talent in talents
                              if tabs[talent[1]][20] & (1 << (cls - 1)) and tabs[talent[1]][22] == tab),
                             key=lambda t: (t[2], t[3]))
            assert len(ranks) <= len(entries), f'{name}: too many talent cells'
            for talent, rank in zip(entries, ranks):
                rank = int(rank)
                assert 0 <= rank <= 5 and (not rank or talent[3 + rank]), f'{name}: nonexistent rank'
                if rank:
                    planned[talent[0]] = (talent, rank)
                    order.append(talent[0])
        assert sum(rank for _, rank in planned.values()) == 71, f'{name}: expected 71 earned points'
        indexed = {t[0]: t for t in talents}
        # Exercise each earned point using core tier/dependency gates; no level-80 point grant is assumed.
        allocated = {}
        for point in range(71):
            for talent_id in order:
                talent, rank = planned[talent_id]
                current = allocated.get(talent_id, 0)
                if current >= rank:
                    continue
                tab_points = sum(n for tid, n in allocated.items() if planned[tid][0][1] == talent[1])
                if tab_points < talent[2] * 5:
                    continue
                # The core ignores legacy dependency references absent from this DBC build.
                if talent[13] in indexed and allocated.get(talent[13], 0) <= talent[16]:
                    continue
                allocated[talent_id] = current + 1
                break
            else:
                raise AssertionError(f'{name}: allocation stalls at point {point + 1}')
        print(f'{name}: 71 earned points valid against installed talents')
    print('All ten class talent templates passed offline data checks.')


if __name__ == '__main__':
    main()
