/*!
 * @file fc_compat.c
 * @brief MSVC に無い POSIX 関数の穴埋め。**先方のソースには触らない**。
 *
 * 基準は （表の 2 番）。
 *
 * `frox/src/autopick.c:3781` と `:3814` が `strncasecmp()` を呼ぶ。これは POSIX の
 * 関数で MSVC の CRT には無く、リンクで未解決になる。
 * **`frox/src` を 1 行直せば済むように見えるが、それをすると「写しと上流が 1 バイトも
 * 違わない」が崩れる**（設計 §1 制約 1）。上流は現役なので、次の取り込み直しで
 * どれが当方の手当てでどれが上流の変更か読めなくなる。だからこちら側で埋める。
 *
 * 宣言は `frox/src/h-system.h` 系には無い。呼び出し側は暗黙宣言（C4013）で通る。
 * 警告は vcxproj で 4013 を切ってある。
 */

#include <string.h>

int strncasecmp(const char *a, const char *b, size_t n)
{
    return _strnicmp(a, b, n);
}
