#!/usr/bin/env python3
"""持ち物メニュー（新 UI）の命令を、全コアで総当たりに確かめる。

気づいたこと（2026-09-25）「変愚でアイテムを破壊したあと持ち物が参照できなくなってフリーズした。
rogue で出た症状に似ている」を受けて作った。共用の持ち物メニュー
`presentation/bridge/native_inventory_menu.h` を使う 9 本のコアについて、

    inventory_open → 行の命令（inventory:<キー>）→ 出た問いに答える／中止する
    → もう一度 inventory_open → 一覧が出て、次の操作（cancel）を受け付ける

を、一覧の行に出る命令の種類ごとに回す。画面側（`hd2d/ui/modern_store_ui.cpp`）の
振る舞いを写して答える:

  * choices で auto_select があれば、カーソルを合わせて choice（行でなく窓の命令なら window_action）
  * number は既定の数、confirm は「はい」（自動の答え `@answer:` があればそれ）、text は確定
  * message / detail は continue
  * 中止の回（--modes に cancel）は、最初に出た問いを cancel で閉じる

1 回ごとにコアを起こし直し（セーブは作業フォルダの写し。**開発ツリーのセーブには触らない**）、
上限時間を置く。止まったもの（返事が来ない・rejected が続く・一覧が開かない）を一覧にする。

    python tools/hd2d_verify/inventory_actions_audit.py heng
    python tools/hd2d_verify/inventory_actions_audit.py all --modes go,cancel

終了コード 0 = 全部 PASS。
"""
from __future__ import annotations

import argparse
import json
import os
import queue
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
CREATE_NO_WINDOW = 0x08000000 if os.name == "nt" else 0


# ------------------------------------------------------------------ 起動の設定
def _script(text: str) -> list[int]:
    """`\\r` `\\e` `\\s` `\\.`（1 秒待つ＝-1）だけを解く小さな記法。"""
    out: list[int] = []
    i = 0
    while i < len(text):
        c = text[i]
        if c != "\\":
            out.append(ord(c))
            i += 1
            continue
        e = text[i + 1]
        i += 2
        out.append({"r": 13, "e": 27, "s": 32, ".": -1}[e])
    return out


def _hengband_birth(mirror: str, birth_class: str = "") -> list[int]:
    """変愚系（短愚蛮怒）の誕生画面を閉ループで抜ける（tools/tangband/tb_smoke.py と同じ判断）。

    `birth_class` を渡すと、職業の一覧からその名前の行を選ぶ（魔法の本の命令を通すためのメイジなど）。"""
    if birth_class and "職業を選んで" in mirror:
        import re
        m = re.search(r"([a-zA-Z0-9])\)\s*" + re.escape(birth_class), mirror)
        if m:
            return [ord(m.group(1))]
    # --lang en では原作の英語の文言が出る（src/birth/birth-wizard.cpp の "Birth Options ((*)) affect score"、
    # "Choose a sex (%c-%c)" ほか birth-select-*.cpp）。日本語の文言だけを見ると初期オプションの窓で Enter を
    # 押し続け、ゲームに入れない。
    if "初期オプション((*)" in mirror or "Birth Options ((*))" in mirror:
        return [ord("2")] * 12 + [ord("n"), 27]
    for label in ("性別", "種族", "職業", "性格", "魔法"):
        if (label + "を選んで") in mirror:
            return [ord("a")]
    for label in ("sex", "race", "class", "personality", "realm"):
        if ("Choose a " + label + " (") in mirror:
            return [ord("a")]
    if "よろしい" in mirror or "[y/n]" in mirror or "(y/n)" in mirror:
        return [ord("y")]
    return [13]


@dataclass
class CoreSpec:
    name: str
    exe: str
    copy: list[str]                      # データルートへ写す開発ツリーの下のフォルダ
    args: list[str] = field(default_factory=list)
    script: str = ""                     # 誕生・ロードを抜けるキー
    birth_loop: bool = False             # 変愚系の閉ループ誕生
    birth_class: str = ""                # 閉ループ誕生で選ぶ職業（空なら一覧の先頭）
    after_birth: str = ""                # ゲームに入った後に原作のキーで流す下ごしらえ（_script の記法）
    title_load: bool = False             # 変愚のタイトル → Load Game → DBG
    settle_key: int = 32                 # ゲームに入るまで押し続けるキー
    env: dict = field(default_factory=dict)
    empty_dirs: list[str] = field(default_factory=list)
    fixtures: dict = field(default_factory=dict)   # データルートの下のパス → 開発ツリーの下の写し元


