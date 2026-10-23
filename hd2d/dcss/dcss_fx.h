/*!
 * @file dcss_fx.h
 * @brief DCSS の攻撃の演出（ビームの弾・矢・投げた物・爆発・閃光）を、原作のタイルの絵のまま立体の地図の上に出す。段 4。
 *
 * ## 何を出すか
 * 原作のタイル版は、ビーム・矢・魔法の玉・爆発を、地図のマスに**重ねる絵**（`t.ov` の画像ページ main のタイル。`bolt::draw`・`explosion_draw_cell`）と、
 * その間の待ち（`delay`）で描く。コアは待ちのたびに今の絵を画面へ送る（`dcss_frame.cpp` の `marks_json` の `f`・`fl`）。
 * ここはそれを受けて、**原作のタイルの画素をそのままボクセルにして**立体の地図の上に置く（決めたこと「原作のタイルの色や形に寄せる」）。
 *
 * 爆発の範囲は、原作が 1 輪ずつ足していく絵（半径 0・1・2… の順。1 輪ごとに 50 ミリ秒の待ち）の**現れた順そのまま**で広がる。
 * ビームの弾・矢の道筋は、原作が**一度にまとめて**出す（`bolt::draw` が道筋の全部のマスを足してから 1 回だけ待つ）。画面は同じ向きに並んだマスの列を
 * 撃ち手の側から順に点け（`kStepSeconds` ずつ。全体で `kFlightSeconds` まで）、弾が飛ぶ道筋どおりに動かす。撃ち手はコアが教える（`DcssFxCell::by_player`。
 * プレイヤーの行動の最中ならプレイヤーの側から、怪物・味方の行動ならプレイヤーから遠い端から）。
 * 各タイルは**点いた時刻**から、新しいものほど明るく、古いものは薄れる（彗星の尾）。原作の待ちは 15〜150 ミリ秒と短いので、
 * 消えてからも短く残す（`kGhostSeconds`）。同じ絵が止まったまま残る場面（`-more-` の待ちなど）でも、一定の時間で薄れて消える。
 *
 * ## 既存の演出の仕組みとの関係
 * 他のコアの攻撃の演出（`render/attack_fx_view.*`・）は、コアが `ProjectionFx`（形・属性の名前・始点・着弾・経路・範囲）を送る。
 * DCSS のウェブタイル版の JSON にはそれらが無く（タイルの番号だけ）、足すには原作の `bolt` に観測の呼び出しが要る（原作には手を入れない約束なので行わない）。
 * そこで**描き手だけ共有**する: ボクセル（`FxVoxel`）と点光源を `FxFrame` へ足し、`VoxelFxRenderer`（雲と同じ道）が描く。
 *
 * ## 絵の置き方（`assets/ui/dcss/atlas/fx_tiles.tsv`。`tools/dcss/gen_dcss_fx.py` が作る）
 * - 実物の矢・弾（`M`）と向きのある魔法の絵（`D`）: 地図の面に**寝かせて**浮かせる。向きの変種（8 方向）が絵の中に描かれているので、カメラを回しても向きが合う。
 * - 丸い塊の魔法の絵（`B`）・表に無いタイル（投げたアイテムの絵ほか）: カメラへ向けて**立てる**。
 * 光る絵（`D`・`B`）は自己発光にして、点光源を足す（枠は少ない。弾の先頭と全体の重心）。
 */
#pragma once

