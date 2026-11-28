#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""wwhd-rando without game files: settings, presets, seed names, permalinks and the refusals.

usage: test_rando_cli.py BUILD_DIR   (cmake -S tools/rando -B BUILD_DIR && cmake --build BUILD_DIR)
The game-file checks (the output against the official TWWHD Randomizer 1.2.0, seed by seed) need a USA game and run
outside CI; see tools/rando/README.md.
"""
import json
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

BUILD = Path(sys.argv.pop(1) if len(sys.argv) > 1 else 'build-rando').resolve()
EXE = BUILD / ('wwhd-rando.exe' if sys.platform == 'win32' else 'wwhd-rando')
DATA = BUILD / 'data'


class Cli:
    def __init__(self, work):
        self.work = work

    def __call__(self, *args, ok=True):
        p = subprocess.run([str(EXE), '--data', str(DATA), '--work', str(self.work), *args],
                           capture_output=True, text=True, timeout=120)
        r = json.loads(p.stdout.strip().splitlines()[-1])
        if ok is not None:
            assert r['ok'] == ok, (args, r, p.stderr[-2000:])
            assert p.returncode == (0 if ok else 1), (args, p.returncode)
        return r


class RandoCli(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.cli = Cli(Path(self.tmp.name) / 'work')

    def tearDown(self):
        self.tmp.cleanup()

    def test_version(self):
        p = subprocess.run([str(EXE), 'version'], capture_output=True, text=True, timeout=60)
        self.assertEqual(json.loads(p.stdout), {'ok': True, 'version': '1.2.0'})

    def test_locations(self):
        locs = self.cli('locations')['locations']
        self.assertGreater(len(locs), 300)
        chest = [l for l in locs if l['paths']]
        self.assertTrue(chest)
        path, offsets = next(iter(chest[0]['paths'].items()))
        self.assertTrue(path.startswith('content/'))
        self.assertTrue(all(isinstance(o, int) for o in offsets))

    def test_data_has_no_game_art(self):
        files = [p.relative_to(DATA).as_posix() for p in DATA.rglob('*') if p.is_file()]
        self.assertTrue(files)
        self.assertFalse([f for f in files if f.lower().endswith(('.dds', '.tga', '.png', '.bftex', '.bflim', '.szs'))])
        self.assertFalse([f for f in files if f.startswith('assets/')])
        self.assertIn('LICENSE-TWWHD-Randomizer.txt', files)

    def test_settings_round_trip(self):
        s = self.cli('settings')['settings']
        self.assertIn('Progress Dungeons', s)
        self.assertNotIn('Starting Gear', s)
        self.assertEqual(s['Progress Mail']['kind'], 'bool')
        self.assertEqual((s['Path Hints']['kind'], s['Path Hints']['min'], s['Path Hints']['max']), ('count', 0, 7))
        self.assertEqual(s['Dungeon Small Keys']['choices'][0], 'Vanilla')
        self.assertEqual(s['Damage Multiplier']['min'], 2)
        self.cli('set', 'Path Hints', '8', ok=False)  # outside the PC randomizer's range
        self.cli('set', 'Damage Multiplier', '1', ok=False)
        self.cli('set', 'Progress Mail', '1')
        self.assertEqual(self.cli('get', 'Progress Mail')['value'], 1)
        self.cli('set', 'Progress Mail', '0')
        self.assertEqual(self.cli('get', 'Progress Mail')['value'], 0)
        self.cli('set', 'No such setting', '1', ok=False)
        self.cli('set', 'Starting Gear', '1', ok=False)
        self.cli('set', 'Progress Mail', 'x', ok=False)

    def test_names_of_the_pages(self):
        # the six pages of the setup window and the game (runtime/src/randomizer_settings.h) name every setting by the
        # names this command gives, and leave out only race mode (refused)
        table = (Path(__file__).resolve().parents[2] / 'runtime/src/randomizer_settings.h').read_text()
        names = re.findall(r'\{"([^"]+)", "[^"]*"\}', table)
        s = self.cli('settings')['settings']
        self.assertEqual([n for n in names if n not in s], [])
        self.assertEqual([n for n in s if n not in names], ['No Spoiler Log'])
        self.assertNotIn('Invalid Option', s)
        # the two names the randomizer can't round-trip itself (cli_name / cli_option)
        self.cli('set', 'Required Boss Items', '1')
        self.assertEqual(self.cli('get', 'Required Boss Items')['value'], 1)
        self.cli('set', 'Pig Color', '1')
        self.assertEqual(self.cli('settings')['settings']['Pig Color']['value'], 1)
        self.cli('set', 'PigColor', '2')
        self.assertEqual(self.cli('get', 'Pig Color')['value'], 2)

    def test_preferences_and_the_permalink(self):
        # a setting the settings command marks in_permalink changes the permalink; the others (preferences) never do
        self.cli('set-seed', 'WWHDTest')
        s = self.cli('settings')['settings']
        base = self.cli('permalink')['permalink']
        for name, d in s.items():
            if name == 'No Spoiler Log':
                continue
            other = d['min'] if d['value'] != d['min'] else d['max']
            self.cli('set', name, str(other))
            changed = self.cli('permalink')['permalink'] != base
            self.cli('load-permalink', base)  # (back; Remove Swords also took the sword out of the starting items)
            self.assertEqual(changed, d['in_permalink'], name)
        self.assertEqual(self.cli('permalink')['permalink'], base)

    def test_settings_to_permalink_round_trip(self):
        # what the game's Settings > Randomizer does (set, then the randomizer's own permalink): a fresh work folder that
        # loads that permalink has the same settings and gives the same permalink
        self.cli('preset', 'standard')
        self.cli('set-seed', 'RoundTrip')
        chosen = {'Randomize Dungeon Entrances': 1, 'Randomize Boss Entrances': 1, 'Randomize Miniboss Entrances': 1,
                  'Randomize Cave Entrances': 2, 'Randomize Door Entrances': 1, 'Randomize Misc Entrances': 1,
                  'Mix Caves': 1, 'Decouple Entrances': 1, 'Random Start Island': 1, 'Dungeon Small Keys': 4,
                  'Num Required Dungeons': 3, 'Required Boss Items': 1, 'Path Hints': 5, 'Starting HC': 6,
                  'Start With Random Item Sliding Item': 1, 'Progress Mail': 1, 'Damage Multiplier': 4}
        for name, v in chosen.items():
            self.cli('set', name, str(v))
        mine = self.cli('settings')
        self.assertEqual(mine['preset'], '')
        other = Cli(Path(self.tmp.name) / 'other')
        other('load-permalink', mine['permalink'])
        theirs = other('settings')
        self.assertEqual(theirs['permalink'], mine['permalink'])
        self.assertEqual(theirs['seed'], 'RoundTrip')
        values = lambda r: {n: d['value'] for n, d in r['settings'].items() if d['in_permalink']}
        self.assertEqual(values(theirs), values(mine))
        for name, v in chosen.items():
            self.assertEqual(theirs['settings'][name]['value'], v, name)

    def test_preset_of_the_settings(self):
        for name in ('beginner', 'everything', 'standard'):
            self.cli('preset', name)
            self.assertEqual(self.cli('settings')['preset'], name)
        self.cli('set', 'Camera', '1')  # a preference: still the preset
        self.assertEqual(self.cli('settings')['preset'], 'standard')
        self.cli('set', 'Randomize Dungeon Entrances', '1')
        self.assertEqual(self.cli('settings')['preset'], '')
        self.cli('set', 'Randomize Dungeon Entrances', '0')
        self.assertEqual(self.cli('settings')['preset'], 'standard')

    def test_seed_and_permalink(self):
        self.cli('set-seed', 'WWHDTest')
        link = self.cli('permalink')['permalink']
        self.cli('new-seed')
        self.assertNotEqual(self.cli('permalink')['permalink'], link)
        self.cli('load-permalink', link)
        self.assertEqual(self.cli('seed')['seed'], 'WWHDTest')
        self.assertEqual(self.cli('permalink')['permalink'], link)
        self.cli('load-permalink', 'not a permalink', ok=False)
        self.assertEqual(self.cli('permalink')['permalink'], link)  # (a refused one changes nothing)

    def test_presets(self):
        self.cli('set-seed', 'WWHDTest')
        links = {name: self.cli('preset', name)['permalink'] for name in ('standard', 'beginner', 'everything')}
        self.assertEqual(len(set(links.values())), 3)
        self.cli('preset', 'beginner')
        self.assertEqual(self.cli('get', 'Progress Puzzle Caves')['value'], 0)
        self.assertEqual(self.cli('seed')['seed'], 'WWHDTest')  # presets keep the seed name
        self.cli('preset', 'standard')
        self.cli('defaults')
        self.assertEqual(self.cli('permalink')['permalink'], links['standard'])
        self.cli('preset', 'nope', ok=False)

    def test_race_mode_refused(self):
        self.cli('set-seed', 'WWHDTest')
        self.cli('set', 'No Spoiler Log', '1')
        race = self.cli('permalink')['permalink']
        r = self.cli('generate', '--game', self.tmp.name, '--out', str(Path(self.tmp.name) / 'out'), ok=False)
        self.assertIn('Race-mode', r['error'])
        self.cli('set', 'No Spoiler Log', '0')
        normal = self.cli('permalink')['permalink']
        r = self.cli('load-permalink', race, ok=False)
        self.assertIn('Race-mode', r['error'])
        self.assertEqual(self.cli('permalink')['permalink'], normal)

    def test_releases(self):
        game = Path(self.tmp.name) / 'game'
        (game / 'meta').mkdir(parents=True)
        out = str(Path(self.tmp.name) / 'out')
        (game / 'meta/meta.xml').write_text('<menu><title_id type="hexBinary" length="8">000500001234ABCD</title_id></menu>')
        self.assertIn('knows', self.cli('generate', '--game', str(game), '--out', out, ok=False)['error'])
        self.assertFalse(Path(out).exists())

    def test_output_must_be_empty(self):
        game = Path(self.tmp.name) / 'game'
        (game / 'meta').mkdir(parents=True)
        (game / 'meta/meta.xml').write_text('<menu><title_id>0005000010143500</title_id></menu>')
        out = Path(self.tmp.name) / 'out'
        out.mkdir()
        (out / 'x').write_text('x')
        self.assertIn('not empty', self.cli('generate', '--game', str(game), '--out', str(out), ok=False)['error'])


if __name__ == '__main__':
    unittest.main()
