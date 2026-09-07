/*!
 * @file input_event_adapter.cpp
 * @brief `input_event_adapter.h` の実装。
 *
 * 各展開は「現行 ui の翻訳と 1 バイトも違わない」ことが正。出どころ:
 * - move: 旧 2D UI の矢印・テンキー→'1'..'9'（dy<0 が北）
 * - fkey: 同（31, [C][S][A], 'x', 16 進 2 桁, 13）
 * - set_number: 同（0x05, 0x08×10, 0 詰め数字）
 */
#include "bridge/input_event_adapter.h"

#include <string>

namespace presentation {

namespace {

//! PC/AT スキャンコード（F1..F12）。旧 2D UI の kPcAtF1..F12 と同値。
constexpr unsigned char kPcAtFn[12] = { 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x57, 0x58 };

constexpr char kHexDigits[] = "0123456789ABCDEF";

//! (dx,dy) → テンキー文字。dy<0 が北（画面上）。中央 (0,0) は '5'。
char direction_key(int dx, int dy)
{
    static constexpr char table[3][3] = {
        // dx: -1    0    1
        { '7', '8', '9' }, // dy = -1
        { '4', '5', '6' }, // dy =  0
        { '1', '2', '3' }, // dy =  1
    };
    return table[dy + 1][dx + 1];
}

} // namespace

void append_input_event_keys(const InputEventsMessage &message, std::vector<int> &out)
{
    for (const auto &event : message.events) {
        if (event.e == "move") {
            out.push_back(direction_key(event.dx, event.dy));
        } else if (event.e == "cancel") {
            out.push_back(0x1B);
        } else if (event.e == "confirm") {
            out.push_back(0x0D);
        } else if (event.e == "answer") {
            out.push_back((event.value == "yes") ? 'y' : 'n');
        } else if (event.e == "key") {
            if (event.name == "tab") {
                out.push_back(0x09);
            } else if (event.name == "delete") {
                out.push_back(0x7F);
            } else if (event.name == "backspace") {
                out.push_back(0x08);
            } else {
                const char c = event.chr[0];
                const bool alpha = ((c >= 'A') && (c <= 'Z')) || ((c >= 'a') && (c <= 'z'));
                // KTRL は英字だけ（ui の翻訳も Ctrl+英字のみ。それ以外の ctrl は素通し）。
                out.push_back((event.ctrl && alpha) ? (c & 0x1F) : c);
            }
        } else if (event.e == "text") {
            /*
             * 自由文字入力の文字列（UTF-8）。**ここでは文字コードを直さない。**
             *
             * 直すのは `presentation/term/sdl_null_term.cpp` の
             * `convert_text_input_to_system_encoding`（A-T13）で、キューへ積む直前に
             * 「0x80 以上の連なり」を UTF-8 と見て `utf8_to_sys` に掛けている。
             * **ここでも直すと二重変換になり、名前が `????` になる**
             * （2026-08-13 に実際にそうした。SJIS へ直した 4 バイトを、あちらが
             * さらに UTF-8 として読もうとして落ちる）。変換の家は 1 つである。
             *
             * バイトの並びのまま積めばよい。コアの `askfor` は 1 バイト目が `iskanji`
             * なら `inkey_base` で次の 1 バイトをそのまま取るので、続けて積んであれば
             * 2 バイト文字として組み上がる（`src/core/asking-player.cpp`）。
             */
            for (const char byte : event.text) {
                out.push_back(static_cast<unsigned char>(byte));
            }
        } else if (event.e == "fkey") {
            out.push_back(31);
            if (event.ctrl) {
                out.push_back('C');
            }
            if (event.shift) {
                out.push_back('S');
            }
            if (event.alt) {
                out.push_back('A');
            }
            const unsigned char code = kPcAtFn[event.n - 1];
            out.push_back('x');
            out.push_back(kHexDigits[code / 16]);
            out.push_back(kHexDigits[code % 16]);
            out.push_back(13);
        } else if (event.e == "keyseq") {
            /*
             * 生のキー列。**翻訳しない**（`protocol_messages.h` の注）。
             * マクロのトリガーがこれで来る。トリガーはキー配列の変換より前、
             * `inkey_aux`（`src/io/input-key-acceptor.cpp:98`）が生のキーで突き合わせるので、
             * ここで意味へ直すとかえって当たらなくなる。値域は decode 側で検めてある。
             */
            for (const int byte : event.bytes) {
                out.push_back(byte);
            }
        } else if (event.e == "set_number") {
            std::string text = std::to_string(event.number);
            while (static_cast<int>(text.size()) < event.digits) {
                text.insert(text.begin(), '0');
            }
            /*
             * **鳴らないキーだけで消す**（FH-15 の続き。実機で見つけたこと
             * 2026-08-24「数値はカーソルで変更できず。カーソル入力すると空白に」）。
             *
             * 以前は `KTRL('E')`（行末へ）＋退格 10 で消していたが、**この綴りを
             * 受けるのは変愚だけ**である。Frox の `_askfor_aux`（`util.c:2974`）に
             * `KTRL('e')` は無く、既定の枝へ落ちて `bell()` を鳴らす——そして
             * **`bell()` は `flush()` を呼んで、後に積んである退格も数字も丸ごと
             * 捨てる**（`util.c` の `bell()`）。結果、既定値を消したところで列が
             * 途切れ、**数字の欄が空のまま**になっていた。
             *
             * `KTRL('b')`（左）と `KTRL('d')`（前方削除）は**変愚にも Frox にも在り**、
             * どちらも端では黙って何もしない（`asking-player.cpp:81`/`:129`、
             * `util.c` の同名の枝）。だから頭まで戻して端から消す形にする。
             * 10 は入力欄の実長（`askfor` の `len`）に対する余裕。
             */
            for (int i = 0; i < 10; ++i) {
                out.push_back(0x02); // KTRL('B') = 1 文字左へ（頭で止まる）
            }
            for (int i = 0; i < 10; ++i) {
                out.push_back(0x04); // KTRL('D') = カーソル位置を消す（末尾で止まる）
            }
            for (const char c : text) {
                out.push_back(static_cast<unsigned char>(c));
            }
        }
        // 未知の e は decode が落としているのでここには来ない。
    }
}

} // namespace presentation
