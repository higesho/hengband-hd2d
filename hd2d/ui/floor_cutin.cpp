/*!
 * @file floor_cutin.cpp
 * @brief 階が変わったときのカットイン演出（P10。基準は `floor_cutin.h`）。
 */
#include "ui/floor_cutin.h"

#include "i18n/lang.h"
#include "world/floor_meaning.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace hd2d {
namespace {

/*!
 * @name 段の長さ（秒）
 * @details **入りは出より長い。**入りは「読ませる」ための動きなので、目が字を追いきる
 * 時間が要る。出は既に読み終わっているので、待たせないほうがよい。
 * @{
 */
constexpr float kInSeconds = 0.42f;
constexpr float kOutSeconds = 0.30f;
constexpr float kRevealSeconds = 0.55f;
/*! @} */

/*!
 * @brief 行先の地図を待つ（`Pending`）上限（秒）。**保険である。**
 * @details 普通は `-more-` を 1 回越えれば着く（＝プレイヤがキーを押すまで）ので、
 * ここへ届くことはない。届いたときは「階は変わったのに地図が来ない」＝こちらの
 * 読み違いなので、**幕を下ろして元の絵へ戻す**。黒いままにするより害が小さい。
 */
constexpr float kPendingMaxSeconds = 8.f;

/*!
 * @brief 字の高さ＝**マップ縦幅の 1/6**（2026-08-11 に決めた）。
 * @details 板は 2 枚（地名・階層）あるので、2 枚合わせても縦の 1/3 に収まる。
 */
constexpr float kPlateHeightRatio = 1.f / 6.f;

/*!
 * @brief 字の大きさの上限（画素）。
 * @details `TextOverlay` のアトラスは 1024×1024 の 1 枚である。1 字が 200px を超えると
 * 5×5 = 25 字しか焼けず、長い地名で字が抜ける。1/6 が効くのは縦 1200px までで、
 * それより大きい画面では**頭打ちにする**（比を守って字を落とすより、読めるほうが大事）。
 */
constexpr int kMaxFontPx = 200;
constexpr int kMinFontPx = 24;

//! 入りは減速して止まる（`1-(1-t)^3`）。
float ease_out(float t)
{
    const float u = 1.f - std::clamp(t, 0.f, 1.f);
    return 1.f - (u * u * u);
}

//! 出は加速して抜ける（`t^3`）。
float ease_in(float t)
{
    const float u = std::clamp(t, 0.f, 1.f);
    return u * u * u;
}

//! 幕の色。**真っ黒**（決めたことの「黒画面」そのまま）。
PaintColor veil_color(float alpha)
{
    return PaintColor{ 0.f, 0.f, 0.f, std::clamp(alpha, 0.f, 1.f) };
}

/*!
 * @brief **同じ「層」か**（2026-08-11 に決めた:「層を移動する際のみにカットインで」
 *   「建物から出る場合のカットインはいらない」）。
 *
 * @details `world/floor_meaning.h` の `same_floor()` とは**わざと別物**である。
 * あちらは `generated_turn` まで見る「同じ実体か」で、見た目の種を引くのに要る
 * ——作り直された階は別物として描かねばならない。
 *
 * こちらが答えたいのは「**行き先が変わったか**」である。店・建物から出ると地上の階は
 * 作り直されて `generated_turn` が変わるが、**プレイヤは同じ町の同じ場所に戻ってきている**。
 * そこで演出を出すと、買い物のたびに黒幕と決定キー待ちが挟まる。
 *
 * 見るのは 3 つだけ:
 * | | 変わる場面 |
 * |---|---|
 * | `dun_level` | 階段の上り下り（**これが「層の移動」そのもの**） |
 * | `dungeon_id` | 別のダンジョンへ入る |
 * | `kind` | 地上 ⇔ 地下・クエスト階・闘技場 |
 */
bool same_place(const FloorIdentity &a, const FloorIdentity &b)
{
    return (a.dungeon_id == b.dungeon_id) && (a.dun_level == b.dun_level) && (a.kind == b.kind);
}

/*!
 * @brief **その階の地図そのもの**の印（FNV-1a 64）。
 *
 * @details 「行先へ着いたか」を判じるのに要る。`generated_turn` だけでは足りない
 * ——**4 コアのうち 3 本は 0 のまま送る**（`frox/adapter/fc_frame.cpp` /
 * `silq/adapter/sq_frame.cpp` / `gensoband/adapter/gb_frame.cpp` の
 * `fill_floor_and_lighting()`。どれも「階に『作られた turn』を持つ欄が無い」）。
 * `generated_turn` だけを見ていた版は、**その 3 本でカットインが 1 度も出なかった**
 * ——`last_floor_` を更新する条件も同じ式なので、一度階を移ると以後ずっと
 * 「まだ着いていない」に貼り付く（2026-08-28 に読んで気づいた）。
 *
 * **実体の印は床へ均してから混ぜる**（`rebuild_floor_meaning()` と同じ規則）。
 * 均さないと敵が 1 歩あるくだけで印が変わり、`-more-` の待ちの 1 枚を
 * 「もう着いた」と読んでしまう。
 */
std::uint64_t map_signature(const MinimapSnapshot &minimap)
{
    if (!minimap.valid()) {
        return 0;
    }
    std::uint64_t hash = 14695981039346656037ull;
    const auto mix = [&hash](std::uint64_t v) {
        hash ^= v;
        hash *= 1099511628211ull;
    };
    mix(static_cast<std::uint64_t>(static_cast<std::uint32_t>(minimap.width)));
    mix(static_cast<std::uint64_t>(static_cast<std::uint32_t>(minimap.height)));
    for (const std::uint8_t kind : minimap.kinds) {
        std::uint8_t canon = kind;
        switch (static_cast<MinimapKind>(kind)) {
        case MinimapKind::Monster:
        case MinimapKind::Item:
        case MinimapKind::Player:
            canon = static_cast<std::uint8_t>(MinimapKind::Floor);
            break;
        default:
            break;
        }
        mix(canon);
    }
    //! 0 は「地図が無い」の意味に使うので、当たったら 1 へ倒す。
    return (hash == 0) ? 1ull : hash;
}

} // namespace