CORES: dict[str, CoreSpec] = {
    # 新規キャラ（既定のオプション＝`auto_more` 偽）で遊ぶ。検査用のセーブ DBG は
    # `auto_more` が真なので、気づいたこと（破壊の後の `-続く-`）が再現しない。
    "heng": CoreSpec("heng", "HengbandCore.exe", ["lib"], birth_loop=True),
    # 魔法の本の命令（読む b・唱える m・学ぶ G）は戦士では一覧に出ないので、メイジでも回す
    # （遊ぶ人に届いた報告 2026-10-06「魔導書から呪文を覚えようとすると上手く動作しない」）。
    "heng-mage": CoreSpec("heng-mage", "HengbandCore.exe", ["lib"], birth_loop=True, birth_class="メイジ"),
    # 覚えられる数を使い切った後の「覚える」（G → 1 冊目 → 先頭の呪文で 1 つ覚えてから試行に入る）
    "heng-mage-spent": CoreSpec("heng-mage-spent", "HengbandCore.exe", ["lib"], birth_loop=True, birth_class="メイジ",
                                after_birth=r"Gaa\.\e"),
    # 呪文を選ばずに覚える職業（生命の魔法書。原作の do_cmd_study の LIFE_BOOK の枝）
    "heng-priest": CoreSpec("heng-priest", "HengbandCore.exe", ["lib"], birth_loop=True, birth_class="プリースト"),
    "heng-dbg": CoreSpec("heng-dbg", "HengbandCore.exe", ["lib"], title_load=True,
                         fixtures={"lib/save/DBG": "tools/hd2d_verify/fixtures/DBG"}),
    "tang": CoreSpec("tang", "TangbandCore.exe", ["lib", "tangband/lib"], birth_loop=True),
    "tang-mage": CoreSpec("tang-mage", "TangbandCore.exe", ["lib", "tangband/lib"], birth_loop=True, birth_class="メイジ"),
    "genso": CoreSpec("genso", "GensobandCore.exe", ["gensoband/lib", "gensoband/lib-en", "gensoband/lang"],
                      args=["--savefile=audit"], script=r"b\raaayayay\e222222\r\raudit\r\r\r",
                      empty_dirs=["gensoband/lib/xtra"]),
    # メイジ（名もなき住人 → 女性 → 人間 → メイジ → 領域 元素・予見 → 性格 極めて普通）。魔法の本の命令のため
    "genso-mage": CoreSpec("genso-mage", "GensobandCore.exe", ["gensoband/lib", "gensoband/lib-en", "gensoband/lang"],
                           args=["--savefile=audit"], script=r"b\rbaaybyayayay\e222222\r\raudit\r\r\r",
                           empty_dirs=["gensoband/lib/xtra"]),
    "silq": CoreSpec("silq", "SilCore.exe", ["silq/lib", "silq/lib-en", "silq/lib-ja", "silq/lang"],
                     args=["--lang=ja"], script=r"b\r\r\r\r\r\rAudit\r\r"),
    "frox": CoreSpec("frox", "FroxCore.exe", ["frox/lib", "frox/lib-ja", "frox/lang"], script=r"nn\r\r"),
    # メイジ（標準 → 職業 → 魔法 → メイジ → 領域 2 つとも先頭）。魔法の本の命令のため
    "frox-mage": CoreSpec("frox-mage", "FroxCore.exe", ["frox/lib", "frox/lib-ja", "frox/lang"], script=r"nncdeaa\r\r"),
    "angband": CoreSpec("angband", "AngbandCore.exe", [], args=["--lib={repo}/angband/lib", "--data={root}/angband-data"],
                        settle_key=13),
    "rogue2": CoreSpec("rogue2", "Rogue2Core.exe", [], args=["--mesg={repo}/rogue2/mesg"],
                       env={"HOME": "{root}", "R2_TEST_SEED": "7"}),
    "rogue36": CoreSpec("rogue36", "Rogue36Core.exe", [], env={"ROGUEOPTS": "name=HD2D,fruit=slime-mold"}),
    "rogue54": CoreSpec("rogue54", "Rogue54Core.exe", [], env={"ROGUEOPTS": "name=HD2D,fruit=slime-mold"}),
}


UI_LANG = "ja"


def build_root(spec: CoreSpec, root: Path) -> None:
    """開発ツリーの資料を作業フォルダへ写す。save の中身は写さない（実セーブを使わない）。"""
    def ignore(directory: str, names: list[str]) -> list[str]:
        p = Path(directory)
        out = [n for n in names if p.name == "save" and n not in ("delete.me", ".keepalive", ".gitkeep")]
        if p.name == "lib" and p.parent.name == "gensoband":
            out.append("xtra")      # 音と曲（93MB）。在ることだけ見られる
        return out
    for rel in spec.copy:
        if not (root / rel).exists():
            shutil.copytree(REPO / rel, root / rel, ignore=ignore)
    for rel in spec.empty_dirs:
        (root / rel).mkdir(parents=True, exist_ok=True)
    # 検査用のセーブ（heng-dbg の DBG など）も置く。`reset_saves()` を通らない呼び出し
    # （other_cores_input_check.py）でも、タイトルの Load Game から開けるようにするため。
    # 通る呼び出し（監査本体・menu_tabs_audit）は、そのあと同じ写しをもう一度置くだけで害は無い。
    for rel, src in spec.fixtures.items():
        dst = root / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(REPO / src, dst)


