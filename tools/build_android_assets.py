#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Android 版（android/）へ入れるアセット一式を組み立てる。

出力先は ``android/build-assets``（Gradle の ``assets.srcDirs`` に足してある）。
リポジトリには入れない生成物なので、`.gitignore` で外している。

内訳と、なぜそうするか
----------------------

============================ ======== ==================================================
入るもの                     展開     理由
============================ ======== ==================================================
``lib/`` のゲームデータ       する     コアは stdio（``angband_fopen``）で読む。
                                       APK の assets は stdio では開けない。
``lib/xtra/{sound,music}``   **する** SDL_mixer 自体は ``SDL_RWFromFile`` で assets から
  の音声ファイル本体                   読めるが、**どの音を鳴らすかを決める
                                       ``CfgReader::read_sections`` が
                                       ``is_regular_file`` で実ファイルの存在を確かめて
                                       からでないと項目を登録しない**
                                       （``src/main-unix/unix-cfg-reader.cpp:161``）。
                                       assets のままだと全項目が「割り当て無し」に落ちて
                                       **完全に無音**になる（実機相当で踏んだ）。
``tilework/sfc/*.png``       しない   ``IMG_Load`` が読む。**640px の原本は 1.2GB あり
                                       APK に入らない**ので縮小して積む。
``fonts/``                   しない   ``TTF_OpenFont`` が読む。Android には等幅の
                                       日本語フォントが標準で無いので同梱する。
``assets/ui/*.png``          しない   ``IMG_Load`` が読む（K-47 の背景画）。パネル背景は
                                       **拡大縮小せず切り出す**規則なので、端末の実寸に
                                       合わせて縮めて積む（``--ui-bg-screen``）。
                                       置き場が ``assets/ui/`` の 2 階層なのは、コードが
                                       その相対パスで開くため。
============================ ======== ==================================================

``assets.manifest`` の 1 行目が版で、``<base>/.assets_stamp`` と一致していれば
起動時の展開を丸ごと飛ばす（`platform/android/android_asset_installer.cpp`）。
版は「展開対象のファイル名・サイズ」から算出するので、データを変えれば自動的に変わる。

使い方::

    python tools/build_android_assets.py                 # 既定（タイル 192px）
    python tools/build_android_assets.py --tile-size 128 # もっと小さく
    python tools/build_android_assets.py --skip-tiles    # タイルだけ据え置き（再実行が速い）
"""

from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import os
import shutil
import sys
import urllib.request
from pathlib import Path

try:
    from PIL import Image
except ImportError:  # pragma: no cover
    sys.exit("Pillow が要ります: python -m pip install Pillow")

REPO = Path(__file__).resolve().parent.parent
OUT = REPO / "android" / "build-assets"

"""音声ファイルの拡張子。

以前は「SDL_mixer が assets から直接読めるから展開しない」としていたが、**それでは無音になる**。
鳴らす音を決める `CfgReader::read_sections`（`src/main-unix/unix-cfg-reader.cpp:161`）が
`is_regular_file` で実ファイルの存在を確かめてからでないと項目を登録しないためで、
assets は stdio から見えない。よって**音声も展開する**（この定数はもう分岐に使わない）。
"""
AUDIO_SUFFIXES = {".wav", ".mp3", ".ogg", ".mid", ".midi", ".flac"}

# lib/ から除くもの。ビルド機構と、端末側で作られるべきものを持ち込まない。
LIB_SKIP_NAMES = {"Makefile.am", "Makefile.in", "Makefile", "delete.me", "desktop.ini", ".gitkeep"}
# セーブは端末側で作る。配布物に混ぜると他人のセーブを配ることになる。
LIB_SKIP_DIRS = {"save", "apex", "bone", "data"}
# lib/user に溜まる開発機のプレイ記録も同じ理由で配らない（個人の記録なので）。
LIB_SKIP_PREFIXES = ("playrecord-",)

FONT_NAME = "NotoSansMonoCJKjp-Regular.otf"
FONT_URL = (
    "https://github.com/notofonts/noto-cjk/raw/main/Sans/Mono/NotoSansMonoCJKjp-Regular.otf"
)
FONT_LICENSE_NAME = "LICENSE-NotoSansMonoCJK.txt"
FONT_LICENSE_URL = "https://github.com/notofonts/noto-cjk/raw/main/Sans/LICENSE"


def log(message: str) -> None:
    print(f"[android-assets] {message}", flush=True)


# --------------------------------------------------------------------- lib/


def collect_lib_files() -> list[tuple[Path, Path, bool]]:
    """``(コピー元, assets 内の相対パス, 展開するか)`` を返す。"""
    out: list[tuple[Path, Path, bool]] = []
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
        # lib/<dir>/... の <dir> だけを見る（xtra/sound は残したいので深さを限定する）
        if len(parts) >= 2 and parts[1] in LIB_SKIP_DIRS:
            continue
        # lib/ 配下はすべて展開する（音声を含む。上の AUDIO_SUFFIXES の注記を参照）。
        out.append((src, rel, True))
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


# ------------------------------------------------------------- assets/ui/


#! 旧 2D UI の割り付けの定数の写し。**画面側を変えたらここも直すこと。**
RIGHT_COL_RATIO_MIN = 0.31
RIGHT_COL_RATIO_MAX = 0.35
BOTTOM_ROW_RATIO = 1.0 / 3.0
CONTROLLER_BAR_H_PX = 88


def backdrop_master_size(screen_w: int, screen_h: int) -> dict[str, tuple[int, int]]:
    """端末の画面から、下段・右列のマスタに要る実寸を求める。

    背景画は **拡大縮小せず左上から切り出す**（旧 2D UI と同じ規則）ので、
    マスタがパネルより小さいと足りない部分に下地色が残る。よって
    「その画面で最大になるパネル実寸」がそのまま必要な大きさになる。
    右列の幅は利用者が動かせる（0.31〜0.35）ので**広い側**を、
    下段の幅は右列が最も狭いときに最大になるので**狭い側**から採る。
    """
    game_h = screen_h - CONTROLLER_BAR_H_PX
    bottom_w = screen_w - round(screen_w * RIGHT_COL_RATIO_MIN)
    bottom_h = int(game_h * BOTTOM_ROW_RATIO)
    right_w = round(screen_w * RIGHT_COL_RATIO_MAX)
    return {"bottom": (bottom_w, bottom_h), "right": (right_w, game_h)}


def build_ui_backdrops(screen_w: int, screen_h: int) -> int:
    """K-47 の背景画を端末実寸へ縮めて `assets/ui/` へ置く。

    * パネル背景（下段・右列）は**一様倍率**で縮める。縦横で別の倍率をかけると
      石の柄が伸びる。必要な倍率の大きい側に合わせ、余る辺はそのまま残す。
    * タイトルは `draw_cover`（短辺を合わせて長辺を切る）で**拡大縮小して**描かれるので、
      実寸に意味が無い。原本のまま置く。
    * 置き場が `assets/ui/` なのは、コードが `assets/ui/title.png` という
      相対パスで開くため（旧 2D UI の背景画と同じ）。Android では SDL が APK の
      assets ルートを基準に読むので、この階層のままである必要がある。
    """
    src_dir = REPO / "assets" / "ui"
    dst_dir = OUT / "assets" / "ui"
    if not src_dir.is_dir():
        log("assets/ui/ がありません。背景画は入れません")
        return 0

    want = backdrop_master_size(screen_w, screen_h)
    targets = {
        "panel_bg_bottom.png": want["bottom"],
        "panel_bg_right.png": want["right"],
        "title.png": None,  # 原本のまま
    }
    count = 0
    for name, target in targets.items():
        src = src_dir / name
        if not src.is_file():
            continue
        dst = dst_dir / name
        dst.parent.mkdir(parents=True, exist_ok=True)
        if target is None:
            copy_if_changed(src, dst)
            count += 1
            continue
        with Image.open(src) as image:
            image = image.convert("RGBA")
            sw, sh = image.size
            tw, th = target
            scale = min(1.0, max(tw / sw, th / sh))
            size = (max(1, round(sw * scale)), max(1, round(sh * scale)))
            if size != (sw, sh):
                image = image.resize(size, Image.LANCZOS)
            image.save(dst, format="PNG", optimize=True)
        log(f"  {name}: {sw}x{sh} -> {size[0]}x{size[1]}（要 {tw}x{th}）")
        count += 1
    return count


# ----------------------------------------------------------------- tilework


def build_tiles(max_side: int, jobs: int) -> int:
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
    return len(sources)


# --------------------------------------------------------------------- font


def fetch_font() -> None:
    """等幅の日本語フォントを同梱する。

    Android には**等幅の CJK フォント**が標準で無い。Term ミラーは文字グリッドなので、
    可変幅で描くと枠や 2 カラム目が行ごとにずれる（Windows 版 B-25 と同じ壊れ方）。
    """
    font_dir = OUT / "fonts"
    font_dir.mkdir(parents=True, exist_ok=True)
    font_path = font_dir / FONT_NAME
    if font_path.exists() and font_path.stat().st_size > 1_000_000:
        log(f"フォントは取得済み: {font_path.name}")
    else:
        log(f"フォントを取得: {FONT_URL}")
        urllib.request.urlretrieve(FONT_URL, font_path)
        log(f"  -> {font_path.stat().st_size / 1024 / 1024:.1f}MB")

    license_path = font_dir / FONT_LICENSE_NAME
    if not license_path.exists():
        log("フォントのライセンス（SIL OFL 1.1）を取得")
        urllib.request.urlretrieve(FONT_LICENSE_URL, license_path)


# ------------------------------------------------------------------- 組み立て


def copy_if_changed(src: Path, dst: Path) -> None:
    if dst.exists() and dst.stat().st_size == src.stat().st_size \
            and dst.stat().st_mtime >= src.stat().st_mtime:
        return
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    # 既定 128px の根拠:
    #   - 実効の表示辺（cell_px）は携帯〜板で概ね 64〜96px。1x・1.5x なら 128 で足りる。
    #   - 192px にすると assets 合計が 204MB になり、Google Play の base AAB 上限 200MB を
    #     超える（実測）。128px なら約 152MB に収まる。
    #   - サイドロード配布で容量を気にしないなら `--tile-size 192` を使ってよい。
    parser.add_argument("--tile-size", type=int, default=128,
                        help="タイル 1 枚の最大辺（px）。既定 128（AAB 200MB 制限に収める値）")
    parser.add_argument("--skip-tiles", action="store_true",
                        help="タイルの縮小を飛ばす（既に作ってあるとき）")
    # 背景画（K-47）を縮める基準になる画面。既定 2560x1600 は板の実勢上限で、
    # 携帯（2400x1080 など）はこれに収まる。これより大きい端末では柄が足りず
    # 下地の単色が残る（拡大はしない規則のため）。
    parser.add_argument("--ui-bg-screen", default="2560x1600",
                        help="背景画を合わせる画面の実寸（WxH）。既定 2560x1600")
    parser.add_argument("--jobs", type=int, default=min(16, (os.cpu_count() or 4)),
                        help="縮小の並列数")
    args = parser.parse_args()

    OUT.mkdir(parents=True, exist_ok=True)

    # ---- lib/
    lib_files = collect_lib_files()
    log(f"lib/ から {len(lib_files)} ファイル")
    extract_entries: list[tuple[str, int]] = []
    for src, rel, extract in lib_files:
        dst = OUT / rel
        copy_if_changed(src, dst)
        if extract:
            extract_entries.append((rel.as_posix(), dst.stat().st_size))

    # ---- tilework/
    if args.skip_tiles:
        log("タイルの縮小は飛ばしました（--skip-tiles）")
    else:
        build_tiles(args.tile_size, args.jobs)

    # ---- assets/ui/（K-47 の背景画。端末実寸へ縮める）
    try:
        bg_w, bg_h = (int(v) for v in args.ui_bg_screen.lower().split("x"))
    except ValueError:
        sys.exit(f"--ui-bg-screen の書き方が違います: {args.ui_bg_screen}（例 2560x1600）")
    log(f"assets/ui/ を {bg_w}x{bg_h} に合わせます")
    log(f"  背景画 {build_ui_backdrops(bg_w, bg_h)} 枚")

    # ---- fonts/
    fetch_font()

    # ---- 第三者ライセンス表記
    notices = REPO / "THIRD-PARTY-NOTICES.txt"
    if notices.exists():
        copy_if_changed(notices, OUT / "THIRD-PARTY-NOTICES.txt")

    # ---- assets.manifest
    # 版は「展開対象の名前とサイズ」から作る。データを差し替えれば自動で変わり、
    # 端末側の `.assets_stamp` と食い違って展開し直される。
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
    if total > 190 * 1024 * 1024:
        log("!! 190MB を超えています。Google Play の base AAB 上限（200MB）に近いので、")
        log("!! --tile-size を下げるか、音声を別配信にすることを検討してください。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
