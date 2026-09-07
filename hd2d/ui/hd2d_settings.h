/*!
 * @file hd2d_settings.h
 * @brief 実行中に変えられる設定と、その保存（P8）。
 *
 * ## なぜ要るか
 * 機能メニューで変えたものが**次の起動で消える**と、絵を詰める作業が成立しない
 * （毎回 `--camera=` を打ち直すことになる）。P0〜P7 は起動引数だけで済ませていたが、
 * 人が触る手段ができた以上、覚える場所が要る。
 *
 * ## 置き場所
 * リポジトリ直下の `hd2d.cfg`（`HengbandHd2d.exe` と同じ所）。**`lib/` には置かない**
 * （あちらはコアのもので、セーブと同じ扱いになると上流取り込みの衝突源になる）。
 *
 * ## 優先順位
 * **起動引数 > cfg > 既定値。**引数で指定したものは cfg より強い
 * （「今回だけこの値で見たい」ができないと比べられない）。引数で指定しなかったものだけ
 * cfg から埋める。書き戻すのは**機能メニューで触ったときだけ**で、
 * 引数で渡した値を勝手に覚えたりはしない。
 */
#pragma once

#include "render/post_process.h"
#include "render/scene_look.h" //!< 画調（軸 E）。**enum は描き手の側が正**——写すと片方だけ動く
#include "render/surface_wear.h" //!< 材質ごとの汚しの入切（`kWearAllMaterials`）
#include "ui/fps_mode.h" //!< 一人称の画角の範囲。**数を写さない**（写すと片方だけ動く）
#include "ui/game_pad.h"
#include "ui/key_binds.h"
#include "ui/ui_layout.h"
#include "ui/virtual_pad.h"
#include "xr/xr_math.h" //!< VR の板の置き場所（`PanelSlot` / `PanelPose`）。純関数の木

#include <string>
#include <vector>

