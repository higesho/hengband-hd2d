/*!
 * @file orb_hud.h
 * @brief HP/MP の玉（オーブ）と、バフ・デバフのアイコン。
 *
 * 地図の左下にあった HP/MP の棒に代わるもの。
 * ディアブロ 2 のような玉を画面の下の中央に重ね、玉の左右に、かかっている状態の
 * アイコンを名前つきで並べる（良い効果は左・悪い効果は右）。
 *
 * ## 玉の重ね順
 * `orb_back` → `orb_glass` → 液体（残量ぶんだけ切り出す）→ `orb_glass_top` → `orb_front`
 * → 水面の線・泡 → 字。素材は同じ大きさで、同じ矩形（`UiLayout::orb`）へ重ねる。
 * 液体の絵は**満杯**で、残量の割合に合わせて水面より下だけを幅 2〜3 画素の縦の帯に分けて
 * 貼る。帯ごとに水面の高さを少しずつずらすので、水面が波打つ。
 *
 * ## 動き
 * 表示する割合は、実際の割合へ**時間をかけて寄せる**（瞬時に変えない）。
 * - 回復（増える）: 液面がゆっくり上がる。上がっている間は液体の中を泡が昇る。
 * - ダメージ（減る）: 液面がすばやく下がり、波が一時的に大きくなってだんだん静まる。
 * 時間は**実時計（ms）**で数える（フレームを取りこぼしても速さが変わらない。
 * `combat_fx_view.h` と同じ考え方）。動きの状態（表示中の割合・波の強さ・泡）は
 * 描画の外（`HudState::orb_anim`）に持つ。
 *
 * ## コアごとの意匠と終末の太陽
 * 玉の素材と配置はコアごとに違う（`assets/ui/hud/<コア>/`。変愚は直下の今の素材）。握手の後に 1 度だけ
 * `set_orb_profile` で配置を覚え、描く手（`draw_orb_hud`）とレイアウト（`orb_visible_rect`）が同じ値を見る
 *。短愚だけは、残りの日数が届いていれば中央の飾りに終末の太陽を重ねる。
 *
 * ## 素材が読めなかったとき
 * 玉もアイコンも描かず、**元の棒**を `orb` の矩形の下寄りに描く（理由は呼び出し側が stderr に出す）。
 *
 * @note 画像は呼んだ瞬間に描かれ、`UiPaint`・`TextOverlay` は後でまとめて流れる。
 * 水面の線・泡・字・アイコンの下敷きは画像の上に乗る。
 */
#pragma once

#include "frame/game_frame.h"
#include "ui/ui_image.h"
#include "ui/ui_layout.h"
#include "ui/ui_paint.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hd2d {

class TextOverlay;
class UiPaint;

//! 玉の素材とアイコンのシート。**無くても遊べる**（読めなかった層は null のまま）。
struct OrbHudImages {
    const UiImagePainter *painter{ nullptr };
    const UiImage *back{ nullptr };
    const UiImage *glass{ nullptr };
    const UiImage *liquid_hp{ nullptr };
    const UiImage *liquid_mp{ nullptr };
    const UiImage *glass_top{ nullptr };
    const UiImage *front{ nullptr };
    //! アイコンを 10 列に並べた 1 枚（64x64 のセル）。
    const UiImage *status_icons{ nullptr };
    //! 天候のアイコンを 8 列 x 1 行に並べた 1 枚（64x64 のセル。`tools/hud/gen_weather_icons.py`）。左の状態欄の天候に使う。
    const UiImage *weather_icons{ nullptr };

    //! 玉の 6 層が全部読めているか。
    bool has_orb() const;
    //! アイコンのシートが読めているか。
    bool has_icons() const;
    //! 天候のアイコンのシートが読めているか。
    bool has_weather_icons() const;
};

