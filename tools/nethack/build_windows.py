#!/usr/bin/env python3
"""NetHackCore.exe（英語版）と NetHackJpCore.exe（日本語版）を Visual Studio のコンパイラ（x64）で組む。

原作は ../roguelike-cores/build-sources/nethack・nethackjp を読むだけで、
何も書き込まない（tools/core_sources/prepare.py --verify が通らなくなるため）。

組む物:
  <リポジトリ>/NetHackCore.exe        英語版
  <リポジトリ>/NetHackJpCore.exe      日本語版
  <リポジトリ>/nethack/data/en|ja/    実行時のデータ（dat/ の写し・makedefs の出力・sysconf・porthelp・タイルの絵 tiles.png / tiles.json）
  <リポジトリ>/nethack/data/slab/     怪物・アイテム・主人公・石像のボクセルの板（原作のタイルの絵から。追跡しない）
  <リポジトリ>/assets/voxel/nh_*      地形のブロック（原作のタイルの絵から。追跡しない）

作業フォルダは <リポジトリ>/.taskwork/nethack_build/<en|ja>/。makedefs は dat/ を作業フォルダへ写してから、
そこの util/ で走らせる（makedefs は ../dat/ へ書くので、build-sources の中では走らせない）。

使い方:
    python tools/nethack/build_windows.py             # 英語版と日本語版
    python tools/nethack/build_windows.py --lang en   # 英語版だけ
    python tools/nethack/build_windows.py --data-only # データだけ作り直す（コンパイラは使わない部分のみ。makedefs は使う）
    python tools/nethack/build_windows.py --skip-assets   # 板とブロックを作り直さない

最後に build_assets.py（板・地形のブロックの組み立てと、規則・地形番号の表の照合）を走らせる。numpy・scipy・Pillow の入った Python
（この環境では `python`〔3.10〕）で走らせること。
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SOURCES = ROOT.parent / "roguelike-cores" / "build-sources"
ADAPTER = ROOT / "nethack" / "adapter"
WORK_ROOT = ROOT / ".taskwork" / "nethack_build"
DATA_ROOT = ROOT / "nethack" / "data"
VCVARS = r"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"

VARIANTS = {
    "en": {"source": "nethack", "exe": "NetHackCore.exe", "ja": False},
    "ja": {"source": "nethackjp", "exe": "NetHackJpCore.exe", "ja": True},
}

# 原作の sys/windows/Makefile.nmake（x64・Release）の ccommon / crelease / ctmpflags に合わせる（nethack/m0_probe/build_probe.py と同じ）
CFLAGS_COMMON = (
    "-c -nologo -D_CRT_NONSTDC_NO_DEPRECATE -D_CRT_SECURE_NO_DEPRECATE -D_LIB "
    "-D_SCL_SECURE_NO_DEPRECATE {charset} -DCRTAPI1=_cdecl -DCRTAPI2=_cdecl -DNDEBUG "
    "-DYY_NO_UNISTD_H -DHAS_STDINT_H -DHAS_INLINE -EHsc -fp:precise -Gd -GF -GS -Gy "
    "-MT -O2 -D_AMD64_=1 -DWIN64 -D_WIN64 -DWIN32 -D_WIN32 -W3 -wd4244 -wd4245 -wd4310 -wd4706 "
    "/utf-8"
)

# 原作の src/ のうち組む物（Makefile.nmake の COREOBJTTY の 122 本と、mdlib・version・nhlua・nhlsel・nhlobj・dlb・date・hacklib）
CORE = """allmain alloc apply artifact attrib ball bones botl calendar cfgfiles cmd coloratt
dbridge decl detect dig display do do_name do_wear dog dogmove dokick dothrow drawing dungeon
earlyarg eat end engrave exper explode extralev files fountain getpos glyphs iactions hack insight
invent isaac64 light lock mail makemon mcastu mhitm mhitu minion mklev mkmap mkmaze mkobj mkroom
mon mondata monmove monst mplayer mthrowu muse music o_init objects objnam options pager pickup
pline polyself potion pray priest quest questpgr read rect region report restore revision rip rnd
role rumors save selvar sfbase sfstruct shk shknam sit sounds sp_lev spell stairs steal steed
strutil symbols sys teleport timeout topten track trap u_init uhitm utf8map vault vision weapon
were wield windows wizard wizcmds worm worn write zap
mdlib version nhlua nhlsel nhlobj dlb date hacklib""".split()
# 日本語版が足す、日本語の表の 4 本（原作の NetHack.vcxproj が組んでいる）
CORE_JA = ["mon_jp", "obj_jp", "spell_jp", "jp_data_lookup"]

LUA = """lapi lauxlib lbaselib lcode lcorolib lctype ldblib ldebug ldo ldump lfunc lgc linit liolib
llex lmathlib lmem loadlib lobject lopcodes loslib lparser lstate lstring lstrlib ltable ltablib
ltm lundump lutf8lib lvm lzio""".split()

LIBS = ("kernel32.lib advapi32.lib gdi32.lib ole32.lib Shell32.lib UserEnv.lib dbghelp.lib "
        "Rpcrt4.lib user32.lib winmm.lib bcrypt.lib ws2_32.lib shlwapi.lib")

# プロトコルと共通の部品（Umoria の組み立てと同じ。tools/umoria/build_windows.py）
CPP_ADAPTER = [ADAPTER / "nh_main.cpp", ADAPTER / "nh_frame.cpp", ADAPTER / "nh_terrain.cpp"]
CPP_SHARED = [
    ROOT / "presentation" / "frame" / "frame_codec.cpp",
    ROOT / "presentation" / "frame" / "protocol_messages.cpp",
    ROOT / "presentation" / "frame" / "protocol_transport.cpp",
    ROOT / "presentation" / "bridge" / "input_event_adapter.cpp",
    ROOT / "platform" / "portable" / "legacy_os.cpp",
]


def run_bat(bat: Path, lines: list[str], log: Path, timeout: int) -> tuple[int, str]:
    """vcvars64.bat を通した .bat を作って cmd で走らせる。メッセージは英語（VSLANG=1033）。"""
    body = ["@echo off", "set VSLANG=1033", f'call "{VCVARS}" >nul', "if errorlevel 1 exit /b 1"] + lines
    body.append("if defined FAILED exit /b 1")
    bat.write_text("\r\n".join(body) + "\r\n", encoding="utf-8")
    result = subprocess.run(["cmd", "/c", str(bat)], capture_output=True, text=True, errors="replace", timeout=timeout)
    text = result.stdout + result.stderr
    log.write_text(text, encoding="utf-8")
    return result.returncode, text


def error_lines(text: str) -> list[str]:
    return [line for line in text.splitlines()
            if " error " in line or "fatal error" in line or "LNK" in line or "Command line error" in line]


def check(step: str, code: int, text: str, log: Path) -> None:
    errors = error_lines(text)
    if code != 0 or errors:
        print(f"[{step}] failed (exit {code}, {len(errors)} errors). log: {log}")
        for line in errors[:40]:
            print("  " + line)
        raise SystemExit(1)
    warnings = sum(1 for line in text.splitlines() if " warning " in line)
    print(f"[{step}] ok (warnings {warnings}, errors 0)")


def fwd(path: Path) -> str:
    """cl の /Fo"dir\\" は末尾の \\" が引用符を潰すので、区切りを / にして渡す。"""
    return str(path).replace("\\", "/")


