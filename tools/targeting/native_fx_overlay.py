"""Attack-effect observation calls for the cores that are not built from the Angband-family trees.

Umoria, Rogue 3.6, Rogue 5.4 and RogueClone2 get a handful of one-line calls
added to a copy of their prepared `patched-src` tree.  The calls only record what happened (where a spell or
a missile went, and whether it took effect); they never change a variable, a random number or the screen.

The imported originals stay immutable: this module is called from overlay.py, which writes the changed copies
under `<output>/<core>/patched-src`.  Every site is located by a line hash (line_anchor.py) or by the name of
the function it lives in, so the original text is never quoted here.  A site that no longer matches exactly
once stops the build (the original changed and the site must be reviewed).

Each tree also gets a generated header `hb_fx_hooks.h` that declares the functions the calls use; the adapter
(umoria/adapter/um_frame.cpp, rogue_legacy/adapter/rl_frame.cpp, rogue2/adapter/r2_frame.cpp) defines them.
"""
from __future__ import annotations

import hashlib
import re

import line_anchor as anchor

HOOK_HEADER = 'hb_fx_hooks.h'

# ---------------------------------------------------------------------------------------------------------
#  small helpers
# ---------------------------------------------------------------------------------------------------------


def digest(value):
    return hashlib.sha256(value).hexdigest()


def split_newlines(raw):
    """(text with "\\n" line ends, original line end).  latin1 keeps every byte."""
    text = raw.decode('latin1')
    newline = '\r\n' if '\r\n' in text else '\n'
    return text.replace('\r\n', '\n'), newline


def join_newlines(text, newline):
    return text.replace('\n', newline).encode('latin1')


def function_span(text, name):
    """(start, end) of the definition of `name`: from its first line to the closing brace line (inclusive).

    Handles `void name(...) {` (Umoria) and the K&R style where the return type is on the line above and the
    brace stands alone on the next line (Rogue).  The name must be defined exactly once; calls and prototypes
    do not match (a definition is the only place where `)` is followed by an opening brace).
    """
    esc = re.escape(name)
    found = []
    # (a) the return type and the name are on one line, the brace ends the declaration (Umoria, RogueClone2)
    for m in re.finditer(r'^[A-Za-z_][^\n;{}()]*?\b' + esc + r'\([^;{}]*\)[ \t]*\n?[ \t]*\{[ \t]*$', text, re.M):
        found.append(m.start())
    # (b) K&R: the name starts the line, the return type is the line above
    for m in re.finditer(r'^' + esc + r'\([^;{}]*\)[ \t]*\n\{[ \t]*$', text, re.M):
        above = text.rfind('\n', 0, m.start() - 1) + 1
        found.append(above if re.fullmatch(r'[A-Za-z_][\w \t\*]*', text[above:m.start() - 1]) else m.start())
    if len(set(found)) != 1:
        raise ValueError(f'function {name}: expected one definition, found {len(set(found))}')
    start = found[0]
    end = text.find('\n}', start)
    if end < 0:
        raise ValueError(f'function {name}: no closing brace')
    return start, end + 2


def edit_function(text, name, edit):
    """Apply `edit(body) -> body` to the definition of `name`."""
    start, end = function_span(text, name)
    return text[:start] + edit(text[start:end]) + text[end:]


def indent_of(body, anchors, extra=''):
    lines = body.split('\n')
    first = [anchors] if isinstance(anchors, str) else list(anchors)
    hashes = [anchor.line_hash(l) for l in lines]
    for i in range(len(lines) - len(first) + 1):
        if hashes[i:i + len(first)] == first:
            line = lines[i]
            return line[:len(line) - len(line.lstrip(' \t'))] + extra
    raise ValueError('anchor not found in function body')


def put_before_hash(body, h, new, what):
    pad = indent_of(body, h)
    return anchor.insert_before(body, h, pad + new, what)


def put_after_hash(body, h, new, what, indent_from=None):
    pad = indent_of(body, indent_from or h)
    return anchor.insert_after(body, h, pad + new, what)