//! 液面の動きの設定（`Hd2dSettings` の `orb_*` の写し。100% = 1.0）。
struct OrbHudParams {
    //! 偽なら水面は平ら（波の揺れ・ダメージのときの大波も出ない。残量の増減の動きは残る）。
    bool wave_enabled{ true };
    float wave_size{ 1.f };
    float wave_speed{ 1.f };
    float rise_speed{ 1.f };
    float fall_speed{ 1.f };
};

//! 液体の中を昇る泡 1 つ（設計の単位）。
struct OrbHudBubble {
    float dx{ 0.f }; //!< 玉の中心からの横のずれ
    float y{ 0.f };
    float speed{ 20.f }; //!< 上へ（単位/秒）
    float size{ 1.6f };
    float phase{ 0.f };
};

//! 玉 1 つぶんの動きの状態。
struct OrbHudGauge {
    float shown{ 0.f }; //!< いま表示している割合（0〜1）
    float target{ 0.f }; //!< 直近に渡された実際の割合
    float boost{ 0.f }; //!< ダメージで一時的に足す波の大きさ（だんだん 0 へ戻る）
    float bubble_carry{ 0.f }; //!< 湧く泡の端数（フレームをまたいで積む）
    std::vector<OrbHudBubble> bubbles;
};

//! 玉の動きの状態。**描画の外に持つ**（`HudState::orb_anim`）。
struct OrbHudAnimation {
    bool initialized{ false };
    std::uint32_t last_ms{ 0 };
    double wave_clock{ 0.0 }; //!< 波の位相（秒。速さの設定を掛けて積む）
    std::uint32_t rng{ 0x2545F491u };
    OrbHudGauge hp;
    OrbHudGauge mp;
    /*!
     * 玉の横のアイコンの巡回（あふれたとき）の起点。左（良い効果ほか）・右（悪い効果）で別に持つ。
     * 並びの中身（`status_ring_signature`）が変わったら、その側の起点を今に置き直す（先頭へ戻る）。
     */
    bool ring_initialized[2]{ false, false };
    std::uint32_t ring_origin_ms[2]{ 0u, 0u };
    std::uint64_t ring_signature[2]{ 0u, 0u };

    //! 初期化し直す（次の更新で表示を実際の割合へ即座に合わせる）。
    void reset();
};

/*!
 * @brief 動きを `now_ms`（単調な ms）まで進める。
 * @param hp_ratio,mp_ratio 実際の割合（0〜1。範囲外は丸める）。
 * @details 初回、または間が 1 秒以上空いたとき（メニューを開いていたなど）は、表示を
 * 実際の割合へ**即座に合わせる**（古い動きを引きずって見せない）。
 */
void orb_hud_update(OrbHudAnimation &anim, const OrbHudParams &params, float hp_ratio, float mp_ratio,
    std::uint32_t now_ms);

/*!
 * @brief 玉とアイコンを描く（動きの更新 `orb_hud_update` も中でやる）。
 * @param layout 物理の画素の配置（`MenuScaleScope` の外で呼ぶこと）。
 * @return 玉の画像で描いたら true。素材が無くて棒に落ちたら false。
 * @details `layout.orb` が空、または `hp_max <= 0`（ゲーム前）のときは何も描かない（false）。
 * `sp_max <= 0` のコアでは MP の液体・数字・名札を描かない（玉の枠は描く）。
 */
bool draw_orb_hud(UiPaint &paint, TextOverlay &text, const UiLayout &layout, const GameFrame &frame,
    const OrbHudImages &images, const OrbHudParams &params, OrbHudAnimation &anim, std::uint32_t now_ms);

/*!
 * @name 短愚の終末の太陽（`HudSnapshot::doomsday_days_left`。決めたこと 2026-10-11）
 * @details 短愚の誕生時オプション `doomsday`（既定 ON）の残り日数を、玉の中央の飾りに重ねて見せる。
 * 1 日目（残り 14）は黄色い太陽で、日が進むごとに黒くなり、14 日目（残り 1）でほぼ黒い日食、
 * 15 日目以降（終末。残り 0）は黒い太陽に赤い光。周りに 14 の目盛りを置き、**残りの日数ぶん**を明るくする。
 * 原作が日の変わり目に出すメッセージ（「終末まで 残り N日」）と同じ情報だけを使う。
 * @{
 */