def write_rsp(path: Path, items: list[Path]) -> None:
    path.write_text("\n".join(f'"{p}"' for p in items) + "\n", encoding="utf-8")


def build_exe(lang: str, source: Path, work: Path) -> Path:
    variant = VARIANTS[lang]
    ja = variant["ja"]
    obj = work / "obj"
    if obj.exists():
        shutil.rmtree(obj)
    for sub in ("nh", "lua", "cpp"):
        (obj / sub).mkdir(parents=True)

    charset = "-DUNICODE -D_UNICODE" if ja else "-D_MBCS"
    cflags = CFLAGS_COMMON.format(charset=charset) + " /std:clatest"
    game = "-DSHIM_GRAPHICS -DNOTTYGRAPHICS" + (" -DNH_LANG_JA" if ja else "")
    inc = f'/I"{source}\\include" /I"{source}\\sys\\windows" /I"{source}\\nhlua\\lua\\src"'

    # 1. 原作の C（と、アダプタの C）
    core = CORE + (CORE_JA if ja else [])
    nh_sources = [source / "src" / f"{n}.c" for n in core]
    nh_sources += [source / "sys" / "windows" / "windsys.c", source / "sys" / "windows" / "win10.c"]
    nh_sources += [ADAPTER / "nh_winport.c"]
    for p in nh_sources:
        if not p.exists():
            raise SystemExit(f"source not found: {p}")
    nh_rsp = work / "nh_sources.rsp"
    write_rsp(nh_rsp, nh_sources)

    lua_sources = [source / "nhlua" / "lua" / "src" / f"{n}.c" for n in LUA]
    lua_rsp = work / "lua_sources.rsp"
    write_rsp(lua_rsp, lua_sources)

    # 2. 正規表現（C++。原作の sys/share/cppregex.cpp）
    cppregex = source / "sys" / "share" / "cppregex.cpp"
    cflags_cpp17 = cflags.replace("/std:clatest", "/std:c++17")

    # 3. アダプタの C++ と共通の部品
    cpp_flags = (f'/nologo /c /std:c++20 /utf-8 /EHsc /O2 /MT /W3 /bigobj /DNDEBUG /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS '
                 + ("/DNH_LANG_JA " if ja else "")
                 + f'/I"{ADAPTER}" /I"{ROOT / "presentation"}" /I"{ROOT / "platform"}" /I"{ROOT / "third_party" / "cpp" / "include"}"')
    cpp_rsp = work / "cpp_sources.rsp"
    write_rsp(cpp_rsp, CPP_ADAPTER + CPP_SHARED)

    exe = ROOT / variant["exe"]
    objs: list[str] = []
    lines = [
        f'cl {cflags} {game} {inc} /MP /Fo"{fwd(obj / "nh")}/" @"{nh_rsp}" || set FAILED=1',
        f'cl {cflags} {inc} /MP /Fo"{fwd(obj / "lua")}/" @"{lua_rsp}" || set FAILED=1',
        f'cl {cflags_cpp17} {game} {inc} /Fo"{obj / "nh" / "cppregex.obj"}" "{cppregex}" || set FAILED=1',
        f'cl {cpp_flags} /MP /Fo"{fwd(obj / "cpp")}/" @"{cpp_rsp}" || set FAILED=1',
    ]
    link_rsp = work / "link.rsp"
    lines.append("if defined FAILED exit /b 1")
    lines.append(f'dir /b /s "{obj}\\*.obj" > "{work / "objlist.txt"}"')
    bat = work / "build_exe.bat"
    log = work / "build_exe.log"
    code, text = run_bat(bat, lines, log, timeout=1500)
    check(f"{lang} compile", code, text, log)

    # link.rsp は objlist.txt（dir /b /s の出力）を引用符つきに直して作る
    for line in (work / "objlist.txt").read_text(encoding="utf-8", errors="replace").splitlines():
        if line.strip():
            objs.append(line.strip())
    link_rsp.write_text("\n".join(f'"{o}"' for o in objs) + "\n", encoding="utf-8")
    code, text = run_bat(work / "link.bat",
                         [f'link /nologo /SUBSYSTEM:CONSOLE /STACK:8388608 /OUT:"{exe}" @"{link_rsp}" {LIBS} || set FAILED=1'],
                         work / "link.log", timeout=600)
    check(f"{lang} link", code, text, work / "link.log")
    print(f"built {exe} ({exe.stat().st_size // 1024} KiB, {len(objs)} objects)")
    return exe