def put_after_open(body, new, what, pad):
    """First statement of the function: right after the opening brace line."""
    lines = body.split('\n')
    for i, line in enumerate(lines):
        if line.rstrip().endswith('{'):
            lines.insert(i + 1, pad + new)
            return '\n'.join(lines)
    raise ValueError(what + ': no opening brace')


def put_before_close(body, new, what, pad):
    """Last statement of the function: right before the closing brace line."""
    lines = body.split('\n')
    last = max(i for i, l in enumerate(lines) if l == '}')
    lines.insert(last, pad + new)
    return '\n'.join(lines)


def put_before_last_return(body, make, what, pad):
    """Before the last `return <identifier>;` of the function.  `make(identifier)` builds the new line."""
    lines = body.split('\n')
    hits = [i for i, l in enumerate(lines) if re.fullmatch(r'\s*return\s+[A-Za-z_]\w*\s*;\s*', l)]
    if not hits:
        raise ValueError(what + ': no `return <identifier>;` in the function')
    at = hits[-1]
    ident = re.search(r'return\s+([A-Za-z_]\w*)', lines[at]).group(1)
    lines.insert(at, pad + make(ident))
    return '\n'.join(lines)


def copy_tree(source_dir, destination_dir, manifest_prefix, manifest, edits):
    """Copy every file of a prepared tree.  `edits` maps a file name to a function(bytes) -> bytes.

    Returns the set of file names that were rewritten.  The file is only written when the content changed, so
    the modification time stays put for an unchanged site (the build does not recompile everything).
    """
    changed = set()
    seen = set()
    for source in sorted(source_dir.rglob('*')):
        if not source.is_file():
            continue
        relative = source.relative_to(source_dir).as_posix()
        before = source.read_bytes()
        after = before
        if relative in edits:
            after = edits[relative](before)
            seen.add(relative)
            if after != before:
                changed.add(relative)
        destination = destination_dir / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        if not destination.exists() or destination.read_bytes() != after:
            destination.write_bytes(after)
        manifest['files'].append({'path': manifest_prefix + relative, 'source_sha256': digest(before),
                                  'overlay_sha256': digest(after)})
    missing = set(edits) - seen
    if missing:
        raise ValueError('native fx overlay: files to edit are missing from the tree: ' + ', '.join(sorted(missing)))
    return changed


def write_header(destination_dir, text, manifest_prefix, manifest):
    raw = text.encode('latin1')
    destination = destination_dir / HOOK_HEADER
    destination.parent.mkdir(parents=True, exist_ok=True)
    if not destination.exists() or destination.read_bytes() != raw:
        destination.write_bytes(raw)
    manifest['files'].append({'path': manifest_prefix + HOOK_HEADER, 'source_sha256': '', 'overlay_sha256': digest(raw)})


def include_header(raw):
    """Put `#include "hb_fx_hooks.h"` before the first line of a C/C++ source."""
    text, newline = split_newlines(raw)
    return join_newlines('#include "' + HOOK_HEADER + '"\n' + text, newline)


# ---------------------------------------------------------------------------------------------------------
#  Umoria 5.7 (C++14, CRLF)
# ---------------------------------------------------------------------------------------------------------

UMORIA_HEADER = '''/* Generated by tools/targeting/native_fx_overlay.py -- calls that only record where a spell or a missile went. */
#pragma once
#ifdef __cplusplus /* C linkage when read from C++ */
extern "C" {
#endif
/* A spell that flies in a straight line from the player.  `fn` is the name of the original function (__func__),
 * `end` the coordinate where it stopped, `effective` whether it took effect, `param` a per-function number. */
void hb_um_fx_line(const char *fn, int direction, int end_y, int end_x, int effective, int param);
/* A spell that works around the player or on everything in sight (no direction). */
void hb_um_fx_around(const char *fn, int effective, int param);
/* A monster's breath: the monster number, the spell type and the player's coordinate. */
void hb_um_fx_breath(int monster_id, int spell_type, int y, int x);
/* Throwing / shooting: the thrown item (Inventory_t *), each lit open cell it crosses, and the hit. */
void hb_um_fx_throw_begin(const void *item);
void hb_um_fx_throw_step(int y, int x);
void hb_um_fx_throw_hit(int y, int x, int visible);
#ifdef __cplusplus /* end of the C linkage block */
}
#endif
'''