constexpr int kDoomsdayTicks = 14; //!< 目盛りの数（終末までの日数）

//! 残り日数 1 つぶんの見た目（描く手と検査が同じ値を見る）。
struct DoomsdaySunLook {
    PaintColor disc; //!< 太陽の円の色
    PaintColor glow; //!< 円の周りの光（にじみ）の色。`a` が最も濃い所の濃さ
    PaintColor tick_lit; //!< 明るい目盛りの色
    PaintColor tick_dim; //!< 暗い目盛りの色
    int lit_ticks{ 0 }; //!< 明るい目盛りの数（= 残り日数。終末は 0）
    bool doomsday{ false }; //!< 終末（残り 0）。黒い太陽に赤い光
};

/*!
 * @brief 残り日数から太陽の見た目を決める。
 * @param days_left 残り日数（1〜14。0 は終末。範囲外は丸める）。
 * @details 円の色は黄色（残り 14）から橙・燃えさしの赤を通って黒（残り 1）へ。残り 0 は黒に赤い光。
 */
DoomsdaySunLook doomsday_sun_look(int days_left);
/*! @} */

/*! @name 字の名札（既定は `HP` / `SP`。コアが `hud.hp_label` / `hud.sp_label` で上書きする） @{ */
const char *hp_label_of(const GameFrame &frame);
const char *sp_label_of(const GameFrame &frame);
/*! @} */

/*!
 * @name 設計の単位での寸法
 * @details 変愚の値（`kOrbHpCenterX` ほか）と、コアごとの配置（`OrbProfile`）は `ui/orb_profile.h`。
 * ここに残すのは、配置に依らない字の大きさだけ。
 * @{
 */
//! 「HP」「MP」の字の大きさ（普通の字の倍率。数字の帯の字より大きい）。
constexpr float kOrbLabelScale = 1.5f;
/*! @} */

/*!
 * @name コアごとの配置と素材
 * @{
 */
//! 素材を引く基底（`assets/ui/hud`）。
constexpr const char *kOrbAssetRoot = "assets/ui/hud";

/*!
 * @brief 握手の `core_name` から、素材の組の名前（`assets/ui/hud/<ここ>/`）を引く。
 * @return 登録されたコアなら `tangband` など。**変愚と知らないコアは空**（`assets/ui/hud/` 直下の素材のまま）。
 * @details 大文字小文字は区別しない。対応は `hello_ack.core_name` の綴り（`hengband` / `tangband` / `gensoband` /
 * `silq` / `frox` / `angband` / `Umoria` / `Rogue 3.6` / `Rogue 5.4` / `RogueClone2`）。
 */
std::string orb_asset_key_for_core(std::string_view core_name);

//! 素材の組の置き場（`assets/ui/hud` または `assets/ui/hud/<キー>`）。
std::string orb_asset_dir(std::string_view asset_key);

//! 玉の 1 層の素材のパス。`layer` は `orb_back` / `orb_glass` / `orb_liquid_hp` / `orb_liquid_mp` / `orb_glass_top` / `orb_front`。
std::string orb_layer_path(std::string_view asset_key, std::string_view layer);

/*!
 * @brief `orb_layout.json` の本文から配置を読む。
 * @param json 本文（平たい JSON。数だけを読む）。
 * @param[in,out] out 既定値（変愚）から始めて、書いてある項目を上書きする。`asset_key` は触らない。
 * @return `cx_hp`・`cx_mp`・`cy`・`r` が揃っていて、値が設計の範囲（760x250）に収まっていれば true。
 * @details 項目: `cx_hp` `cx_mp` `cy` `r` `number_y` `label_bottom_y` `art_bottom_y`、飾りは `emblem_x` `emblem_y`
 * `emblem_r`（3 つ揃ったときだけ `emblem.present`）。無い項目は既定のまま。
 */
bool parse_orb_layout(std::string_view json, OrbProfile &out, std::string &err);

