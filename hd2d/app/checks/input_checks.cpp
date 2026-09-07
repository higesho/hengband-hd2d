/*!
 * @file input_checks.cpp
 * @brief 入力と動きの検査（`--pad-check` / `--motion-check`）。窓も GL もコアも要らない。
 *
 * @details `hd2d/app/hd2d_app.cpp` から切り出したもの（2026-09-06）。
 * **遊ぶときには 1 行も通らない**——`run()` の頭で検査のモードを見て、
 * そのまま返る経路にしか無い。分けたのは本体が 17,981 行あって読めなかったため。
 *
 * 宣言は `app/hd2d_checks.h`。振る舞いが変わっていないことは
 * `python tools/hd2d_verify/golden.py --check` で見る。
 */
#include "app/hd2d_checks.h"

#include "ui/game_pad.h"
#include "voxel/part_motion.h"
#include "voxel/prefab.h"
#include "world/prefab_library.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

namespace hd2d {

//! `MotionKind` を人が読む名前へ（画面と記録に出す）。
const char *motion_kind_name(MotionKind kind)
{
    switch (kind) {
    case MotionKind::Rotate:
        return "回転";
    case MotionKind::Pendulum:
        return "振り子";
    case MotionKind::Wind:
        return "風";
    case MotionKind::StateLinked:
        return "状態連動";
    case MotionKind::Blink:
        return "明滅";
    default:
        return "静止";
    }
}

/*!
 * @brief `--pad-check`: 同時押しとマクロのトリガーの検査。**窓も GL もコアも要らない。**
 *
 * @details 実機のコントローラーが無いと手では試せない所を機械で押さえる。
 * とくに `PadChord` は「LB は修飾でもあり単独のコマンドでもある」という**押した瞬間には
 * 決まらない**振る舞いで、間違えると**指を離すたびに階段を上り下りする**。
 */
int run_pad_check(const AppOptions &options)
{
    (void)options;
    int failures = 0;
    const auto fail = [&failures](const char *what) {
        std::fprintf(stderr, "  FAIL %s\n", what);
        ++failures;
    };

    std::fprintf(stderr, "[hd2d] --pad-check: 同時押しとマクロのトリガー\n");

    /* --- 1. トリガーの列。`lib/pref/pref-xxx.prf` の T: 行と対になっている --- */
    {
        const std::vector<int> plain = pad_input_trigger_sequence(PadInput::X);
        const std::vector<int> want_plain{ 31, 'x', 'F', '0', 13 };
        if (plain != want_plain) {
            fail("X の単独トリガーが \x1F x F0 \r になっていない");
        }
        const std::vector<int> ctrl = pad_input_trigger_sequence(PadInput::X, kPadModCtrl);
        const std::vector<int> want_ctrl{ 31, 'C', 'x', 'F', '0', 13 };
        if (ctrl != want_ctrl) {
            fail("LB＋X が control- の列になっていない");
        }
        const std::vector<int> both = pad_input_trigger_sequence(PadInput::X, kPadModCtrl | kPadModAlt);
        const std::vector<int> want_both{ 31, 'C', 'A', 'x', 'F', '0', 13 };
        if (both != want_both) {
            //! 並びは `macro_modifier_chr`（"CSA"）の順。逆にするとコアが読めない。
            fail("LB＋RB＋X が control-alt- の列（C→A の順）になっていない");
        }
        if (!pad_input_trigger_sequence(PadInput::A).empty()) {
            fail("A にトリガーが生えている（決定は固定のはず）");
        }
    }

    /* --- 2. 状態機械。LB は修飾でもあり単独のコマンドでもある --- */
    {
        PadChord chord;
        int mods = 0;
        PadInput solo = PadInput::Count;

        //! (a) LB を押して離しただけ → **単独押しとして出る**（階段を上れないと困る）
        if (!chord.press(PadInput::LeftShoulder, mods)) {
            fail("LB の押しが修飾として飲まれていない");
        }
        if (!chord.release(PadInput::LeftShoulder, solo) || (solo != PadInput::LeftShoulder)) {
            fail("LB を単独で押して離したのに発火しない");
        }

        //! (b) LB を押しながら X → 同時押し。**LB を離しても単独発火しない**
        chord.forget();
        (void)chord.press(PadInput::LeftShoulder, mods);
        mods = 0;
        if (chord.press(PadInput::X, mods)) {
            fail("X が修飾として飲まれた");
        }
        if (mods != kPadModCtrl) {
            fail("LB を押しながらの X に control- が付いていない");
        }
        if (chord.release(PadInput::LeftShoulder, solo)) {
            fail("同時押しに使った LB が離しで単独発火した（階段を上ってしまう）");
        }

        //! (c) LB＋RB → 2 段の層。**どちらを離しても単独発火しない**
        chord.forget();
        (void)chord.press(PadInput::LeftShoulder, mods);
        (void)chord.press(PadInput::RightShoulder, mods);
        mods = 0;
        (void)chord.press(PadInput::Y, mods);
        if (mods != (kPadModCtrl | kPadModAlt)) {
            fail("LB＋RB＋Y に control-alt- が付いていない");
        }
        if (chord.release(PadInput::LeftShoulder, solo) || chord.release(PadInput::RightShoulder, solo)) {
            fail("層に使った LB／RB が離しで単独発火した");
        }

        //! (d) LB＋RB を押して**何も叩かずに**離した → どちらも単独発火しない
        chord.forget();
        (void)chord.press(PadInput::LeftShoulder, mods);
        (void)chord.press(PadInput::RightShoulder, mods);
        if (chord.release(PadInput::RightShoulder, solo) || chord.release(PadInput::LeftShoulder, solo)) {
            fail("修飾を重ねただけで離したのに単独発火した");
        }

        //! (e) 修飾でないボタンの離しは何も出さない（押した瞬間に出しているので二重になる）
        chord.forget();
        mods = 0;
        (void)chord.press(PadInput::X, mods);
        if (chord.release(PadInput::X, solo)) {
            fail("X の離しで二重に発火した");
        }
    }

    /* --- 3. 層つきの割り当てと cfg の往復（2026-08-14 に決めた） --- */
    {
        PadBinds binds;
        binds.command[0][static_cast<int>(PadInput::X)] = 23;
        binds.command[kPadModCtrl][static_cast<int>(PadInput::Y)] = 45;
        binds.command[kPadModCtrl | kPadModAlt][static_cast<int>(PadInput::Start)] = 67;

        /*
         * **綴りは丸ごと突き合わせる。**部分一致で見ていたら、層 0 にも `+` を付ける
         * という壊し方を素通しした（`find("X:23")` は `+X:23` にも当たる。
         * 2026-08-14、検査の検査で判明）。前の版の cfg と同じ形であることが要点なので、
         * 一致すべきものは一致で見る。
         */
        const std::string line = binds.to_line();
        if (line != "X:23,C+Y:45,CA+Start:67") {
            fail("cfg: pad_bind の綴りが期待と違う（層 0 は無印・層つきは C+ / CA+）");
            std::fprintf(stderr, "       出たもの: %s\n", line.c_str());
        }

        PadBinds round;
        round.parse(line);
        for (int mods = 0; mods < kPadModLayerCount; ++mods) {
            for (int i = 0; i < kPadInputCount; ++i) {
                if (round.command[mods][i] != binds.command[mods][i]) {
                    fail("cfg: 書いて読み直したら中身が変わった");
                    mods = kPadModLayerCount;
                    break;
                }
            }
        }

        //! **前の版が書いた cfg がそのまま読めること**（`+` の無い綴りは層 0）。
        PadBinds old_style;
        old_style.parse("X:23,LT:9");
        if ((old_style.command[0][static_cast<int>(PadInput::X)] != 23)
            || (old_style.command[0][static_cast<int>(PadInput::LeftTrigger)] != 9)) {
            fail("cfg: 前の版の pad_bind= が読めない");
        }
    }

    /* --- 4. 検査の検査。**わざと壊した入力に反応すること**（設計書 §14-4） --- */
    {
        PadChord chord;
        int mods = 0;
        (void)chord.press(PadInput::LeftShoulder, mods);
        if (chord.mask() != kPadModCtrl) {
            fail("LB を押している間の mask が control- になっていない");
        }
        chord.forget();
        if (chord.mask() != 0) {
            fail("forget() のあとも修飾が残っている");
        }
    }

    if (failures == 0) {
        std::fprintf(stderr, "  PASS すべて通りました\n");
    }
    return (failures == 0) ? 0 : 1;
}

/*!
 * @brief `--motion-check`: 動き（P6）の検査。**窓も GL も要らない。**
 *
 * @details 設計書 §8.2 は「落とすと必ず破綻するもの」を 3 つ挙げている。そのうち
 *
 * | § | 内容 | どこで守るか |
 * |---|---|---|
 * | 8.2-1 | 影のパスにも同じ変形 | **構造で守る。**頂点シェーダが 1 本しかない（`voxel_renderer.cpp`） |
 * | 8.2-2 | **当たり判定は動かさない** | **ここ。**時刻を変えても footprint と grounded が変わらないこと |
 * | 8.2-3 | 大きな剛体を頂点シェーダで曲げない | **ここ。**風で曲がるのは `motion.kind == Wind` のパーツだけ |
 *
 * 8.2-1 を検査ではなく構造で守るのは、「2 本のシェーダが一致しているか」を機械で見るのが
 * 難しいからである。**一致させる必要が無い作りにすれば、検査そのものが要らない。**
 */
int run_motion_check(const AppOptions &options)
{
    int failures = 0;
    int checked_prefabs = 0;

    /*
     * 手元にあるプレハブを全部通す。動くものが 1 つも無ければ検査になっていない。
     * P10 から目録は**ライブラリ（対応表）から取る**。町の素材（shop / mill / sign）はまだ表に
     * 載っていないので、従来の 6 個と**和**を取る（ライブラリが読めなくても 6 個では回る）。
     */
    std::set<std::string> name_set = { "shop", "tree", "rock", "mill", "grass", "sign" };
    {
        PrefabLibrary library;
        std::string lib_err;
        if (library.load(options.voxel_dir, lib_err)) {
            for (int i = 0; i < library.count(); ++i) {
                name_set.insert(library.entry(i).name);
            }
        } else {
            std::fprintf(stderr, "  (ライブラリが読めないので従来の 6 個だけ回します: %s)\n", lib_err.c_str());
        }
    }
    int moving_parts = 0;

    for (const std::string &name_string : name_set) {
        const char *const name = name_string.c_str();
        Prefab prefab;
        std::string err;
        if (!load_prefab(options.voxel_dir, name, prefab, err)) {
            // 無い素材は飛ばす（P10 で増える）。**読めたのに壊れている場合は下で落ちる。**
            std::fprintf(stderr, "  (skip) %s: %s\n", name, err.c_str());
            continue;
        }
        ++checked_prefabs;

        /*
         * (1) **当たり判定は動かさない**（§8.2-2）。
         * 動きの行列は見た目だけのもので、`footprint` にも `grounded` にも触ってはならない。
         * 時刻と扉の状態を大きく振って、両方が 1 ビットも変わらないことを見る。
         */
        const auto footprint_before = prefab.footprint;
        std::vector<bool> grounded_before;
        for (const auto &part : prefab.parts) {
            grounded_before.push_back(part.grounded);
        }

        std::vector<Mat4> motions;
        bool moved_here = false;
        for (const float t : { 0.f, 0.37f, 1.0f, 3.3f, 60.0f }) {
            MotionContext ctx;
            ctx.time = t;
            ctx.states.emplace_back("door", (t > 1.f) ? 1.f : 0.f);
            compose_part_motions(prefab, ctx, motions);
            if (motions.size() != prefab.parts.size()) {
                std::fprintf(stderr, "  FAIL %s: 動きの行列の数が合わない (%zu != %zu)\n",
                    name, motions.size(), prefab.parts.size());
                ++failures;
                break;
            }
            for (std::size_t i = 0; i < motions.size(); ++i) {
                // 単位行列でなければ「動いた」。
                for (int k = 0; k < 16; ++k) {
                    const float identity = ((k % 5) == 0) ? 1.f : 0.f;
                    if (std::abs(motions[i].m[k] - identity) > 1e-5f) {
                        moved_here = true;
                        break;
                    }
                }
            }
        }
        if (moved_here) {
            ++moving_parts;
        }

        bool untouched = (prefab.footprint == footprint_before);
        for (std::size_t i = 0; (i < prefab.parts.size()) && (i < grounded_before.size()); ++i) {
            untouched = untouched && (prefab.parts[i].grounded == grounded_before[i]);
        }
        if (!untouched) {
            std::fprintf(stderr, "  FAIL %s: **当たり判定が動いた**（§8.2-2）\n", name);
            ++failures;
        }

        /*
         * (2) 静止パーツは時刻を変えても単位行列のまま（§8.2-3 の裏返し）。
         * ここが落ちるのは「全部のパーツを動かしてしまっている」ときで、
         * 見た目では建物が丸ごと揺れるので気づけるが、検査でも捕まえておく。
         */
        MotionContext late;
        late.time = 12.5f;
        std::vector<Mat4> late_motions;
        compose_part_motions(prefab, late, late_motions);
        for (std::size_t i = 0; i < prefab.parts.size(); ++i) {
            const bool rigid_still = (prefab.parts[i].motion.kind == MotionKind::Static)
                || (prefab.parts[i].motion.kind == MotionKind::Wind);
            if (!rigid_still || (prefab.parts[i].parent >= 0)) {
                continue; // 親が動けば子も動くのが正しい
            }
            for (int k = 0; k < 16; ++k) {
                const float identity = ((k % 5) == 0) ? 1.f : 0.f;
                if (std::abs(late_motions[i].m[k] - identity) > 1e-5f) {
                    std::fprintf(stderr, "  FAIL %s/%s: 静止（または風）のパーツが剛体として動いた\n",
                        name, prefab.parts[i].name.c_str());
                    ++failures;
                    break;
                }
            }
        }

        std::string parts_desc;
        for (const auto &part : prefab.parts) {
            if (!parts_desc.empty()) {
                parts_desc += " ";
            }
            parts_desc += part.name + ":" + motion_kind_name(part.motion.kind);
        }
        /*
         * 「剛体として」動いたかを出す。**風のパーツはここでは「静止」と出るのが正しい**
         * （曲げているのは頂点シェーダで、CPU 側の行列は単位行列のまま）。
         * ただ「静止」とだけ書くと風が効いていないように読めるので、そう書かない。
         */
        const char *const verdict = moved_here ? "剛体が動く" : "剛体は静止";
        std::fprintf(stderr, "  %-6s parts=%zu footprint=%zu  %s  %s\n", name, prefab.parts.size(),
            prefab.footprint.size(), verdict, parts_desc.c_str());
    }

    /*
     * (3) **検査の検査**（設計書 §14-4）。
     * ここまでの (1)(2) は「何も動かない」実装でも全部通ってしまう。動くものが 1 つも
     * 無ければ、この検査は何も見ていない。
     */
    std::fprintf(stderr, "  (3) 検査の検査: 実際に動いたプレハブ %d 個 / 読めた %d 個\n",
        moving_parts, checked_prefabs);
    if (moving_parts == 0) {
        std::fprintf(stderr, "  FAIL **動くプレハブが 1 つも無い。この検査は何も見ていない**\n");
        ++failures;
    }

    /*
     * (4) 動きが**時刻だけで決まる**こと。同じ時刻を 2 回引いて一致しなければ、
     * どこかに状態が residual として残っている（決定性が無いと絵がざわつく。§9.3 と同じ理由）。
     */
    {
        Prefab prefab;
        std::string err;
        if (load_prefab(options.voxel_dir, "mill", prefab, err)) {
            std::vector<Mat4> a;
            std::vector<Mat4> b;
            MotionContext ctx;
            ctx.time = 2.75f;
            compose_part_motions(prefab, ctx, a);
            MotionContext other;
            other.time = 9.0f;
            std::vector<Mat4> scratch;
            compose_part_motions(prefab, other, scratch); // 別の時刻を挟む
            compose_part_motions(prefab, ctx, b);
            int mismatches = 0;
            for (std::size_t i = 0; i < a.size(); ++i) {
                for (int k = 0; k < 16; ++k) {
                    if (a[i].m[k] != b[i].m[k]) {
                        ++mismatches;
                    }
                }
            }
            std::fprintf(stderr, "  (4) 同じ時刻なら同じ姿勢: mismatches=%d\n", mismatches);
            failures += (mismatches != 0) ? 1 : 0;
        }
    }

    std::fprintf(stderr, "[hd2d] RESULT: %s\n", (failures == 0) ? "PASS" : "FAIL");
    return (failures == 0) ? 0 : 1;
}

} // namespace hd2d