namespace hd2d {

/*!
 * @brief 起動できるコア 1 件。
 *
 * @details **画面側が持ってよいコアの知識はこれだけ**である（同 §1 制約 1:
 * 「UI が持ってよいのは『起動できるコアの表示名と exe の場所』だけ」）。
 * タイル目録も pad 表も板の置き場も、握手のあとコアが申告する。
 */
struct CoreEntry {
    //! 一覧に出す名。cfg にそのまま入る（cfg は UTF-8。`/execution-charset:utf-8`）。
    std::string name;
    //! exe へのパス。**`HengbandHd2d.exe` の在るディレクトリからの相対**。
    std::string path;
};

/*!
 * @brief 移動のなめらかさ（P10 レビュー 5。2026-08-09 に決めた）。
 * @details コアはマス単位でしか位置を持たないので、**どこをなめらかにするか**は
 * 見せ方の選択になる。両方を残して選べるようにする（どちらが好みかは人による）。
 */
enum class MoveSmoothing : int {
    /*!
     * @brief 地形はマスへ吸着し、実体だけなめらか（**従来の見え**）。
     * @details 地図が 1 マスずつ飛び、キャラがそれを追いかけるので、
     * 症状としては「キャラやモンスターがマップの後追いでひょこひょこ移動」。
     */
    Entities = 0,
    //! **全部なめらか。**カメラがプレイヤのなめらかな位置を追うので、地形も一緒に流れる。
    All = 1,
};

/*!
 * @brief `vr_render_scale` の既定。**どの機でも 1.0**。
 *
 * @details 一度は Quest だけ 0.75 にした（実機 1 巡目の所見 4「辺境で重い」）が、
 * **取り消した**——2026-08-17 に決めた「解像度を落とすのは VR の魅力を削ぐので
 * 避けたい」。重さは塗る量の構造（空の無駄塗り。§15.5）から先に削る。
 *
 * 落とす口そのものは**残してある**（cfg の `vr_render_scale`・機能メニュー ＞ VR ＞
 * 「描画解像度」）。重い機での最後の手段としては正しい口なので、**既定で掛けない**
 * というだけである。
 *
 * **数を 2 か所に書かないための定数である。**初期化（下の `vr_render_scale`）と
 * 差分判定（`differs_from` は既定構築との比較なので自動で追随する）が
 * 同じ値を見ていないと、「メニューで戻したのに cfg に残る」になる。
 */
inline constexpr float kVrRenderScaleDefault = 1.f;

struct Hd2dSettings {
    /*!
     * @brief 画面の言語（`i18n/lang.h`。BCP 47 風のコード）。
     * @details 空なら「まだ選んでいない」で、`assets/lang/` に在るものから
     * 既定（日本語）を採る。**コアの言語ではない**——コアは今のところ組んだ
     * ときの言語で固定で、切り替えは画面側の文言にだけ効く。
     */
    std::string lang;
    /*!
     * @brief 起動できるコアの一覧（設計 §6.1）。cfg の `cores=名|パス,名|パス`。
     * @details 空なら**初回**とみなし、`HengbandHd2d.exe` と同じ場所を見て
     * **在るものだけ**自動登録する（`hd2d_app.cpp` の `discover_cores`）。
     * 1 件しか無ければ選択画面は**出さない**——既存利用者の起動を変えないため。
     */
    std::vector<CoreEntry> cores;
    /*!
     * @brief 前回選んだコアのパス（cfg の `last_core`）。初期カーソルの位置になる。
     * @details 一覧に無いパスが入っていたら黙って無視する（cfg を手で直したときの保険）。
     */
    std::string last_core;
    /*!
     * @name カメラ。**実物を見て決めた値**（2026-08-11）
     * @details `app/hd2d_app.h` の `AppOptions` と**同じ数**にしてあること
     * （引数を打たないときはこちらが使われる）。片方だけ直すと
     * 「`--camera=` を付けたときと付けないときで絵が違う」になる。
     * @{
     */
    float camera_pitch_deg{ 29.f };
    float camera_fov_deg{ 70.f };
    float camera_cell_px{ 65.f };
    /*! @} */
    /*!
     * @brief **横持ちの**画面の作り。既定は画面分割（2026-08-08 に決めた「規定は HengbandUI」）。
     * @details 縦横で別に持つ（同 2026-08-12「縦と横が切り替わる際には自動でそれぞれに
     * 設定された画面構成に切り替わるように」）。**どちらが効いているかは画面の形が決める**
     * ので、読むときは必ず `layout_for()` を通すこと——`layout` を直に読むと、
     * 縦持ちのときだけ「メニューで選んだ物と出ている物が違う」になる。
     */
    LayoutMode layout{ LayoutMode::Split };
    /*!
     * @brief **縦持ちの**画面の作り。既定は `Tall`（2026-08-12 に決めた
     * 「Android 縦持ちはこれをデフォルトに」）。
     */
    LayoutMode layout_portrait{ LayoutMode::Tall };
    //! いま向いている側の作り。`portrait` は画面の形から出す（`h > w`）。
    LayoutMode layout_for(bool portrait) const { return portrait ? this->layout_portrait : this->layout; }
    //! 同じものを書き換える口（メニューと F9 が使う）。
    LayoutMode &layout_ref(bool portrait) { return portrait ? this->layout_portrait : this->layout; }
    bool subs_open{ false };
    /*!
     * @brief サブパネルの割り付け（枚数と仕切り。`ui/ui_layout.h` の `SubSplit`）。
     * @details 既定は**下段 3・右 2 ＝ 従来の絵**。境界を掴んで動かした結果も
     * 機能メニューで選んだ枚数もここへ残る（cfg のキーは `sub_split` と `sub_bottom_w20` /
     * `sub_right_h20`）。
     */
    SubSplit sub_split{};
    /*!
     * @brief 各サブパネルに映すコアのサブウインドウ（`SubWindowRedrawingFlag` の番号）。
     * @details **-1 は「UI 既定」**（この exe が自分で作る簡単な中身）。
     * ここに書いてあるのは**変愚蛮怒系（変愚・短愚・幻想）の既定**で、
     * 左＝メッセージ / 中央＝モンスターの思い出 / 右＝装備・持ち物、
     * 右上＝プレイヤーのステータス（2026-08-08 に決めた）。
     *
     * **番号の意味はコアごとに違う。**Sil-Q の `PW_*` は変愚の
     * `SubWindowRedrawingFlag` と並びが揃っていない（Sil-Q の 7 はメッセージ、変愚の 7 は
     * 周辺の光景）。だから既定は 1 つでは足りず、`default_sub_panel_kinds()` が
     * **コア名（`hello_ack.core_name`）で選ぶ**。cfg がまだ無いときだけ通る道である。
     * @note **添字は場所で固定**（0..3 が下段の左から・4..6 が右列の上から。`kSubRightSlot`）。
     * 下段を 3→4 枚にしても右列の割り当てがずれないのはこのため。枚数を減らして
     * 出なくなった枠の値も**消さずに残す**——戻したときに前の中身が返ってくる。
     * @note 番号は `src/system/redrawing-flags-updater.h` の `SubWindowRedrawingFlag`。
     * **コアのヘッダは引かない**（必守制約 4）ので、値だけをここに写している。
     */
    int sub_panel_kind[kUiSubPanels]{ 6, 8, 4, 1, 3, 0, 0 };
    //! 窓（真）か全画面（偽）か。Alt+Enter と機能メニューの両方から動く。
    bool windowed{ true };
    PostFlags post{};
    float cutaway_radius{ 260.f };
    /*!
     * @brief 被写界深度の強さ（P10 レビュー 4）。0 = 掛けない 〜 2.4 = 最大。
     * @details 2026-08-09 に決めた「DOF 強度は調整できるように。最大は今よりもっと
     * 強く利くように」。1.0 を超えると**ぼかしを 2 段掛ける**ので、従来（0.80 相当）より
     * はっきりぼける。機能メニューから回す。
     */
    float dof_strength{ 0.80f };
    /*!
     * @name 画面全体の色処理（2026-08-23 に決めた）
     *
     * @details 中身は `render/post_process.h`。**どれも切れば従来の絵**である。
     *
     * | | 既定 | 幅 |
     * |---|---|---|
     * | `vignette_strength` | 0.28（従来の値） | 0 〜 0.9 |
     * | `sepia` | 0（掛けない） | 0 〜 1 |
     * | `hdr` | 0（掛けない） | 0 〜 1.5 |
     * | `exposure` | 1.25（従来のトーンマップの係数） | 0.5 〜 2.5 |
     *
     * @note `hdr` は**表示機への HDR 出力ではない**。写真の HDR 合成の見え
     * （影が開き・白飛びが戻り・色が締まる）である。
     * @{
     */
    float vignette_strength{ 0.28f };
    float sepia{ 0.f };
    float hdr{ 0.f };
    float exposure{ 1.25f };
    /*! @} */
    /*!
     * @brief **空中に浮かぶ埃**の量（0 = 出さない・1 = 目一杯。2026-08-23 に決めた）。
     * @details 中身は `render/dust_motes.h`。粒の数と明るさの両方に掛かる。
     * 色は環境光を掛けるので、夜は青く・松明の間では暖かく見える。
     */
    float dust{ 0.35f };
    /*!
     * @brief **空間の明るさ**（環境光の倍率。0.5 = 暗い 〜 3.0 = 明るい）。
     *
     * @details 2026-08-15 に決めた「ダンジョンでの明るさは、ダンジョンの空間自体の
     * 明るさを調整できるようにしよう。**ボクセル板の描画は弄らない**」。
     *
     * 実体をボクセルの板にしたとき、絵が今までより暗くなった——タイルの絵には
     * **既に陰影が焼き込まれている**のに、板はボクセルと同じ光の式を通るので、
     * その上に場面の光が乗るためである。ビルボードは専用のシェーダで光を弱めていたが、
     * **その例外を板に持ち込まない**と決めた。代わりに空間そのものを明るくする。
     *
     * 掛かるのは `SceneLighting::ambient_scale`（環境光）だけで、方向光・点光源・影には
     * 触らない。**壁も床も実体も同じだけ明るくなる**ので、絵の釣り合いが崩れない。
     */
    float dungeon_light{ 1.f };
    /*!
     * @brief 一人称のとき、実体の板を**キャラへ向けて回すやり方**。
     * @details 2026-08-15 に決めた「FPS モードの際は板はキャラに向けて
     * 回転させる必要がある。回転は 360 度フリー回転と 8 方向への吸着を選択式に」。
     * **見下ろしの本線には掛からない**（同日「見下ろし表示は南側固定で OK」）。
     *
     * **既定は 8 方向吸着**（2026-08-15 に決めた。実機で両方を見たうえでの決定）。
     * 自由回転は旧ビルボードに一番近い見え方だが、選ばれたのは吸着のほうである。
     */
    enum class SlabTurn : int {
        Free = 0, //!< 360 度自由。常に正対する（旧ビルボードに一番近い）
        Snap8 = 1, //!< 45 度刻みへ吸着。**向きが 8 通りしか無いので絵が落ち着く**
    };
    SlabTurn fps_slab_turn{ SlabTurn::Snap8 };
    /*!
     * @name VR の盤
     * @details **どれも仮の値である。**見え方は実機で基準を決める
     * （設計書 §0「見え方の数値はすべて仮」）。cfg に置いてあるのは、実機で
     * 撮り比べるために**再ビルド無しで動かせる**必要があるため。
     * @{
     */
    /*!
     * @brief 1 マスを何メートルで置くか。**卓は固定・縮尺は設定**（2026-08-14 に決めた）。
     * @details 既定 0.05（1 マス 5cm）は「**ブロックをもっと拡大したい**」（同日の指示）に
     * 合わせた値で、畳 1 畳の天板に 36×18 マスが載る。**卓からはみ出したぶんは切る**
     * （§21。天板が地図の枠になる）ので、上げるほど「近くを大きく」見ることになる。
     * 機能メニュー ＞ VR ＞ ジオラマの拡大率で 5mm 刻みに回せる。
     */
    float vr_tile_m{ 0.05f };
    //! 再中心のとき、頭から**卓の中心**まで何メートル前に置くか。
    float vr_table_forward_m{ 0.70f };
    /*!
     * @brief 天板の高さ（メートル）。**0 なら頭の高さから導く**（2026-08-14 に決めた）。
     * @details 導き方は `頭の高さ − 0.52m`（`xr::place_room`）。座って被れば約 0.70m、
     * 立てば約 1.15m。0 以外を書くとその値で固定される（撮り比べる手段）。
     * **下は床（0.05m）まで下げられる**（2026-08-14 に決めた「視点高さは下限を
     * 超えて下にしていい」）——盤を低く置いて見下ろす形にできる。
     *
     * **既定は 0.60（固定）**へ変えた（2026-08-16 に決めた。実機 1 巡目。
     *）。自動（0）は座り方で 0.70〜1.15m と揺れ、
     * 被り直すたびに盤の高さが変わる。0 を書けば今までどおり自動に戻せる。
     */
    float vr_table_height_m{ 0.60f };
    /*!
     * @name 空中の板（§20・§22）
     * @details 板は**画面をそのまま写した 1 枚**で、向いている方角の頭上に浮かぶ。
     * @{
     */
    /*!
     * @brief 1 桁（8 画素）の見込み角（度）。**読み味も板の大きさもこれ 1 つで決まる。**
     * @details 板の幅は「桁数 × これ」なので、**上げると読みやすくなるが視界に入らなくなる**。
     * 窓 1920 幅（240 桁）での勘定:
     *
     * | 値 | 板の見込み角 | Quest 2 の 1 桁 | 具合 |
     * | --- | --- | --- | --- |
     * | **0.22（既定）** | **53°×30°** | **4.5 画素** | 見上げずに全体が入る |
     * | 0.25 | 60°×34° | 5.2 画素 | 全部見えるが字が潰れる |
     * | 0.32 | 77°×43° | 6.6 画素 | 見上げれば全体が入る |
     * | 0.40 | 96°×54° | 8.2 画素 | 首を振らないと端が読めない |
     *
     * **字を大きくしたいなら桁を減らすのが筋である**（§16）。`--windowed=1280x720` で
     * 起動すれば 160 桁になり、0.48° でも 77° に収まる。
     *
     * @note 既定を 0.32 から **0.22** へ落としたのは実機 1 巡目の所見
     * 読み味の基準は実機であって、この表の勘定ではない。
     */
    float vr_panel_deg_per_cell{ 0.22f };
    //! 頭から板までの距離。
    float vr_panel_dist_m{ 1.20f };
    /*!
     * @brief 板を目線より**上へ**何度置くか（2026-08-14 に決めた
     * 「HMD の少し上、見上げる位置に固定」）。
     * @details 板の下端は「この角 − 画面の半分ぶんの角」に来る。
     *
     * **既定は 12°**（2026-08-16 に決めた。実機 1 巡目。
     *）。字の大きさを 0.22°/桁 へ落として板が
     * 小さくなったぶん、置き場所も下げないと目線から離れすぎる。
     * 22°（前の既定）と 0.32°/桁 の組では下端が −0.5°（ほぼ目線）だった。
     */
    float vr_panel_lift_deg{ 12.f };
    /*!
     * @brief 板が付いてくるまでの**遊びの角**（度）。
     * @details 2026-08-14 に決めたは 2 つあって、両方を満たす必要がある——
     * 「北固定だと旋回すると見えなくなる」「頭に固定だと振り向きについてきて疲れる」。
     * この角の中で首を振っても板は動かず、外へ出たぶんだけゆっくり付いてくる。
     * **0 で頭に貼り付き、180 で空間に固定される。**
     *
     * 既定は 180（**完全に空間固定**）——2026-08-14 に決めた「パネルは空間固定で
     * いい。かつ HMD に正対させなくていい」。付いてきてほしければ機能メニューで下げる。
     */
    float vr_panel_follow_deg{ 180.f };
    /*!
     * @brief 一人称のとき、板に**何を描くか**（2026-08-14 に決めた
     * 「FPS モードの際の各パネルの ON/OFF」）。
     * @details 添字は `xr::PanelSlot`。板は 1 枚なので、これは「そこへ描くかどうか」。
     * 切ったぶんは画面から消えるので、そのぶん世界が見える。**既定は全部入。**
     */
    bool vr_fps_show[static_cast<int>(xr::PanelSlot::Count)]{ true, true, true, true, true, true, true, true,
        true, true };
    /*! @} */
    /*!
     * @brief 一人称 VR の 1 マスのメートル数。**0 = 頭の高さから決める**（既定）。
     * @details。自動のときは
     * 「平らな画面の目の高さ（0.55 マス）＝ 実際の身長」になる縮尺を採る
     * ——身長 1.65m なら 1 マス 3m。壁の上端が見えるか見えないかが平らな画面と一致する。
     * 座って被る（頭の高さが床から 0.8m 未満）ときは 3m へ落とす。
     * ここに 0 以外を書くとその値で固定される（実機で撮り比べる手段）。
     */
    float vr_fps_cell_m{ 0.f };
    /*!
     * @brief 目の描き先を、ランタイムの推奨値の何倍で作るか。
     * @details **既定の 1.0 は「推奨値そのまま」で、VR の絵は 1 画素も動かない。**
     * 落とす口を最初から持っておくのは Quest のためである——推奨値（片目 1440×1584 級）で
     * 影・ブルーム込みの両眼 72Hz が出るかは実測でしか分からず、重かったときに
     * 再ビルドせず下げられなければ実機の検分が止まる。**掛かるのはセッションを立てる
     * 瞬間だけ**（swapchain は作り直す手段を持たない）なので、変えたら立て直すこと。
     * 機能メニュー ＞ VR ＞「描画解像度」で 0.05 刻みに回せる（効くのは次回起動から）。
     *
     * @note 既定は `kVrRenderScaleDefault`（**どの機でも 1.0**。§15.5）。
     * **ここに数を直接書かないこと**——書くと初期化と差分判定が別の値を見る。
     */
    float vr_render_scale{ kVrRenderScaleDefault };
    /*! @} */
    /*! @} */
    /*!
     * @brief 移動のなめらかさ。**既定は「全部」**（2026-08-11 に決めた）。
     * @details 足したときの既定は「実体だけ」（従来の見え）だったが、実物を見て
     * 全部なめらかへ変わった。地形もカメラも一緒に流れる。
     */
    MoveSmoothing move_smoothing{ MoveSmoothing::All };
    /*!
     * @brief 一人称の水平画角（度）。ホイールで `kFpsFovMinDeg`〜`kFpsFovMaxDeg` を回る。
     * @details 見下ろしの `camera_fov_deg`（望遠寄りの 40°）とは**別の値**。
     * 1 つの数で兼ねると、一人称へ入るたびに見下ろしの絵が広角へ化ける。
     */
    float fps_fov_deg{ kFpsFovDefaultDeg };
    /*!
     * @brief 一人称で上下（仰角・俯角）も見るか。
     * @details 2026-08-11 に決めた「FPS モードで視界は仰角、下角も見れるように。
     * これはオプションで上下角の視野移動を切り替えられるように」。**既定は入**
     * （足したばかりの機能なので、まず効いている状態を見せる）。
     */
    bool fps_vertical_look{ true };
    /*!
     * @name 一人称のダンジョンの 2 段目と天井（2026-08-11 に決めた）
     * @details 「FPS モードの際に、ダンジョンの壁の高さを 2 ブロックにして、天井も作って
     * みよう」「オプションで天井と 2 ブロック目のオンオフをつけて」。**既定はどちらも入**
     * （上と同じ理由——まず効いている状態を見せる）。効くのは**一人称かつ地下**だけで、
     * 見下ろしは従来どおり 1 段・天井なし（同日の指示）。実装は `world/terrain_view.h`。
     * @{
     */
    bool fps_wall_upper{ true };
    bool fps_ceiling{ true };
    /*! @} */
    /*!
     * @brief 一人称で歩き続けるときの 1 歩の間隔（ミリ秒）。
     * @details 2026-08-14 に決めた「押しっぱなしの場合はターン毎に動くのではなく、
     * ヌルヌルと継続して移動してほしい。1 マス事だと酔う。移動が止まる際にマスに
     * 吸着する感じで」。**1 歩目のあとの待ち（260ms）を置かず、この間隔で等間隔に出す。**
     * 絵の追随にも同じ値から出した最低速度（1 マス ÷ この間隔）を渡すので、
     * 押している間はなめらかに流れ、離せばマスへ収まる。
     *
     * 短くすると速く歩く。VR では 1 マスが 3m もあるので、**速すぎると酔う**
     * （既定 160ms ＝ 毎秒 6 マス）。実機で詰めること。
     */
    int fps_step_ms{ 160 };
    /*!
     * @brief 操作 → キーボードのキー（`ui/key_binds.h`）。
     * @details **入っているのは利用者が変えたぶんだけ**。コアのコマンドは既定では
     * コア自身のキー（`拾う` なら `g`）で出るので、ここに無い＝既定のまま、である。
     */
    KeyBinds key_binds{ default_key_binds() };
    /*!
     * @brief パッドのボタンに割り当てた操作（0 = 割り当て無し）。
     * @details A（決定）・B（取消）・Back（機能メニュー）は**固定**なので入らない。
     */
    PadBinds pad{};
    /*!
     * @brief 既定のパッド割り当て（`apply_default_pad_binds()`）を一度でも入れたか。
     *
     * @details **コアのコマンド表が届かないと既定を書けない**（`id` は実行時にしか
     * 分からない）ので、cfg を読んだ時点では入れられず、握手のあとで入れることになる。
     * その「入れたかどうか」を覚える場所がここ。
     *
     * これが無いと、**利用者が外した割り当てが毎回復活する**。
     * 逆に持っていれば、既定は初回の 1 度だけ入って、あとは触られたとおりに残る。
     */
    bool pad_defaults_applied{ false };
    //! 背面の 1 枚絵（タイトル画・パネル）を出すか。
    /*!
     * @brief **実体の表現**。板か、アスキーの文字か。
     *
     * @details こう決めた——「アスキーモードの HD2D（地面、地形は HD2D のボクセル。
     * きゃら、モンスター、アイテムはアスキーを板にしたもの）」。
     * **地形は触らない**——変わるのは実体（キャラ・モンスター・アイテム）だけである。
     *
     * 見下ろしでも一人称でも VR でも同じ値が効く（軸 C は軸 B・軸 D と直交する。同 §2-1）。
     * @note 値は cfg に書くので**増やすときは末尾へ**。
     */
    enum class EntityStyle : int {
        Slab = 0, //!< タイルの板（従来。既定）
        Ascii, //!< コアの `ascii_fallback` を文字の板で
    };
    /*!
     * @brief **メインパネルの中身**。3D か、2D のアスキー地図か。
     *
     * @details 「オリジナルモード（オリジナルのコアと同じアスキー表示）」と決めた
     * ——ただし**中身は改訂されている**（2026-08-19）。初版の設計は Term 80×24 を丸写しして
     * 画面全部を置き換える案だったが、決めたことで取り下げになった:
     *
     * > Term 丸写しはコントローラー操作などの UI 側の追加要素が動かなさそうなので、無しだね
     * > あくまでもメインパネルに 2D でアスキー表示するのみで
     *
     * したがって**変わるのはメインパネル（`UiLayout::scene`）の中身だけ**である。
     * 状態列・サブパネル・最下段・ミニマップ・カーソル層・バーチャルパッド・
     * コントローラーの割り当ては**全部そのまま効く**。プロトコルの変更も要らない
     * （マスごとの `ascii_fallback` と色は既に毎フレーム届いている）。
     * @note 値は cfg に書くので**増やすときは末尾へ**。
     */
    enum class MainPanel : int {
        Hd2d = 0, //!< 3D のボクセル（従来。既定）
        Ascii, //!< 2D のアスキー地図（オリジナル表示）
    };
    MainPanel main_panel{ MainPanel::Hd2d };
    /*!
     * @brief **状態列の位置**。
     *
     * @details `自動` はコアの申告（`GameFrame::status_col_side`。0 = 左・1 = 右）に
     * 従う——変愚・短愚・幻想・Sil-Q は左、FroxComposband は右
     * （あちらの `ui_char_info_rect()` が右端 12 桁）。`左 / 右` を選んだら
     * **コアの申告より人の選択が勝つ**。**5 本すべてのコアで効く**——状態列は
     * `frame.status_col_lines` のテキストを画面が自分で並べているので、
     * 置く側は画面の都合だけで決められる。
     * @note 値は cfg に書くので**増やすときは末尾へ**。
     */
    enum class StatusColSide : int {
        Auto = 0, //!< コアの申告に従う（既定）
        Left, //!< 常に左
        Right, //!< 常に右
    };
    StatusColSide status_col_side{ StatusColSide::Auto };
    /*!
     * @brief **タイルが無い実体を字の板で描く**（同 §6.2。決めたこと F5）。
     *
     * @details 板の道（`EntityStyle::Slab`）で目録に無い実体は、いままで
     * **何も描かれなかった**（`missing_tiles` に数えるだけ）。真なら
     * アスキー実体と同じ字の板へ落とす——出す字はコアが送ってきた
     * `ascii_fallback` そのもので、捏造ではない。**「何も描かない」ほうが
     * 情報を落としている。** 既定は入。切る手段を残すのは、既存 4 本の絵を
     * 変えうる改修だからである。
     */
    bool entity_glyph_fallback{ true };
    /*!
     * @brief 2D アスキー地図の**字の高さ**（画素）。**0 = UI の文字と同じマス**。
     *
     * @details 2026-08-19 に決めた「設定で回せるように」。UI と同じマスだと
     * 1600×900 の枠に 100×40 マスほど入り、**本家（地図領域は 66×22 マスほど）より
     * かなり細かく広く**なる。どちらが良いかは好みなので、回せるようにした。
     * @note 大きくすると**コアへ要求する可視窓が狭くなる**（枠に入るマスが減るので）。
     * 字の大きさと見える範囲は必ず反対に動く——これは避けようがない。
     */
    int ascii_panel_px{ 0 };
    static constexpr int kAsciiPanelPxMin = 10;
    static constexpr int kAsciiPanelPxMax = 48;