/*!
 * @brief コア名に合う玉の配置を読む（`assets/ui/hud/<コア>/orb_layout.json`）。
 * @return 読めた配置。**読めない・登録が無いときは既定（変愚）**を返し、`note` に理由（登録が無いだけなら空）を入れる。
 * 画像はここでは読まない（呼び出し側が `orb_layer_path` で読む）。
 */
OrbProfile load_orb_profile(std::string_view core_name, std::string &note);
/*! @} */

/*!
 * @brief アイコンの名前（`StatusEffectView::key`）から、シートの番号を引く。
 * @return 知らない名前は 0（汎用のアイコン）。表は `hd2d/ui/status_icon_table.inc`（生成物）。
 */
int status_icon_index(std::string_view key);
//! 表に載っている名前か（0 番の `unknown` 自身も載っている）。
bool status_icon_known(std::string_view key);

//! 名前（アイコンの下）・効果量（アイコンの上）の字を、アイコンの幅へ縮める下限（普通の字の倍率）。
constexpr float kStatusTextMinScale = 0.55f;
//! 基準の字の大きさを決めるときに測る文字列（全角 4 文字）。
constexpr const char *kStatusTextReferenceString = "ああああ";
//! 基準の大きさのまま描く名前の長さの上限（表示上の幅。全角 1 文字 = 半角 2 文字 = 2）。
constexpr int kStatusTextReferenceColumns = 8;
//! 悪い効果（`Bad` / `PermanentBad`）の名前・効果量の下敷きの色（暗めの赤）。それ以外は黒 58%。
constexpr float kStatusBandBadR = 0.55f;
constexpr float kStatusBandBadG = 0.06f;
constexpr float kStatusBandBadB = 0.06f;
constexpr float kStatusBandBadA = 0.80f;
//! 常時の列のアイコンの大きさ = 玉の横のアイコン x この値（下限 `kPermanentIconMinPx`）。
constexpr double kPermanentIconRatio = 0.8;
constexpr int kPermanentIconMinPx = 24;

/*!
 * @brief 基準の字の高さ（画素）。「全角 4 文字が `icon_px`（アイコンの幅）に収まる、いちばん大きい字」。
 * @details 普通の字の大きさは超えない。アイコンの大きさごとに 1 つ決まる（玉の横と常時の列は
 * アイコンの大きさが違うので、それぞれ別の値になる）。`layout_scale` が 1 でないときは普通の大きさ。
 */
int status_text_reference_px(TextOverlay &text, int icon_px);

/*!
 * @brief 字を `target_w`（アイコンの幅）に収める大きさを決める。
 * @details 基準の大きさは `status_text_reference_px(text, target_w)`。
 * - 表示上の幅が 4 文字ぶん（`kStatusTextReferenceColumns`）以内の名前は、**すべて基準の大きさ**
 *   （短い名前も大きくしない・小さくしない）。
 * - 基準の大きさで収まる名前も基準の大きさ。
 * - 収まらない名前は、字の高さ（画素）を 1 つずつ下げ、最初に収まった大きさにする。
 *   下限は普通の字の `kStatusTextMinScale` 倍（基準がそれより小さければ基準）で、下限でも
 *   収まらないときは**下限の大きさのまま**幅を返す（呼ぶ側が 1 個の幅を広げる）。
 */
struct FittedText {
    float scale{ 1.f };
    //! 幅。2 行のときは長いほうの行の幅。
    int width{ 0 };
    //! 下限でも `target_w` に収まらなかった。
    bool overflow{ false };
    /*!
     * 行数（1 または 2）。`fit_status_text` は常に 1。名前（`fit_status_name`）だけ 2 になりうる。
     * 2 行のとき、1 行目は `[0, break_end)`、2 行目は `[break_begin, 文字列の終わり)`
     * （折る位置が空白なら、その空白は両方の行に含めない）。取り出しは `status_name_line`。
     */
    int lines{ 1 };
    std::size_t break_end{ 0 };
    std::size_t break_begin{ 0 };
    //! 各行の幅（画素）。**2 行のときだけ**入る（1 行のときは `width` を見る）。
    int line_width[2]{ 0, 0 };
};
FittedText fit_status_text(TextOverlay &text, std::string_view utf8, int target_w);

