# -*- coding: utf-8 -*-
"""幻想蛮怒・Frox・Sil-Q の「魔法・投射物の効果」の実機の検査の共通部分。

フレームの `projections`・`missiles`（`presentation/frame/frame_codec.cpp` の符号化。省略値は書かれない）を読み、
形・属性・範囲・経路・旗が、そのコアの原作の仕様どおりに出ているかを数える。各コアの検査（`tools/<core>/*_attack_fx_check.py`）が使う。
"""
from __future__ import annotations

import re
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

# ProjectionShape / MissileKind / AttackFxOrigin / ProjectionFxFlag（presentation/frame/attack_fx.h）
SHAPE_BOLT, SHAPE_BEAM, SHAPE_BALL, SHAPE_BREATH, SHAPE_JUMP, SHAPE_TOUCH = range(6)
KIND_ARROW, KIND_BOLT, KIND_SHOT, KIND_THROWN, KIND_BOOMERANG = range(5)
ORIGIN_PLAYER, ORIGIN_MONSTER, ORIGIN_OTHER = range(3)
FLAG_HIDDEN, FLAG_BLIND, FLAG_REFLECTED, FLAG_ALL_IN_SIGHT = 1, 2, 4, 8


def angband_distance(ax: int, ay: int, bx: int, by: int) -> int:
    """原作の `distance()`（斜めは 1.5 倍の近似）。"""
    dy = abs(ay - by)
    dx = abs(ax - bx)
    return (dy + (dx >> 1)) if dy > dx else (dx + (dy >> 1))


def known_attr_names() -> set[str]:
    """画面側の登録表の名前（`attack_fx_attr_names.h` と `hd2d/render/fx_styles/*` の登録）。"""
    text = (REPO / 'presentation/bridge/attack_fx_attr_names.h').read_text(encoding='utf-8-sig', errors='replace')
    names = set(re.findall(r'\{\s*AttributeType::\w+,\s*"([a-z0-9_]+)"\s*\}', text))
    styles = ''
    for p in (REPO / 'hd2d' / 'render' / 'fx_styles').glob('*'):
        if p.suffix in ('.cpp', '.h'):
            styles += p.read_text(encoding='utf-8-sig', errors='replace')
    names |= set(re.findall(r'"([a-z][a-z0-9_]*)"', styles))
    return names


def g(d: dict, key: str, default=0):
    return d.get(key, default)


def ends_at_impact(p: dict) -> bool:
    path = p.get('p') or []
    return bool(path) and path[-1] == [g(p, 'iy'), g(p, 'ix')]


def starts_next_to(p: dict) -> bool:
    path = p.get('p') or []
    if not path:
        return False
    return angband_distance(g(p, 'sx'), g(p, 'sy'), path[0][1], path[0][0]) == 1


def describe(p: dict) -> str:
    return (f"s={g(p, 's')} at={p.get('at')!r} r={g(p, 'r')} o={g(p, 'o')} f={g(p, 'f')} "
            f"src=({g(p, 'sy')},{g(p, 'sx')}) impact=({g(p, 'iy')},{g(p, 'ix')}) path={len(p.get('p') or [])} area={len(p.get('a') or [])}")


