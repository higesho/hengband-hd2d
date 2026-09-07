#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""C ソースのツリーを、NDK の clang が **MSVC と同じバイト列を焼く**変換ツリーへ写す。

``src/`` や ``presentation/`` のような内部 UTF-8 の木には使わない。

## 2 つの向き

| ``--from`` | 写し元 | 誰に使うか |
|---|---|---|
| ``cp932`` | CP932 で書かれた木 | ``gensoband/src``（88 ファイルが CP932） |
| ``utf8`` | UTF-8 で書かれた木 | ``gensoband/adapter`` / ``silq/adapter``（BOM 付き UTF-8） |

**どちらの向きでも、出来上がるリテラルのバイト列は CP932 である。** これは
「Windows の実行体と 1 バイトも違わないものを Android でも焼く」という一点のためで、
向きが 2 つあるのは写し元の書き方が違うからにすぎない。

## なぜ要るか

``gensoband/src`` の 88 ファイルは **CP932 で書かれている**。MSVC は既定の実行文字集合が
CP932 なのでそのまま通るが、**NDK の clang は入力を UTF-8 と決め打つ**（``-finput-charset``
は UTF-8 以外を受けず、``-fexec-charset`` も無い。の「EUC 変換ツリー」の裏返し）。

素通しすると 2 つの形で壊れる。どちらも**黙って**壊れるのが厄介である:

1. **文字列の中の 0x5C。** CP932 の 2 バイト目には ``0x5C``（``\\``）が現れる
   （``表``＝``0x95 0x5C``、``ソ``＝``0x83 0x5C`` など）。clang はそこを逃げ札と読み、
   次の 1 バイトを食う。``"表"`` は ``"\\"`` と読まれて**文字列が閉じない**。
2. **``//`` 注釈の行末の 0x5C。** 行継ぎと読まれ、**次の行が注釈に飲まれて消える**
   （``rc.exe`` で 1 度踏んだのと同じ罠。記憶 hengband-rc-cp932-eol-trap）。

``--from utf8`` の側にも別の食い違いがある。**MSVC の実行文字集合はこの機体では CP932**
（vcxproj は ``/utf-8`` を渡していない）なので、BOM 付き UTF-8 のアダプタに書かれた
``"地上"`` は **CP932 のバイト列**として焼かれ、そのまま SJIS のコアへ渡る。clang は
UTF-8 のまま焼くので、素通しすると**アダプタの日本語だけが化ける**。

## どう写すか

バイトのまま SJIS を知っている走査器で読み、置かれた場所で扱いを変える:

| 場所 | 扱い |
|---|---|
| 文字列 ``"..."`` / 文字 ``'...'`` の中 | **8 進の逃げ札**（``\\225`` 等）で CP932 のバイト列を書く |
| 注釈（``//`` と ``/* */``） | UTF-8 へ（``--from utf8`` なら素通し。人が読める。0x5C の罠も消える） |
| それ以外（``#if 0`` の中の地の日本語など） | 同上 |

**16 進（``\\xNN``）ではなく 8 進を使う。** ``\\x`` は続く 16 進数字を際限なく食うので
``"\\x95\\x5ca"`` が壊れる。8 進は**きっかり 3 桁**で閉じるため、後ろに何が来ても安全である。

出力は**丸ごと妥当な UTF-8**になる。これは検算できる不変条件なので、書いた直後に確かめる。

## 使い方

    python tools/transcode_cp932_src.py gensoband/src android/build-src-sjis/gensoband/src