//! 名前を折ってよい位置 1 つ。1 行目は `[0, first_end)`、2 行目は `[second_begin, 終わり)`。
struct StatusNameBreak {
    std::size_t first_end{ 0 };
    std::size_t second_begin{ 0 };
};

/*!
 * @brief 名前を 2 行に折ってよい位置の一覧（左から順。折れないなら空）。
 * @details 規則:
 * - すべて ASCII（英語）なら、**空白の位置だけ**（空白は行に含めない。どちらかの行が空になる位置は除く）。
 * - それ以外（日本語）は、隣り合う 2 字 a・b の間で、
 *   a が「の」、または b が「（」なら折ってよい。そうでなければ、a・b のどちらかが
 *   ひらがな・カタカナ・漢字のどれでもない字（記号・数字・英字・空白など）なら折らない、
 *   両方カタカナ（長音記号「ー」を含む）なら折らない、両方ひらがなら折らない、
 *   a が漢字で b がひらがな（送りがな）なら折らない。それ以外は折ってよい。
 * 字の種類は UTF-8 を解いた符号位置で判定する。
 */
std::vector<StatusNameBreak> status_name_break_points(std::string_view utf8);

/*!
 * @brief 名前（アイコンの下）を、アイコンの幅 `target_w` に収まる大きさ・行数に決める。
 * @details 基準の大きさは `status_text_reference_px(text, target_w)`。
 * 1. 基準の大きさで 1 行に収まる（または全角 4 文字以内）なら 1 行。
 * 2. 収まらなければ、基準の大きさで 2 行に折り、両方の行が収まればそれ。
 *    折る位置が複数あれば、2 行の長いほうの幅がいちばん短くなる位置（同じなら左）。
 * 3. 収まらなければ、2 行のまま字を 1 画素ずつ縮め、両方の行が収まった大きさにする
 *    （下限は `fit_status_text` と同じ）。
 * 4. 下限でも収まらなければ、下限の大きさの 2 行のまま `overflow` を立てる（呼ぶ側が幅を広げる）。
 * 5. 折る位置が 1 つも無い名前は、1 行のまま `fit_status_text` と同じに縮める。
 * `layout_scale` が 1 でないときは折らない（1 行）。
 */
FittedText fit_status_name(TextOverlay &text, std::string_view utf8, int target_w);

//! 折った名前の `index` 行目（0 または 1）。`fit.lines` が 1 のとき、1 行目は名前そのもの、2 行目は空。
std::string_view status_name_line(std::string_view utf8, const FittedText &fit, int index);

/*!
 * @brief 名前の 1 行の高さ（画素）。基準の字（`status_text_reference_px`）の行の高さ。
 * @details 名前の帯は `行数 x この高さ`。玉の横の段は名前の場所を常に 2 行ぶん取る。
 */
int status_name_line_height(TextOverlay &text, int icon_px);

//! アイコン 1 個の置き場所。
struct StatusIconPlacement {
    int effect_index{ 0 }; //!< `frame.status_effects` の添字
    bool left{ true }; //!< 玉の左側か
    int row{ 0 }; //!< 玉の高さの行（常に 0。1 行だけ）
    int icon_id{ 0 }; //!< シートの番号
    /*!
     * 巡回しているときの印。下の矩形は整数（小数の位置を切り下げたもの）で、本当の位置は
     * 横に `frac_x`（0 以上 1 未満）を足した所。描くときは必ず足す。
     * `clip` は見えている枠（これの外へは描かない）、`alpha` は枠に見えている割合（端で薄くなる）。
     * `ring_index` は輪の中での並びの番号（0 が先頭）、`ring_distance` は玉に近い端から
     * このアイコンの玉側の辺までの距離（画素。負なら玉側の端から入りかけ）。
     */
    bool rotating{ false };
    float frac_x{ 0.f };
    RectPx clip;
    float alpha{ 1.f };
    int ring_index{ 0 };
    float ring_distance{ 0.f };
    RectPx cell; //!< アイコン・名前（常に 2 行ぶん）・効果量を含む 1 個ぶんの場所（隙間を含む）
    RectPx icon;
    //! アイコンの下の名前の帯。高さは `name.lines x StatusIconLayout::name_line_h`（1 行なら上の行に置く）。
    RectPx name_band;
    RectPx amount_band; //!< アイコンの上の効果量の帯（効果量が無ければ空）
    FittedText name; //!< 名前の字の大きさ・行数
    FittedText amount; //!< 効果量の字の大きさ
};