# Hashes of original lines (see line_anchor.py).  The original text is not quoted here.
UM = {
    'ball_explosion': 'fa4c2df5859f',      # the comment line that opens the explosion loop in spellFireBall()
    'ball_range_if': 'f845c7601265',       # the "past the maximum range" test in spellFireBall()
    'finished_true': None,                 # filled below from the line text's hash (a short assignment)
    'clone_found': 'e9b8fc45ff15',         # the first statement in the branch that found a monster in spellCloneMonster()
    'throw_facts': '8dd43fb84590',         # the call that works out the hit and damage bonuses in playerThrowItem()
    'throw_lit_if': 'b2752ee85185',        # the "draw the flying object" test in playerThrowItem()
    'throw_damage': 'ed7e8da4ab92',        # the first statement after a thrown item hit in playerThrowItem()
}
UM['finished_true'] = anchor.line_hash('finished = true;')

# Directional spells that report whether they took effect (the returned flag).  (function, param expression).
UM_LINE_BY_RETURN = (
    ('spellChangeMonsterHitPoints', 'damage_hp'),
    ('spellDrainLifeFromMonster', '0'),
    ('spellSpeedMonster', 'speed'),
    ('spellConfuseMonster', '0'),
    ('spellSleepMonster', '0'),
    ('spellWallToMud', '0'),
    ('spellDestroyDoorsTrapsInDirection', '0'),
    ('spellPolymorphMonster', '0'),
    ('spellBuildWall', '0'),
    ('spellTeleportAwayMonsterInDirection', '0'),
    ('spellDisarmAllInDirection', '0'),
)
# Spells that work around the player or on everything in sight.  (function, file, param expression).
UM_AROUND_BY_RETURN = (
    ('spellLightArea', 'spells.cpp', '0'),
    ('spellDarkenArea', 'spells.cpp', '0'),
    ('spellSleepAllMonsters', 'spells.cpp', '0'),
    ('spellSpeedAllMonsters', 'spells.cpp', 'speed'),
    ('spellMassPolymorph', 'spells.cpp', '0'),
    ('spellDispelCreature', 'spells.cpp', 'creature_defense'),
    ('spellTurnUndead', 'spells.cpp', '0'),
    ('monsterSleep', 'monster.cpp', '0'),
)


def _umoria_spells(raw):
    text, newline = split_newlines(raw)
    for name, param in UM_LINE_BY_RETURN:
        text = edit_function(text, name, lambda body, p=param, n=name: put_before_last_return(
            body, lambda var: f'hb_um_fx_line(__func__, direction, coord.y, coord.x, {var} ? 1 : 0, {p});', n, '    '))
    # Clone: the original returns inside the loop, so record that a monster was found.
    text = edit_function(text, 'spellCloneMonster', lambda body: put_before_hash(
        body, UM['clone_found'], 'hb_um_fx_line(__func__, direction, coord.y, coord.x, 1, 0);', 'umoria clone site'))
    # Bolt: after the loop (the function ends with it).
    text = edit_function(text, 'spellFireBolt', lambda body: put_before_close(
        body, 'hb_um_fx_line(__func__, direction, coord.y, coord.x, 1, spell_type);', 'umoria bolt site', '    '))

    # Ball: the explosion, and the flight that ran out of range without exploding.
    def ball(body):
        body = put_before_hash(body, UM['ball_explosion'], 'hb_um_fx_line(__func__, direction, coord.y, coord.x, 1, spell_type);',
                               'umoria ball explosion site')
        return anchor.insert_after(body, [UM['ball_range_if'], UM['finished_true']],
                                   indent_of(body, UM['finished_true']) + 'hb_um_fx_line(__func__, direction, coord.y, coord.x, 0, spell_type);',
                                   'umoria ball range site')
    text = edit_function(text, 'spellFireBall', ball)
    # Light line: the walk ends one cell past the last lit cell.
    text = edit_function(text, 'spellLightLine', lambda body: put_before_close(
        body, 'hb_um_fx_line(__func__, direction, coord.y, coord.x, 1, 0);', 'umoria light line site', '    '))
    # Monster breath: at the top of the function, before anything is damaged.
    text = edit_function(text, 'spellBreath', lambda body: put_after_open(
        body, 'hb_um_fx_breath(monster_id, spell_type, coord.y, coord.x);', 'umoria breath site', '    '))
    for name, file_name, param in UM_AROUND_BY_RETURN:
        if file_name != 'spells.cpp':
            continue
        text = edit_function(text, name, lambda body, p=param, n=name: put_before_last_return(
            body, lambda var: f'hb_um_fx_around(__func__, {var} ? 1 : 0, {p});', n, '    '))
    return join_newlines('#include "' + HOOK_HEADER + '"\n' + text, newline)


