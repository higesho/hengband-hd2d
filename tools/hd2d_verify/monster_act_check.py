#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""変愚蛮怒・短愚蛮怒の「敵の行動を指定する入口」（環境変数 `HD2D_TEST_MONSTER_ACT`）の実機の検査。

    python tools/hd2d_verify/monster_act_check.py --heng-dir <HengbandCore.exe の置き場> --tang-dir <TangbandCore.exe の置き場> [--core hengband|tangband] [--work <作業フォルダ>]

入口の決まりは `presentation/bridge/hengband_test_monster_act.h`。コアを標準入出力（プロトコル v1）で駆動し、
検査用のセーブ（`tools/gate/fixtures/saves/<コア>/dungeon`。敵はいない）を読み込み、
デバッグの命令（`^A`）で敵（大白ヘビ 2・山猫・見習メイジ）を呼んでから、命令ファイルで操作して、
**すぐ次のフレーム**に結果が載ることを見る。敵は `hold` で止め、`place` で地図の決まったマス（フレームの `cells` から選ぶ）へ置く
（「置く → 撃たせる」を 1 つのファイルにまとめる）。敵の m_idx はフレームの `cells` の `monster_slot` から読む。

見ること（コアごと）:

  1. 環境変数が**無い**とき、命令ファイルを置いても何も起きない（ファイルは残り、標準エラーに `[test-monster-act]` が出ず、投射が出ない）。
  2. 複数行: 空行と `#` の行は読み飛ばし、実行した行ぶんの `ok` が上から順に出る。命令の実行中はゲームの時間が進まない
     （止めていない山猫を置いて 6 行実行しても、山猫は動かない。対照として、そのあと 3 ターン過ごすと動く）。
  3. `place`: 置いたマスに敵が来ている（フレームの敵の位置）。地図の外・壁・ほかの敵がいるマス・プレイヤーのマスは `fail` で、
     敵は動かない。fail のあとの行は実行されず `skip`（行の中身つき）が出る。
  4. `cast`（BR_ 3 種・BO_ 3 種・BA_ 2 種）: 形・属性・撃った者・範囲が原作どおりで、撃った位置が `place` で置いたマス。
     `cast BLINK`・`melee`・`cast MISSILE nearest` も通る。
  5. `hold`: 止めた敵は 10 ターン過ごしても動かず、投射が出ず、行動のメッセージが出ず、HP が減らない。入口の `cast` / `melee` は働く。
     `off` で元に戻る（3 ターンで動く）。`hold all on` の間は、あとから呼んだ敵も止まる。`hold all off` で戻る。
  6. 失敗の形: 知らない魔法・存在しない m_idx・隣にいないときの `melee`・知らない命令・引数の誤りが `fail` になり、ゲームは続く。