def reset_saves(spec: CoreSpec, root: Path, pristine: Path) -> None:
    """1 回ごとに作業フォルダのセーブを空（＋写し）へ戻す。"""
    for rel in spec.copy:
        save = root / rel / "save"
        if save.is_dir():
            shutil.rmtree(save)
            shutil.copytree(pristine / rel / "save", save)
    for rel, src in spec.fixtures.items():
        shutil.copy2(REPO / src, root / rel)
    for leftover in ("angband-data", "rogue.save", "rogue36.sav", "rogue54.sav", ".rogue.save", "player-identities"):
        p = root / leftover
        if p.is_dir():
            shutil.rmtree(p)
        elif p.exists():
            p.unlink()


# ------------------------------------------------------------------ 通信
class Core:
    def __init__(self, spec: CoreSpec, root: Path, log: Path):
        env = os.environ.copy()
        env["HENGBAND_DATA_ROOT"] = str(root)
        env.setdefault("HENGBAND_MUTE", "1")   # 検査中は曲・効果音を鳴らさない
        for k in ("ROGUEOPTS", "ROGUEOPTS1", "FIGHTER"):
            env.pop(k, None)
        fmt = {"repo": str(REPO), "root": str(root)}
        for k, v in spec.env.items():
            env[k] = v.format(**fmt)
        args = [str(REPO / spec.exe), "--ui-protocol=stdio"] + [a.format(**fmt) for a in spec.args]
        self.errfp = log.open("wb")
        self.proc = subprocess.Popen(args, cwd=str(root), env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=self.errfp, creationflags=CREATE_NO_WINDOW)
        self.q: "queue.Queue[dict | None]" = queue.Queue()
        self.frames: list[dict] = []
        self.eof = False
        self.rid = 0
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self) -> None:
        try:
            out = self.proc.stdout
            while True:
                head = out.read(4)
                if len(head) < 4:
                    break
                size = struct.unpack("<I", head)[0]
                body = b""
                while len(body) < size:
                    chunk = out.read(size - len(body))
                    if not chunk:
                        break
                    body += chunk
                self.q.put(json.loads(body.decode("utf-8")))
        except Exception:  # noqa: BLE001
            pass
        self.q.put(None)

    def send(self, obj: dict) -> None:
        body = json.dumps(obj, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        try:
            self.proc.stdin.write(struct.pack("<I", len(body)) + body)
            self.proc.stdin.flush()
        except OSError as exc:
            # Windows のパイプは、相手（コア）が終了した後に書くと EPIPE ではなく
            # `[Errno 22] Invalid argument` を返す。これまで幻想蛮怒で 1 件だけ出ていた
            # 「Invalid argument」はこれで、原因はコアの終了だった（道具の書き方ではない）。
            # 終了コードと stderr の末尾を添えて、何が起きたか読めるようにする。
            self.eof = True
            try:
                code = self.proc.wait(timeout=5)
            except Exception:  # noqa: BLE001
                code = self.proc.poll()
            tail = ""
            try:
                self.errfp.flush()
                tail = Path(self.errfp.name).read_bytes()[-400:].decode("utf-8", "replace").strip().replace("\n", " | ")
            except Exception:  # noqa: BLE001
                pass
            raise RuntimeError(f"core exited before input (exit={code}; {exc}; stderr tail: {tail})") from exc

    def drain(self, seconds: float) -> int:
        end = time.monotonic() + seconds
        seen = 0
        while True:
            left = end - time.monotonic()
            if left <= 0:
                return seen
            try:
                m = self.q.get(timeout=min(left, 0.1))
            except queue.Empty:
                continue
            if m is None:
                self.eof = True
                return seen
            seen += 1
            if m.get("t") == "frame":
                self.frames.append(m)

    def settle(self, quiet: float = 0.5, cap: float = 4.0) -> None:
        end = time.monotonic() + cap
        while time.monotonic() < end and not self.eof:
            if self.drain(quiet) == 0:
                return

    @property
    def f(self) -> dict:
        return self.frames[-1] if self.frames else {}

    @property
    def m(self) -> dict:
        return self.f.get("modern_ui") or {}

    def keys(self, *keys: int) -> None:
        self.send({"t": "keys", "keys": list(keys)})

    def act(self, action: str, target: str = "", value: int = 0, revision: int | None = None) -> int:
        self.rid += 1
        rev = self.m.get("revision", 1) if revision is None else revision
        self.send({"t": "input_event", "events": [{"e": "ui_action", "request_id": self.rid, "revision": max(1, int(rev or 1)),
                                                    "action": action, "target": target, "value": value}]})
        return self.rid

    def wait_reply(self, rid: int, timeout: float = 6.0) -> bool:
        """画面側の「応答待ち」が解ける条件（request_id の返事、または窓が閉じた）を待つ。"""
        end = time.monotonic() + timeout
        start = len(self.frames)
        while time.monotonic() < end and not self.eof:
            self.drain(0.1)
            for fr in self.frames[start:]:
                m = fr.get("modern_ui") or {}
                if int(m.get("request_id") or 0) >= rid or not m.get("stage"):
                    self.settle()
                    return True
        return False

    def close(self) -> None:
        try:
            if self.proc.poll() is None:
                self.proc.kill()
            self.proc.wait(timeout=10)
        except Exception:  # noqa: BLE001
            pass
        self.errfp.close()


def mirror(frame: dict) -> str:
    return "\n".join(x.get("text_utf8", "") for x in frame.get("menu_term_lines") or [])


def in_game(frame: dict) -> bool:
    m = frame.get("modern_ui") or {}
    return bool(frame.get("awaiting_command")) and not frame.get("menu_open") and not frame.get("pre_game_menu") and not m.get("stage")


def boot(spec: CoreSpec, root: Path, log: Path, timeout: float = 120.0) -> Core:
    c = Core(spec, root, log)
    c.send({"t": "hello", "protocol": 1, "ui_name": "inventory-actions-audit", "ui_version": "1", "features": []})
    c.drain(1.0)
    # 画面側（hd2d/app/app_support.cpp の build_ui_state）と同じく、新 UI なら照準の UI も立てる。
    # 画面の言語（--lang）。既定は ja（以前からこの検査は ja を送っていた）。Angband は ui_state.lang で
    # 日本語層が起きるので、en と ja の両方で通す（持ち物の見出しの札が英日で違う）。
    c.send({"t": "ui_state", "modern_ui": True, "targeting_ui": True, "lang": UI_LANG, "view_cells": {"w": 48, "h": 40}})
    c.settle(0.8, 20)
    end = time.monotonic() + timeout
    if spec.title_load:
        c.keys(ord("2"), 13)
        c.settle(0.8, 10)
        c.keys(13)
        c.settle(0.8, 20)
    for k in _script(spec.script):
        if in_game(c.f):
            break
        if k < 0:
            c.drain(1.0)
            continue
        c.keys(k)
        c.settle(0.6, 15)
    while time.monotonic() < end and not in_game(c.f) and not c.eof:
        if spec.birth_loop and c.f.get("pre_game_menu", True) is not False and c.f.get("menu_open"):
            c.keys(*_hengband_birth(mirror(c.f), spec.birth_class))
        else:
            c.keys(spec.settle_key)
        c.settle(0.6, 15)
    if not in_game(c.f):
        c.close()
        raise RuntimeError(f"{spec.name}: could not reach the game (last={summary(c.f)})")
    if spec.after_birth:
        # 下ごしらえの間だけ新 UI を外す（立っていると原作の選択肢が新 UI の窓になり、原作のキーが届かない）
        c.send({"t": "ui_state", "modern_ui": False, "targeting_ui": False, "lang": UI_LANG, "view_cells": {"w": 48, "h": 40}})
        c.settle(0.8, 10)
        for k in _script(spec.after_birth):
            if k < 0:
                c.drain(1.0)
                continue
            c.keys(k)
            c.settle(0.6, 15)
        c.send({"t": "ui_state", "modern_ui": True, "targeting_ui": True, "lang": UI_LANG, "view_cells": {"w": 48, "h": 40}})
        c.settle(0.8, 10)
    return c


def stable_label(label: str | None) -> str:
    """ゲームの時間で勝手に変わる数（光源の残りターン）を伏せる。

    命令の間にターンが進むと、装備中の松明の「(2999ターンの寿命)」が 2998 になる。
    これを「別のアイテムが変わった」と数えると、何にも作用していない命令が FAIL になる
    （heng-dbg の「覚える」で出た。2026-09-26）。
    """
    import re
    text = label or ""
    text = re.sub(r"\(\d+ターンの寿命\)", "(#ターンの寿命)", text)
    # 英語は "(with 2999 turns of light)"（原作 src/flavor/flavor-describer.cpp の光源の寿命の書式）。
    return re.sub(r"\((with )?\d+ turns? of light\)", "(# turns of light)", text)


def summary(frame: dict) -> dict:
    m = frame.get("modern_ui") or {}
    return {"aw": frame.get("awaiting_command"), "menu": frame.get("menu_open"), "kind": m.get("kind"), "stage": m.get("stage"),
            "rev": m.get("revision"), "req": m.get("request_id"), "res": m.get("result"), "auto": m.get("auto_select"),
            "rows": len(m.get("rows") or []), "msg": ((frame.get("messages") or [{}])[-1].get("text_utf8", ""))[:60],
            "prompt": (frame.get("prompt") or {}).get("text_utf8", "")[:60]}


# ------------------------------------------------------------------ 1 回の試行
def open_inventory(c: Core) -> tuple[bool, str]:
    rid = c.act("inventory_open", revision=1)
    if not c.wait_reply(rid):
        return False, "inventory_open: no reply"
    end = time.monotonic() + 5
    while time.monotonic() < end:
        m = c.m
        if m.get("kind") == "inventory" and m.get("stage") == "browse" and m.get("input_ready", True)                 and int(m.get("request_id") or 0) >= rid:
            if m.get("result") != "accepted":
                return False, f"inventory_open: result={m.get('result')}"
            if not m.get("rows"):
                return False, "inventory_open: empty rows"
            return True, ""
        if int(m.get("request_id") or 0) >= rid and m.get("result") == "rejected":
            return False, f"inventory_open: rejected ({summary(c.f)})"
        c.drain(0.2)
    return False, f"inventory_open: browse window never came ({summary(c.f)})"


def close_inventory(c: Core) -> tuple[bool, str]:
    rid = c.act("cancel")
    if not c.wait_reply(rid):
        return False, "cancel: no reply"
    m = c.m
    if int(m.get("request_id") or 0) >= rid and m.get("result") == "rejected":
        return False, f"cancel: rejected ({summary(c.f)})"
    end = time.monotonic() + 4
    while time.monotonic() < end and not in_game(c.f):
        c.drain(0.2)
    if not in_game(c.f):
        return False, f"cancel: did not return to the command prompt ({summary(c.f)})"
    return True, ""


def run_trial(spec: CoreSpec, root: Path, work: Path, action: str, row_label: str, mode: str, verbose: bool,
              equipped: bool = False) -> tuple[str, str, list[str]]:
    """戻り値: (PASS|FAIL|SKIP, 詳細, 手順の記録)"""
    log = work / f"{spec.name}-{action.replace(':', '_')}-{'e' if equipped else 'p'}-{mode}.stderr.log"
    trail: list[str] = []
    c = boot(spec, root, log)
    try:
        ok, why = open_inventory(c)
        if not ok:
            return "FAIL", "first " + why, trail
        rows = c.m.get("rows") or []
        def offers(r):
            return bool(r.get("equipped")) == equipped and any(a.get("id") == action and a.get("enabled") for a in r.get("actions") or [])
        row = next((r for r in rows if r.get("label") == row_label and offers(r)), None)
        if row is None:
            row = next((r for r in rows if offers(r)), None)
        if row is None:
            return "SKIP", "no row offers the action", trail
        before = [(stable_label(r.get("label")), int(r.get("count") or 0), bool(r.get("equipped"))) for r in rows]
        target_item = (stable_label(row.get("label")), int(row.get("count") or 0), bool(row.get("equipped")))
        command_frame = len(c.frames)
        auto_navigates = 0
        rid = c.act(action, row.get("id", ""))
        trail.append(f"{action} on {row.get('label')}")
        if not c.wait_reply(rid):
            return "FAIL", f"item command: no reply ({summary(c.f)})", trail
        cancelled = False
        invisible: list[str] = []
        navigation = 0
        picked = False      # auto_select で対象のアイテムを選び終えた（以後の choices は後続の窓）
        rejected_in_row = 0
        last_continue_body = None   # 行を持たないカーソル選択で、直前に continue を送ったときの画の本文
        deadline = time.monotonic() + 60
        for _ in range(60):
            if c.eof:
                return "FAIL", "core exited", trail
            if time.monotonic() > deadline:
                return "FAIL", f"time limit in child prompts ({summary(c.f)})", trail
            f, m = c.f, c.m
            st = m.get("stage") or ""
            if int(m.get("request_id") or 0) >= c.rid and m.get("result") == "rejected":
                rejected_in_row += 1
                if rejected_in_row >= 3:
                    return "FAIL", f"rejected 3 times in a row ({summary(f)})", trail
            else:
                rejected_in_row = 0
            if m.get("kind") == "inventory" and not m.get("input_ready", True):
                # 原作が命令を実行している間の一覧（入力を受けない。NativeInventoryMenu の busy）。
                c.drain(0.2)
                continue
            if m.get("kind") == "inventory" and st == "browse":
                trail.append("root list reopened")
                break
            if in_game(f):
                trail.append("back at the command prompt")
                break
            tg = f.get("targeting") or {}
            if tg.get("active") and not tg.get("preview_only"):
                # 照準（方向・標的）。画面側の照準 UI と同じ aim_* を送る。
                act = "aim_cancel" if (mode == "cancel" and not cancelled) or not tg.get("can_confirm") else "aim_confirm"
                cancelled = cancelled or act == "aim_cancel"
                c.rid += 1
                rid = c.rid
                c.send({"t": "input_event", "events": [{"e": "ui_action", "request_id": rid, "revision": int(tg.get("revision") or 1),
                                                        "action": act, "target": "", "value": 0}]})
                trail.append(f"targeting -> {act}")
                end = time.monotonic() + 6
                while time.monotonic() < end:
                    c.drain(0.1)
                    t2 = c.f.get("targeting") or {}
                    if not t2.get("active") or int(t2.get("request_id") or 0) >= rid:
                        break
                c.settle()
                continue
            if not st:
                # 新 UI の窓を持たない原作の問い（方向・標的など）。画面側は別の UI（照準）や
                # プロンプトの帯で答える。ここでは ESC で閉じ、持ち物メニューの状態が戻るかを見る。
                # **画面に何も出ていない待ち**（帯も窓も無い）は、遊ぶ人から見ると止まっている
                # ので FAIL に数える（2026-09-25 の変愚の破壊は `-続く-` がこれだった）。
                last = ((f.get("messages") or [{}])[-1].get("text_utf8", "")).rstrip()
                # メッセージの欄に「問い」（？で終わる）や［続く］が見えていれば、遊ぶ人は待たれていると分かる。
                asks = last.endswith(("?", "？", "-more-", "-続く-", "［続く］"))
                visible = bool(summary(f)["prompt"].strip()) or bool(f.get("targeting")) or bool(f.get("menu_open")) or asks
                if not visible:
                    invisible.append(summary(f)["msg"])
                trail.append(f"raw prompt{'' if visible else ' (INVISIBLE)'} -> ESC ({summary(f)['prompt'] or summary(f)['msg']})")
                c.keys(27)
                c.settle()
                if not in_game(c.f) and not c.m.get("stage"):
                    c.keys(32)      # Rogue の［続く］は ESC を読み捨てる
                    c.settle()
                continue
            want_cancel = mode == "cancel" and not cancelled and st in ("choices", "cursor", "number", "confirm", "text") \
                and not m.get("auto_select")
            if want_cancel:
                cancelled = True
                rid = c.act("cancel")
                trail.append(f"{st} -> cancel")
            elif st == "choices" and m.get("auto_select"):
                target = m["auto_select"]
                ids = [r.get("id") for r in m.get("rows") or []]
                cur = int(m.get("cursor", -1) if m.get("cursor") is not None else -1)
                if target in ids:
                    at = ids.index(target)
                    key = int((m.get("rows") or [])[at].get("key") or 0)
                    # 画面側と同じ: 英字のキーの行はその字で直接選ぶ（カーソルを動かさない）。
                    direct = ord("a") <= key <= ord("z") or ord("A") <= key <= ord("Z")
                    if not direct and cur >= 0 and cur != at:
                        auto_navigates += 1
                        rid = c.act("navigate", target, 2 if at > cur else 8)
                        trail.append("choices -> navigate (hidden)")
                    else:
                        picked = True
                        rid = c.act("choice", target)
                        trail.append(f"choices -> choice (auto_select{', direct' if direct else ''})")
                else:
                    navigation += 1
                    rid = c.act("window_action", target)
                    trail.append(f"choices -> window_action {target}")
            elif st == "choices" and navigation and not picked:
                # 一覧は対象の行を探して原作の選択肢を移っていったが、見つからないまま諦めた。
                # 原作がその行を選ばせない命令を、一覧がその行に出している。
                return "FAIL", f"the original selector never offered the row ({navigation} source moves)", trail
            elif st == "choices":
                rws = [r for r in m.get("rows") or [] if r.get("available", True)]
                if not rws:
                    rid = c.act("cancel")
                    trail.append("choices (no row) -> cancel")
                else:
                    ids = [r.get("id") for r in m.get("rows") or []]
                    cur = int(m.get("cursor", -1) if m.get("cursor") is not None else -1)
                    at = ids.index(rws[0].get("id"))
                    if cur >= 0 and cur != at:
                        rid = c.act("navigate", rws[0].get("id"), 2 if at > cur else 8)
                        trail.append("choices -> navigate")
                    else:
                        rid = c.act("choice", rws[0].get("id"))
                        trail.append(f"choices -> choice {rws[0].get('label')}")
            elif st == "cursor" and m.get("rows"):
                rid = c.act("choice", m.get("cursor_target", ""))
                trail.append("cursor -> choice")
            elif st == "cursor" and m.get("body") == last_continue_body:
                # 行を持たない原作のカーソル選択で、決定（continue）を送っても画が変わらない。
                # 原作の「閲覧」（Frox の魔法書の「調べる」＝呪文の説明）は、決定で説明を出して
                # **取消（Esc）まで同じ一覧に戻り続ける**ループである（原作の `spell_menu_browse`）。
                # 画面側は遊ぶ人が取消を押して抜ける（窓の取消は常に有効）ので、ここも取消で抜ける。
                # 以前は continue を延々と送り続け、60 手で「too many child steps」と数えていた
                # （道具の誤り。画面側は詰まらない。2026-10-07）。
                rid = c.act("cancel")
                trail.append("cursor -> cancel (the original browse loops until Esc)")
            elif st == "cursor":
                # 行を持たない原作のカーソル選択（呪文の一覧など）。画面側は決定で continue を送る。
                last_continue_body = m.get("body")
                rid = c.act("continue")
                trail.append("cursor -> continue")
            elif st == "number":
                num = f.get("numeric") or {}
                rid = c.act("number", value=int(num.get("value", 1) or 1))
                trail.append(f"number -> {num.get('value')}")
            elif st == "confirm":
                auto = str(m.get("auto_select") or "")
                answer = int(auto[8:]) if auto.startswith("@answer:") else max(0, int(m.get("default_answer", 0) or 0))
                if not auto.startswith("@answer:"):
                    labels = [ch.get("key") for ch in (f.get("prompt") or {}).get("choices") or []]
                    if ord("y") in labels:
                        answer = labels.index(ord("y"))
                    elif ord("Y") in labels:
                        answer = labels.index(ord("Y"))
                rid = c.act("answer", value=answer)
                trail.append(f"confirm -> answer {answer} ({summary(f)['prompt'] or summary(f)['msg']})")
            elif st == "text":
                rid = c.act("text_confirm")
                trail.append("text -> text_confirm")
            elif st in ("message", "detail") or m.get("message_only"):
                rid = c.act("continue")
                trail.append(f"{st} -> continue")
            else:
                rid = c.act("cancel")
                trail.append(f"{st} -> cancel (unknown stage)")
            if not c.wait_reply(rid):
                return "FAIL", f"no reply to the last step ({trail[-1]}; {summary(c.f)})", trail
        else:
            return "FAIL", f"too many child steps ({summary(c.f)})", trail
        # 原作の子の選択肢（アイテムを選ぶ窓）が画面に見えたか。画面側
        # （hd2d/ui/modern_store_ui.cpp の sync）は、auto_select の付いた窓と、その窓と
        # 同じ revision の窓を描かず、直前の一覧を描き続ける。それより前に auto_select の
        # 無いアイテムの選択肢が来ていれば、遊ぶ人にはその窓とカーソルの動きが見える
        # （2026-09-26 の報告「一覧の上からカーソルが対象まで移動しなおす」）。
        after_command = c.frames[command_frame:]
        hidden_revisions = {(fr.get("modern_ui") or {}).get("revision") for fr in after_command
                            if (fr.get("modern_ui") or {}).get("stage") == "choices" and (fr.get("modern_ui") or {}).get("auto_select")}
        first_auto = next((i for i, fr in enumerate(after_command)
                           if (fr.get("modern_ui") or {}).get("stage") == "choices" and (fr.get("modern_ui") or {}).get("auto_select")), None)
        shown = [i for i, fr in enumerate(after_command)
                 if (fr.get("modern_ui") or {}).get("stage") == "choices" and (fr.get("modern_ui") or {}).get("content_type") == "items"
                 and not (fr.get("modern_ui") or {}).get("auto_select")
                 and (fr.get("modern_ui") or {}).get("revision") not in hidden_revisions
                 and (first_auto is None or i < first_auto)]
        trail.append(f"selector frames: hidden={sum(1 for fr in after_command if (fr.get('modern_ui') or {}).get('revision') in hidden_revisions)}"
                     f" shown_before_auto={len(shown)} hidden_navigates={auto_navigates}")
        if shown:
            return "FAIL", f"the native item selector was painted before auto_select ({len(shown)} frames)", trail
        # 命令の後、一覧が開き直すまでに「一覧が閉じた画」（新 UI の窓が無い＝画面側は
        # マップだけを出す）が送られていないか（気づいたこと 2026-09-26「壊したあと一度
        # マップ画面に切り替わらず、そのまま持ち物一覧で完結させてほしい」）。
        # 照準（targeting.active）と、原作の問いの帯（方向など。prompt）の画は、マップを
        # 見せる所なので除く（除いた数は trail に残す）。
        closed = aimed = busy = bridged = 0
        for at, fr in enumerate(after_command):
            mm = fr.get("modern_ui") or {}
            if mm.get("kind") == "inventory" and mm.get("stage") == "browse" and mm.get("input_ready", True):
                break
            busy += mm.get("kind") == "inventory" and not mm.get("input_ready", True)
            if mm.get("stage"):
                continue
            tg = fr.get("targeting") or {}
            last = ((fr.get("messages") or [{}])[-1].get("text_utf8", "")).rstrip()
            # 「？」で終わるメッセージだけの問い（Rogue 系）。画面側（modern_store_ui.cpp の sync）が
            # 0.3 秒だけ一覧を出し続け、その間に選択肢が来なければマップと問いを見せる。
            if (tg.get("active") and not tg.get("preview_only")) or ((fr.get("prompt") or {}).get("text_utf8") or "").strip()                     or last.endswith(("?", "？")):
                aimed += 1
                continue
            nxt = (after_command[at + 1].get("modern_ui") or {}) if at + 1 < len(after_command) else {}
            if nxt.get("stage"):
                # 窓の無い画のすぐ後に窓が来た。画面側（modern_store_ui.cpp の sync）は窓の無い画を
                # 0.3 秒だけ一覧のまま出し続けるので、その間に次の窓が来ればマップは見えない。
                bridged += 1
                continue
            closed += 1
        # 命令の結果のメッセージ（持ち物が変わらない命令——呪文を覚える G など——は、効いたかをここでしか見られない）
        said: list[str] = []
        for fr in after_command:
            for msg in (fr.get("messages") or [])[-3:]:
                text = (msg.get("text_utf8") or "").strip()
                if text and text not in said:
                    said.append(text)
        trail.append("messages: " + " / ".join(said[-4:]))
        trail.append(f"list-closed frames: {closed} (aim/prompt frames excluded: {aimed}; bridged by the UI hold: {bridged}; list held while busy: {busy})")
        if closed:
            return "FAIL", f"the inventory closed to the map after the command ({closed} frames)", trail
        # 開き直しの確認。自動で一覧に戻っていれば一度閉じる。
        if c.m.get("kind") == "inventory" and c.m.get("stage") == "browse":
            ok, why = close_inventory(c)
            if not ok:
                return "FAIL", "after command: " + why, trail
        ok, why = open_inventory(c)
        if not ok:
            return "FAIL", "reopen " + why, trail
        trail.append("reopen ok")
        # 対象のアイテムに作用したか。対象が元のままで別のアイテムだけが変わっていれば、
        # 別のアイテムに作用した（装備中の行の破壊で別のアイテムを壊した事故があった）。
        after = [(stable_label(r.get("label")), int(r.get("count") or 0), bool(r.get("equipped"))) for r in c.m.get("rows") or []]
        from collections import Counter
        target_unchanged = Counter(after)[target_item] >= Counter(before)[target_item]
        others_changed = Counter(after) != Counter(before)
        if mode == "go" and target_unchanged and others_changed:
            gone = list((Counter(before) - Counter(after)).elements())
            return "FAIL", f"another item changed while the target stayed ({gone[:2]})", trail
        trail.append("target changed" if not target_unchanged else ("nothing changed" if not others_changed else "target unchanged"))
        ok, why = close_inventory(c)
        if not ok:
            return "FAIL", "reopen then " + why, trail
        trail.append("cancel ok")
        if invisible:
            return "FAIL", f"the core waited with nothing on screen (after: {invisible[0]!r})", trail
        return "PASS", "", trail
    finally:
        if verbose:
            for t in trail:
                print("      ", t)
        c.close()


def enumerate_actions(spec: CoreSpec, root: Path, work: Path) -> list[tuple[str, str, str]]:
    c = boot(spec, root, work / f"{spec.name}-enumerate.stderr.log")
    try:
        ok, why = open_inventory(c)
        if not ok:
            raise RuntimeError(f"{spec.name}: {why}")
        # 命令ごとに、持ち物の行と装備中の行を 1 つずつ（原作が装備を選べない命令を
        # 装備の行に出していないかも見る）。
        seen: dict[tuple[str, bool], tuple[str, str]] = {}
        for row in c.m.get("rows") or []:
            for a in row.get("actions") or []:
                key = (a.get("id", ""), bool(row.get("equipped")))
                if key[0].startswith("inventory:") and a.get("enabled") and key not in seen:
                    seen[key] = (row.get("label", ""), a.get("label", ""))
        return [(k[0], k[1], v[0], v[1]) for k, v in seen.items()]
    finally:
        c.close()


def audit_core(spec: CoreSpec, work: Path, modes: list[str], only: set[str], verbose: bool) -> list[dict]:
    root = work / f"root-{spec.name}"
    root.mkdir(parents=True, exist_ok=True)
    build_root(spec, root)
    pristine = work / f"pristine-{spec.name}"
    if not pristine.exists():
        for rel in spec.copy:
            if (root / rel / "save").is_dir():
                shutil.copytree(root / rel / "save", pristine / rel / "save")
        pristine.mkdir(parents=True, exist_ok=True)
    reset_saves(spec, root, pristine)
    results: list[dict] = []
    actions = enumerate_actions(spec, root, work)
    for action, equipped, row_label, label in actions:
        if only and action not in only:
            continue
        for mode in modes:
            reset_saves(spec, root, pristine)
            t0 = time.monotonic()
            try:
                status, detail, trail = run_trial(spec, root, work, action, row_label, mode, verbose, equipped)
            except Exception as exc:  # noqa: BLE001
                status, detail, trail = "FAIL", f"exception: {exc}", []
            dt = time.monotonic() - t0
            results.append({"core": spec.name, "action": action, "label": label, "row": row_label, "equipped": equipped, "mode": mode,
                            "status": status, "detail": detail, "trail": trail, "seconds": round(dt, 1)})
            print(f"  [{status}] {spec.name:8} {action:14} {label:18} {'equip' if equipped else 'pack':5} {mode:6} {dt:5.1f}s {detail}", flush=True)
    return results


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("cores", help="comma list or 'all': " + ",".join(CORES))
    ap.add_argument("--modes", default="go,cancel")
    ap.add_argument("--only", default="", help="comma list of action ids (inventory:107 ...)")
    ap.add_argument("--work", type=Path, default=None)
    ap.add_argument("--json", type=Path, default=None)
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--lang", choices=("ja", "en"), default="ja", help="ui_state.lang to send (default ja)")
    a = ap.parse_args()
    global UI_LANG
    UI_LANG = a.lang
    names = list(CORES) if a.cores == "all" else a.cores.split(",")
    temp = None
    work = a.work
    if work is None:
        temp = tempfile.TemporaryDirectory(prefix="inventory-audit-")
        work = Path(temp.name)
    work.mkdir(parents=True, exist_ok=True)
    results: list[dict] = []
    try:
        for n in names:
            print(f"== {n} (lang={UI_LANG})", flush=True)
            try:
                results += audit_core(CORES[n], work, a.modes.split(","), set(filter(None, a.only.split(","))), a.verbose)
            except Exception as exc:  # noqa: BLE001
                results.append({"core": n, "action": "*", "label": "", "row": "", "mode": "*", "status": "FAIL",
                                "detail": f"setup: {exc}", "trail": [], "seconds": 0})
                print(f"  [FAIL] {n} setup: {exc}", flush=True)
    finally:
        if a.json:
            a.json.write_text(json.dumps(results, ensure_ascii=False, indent=1), encoding="utf-8")
        if temp is not None:
            temp.cleanup()
    fails = [r for r in results if r["status"] == "FAIL"]
    print(f"\n{len(results) - len(fails)} / {len(results)} passed")
    for r in fails:
        print(f"FAIL {r['core']} {r['action']} {r['label']} {r['mode']}: {r['detail']}")
        for t in r["trail"]:
            print("    ", t)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