def _umoria_monster(raw):
    text, newline = split_newlines(raw)
    for name, file_name, param in UM_AROUND_BY_RETURN:
        if file_name != 'monster.cpp':
            continue
        text = edit_function(text, name, lambda body, p=param, n=name: put_before_last_return(
            body, lambda var: f'hb_um_fx_around(__func__, {var} ? 1 : 0, {p});', n, '    '))
    return join_newlines('#include "' + HOOK_HEADER + '"\n' + text, newline)


def _umoria_throw(raw):
    text, newline = split_newlines(raw)

    def thrown(body):
        body = put_after_hash(body, UM['throw_facts'], 'hb_um_fx_throw_begin(&thrown_item);', 'umoria throw begin site')
        body = put_before_hash(body, UM['throw_lit_if'], 'hb_um_fx_throw_step(coord.y, coord.x);', 'umoria throw step site')
        return put_before_hash(body, UM['throw_damage'], 'hb_um_fx_throw_hit(coord.y, coord.x, visible ? 1 : 0);',
                               'umoria throw hit site')
    text = edit_function(text, 'playerThrowItem', thrown)
    return join_newlines('#include "' + HOOK_HEADER + '"\n' + text, newline)


UMORIA_EDITS = {
    'spells.cpp': _umoria_spells,
    'monster.cpp': _umoria_monster,
    'player_throw.cpp': _umoria_throw,
}


def build_umoria(sources, output, manifest):
    source_dir = sources / 'umoria' / 'patched-src'
    if not source_dir.is_dir():
        return
    destination = output / 'umoria' / 'patched-src'
    copy_tree(source_dir, destination, 'umoria/patched-src/', manifest, UMORIA_EDITS)
    write_header(destination, UMORIA_HEADER, 'umoria/patched-src/', manifest)


# ---------------------------------------------------------------------------------------------------------
#  Rogue 3.6.4 / Rogue 5.4.4 (C, LF, tabs).  Both generations share one hook header and one adapter
#  (rogue_legacy/adapter/rl_frame.cpp).
# ---------------------------------------------------------------------------------------------------------

ROGUE_HEADER = """/* Generated by tools/targeting/native_fx_overlay.py -- calls that only record where a spell or a missile went. */
#ifndef HB_FX_HOOKS_H
#define HB_FX_HOOKS_H
#ifdef __cplusplus /* C linkage when read from C++ */
extern "C" {
#endif
/* A thrown or fired weapon: its name (the original's), where the hero stood, the direction, where it stopped,
 * and whether the wielded weapon is the launcher the missile needs. */
void hb_rl_fx_throw(const char *weapon, int hero_y, int hero_x, int dy, int dx, int end_y, int end_x, int launcher_matches);
/* fight() hit something: `thrown_weapon` is true for a thrown weapon (not a melee blow, not a wand bolt). */
void hb_rl_fx_thrown_hit(int thrown_weapon, int y, int x);
/* A wand or scroll effect.  `name` is the original's own name for it ("polymorph", "hold monster", ...).
 * `end` is where it ended; `end_is_monster` says a monster stood there. */
void hb_rl_fx_zap(const char *name, int hero_y, int hero_x, int end_y, int end_x, int end_is_monster);
/* A bouncing bolt (lightning / flame / ice): each cell the original draws, then the end of the bolt. */
void hb_rl_fx_bolt_cell(int y, int x);
void hb_rl_fx_bolt_end(const char *name, int from_monster, int start_y, int start_x);
#ifdef __cplusplus /* end of the C linkage block */
}
#endif
#endif
"""