安全: 必ずサンドボックス（`tools/gate/sandbox.py` の `Sandbox.create`）で流す。音は鳴らさない（`HENGBAND_MUTE=1`）。
開発ツリー本体のセーブ・cfg は書かない。1 コアごとに上限時間（`DEADLINE_S`）。起こしたプロセスは自分で終わらせる。
"""
from __future__ import annotations

import argparse
import os
import shutil
import sys
import time
import traceback
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(REPO / "tools" / "gate" / "fixtures"))
sys.path.insert(0, str(REPO / "tools" / "gate"))
sys.path.insert(0, str(HERE))

for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

import attack_fx_live_common as C  # noqa: E402
import sandbox as sbx  # noqa: E402
from fxdriver import (C_FF, C_GX, C_GY, C_MONSTER, C_SLOT, FEAT_DOOR, FEAT_DOOR_OPEN, FEAT_PASSABLE, FEAT_PLAYER, FEAT_WALL,  # noqa: E402
                      Core, CoreError, default_ui_state, player_pos)
from fxlib import DIR_KEYS_DIGITS, dismiss, more_pending  # noqa: E402

EXES = {"hengband": "HengbandCore.exe", "tangband": "TangbandCore.exe"}
#: 呼ぶ敵（変愚・短愚とも r_info の 21 番＝大白ヘビ。遅くて弱い。HP 3d6 なのでブレスの威力も小さい）
R_SNAKE = "21"
SNAKE_NAME = "大白ヘビ"
#: コア 1 本あたりの上限（秒）
DEADLINE_S = 480.0
TAG = "[test-monster-act]"


class Checks:
    def __init__(self) -> None:
        self.ok = 0
        self.ng = 0
        self.lines: list[str] = []

    def add(self, cond: bool, label: str, detail: str = "") -> bool:
        if cond:
            self.ok += 1
            print(f"  [OK] {label}", flush=True)
        else:
            self.ng += 1
            print(f"  [NG] {label}  {detail}", flush=True)
        return bool(cond)


class OutOfTime(RuntimeError):
    pass


class Session:
    """コア 1 回の起動と、命令ファイルの受け渡し。"""

    def __init__(self, sb: sbx.Sandbox, core_key: str, with_env: bool, tag: str) -> None:
        self.sb = sb
        self.cmd_path = sb.root / "scratch_old" / "monster_act.txt"
        self.tmp_path = sb.root / "scratch_old" / "monster_act.tmp"
        env = sb.env()
        env.pop("HD2D_TEST_MONSTER_ACT", None)
        if with_env:
            env["HD2D_TEST_MONSTER_ACT"] = str(self.cmd_path)
        self.core = Core(sb.root, EXES[core_key], env_base=env, ui_state=default_ui_state(), tag=tag)
        self.t0 = time.monotonic()

    def check_time(self) -> None:
        if time.monotonic() - self.t0 > DEADLINE_S:
            raise OutOfTime(f"{DEADLINE_S:.0f} 秒")

    # --- 標準エラーの行 ---------------------------------------------------
    def verdicts(self) -> list[str]:
        try:
            text = self.core.err_path.read_bytes().decode("cp932", "replace")
        except OSError:
            return []
        return [ln.strip() for ln in text.splitlines() if TAG in ln]

    # --- 読み込み ----------------------------------------------------------
    def load(self, entry: dict) -> None:
        ack = self.core.hello()
        sbx.check_fixture_version(self.core_key_for_manifest, ack)
        self.core.settle(1.5, 60)
        c = self.core
        # タイトル: New Game / Load / Quit。下へ 1 つ動かして Enter で Load、一覧の先頭を Enter で読み込む
        c.wait_for(lambda f: any("Load" in t for t in c.mirror(f)) or bool(f.get("cells")), "タイトル", timeout=90)
        if not c.ingame_frame() or c.frame.get("pre_game_menu"):
            c.keys("2")
            c.keys("\\r")
            c.wait_for(lambda f: any("Load Game" in t or "FxMage" in t or "a)" in t for t in c.mirror(f)), "セーブの一覧", 30)
            c.keys("\\r")
        # 読み込み中のメッセージは、データ置き場のパスが長いと `-続く-` で止まる。片づけながら待つ
        end = time.monotonic() + 120
        while time.monotonic() < end:
            dismiss(c)
            f = c.frame or {}
            if f.get("cells") and not f.get("menu_open") and not more_pending(c):
                break
            c.settle(0.8, 20)
        else:
            raise CoreError("読み込み後の地図が出ない\n" + c.screen())
        c.settle(1.0, 30)
        dismiss(c)

    core_key_for_manifest = ""

    # --- 操作 --------------------------------------------------------------
    def ready(self) -> None:
        """続きの待ちを片づけ、静かになるまで待つ。"""
        self.check_time()
        dismiss(self.core)
        self.core.settle(0.5, 15)
        dismiss(self.core)

    def tidy(self) -> None:
        """`ready` の軽い版。続きの待ち・能力値の選択が出ているときだけ片づける。"""
        self.check_time()
        self.core.drain(0.2)
        if more_pending(self.core) or "どの能力値を上げますか" in self.core.screen():
            dismiss(self.core)

    def dbg(self, cmd: str) -> None:
        """`^A` + 命令。最初の `^A` では確認の問いが出るので文面で見て y を答える。"""
        c = self.core
        c.keys("\\^A")
        for _ in range(4):
            dismiss(c)
            first = (c.mirror() or [""])[0]
            if "デバッグ・コマンドを使いますか" in c.prompt_text() or "デバッグ・コマンドを使いますか" in first:
                c.keys("y")
            else:
                break
        c.keys(cmd)
        dismiss(c)

    def summon(self, race: int) -> None:
        """種族番号 `race` の敵を 1 体呼ぶ（`0` 番号 `^A n`。回数の入力は、数字のあとに Enter を押さず、そのままコマンドを押す）。時間は使わない。"""
        self.core.keys("0")
        self.core.keys(str(race))
        self.dbg("n")
        self.ready()

    def slot_map(self) -> dict:
        """いま見えている敵の {m_idx: (種族番号, x, y)}（最新の地図のフレーム）。"""
        f = self.core.ingame_frame() or {}
        return {c[C_SLOT]: (c[C_MONSTER], c[C_GX], c[C_GY]) for c in (f.get("cells") or []) if c[C_MONSTER] and c[C_SLOT]}

    def candidates(self, dmin: int, dmax: int) -> list[tuple[int, int]]:
        """プレイヤーからの (dx, dy) で、敵を置けそうなマス（床（階段を含む）で、ドア・敵・プレイヤーのいないマス）。
        原作の距離ではなく、升目の大きいほうの差（dmin 以上 dmax 以下）で選ぶ。上下左右 → 斜め → それ以外、近い順。"""
        f = self.core.ingame_frame() or {}
        mx, my = player_pos(f)
        out = []
        for c in f.get("cells") or []:
            ff = c[C_FF]
            if not (ff & FEAT_PASSABLE) or (ff & (FEAT_WALL | FEAT_DOOR | FEAT_DOOR_OPEN | FEAT_PLAYER)) or c[C_MONSTER]:
                continue
            dx, dy = c[C_GX] - mx, c[C_GY] - my
            d = max(abs(dx), abs(dy))
            if dmin <= d <= dmax:
                kind = 0 if (dx == 0 or dy == 0) else (1 if abs(dx) == abs(dy) else 2)
                out.append((kind, d, dx, dy))
        out.sort()
        return [(dx, dy) for _k, _d, dx, dy in out]

    def wall_candidates(self, dmax: int) -> list[tuple[int, int]]:
        """プレイヤーに近い順の、壁のマスの (dx, dy)。"""
        f = self.core.ingame_frame() or {}
        mx, my = player_pos(f)
        out = []
        for c in f.get("cells") or []:
            if c[C_FF] & FEAT_WALL:
                dx, dy = c[C_GX] - mx, c[C_GY] - my
                d = max(abs(dx), abs(dy))
                if 0 < d <= dmax:
                    out.append((d, dx, dy))
        out.sort()
        return [(dx, dy) for _d, dx, dy in out]

    def player(self) -> tuple:
        return player_pos(self.core.ingame_frame())

    def hp(self) -> int:
        return int(self.core.hud().get("hp", 0))

    def msg_seq(self) -> int:
        """これまでに届いたメッセージの最大の通し番号。"""
        top = 0
        for f in self.core.frames:
            for m in f.get("messages") or []:
                top = max(top, int(m.get("seq", 0)))
        return top

    def wait_turns(self, n: int) -> None:
        """プレイヤーが `,`（その場で 1 ターン過ごす）を n 回押す。毎回、続きの待ちを片づける。"""
        for _ in range(n):
            self.core.keys(",")
            if more_pending(self.core):
                dismiss(self.core)
        self.core.settle(0.4, 10)
        self.tidy()

    def heal(self) -> None:
        self.dbg("a")
        self.ready()

    # --- 命令ファイル --------------------------------------------------------
    def put_command(self, text: str) -> None:
        self.tmp_path.write_text(text, encoding="utf-8")
        os.replace(self.tmp_path, self.cmd_path)

    def act(self, text: str, wait_verdicts: int = 1, timeout: float = 12.0) -> tuple[list[str], list[dict]]:
        """命令を置き、判定の行が `wait_verdicts` 行増えるまで待つ。(新しい判定の行, 命令のあとに届いたフレーム) を返す。"""
        self.tidy()
        before = len(self.verdicts())
        fid0 = (self.core.frame or {}).get("frame_id", 0)
        self.put_command(text)
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            self.core.drain(0.3)
            if len(self.verdicts()) - before >= wait_verdicts:
                break
            # 魔法のメッセージが `-続く-` を出すと、原作の関数はその入力待ちの中で止まる（判定の行はその後に出る）。片づけて進める
            if more_pending(self.core):
                self.core.keys("\s")
        self.core.settle(0.5, 10)
        if more_pending(self.core):
            dismiss(self.core)
            self.core.settle(0.3, 10)
        new = self.verdicts()[before:]
        frames = [f for f in self.core.frames if f.get("frame_id", 0) > fid0]
        return new, frames

    def monsters_on_screen(self) -> list[tuple[int, int]]:
        f = self.core.ingame_frame() or {}
        return [(c[C_GX], c[C_GY]) for c in (f.get("cells") or []) if c[C_MONSTER]]


def is_ok(line: str) -> bool:
    return line.split(TAG, 1)[-1].strip().startswith("ok ")


def is_fail(line: str) -> bool:
    return line.split(TAG, 1)[-1].strip().startswith("fail ")


def messages_of(frames: list[dict]) -> list[str]:
    out: list[str] = []
    for f in frames:
        for m in f.get("messages") or []:
            t = (m.get("text_utf8") or "").strip()
            if t and t not in out:
                out.append(t)
    return out


def messages_after(frames: list[dict], seq0: int) -> list[str]:
    """通し番号が seq0 より大きいメッセージ（それ以前のログが毎回のフレームに載るので、新しいぶんだけ取り出す）。"""
    out: list[str] = []
    for f in frames:
        for m in f.get("messages") or []:
            t = (m.get("text_utf8") or "").strip()
            if t and int(m.get("seq", 0)) > seq0 and t not in out:
                out.append(t)
    return out


def projections_of(frames: list[dict]) -> list[dict]:
    out: list[dict] = []
    seen: set = set()
    for f in frames:
        for p in f.get("projections") or []:
            key = (p.get("q"), p.get("s"), p.get("at"), p.get("sx"), p.get("sy"), p.get("ix"), p.get("iy"))
            if key in seen:
                continue
            seen.add(key)
            out.append(p)
    return out


def install(sb: sbx.Sandbox, core_key: str, name: str) -> dict:
    """検査用のセーブを写す（`sbx.install_fixture` は開発ツリー直下の実行体を見るので、ここでは同じことを自前でする）。"""
    man = sbx.fixture_manifest(core_key)
    entry = man["fixtures"][name]
    for rel in man.get("clean_dirs", []):
        d = sb.root / rel
        if d.is_dir():
            for f in d.iterdir():
                if f.is_file():
                    f.unlink()
    for rec in entry["files"]:
        dst = sb.root / rec["path"]
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(sbx.FIXTURE_SAVES / core_key / name / rec["path"], dst)
    return entry


#: 呼ぶ敵の種族番号（変愚・短愚とも r_info の番号は同じ）。
#: 21 大白ヘビ（遅い・弱い）／62 山猫（常に起きていて速い。放っておけば数ターンで近づいて殴る）／46 見習メイジ（魔法を撃つ）
R_CAT = "62"
R_MAGE = "46"
CAT_NAME = "山猫"
MAGE_NAME = "見習メイジ"


def verdict_kinds(lines: list[str]) -> list[str]:
    """判定の行の種類（ok / fail / skip）の並び。"""
    out = []
    for ln in lines:
        body = ln.split(TAG, 1)[-1].strip()
        out.append(body.split(" ", 1)[0])
    return out


def run_core(core_key: str, exe_dir: Path, work: Path) -> Checks:
    ck = Checks()
    print(f"=== {core_key} ===", flush=True)
    sb = sbx.Sandbox.create(work / core_key, exe_dir=exe_dir)
    sb.force_silent_cfg()
    s = None
    try:
        # ------------------------------------------------------------------ 1. 環境変数が無いとき
        entry = install(sb, core_key, "dungeon")
        s = Session(sb, core_key, with_env=False, tag=f"act_off_{core_key}")
        s.core_key_for_manifest = core_key
        s.load(entry)
        s.ready()
        s.summon(21)
        new, frames = s.act("cast BR_FIRE nearest\n", wait_verdicts=1, timeout=6.0)
        ck.add(not new, "環境変数が無いとき、命令ファイルを置いても判定の行が出ない", f"{new}")
        ck.add(s.cmd_path.exists(), "環境変数が無いとき、命令ファイルは読まれず残る")
        ck.add(not projections_of(frames), "環境変数が無いとき、投射が出ない", f"{projections_of(frames)}")
        s.core.close()
        s = None

        # ------------------------------------------------------------------ 2. 環境変数があるとき
        entry = install(sb, core_key, "dungeon")
        stale = sb.root / "scratch_old" / "monster_act.txt"
        if stale.exists():
            stale.unlink()
        s = Session(sb, core_key, with_env=True, tag=f"act_on_{core_key}")
        s.core_key_for_manifest = core_key
        s.load(entry)
        s.ready()
        s.dbg("w")
        s.ready()
        # 大白ヘビ 2 体（T=撃たせる敵・U=ふさぐ敵）・山猫（K）・見習メイジ（G）を呼ぶ。デバッグの命令は時間を使わない
        for race in (21, 21, R_CAT, R_MAGE):
            s.summon(race)
        mons = s.slot_map()
        snakes = sorted(slot for slot, (race, _x, _y) in mons.items() if race == 21)
        cats = [slot for slot, (race, _x, _y) in mons.items() if race == int(R_CAT)]
        mages = [slot for slot, (race, _x, _y) in mons.items() if race == int(R_MAGE)]
        if not ck.add(len(snakes) == 2 and len(cats) == 1 and len(mages) == 1, "呼んだ敵 4 体の m_idx が画面から分かる", f"{mons}"):
            raise OutOfTime("敵の呼び出しに失敗")
        T, U, K, G = snakes[0], snakes[1], cats[0], mages[0]
        px, py = s.player()

        def at(slot: int):
            """敵 slot の見えている位置（絶対座標）。見えていなければ None。"""
            m = s.slot_map().get(slot)
            return (m[1], m[2]) if m else None

        def rel(slot: int):
            """敵 slot のプレイヤーからの (dx, dy)。見えていなければ None。"""
            p = at(slot)
            return (p[0] - px, p[1] - py) if p else None

        def place_visible(slot: int, cands, label: str, limit: int = 10):
            """候補のマスを順に試し、置けて、しかも画面にその敵が見えるマスを探す。見つかったら (dx, dy)。"""
            tried = []
            for dx, dy in cands[:limit]:
                new, _ = s.act(f"place {slot} {dx} {dy}\n", wait_verdicts=1)
                if new and is_ok(new[0]) and at(slot) == (px + dx, py + dy):
                    return (dx, dy)
                tried.append(((dx, dy), new[0].split(TAG)[-1].strip()[:30] if new else "(判定なし)"))
                if not new or "no-such-monster" in new[0]:
                    break  # 置き場所のせいではない。ほかの候補を試しても同じ
            ck.add(False, f"{label}: 置けて見えるマスが候補に見つかる", f"{tried}")
            return None

        def cells(dmin: int, dmax: int):
            return s.candidates(dmin, dmax)

        # --- 複数行の命令ファイル ------------------------------------------------
        # 空行・`#` の行・前後の空白は読み飛ばす。判定の行は実行した行ぶんだけ、上から順に出る
        new, _ = s.act(f"# 準備\n\nhold {T} on\n   # 注釈\nhold {U} on\n\nhold {G} on\n", wait_verdicts=3)
        ck.add(verdict_kinds(new) == ["ok", "ok", "ok"] and new[0].endswith(f"hold {T} on") and new[2].endswith(f"hold {G} on"),
               "複数行: 空行と # の行は読み飛ばし、実行した 3 行ぶんの ok が上から順に出る", f"{new}")
        s.core.drain(1.0)
        ck.add(len(s.verdicts()) == 3, "複数行: 読み飛ばした行について判定の行が出ない", f"{s.verdicts()}")

        # --- place: 置いたマスに敵が来ている（フレームの敵の位置）---------------------------
        # 山猫は止めない。ほかの 3 体は止めてあり、山猫の行く手をふさがないよう、少し離れた別々のマスへ置く
        up = place_visible(U, cells(5, 6), "place 大白ヘビ U")
        ck.add(up is not None, "place: 置いたマスに大白ヘビ（U）が来ている（フレームの敵の位置）", f"{at(U)}")
        gp = place_visible(G, [c for c in cells(5, 7) if c != up], "place 見習メイジ G")
        ck.add(gp is not None, "place: 置いたマスに見習メイジ（G）が来ている", f"{at(G)}")
        tp = place_visible(T, [c for c in cells(4, 6) if c not in (up, gp)], "place 大白ヘビ T")
        ck.add(tp is not None, "place: 置いたマスに大白ヘビ（T）が来ている", f"{at(T)}")
        kp = place_visible(K, [c for c in cells(3, 3) if c not in (up, gp, tp)], "place 山猫 K")
        if None in (up, gp, tp, kp):
            raise OutOfTime("置けるマスが見つからない")

        # --- 命令の実行中はゲームの時間が進まない ------------------------------------
        # 止めていない山猫を置き直しながら ok の行を 6 つ実行する。時間が進めば山猫は近づく
        new, _ = s.act(f"place {K} {kp[0]} {kp[1]}\nhold {T} on\nplace {K} {kp[0]} {kp[1]}\nhold {U} on\nhold {G} on\nplace {K} {kp[0]} {kp[1]}\n", wait_verdicts=6)
        ck.add(verdict_kinds(new) == ["ok"] * 6, "複数行: 6 行がすべて ok", f"{new}")
        ck.add(rel(K) == kp, "複数行: 命令の実行中は時間が進まず、山猫は置いたマスのまま", f"{rel(K)} 期待 {kp}")
        # 対照: 止めていない山猫は、プレイヤーが数ターン過ごすと動く（近づく）
        s.wait_turns(3)
        ck.add(rel(K) is not None and rel(K) != kp, "対照: 止めていない山猫は、3 ターン過ごすと動く", f"{rel(K)}")
        s.heal()

        # --- place の失敗（地図の外・壁・ふさがったマス・プレイヤーのマス）と skip ------------------
        new, _ = s.act(f"hold {K} on\nplace {T} {tp[0]} {tp[1]}\nplace {T} 9999 0\n", wait_verdicts=3)
        ck.add(verdict_kinds(new) == ["ok", "ok", "fail"] and "out-of-map" in new[2], "place: 地図の外は fail", f"{new}")
        ck.add(rel(T) == tp, "place: 失敗のとき敵は動かない（地図の外）", f"{rel(T)} 期待 {tp}")
        walls = s.wall_candidates(3)
        if ck.add(bool(walls), "地図に壁のマスがプレイヤーの近くにある", f"{walls}"):
            new, _ = s.act(f"place {T} {walls[0][0]} {walls[0][1]}\n", wait_verdicts=1)
            ck.add(len(new) == 1 and is_fail(new[0]) and "cannot-stand-there" in new[0], "place: 壁は fail", f"{new}")
            ck.add(rel(T) == tp, "place: 失敗のとき敵は動かない（壁）", f"{rel(T)} 期待 {tp}")
        new, _ = s.act(f"place {T} {up[0]} {up[1]}\n", wait_verdicts=1)
        ck.add(len(new) == 1 and is_fail(new[0]) and "occupied-by-monster" in new[0], "place: ほかの敵がいるマスは fail", f"{new}")
        ck.add(rel(T) == tp and rel(U) == up, "place: 失敗のとき敵は動かない（ふさがったマス）", f"T={rel(T)} U={rel(U)}")
        new, _ = s.act(f"place {T} 0 0\n", wait_verdicts=1)
        ck.add(len(new) == 1 and is_fail(new[0]) and "player-is-there" in new[0], "place: プレイヤーのマスは fail", f"{new}")
        # fail のあとの行は実行されず skip になる（行の中身が出る）
        new, _ = s.act(f"place {T} 9999 0\nplace {T} {gp[0]} {gp[1]}\nhold {T} off\n", wait_verdicts=3)
        ck.add(verdict_kinds(new) == ["fail", "skip", "skip"] and new[1].endswith(f"place {T} {gp[0]} {gp[1]}") and new[2].endswith(f"hold {T} off"),
               "fail のあとの行は skip になる（行の中身が出る）", f"{new}")
        ck.add(rel(T) == tp, "skip: 飛ばした place は実行されず、敵は動かない", f"{rel(T)} 期待 {tp}")
        # 成功した place は何度でも置き直せる
        again = [c for c in cells(2, 4) if c not in (up, gp, tp, kp)]
        ap = place_visible(T, again, "place 置き直し")
        ck.add(ap is not None, "place: 置き直せる（フレームの敵の位置）", f"{at(T)}")

        # --- そのほかの失敗の形 --------------------------------------------------------
        def single_fail(text: str, needle: str, label: str) -> None:
            new, _ = s.act(text, wait_verdicts=1)
            ck.add(len(new) == 1 and is_fail(new[0]) and needle in new[0], label, f"{new}")

        single_fail("cast NO_SUCH_SPELL nearest\n", "unknown-spell", "知らない魔法は fail")
        single_fail("cast BR_FIRE 9999\n", "no-such-monster", "存在しない m_idx は fail")
        single_fail("frobnicate\n", "unknown-command", "知らない命令は fail")
        single_fail("cast\n", "usage", "魔法の名前の無い cast は fail")
        single_fail(f"hold {T} maybe\n", "usage", "hold の on/off 以外は fail")
        single_fail(f"place {T} x 0\n", "usage", "place の数値が数でないと fail")
        single_fail("place 9999 1 0\n", "no-such-monster", "place: 存在しない m_idx は fail")
        # 隣にいないときの melee（2 行目で fail）
        new, _ = s.act(f"place {T} {tp[0]} {tp[1]}\nmelee {T}\n", wait_verdicts=2)
        ck.add(verdict_kinds(new) == ["ok", "fail"] and "not-adjacent" in new[1], "隣にいないときの melee は fail", f"{new}")

        # --- 準備: 見えている敵 3 体をプレイヤーから遠ざける（撃つ魔法の範囲に入れない）--------------
        # 撃たせる敵 T のほかは、プレイヤーから 7 マス以上離れた所へ。画面に見えなくてもよい
        far = s.candidates(7, 14)
        home: dict[int, tuple[int, int]] = {}

        def park(slot: int) -> bool:
            """敵 slot を、ほかの敵が使っていない遠いマスへ置く（置けたら home に覚える。置き直しは同じマスへ）。"""
            if slot in home:
                new, _ = s.act(f"place {slot} {home[slot][0]} {home[slot][1]}\n", wait_verdicts=1)
                if new and is_ok(new[0]):
                    return True
            for dx, dy in far[:16]:
                if (dx, dy) in home.values():
                    continue
                new, _ = s.act(f"place {slot} {dx} {dy}\n", wait_verdicts=1)
                if new and is_ok(new[0]):
                    home[slot] = (dx, dy)
                    return True
            ck.add(False, f"遠くへ置けるマスが見つかる（m_idx {slot}）", f"{far[:16]}")
            return False

        for slot in (U, G, K, T):
            park(slot)

        def ensure_target(slot: int) -> int:
            """撃たせる敵が生きていること。`hold on` が失敗したら（死んでいる）呼び直して止める。"""
            new, _ = s.act(f"hold {slot} on\n", wait_verdicts=1)
            if new and is_ok(new[0]):
                return slot
            before = set(s.slot_map())
            s.summon(21)
            fresh = [m for m, (race, _x, _y) in s.slot_map().items() if race == 21 and m not in before]
            if fresh:
                slot = fresh[0]
                s.act(f"hold {slot} on\n", wait_verdicts=1)
            return slot

        def place_and_do(slot: int, command: str, cands, label: str):
            """`hold on` → `place` → 命令、を 1 つのファイルで実行する。置き場所の候補を順に試す。返り値は (slot, 判定の行, フレーム, 置いたマス)。"""
            slot = ensure_target(slot)
            s.heal()
            tried = []
            new, frames = [], []
            for dx, dy in cands[:10]:
                new, frames = s.act(f"hold {slot} on\nplace {slot} {dx} {dy}\n{command.replace('@', str(slot))}\n", wait_verdicts=3)
                tried.append(((dx, dy), verdict_kinds(new)))
                if not new or "no-such-monster" in new[0] or "unknown" in new[0] or "usage" in new[0]:
                    break  # 置き場所のせいではない。ほかの候補を試しても同じ
                if verdict_kinds(new) == ["ok", "ok", "ok"]:
                    return slot, new, frames, (dx, dy)
            ck.add(False, f"{label}: 置き場所の候補のどれかで 3 行とも ok になる", f"{tried} {new}")
            return slot, new, frames, None

        # --- 魔法 -------------------------------------------------------------------
        spell_cases = [
            ("BR_FIRE", "breath", "fire", None), ("BR_COLD", "breath", "cold", None), ("BR_POIS", "breath", "pois", None),
            ("BO_FIRE", "bolt", "fire", None), ("BO_COLD", "bolt", "cold", None), ("MISSILE", "bolt", "missile", None),
            ("BA_FIRE", "ball", "fire", (2, 4)), ("BA_POIS", "ball", "pois", (2, 2)),
        ]
        for spell, shape, attr, radii in spell_cases:
            label = f"cast {spell}"
            T, new, frames, pos = place_and_do(T, f"cast {spell} @", s.candidates(2, 4), label)
            if pos is None:
                continue
            ck.add(verdict_kinds(new) == ["ok", "ok", "ok"] and new[2].endswith(f"cast {spell} {T}"), f"{label}: place と cast の 3 行が ok", f"{new}")
            ck.add(not s.cmd_path.exists(), f"{label}: 命令ファイルが消える")
            projs = [p for p in projections_of(frames) if p.get("at") == attr]
            if not ck.add(bool(projs), f"{label}: 次のフレームの projections に属性 {attr} が出る", f"{[C.describe(p) for p in projections_of(frames)]}"):
                continue
            p = projs[0]
            if shape == "breath":
                C.check_breath(ck, label, p, attr=attr, origin=C.ORIGIN_MONSTER)
            elif shape == "bolt":
                C.check_bolt(ck, label, p, attr=attr, origin=C.ORIGIN_MONSTER)
            else:
                C.check_ball(ck, label, p, attr=attr, origin=C.ORIGIN_MONSTER)
                ck.add(C.g(p, "r") in radii, f"{label}: 半径が原作の値（{radii}）", f"r={C.g(p, 'r')}")
            ck.add((C.g(p, "sx"), C.g(p, "sy")) == (px + pos[0], py + pos[1]), f"{label}: 撃った位置が place で置いたマス", f"sx,sy={(C.g(p, 'sx'), C.g(p, 'sy'))} 期待 {(px + pos[0], py + pos[1])}")
            msgs = messages_of(frames)
            ck.add(any(SNAKE_NAME in m for m in msgs), f"{label}: メッセージにモンスターの名前が出る", f"{msgs}")

        # 投射を持たない魔法（敵自身が移動する）
        T, new, frames, pos = place_and_do(T, "cast BLINK @", s.candidates(2, 4), "cast BLINK")
        ck.add(verdict_kinds(new) == ["ok", "ok", "ok"], "cast BLINK: place と cast の 3 行が ok", f"{new}")
        ck.add(bool(messages_of(frames)), "cast BLINK: メッセージが出る", f"{messages_of(frames)}")

        # 直接攻撃（隣へ置いてから）
        T, new, frames, pos = place_and_do(T, "melee @", s.candidates(1, 1), "melee")
        ck.add(verdict_kinds(new) == ["ok", "ok", "ok"], "melee: place と melee の 3 行が ok", f"{new}")
        ck.add(any(SNAKE_NAME in m for m in messages_of(frames)), "melee: メッセージにモンスターの名前が出る", f"{messages_of(frames)}")

        # nearest の指定（ほかの 3 体は遠くにいるので、置いた T がいちばん近い）
        T = ensure_target(T)
        s.heal()
        T, new, frames, pos = place_and_do(T, "cast MISSILE nearest", s.candidates(2, 3), "cast MISSILE nearest")
        ck.add(verdict_kinds(new) == ["ok", "ok", "ok"], "cast MISSILE nearest: place のあとの行が ok", f"{new}")
        ck.add(any(p.get("at") == "missile" for p in projections_of(frames)), "cast MISSILE nearest: missile の投射が出る", f"{[C.describe(p) for p in projections_of(frames)]}")

        # --- hold: 止めている間は動かず、撃たず、殴らない -------------------------------------
        # 山猫は止めて 3 マス先へ。見習メイジは止めて 4 マス先へ。数ターン過ごしても位置が変わらない。T は遠くへ
        T = ensure_target(T)
        park(T)
        s.heal()
        hp0 = s.hp()
        new, _ = s.act(f"hold {K} on\nhold {G} on\n", wait_verdicts=2)
        ck.add(verdict_kinds(new) == ["ok", "ok"], "hold: 山猫と見習メイジを止める", f"{new}")
        kp = place_visible(K, cells(3, 3), "hold 山猫を置く")
        gp = place_visible(G, [c for c in cells(4, 5) if c != kp], "hold 見習メイジを置く")
        if kp is None or gp is None:
            raise OutOfTime("置けるマスが見つからない")
        s.heal()
        hp0 = s.hp()
        fid0 = (s.core.frame or {}).get("frame_id", 0)
        seq0 = s.msg_seq()
        s.wait_turns(10)
        frames = [f for f in s.core.frames if f.get("frame_id", 0) > fid0]
        ck.add(rel(K) == kp, "hold: 止めた山猫は 10 ターン過ごしても動かない", f"{rel(K)} 期待 {kp}")
        ck.add(rel(G) == gp, "hold: 止めた見習メイジは 10 ターン過ごしても動かない", f"{rel(G)} 期待 {gp}")
        ck.add(not projections_of(frames), "hold: 止めている間は敵の投射が出ない", f"{[C.describe(p) for p in projections_of(frames)]}")
        msgs = messages_after(frames, seq0)
        ck.add(not any(CAT_NAME in m or MAGE_NAME in m for m in msgs), "hold: 止めている間は山猫・見習メイジの行動のメッセージが出ない", f"{msgs}")
        ck.add(s.hp() >= hp0, "hold: 止めている間はプレイヤーの HP が減らない", f"{hp0} -> {s.hp()}")
        # 止めていても入口からの命令は働く
        _g, new, frames, _pos = place_and_do(G, "cast MISSILE @", s.candidates(2, 3), "hold 中の見習メイジの cast")
        ck.add(verdict_kinds(new) == ["ok", "ok", "ok"] and any(p.get("at") == "missile" for p in projections_of(frames)),
               "hold: 止めている見習メイジも、入口の cast は働く", f"{new}")
        # 見習メイジが山猫の行く手をふさがないよう、遠くへ戻す
        park(G)
        _k, new, frames, _pos = place_and_do(K, "melee @", s.candidates(1, 1), "hold 中の山猫の melee")
        ck.add(verdict_kinds(new) == ["ok", "ok", "ok"], "hold: 止めている山猫も、入口の melee は働く", f"{new}")
        # 解除で元に戻る
        kp = place_visible(K, cells(3, 3), "hold off 山猫を置く")
        new, _ = s.act(f"hold {K} off\n", wait_verdicts=1)
        ck.add(verdict_kinds(new) == ["ok"] and rel(K) == kp, "hold off: 山猫の止めを解く（置いたマスのまま）", f"{new} {rel(K)}")
        s.wait_turns(3)
        ck.add(rel(K) is not None and rel(K) != kp, "hold off: 解いた山猫は、3 ターン過ごすと動く", f"{rel(K)}")
        s.heal()

        # --- 止めた敵が死んだら記録から外れる（同じ番号に入った別の敵は止まらない）----------------
        # 止めた山猫を隣へ置き、プレイヤーが倒す（止めているので反撃されない）。そのあと山猫を呼び直すと、
        # 同じ番号に入りうる。入らなくても、呼び直した山猫は止まっていない（3 ターンで動く）。
        s.heal()
        new, _ = s.act(f"hold {K} on\n", wait_verdicts=1)
        ap = place_visible(K, cells(1, 1), "倒す山猫を隣へ置く")
        killed = False
        if ap is not None and ap in DIR_KEYS_DIGITS:
            for _ in range(20):
                s.core.keys(DIR_KEYS_DIGITS[ap])
                if more_pending(s.core):
                    dismiss(s.core)
                s.core.drain(0.2)
                if at(K) is None:
                    killed = True
                    break
            s.tidy()
        ck.add(killed, "止めた山猫をプレイヤーが倒せる（止めている間は反撃されない）")
        if killed:
            before = set(s.slot_map())
            s.summon(int(R_CAT))
            fresh = [slot for slot, (race, _x, _y) in s.slot_map().items() if race == int(R_CAT) and slot not in before]
            if ck.add(len(fresh) == 1, "倒したあとに呼び直した山猫の m_idx が分かる", f"{s.slot_map()}"):
                print(f"    (倒した山猫の m_idx {K} / 呼び直した山猫の m_idx {fresh[0]})", flush=True)
                K = fresh[0]
                kp = place_visible(K, cells(3, 3), "呼び直した山猫を置く")
                s.wait_turns(3)
                ck.add(kp is not None and rel(K) is not None and rel(K) != kp, "止めた敵が死んだあと、呼び直した別の敵は止まらない（3 ターンで動く）", f"{rel(K)} 置いたマス {kp}")
        s.heal()

        # --- hold all: あとから呼んだ敵も止まる ---------------------------------------------
        # 先に解いた山猫（K）も、hold all on で止まる
        new, _ = s.act("hold all on\n", wait_verdicts=1)
        ck.add(verdict_kinds(new) == ["ok"], "hold all on が ok", f"{new}")
        kp = place_visible(K, cells(3, 3), "hold all 山猫を置く")
        before = set(s.slot_map())
        s.summon(int(R_CAT))
        fresh = [slot for slot, (race, _x, _y) in s.slot_map().items() if race == int(R_CAT) and slot not in before]
        if ck.add(len(fresh) == 1 and kp is not None, "hold all: あとから呼んだ山猫の m_idx が分かる", f"{s.slot_map()}"):
            K2 = fresh[0]
            k2p = place_visible(K2, [c for c in cells(2, 4) if c != kp], "hold all 呼んだ山猫を置く")
            s.wait_turns(6)
            ck.add(k2p is not None and rel(K2) == k2p, "hold all on: あとから呼んだ山猫も 6 ターン過ごして動かない", f"{rel(K2)} 期待 {k2p}")
            ck.add(rel(K) == kp, "hold all on: 先に解いた山猫も 6 ターン過ごして動かない", f"{rel(K)} 期待 {kp}")
            new, _ = s.act("hold all off\n", wait_verdicts=1)
            ck.add(verdict_kinds(new) == ["ok"], "hold all off が ok", f"{new}")
            s.wait_turns(3)
            ck.add(rel(K2) is not None and rel(K2) != k2p, "hold all off: あとから呼んだ山猫は、3 ターン過ごすと動く", f"{rel(K2)}")
            ck.add(rel(K) is not None and rel(K) != kp, "hold all off: 先に解いた山猫も、3 ターン過ごすと動く", f"{rel(K)}")

        ck.add(s.core.proc.poll() is None, "検査の終わりまでコアが生きている")
    except OutOfTime as e:
        ck.add(False, f"上限時間・中断（{e}）")
    except Exception as e:  # noqa: BLE001
        ck.add(False, f"例外 {type(e).__name__}: {e}", traceback.format_exc(limit=4))
    finally:
        tail = ""
        if s is not None:
            tail = s.core.stderr_tail(8)
            s.core.close()
        if ck.ng:
            print("--- 標準エラーの末尾 ---\n" + tail, flush=True)
        try:
            sb.destroy()
        except OSError:
            pass
    print(f"  => {core_key}: OK {ck.ok} / NG {ck.ng}", flush=True)
    return ck


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--heng-dir", help="HengbandCore.exe の置き場")
    ap.add_argument("--tang-dir", help="TangbandCore.exe の置き場")
    ap.add_argument("--core", action="append", choices=sorted(EXES))
    ap.add_argument("--work", default=str(Path(os.environ.get("TEMP", str(REPO / ".taskwork"))) / "hb_ma"), help="作業フォルダ。データ置き場のパスが長いと読み込みで -続く- が出るので短めに")
    args = ap.parse_args()
    dirs = {"hengband": args.heng_dir, "tangband": args.tang_dir}
    cores = args.core or [c for c in sorted(EXES) if dirs[c]]
    work = Path(args.work)
    work.mkdir(parents=True, exist_ok=True)
    total_ng = 0
    for core in cores:
        exe_dir = Path(dirs[core]) if dirs[core] else REPO
        if not (exe_dir / EXES[core]).is_file():
            print(f"[NG] 実行体が無い: {exe_dir / EXES[core]}")
            total_ng += 1
            continue
        total_ng += run_core(core, exe_dir, work).ng
    print("RESULT", "FAIL" if total_ng else "PASS", flush=True)
    return 1 if total_ng else 0


if __name__ == "__main__":
    sys.exit(main())