    EntityStyle entity_style{ EntityStyle::Slab };
    /*!
     * @brief **画調**。
     *
     * @details 2026-08-19 に決めた「TRON 風の HD2D モード」。**独立したモードでは
     * なく軸**にした（決めたこと）ので、見下ろし・一人称・VR・板・アスキー実体の
     * どれとでも組み合わさる。
     *
     * 型は描き手の側（`render/scene_look.h`）の `SceneLookKind` をそのまま持つ。
     * ここに写した enum を別に立てると、値の並びが片方だけ動いたときに
     * **cfg の中身が黙って別の画調を指す**（`MoveSmoothing` と同じ流儀）。
     * @note 値は cfg に書くので**増やすときは末尾へ**。
     */
    SceneLookKind scene_look{ SceneLookKind::Standard };
    /*!
     * @brief TRON のときに**時刻に応じた空を出すか**（偽 ＝ 真っ黒。既定）。
     *
     * @details 2026-08-19 に決めた（昼夜・天候・空の扱い＝「利用者が切り替え」）。
     *
     * | | 偽（既定） | 真 |
     * |---|---|---|
     * | 空の天球・雲・霧・蒸気 | **出さない** | 出す |
     * | 方向光・環境光 | ほぼ消す（形がかすかに読めるぶん） | 時刻に従う（寒色へ寄せて弱める） |
     * | フォグの色 | 黒 | その時刻の空の色を暗く寒色へ |
     *
     * @note **標準の画調では 1 ビットも効かない。**TRON でないときにこの旗を回しても
     * 絵は変わらない（`apply_look_to_*` が画調で早く戻る）。
     */
    bool tron_sky{ false };
    /*!
     * @brief TRON のときに**ブロックの面へマスの記号を出すか**（既定 真）。
     *
     * @details 最初に決めたこと「ブロックの各面には
     * アスキーアートの文字が入っている……各面の真ん中にアスキー文字が浮かんでいる」。
     * 既定で出す。**細かすぎると感じたときの逃げ口**として切れるようにしてある
     * （切っても黒い面とネオンの線は残る）。
     * @note 標準の画調では 1 ビットも効かない。
     */
    bool tron_face_glyph{ true };
    /*!
     * @brief **面の汚し**の強さ（百分率。0 = 掛けない・100 = 既定）。
     *
     * @details 2026-08-23 に決めた:「ボクセルでできたブロックやオブジェクトについて
     * ボクセルの凹凸以上にディテールを追加したい。テクスチャに汚しを加えてディテール」。
     * 中身は `render/surface_wear.h`（溜まり汚れ・角の摩耗・斑・垂れの 4 層）。
     *
     * **画調とは別の軸**である——標準でも TRON でも同じように掛かる。
     * 0 にすると**従来と 1 ビットも変わらない**（シェーダが早く抜ける）。
     * @note 値は cfg に書くので**増やすときは末尾へ**。
     */
    int wear_pct{ 100 };
    static constexpr int kWearPctMin = 0;
    static constexpr int kWearPctMax = 200;
    /*!
     * @brief **材質ごとに汚しを掛けるか**（cfg の `wear_materials=`。既定は全部）。
     *
     * @details 2026-08-23 に決めた:「まずは各オブジェクトごとにどの色が何に
     * 使われているのかを確認し、その情報を持つようにして。そのうえで、材質ごとに
     * 汚しをかけるか否か決める」。何が何に使われているかは
     * （焼くのは `tools/voxel/report_palette_uses.py`）。
     *
     * **既定は石・土・木・金・漆喰・布**（2026-08-23 に決めた。それ以外は掛けない）。
     * 綴りは `stone,wood,leaf` の形。`all` / `none` も書ける
     * （読み書きは `render/surface_wear.h` の `parse_wear_materials` / `wear_materials_text`）。
     */
    std::uint32_t wear_materials{ kWearDefaultMaterials };
    /*!
     * @brief **木の葉**の細かさ（百分率。0 = 描かない・100 = 既定）。
     *
     * @details 2026-08-23 に決めた「木の葉を詳細な葉にしたい」。中身は
     * `render/leaf_detail.h`（粒・塊の縁・葉の形の 3 層）。**汚しとは別の軸**で、
     * 掛かるのは材質が `MaterialClass::Leaf` の面だけ。
     * @note 値は cfg に書くので**増やすときは末尾へ**。
     */
    int leaf_pct{ 100 };
    static constexpr int kLeafPctMin = 0;
    static constexpr int kLeafPctMax = 200;
    /*!
     * @brief **アスキーの字の大きさ**（1 マスに対する %）。2026-08-19 に決めた「設定で回せるように」。
     *
     * @details 既定 80 は実装時の仮の値そのもの（背丈 0.8 マス）。実機で見ながら詰められるように
     * メニューへ出した——**字の読みやすさと地形の見えやすさは相反する**ので、
     * どこで釣り合うかは人によって違う（設計書 §14-3 の「詰める前に基準を聞く」の続き）。
     * 幅は字ごとの縦横比から出るので、ここは背丈だけを持つ。
     * @note 板（`EntityStyle::Slab`）のときは使わない。
     */
    int entity_glyph_pct{ 80 };
    static constexpr int kEntityGlyphPctMin = 40;
    static constexpr int kEntityGlyphPctMax = 160;