# Hashes of original lines (see line_anchor.py).
RL = {
    'open_brace': anchor.line_hash('{'),
    # 3.6.4
    '36_wand_light': '7896c15c36b6',       # marks the light wand as known (sticks.c do_zap)
    '36_mon_if_a': 'd56830466cca',         # "a monster stands at the end of the line" (polymorph, teleport, cancel)
    '36_mon_if_b': 'af60d8d66c8d',         # the same test for the haste / slow wands
    '36_missile_motion': '56d4eab7d476',   # the magic missile wand flies (do_motion)
    '36_bolt_cell': '6d6ee03422c4',        # the bouncing bolt draws one cell
    '36_draw': '827651027736',             # ... and refreshes
    '36_bolt_end': 'b00ce0d08903',         # the bouncing bolt marks its wand as known
    '36_drain': '8d62baffa73e',            # drain life shares the hit points among the monsters it found
    '36_throw_motion': '705d3c18bc09',     # missile(): the thrown weapon flies (do_motion)
    '36_hit_sound': '067bce8194db',        # fight(): the hit sound
    '36_scroll_light': '9b623219a317',     # the light scroll is known
    '36_scroll_hold': '2f9a8055f3e7',      # the hold monster scroll holds one monster
    # 5.4.4
    '54_wand_light': '9647985e12eb',
    '54_mon_if': '007d54bc6a5d',           # "a monster stands at the end of the line" (appears twice in do_zap)
    '54_missile_motion': '56d4eab7d476',
    '54_bolt_cell': '63868a1f0955',
    '54_refresh': 'eae854b5e4b8',
    '54_bolt_end': 'c0ddc157ed89',         # fire_bolt(): the last statement, which erases the drawn bolt
    '54_drain': '368cc101ca8a',            # drain(): the hero's hit points are halved
    '54_throw_motion': '705d3c18bc09',
    '54_hit_sound': 'a06c152f34e7',
    '54_scroll_hold': '684cb7d9362c',
}


def _rl_after(body, anchors, new, what, deeper=False):
    last = anchors if isinstance(anchors, str) else anchors[-1]
    pad = indent_of(body, last)
    return anchor.insert_after(body, anchors, pad + ('\t' if deeper else '') + new, what)


def _rl_before(body, h, new, what):
    return anchor.insert_before(body, h, indent_of(body, h) + new, what)


def _with_header(text, newline):
    return join_newlines('#include "' + HOOK_HEADER + '"\n' + text, newline)


def _rogue36_sticks(raw):
    text, newline = split_newlines(raw)

    def zap(body):
        body = _rl_after(body, RL['36_wand_light'], 'hb_rl_fx_zap(ws_magic[WS_LIGHT].mi_name, hero.y, hero.x, hero.y, hero.x, 0);', 'rogue36 light wand site')
        hook = 'hb_rl_fx_zap(ws_magic[obj->o_which].mi_name, hero.y, hero.x, y, x, 1);'
        body = _rl_after(body, [RL['36_mon_if_a'], RL['open_brace']], hook, 'rogue36 monster wand site a', deeper=True)
        body = _rl_after(body, [RL['36_mon_if_b'], RL['open_brace']], hook, 'rogue36 monster wand site b', deeper=True)
        body = _rl_after(body, RL['36_missile_motion'],
                         'hb_rl_fx_zap(ws_magic[WS_MISSILE].mi_name, hero.y, hero.x, bolt.o_pos.y, bolt.o_pos.x, isupper(mvwinch(mw, bolt.o_pos.y, bolt.o_pos.x)) ? 1 : 0);',
                         'rogue36 magic missile site')
        body = _rl_after(body, [RL['36_bolt_cell'], RL['36_draw']], 'hb_rl_fx_bolt_cell(pos.y, pos.x);', 'rogue36 bolt cell site')
        return _rl_before(body, RL['36_bolt_end'], 'hb_rl_fx_bolt_end(name, 0, hero.y, hero.x);', 'rogue36 bolt end site')
    text = edit_function(text, 'do_zap', zap)
    text = edit_function(text, 'drain', lambda body: _rl_after(
        body, RL['36_drain'], 'hb_rl_fx_zap(ws_magic[WS_DRAIN].mi_name, hero.y, hero.x, hero.y, hero.x, 0);', 'rogue36 drain site'))
    return _with_header(text, newline)


