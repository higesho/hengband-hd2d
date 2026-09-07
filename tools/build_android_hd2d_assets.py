#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Android ボクセル HD2D 版（android/hd2d/）へ入れるアセット一式を組み立てる。

（Gradle の ``assets.srcDirs`` に足してある）。リポジトリには入れない生成物。

旧 SDL2 UI 版（``tools/build_android_assets.py``）との違い
----------------------------------------------------------

============================ ======== ==================================================
入るもの                     展開     理由
============================ ======== ==================================================
``lib/``（音声込み）          する     旧版と同じ（コアは stdio。音声は CfgReader が
                                       実ファイルの存在を見る＝A-T12）。
``tilework/sfc/*.png``       **する** 旧版は「IMG_Load が assets から読める」ので
                                       展開しなかったが、**hd2d はコアの asset_manifest
                                       の root と合成した絶対パスで読む**
                                       （``hd2d/assets/tile_catalog.h``）。絶対パスは
                                       assets へ落ちないので実ファイルが要る。
``assets/voxel/``            **する** プレハブ（.vox）と対応表（.jsonc）。
                                       ``std::ifstream`` で読む（``prefab_library``）。
``assets/ui/``               しない   ``IMG_Load`` に**相対パス**で渡る（``ui_image.h`` /
                                       ``floor_cutin.h``）ので APK の assets から読める。
``assets/fonts/``            しない   ``TTF_OpenFont``（＝``SDL_RWFromFile``）に相対パスで
                                       渡る。``text_overlay.cpp`` の候補 1 位
                                       ``assets/fonts/placeholder_mono.ttf`` として
                                       Noto Sans Mono CJK JP を置く（コード無改変で拾われる）。
``fonts/``（旧版の置き場）    —        **入れない**。あれは ui/ の font_face 用で、
                                       hd2d はこのモジュールに ui/ を含まない。
``assets/tiles/``            —        **入れない**。hd2d から参照が無い（旧 UI 専用）。
============================ ======== ==================================================

使い方::

    python tools/build_android_hd2d_assets.py                 # 既定（タイル 128px）
    python tools/build_android_hd2d_assets.py --skip-tiles    # タイル据え置きで再実行を速く
"""

from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import os
import shutil
import sys
import pathlib
import urllib.request
from pathlib import Path

try:
    from PIL import Image
except ImportError:  # pragma: no cover
    sys.exit("Pillow が要ります: python -m pip install Pillow")

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
# のような**ユーザー名入りの記録が APK に入っていた**（2026-08-24 に実物を確認）。
# ディレクトリ自体は `android_asset_installer.cpp` が無条件に作る。
LIB_SKIP_NAMES = {"Makefile.am", "Makefile.in", "Makefile", "delete.me", "desktop.ini", ".gitkeep"}
LIB_SKIP_DIRS = {"save", "apex", "bone", "data", "user"}
LIB_SKIP_PREFIXES = ("playrecord-",)

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
        if src.name in LIB_SKIP_NAMES:
            continue
        if src.name.startswith(LIB_SKIP_PREFIXES):
            continue
        if len(parts) >= 2 and parts[1] in LIB_SKIP_DIRS:
            continue
        out.append((src, rel))
    return out


# ----------------------------------------------------------------- tilework


def scale_one(job: tuple[Path, Path, int]) -> tuple[Path, bool, str]:
    src, dst, max_side = job
    try:
        with Image.open(src) as image:
            image = image.convert("RGBA")
            width, height = image.size
            if max(width, height) > max_side:
                scale = max_side / float(max(width, height))
                size = (max(1, round(width * scale)), max(1, round(height * scale)))
                # LANCZOS 固定。NEAREST で縮めるとドット絵の細部が飛ぶ
                # （PIXELART_QWEN_METHOD の縮小規則と同じ）。
                image = image.resize(size, Image.LANCZOS)
            dst.parent.mkdir(parents=True, exist_ok=True)
            image.save(dst, format="PNG", optimize=True)
        return (src, True, "")
    except Exception as exc:  # noqa: BLE001 - 1 枚の失敗で全体を止めない
        return (src, False, str(exc))


def build_tiles(max_side: int, jobs: int) -> list[Path]:
    src_dir = REPO / "tilework" / "sfc"
    dst_dir = OUT / "tilework" / "sfc"
    if not src_dir.is_dir():
        sys.exit(f"タイルがありません: {src_dir}")

    sources = sorted(src_dir.glob("*.png"))
    log(f"タイル {len(sources)} 枚を最大 {max_side}px へ縮小します（{jobs} 並列）")
    dst_dir.mkdir(parents=True, exist_ok=True)

    work = [(src, dst_dir / src.name, max_side) for src in sources]
    done = 0
    failed = 0
    with concurrent.futures.ProcessPoolExecutor(max_workers=jobs) as pool:
        for _src, ok, err in pool.map(scale_one, work, chunksize=16):
            done += 1
            if not ok:
                failed += 1
                log(f"  !! 失敗: {_src.name}: {err}")
            if done % 500 == 0:
                log(f"  {done}/{len(work)}")
    if failed:
        sys.exit(f"タイルの縮小に {failed} 件失敗しました。中断します。")
    return [dst_dir / src.name for src in sources]


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
    "gensoband/tilework/64",     # mapping.csv の `tile` 行が指す先
    "gensoband/tilework/ascii",  # gb_manifest.cpp の kAsciiDir
    "gensoband/assets/slab",     # hello_ack.asset_roots.slab
    # --- Sil-Q
    "silq/lib",
    "silq/lib-ja",               # 焼いた日本語 edit とマニュアル（M1 P6）
    "silq/lib-en",               # 作成した英語マニュアル（追補 ③。2026-08-23）
    "silq/lang/ja",              # 訳文カタログ（sq_lang）
    "silq/tilework/128",         # sq_main.cpp の add_entities が並べる先
    "silq/assets/slab",
 # --- FroxComposband
 # **M2 で実体の絵が入った**。`asset_roots.slab` は
    # `frox/assets/slab` を申告するので、板と 64px の絵をここへ並べる。
    # **変愚から流用する 1,531 枚は写さない**——目録が `tilework/sfc/…` を指していて、
    # 板は既定の置き場（`assets/voxel/slab`）で当たるからである。
    # 地形の立体はプレハブ側なので、ここには要らない。
    "frox/lib",
    "frox/lib-ja",               # 作成した日本語 edit（M1 J3。生成物）
    "frox/lang/ja",              # 訳文カタログ（fc_lang が実行時に読む）
    "frox/tilework/64",          # Frox 固有の絵 496 枚（実体 473 ＋ 地形 23）
    "frox/assets/slab",          # 実体の板 473 枚（fc_main の asset_roots.slab）
)

#: 木の外にある 1 枚もの。目録そのものなので、これが無いと絵が 1 枚も出ない。
LEGACY_FILES = (
    "gensoband/tilework/mapping.csv",
    "silq/tilework/terrain_map.csv",
    "frox/tilework/terrain_map.csv",
    "frox/tilework/mapping.csv",
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
LEGACY_LIB_SKIP_SUBDIRS = {("xtra", "music"), ("xtra", "sound")}

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
    # 128px の根拠は Windows の配布一式（tools/build_hd2d_dist.ps1）と同じ。
    # hd2d の実効表示辺（cell_px）は 64〜128px 帯なので、これで見た目は保たれる。
    parser.add_argument("--tile-size", type=int, default=128,
                        help="タイル 1 枚の最大辺（px）。既定 128（配布一式と同じ）")
    parser.add_argument("--skip-tiles", action="store_true",
                        help="タイルの縮小を飛ばす（既に作ってあるとき）")
    parser.add_argument("--jobs", type=int, default=min(16, (os.cpu_count() or 4)),
                        help="縮小の並列数")
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

    # ---- tilework/sfc（縮小して、展開する）
    if args.skip_tiles:
        log("タイルの縮小は飛ばしました（--skip-tiles）")
        tile_files = sorted((OUT / "tilework" / "sfc").glob("*.png"))
        if not tile_files:
            sys.exit("--skip-tiles ですが縮小済みタイルがありません。一度は通常実行が要ります。")
    else:
        tile_files = build_tiles(args.tile_size, args.jobs)
    for dst in tile_files:
        rel = dst.relative_to(OUT)
        extract_entries.append((rel.as_posix(), dst.stat().st_size))

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

    # ---- assets/ui（そのまま写す。展開しない＝APK の assets から IMG_Load）
    ui_files = copy_tree_verbatim("assets/ui")
    log(f"assets/ui から {len(ui_files)} ファイル（展開しない）")

    # ---- assets/fonts（同梱のみ。TTF_OpenFont が assets から読む）
    fetch_font()

    # ---- 第三者ライセンス表記
    notices = REPO / "THIRD-PARTY-NOTICES.txt"
    if notices.exists():
        copy_if_changed(notices, OUT / "THIRD-PARTY-NOTICES.txt")

    # ---- assets.manifest（旧版と同じ形式。installer は同じコード）
    digest = hashlib.sha256()
    for name, size in extract_entries:
        digest.update(name.encode("utf-8"))
        digest.update(str(size).encode("ascii"))
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
        "提出するときは音声の別配信などの削り方を決めること")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