    /*!
     * @name ミニマップ
     *
     * @details 指示は「機能メニューの中に以下の詳細メニューをつくって機能させて／
     * 表示位置（右上、右下、左上、左下、画面中央）／表示サイズ／表示縮尺／表示濃度／
     * 方向表示（絶対表示、相対表示）」。
     *
     * **既定値は従来の見え方そのもの**にしてある（位置＝右上・サイズ 100%・
     * 1 マス 6px・濃度 100%・絶対）。ここを変えると、設定を足しただけで
     * 既存の絵の検査が動く。
     * @{
     */
    MinimapCorner minimap_corner{ MinimapCorner::TopRight };
    //! 大きさ（%）。枠そのものの寸法に掛かる。
    int minimap_size_pct{ 100 };
    /*!
     * @brief 縮尺 ＝ **1 マスの画素**。大きいほど拡大（見えるマス数は減る）。
     * @details 既定 6 は `draw_minimap` が持っていた `kPixelsPerGrid`
     * （2026-08-09 に決めた「縦横今の倍のサイズに」で 3 → 6 にした値）。
     */
    int minimap_cell_px{ 6 };
    //! 濃度（%）。下敷きと点の不透明度に掛かる。100 が従来。
    int minimap_opacity_pct{ 100 };
    /*!
     * @brief **方向表示**。偽 = 絶対（地図は北上のまま）／真 = 相対（地図ごと 90° 回す）。
     *
     * @details 2026-08-19 に決めた:「地図ごと回すかどうかを切り替える」。
     * 相対では**画面の奥とミニマップの上が揃う**（カーナビのヘッドアップ式）。
     * 絶対のままだと、視点を 90° 回したときに 3D と地図の向きが食い違う。
     *
     * @note 覚えた地図の形は世界座標なので、相対では**形が回って見える**。
     * それが嫌な人のために切り替えにしてある（既定は絶対）。
     */
    bool minimap_relative{ false };
    static constexpr int kMinimapSizePctMin = 50;
    static constexpr int kMinimapSizePctMax = 200;
    static constexpr int kMinimapCellPxMin = 3;
    static constexpr int kMinimapCellPxMax = 14;
    static constexpr int kMinimapOpacityPctMin = 25;
    static constexpr int kMinimapOpacityPctMax = 200;
    /*! @} */