//! 片側（左または右）の巡回の輪の情報。
struct StatusRing {
    bool rotating{ false }; //!< 1 行に入りきらず、巡回しているか
    int count{ 0 }; //!< 輪の個数（その側のアイコンの数）
    int capacity{ 0 }; //!< 枠に入る個数（巡回しているときのみ）
    int pitch{ 0 }; //!< 1 個ぶんの幅（全員同じ。巡回しているときのみ）
    RectPx window; //!< 見えている枠（巡回しているときのみ）
    double length{ 0.0 }; //!< 輪の長さ = count x pitch
    double speed{ 0.0 }; //!< 画素/秒（アイコンの幅 + すき間）
};

struct StatusIconLayout {
    int icon_px{ 0 };
    //! 名前 1 行の高さ（`status_name_line_height`）。名前の場所は常に `2 x` これ（1 行の名前があっても段の高さは変わらない）。
    int name_line_h{ 0 };
    std::vector<StatusIconPlacement> items;
    StatusRing ring_left;
    StatusRing ring_right;
    //! 場所が足りなくて置けなかった個数。
    int dropped{ 0 };
};

//! アイコンの一辺（玉の高さ × 0.24。下限 24・上限 64）。
int status_icon_size(const RectPx &orb);

/*!
 * @brief 巡回の輪の中身の印（左 = 良い・待っている・特殊 / 右 = 悪い）。
 * @details 輪に入るアイコンの並び（名前 `key` と種類 `kind`）から作る。効果量や名前の字の変化では
 * 変わらない（`+10` が `+11` になっても先頭へ戻さない）。変わったら巡回を先頭から始め直す。
 */
std::uint64_t status_ring_signature(const GameFrame &frame, bool left);

/*!
 * @brief `frame.status_effects` の置き場所を決める（描かずに）。
 * @details 良い・待っている・特殊は玉の左、悪いは右。**左は 良い → 待っている → 特殊 の順で
 * 玉に近い側から外へ**、右は玉に近い側から外へ並べる（並びの中はフレームの順）。
 * **左右とも 1 行だけ**（上に段を積まない）。縦は、アイコンの上端が玉の上から 42% あたり。
 * 左の端は地図の左＋1 マス（状態列が左にあるときはその右）、右の端は地図の右−1 マス
 * （状態列が右にあるときはその左）。1 行目は常時の列（`layout_permanent_icons`）より下なので、
 * 常時の列があっても端まで使える。
 * 1 個の幅は「アイコンの幅＋すき間」で、名前は最大 2 行に折り、効果量は 1 行で、字を縮めて収める
 * （収まらないものだけ幅を広げる。`fit_status_name` / `fit_status_text`）。
 * 名前の場所は**常に 2 行ぶん**（1 行の名前は上の行。帯は実際の行数ぶん）なので、1 行・2 行が
 * 混ざっても段の高さ（`StatusIconPlacement::cell` の高さ）は変わらない。
 *
 * ## 1 行に入りきらないとき（巡回）
 * 入りきらない側は、全員を同じ幅（いちばん広いものの幅）の**輪**にして、玉から遠ざかる向きへ
 * **等速で止まらずに流す**（左は左へ・右は右へ。速さは 1 秒にアイコン 1 個ぶん =
 * アイコンの幅＋すき間）。位置は `left_ring_ms` / `right_ring_ms`（その側の巡回を始めてからの
 * 実時計の ms）から直接計算し、画素の端数は `StatusIconPlacement::frac_x` に出す。輪の最後の次に
 * 最初が続く（継ぎ目で飛ばない）。見えている枠（`StatusRing::window`）の外へはみ出す分は描く側が
 * 切り、端で入りかけ・出かけのアイコンは `alpha` で薄くなる。置き場所に出るのは**枠に一部でも
 * 見えているもの**だけ。入りきる側は回さない（`*_ring_ms` を無視する）。
 * **検査もこれを使う**（描いた所と数える所を同じ値から出す）。
 * 常時の効果（`Permanent` / `PermanentBad`）はここでは置かない。
 */