def build_makedefs(lang: str, source: Path, work: Path) -> Path:
    variant = VARIANTS[lang]
    ja = variant["ja"]
    md = work / "md"
    if md.exists():
        shutil.rmtree(md)
    md.mkdir(parents=True)
    charset = "-DUNICODE -D_UNICODE" if ja else "-D_MBCS"
    cflags = CFLAGS_COMMON.format(charset=charset) + " /std:clatest"
    inc = f'/I"{source}\\include" /I"{source}\\sys\\windows" /I"{source}\\nhlua\\lua\\src"'
    files = ["util/makedefs.c", "src/monst.c", "src/objects.c", "src/date.c", "src/alloc.c", "util/panic.c", "src/hacklib.c"]
    objs = []
    lines = []
    for rel in files:
        o = md / ("md_" + Path(rel).stem + ".obj")
        objs.append(o)
        lines.append(f'cl {cflags} -DENUM_PM {inc} -Fo"{o}" "{source / rel}" || set FAILED=1')
    lines.append("if defined FAILED exit /b 1")
    exe = md / "makedefs.exe"
    lines.append(f'link /nologo /OUT:"{exe}" {" ".join(chr(34) + str(o) + chr(34) for o in objs)} || set FAILED=1')
    log = work / "build_makedefs.log"
    code, text = run_bat(work / "build_makedefs.bat", lines, log, timeout=600)
    check(f"{lang} makedefs", code, text, log)
    return exe