def _rogue36_weapons(raw):
    text, newline = split_newlines(raw)
    text = edit_function(text, 'missile', lambda body: _rl_after(
        body, RL['36_throw_motion'],
        'hb_rl_fx_throw(w_names[obj->o_which], hero.y, hero.x, ydelta, xdelta, obj->o_pos.y, obj->o_pos.x, (cur_weapon != NULL && cur_weapon->o_which == obj->o_launch) ? 1 : 0);',
        'rogue36 throw site'))
    return _with_header(text, newline)


def _rogue36_fight(raw):
    text, newline = split_newlines(raw)
    text = edit_function(text, 'fight', lambda body: _rl_after(
        body, RL['36_hit_sound'], 'hb_rl_fx_thrown_hit(thrown && weap->o_type == WEAPON, mp->y, mp->x);', 'rogue36 thrown hit site'))
    return _with_header(text, newline)


def _rogue36_scrolls(raw):
    text, newline = split_newlines(raw)

    def read(body):
        body = _rl_after(body, RL['36_scroll_light'], 'hb_rl_fx_zap(s_magic[S_LIGHT].mi_name, hero.y, hero.x, hero.y, hero.x, 0);', 'rogue36 light scroll site')
        return _rl_after(body, RL['36_scroll_hold'], 'hb_rl_fx_zap(s_magic[S_HOLD].mi_name, hero.y, hero.x, y, x, 1);', 'rogue36 hold scroll site')
    text = edit_function(text, 'read_scroll', read)
    return _with_header(text, newline)


ROGUE36_EDITS = {
    'sticks.c': _rogue36_sticks,
    'weapons.c': _rogue36_weapons,
    'fight.c': _rogue36_fight,
    'scrolls.c': _rogue36_scrolls,
}


def _rogue54_sticks(raw):
    text, newline = split_newlines(raw)

    def zap(body):
        body = _rl_after(body, RL['54_wand_light'], 'hb_rl_fx_zap(ws_info[WS_LIGHT].oi_name, hero.y, hero.x, hero.y, hero.x, 0);', 'rogue54 light wand site')
        pad = indent_of(body, RL['54_mon_if']) + '\t'
        body = anchor.edit_exactly(
            body, [RL['54_mon_if'], RL['open_brace']],
            lambda lines: lines + [pad + 'hb_rl_fx_zap(ws_info[obj->o_which].oi_name, hero.y, hero.x, y, x, 1);'], 2, 'rogue54 monster wand sites')
        return _rl_after(body, RL['54_missile_motion'],
                         'hb_rl_fx_zap(ws_info[WS_MISSILE].oi_name, hero.y, hero.x, bolt.o_pos.y, bolt.o_pos.x, moat(bolt.o_pos.y, bolt.o_pos.x) != NULL ? 1 : 0);',
                         'rogue54 magic missile site')
    text = edit_function(text, 'do_zap', zap)
    text = edit_function(text, 'drain', lambda body: _rl_after(
        body, RL['54_drain'], 'hb_rl_fx_zap(ws_info[WS_DRAIN].oi_name, hero.y, hero.x, hero.y, hero.x, 0);', 'rogue54 drain site'))

    def bolt(body):
        body = _rl_after(body, [RL['54_bolt_cell'], RL['54_refresh']], 'hb_rl_fx_bolt_cell(pos.y, pos.x);', 'rogue54 bolt cell site')
        return _rl_after(body, RL['54_bolt_end'], 'hb_rl_fx_bolt_end(name, start != &hero, start->y, start->x);', 'rogue54 bolt end site')
    text = edit_function(text, 'fire_bolt', bolt)
    return _with_header(text, newline)


