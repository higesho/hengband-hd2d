// CPU-only tests for hd2d/ui/text_wrap.h (wrap_ui_text の禁則と英語の折り返し)。
// 組み方（どのコンパイラでもよい。ヘッダ 1 本だけを引く）:
//   core-import/toolchain/bin/clang++.exe --target=i686-w64-windows-gnu -static -std=c++20 -I hd2d tools/hd2d_verify/test_text_wrap.cpp -o test_text_wrap.exe
//   test_text_wrap.exe   （失敗が 0 なら終了コード 0）
#include "ui/text_wrap.h"
#include <cstdio>
#include <string>
#include <vector>

namespace {
int checks = 0, failures = 0;
void check(bool ok, const char *what)
{
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

// 1 字＝画面の 1 セル（全角は 2 セル）で数える fit。width はセル数。
size_t cell_fit(std::string_view s, int width)
{
    size_t i = 0;
    int cols = 0;
    while (i < s.size()) {
        size_t len = 1;
        const char32_t cp = hd2d::text_wrap_detail::decode_at(s, i, &len);
        const int w = hd2d::text_wrap_detail::is_wide(cp) ? 2 : 1;
        if (cols + w > width) {
            break;
        }
        cols += w;
        i += len;
    }
    return i;
}

std::string join(const std::vector<std::string> &lines)
{
    std::string out;
    for (const auto &l : lines) {
        out += l;
    }
    return out;
}

bool starts_with_any(const std::string &s, std::initializer_list<const char *> heads)
{
    for (const char *h : heads) {
        if (s.rfind(h, 0) == 0) {
            return true;
        }
    }
    return false;
}

bool ends_with(const std::string &s, const char *tail)
{
    const std::string t(tail);
    return s.size() >= t.size() && s.compare(s.size() - t.size(), t.size(), t) == 0;
}
} // namespace

int main()
{
    using hd2d::wrap_ui_text;
    // 英語は今のまま（語の境で折る・長い 1 語は字で切る・空行を残す）。
    check(wrap_ui_text("resistance to disenchantment", 14, cell_fit) == std::vector<std::string>({ "resistance to", "disenchantment" }), "english words");
    check(wrap_ui_text("one\n\ntwo", 8, cell_fit) == std::vector<std::string>({ "one", "", "two" }), "blank line kept");
    check(wrap_ui_text("abcdefghijklmnopqrstuvwxyz", 8, cell_fit).size() == 4, "long word cut");

    // 食料の説明（実窓で行頭に「、」が来た文）。どの幅でも行頭禁則の字で始まらず、字が失われない。
    const std::string ration = "家族から受け継いだ物。栄養はあるがかなり味気ないこの食べ物は、作り手の湖の人々にクラムと呼ばれ、"
                               "長い旅を考える者なら誰でも知っている。腹持ちはよいが、あごの運動になるほかはひどく退屈だ。";
    for (int width = 6; width <= 80; ++width) {
        const auto lines = wrap_ui_text(ration, width, cell_fit);
        check(join(lines) == ration, "japanese text kept");
        for (size_t i = 1; i < lines.size(); ++i) {
            check(!starts_with_any(lines[i], { "、", "。", "」", "）", "ー", "っ", "ょ" }), "no head-forbidden char");
        }
        for (const auto &l : lines) {
            check(static_cast<int>(cell_fit(l, width)) == static_cast<int>(l.size()), "line fits the width");
        }
    }
    // 目で見た実例: 幅 72 セルで「〜よいが」「、あごの」と割れていた所。
    {
        const auto lines = wrap_ui_text(ration, 72, cell_fit);
        for (const auto &l : lines) {
            check(!starts_with_any(l, { "、" }), "ration width 72");
        }
    }
    // 行末禁則: 「 で行が終わらない。
    const std::string quote = "彼はこう言った「死ね！」そして消えた。";
    for (int width = 4; width <= 40; ++width) {
        const auto lines = wrap_ui_text(quote, width, cell_fit);
        check(join(lines) == quote, "quote kept");
        for (size_t i = 0; i + 1 < lines.size(); ++i) {
            check(!ends_with(lines[i], "「"), "no tail-forbidden char");
        }
    }
    // 全角の境では空白を探さない（「(3000 ターン分」が空白で早めに割れない）が、英数の語は割らない。
    {
        const auto lines = wrap_ui_text("食べると、栄養になる (3000 ターン分、30 パーセント)。", 30, cell_fit);
        for (const auto &l : lines) {
            check(l.find("30\n") == std::string::npos, "digits not split");
        }
        check(join(lines).find("3000") != std::string::npos, "number kept");
    }
    std::printf("text_wrap: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