#include "dcss/dcss_marks.h"
#include "render/math3d.h"
#include "render/voxel_fx.h"

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace hd2d::dcss {

//! 絵の種類（`fx_tiles.tsv` の 1 字）。
enum class DcssFxKind : std::uint8_t {
    Missile = 0, //!< M: 実物の矢・弾・投げ槍・ブーメラン。寝かせる。光らない
    Directional = 1, //!< D: 向きのある魔法の絵。寝かせる。光る
    Billboard = 2, //!< B: 丸い塊の魔法の絵。カメラへ向けて立てる。光る
    Item = 3, //!< 表に無い main のタイル（投げたアイテムの絵ほか）。立てる。光らない
};

//! タイルの画素 1 つ（絵の左上を (0,0)・右下を (1,1) とした中心の位置を -0.5〜0.5 に直した値）。色は線形。
struct DcssFxPixel {
    float u{ 0.f };
    float v{ 0.f };
    float r{ 0.f };
    float g{ 0.f };
    float b{ 0.f };
    float a{ 0.f };
};

//! タイル 1 枚を作り直した物。`full` は 32x32、`coarse` は 2x2 を 1 画素にした 16x16。
struct DcssFxSprite {
    bool valid{ false };
    DcssFxKind kind{ DcssFxKind::Item };
    std::vector<DcssFxPixel> full;
    std::vector<DcssFxPixel> coarse;
    //! 絵の明るい所の平均の色（点光源の色。最大の成分が 1）。
    float light_r{ 1.f };
    float light_g{ 1.f };
    float light_b{ 1.f };
};

//! 画面の右と上（カメラ）。立てる絵をカメラへ向ける。
struct DcssFxView {
    Vec3 right{ 1.f, 0.f, 0.f };
    Vec3 up{ 0.f, 0.f, 1.f };
};

class DcssFx {
public:
    // ---- 寸法・時間（作って見せてから詰める。単体の検査もこの値を見る）----
    //! 絵の 1 辺（マス）。原作のタイルは 1 マスいっぱいに描かれている。
    static constexpr float kPlateSize = 1.0f;
    //! 実物の矢・弾（細長い絵）の拡大率。原作のタイルのままでは立体の地図の上で小さいので、絵を大きくする（形は変えない）。
    static constexpr float kMissileScale = 1.35f;
    //! 寝かせた絵の浮かせる高さ（床から。マス）。
    static constexpr float kFlatHeight = 0.45f;
    //! 立てた絵の中心の高さと、カメラの側へ寄せる量（同じマスの怪物の板は 2 割奥にあるので、その手前に出す）。
    static constexpr float kStandHeight = 0.5f;
    static constexpr float kStandToward = 0.2f;
    //! 同じ絵が止まったまま残っても、初めて届いてからこの秒数で薄れ始め、さらに `kFadeSeconds` かけて消える。
    static constexpr double kLifeSeconds = 0.9;
    static constexpr double kFadeSeconds = 0.35;
    //! コアが絵を消してからも残す時間（点いてから、または消えてから、のどちらか遅いほうから数える。この間に薄れる）。原作の待ちは短いので、見えるうちに消えないようにする。
    static constexpr double kGhostSeconds = 0.35;
    //! 一度に届いた直線の列を、1 マスずつ点ける間隔と、列全体で使う時間の上限（飛ぶ速さは、長い列でも `kFlightSeconds` で着く）。
    static constexpr double kStepSeconds = 0.05;
    static constexpr double kFlightSeconds = 0.45;
    //! 先頭（初めて届いた直後）の明るさの倍率と、尾の明るさの倍率（`kTrailSeconds` かけて移る）。
    static constexpr float kHeadBright = 1.35f;
    static constexpr float kTailBright = 0.6f;
    static constexpr double kTrailSeconds = 0.5;
    //! 1 フレームに足すボクセルの上限（広い爆発でも描画が止まらないように）。これを超える見込みのとき、立てる絵は 16x16 で作る。
    static constexpr std::size_t kVoxelBudget = 16000;
    static constexpr std::size_t kFlashBudget = 600;

    //! `fx_tiles.tsv`・`tiles.tsv`・`main.png` を読む（`dir` は `assets/ui/dcss/atlas`）。
    bool load(const std::string &dir, std::string &err);
    bool loaded() const { return this->loaded_; }

    /*!
     * @brief コアの印が届いたとき（「いまある物のすべて」）。新しく現れた物の時刻を覚え、消えた物は消えた時刻を覚える。
     * @param player_gx・player_gy プレイヤーのマス（無ければ負）。弾の飛ぶ向きの手がかり
     * @param now 単調な秒（`seconds_now()`）
     */
    void update(const std::vector<DcssFxCell> &cells, const std::vector<DcssFlashCell> &flashes, int player_gx, int player_gy, double now);

    /*!
     * @brief 一度に届いた新しいマスの並びのうち、直線の列なら、撃ち手の側から順に点ける遅れ（秒）を返す（単体の検査用に公開）。
     * @param cells 新しく届いたマス（重複なし）
     * @param origin_is_player 撃ったのがプレイヤーか
     * @return `cells` と同じ並びの遅れ。直線の列でなければ全部 0（爆発の輪などは一度に出す）
     */
    static std::vector<double> flight_delays(const std::vector<std::pair<int, int>> &cells, bool origin_is_player, int player_gx, int player_gy);

    /*!
     * @brief 今の絵のボクセルと点光源を `out` に足す。足したボクセルの数を返す。
     * @details 消えて時間のたった物はここで捨てる。
     */
    std::size_t append(FxFrame &out, double now, const DcssFxView &view);

    //! 出す物が（消えかけのものを含めて）あるか。
    bool active() const { return !this->tracked_.empty() || !this->flash_tracked_.empty(); }
    std::size_t tracked_count() const { return this->tracked_.size(); }
    std::size_t flash_tracked_count() const { return this->flash_tracked_.size(); }

    //! 絵の作り直し（検査用。無ければ `valid` が偽）。
    const DcssFxSprite &sprite(int tile);
    //! 閃光の色番号 → 色（0〜255）と既定の濃さ（原作 `view_data.js` の `flash_colours`）。
    static void flash_colour(int colour, int &r, int &g, int &b, int &a);
    //! 単調な秒。
    static double seconds_now();

private:
    struct Row {
        int w{ 0 };
        int h{ 0 };
        int ox{ 0 };
        int oy{ 0 };
        int sx{ 0 };
        int sy{ 0 };
        int ex{ 0 };
        int ey{ 0 };
    };
    struct Tracked {
        double first{ 0.0 }; //!< 初めて届いた時刻
        double reveal{ 0.0 }; //!< 点く時刻（`first` に、飛ぶ道筋の順の遅れを足した値）
        double last{ 0.0 }; //!< 最後に届いていた時刻
        bool alive{ false };
    };

    bool build_sprite(int tile, DcssFxSprite &out) const;

    bool loaded_{ false };
    std::unordered_map<int, Row> rows_; //!< 画像ページ main の切り出し（`tiles.tsv` の `m` の行）
    std::unordered_map<int, DcssFxKind> kinds_; //!< `fx_tiles.tsv`
    FxTileImage page_; //!< `main.png`（CPU の画素）
    std::unordered_map<int, DcssFxSprite> sprites_;
    std::map<std::uint64_t, Tracked> tracked_; //!< (gx, gy, タイル番号)
    std::map<std::uint64_t, Tracked> flash_tracked_; //!< (gx, gy, 色の番号, 濃さ)
};

//! 画面の部品だけの検査（`HengbandHd2d.exe --dcss-fx-check`。窓も GL もコアも要らない）。0 = 全部通った。`dcss_fx_check.cpp`。
int run_dcss_fx_check();

} // namespace hd2d::dcss
