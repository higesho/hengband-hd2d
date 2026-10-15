#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Android ボクセル HD2D 版（android/hd2d/）へ入れるアセット一式を組み立てる。

正本は ` ` §4。出力先は ``android/build-assets-hd2d``
（Gradle の ``assets.srcDirs`` に足してある）。リポジトリには入れない生成物。

旧 SDL2 UI 版（``tools/build_android_assets.py``）との違い
----------------------------------------------------------

============================ ======== ==================================================
入るもの                     展開     理由
============================ ======== ==================================================
``lib/``（音声込み）          する     旧版と同じ（コアは stdio。音声は CfgReader が
                                       実ファイルの存在を見る＝A-T12）。
``tilework`` と ``xtra/graf`` **しない** 配布版にはタイル画像を入れない。店・所持品・
                                       品物選択ではUIがボクセル板を直接描く。
``assets/voxel/``            **する** プレハブ（.vox）と対応表（.jsonc）。
                                       ``std::ifstream`` で読む（``prefab_library``）。
``assets/ui/``               しない   ``IMG_Load`` に**相対パス**で渡る（``ui_image.h`` /
                                       ``floor_cutin.h``）ので APK の assets から読める。
``assets/fx/``               しない   攻撃の視覚効果の絵の板。``IMG_Load`` に相対パスで渡る
                                       （``fx_parts.cpp`` の ``fx_sprite_set_dir``）。
``assets/fonts/``            しない   ``TTF_OpenFont``（＝``SDL_RWFromFile``）に相対パスで
                                       渡る。``text_overlay.cpp`` の候補 1 位
                                       ``assets/fonts/placeholder_mono.ttf`` として
                                       Noto Sans Mono CJK JP を置く（コード無改変で拾われる）。
``fonts/``（旧版の置き場）    —        **入れない**。あれは ui/ の font_face 用で、
                                       hd2d はこのモジュールに ui/ を含まない。
``assets/tiles/``            —        **入れない**。hd2d から参照が無い（旧 UI 専用）。
============================ ======== ==================================================

使い方::

    python tools/build_android_hd2d_assets.py