    bool backdrops{ true };
    /*!
     * @brief バーチャルパッド（2026-08-12 に決めた）。
     * @details Android の既定は表示・全ボタン入。Windows の既定は切
     * （メニューにも並べない。`ui/virtual_pad.h` の注記）。
     */
    VirtualPadSettings vpad{};

    /*!
     * @name ゲーム進行
     * @details **ここに持っても、そのままでは効かない。** 実際に時計を回すのは
     * コア側（`HengbandCore.exe` の `RealtimeClock`）なので、`UiStateMessage` に乗せて
     * 送り届けて初めて働く（`hd2d/net/core_link.cpp`）。**送り忘れると、
     * メニューの値だけ変わって何も起きない**という分かりにくい壊れ方をする。
     * @{
     */
    //! true = 実時間でゲームが進む。**既定 OFF**（従来どおりのターン制）。
    bool realtime_enabled{ false };
    //! 1 行動（通常速度で 10 刻み）あたりの秒数の表への添字。既定 4 ＝ 1.0 秒。
    int realtime_speed_index{ 4 };
    /*!
     * @brief 被弾したときの全面フラッシュ（属性で色が変わる）。
     * @details **リアルタイムではこれが「殴られたことに気づく」唯一の手立て**になっている
     * （被弾時の `-more-` を外してあるため）。切ると気づきにくくなる。
     */
    bool damage_flash{ true };
    //! 被弾したときの画面の揺れ。**酔う人がいるのでフラッシュと別に切れる。**
    bool damage_shake{ true };
    /*!
     * @brief 小窓（持ち物・足元の選択）の裏でも世界を進めるか。
     * @details true = 選んでいる間も殴られ、死ぬこともある（こちらの理想）。
     * false = 小窓の間は時計が止まる（落ち着いて選べる）。**遊び方の好みなので選択式**
     * （2026-08-12 に決めた「小窓の停止は選択式にしよう」）。
     */
    bool realtime_prompt_live{ true };
    /*!
     * @brief 速さの振れ幅の段（設計書 §4-2）。**自分の行動間隔がここまでしか変わらない。**
     * @details 加速・減速のうち、はみ出た分は**世界の拍**へ載る（加速＝世界がスロー）。
     * どれを選んでも**ゲームの有利不利は変わらない**（相対の倍率が保存されるため）。
     * 既定 1 ＝ 2.0 倍まで。0 にすると自分の拍は一切変わらない。
     */
    int realtime_self_span_index{ 1 };
    static constexpr int kRealtimeSelfSpanCount = 5;
    //! 段 → 表示。**値そのものはコア側（`SdlUiOptions`）が持つ**なので、ここは名前だけ持つ。
    static const char *realtime_self_span_label(int index);
    static constexpr int kRealtimeSpeedCount = 8;
    //! 添字 → 1 行動あたりの秒数。**`SdlUiOptions` の同名関数と同じ表**（片方だけ直さないこと）。
    static float realtime_seconds_per_turn(int index);
    /*! @} */

