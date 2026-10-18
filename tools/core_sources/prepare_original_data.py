"""組み立てと配布物が読む「原作のゲームデータ」を、固定版の原作から写す。

このリポジトリには原作のデータを置かない（原作のソースと同じ扱い）。
`prepare.py` がソースを外部へ再構成するのと同じく、データも固定コミットの Git オブジェクトから
作業ツリーへ写す。写した物は `.gitignore` で追跡の対象から外してある。

    angband/lib/{gamedata,customize,screens,help}   <- Angband 4.2.6 の lib/ の同名ディレクトリ
    rogue2/mesg                                     <- RogueClone2 の mesg_J（日本語のメッセージ表）
    toband/lib/{edit,file,help,pref,xtra/sound,...}  <- TOband の lib/。原作は EUC-JP なので CP932 に写す
                                                      （フォントと画像は写さない）

コミットは `manifest.json` の `angband`・`rogue2`・`toband` の項に書いてある固定版。
原作のリポジトリは `prepare.py` と同じく既定で `../roguelike-cores/upstream/`
（`--upstream` か `HENGBAND_UPSTREAM_ROOT` で変えられる）。原作の作業ツリーは見ず、
固定コミットのオブジェクトを `core.autocrlf=false` で読む（改行は原作のコミットのまま LF）。

    python tools/core_sources/prepare_original_data.py            # 無い物と、内容（改行を除く）が固定版と違う物を写す
    python tools/core_sources/prepare_original_data.py --force    # すべて写し直す
    python tools/core_sources/prepare_original_data.py --check    # 写し済みか確かめる（書かない）

Angband の `lib/tiles` `lib/fonts` `lib/sounds` `lib/icons` は写さない。
`angband/lib/hd2d/town.txt` と `angband/lib/user/.gitkeep` はこのリポジトリが追跡する自前の物で、触らない。
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
UI_ROOT = HERE.parent.parent
DEFAULT_UPSTREAM = UI_ROOT.parent / "roguelike-cores" / "upstream"

ANGBAND_LIB_DIRS = ("gamedata", "customize", "screens", "help")
# TOband の lib/ のうち写さない物（フォント・画像・曲の置き場。設計 §1 制約 6）
TOBAND_SKIP = ("lib/xtra/font/", "lib/xtra/graf/", "lib/xtra/music/")


def convert(data: bytes, kind: str) -> bytes:
    if kind == "raw":
        return data
    if kind == "eucjp-cp932":
        # 写せない字は止める（黙って置き換えない）。ASCII だけの物はそのまま同じになる
        return data.decode("euc_jp").encode("cp932")
    raise SystemExit("未知の変換: " + kind)


def git(repo: Path, *args: str) -> bytes:
    cmd = ["git", "-C", str(repo), "-c", "safe.directory=" + repo.as_posix(), "-c", "core.autocrlf=false", *args]
    return subprocess.run(cmd, check=True, stdout=subprocess.PIPE).stdout


def pinned_commit(name: str) -> str:
    manifest = json.loads((HERE / "manifest.json").read_text(encoding="utf-8"))
    for source in manifest["sources"]:
        if source["name"] == name:
            return source["commit"]
    raise SystemExit(f"manifest.json に {name} の項がありません")


def list_files(repo: Path, commit: str, prefix: str) -> list[str]:
    out = git(repo, "ls-tree", "-r", "--name-only", "-z", commit, "--", prefix)
    return [p for p in out.decode("utf-8").split("\0") if p]


def plan(upstream: Path) -> list[tuple[Path, Path, str, str, str]]:
    """(原作のリポジトリ, 書き込み先, コミット, 原作内のパス, 変換) の一覧。変換は "raw" か "eucjp-cp932"。"""
    items: list[tuple[Path, Path, str, str, str]] = []
    repo = upstream / "angband"
    commit = pinned_commit("angband")
    for sub in ANGBAND_LIB_DIRS:
        files = list_files(repo, commit, f"lib/{sub}")
        if not files:
            raise SystemExit(f"Angband の固定コミットに lib/{sub} がありません: {repo} @ {commit}")
        for path in files:
            if path.rsplit("/", 1)[-1] == "Makefile":
                continue  # 原作の autotools 用で、遊ぶのにも組むのにも使わない
            items.append((repo, UI_ROOT / "angband" / path, commit, path, "raw"))
    repo = upstream / "rogue2"
    commit = pinned_commit("rogue2")
    items.append((repo, UI_ROOT / "rogue2" / "mesg", commit, "mesg_J", "raw"))
    repo = upstream / "toband"
    commit = pinned_commit("toband")
    for path in list_files(repo, commit, "lib"):
        if path.startswith(TOBAND_SKIP):
            continue
        items.append((repo, UI_ROOT / "toband" / path, commit, path, "eucjp-cp932"))
    return items


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--upstream", type=Path, default=Path(os.environ.get("HENGBAND_UPSTREAM_ROOT", DEFAULT_UPSTREAM)))
    parser.add_argument("--force", action="store_true", help="内容が同じ物も含め、すべて固定版のバイト列で書き直す")
    parser.add_argument("--check", action="store_true", help="写し済みで固定版と一致するか確かめるだけ（書かない）")
    args = parser.parse_args()
    upstream = args.upstream.resolve()
    for name in ("angband", "rogue2", "toband"):
        if not (upstream / name).is_dir():
            raise SystemExit(f"原作のリポジトリがありません: {upstream / name}（--upstream か HENGBAND_UPSTREAM_ROOT で指定）")

    written = same = missing = differ = 0
    for repo, dest, commit, path, kind in plan(upstream):
        data = convert(git(repo, "show", f"{commit}:{path}"), kind)
        if dest.is_file():
            current = dest.read_bytes()
            if current == data:
                same += 1
                continue
            # 改行だけの違い（Windows の autocrlf で写した物）は同じ内容として扱う
            if not args.force and current.replace(b"\r\n", b"\n") == data.replace(b"\r\n", b"\n"):
                same += 1
                continue
            if args.check:
                differ += 1
                print("固定版と違う:", dest.relative_to(UI_ROOT))
                continue
        elif args.check:
            missing += 1
            print("無い:", dest.relative_to(UI_ROOT))
            continue
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data)
        written += 1
    if args.check:
        print(f"一致 {same} / 違う {differ} / 無い {missing}")
        return 1 if (differ or missing) else 0
    print(f"原作のデータ: 書き込み {written} / 既存 {same}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
