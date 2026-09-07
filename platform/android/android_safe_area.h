/*!
 * @file android_safe_area.h
 * @brief 画面の「安全領域」（ノッチ・パンチ穴を避けた範囲）を Android から採る。
 *
 * ## なぜ自前で採るのか
 * 素直な道は**テーマの `windowLayoutInDisplayCutoutMode`** で、実際 `never` を指定してある。
 * ところが **Android 15（API 35）を target にすると、システムがそれを `always` へ
 * 書き換える**（端から端まで描く方針の強制。`adb shell dumpsys window windows` の
 * `layoutInDisplayCutoutMode=always` で実測）。つまり窓は必ず切り欠きの下まで広がるので、
 * **避けるのはアプリの仕事**になった。
 *
 * ここは切り欠きの寸法（画素）を返すだけ。避け方（配置をどう詰めるか）は呼ぶ側が決める。
 *
 * @note 値は**窓の向きに追随する**（横持ちなら左右のどちらかに寄る）。回転で変わるので
 * 1 度読んで覚えっぱなしにしない。
 */
#pragma once

namespace platform_android {

/*!
 * @brief 切り欠きを避けるための余白（画素）を採る。
 * @param[out] left,top,right,bottom 各辺の余白。切り欠きが無ければすべて 0。
 * @return 採れたら true。**採れないことは珍しくない**（窓がまだ画面に付いていない起動直後・
 *   古い端末）。false のときは出力を触らないので、呼ぶ側は前の値を使い続けてよい。
 *
 * @details 実体は JNI で
 * `Activity#getWindow().getDecorView().getRootWindowInsets().getDisplayCutout()` を辿り、
 * `getSafeInsetLeft()` 等を読む。**SDL のスレッドから呼んでよい**（読むだけ）。
 */
bool get_safe_insets(int &left, int &top, int &right, int &bottom);

/*!
 * @brief ソフトキーボード（IME）が下から覆っている高さ（画素）を採る。
 * @param[out] bottom 覆われている画素。**出ていなければ 0**。
 * @return 採れたら true。窓がまだ画面に付いていない起動直後は false（出力を触らない）。
 *
 * @details 実体は `getRootWindowInsets().getInsets(WindowInsets.Type.ime()).bottom`。
 * どちらも **API 30** で入ったもので、この版の `minSdk` は 33 なので全端末に在る。
 *
 * **切り欠きと同じ「余白」として扱えることが要点**である。呼ぶ側はこれを下の余白へ
 * 足すだけでよく、そうすると割り付け（プロンプト行・バーチャルパッド）が丸ごと
 * キーボードの上へ退く。IME 専用の場所取りをどこかに作る必要はない。
 *
 * @note 値は**アニメーションの途中でも変わる**（キーボードはせり上がってくる）。
 * 1 度読んで覚えっぱなしにせず、文字入力の間は短い間隔で読み直すこと。
 */
bool get_ime_inset(int &bottom);

} // namespace platform_android