    /*!
     * @name 音（2026-08-21 に決めた「Sound オプションとして機能メニューに追加」）
     *
     * @details **ここに持っても、そのままでは鳴らない。** 鳴らすのはコア側で
     * （変愚は `presentation/audio/`、幻想蛮怒は `gensoband/adapter/gb_audio.c`）、
     * 画面側は `UiStateMessage` の `audio` に乗せて送り届けるだけである
     * （`hd2d_app.cpp` の `build_ui_state`）。**送り忘れると、メニューの値だけ
     * 変わって何も起きない**——リアルタイム進行と同じ壊れ方をする。
     *
     * @note **音量の段はここが 0〜10、プロトコルは 0〜9 の添字で向きが逆**である
     * （`SdlUiOptions::kVolumeLevels` ＝ 10。添字 0 が 100%、9 が 10%）。
     * 人が回すつまみは「大きいほど大きい」でなければ読めないので、こちらを段で持ち、
     * 変換は `volume_step_to_index()` の 1 か所だけでやる。
     * @note **0 は無音**（表に 0% の段が無いので、入切のほうを偽にして届ける）。
     * 「入のままつまみを 0 まで絞る」を潰さないための扱いで、次に上げれば元へ戻る。
     * @{
     */
    //! 音量の段の上限。**0〜`kVolumeMax` の 11 段**（0 は無音）。
    static constexpr int kVolumeMax = 10;
    /*!
     * @brief BGM に何を流すか。
     * @details **入切ではなく 3 択**にしてある——「環境音」と「切」を別の行に置くと
     * 切る道が 2 か所になり、どちらが効いているのか読めなくなる。
     *
     * | 値 | 誰が鳴らすか |
     * |---|---|
     * | `Music` | **コア側**（変愚は MCI / SDL_mixer、幻想蛮怒は MCI）。従来どおり |
     * | `Ambience` | **画面側**（`hd2d/audio/`）。同時にコアの曲は止める |
     * | `Off` | 誰も鳴らさない |
     *
     * @note `Ambience` のときコアへ `music_on = false` を送る（`build_ui_state`）。
     * **鳴らす場所を 2 つにしない**ための約束で、これでプロトコルを増やさずに済む。
     */
    enum class BgmMode {
        Music = 0,
        Ambience = 1,
        Off = 2,
        Count = 3,
    };
    BgmMode bgm_mode{ BgmMode::Music };
    bool sound_enabled{ true };
    //! 0（無音）〜10（最大）。既定は最大——従来（設定が無かった頃）と同じ鳴り方にする。
    int music_volume{ kVolumeMax };
    int sound_volume{ kVolumeMax };
    //! 段 → プロトコルの音量添字（10 → 0 ＝ 100% … 1 → 9 ＝ 10%）。0 は 1 と同じ扱い。
    static int volume_step_to_index(int step);
    /*! @} */