StatusIconLayout layout_status_icons(TextOverlay &text, const UiLayout &layout, const GameFrame &frame,
    double left_ring_ms = 0.0, double right_ring_ms = 0.0);

//! 常時の効果 1 個の置き場所。
struct PermanentIconPlacement {
    int effect_index{ 0 }; //!< `frame.status_effects` の添字
    bool bad{ false }; //!< 呪い（`PermanentBad`）か
    int column{ 0 }; //!< 状態列に近い側から数えた列の番号
    int icon_id{ 0 };
    //! アイコンと名前を含む 1 個ぶん（横の隙間を含み、縦の隙間は含まない）。高さは名前の行数で変わる。
    RectPx cell;
    RectPx icon;
    RectPx name_band; //!< 高さは `name.lines x PermanentIconLayout::name_line_h`
    FittedText name;
};

struct PermanentIconLayout {
    int icon_px{ 0 };
    //! 名前 1 行の高さ（`status_name_line_height`）。
    int name_line_h{ 0 };
    //! 状態列が右にあるため、列を左へ伸ばしているか。
    bool grows_left{ false };
    int columns{ 0 };
    std::vector<PermanentIconPlacement> items;
    int dropped{ 0 };
    //! 置いてよい縦の範囲（上端・下端）。ミニマップと横が重なる列は、これより狭い（下の列ごとの値）。
    int top{ 0 };
    int bottom{ 0 };
    //! 列ごとの縦の範囲（`columns` 個）。
    std::vector<int> column_top;
    std::vector<int> column_bottom;
    //! 列ぜんぶを囲む矩形（置いたものが無ければ空）。
    RectPx extent;
};

//! 常時の列のアイコンの一辺（玉の横のアイコン × 0.8。下限 24）。
int permanent_icon_size(const RectPx &orb);

/*!
 * @brief 常時の効果（`Permanent` / `PermanentBad`）の置き場所を決める（描かずに）。
 * @details 状態列（`UiLayout::status_col`）の**横**に縦に並べる。状態列が左にある（または無い）
 * 作りでは、状態列の右端＋すき間（無ければ地図の左端＋1 マス）から右へ列を増やし、
 * 右にある作り（Frox など）では状態列の左端−すき間から左へ増やす。
 * 並びは 良い効果 → 呪い（間に少しすき間）で、中はフレームの順。上から下へ並べ、
 * 下まで来たら次の列へ移る。縦の範囲は、上が地図の上端（ミニマップが同じ側の上にあるときは
 * その下）、下が「玉の横のアイコンの 1 段目の上端」とメッセージ・プロンプト・最下段の帯の上
 * （ミニマップが同じ側の下にあるときはその上）。**ミニマップと横が重なる列だけ**その範囲を狭め、
 * 重ならない列（ミニマップの向こう側）は地図の上端から使う。横は玉の矩形の手前まで。
 * 置けなかった分は `dropped` に数える。
 * 名前は最大 2 行で、1 個ずつ**実際の行数**で詰めて並べる（下の列の 1 個の高さは行数で変わる）。
 */
PermanentIconLayout layout_permanent_icons(TextOverlay &text, const UiLayout &layout, const GameFrame &frame);

} // namespace hd2d