中身が変わっていないファイルは触らない（mtime を保って CMake の作り直しを減らす）。
"""

from __future__ import annotations

import argparse
import pathlib
import sys

#: CP932 の 2 バイト文字の先行バイト（``h-config.h`` の ``iskanji`` と同じ範囲）。
def is_lead(c: int) -> bool:
    return (0x81 <= c <= 0x9F) or (0xE0 <= c <= 0xFC)


#: 同じく後続バイト。**0x5C を含む**のがこの道具の存在理由である。
def is_trail(c: int) -> bool:
    return (0x40 <= c <= 0x7E) or (0x80 <= c <= 0xFC)


def _octal(chunk: bytes) -> bytes:
    """非 ASCII バイト列を 8 進の逃げ札へ。``\\225\\134`` のように 1 バイト 4 文字。"""
    out = bytearray()
    for b in chunk:
        out += b"\\%03o" % b
    return bytes(out)


def _to_utf8(chunk: bytes) -> bytes:
    """CP932 のバイト列を UTF-8 へ。翻せないバイトは下駄（〓）に落とす。

    注釈にしか使わないので、1 バイトのために全体を落とさない方を選ぶ。
    """
    return chunk.decode("cp932", errors="replace").encode("utf-8")


#: 逃げ札を書けない文脈。C++26 の「評価されない文字列」（clang はもうエラーにする）。
_UNEVALUATED = (b"static_assert", b"[[deprecated", b"[[nodiscard")


def _reject_escapes_in_unevaluated(body: bytes) -> None:
    """写した結果に「評価されない文字列 × 8 進逃げ札」が同居していたら落とす。

    完全な構文解析はしない——``static_assert(`` の行から閉じ括弧までを見て、
    そこに ``\\`` 始まりの 3 桁が居たら止めるだけ。取りこぼしはあり得るが、
    **見つけたものは必ず本物**なので、黙って壊れた木を組ませるよりずっとよい。
    """
    lines = body.split(b"\n")
    depth = 0
    watching = False
    for number, line in enumerate(lines, start=1):
        if not watching and any(key in line for key in _UNEVALUATED):
            watching = True
            depth = 0
        if not watching:
            continue
        depth += line.count(b"(") + line.count(b"[") - line.count(b")") - line.count(b"]")
        for pos in range(len(line) - 3):
            if line[pos] == 0x5C and all(0x30 <= b <= 0x37 for b in line[pos + 1 : pos + 4]):
                raise RuntimeError(
                    f"{number} 行目: 評価されない文字列（static_assert 等）に逃げ札が要る字がある。"
                    "説明文は ASCII で書くこと")
        if depth <= 0:
            watching = False


def transcode(src: bytes, source_encoding: str = "cp932") -> tuple[bytes, dict[str, int]]:
    """1 ファイル分を写す。

    @param source_encoding ``"cp932"`` か ``"utf8"``。**出来上がるリテラルはどちらも CP932**。
    @return ``(写した中身, 数えたもの)``
    @exception RuntimeError 走査に取りこぼしがあったとき（下の「検算」）。

    ## 検算——走査した分を積み直して入力と比べる

    この道具の危ないところは変換そのものではなく、**走査器がバイトを取り落とすこと**である。
    落ちたバイトは黙って地の文へ漏れ、それが ``\\`` なら次の行を注釈へ飲み込む。
    実際 1 度踏んだ（``defines.h:8619`` の ``//↑の技能数``。``技``＝``8B 5A`` と
    ``能``＝``94 5C`` の 2 バイト目が ASCII なので、「0x80 以上の連なり」で切ると割れた）。

    そこで、写しを作りながら**読んだ元のバイトも順に積み直す**。最後に入力と突き合わせ、
    1 バイトでも違えば落とす。取りこぼしがあれば必ずここで見つかる。
    """
    from_utf8 = source_encoding == "utf8"
    out = bytearray()
    #: 走査した順に**元のバイト**を積み直したもの。最後に入力と突き合わせる。
    back = bytearray()
    stat = {"literal_bytes": 0, "comment_bytes": 0, "bare_bytes": 0, "not_in_cp932": 0}
    i, n = 0, len(src)
    #: 'N' 地の文 / 'L' 行注釈 / 'B' 塊注釈 / 'S' 文字列 / 'C' 文字
    state = "N"
    pending = bytearray()  # 注釈・地の文の非 ASCII を溜める（まとめて翻すため）

    def emit(shadow: bytes, origin: bytes) -> None:
        """変換ツリーへ ``shadow`` を書き、元の ``origin`` を積み直しへ回す。"""
        out.extend(shadow)
        back.extend(origin)

    def flush(kind: str) -> None:
        """溜めた非 ASCII を吐く。行き先は UTF-8（元から UTF-8 なら素通し）。"""
        nonlocal pending
        if pending:
            stat[kind] += len(pending)
            raw = bytes(pending)
            emit(raw if from_utf8 else _to_utf8(raw), raw)
            pending = bytearray()

    def take_multibyte(pos: int) -> int:
        """注釈・地の文の**多バイト 1 文字**を溜める。@return 進んだバイト数（0 なら ASCII）。

        ここを「0x80 以上の連なり」で切ってはいけない。**CP932 の 2 バイト目は
        ASCII の範囲（0x40〜0x7E）に降りてくる**からである（上の註の ``技`` / ``能``）。
        """
        c = src[pos]
        if c < 0x80:
            return 0
        if (not from_utf8) and is_lead(c) and (pos + 1) < n and is_trail(src[pos + 1]):
            pending.extend(src[pos : pos + 2])
            return 2
        # UTF-8 の継続バイト、半角カナ、対にならなかった孤立バイト。
        pending.append(c)
        return 1

    def as_cp932_literal(chunk: bytes) -> bytes:
        """リテラルの中の非 ASCII を CP932 の 8 進逃げ札へ。

        ``--from utf8`` のときは **CP932 へ翻してから**書く。MSVC が実行文字集合で
        やっていることをそのまま真似る。CP932 に無い字は MSVC と同じく ``?`` に落ち、
        数えて後で報せる（黙って化けるのがいちばん困る）。
        """
        if not from_utf8:
            return _octal(chunk)
        text = chunk.decode("utf-8", errors="replace")
        encoded = text.encode("cp932", errors="replace")
        if b"?" in encoded and "?" not in text:
            stat["not_in_cp932"] += 1
        return _octal(encoded)

    while i < n:
        c = src[i]

        # ---- 注釈と地の文: 多バイトは溜めてまとめて翻す
        if state in ("N", "L", "B"):
            step = take_multibyte(i)
            if step:
                i += step
                continue
            flush("bare_bytes" if state == "N" else "comment_bytes")

        if state == "N":
            if (c == 0x2F) and ((i + 1) < n) and (src[i + 1] == 0x2F):  # //
                emit(b"//", src[i : i + 2])
                state, i = "L", i + 2
                continue
            if (c == 0x2F) and ((i + 1) < n) and (src[i + 1] == 0x2A):  # /*
                emit(b"/*", src[i : i + 2])
                state, i = "B", i + 2
                continue
            if c == 0x22:  # "
                state = "S"
            elif c == 0x27:  # '
                state = "C"
            emit(src[i : i + 1], src[i : i + 1])
            i += 1
            continue

        if state in ("L", "B"):
            if (state == "L") and (c == 0x0A):
                emit(src[i : i + 1], src[i : i + 1])
                state, i = "N", i + 1
                continue
            if (state == "B") and (c == 0x2A) and ((i + 1) < n) and (src[i + 1] == 0x2F):  # */
                emit(b"*/", src[i : i + 2])
                state, i = "N", i + 2
                continue
            emit(src[i : i + 1], src[i : i + 1])
            i += 1
            continue

        # ---- 文字列・文字リテラル。**焼き上がりのバイト列は CP932 で揃える**
        if from_utf8:
            # UTF-8 の多バイトは全部 0x80 以上で、0x5C は現れない。連なりをまとめて翻す。
            if c >= 0x80:
                j = i
                while (j < n) and (src[j] >= 0x80):
                    j += 1
                emit(as_cp932_literal(src[i:j]), src[i:j])
                stat["literal_bytes"] += j - i
                i = j
                continue
        elif is_lead(c) and ((i + 1) < n) and is_trail(src[i + 1]):
            # 先行バイトの判定を逃げ札より先に置くのが肝（0x5C が後続バイトに来るため）。
            emit(as_cp932_literal(src[i : i + 2]), src[i : i + 2])
            stat["literal_bytes"] += 2
            i += 2
            continue
        elif c >= 0x80:  # 孤立した非 ASCII バイト（対にならなかったもの）
            emit(as_cp932_literal(src[i : i + 1]), src[i : i + 1])
            stat["literal_bytes"] += 1
            i += 1
            continue

        if (c == 0x5C) and ((i + 1) < n):  # 元からある逃げ札。2 バイトそのまま
            emit(src[i : i + 2], src[i : i + 2])
            i += 2
            continue
        if ((state == "S") and (c == 0x22)) or ((state == "C") and (c == 0x27)):
            state = "N"
        emit(src[i : i + 1], src[i : i + 1])
        i += 1

    flush("bare_bytes" if state == "N" else "comment_bytes")

    # 「評価されない文字列」に逃げ札は書けない（C++26。clang は既にエラー）。
    # `static_assert` の説明文に日本語があるとここへ落ちる——組んでから気づくと
    # 原因が見えにくいので、写した時点で止める（2026-08-21 に 1 度踏んだ）。
    _reject_escapes_in_unevaluated(bytes(out))

    if bytes(back) != src:
        # 取りこぼしの場所を指して落とす。黙って壊れた木を組ませない。
        pos = next((k for k in range(min(len(back), n)) if back[k] != src[k]), min(len(back), n))
        raise RuntimeError(f"走査の取りこぼし: 位置 {pos} 付近（{len(back)} バイト積み直し / 入力 {n} バイト）")

    return bytes(out), stat


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="CP932 の C ソースを clang が読める変換ツリーへ写す")
    ap.add_argument("src", type=pathlib.Path, help="写し元のディレクトリ")
    ap.add_argument("dst", type=pathlib.Path, help="変換ツリーのディレクトリ")
    ap.add_argument("--ext", default=".c,.h", help="対象の拡張子（既定 .c,.h）")
    ap.add_argument("--from", dest="source_encoding", default="cp932", choices=("cp932", "utf8"),
        help="写し元の文字コード。**行き先のリテラルはどちらでも CP932**")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args(argv)

    exts = {e if e.startswith(".") else "." + e for e in args.ext.split(",")}
    args.dst.mkdir(parents=True, exist_ok=True)

    written = skipped = 0
    total = {"literal_bytes": 0, "comment_bytes": 0, "bare_bytes": 0, "not_in_cp932": 0}
    for src in sorted(args.src.rglob("*")):
        if not src.is_file() or src.suffix not in exts:
            continue
        try:
            body, stat = transcode(src.read_bytes(), args.source_encoding)
        except RuntimeError as err:
            print(f"[transcode] {src}: {err}", file=sys.stderr)
            return 1

        # 不変条件: 写した後は丸ごと妥当な UTF-8 でなければならない。
        # ここが破れるのは走査器の取りこぼしなので、黙って進めてはいけない。
        try:
            body.decode("utf-8")
        except UnicodeDecodeError as err:
            print(f"[transcode] {src}: UTF-8 として読めない写しになった: {err}", file=sys.stderr)
            return 1

        for key in total:
            total[key] += stat[key]

        dst = args.dst / src.relative_to(args.src)
        dst.parent.mkdir(parents=True, exist_ok=True)
        # 中身が同じなら触らない（CMake の作り直しを減らす）。
        if dst.exists() and dst.read_bytes() == body:
            skipped += 1
            continue
        dst.write_bytes(body)  # **バイナリで書く**（テキストだと改行が CRLF に化ける）
        written += 1

    if not args.quiet:
        print(
            f"[transcode] {args.src} -> {args.dst}: 書いた {written} / 据え置き {skipped}"
            f"（リテラル {total['literal_bytes']}B・注釈 {total['comment_bytes']}B・"
            f"地の文 {total['bare_bytes']}B）",
            flush=True,
        )
    # CP932 に無い字は MSVC でも `?` になる（＝Windows 版も同じ見た目）。
    # 「Android だけ化けた」と誤診しないよう、数だけは必ず報せる。
    if total["not_in_cp932"]:
        print(f"[transcode] 註: CP932 に無い字を含むリテラル {total['not_in_cp932']} 件"
              "（`?` に落ちる。Windows 版も同じ）", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