"""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import pathlib
import urllib.request
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
OUT = REPO / "android" / "build-assets-hd2d"
#: 旧版のフォント置き場。取得済みならそこから写して再ダウンロードを避ける。
OLD_FONT_DIR = REPO / "android" / "build-assets" / "fonts"

# lib/ から除くもの（旧版と同じ。`user` だけ 2026-08-24 に足した）。
#
# `user` … **遊んだ跡が溜まる場所**で、git で追いかけている物が 1 つも無い
# （`playrec-*.txt` ・ `playrecord-*.txt` ・ `picktype.prf` ・ `dump.txt`）。
# 名前の前置きで弾く形（`LIB_SKIP_PREFIXES`）は `playrecord-` しか知らず、
# いまの版が書くのは `playrec-` なので**素通りしていた**——`playrec-1000.Shisy.txt`
# のような**遊ぶ人の名前入りの記録が APK に入っていた**（2026-08-24 に実物を確認）。
# ディレクトリ自体は `android_asset_installer.cpp` が無条件に作る。
LIB_SKIP_NAMES = {"Makefile.am", "Makefile.in", "Makefile", "delete.me", "desktop.ini", ".gitkeep"}
LIB_SKIP_DIRS = {"save", "apex", "bone", "data", "user"}
LIB_SKIP_PREFIXES = ("playrecord-",)
LIB_SKIP_SUBDIRS = {("xtra", "graf")}

FONT_NAME = "NotoSansMonoCJKjp-Regular.otf"
FONT_URL = (
    "https://github.com/notofonts/noto-cjk/raw/main/Sans/Mono/NotoSansMonoCJKjp-Regular.otf"
)
FONT_LICENSE_NAME = "LICENSE-NotoSansMonoCJK.txt"
FONT_LICENSE_URL = "https://github.com/notofonts/noto-cjk/raw/main/Sans/LICENSE"
#: text_overlay.cpp の候補 1 位の名前（拡張子は .ttf だが FreeType は中身で判別するので
#: OTF をこの名前で置いてよい）。
FONT_PLACEHOLDER_NAME = "placeholder_mono.ttf"


def log(message: str) -> None:
    print(f"[hd2d-assets] {message}", flush=True)


def copy_if_changed(src: Path, dst: Path) -> None:
    if dst.exists() and dst.stat().st_size == src.stat().st_size \
            and dst.stat().st_mtime >= src.stat().st_mtime:
        return
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)


def copy_rogue_legacy_licenses() -> list[Path]:
    """Bundle the exact license texts for the two restored Rogue cores."""
    upstream = REPO.parent / "roguelike-cores" / "upstream"
    copied: list[Path] = []
    for core, name in (
        ("rogue36", "Rogue36-LICENSE.txt"),
        ("rogue54", "Rogue54-LICENSE.txt"),
    ):
        src = upstream / core / "LICENSE.TXT"
        if not src.is_file():
            raise FileNotFoundError(src)
        dst = OUT / "assets" / "licenses" / name
        copy_if_changed(src, dst)
        copied.append(dst)
    return copied


# --------------------------------------------------------------------- lib/


def collect_lib_files() -> list[tuple[Path, Path]]:
    """``(コピー元, assets 内の相対パス)``。lib/ はすべて展開対象。"""
    out: list[tuple[Path, Path]] = []
    lib = REPO / "lib"
    for src in sorted(lib.rglob("*")):
        if not src.is_file():
            continue
        rel = src.relative_to(REPO)
        parts = rel.parts
        if any(part.startswith(".hd2d-save-") for part in parts):
            continue
        if src.name in LIB_SKIP_NAMES:
            continue
        if src.name.startswith(LIB_SKIP_PREFIXES):
            continue
        if len(parts) >= 2 and parts[1] in LIB_SKIP_DIRS:
            continue
        if len(parts) >= 3 and (parts[1], parts[2]) in LIB_SKIP_SUBDIRS:
            continue
        out.append((src, rel))
    return out


# --------------------------------------------------------------- assets/


def copy_tree_verbatim(rel_root: str) -> list[Path]:
    """``rel_root``（リポジトリ相対）を OUT へそのまま写し、写した先の一覧を返す。"""
    src_dir = REPO / rel_root
    if not src_dir.is_dir():
        sys.exit(f"{rel_root} がありません: {src_dir}")
    out: list[Path] = []
    for src in sorted(src_dir.rglob("*")):
        if not src.is_file():
            continue
        if src.suffix.lower() == ".bak":  # エディタの控えは配らない
            continue
        # **先頭が . のものは写さない。**aapt が隠しファイルとして黙って落とすので、
        # 写して一覧に載せると「APK に無いものを展開しろ」になり、**起動時に落ちる**
        # （2026-08-15。`assets/voxel/slab/.slab_manifest.json` で実際に踏んだ）。
        # 落とすのは aapt の側なので、こちらが先に外しておくしかない。
        if src.name.startswith("."):
            continue
        rel = src.relative_to(REPO)
        dst = OUT / rel
        copy_if_changed(src, dst)
        out.append(dst)
    return out


# ------------------------------------------- 旧 C の 3 コア（幻想蛮怒・Sil-Q・Frox）

#: 同梱する木。**遊ぶのに要るものだけ**を名指しする。
#:
#: 名指しなのは、両コアの木に**作業用の素材が大量に混ざっている**ためである:
#:   - ``gensoband/tilework/sample2_1028`` … 127MB（絵の見比べ用）
#:   - ``silq/tilework/src`` / ``640``     … 306MB（焼く前の原画）
#: 丸ごと写すと APK が 800MB を超える。実際に読まれるのは
#: ``mapping.csv`` / ``terrain_map.csv`` が指す先だけなので、そこを列挙する。
#: **生成物の木**（追跡していない）と、その作り方。無いときに何を走らせればよいかを言う。
#: ここに載っていない木が無いのは「置き忘れ」なので、作り方は出さない。
GENERATED_TREES = {
    "gensoband/lib-en": "python tools/gensoband/build_en_edit.py && "
                        "python tools/gensoband/build_en_file.py && "
                        "python tools/gensoband/build_en_help.py",
    "silq/lib-ja": "python tools/silq/build_ja_edit.py && python tools/silq/build_ja_help.py --all",
    "silq/lib-en": "python tools/silq/build_ja_help.py --all",
    "frox/lib-ja": "python tools/frox/fc_build_ja_edit.py",
}

LEGACY_TREES = (
    # --- 幻想蛮怒
    "gensoband/lib",
    "gensoband/lib-en",          # 作成した英語 edit / file / help（EN E0・E4・E7。生成物）
    "gensoband/lang/en",         # 訳文カタログ（gb_lang が実行時に読む）
    "gensoband/assets/slab",     # hello_ack.asset_roots.slab
    # --- Sil-Q
    "silq/lib",
    "silq/lib-ja",               # 焼いた日本語 edit とマニュアル（M1 P6）
    "silq/lib-en",               # 作成した英語マニュアル（追補 ③。2026-08-23）
    "silq/lang/ja",              # 訳文カタログ（sq_lang）
    "silq/assets/slab",
    # --- FroxComposband
    # `asset_roots.slab` が申告するボクセル板だけを入れる。元の64px画像と
    # 変愚から流用する画像は配布しない。
    # 地形の立体はプレハブ側なので、ここには要らない。
    "frox/lib",
    "frox/lib-ja",               # 作成した日本語 edit（M1 J3。生成物）
    "frox/lang/ja",              # 訳文カタログ（fc_lang が実行時に読む）
    "frox/assets/slab",          # 実体の板 473 枚（fc_main の asset_roots.slab）
)

#: 木の外にある 1 枚もの。目録そのものなので、これが無いと絵が 1 枚も出ない。
LEGACY_FILES = (
    "gensoband/tilework/mapping.csv",
    "silq/tilework/terrain_map.csv",
    "frox/tilework/terrain_map.csv",
    "frox/tilework/mapping.csv",
    "rogue2/mesg",
)

#: ``<コア>/lib`` の下で写さないもの。
#:
#: - ``save`` … **開発中のセーブが入っている**（`gensoband/lib/save` に 30 枠ほど）。
#:   配ると他人の冒険が入った APK になる。実行時のディレクトリは installer が作る。
#: - ``user`` … 同じ理由（2026-08-24 に足した）。遊んだ跡（`autodump_*.txt` ・
#:   `playrecord-*.txt` ・ `picktype.prf` ・キャラクターの記録）が溜まる場所で、
#:   git で追いかけているのは `delete.me` / `.keepalive` だけである。**コアが読む物は
#:   1 つも無い**（読ませる設定は `lib/pref` の側）。ディレクトリ自体は要る
#:   （`fc_validate_dir(ANGBAND_DIR_USER, 1)` は在ることを求める）ので installer が作る。
#: - ``apex`` / ``bone`` / ``data`` … 生成物（`data` の raw は初回起動で焼き直される）。
#: - ``xtra/music`` / ``xtra/sound`` … 93MB の BGM と効果音。**Android では鳴らない**
#:   （`gb_audio.c` が空実装。`gensoband/adapter/gb_audio.c` の註）。鳴らせるように
#:   なったらここを外す。
LEGACY_LIB_SKIP_DIRS = {"save", "apex", "bone", "data", "user"}
LEGACY_LIB_SKIP_SUBDIRS = {("xtra", "music"), ("xtra", "sound"), ("xtra", "graf")}

#: **Frox の日本語側の作り直しの印は入れない**（`frox/lib-ja/edit/help_upd.txt`）。
#: コアは起動時にこの印の版を見て、実行体と一致していたら `generate_spoilers()` を
#: **飛ばす**（`frox/src/init2.c` の `init_help_files()`）。種族・職業・性格など
#: 13 本の本はそこで作られるので、**中身が英語のまま印だけ合っていると、
#: 遊ぶ人が何度日本語で起動しても英語のままになり、遊ぶ側では直せない**。
#: 印を入れなければ初回の日本語起動で必ず作り直される。
#: 英語側（`frox/lib/edit/help_upd.txt`）はそのままでよい。
#: Windows の配布物にも同じ手当てがある（`tools/package/Build-Hd2dPackage.ps1`）。
SKIP_EXACT = {("frox", "lib-ja", "edit", "help_upd.txt")}


def collect_legacy_files() -> list[Path]:
    """旧 C の 3 コアのデータを写し、写した先の一覧を返す。"""
    out: list[Path] = []
    for rel_root in LEGACY_TREES:
        src_dir = REPO / rel_root
        if not src_dir.is_dir():
            #: **作り方まで言う。** 生成物の木（`lib-ja` / `lib-en`）は追跡していないので、
            #: 別の機体や git clean のあとでは必ず無い。素の「がありません」だけだと、
            #: 何を走らせればよいか分からずに止まる。
            hint = GENERATED_TREES.get(rel_root)
            tail = f"\n  先にこれを走らせること: {hint}" if hint else ""
            sys.exit(f"{rel_root} がありません: {src_dir}{tail}")
        parts_root = tuple(pathlib.PurePosixPath(rel_root).parts)
        for src in sorted(src_dir.rglob("*")):
            if not src.is_file():
                continue
            if src.suffix.lower() == ".bak" or src.name.startswith("."):
                continue
            if src.name in LIB_SKIP_NAMES:
                continue
            rel = src.relative_to(REPO)
            parts = rel.parts
            if any(part.startswith(".hd2d-save-") for part in parts):
                continue
            if parts in SKIP_EXACT:
                continue
            # `<コア>/lib/<ここ>/...` を見る。木が `<コア>/lib` のときだけ効く。
            if (len(parts_root) == 2) and (parts_root[1] == "lib") and (len(parts) >= 3):
                if parts[2] in LEGACY_LIB_SKIP_DIRS:
                    continue
                if (len(parts) >= 4) and ((parts[2], parts[3]) in LEGACY_LIB_SKIP_SUBDIRS):
                    continue
            dst = OUT / rel
            copy_if_changed(src, dst)
            out.append(dst)

    for rel_name in LEGACY_FILES:
        src = REPO / rel_name
        if not src.is_file():
            sys.exit(f"{rel_name} がありません: {src}")
        dst = OUT / rel_name
        copy_if_changed(src, dst)
        out.append(dst)
    return out


def fetch_font() -> None:
    """等幅の日本語フォントを ``assets/fonts/placeholder_mono.ttf`` として同梱する。"""
    font_dir = OUT / "assets" / "fonts"
    font_dir.mkdir(parents=True, exist_ok=True)
    font_path = font_dir / FONT_PLACEHOLDER_NAME
    license_path = font_dir / FONT_LICENSE_NAME

    if font_path.exists() and font_path.stat().st_size > 1_000_000:
        log(f"フォントは配置済み: {font_path.name}")
    else:
        old = OLD_FONT_DIR / FONT_NAME
        if old.exists() and old.stat().st_size > 1_000_000:
            log(f"フォントを旧版の生成物から写す: {old}")
            shutil.copy2(old, font_path)
        else:
            log(f"フォントを取得: {FONT_URL}")
            urllib.request.urlretrieve(FONT_URL, font_path)
        log(f"  -> {font_path.stat().st_size / 1024 / 1024:.1f}MB")

    if not license_path.exists():
        old = OLD_FONT_DIR / FONT_LICENSE_NAME
        if old.exists():
            shutil.copy2(old, license_path)
        else:
            log("フォントのライセンス（SIL OFL 1.1）を取得")
            urllib.request.urlretrieve(FONT_LICENSE_URL, license_path)


# ------------------------------------------------------------------- 組み立て


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--without-rogue-licenses", action="store_true",
                        help="UI/import-only asset bundle; do not require the packaged Rogue upstream trees")
    args = parser.parse_args()

    OUT.mkdir(parents=True, exist_ok=True)
    extract_entries: list[tuple[str, int]] = []

    # ---- lib/（展開する）
    lib_files = collect_lib_files()
    log(f"lib/ から {len(lib_files)} ファイル")
    for src, rel in lib_files:
        dst = OUT / rel
        copy_if_changed(src, dst)
        extract_entries.append((rel.as_posix(), dst.stat().st_size))

    # ---- tilework/tile_mapping.csv（木の外にある 1 枚もの。画像は入れないが、この 1 枚は要る）
    # `an_manifest.cpp`（`angband/adapter/an_main.cpp` の `tile_mapping_csv_path()`）が
    # 実行時に `<exe_dir>/tilework/tile_mapping.csv` を読み、P/R/K/F の名前を変愚の
    # 地形番号へ名寄せする。ほかのコアは同じ表を `presentation/bridge/official_tile_table.h`
    # として**組み込み時に焼き込む**ので実行時にこの CSV を読まないが、Angband だけは
    # 実行時パースを選んでいる（設計 §6.2）。この 1 枚を積み忘れると `TileMapping::load()`
    # が失敗し、Angband の実体（プレイヤー・モンスター・アイテム）が軒並み ASCII の字へ
    # 落ちる（板 GPU 資源自体は既定の置き場 `assets/voxel/slab` に在るのに、名前を
    # 索引できず引けない）。中身は CSV（テキスト）で画像ではないので、直後のタイル画像
    # 除外（`tilework/sfc` 等）とはぶつからない。
    tile_mapping_src = REPO / "tilework" / "tile_mapping.csv"
    if not tile_mapping_src.is_file():
        sys.exit(f"tilework/tile_mapping.csv がありません: {tile_mapping_src}")
    tile_mapping_dst = OUT / "tilework" / "tile_mapping.csv"
    copy_if_changed(tile_mapping_src, tile_mapping_dst)
    extract_entries.append((tile_mapping_dst.relative_to(OUT).as_posix(), tile_mapping_dst.stat().st_size))

    # ---- タイル画像は配布しない。以前の生成物が残っていても素材束から除く。
    for relative in ("tilework/sfc","gensoband/tilework/64","gensoband/tilework/ascii","silq/tilework/128","frox/tilework/64",
                     "lib/xtra/graf","gensoband/lib/xtra/graf","silq/lib/xtra/graf","frox/lib/xtra/graf"):
        stale=OUT/relative
        if stale.exists():shutil.rmtree(stale)

    # ---- assets/voxel（そのまま写して、展開する）
    voxel_files = copy_tree_verbatim("assets/voxel")
    log(f"assets/voxel から {len(voxel_files)} ファイル")
    for dst in voxel_files:
        rel = dst.relative_to(OUT)
        extract_entries.append((rel.as_posix(), dst.stat().st_size))

    # ---- assets/lang（そのまま写して、展開する）
    # 言語カタログ。`hd2d::i18n::load()` は std::ifstream で読む（APK の assets からは
    # 読めない）ので、voxel と同じく実ファイルに展開する。無いと言語メニューが空になる。
    lang_files = copy_tree_verbatim("assets/lang")
    log(f"assets/lang から {len(lang_files)} ファイル")
    for dst in lang_files:
        rel = dst.relative_to(OUT)
        extract_entries.append((rel.as_posix(), dst.stat().st_size))

    # ---- assets/audio（写して、展開する。 の 3 番）
    # 画面側の音（`hd2d/audio/`）は目録（`sfx.jsonc` / `ambience.jsonc`）を std::ifstream で、
    # wav を `SDL_LoadWAV` で**作業ディレクトリからの相対パス**で読む。Android では
    # 起動時に内部ストレージへ chdir してから展開するので、voxel と同じく実ファイルにする。
    # 効果音の本体は `sfx.jsonc` の `dir`＝`lib/xtra/sound`（上の lib/ に入っている）と
    # `assets/audio/sfx_rogue/`。ベッドと層は `assets/audio/bed/`（追跡外。在れば入れる）。
    # `assets/audio/ai/` は生成 AI の候補置き場で、実行時に読まないので入れない。
    audio_files = []
    for name in ("ambience.jsonc", "sfx.jsonc"):
        src = REPO / "assets" / "audio" / name
        if not src.is_file():
            sys.exit(f"assets/audio/{name} がありません: {src}")
        dst = OUT / "assets" / "audio" / name
        copy_if_changed(src, dst)
        audio_files.append(dst)
    audio_files += copy_tree_verbatim("assets/audio/sfx_rogue")
    if (REPO / "assets" / "audio" / "bed").is_dir():
        audio_files += copy_tree_verbatim("assets/audio/bed")
    else:
        log("assets/audio/bed が無い（環境音のベッドと層は鳴らない。tools/gen_audio_assets.py で作る）")
    audio_bytes = sum(dst.stat().st_size for dst in audio_files)
    log(f"assets/audio から {len(audio_files)} ファイル / {audio_bytes / 1024 / 1024:.1f}MB")
    for dst in audio_files:
        rel = dst.relative_to(OUT)
        extract_entries.append((rel.as_posix(), dst.stat().st_size))

    # ---- tangband/lib（そのまま写して、展開する）
    # 短愚蛮怒のデータ。
    # 束は 1 つのまま（:quest が共有束を読む流儀に合わせる）なので変愚の電話版にも
    # 数 MB 同梱されるが、TANGBAND ビルド以外は読まない。save 等の実行時ディレクトリは
    # installer が作る（android_asset_installer.cpp の TANGBAND ゲート）。
    for tang_root in ("tangband/lib/edit", "tangband/lib/help"):
        tang_files = copy_tree_verbatim(tang_root)
        log(f"{tang_root} から {len(tang_files)} ファイル")
        for dst in tang_files:
            rel = dst.relative_to(OUT)
            extract_entries.append((rel.as_posix(), dst.stat().st_size))

    # ---- 旧 C の 3 コア（幻想蛮怒・Sil-Q・Frox）のデータ（そのまま写して、展開する）
    legacy_files = collect_legacy_files()
    log(f"幻想蛮怒 / Sil-Q / Frox から {len(legacy_files)} ファイル")
    for dst in legacy_files:
        rel = dst.relative_to(OUT)
        extract_entries.append((rel.as_posix(), dst.stat().st_size))

    # ---- angband/lib（そのまま写して、展開する）
    # Angband 4.2.6。`angband_lib_dir` は
    # `<exe_dir>/angband/lib` を読む。`hd2d/town.txt`（手描きの町。A5）はこの木の中に
    # 在るので、写せば一緒に入る。`angband/{save,user,panic,scores,archive}`（木の外。
    # `angband_data_dir()` の置き場）は開発中のセーブそのものなので**入れない**
    # （旧 C コアの `save`/`user` 除外と同じ理由）。
    # 原作のデータ（gamedata など）はリポジトリに無い。写していなければ止める。
    if not (REPO / "angband/lib/gamedata").is_dir():
        sys.exit("angband/lib/gamedata がありません。先に python tools/core_sources/prepare_original_data.py を実行してください")
    angband_lib_files = copy_tree_verbatim("angband/lib")
    log(f"angband/lib から {len(angband_lib_files)} ファイル")
    for dst in angband_lib_files:
        rel = dst.relative_to(OUT)
        extract_entries.append((rel.as_posix(), dst.stat().st_size))

    # ---- angband/lang/ja と angband/lib-ja（日本語）
    # 訳のカタログ・名前の表と、説明の行だけ日本語にした gamedata の写し。lib-ja は作成物で
    # 追跡しないので、ここで作り直す（an_bootstrap.c が日本語のときだけ <lib>/../lib-ja/gamedata を読む）。
    subprocess.run([sys.executable, str(REPO / "tools" / "angband" / "an_build_ja_gamedata.py"), "build"],
                   cwd=str(REPO), check=True)
    # ゲーム内ヘルプの訳（lib-ja/help。設計 §18。ui-help.c が日本語のときだけ先に探す）
    subprocess.run([sys.executable, str(REPO / "tools" / "angband" / "an_build_ja_help.py"), "build"],
                   cwd=str(REPO), check=True)
    for angband_ja_root in ("angband/lang/ja", "angband/lib-ja"):
        angband_ja_files = copy_tree_verbatim(angband_ja_root)
        log(f"{angband_ja_root} から {len(angband_ja_files)} ファイル")
        for dst in angband_ja_files:
            rel = dst.relative_to(OUT)
            extract_entries.append((rel.as_posix(), dst.stat().st_size))

    # ---- angband/assets/slab（そのまま写して、展開する）
    # `hello_ack.asset_roots.slab`（M2）が申告するボクセル板。変愚から流用できない
    # Angband 固有の実体だけが入っている。
    angband_slab_files = copy_tree_verbatim("angband/assets/slab")
    log(f"angband/assets/slab から {len(angband_slab_files)} ファイル")
    for dst in angband_slab_files:
        rel = dst.relative_to(OUT)
        extract_entries.append((rel.as_posix(), dst.stat().st_size))

    # ---- angband/tilework/mapping.csv（木の外にある 1 枚もの。無いと絵が 1 枚も出ない）
    angband_mapping_src = REPO / "angband" / "tilework" / "mapping.csv"
    if not angband_mapping_src.is_file():
        sys.exit(f"angband/tilework/mapping.csv がありません: {angband_mapping_src}")
    angband_mapping_dst = OUT / "angband" / "tilework" / "mapping.csv"
    copy_if_changed(angband_mapping_src, angband_mapping_dst)
    extract_entries.append((angband_mapping_dst.relative_to(OUT).as_posix(), angband_mapping_dst.stat().st_size))

    # ---- umoria/data と umoria/hd2d（写して、展開する）
    # Umoria 5.7。`um_main.cpp` の `configure_paths` は `<exe_dir>/umoria/data`、
    # `um_town.cpp` の `load_map()` は `<exe_dir>/umoria/hd2d/town.txt` を読む。`umoria/data` は原作の文章で
    # 追跡していないので、固定した原作のコミットから作り直す。セーブとスコア（`data_dir()/umoria/`）は入れない。
    subprocess.run([sys.executable, str(REPO / "tools" / "umoria" / "build_windows.py"), "--data-only"],
                   cwd=REPO, check=True)
    for umoria_root in ("umoria/data", "umoria/hd2d", "umoria/lang"):
        umoria_files = copy_tree_verbatim(umoria_root)
        log(f"{umoria_root} から {len(umoria_files)} ファイル")
        for dst in umoria_files:
            rel = dst.relative_to(OUT)
            extract_entries.append((rel.as_posix(), dst.stat().st_size))

    # ---- assets/ui（そのまま写す。展開しない＝APK の assets から IMG_Load）
    ui_files = copy_tree_verbatim("assets/ui")
    log(f"assets/ui から {len(ui_files)} ファイル（展開しない）")

    # ---- assets/fx（そのまま写す。展開しない＝APK の assets から IMG_Load）
    # 攻撃の視覚効果の絵の板（どくろなど。`hd2d/render/fx_styles/fx_parts.cpp` の
    # `fx_sprite_set_dir("assets/fx")` が相対パスで読む）。無いと絵の板は何も出さない（落ちない）。
    fx_files = copy_tree_verbatim("assets/fx")
    log(f"assets/fx から {len(fx_files)} ファイル（展開しない）")

    # ---- assets/fonts（同梱のみ。TTF_OpenFont が assets から読む）
    fetch_font()

    # ---- 第三者ライセンス表記
    for name in ("LICENSE", "README.md", "readme_angband", "THIRD-PARTY-NOTICES.txt"):
        notices = REPO / name
        if notices.is_file():
            copy_if_changed(notices, OUT / name)
    # OpenAL Soft（LGPL-2.1。libopenal.so として同梱する）の本文。
    openal_copying = REPO / "third_party" / "android" / "openal-soft" / "COPYING"
    if not openal_copying.is_file():
        sys.exit(f"OpenAL Soft の COPYING がありません（tools/fetch_android_deps.ps1 を先に）: {openal_copying}")
    copy_if_changed(openal_copying, OUT / "assets" / "licenses" / "OpenAL-Soft-COPYING.txt")
    if not args.without_rogue_licenses:
        rogue_license_files = copy_rogue_legacy_licenses()
        log(f"Rogue 3.6 / 5.4 license files: {len(rogue_license_files)}")

    # 同梱コンパイラーのヘッダー・パッチ・接続 SDK。実行ファイルは jniLibs へ別途配置する。
    import_kit = REPO / "android/core-import-kit"
    if not (import_kit / "catalog.json").is_file():
        raise FileNotFoundError("Run tools/core_import/package_kit.py before packaging Android")
    for source in sorted(import_kit.rglob("*")):
        if any(part.startswith(".") for part in source.relative_to(import_kit).parts):
            continue  # AAPT が除外する管理用ファイルを展開一覧に載せない。
        if not source.is_file():
            continue
        target = OUT / "core-import" / source.relative_to(import_kit)
        copy_if_changed(source, target)
        extract_entries.append((target.relative_to(OUT).as_posix(), target.stat().st_size))

    image_suffixes={".png",".bmp",".jpg",".jpeg",".webp"}
    forbidden=[]
    for path in OUT.rglob("*"):
        if not path.is_file() or path.suffix.lower() not in image_suffixes:continue
        parts=path.relative_to(OUT).parts
        if "tilework" in parts or parts[:2]==("assets","tiles") or any(parts[i:i+3]==("lib","xtra","graf") for i in range(max(0,len(parts)-2))):
            forbidden.append(path)
    if forbidden:raise RuntimeError(f"Tile image entered Android assets: {forbidden[0]}")

    # ---- assets.manifest（旧版と同じ形式。installer は同じコード）
    digest = hashlib.sha256()
    for name, size in extract_entries:
        digest.update(name.encode("utf-8"))
        digest.update(str(size).encode("ascii"))
        # 同じサイズの SDK 更新も端末へ反映する。
        with (OUT / name).open("rb") as source:
            for block in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(block)
    version = digest.hexdigest()[:16]

    manifest = OUT / "assets.manifest"
    with manifest.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write(version + "\n")
        for name, size in extract_entries:
            handle.write(f"{name}\t{size}\n")

    total = sum(path.stat().st_size for path in OUT.rglob("*") if path.is_file())
    extract_total = sum(size for _name, size in extract_entries)
    log(f"assets.manifest 版 = {version}")
    log(f"展開対象: {len(extract_entries)} ファイル / {extract_total / 1024 / 1024:.1f}MB")
    log(f"アセット合計: {total / 1024 / 1024:.1f}MB  ({OUT})")
    log("※ この APK はサイドロード用の大きさ（Google Play の 200MB には収まらない）。"
        "提出するときは音声の別配信などの削り方を決めること（ANDROID_HD2D_PORT.md §6）")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