bool FloorCutin::init(std::string &err)
{
    if (!this->paint_.init(err)) {
        return false;
    }
    this->ready_ = true;
    return true;
}

void FloorCutin::shutdown()
{
    this->place_.image.unload();
    this->level_.image.unload();
    if (this->font_px_ > 0) {
        this->text_.shutdown();
        this->font_px_ = 0;
    }
    this->paint_.shutdown();
    this->ready_ = false;
}

void FloorCutin::ensure_font(const UiLayout &layout)
{
    const int want = std::clamp(
        static_cast<int>(std::lround(static_cast<float>(layout.scene.h) * kPlateHeightRatio)),
        kMinFontPx, kMaxFontPx);
    if (want == this->font_px_) {
        return;
    }
    if (this->font_px_ > 0) {
        this->text_.shutdown();
        this->font_px_ = 0;
    }
    std::string err;
    if (!this->text_.init(want, err)) {
        std::fprintf(stderr, i18n::tr("hd2d.ui.floor-cutin.hd2d-could-not-prepare-the-cut-in-text"), err.c_str());
        return;
    }
    this->font_px_ = want;
}

void FloorCutin::load_plate(CutinPlate &plate, const std::string &text, const std::string &art_key)
{
    plate.image.unload();
    plate.text = text;
    plate.art_key = art_key;
    if (art_key.empty()) {
        return;
    }
    /*
     * **無くて当たり前**（いまは 1 枚も置いていない）。読めなければ字へ落ちるだけなので、
     * 失敗を画面にも記録にも出さない——出すと起動のたびに嘘の警告が並ぶ。
     */
    std::string err;
    (void)plate.image.load("assets/ui/cutin/" + art_key + ".png", err);
}

void FloorCutin::begin(const GameFrame &frame, const UiLayout &layout)
{
    this->ensure_font(layout);

    /*
     * 地名の絵の鍵は**数字**で作る（`floor_cutin.h` の表）。
     * 地下はダンジョンの番号、町は町の番号、それ以外（荒野・クエスト階）は種別。
     */
    std::string place_key;
    if (frame.floor.dungeon_id > 0) {
        place_key = "place_d" + std::to_string(frame.floor.dungeon_id);
    } else if (frame.floor.town_id > 0) {
        place_key = "place_t" + std::to_string(frame.floor.town_id);
    } else {
        place_key = "place_k" + std::to_string(frame.floor.kind);
    }
    this->load_plate(this->place_, frame.floor.place_name_utf8, place_key);

    /*
     * 階層。**地上（階層が無い所）は地名だけ**（2026-08-11 に決めた）。
     * 「階が 0」ではなく「地上か」で決める——闘技場やクエスト階でも `dun_level` は
     * 立っているので、数字の有無で分けると地上だけを外せない。
     */
    const bool underground = frame.floor.kind != static_cast<int>(FloorKind::Surface);
    if (underground && (frame.floor.dun_level > 0)) {
        //! 語順が言語で変わる（「2 層」/「Level 2」）ので、数字はカタログの書式で埋める。
        char level_text[32];
        std::snprintf(level_text, sizeof(level_text), i18n::tr("hd2d.ui.floor-cutin.level-d"),
            frame.floor.dun_level);
        this->load_plate(this->level_, level_text,
            "level_" + std::to_string(frame.floor.dun_level));
    } else {
        this->load_plate(this->level_, std::string(), std::string());
    }

    this->phase_ = CutinPhase::In;
    this->elapsed_ = 0.f;
}