    //! いま覚えている中身と違うか（書き戻すかの判定）。
    bool differs_from(const Hd2dSettings &other) const;
};

/*!
 * @brief `hd2d.cfg` を読む。
 * @return 読めたら true（無ければ false。**それは失敗ではない**）。
 * @details 知らないキーは黙って飛ばす（版が進んだ cfg を読んでも落ちない）。
 */
bool load_settings(const std::string &path, Hd2dSettings &out);

//! `hd2d.cfg` へ書く。書けたら true。
bool save_settings(const std::string &path, const Hd2dSettings &settings);

//! 既定の置き場所（`hd2d.cfg`）。
const char *default_settings_path();

/*!
 * @brief **そのコアでの**サブパネルの中身の既定を `out` へ書く（2026-08-23 に決めた）。
 * @param core_name コア名（`hello_ack.core_name`。`"silq"` / `"hengband"` …）。
 *
 * @details サブウインドウの番号は**コアごとに意味が違う**ので、既定も 1 つでは足りない。
 * Sil-Q で変愚の既定（6 = メッセージ）をそのまま使うと、その番号に描画関数が無く
 * **空の枠が 3 枚並ぶ**（実際にそうなっていた）。
 *
 * | | 下段 1〜4 | 右列 1〜3 |
 * |---|---|---|
 * | Sil-Q | 前のメッセージ・見えている敵・戦いの目・敵の地形の記憶 | 冒険者・装備・持ち物 |
 * | それ以外 | メッセージ・思い出・装備品・プレイヤー | 所持品・UI 既定・UI 既定 |
 *
 * **そのコア専用の cfg がまだ無いときだけ呼ぶこと**（＝そのコアで初めて遊ぶとき）。
 * 呼ぶ側は `hd2d_app.cpp` の握手の直後 1 か所だけである。
 */
void default_sub_panel_kinds(const std::string &core_name, int out[kUiSubPanels]);

/*!
 * @brief **コアごとの枚数と仕切りの既定**（`sub_split`）。
 * @details いま違うのは右列の枚数だけ（Sil-Q は 3 枚・ほかは 2 枚）。
 * `default_sub_panel_kinds()` と**同じ所で**呼ぶこと——中身と枚数が食い違うと、
 * 出ない枠に割り当てが残る。
 */
void default_sub_split(const std::string &core_name, SubSplit &out);

/*!
 * @brief **そのコア専用の cfg の名前**（`SilCore.exe` → `hd2d-SilCore.cfg`）。
 *
 * @details 設定は**コアごとに別のファイルへ持つ**（2026-08-26 に決めた
 * 「cfg ってサブパネル以外にも共通項目多いよね。完全にコア毎の記述になるようにして」）。
 *
 * 1 つのファイルを全コアで使い回していたので、**別のコアで遊ぶと前のコアの設定が
 * 書き換わっていた**。とくにサブパネルの割り当ては番号の意味がコアごとに違うため、
 * 起動しただけで潰れていた（SH-39）。画面の作り・カメラ・ミニマップ・鍵の割り当ても
 * 同じ列で、コアが違えば入れたい値も違う。
 *
 * `hd2d.cfg` は**残す**。役目は 2 つ:
 *   - コアの一覧（`cores`）と前回選んだコア（`last_core`）。**コアを選ぶ前に要る**
 *   - そのコアのファイルがまだ無いときの**引き継ぎ元**（初めてそのコアで遊ぶとき）
 *
 * @param core_path cfg に書いてある exe への道。空なら `hd2d.cfg` を返す。
 */
std::string settings_path_for_core(const std::string &core_path);

} // namespace hd2d
