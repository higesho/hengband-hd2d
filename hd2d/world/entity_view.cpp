/*!
 * @file entity_view.cpp
 * @brief `entity_view.h` の実装。
 */
#include "world/entity_view.h"

#include "render/term_colors.h" //!< コアの 16 色（アスキー実体の色）

#include "frame/cell_feature_bits.h"
#include "frame/game_frame.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace hd2d {

namespace {

//! これより遠くへ跳んだら補間せず吸着する（マス）。
constexpr float kSnapDistance = 2.5f;
//! 1 秒あたりどれだけ目標へ寄るか。大きいほどきびきび動く。
constexpr float kFollowRate = 14.f;
//! プレイヤを追う鍵。`monster_slot` は 1 以上なので 0 は空いている。
constexpr std::uint32_t kPlayerKey = 0;
//! アイテムの鍵はマス座標から作る（個体番号が無いので動かない前提）。
std::uint32_t object_key(int gx, int gy)
{
    return 0x80000000u | (static_cast<std::uint32_t>(gy & 0xFFFF) << 8) | static_cast<std::uint32_t>(gx & 0xFF);
}

/*!
 * @brief 実体を `at` へ向けるヨー（ラジアン）。0 = 南向き（＝ +y を向く）。
 *
 * @details z 軸まわりに θ 回すと、板の面の法線 (0,1) は **(-sinθ, cosθ)** へ移る。
 * それを「実体 → 向く先」の向きに合わせたいので θ = atan2(-dx, dy) になる。
 * **符号を落とすと左右が反転する**（板は裏返っても輪郭が同じなので、実機で
 * 気づきにくい種類の間違いである）。
 */
float facing_yaw(float from_x, float from_y, const SlabFacing &facing)
{
    if (!facing.active) {
        //! 見下ろし・ジオラマ。**視点回転ぶんだけ**回して正対を保つ（`SlabFacing::fixed_yaw`）。
        return facing.fixed_yaw;
    }
    const float dx = facing.at_x - from_x;
    const float dy = facing.at_y - from_y;
    if (((dx * dx) + (dy * dy)) < 1e-8f) {
        return facing.fixed_yaw; // 真上に重なっている（＝自分自身）。向く先が決まらない
    }
    float yaw = std::atan2(-dx, dy);
    if (facing.snap8) {
        //! **8 方向へ吸着**。45 度刻みに丸める（決めたことで選択式）。
        constexpr float kStep = 3.14159265f / 4.f;
        yaw = std::round(yaw / kStep) * kStep;
    }
    return yaw;
}

/* ======================================================= 足元のリング（SQ-1） */

/*!
 * @name 輪の寸法と色
 * @details **どれも見せ方の決め**であって、コアの数字ではない（必守制約 1）。
 * 色は `TermPalette` の番号で引くので、`&` や pref で色表を変えればここも従う。
 * @{
 */
constexpr float kRingRadius = 0.42f; //!< マスの中に収まる大きさ（1 マスの 84%）
constexpr float kRingLift = 0.02f; //!< 床から浮かせる量（z 争いよけ）
//! 照準の輪は**少し大きく**して、警戒度の輪と重なっても両方読めるようにする。
constexpr float kTargetRadius = 0.48f;
/*!
 * @brief 照準の輪の濃さ。**警戒度より濃い。**
 * @details あちらは常に出ている状態の表示だが、こちらは遊ぶ人が自分で決めた 1 つのマスで、
 * しかも同じマスに警戒度の輪と重なって出る。同じ薄さにするとどちらがどちらか読めない。
 */
constexpr float kTargetAlpha = 0.8f;
/*! @} */

/*!
 * @name 警戒度の輪の明滅（2026-08-22 に決めた）
 *
 * > この輪を明滅させよう。警戒が低い時はゆっくり。警戒度が上がるほどに早く明るく
 *
 * @details 段ごとに**周期**と**濃さの幅**を変える。速さと明るさの両方が上がるので、
 * 画の端で見ても「あの敵はこちらに気づいている」が読める——色を見分けるより、
 * 動きの速さのほうが目の端で拾いやすい。
 *
 * **消え切らせない**（下限を残す）。完全に消えると、そこに敵が居ることまで
 * 見失う瞬間ができる。明滅は**強さの表現**であって、隠す仕掛けではない。
 *
 * **位相は揃える**（個体ごとにずらさない）。同じ段の輪が揃って脈打つので、
 * 「速い輪が 3 つ」＝気づかれている敵が 3 匹、と一目で数えられる。
 * ずらすと賑やかになるだけで、数が読めなくなる。
 *
 * 前の版（濃さ 0.45 で静止）は「敵の足元の円は半透明にして」の指示によるもので、
 * ここの下限・上限はその濃さを挟むように取ってある。
 * @{
 */
struct RingPulse {
    float period; //!< 1 明滅の秒数。**短いほど切迫している**
    float low; //!< いちばん薄いときの濃さ（**0 にしない**）
    float high; //!< いちばん濃いとき
};

//! 眠り → 非警戒 → 警戒 の順に**速く・明るく**なる。
constexpr RingPulse kPulseAsleep{ 2.6f, 0.16f, 0.38f };
constexpr RingPulse kPulseUnwary{ 1.5f, 0.22f, 0.55f };
constexpr RingPulse kPulseAlert{ 0.65f, 0.34f, 0.90f };

//! 段ごとの明滅の型。分からない段は薄いほうへ倒す。
const RingPulse &pulse_of(std::uint8_t level)
{
    switch (level) {
    case static_cast<std::uint8_t>(MonsterAlert::Alert):
        return kPulseAlert;
    case static_cast<std::uint8_t>(MonsterAlert::Unwary):
        return kPulseUnwary;
    default:
        return kPulseAsleep;
    }
}

/*!
 * @brief いまの濃さ。@param seconds 場面が始まってからの秒数（`EntityTracker::elapsed_`）
 * @details 余弦で往復する。**方形波にしない**——ぱちぱち点滅すると、この画では
 * 目障りなだけで段の違いも読めない。0 から始めて滑らかに上がる形にしてある。
 */
float ring_alpha_now(std::uint8_t level, float seconds)
{
    const RingPulse &pulse = pulse_of(level);
    constexpr float kTau = 6.28318531f;
    const float phase = (pulse.period > 0.01f) ? (seconds / pulse.period) : 0.f;
    const float wave = 0.5f - (0.5f * std::cos(phase * kTau));
    return pulse.low + ((pulse.high - pulse.low) * wave);
}
/*! @} */

/*!
 * @brief 警戒度 → コアの 16 色の番号。
 * @details 眠り＝青・非警戒＝水色は**コアの `get_alertness_text()` の色と同じ**にした
 * （状態列の字と輪が食い違わない）。気づかれている段だけは赤にしてある——
 * あちらは士気で白／紫に分けるが、輪で言いたいのは「気づかれた」という 1 事だけである。
 */
std::uint8_t ring_color_of(std::uint8_t level)
{
    switch (level) {
    case static_cast<std::uint8_t>(MonsterAlert::Asleep):
        return 6; //!< TERM_BLUE
    case static_cast<std::uint8_t>(MonsterAlert::Unwary):
        return 14; //!< TERM_L_BLUE
    case static_cast<std::uint8_t>(MonsterAlert::Alert):
        return 4; //!< TERM_RED
    default:
        return 0;
    }
}

//! 色番号・半径・濃さから輪を 1 つ作る。
GroundRingInstance make_ring(float x, float y, float radius, std::uint8_t color_index, float alpha)
{
    const RgbColor rgb = term_color_to_rgb(color_index);
    GroundRingInstance ring;
    ring.x = x;
    ring.y = y;
    ring.z = kRingLift;
    ring.radius = radius;
    ring.r = static_cast<float>(rgb.r) / 255.f;
    ring.g = static_cast<float>(rgb.g) / 255.f;
    ring.b = static_cast<float>(rgb.b) / 255.f;
    ring.a = alpha;
    return ring;
}

/*!
 * @brief そのマスの敵の警戒度を引く。@return `MonsterAlert::Unknown` なら輪を出さない
 * @details 一覧は見えている敵ぶんしか無い（数体）ので素直に走査する。
 * 表を組むほうが高くつく。
 */
std::uint8_t alert_at(const GameFrame &frame, int gx, int gy)
{
    for (const MonsterAlertCell &cell : frame.monster_alerts) {
        if ((cell.gx == gx) && (cell.gy == gy)) {
            return cell.level;
        }
    }
    return static_cast<std::uint8_t>(MonsterAlert::Unknown);
}

} // namespace