def _rogue54_weapons(raw):
    text, newline = split_newlines(raw)
    text = edit_function(text, 'missile', lambda body: _rl_after(
        body, RL['54_throw_motion'],
        'hb_rl_fx_throw(weap_info[obj->o_which].oi_name, hero.y, hero.x, ydelta, xdelta, obj->o_pos.y, obj->o_pos.x, (cur_weapon != NULL && cur_weapon->o_which == obj->o_launch) ? 1 : 0);',
        'rogue54 throw site'))
    return _with_header(text, newline)


def _rogue54_fight(raw):
    text, newline = split_newlines(raw)
    text = edit_function(text, 'fight', lambda body: _rl_after(
        body, RL['54_hit_sound'], 'hb_rl_fx_thrown_hit(thrown && weap->o_type == WEAPON, tp->t_pos.y, tp->t_pos.x);', 'rogue54 thrown hit site'))
    return _with_header(text, newline)


def _rogue54_scrolls(raw):
    text, newline = split_newlines(raw)
    text = edit_function(text, 'read_scroll', lambda body: _rl_after(
        body, RL['54_scroll_hold'], 'hb_rl_fx_zap(scr_info[S_HOLD].oi_name, hero.y, hero.x, y, x, 1);', 'rogue54 hold scroll site'))
    return _with_header(text, newline)


ROGUE54_EDITS = {
    'sticks.c': _rogue54_sticks,
    'weapons.c': _rogue54_weapons,
    'fight.c': _rogue54_fight,
    'scrolls.c': _rogue54_scrolls,
}


def build_rogue_legacy(sources, output, manifest, core, edits):
    source_dir = sources / core / 'patched-src'
    if not source_dir.is_dir():
        return
    destination = output / core / 'patched-src'
    copy_tree(source_dir, destination, core + '/patched-src/', manifest, edits)
    write_header(destination, ROGUE_HEADER, core + '/patched-src/', manifest)


# ---------------------------------------------------------------------------------------------------------
#  RogueClone2 (C, LF, tabs, Japanese text in UTF-8).  Adapter: rogue2/adapter/r2_frame.cpp.
# ---------------------------------------------------------------------------------------------------------

ROGUE2_HEADER = """/* Generated by tools/targeting/native_fx_overlay.py -- calls that only record where a spell or a missile went. */
#ifndef HB_FX_HOOKS_H
#define HB_FX_HOOKS_H
#ifdef __cplusplus /* C linkage when read from C++ */
extern "C" {
#endif
/* The magic missile wand: where the hero stood, the last cell the bolt crossed (or the monster's cell), and whether a monster stood there. */
void hb_r2_fx_missile_bolt(int hero_row, int hero_col, int end_row, int end_col, int end_is_monster);
/* A wand ('W') or scroll ('S') took hold of a monster: the original's kind number (WAND/SCROLL macros in rogue.h). */
void hb_r2_fx_zap(int group, int kind, int hero_row, int hero_col, int end_row, int end_col);
/* A thrown or fired weapon (kind = BOW/DART/ARROW... in rogue.h), where it stopped, and whether a monster stood there. */
void hb_r2_fx_throw(int kind, int hero_row, int hero_col, int end_row, int end_col, int monster_found, int launcher_matches);
void hb_r2_fx_thrown_hit(int row, int col);
/* A monster's flame: the monster, the hero, and whether the original draws the flight (not blind, not next to the hero). */
void hb_r2_fx_flame(int monster_row, int monster_col, int hero_row, int hero_col, int visible_flight);
#ifdef __cplusplus /* end of the C linkage block */
}
#endif
#endif
"""