void FloorCutin::update(const GameFrame &frame, float dt, const UiLayout &layout, bool map_shown)
{
    if (!this->ready_) {
        return;
    }

    /*
     * ---- 始める条件 ----
     * (0) **ゲームが始まっていること**（`pre_game_menu` が偽 ＝ キャラクターが出来ていて
     *     生きている）
     * (1) 地図が出ていること（タイトル・店・メニューの最中には割り込まない）
     * (2) **全体マップではないこと**（決めたこと）
     * (3) **層が変わったこと**（`same_place`。作り直しただけの同じ場所では出さない
     *     ——2026-08-11 に決めた「建物から出る場合のカットインはいらない」）
     *
     * ## (0) を落としていて実機を止めた（2026-08-11）
     * `frame_shows_map()` は **`menu_term_lines` が空か**しか見ていない。ところが
     * **行 0 のプロンプトは Term ミラーに出ない**（icky にならないので。`sdl2_verify` の
     * README にも書いてある）——つまり**キャラクター作成の能力値の画面と名前の入力**は
     * 「地図が出ている」と読まれる。そこへキャラクターが出来る前の（まだ何も入っていない）
     * フロアの素性が変わりながら流れてくるので、**演出が立ち上がって決定キーを食っていた**
     * （2026-08-11 に気づいた:「新規で開始する際、名前選択の手前で決定ができない」）。
     *
     * 「地図が出ているか」だけでは足りない。**ゲームが始まっているか**を別に見る。
     *
     * 「前のフロア」も (0) が立っている間だけ覚える。作成中の空のフロアを覚えると、
     * 最初の階が「変わった」に見えて、始めた瞬間に演出が入る。
     */
    const bool in_game = !frame.pre_game_menu;
    const bool showable = in_game && map_shown && !frame.floor.wild_mode
        && ((frame.floor.kind == static_cast<int>(FloorKind::Surface))
            || (frame.floor.kind == static_cast<int>(FloorKind::Dungeon)));
    const bool moved = this->has_last_floor_ && !same_place(this->last_floor_, frame.floor);
    /*
     * ---- **地図が行き先のものになるまで待つ**（2026-08-21 に気づいた）----
     *
     * コアは階段を下りた直後に「**階だけ新しく・地図は元の階のまま**」のフレームを
     * 1 枚送ってくる。`leave_floor()` が `dun_level` を進めた後、`change_floor()` が
     * 新しい階を作る**前**に `-more-` の待ちが入るからである
     * （`src/core/game-play.cpp` の `process_game_turn`: `process_dungeon` →
     * `handle_stuff` → `change_floor` の間）。
     *
     * 実測（`HengbandCore.exe` をプロトコルで直に叩いて採った。2026-08-21）:
     * | | `dun_level` | `generated_turn` | 地図 |
     * |---|---|---|---|
     * | 下りる前 | 1 | 6463 | イークの洞穴 1 階 |
     * | **`-more-` の待ち** | **3** | **6463** | **1 階のまま** |
     * | キーを送った後 | 3 | 17861 | 3 階（既知 532 マス） |
     *
     * 真ん中の 1 枚で演出を始めると、幕が上がったときに**元の階の地図**が出る
     * （＝「行先でない画面が表示される」）。階が作り直された印は `generated_turn` なので、
     * **これが変わっていない場所替わり**は「まだ着いていない」と見て**丸ごと無視する**
     * ——覚えもしない。覚えてしまうと、続く本物の到着フレームが
     * 「同じ場所」に見えて演出が 1 度も出なくなる。
     */
    /*
     * **着いたか**は 2 つのどちらかで見る:
     *
     * | 印 | どのコアで効くか |
     * |---|---|
     * | `generated_turn` が変わった | 変愚蛮怒（唯一これを送る） |
     * | **地図そのものが入れ替わった** | 残り 3 本（`generated_turn` は 0 のまま） |
     *
     * `-more-` の待ちの 1 枚は**階だけ新しく地図は元のまま**なので、
     * どちらの印も動かない＝「まだ着いていない」と読める。
     */
    const std::uint64_t map_sig = map_signature(frame.minimap);
    const bool arrived = moved
        && ((this->last_floor_.generated_turn != frame.floor.generated_turn)
            || ((map_sig != 0) && (map_sig != this->last_map_sig_)));
    const bool not_arrived_yet = moved && !arrived;
    const bool changed = arrived;
    /*
     * **最初の 1 フレームでは出さない。**起動直後は「前の階」を知らないので、
     * ここで出すと読み込んだ瞬間に必ず演出が入る。階が変わったときだけ出したい。
     */
    if (in_game && map_shown && !not_arrived_yet) {
        this->last_floor_ = frame.floor;
        this->last_map_sig_ = map_sig;
        this->has_last_floor_ = true;
    }
    if (!in_game) {
        //! 作成中・死んだ後は**覚えたことも捨てる**（次に始めたとき「変わった」にしない）。
        this->has_last_floor_ = false;
    }
    /*
     * ---- ⓪ **階を離れた時点で幕を張る**（2026-08-28 に決めた。全コア共通）----
     *
     * 上の `not_arrived_yet` の 1 枚は `-more-` の待ちで、**プレイヤがキーを押すまで
     * 画面に残る**。演出をここで始めてはいけない（幕が上がったとき元の階が出る）が、
     * **元の階の絵を見せ続けるのも違う**——気づいたことは
     * 「階段を使った際にカットインが入るとき、元のフロアの画像が表示される」である。
     *
     * そこで `Pending` を挟む。幕だけ張って、行先の地図が来たら `begin()` へ進む。
     * **入力は止めない**（`blocks_input()` は偽のまま）——`-more-` を越えるキーが
     * コアへ届かないと、黒い画面のまま先へ進めなくなる。
     */
    if (not_arrived_yet && showable && (this->phase_ == CutinPhase::Idle)) {
        this->phase_ = CutinPhase::Pending;
        this->elapsed_ = 0.f;
        this->pending_seconds_ = 0.f;
    }
    /*
     * 行先が来た。`Pending` からでも `Idle` からでも同じ入口で始める
     * ——`begin()` が読むのは**いまのフレーム**（＝行先の階）だけである。
     */
    if (changed && showable
        && ((this->phase_ == CutinPhase::Idle) || (this->phase_ == CutinPhase::Pending))) {
        this->begin(frame, layout);
    }

    if (this->phase_ == CutinPhase::Idle) {
        return;
    }
    /*
     * 地図が消えた（店に入った・死んだ）ら**畳む**。幕を張ったまま別の画面へ行くと、
     * 決定キーを待っている幕が Term の写しの上に残って何も見えなくなる。
     */
    if (!map_shown || !in_game) {
        this->phase_ = CutinPhase::Idle;
        this->elapsed_ = 0.f;
        return;
    }

    this->ensure_font(layout);
    this->elapsed_ += std::max(0.f, dt);
    switch (this->phase_) {
    case CutinPhase::Pending:
        /*
         * 時間では進まない（行先の地図が来たら上で `begin()` に入る）。
         * **頭打ちだけ見る**——「階は変わったのに地図が来ない」で黒いまま固まらせない。
         */
        this->pending_seconds_ += std::max(0.f, dt);
        if (this->pending_seconds_ >= kPendingMaxSeconds) {
            std::fprintf(stderr, "[hd2d] %s\n",
                i18n::tr("hd2d.ui.floor-cutin.pending-timed-out"));
            this->phase_ = CutinPhase::Idle;
            this->elapsed_ = 0.f;
            this->pending_seconds_ = 0.f;
        }
        break;
    case CutinPhase::In:
        if (this->elapsed_ >= kInSeconds) {
            this->phase_ = CutinPhase::Wait;
            this->elapsed_ = 0.f;
        }
        break;
    case CutinPhase::Wait:
        break; //!< キーを待つ（時間では進まない）
    case CutinPhase::Out:
        if (this->elapsed_ >= kOutSeconds) {
            this->phase_ = CutinPhase::Reveal;
            this->elapsed_ = 0.f;
        }
        break;
    case CutinPhase::Reveal:
        if (this->elapsed_ >= kRevealSeconds) {
            this->phase_ = CutinPhase::Idle;
            this->elapsed_ = 0.f;
        }
        break;
    case CutinPhase::Idle:
    default:
        break;
    }
}