void EntityTracker::reset()
{
    this->tracks_.clear();
}

EntityTracker::Track &EntityTracker::advance(std::uint32_t key, float target_x, float target_y, float dt,
    float min_speed)
{
    auto found = this->tracks_.find(key);
    if (found == this->tracks_.end()) {
        Track fresh;
        fresh.x = target_x;
        fresh.y = target_y;
        fresh.seen = this->stamp_;
        return this->tracks_.emplace(key, fresh).first->second;
    }
    Track &track = found->second;
    track.seen = this->stamp_;

    const float dx = target_x - track.x;
    const float dy = target_y - track.y;
    if (((dx * dx) + (dy * dy)) > (kSnapDistance * kSnapDistance)) {
        // 遠くへ跳んだ＝転移か、枠の使い回しで別の個体になった。**流し撮りにしない。**
        track.x = target_x;
        track.y = target_y;
        return track;
    }
    const float t = std::clamp(dt * kFollowRate, 0.f, 1.f);
    float step_x = dx * t;
    float step_y = dy * t;
    /*
     * **等速**（2026-08-14 に決めた。ここは 1 度読み違えた）。
     *
     * > スムース移動というのは一歩ごとに xxms 待つではなく、**1 歩を xxms かけて
     * > スムーズにスクロール**し、それが連続になることで、一旦待つことなく、
     * > すーっと移動していくこと
     *
     * 指数の追随は目標に近づくほど遅くなるので、**1 歩の中に速い所と遅い所**ができる。
     * 最低速度の床を敷くだけでは足りない（速い所はそのまま残る）——`min_speed` が
     * 入っているときは指数を通さず、1 歩ぶんの距離を一定の速さで渡り切る。
     * 次の一歩が同じ速さで続くので、押している間は途切れない。
     */
    if (min_speed > 0.f) {
        const float distance = std::sqrt((dx * dx) + (dy * dy));
        const float want = min_speed * dt;
        const float k = (distance > 1e-5f) ? (std::min(want, distance) / distance) : 0.f;
        step_x = dx * k;
        step_y = dy * k;
    }
    track.x += step_x;
    track.y += step_y;
    return track;
}