R2 = {
    'missile_get': '4198f6e5d134',    # zapp(): the magic missile wand flies (get_missiled_monster)
    'wake_up': '33071893eda5',        # zapp(): a monster was found; it is woken and the wand takes hold of it
    'throw_get': 'a7aeb66ee26a',      # throw(): the thrown weapon flies (get_thrown_at_monster)
    'thrown_hit': 'a7ce964a1033',     # throw_at_monster(): the hit message
    'flame': '48a4ecb19b5a',          # flame_broil(): the breath sound (after the flight is drawn)
    'hold': '4e73c76cfef0',           # hold_monster(): one more monster is held
}


def _rogue2_zap(raw):
    text, newline = split_newlines(raw)

    def zapp(body):
        body = _rl_after(body, R2['missile_get'], 'hb_r2_fx_missile_bolt(rogue.row, rogue.col, row, col, monster != NULL ? 1 : 0);', 'rogue2 magic missile site')
        return _rl_before(body, R2['wake_up'], "hb_r2_fx_zap('W', wand->which_kind, rogue.row, rogue.col, monster->row, monster->col);", 'rogue2 wand site')
    text = edit_function(text, 'zapp', zapp)
    return _with_header(text, newline)


def _rogue2_throw(raw):
    text, newline = split_newlines(raw)
    text = edit_function(text, 'throw', lambda body: _rl_after(
        body, R2['throw_get'],
        'hb_r2_fx_throw(weapon->which_kind, rogue.row, rogue.col, row, col, monster != NULL ? 1 : 0, (rogue.weapon != NULL && rogue.weapon->which_kind == BOW) ? 1 : 0);',
        'rogue2 throw site'))
    text = edit_function(text, 'throw_at_monster', lambda body: _rl_after(
        body, R2['thrown_hit'], 'hb_r2_fx_thrown_hit(monster->row, monster->col);', 'rogue2 thrown hit site'))
    return _with_header(text, newline)


def _rogue2_spechit(raw):
    text, newline = split_newlines(raw)
    text = edit_function(text, 'flame_broil', lambda body: _rl_before(
        body, R2['flame'],
        'hb_r2_fx_flame(monster->row, monster->col, rogue.row, rogue.col, (!blind && !rogue_is_around(monster->row, monster->col)) ? 1 : 0);',
        'rogue2 flame site'))
    return _with_header(text, newline)


def _rogue2_use(raw):
    text, newline = split_newlines(raw)
    text = edit_function(text, 'hold_monster', lambda body: _rl_after(
        body, R2['hold'], "hb_r2_fx_zap('S', HOLD_MONSTER, rogue.row, rogue.col, row, col);", 'rogue2 hold site'))
    return _with_header(text, newline)


ROGUE2_EDITS = {
    'zap.c': _rogue2_zap,
    'throw.c': _rogue2_throw,
    'spechit.c': _rogue2_spechit,
    'use.c': _rogue2_use,
}


def build_rogue2(sources, output, manifest):
    source_dir = sources / 'rogue2' / 'patched-src'
    if not source_dir.is_dir():
        return
    destination = output / 'rogue2' / 'patched-src'
    copy_tree(source_dir, destination, 'rogue2/patched-src/', manifest, ROGUE2_EDITS)
    write_header(destination, ROGUE2_HEADER, 'rogue2/patched-src/', manifest)


# ---------------------------------------------------------------------------------------------------------
#  entry point
# ---------------------------------------------------------------------------------------------------------

def build(sources, output, manifest):
    """Write the changed copies of the Umoria / Rogue trees under `output`.  Called from overlay.py."""
    build_umoria(sources, output, manifest)
    build_rogue_legacy(sources, output, manifest, 'rogue36', ROGUE36_EDITS)
    build_rogue_legacy(sources, output, manifest, 'rogue54', ROGUE54_EDITS)
    build_rogue2(sources, output, manifest)