bool FloorCutin::confirm()
{
    /*
     * **`Pending` では受けない。** あそこで押されるキーは `-more-` を越えるための
     * もので、コアへ流さねばならない（`blocks_input()` も偽にしてある）。
     */
    if ((this->phase_ != CutinPhase::Wait) && (this->phase_ != CutinPhase::In)) {
        return false;
    }
    this->phase_ = CutinPhase::Out;
    this->elapsed_ = 0.f;
    return true;
}

void FloorCutin::draw(const UiLayout &layout, int screen_w, int screen_h, const UiImagePainter &images)
{
    if (!this->ready_ || (this->phase_ == CutinPhase::Idle) || layout.scene.empty()) {
        return;
    }

    /*
     * 幕は**地図の矩形だけ**を覆う。画面ぜんぶを黒くすると HP もメッセージも消えるので、
     * 「地図が入れ替わった」という話が「ゲームが止まった」に見える。
     * 覆う濃さは `Reveal` の間だけ薄れていく（＝マップが浮かび上がる）。
     */
    const float veil = (this->phase_ == CutinPhase::Reveal)
        ? (1.f - std::clamp(this->elapsed_ / kRevealSeconds, 0.f, 1.f))
        : 1.f;
    this->paint_.begin(screen_w, screen_h);
    this->paint_.rect(layout.scene, veil_color(veil));
    this->paint_.flush();
    if (this->phase_ == CutinPhase::Reveal) {
        return; //!< 板はもう抜けている
    }
    if (this->phase_ == CutinPhase::Pending) {
        //! 幕だけ。**地名も階層もまだ出さない**——行先がどこかを知らないからである。
        return;
    }

    /*
     * ---- 板の位置 ----
     * 進み具合 `t` は 0（画面の外）→ 1（中央）。`Out` では 1 → 0 の逆走で、
     * **入ってきた側と同じ側へ抜ける**（地名は左へ、階層は右へ）。
     */
    float t = 1.f;
    if (this->phase_ == CutinPhase::In) {
        t = ease_out(this->elapsed_ / kInSeconds);
    } else if (this->phase_ == CutinPhase::Out) {
        t = 1.f - ease_in(this->elapsed_ / kOutSeconds);
    }

    const int plate_h = (this->font_px_ > 0) ? this->text_.cell_h()
                                            : static_cast<int>(layout.scene.h * kPlateHeightRatio);
    const int gap = plate_h / 5;
    const bool two = !this->level_.empty();
    const int total_h = two ? ((plate_h * 2) + gap) : plate_h;
    const int top = layout.scene.y + ((layout.scene.h - total_h) / 2);
    const int centre_x = layout.scene.x + (layout.scene.w / 2);

    const bool text_ready = this->font_px_ > 0;
    if (text_ready) {
        this->text_.begin(screen_w, screen_h);
    }

    /*!
     * @brief 板 1 枚を置く。
     * @param from_left 左から入ってくるか（地名 = 真 / 階層 = 偽）。
     */
    const auto place_plate = [&](const CutinPlate &plate, int y, bool from_left) {
        if (plate.empty()) {
            return;
        }
        //! 絵の幅は原寸、字の幅は測る。**どちらでも同じ式で置ける**ようにしておく。
        int w = 0;
        int h = plate_h;
        if (plate.image.valid()) {
            //! 高さを板の高さへ合わせ、横は縦横比で決める。
            h = plate_h;
            w = (plate.image.height() > 0)
                ? static_cast<int>(std::lround(static_cast<double>(plate.image.width()) * h / plate.image.height()))
                : plate_h;
        } else if (text_ready) {
            w = this->text_.measure(plate.text);
        } else {
            return; //!< 字も絵も出せない（フォントが開けなかった）
        }

        //! 画面の外（`t=0`）から中央（`t=1`）へ。外は**板の幅ぶん**さらに向こうへ置く。
        const int centred = centre_x - (w / 2);
        const int outside = from_left ? (layout.scene.x - w) : (layout.scene.x + layout.scene.w);
        const int x = outside + static_cast<int>(std::lround((centred - outside) * static_cast<double>(t)));

        if (plate.image.valid()) {
            images.draw_crop(plate.image, RectPx{ x, y, w, h }, 0, 0, plate.image.width(), plate.image.height(), 1.f);
        } else {
            this->text_.draw(x, y, plate.text, TextColor{ 1.f, 0.97f, 0.88f, 1.f });
        }
    };

    place_plate(this->place_, top, true);
    if (two) {
        place_plate(this->level_, top + plate_h + gap, false);
    }
    if (text_ready) {
        this->text_.flush();
    }
}

} // namespace hd2d