def make_data(lang: str, source: Path, work: Path, makedefs: Path) -> Path:
    """dat/ を作業フォルダへ写し、makedefs で作ってから、実行時のデータの置き場へ写す。"""
    dat = work / "dat"
    util = work / "util"
    if dat.exists():
        shutil.rmtree(dat)
    shutil.copytree(source / "dat", dat)
    util.mkdir(parents=True, exist_ok=True)
    for flag in ("-d", "-r", "-h", "-1", "-2", "-3"):
        result = subprocess.run([str(makedefs), flag], cwd=str(util), capture_output=True, text=True,
                                errors="replace", timeout=300)
        if result.returncode != 0:
            print(f"[{lang} makedefs {flag}] failed (exit {result.returncode})")
            print(result.stdout + result.stderr)
            raise SystemExit(1)
    print(f"[{lang} makedefs] ok")

    out = DATA_ROOT / lang
    # 消すのは自分が作る置き場だけ（名前で確かめる）
    if out.exists():
        if out.parent != DATA_ROOT or out.name not in VARIANTS:
            raise SystemExit(f"unexpected data directory: {out}")
        shutil.rmtree(out)
    out.mkdir(parents=True)
    for item in sorted(dat.iterdir()):
        if item.is_file() and item.suffix != ".tmp":
            shutil.copy2(item, out / item.name)
    shutil.copy2(source / "sys" / "windows" / "sysconf.template", out / "sysconf")
    shutil.copy2(source / "sys" / "windows" / "porthelp", out / "porthelp")
    print(f"installed {out} ({len(list(out.iterdir()))} files)")
    return out


def build_assets(tiles_dir: Path) -> None:
    """板（nethack/data/slab）と地形のブロック（assets/voxel/nh_*）を作り、規則・地形番号の表と照合する（build_assets.py）。"""
    result = subprocess.run([sys.executable, str(Path(__file__).resolve().parent / "build_assets.py"), "--tiles", str(tiles_dir)],
                            capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=3600,
                            env=dict(os.environ, PYTHONIOENCODING="utf-8", PYTHONUTF8="1"))
    if result.returncode != 0:
        print(f"[assets] failed (exit {result.returncode})")
        print(result.stdout[-4000:] + result.stderr[-2000:])
        raise SystemExit(1)
    print("[assets] ok (slabs, terrain blocks, rules check)")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--lang", choices=["en", "ja", "both"], default="both")
    parser.add_argument("--source-root", type=Path,
                        default=Path(os.environ.get("HENGBAND_CORE_SOURCE_ROOT", DEFAULT_SOURCES)))
    parser.add_argument("--data-only", action="store_true", help="exe は組まず、データ（makedefs の出力）だけ作り直す")
    parser.add_argument("--skip-assets", action="store_true", help="板（nethack/data/slab）と地形のブロック（assets/voxel/nh_*）を作らない")
    args = parser.parse_args()
    if not Path(VCVARS).exists():
        raise SystemExit(f"vcvars64.bat not found: {VCVARS}")
    langs = ["en", "ja"] if args.lang == "both" else [args.lang]
    for lang in langs:
        variant = VARIANTS[lang]
        source = args.source_root.resolve() / variant["source"]
        if not (source / "src" / "allmain.c").exists():
            raise SystemExit(f"{source} がありません。python tools/core_sources/prepare.py で原作を再構成してください")
        work = WORK_ROOT / lang
        work.mkdir(parents=True, exist_ok=True)
        makedefs = build_makedefs(lang, source, work)
        make_data(lang, source, work, makedefs)
        # 原作のタイルの絵（tiles.png と glyph → 絵の番号の表 tiles.json）。 
        result = subprocess.run([sys.executable, str(Path(__file__).resolve().parent / "build_tiles.py"),
                                 "--lang", lang, "--source-root", str(args.source_root.resolve()),
                                 "--out", str(DATA_ROOT)],
                                capture_output=True, text=True, errors="replace", timeout=600)
        if result.returncode != 0:
            print(f"[{lang} tiles] failed (exit {result.returncode})")
            print(result.stdout + result.stderr)
            raise SystemExit(1)
        print(f"[{lang} tiles] ok")
        if not args.data_only:
            build_exe(lang, source, work)
    if not args.skip_assets:
        # 板とブロックは言語に依らない（絵は英日で同じ）。組んだ最初の言語のタイルの絵から作る。失敗したら止める
        build_assets(DATA_ROOT / langs[0])


if __name__ == "__main__":
    main()