void EntityTracker::build(const GameFrame &frame, const TileCatalog &catalog, SlabLibrary &slabs,
    VoxelRenderer &renderer, float dt, EntityView &out, bool hide_player, float player_min_speed,
    const SlabFacing &facing, GlyphAtlas *glyph_atlas, float glyph_height_cells,
    GlyphAtlas *fallback_glyphs)
{
    //! **アスキー実体**。字が焼けないなら板へ落ちる。
    const bool ascii = (glyph_atlas != nullptr) && glyph_atlas->ready();
    //! 板の道の**字の受け皿**。無ければ従来どおり。
    GlyphAtlas *const fallback
        = (!ascii && (fallback_glyphs != nullptr) && fallback_glyphs->ready()) ? fallback_glyphs : nullptr;
    out.glyphs.clear();
    out.slabs.clear();
    out.rings.clear();
    out.monsters = 0;
    out.objects = 0;
    out.players = 0;
    out.missing_tiles = 0;
    out.player_valid = false;
    ++this->stamp_;
    /*
     * 明滅の時計（SQ-1。`ring_alpha_now`）。**フレーム数ではなく秒で持つ**——
     * 数で持つと、重い場面で明滅までゆっくりになる。
     */
    this->elapsed_ += (dt > 0.f) ? dt : 0.f;

    for (const auto &cell : frame.cells) {
        if ((cell.feature_flags & CELL_FEAT_KNOWN) == 0u) {
            continue;
        }
        const bool is_player = (cell.feature_flags & CELL_FEAT_PLAYER) != 0u;
        const bool is_monster = cell.monster_id != 0;
        const bool is_object = cell.object_id != 0;
        if (!is_player && !is_monster && !is_object) {
            continue;
        }
        /*
         * 絵を用意する。**アスキーでは板の目録を引かない**——字はコアが送ってきた
         * `ascii_fallback` だけで描けるので、タイルの無い実体（従来は
         * `missing_tiles` に数えて消していたもの）もちゃんと出る。
         */
        const GlyphAtlas::Entry *glyph = nullptr;
        int slab = -1;
        if (ascii) {
            glyph = glyph_atlas->entry_for(cell.ascii_fallback);
            if (glyph == nullptr) {
                ++out.missing_tiles; //!< 空白や焼けなかった字。**捏造しない**
                continue;
            }
        } else {
            /*
             * 板の道。目録に無い・板を作れない実体は、**受け皿があれば字の板へ落とす**
             * Frox の M0 は
             * 実体の絵を 1 枚も持たない（706 体が M2）ので、これが無いと
             * 敵も品も @ もまるごと消える。受け皿が無ければ従来どおり
             * `missing_tiles` に数えて何も描かない（既存 4 本の絵は変えない）。
             */
            const TileEntry *entry = (cell.tile_index != 0) ? catalog.find(cell.tile_index) : nullptr;
            if (entry != nullptr) {
                slab = slabs.acquire(cell.tile_index, entry->path, renderer);
            }
            if (slab < 0) {
                /*
                 * **字の板へ落ちたことを 1 度だけ言う**（2026-08-25。気づいたこと
                 * 「再開したら狼が C になった」）。ここは**黙って落ちる道**だった——
                 * 受け皿の字が引ければ `missing_tiles` にも数えないので、
                 * 画面には「C」が出るだけで、なぜ板が出ないのかはどこにも残らない。
                 *
                 * 落ちる理由は 2 つあり、**言い分けないと切り分けられない**:
                 *
                 * - **索引 0**（`cell.tile_index == 0`）… コアが「絵は無い」と言っている。
                 *   Sil-Q なら `resolve_tile_index()` の `lookup_monster()` が外れた側で、
                 *   目録（`SR<r_idx>.png` の走査）に届いていないということである。
                 * - **索引はあるが板が無い**… 目録に無い索引か、`SlabLibrary` が読めなかった
                 *   （そちらは `slab_library.cpp` が理由まで出す）。
                 *
                 * 同じ実体で毎フレーム言うと流れてしまうので、**索引と字の組で 1 度だけ**。
                 */
                static std::set<std::uint32_t> warned;
                const auto warn_key = (static_cast<std::uint32_t>(cell.tile_index) << 8)
                    | static_cast<std::uint32_t>(static_cast<unsigned char>(cell.ascii_fallback));
                if (warned.insert(warn_key).second) {
                    std::fprintf(stderr,
                        "[hd2d] 板ではなく字で出します: '%c'（索引 %u%s・%s）\n",
                        (cell.ascii_fallback >= 0x20) ? cell.ascii_fallback : '?',
                        static_cast<unsigned>(cell.tile_index),
                        (cell.tile_index == 0) ? "＝コアが絵を持っていない" : "",
                        is_monster ? "敵" : (is_player ? "@" : "品"));
                }
                if (fallback != nullptr) {
                    glyph = fallback->entry_for(cell.ascii_fallback);
                }
                if (glyph == nullptr) {
                    ++out.missing_tiles; //!< 字も焼けなかった。**捏造しない**
                    continue;
                }
            }
        }

        // 個体を追う鍵。**モンスターは `monster_slot`**（種族番号では個体を追えない）。
        std::uint32_t key = 0;
        float width = 1.f;
        float height = 1.f;
        if (is_player) {
            key = kPlayerKey;
            ++out.players;
        } else if (is_monster) {
            key = (cell.monster_slot != 0) ? static_cast<std::uint32_t>(cell.monster_slot)
                                           : object_key(cell.gx, cell.gy);
            ++out.monsters;
        } else {
            key = object_key(cell.gx, cell.gy);
            width = 0.7f;
            height = 0.7f;
            ++out.objects;
        }

        //! 最低速度は**プレイヤだけ**（歩調に合わせる。モンスターは従来どおり）。
        const Track &track = this->advance(key, static_cast<float>(cell.gx) + 0.5f,
            static_cast<float>(cell.gy) + 0.5f, dt, is_player ? player_min_speed : 0.f);

        if (is_player) {
            //! カメラが追う先（`MoveSmoothing::All`）。**ビルボードと同じ値**にすること。
            out.player_valid = true;
            out.player_x = track.x;
            out.player_y = track.y;
        }

        /*
         * **足元のリング**（SQ-1）。**なめらかな位置（`track`）に置く**——マスの中心に
         * 置くと、動いている敵の輪だけが取り残されて別の場所に浮く。
         *
         * @note ここは**絵を用意できた実体だけ**を通る（目録に無い板・焼けなかった字は
         * 上の `continue` で落ちている）。絵の無い敵には輪も出ない。
         * Sil-Q は M2 で 319 枚焼いてあり、アスキー実体なら字は必ず在るので、
         * いまのところ当たらない道である。
         */
        if (is_monster) {
            const std::uint8_t level = alert_at(frame, cell.gx, cell.gy);
            if (level != static_cast<std::uint8_t>(MonsterAlert::Unknown)) {
                out.rings.push_back(make_ring(track.x, track.y, kRingRadius, ring_color_of(level),
                    ring_alpha_now(level, this->elapsed_)));
            }
        }

        if (is_player && hide_player) {
            // 一人称。カメラが本人の目の位置に居るので絵は出さない（出すと視界を塞ぐ）。
            // 位置の追随（上の `player_x/y`）は済ませてあるので、カメラはなめらかなまま。
            continue;
        }

        /*
         * 字の板で出す実体。アスキー実体（`ascii`）のほか、**板の道の受け皿**
         * （`fallback` で字が引けた実体。上で `glyph` が立っている）もここへ来る
         * `ascii` の旗ではなく **`glyph` で
         * 分ける**——旗で分けると受け皿の実体が `slab < 0` のまま板の枝へ落ちる。
         */
        if (glyph != nullptr) {
            /*
             * **文字の板**。
             *
             * `BillboardInstance` の位置は**足元**（板はそこから上へ立つ）なので、
             * マスの中心へ置いて少しだけ浮かせる。浮かせるのは床の板との z 争いを避けるため
             * ——地面と同じ高さだと足元が縞に割れる。
             *
             * 幅は**作った字の縦横比から出す**（等幅フォントでも `i` と `M` で違う）。
             * 高さを決め打ちにして幅を比で追わせると、どの字も同じ背丈で並ぶ。
             *
             * **すべて仮の値**である（設計書 §5.2-5・§14-3。実物を見て詰める）。
             */
            //! 字の背丈（マス）。機能メニューで回せる（`Hd2dSettings::entity_glyph_pct`）。
            const float kGlyphHeight = (glyph_height_cells > 0.01f) ? glyph_height_cells : 0.8f;
            constexpr float kGlyphLift = 0.05f; //!< 床から浮かせる量（マス）
            //! アイテムは少し小さく（板のときの 0.7 と同じ意図——実体の格が読める）。
            const float scale = is_player ? 1.f : (is_monster ? 1.f : 0.8f);
            const float height_cells = kGlyphHeight * scale;
            const float aspect = (glyph->h > 0) ? (static_cast<float>(glyph->w) / static_cast<float>(glyph->h)) : 0.6f;
            const RgbColor rgb = term_color_to_rgb(cell.fg_color);
            BillboardInstance board;
            board.x = track.x;
            board.y = track.y;
            board.z = kGlyphLift;
            board.height = height_cells;
            board.width = height_cells * aspect;
            board.u0 = glyph->u0;
            board.v0 = glyph->v0;
            board.u1 = glyph->u1;
            board.v1 = glyph->v1;
            //! 色は**コアの 16 色そのまま**（`TermPalette`。`&` や pref で変えた色も届く）。
            board.r = static_cast<float>(rgb.r) / 255.f;
            board.g = static_cast<float>(rgb.g) / 255.f;
            board.b = static_cast<float>(rgb.b) / 255.f;
            out.glyphs.push_back(board);
            continue;
        }

        /*
         * 板を置く。**プレハブの原点はマスの角**（`terrain_view` が `inst.x = fx` と
         * 置いているのと同じ約束）なので、実体の中心から半マス引いて据える。
         * 縮めるとき（アイテムの 0.7）も**中心を保つ**ように引く量を変える——
         * `x - 0.5` のままだと、小さい物が北西へずれて置かれる。
         */
        SlabInstance slot;
        slot.slab = slab;
        slot.cx = track.x;
        slot.cy = track.y;
        slot.height = height;
        slot.inst.x = track.x - (width * 0.5f);
        slot.inst.y = track.y - (width * 0.5f);
        slot.inst.z = 0.f; //!< 足元は床の上（板は `--trim-bottom` で焼いてあるので接地する）
        slot.inst.sx = width;
        slot.inst.sy = width;
        slot.inst.sz = height;
        //! 一人称のときだけキャラへ正対させる（見下ろしは南向きのまま）。
        slot.inst.yaw = facing_yaw(track.x, track.y, facing);
        out.slabs.push_back(slot);
    }

    /*
     * **照準の輪**（SQ-2 の 8 系統目）。`hilite_target` はコアでは Term のカーソルを
     * 動かすだけなので、`map_overlay`（`print_rel` の差分）には 1 度も出てこない。
     * 位置がフレームに直に載っているので、ここで輪にする。
     *
     * **マスの中心へ置く**（なめらかにしない）——照準はマスを指すものであって、
     * 個体に貼りついているわけではない。敵が動けば次のフレームでマスごと動く。
     */
    if ((frame.target_gx >= 0) && (frame.target_gy >= 0)) {
        out.rings.push_back(make_ring(static_cast<float>(frame.target_gx) + 0.5f,
            static_cast<float>(frame.target_gy) + 0.5f, kTargetRadius, 11 /* TERM_YELLOW */, kTargetAlpha));
    }

    // 見えなくなった個体の記憶を捨てる（フロアを歩き回ると際限なく溜まる）。
    for (auto it = this->tracks_.begin(); it != this->tracks_.end();) {
        it = ((this->stamp_ - it->second.seen) > 240) ? this->tracks_.erase(it) : std::next(it);
    }
}

} // namespace hd2d