def check_ball(ck, label: str, p: dict, radius: int | None = None, attr: str | None = None, origin: int = ORIGIN_PLAYER) -> None:
    """ボール：形 Ball・撃った者・始点の隣から着弾点までの経路・爆発の範囲（中心が距離 0、距離は半径以内で、中心からの原作の距離）。"""
    ck.add(g(p, 's') == SHAPE_BALL, f'{label}: 形が Ball', describe(p))
    if attr is not None:
        ck.add(p.get('at') == attr, f'{label}: 属性が {attr}', f"at={p.get('at')!r}")
    if radius is not None:
        ck.add(g(p, 'r') == radius, f'{label}: 半径 {radius}', f"r={g(p, 'r')}")
    ck.add(g(p, 'o') == origin, f'{label}: 撃った者が {"プレイヤ" if origin == ORIGIN_PLAYER else "モンスター"}')
    ck.add(starts_next_to(p) and ends_at_impact(p), f'{label}: 経路は始点の隣から着弾点まで', f"p={p.get('p')} 着弾=({g(p, 'iy')},{g(p, 'ix')})")
    area = p.get('a') or []
    ck.add(len(area) > 1, f'{label}: 爆発の範囲が複数マス', f'{len(area)} マス')
    rad = g(p, 'r')
    ck.add(all(c[2] <= rad for c in area), f'{label}: 範囲の距離はすべて半径以内', f'最大 {max([c[2] for c in area] or [0])}')
    ck.add(any(c[2] == 0 and [c[0], c[1]] == [g(p, 'iy'), g(p, 'ix')] for c in area), f'{label}: 中心（着弾点）の距離が 0')
    ck.add(all(c[2] == angband_distance(c[1], c[0], g(p, 'ix'), g(p, 'iy')) for c in area), f'{label}: 範囲の距離＝中心からの原作の距離')
    ck.add(not (g(p, 'f') & (FLAG_HIDDEN | FLAG_BLIND | FLAG_ALL_IN_SIGHT)), f'{label}: 隠し・盲目・全員の旗が立たない', f"f={g(p, 'f')}")


def check_bolt(ck, label: str, p: dict, attr: str | None = None, origin: int = ORIGIN_PLAYER) -> None:
    ck.add(g(p, 's') == SHAPE_BOLT and g(p, 'r') == 0, f'{label}: 形 Bolt・半径 0', describe(p))
    if attr is not None:
        ck.add(p.get('at') == attr, f'{label}: 属性が {attr}', f"at={p.get('at')!r}")
    ck.add(g(p, 'o') == origin, f'{label}: 撃った者が {"プレイヤ" if origin == ORIGIN_PLAYER else "モンスター"}')
    ck.add(starts_next_to(p) and ends_at_impact(p), f'{label}: 経路は始点の隣から着弾点まで', f"p={p.get('p')} 着弾=({g(p, 'iy')},{g(p, 'ix')})")


def check_beam(ck, label: str, p: dict, attr: str | None = None, origin: int = ORIGIN_PLAYER) -> None:
    ck.add(g(p, 's') == SHAPE_BEAM, f'{label}: 形が Beam', describe(p))
    if attr is not None:
        ck.add(p.get('at') == attr, f'{label}: 属性が {attr}', f"at={p.get('at')!r}")
    ck.add(g(p, 'o') == origin, f'{label}: 撃った者が {"プレイヤ" if origin == ORIGIN_PLAYER else "モンスター"}')
    path = p.get('p') or []
    area = p.get('a') or []
    ck.add(len(path) >= 1 and starts_next_to(p), f'{label}: 経路が始点の隣から始まる', f'{len(path)} マス')
    ck.add(all(c[2] == 0 for c in area) and all([c[0], c[1]] in path for c in area), f'{label}: 範囲は経路の上で、距離はすべて 0',
           f'{len(area)} マス')


def check_breath(ck, label: str, p: dict, attr: str | None = None, origin: int = ORIGIN_PLAYER) -> None:
    ck.add(g(p, 's') == SHAPE_BREATH, f'{label}: 形が Breath', describe(p))
    if attr is not None:
        ck.add(p.get('at') == attr, f'{label}: 属性が {attr}', f"at={p.get('at')!r}")
    ck.add(g(p, 'o') == origin, f'{label}: 撃った者が {"プレイヤ" if origin == ORIGIN_PLAYER else "モンスター"}')
    area = p.get('a') or []
    ck.add(len(area) >= 1, f'{label}: 扇形の範囲がある', f'{len(area)} マス')
    ck.add(all(c[2] == angband_distance(g(p, 'sx'), g(p, 'sy'), c[1], c[0]) for c in area), f'{label}: 範囲の距離は撃った者からの原作の距離')


def check_names_registered(ck, seen: set[str]) -> None:
    known = known_attr_names()
    bad = sorted(a for a in seen if a and a not in known)
    ck.add(not bad, '送った属性の名前はすべて画面側の登録表にある', f'登録外 {bad} / 見た名前 {sorted(seen)}')
