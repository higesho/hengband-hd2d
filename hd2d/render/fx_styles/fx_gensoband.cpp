/*!
 * @file fx_gensoband.cpp
 * @brief 幻想蛮怒（Gensoband）にあって変愚に無い属性の見た目の表（`gb_` で始まる名前。41 個）。
 *
 * 属性の名前（コア名 → 見た目の名前）と、既存の見た目へ寄せる属性は `presentation/bridge/attack_fx_core_attr_aliases.h`。
 * 原作は `gensoband/src/defines.h` の `GF_xxx`、色は `gensoband/lib/pref/spell-xx.prf`。見た目は東方 Project の元ネタの技から決めた。
 * 実装は `fx_gensoband_a.cpp`（前半）・`fx_gensoband_b.cpp`（後半）。共通の枠は `fx_look_kit.h`、模様は `fx_look_motifs.h`。
 *
 * | 属性 | 元ネタ | 見た目 |
 * |---|---|---|
 * | `gb_tsukumo_storm` | 付喪神の嵐（雷・水・竜巻・轟音の合奏） | 傘・太鼓・音符が渦を巻く。視界全体は 4 色の波と、撃った者の周りを回る道具 |
 * | `gb_yukari_storm` | 紫の嵐（閃光と暗黒の市松） | スキマ（目のある裂け目）が開く。視界全体は白と紫の市松の波 |
 * | `gb_kinkakuji` | 金閣寺の一枚天井 | 金の屋根が落ちて押しつぶす。視界全体は撃った者の 7 マス四方が金に光ってほどける |
 * | `gb_tornado` | 竜巻 | 緑の渦と木の葉 |
 * | `gb_windcutter` | 風の刃 | 緑の三日月が三枚、体を切り裂く |
 * | `gb_banki_beam`・`gb_banki_beam2` | 赤蛮奇（ろくろ首） | 首が伸びて頭が噛みつく。二つ目は二つの頭と追う弾 |
 * | `gb_seiran_beam` | 清蘭「鷹は撃ち抜いた」 | 青い光線に乗った鷹が射抜く |
 * | `gb_rainbow` | 虹・プリズム | 七色の虹の橋。ビームは七色の帯 |
 * | `gb_lunatic_torch` | クラウンピースの松明 | 紅と紫の狂気の炎と、回る星 |
 * | `gb_redeye` | 鈴仙の赤眼催眠 | 赤い目が見つめ、波紋が広がり、姿が二重にぶれる |
 * | `gb_kyuttoshitedoka_n` | フランドール「きゅっとしてドカーン」 | 球が一点へ縮み、色とりどりの欠片になって弾ける |
 * | `gb_ryuuri` | 小野塚小町「生魂流離」 | 青いひとだまが昇る。ビームはひとだまの列 |
 * | `gb_slow_twine` | 足絡め | 縄が脚を螺旋に締めつける |
 * | `gb_possession` | 憑依 | 幽霊が上から入り込み、体が二重にぶれる |
 * | `gb_soulsteal` | 魂奪い | 赤いひとだまが抜けて撃った者へ飛ぶ |
 * | `gb_nightmare` | 悪夢 | 三日月・暗い雲・眠りの Z |
 * | `gb_train` | 幽霊列車 | 緑に光る列車が走り抜ける |
 * | `gb_northern` | レティの「ノーザンウィナー」 | 雪の結晶が降り、氷の柱が伸びる |
 * | `gb_n_e_p` | 因果律の修正 | 白い光の柱と、走る欠損の帯。画面が白く飛ぶ |
 * | `gb_houtou` | 宝塔（毘沙門天） | 金の宝塔から光が降りる。ビームは金の太い光線 |
 * | `gb_satori` | さとり「テリブルスーヴニール」 | 第三の目、脈打つハートが割れて散る |
 * | `gb_kokoro` | こころの面 | 赤・白・青の面が回って、一つが顔に張りつく |
 * | `gb_moses` | 早苗「海が割れる日」 | 水の壁が左右にそびえ、中に道ができる |
 * | `gb_redmagic` | レミリアの紅い地獄の劫火 | 紅い業火が噴き上がり、蝙蝠が飛ぶ |
 * | `gb_gungnir` | レミリアのグングニル | 紅い槍が降ってくる |
 * | `gb_throw_plate` | 皿投げ | 回る皿が割れて破片が飛ぶ |
 * | `gb_blazingstar` | 魔理沙のブレイジングスター | 星が開いて星屑が四方へ飛ぶ。玉は星の彗星 |
 * | `gb_soulsculpture` | 妖夢のソウルスカルプチュア | 三日月の刃が体のまわりを回る |
 * | `gb_yaku` | 雛の厄 | 黒赤の帯が螺旋を描いて巻きつく |
 * | `gb_special_shot` | 銃の特殊射撃 | 細く明るい曳光弾と、命中の火花 |
 * | `gb_youmu` | 妖夢「六根清浄斬」 | 六本の斬撃が交差する |
 * | `gb_brain_finger_print` | さとり「ブレインフィンガープリント」 | 緑の指紋の渦を、走査線がなぞる |
 * | `gb_kanameishi` | 要石 | 注連縄を巻いた岩が落ちる |
 * | `gb_steam` | 蒸気 | 白い湯気と熱い水滴 |
 * | `gb_super_ego` | 超自我 | 後光と、押さえつける青い輪 |
 * | `gb_dig_oil` | 石油 | 黒い石油が噴き上がって床に広がる |
 * | `gb_alcohol` | 酒 | 盃から琥珀色の雫、泡、ほろ酔いのハート |
 * | `gb_pit_fall` | 落とし穴 | 床に穴が開き、体が沈む |
 * | `gb_berserk` | 狂戦士化 | 赤い気と怒りの印、蒸気 |
 * | `gb_activ_trap` | 罠の発動 | 仕掛け板が沈んで火花が散り、「！」が跳ねる |
 *
 * **原作より情報を増やさない**（効いたか・耐えたかで絵を変えない）。
 */
#include "render/fx_styles/fx_gensoband_looks.h"

namespace hd2d {

namespace {

using namespace looks;
using namespace looks::gb;

constexpr FxColour kWhite{ 1.f, 1.f, 1.f };

const Entry kEntries[] = {
    // attr, base, smoke, emissive, tint, tint2, look, seconds, level, flight, beam, beam_seconds, allsight, allsight_seconds, flags
    { "gb_tsukumo_storm", { 0.7f, 0.5f, 1.f }, 0.15f, 1.2f, { 0.4f, 0.8f, 1.f }, kWhite, look_tsukumo_storm, 1.4f, 0, nullptr, nullptr, 0.f, all_tsukumo_storm, 1.25f, kNone },
    { "gb_yukari_storm", { 0.7f, 0.2f, 0.9f }, 0.15f, 1.2f, { 0.95f, 0.25f, 0.8f }, { 0.25f, 0.05f, 0.4f }, look_yukari_storm, 1.35f, 0, nullptr, nullptr, 0.f, all_yukari_storm, 1.3f, kNone },
    { "gb_kinkakuji", { 1.f, 0.8f, 0.25f }, 0.1f, 1.3f, { 1.f, 0.95f, 0.6f }, kWhite, look_kinkakuji, 1.6f, 0, nullptr, nullptr, 0.f, all_kinkakuji, 1.45f, kNone },
    { "gb_tornado", { 0.35f, 0.85f, 0.4f }, 0.15f, 1.2f, { 0.85f, 1.f, 0.85f }, kWhite, look_tornado, 1.5f, 0, flight_tornado, nullptr, 0.f, nullptr, 0.f, kNone },
    { "gb_windcutter", { 0.45f, 1.f, 0.55f }, 0.1f, 1.2f, { 0.9f, 1.f, 0.9f }, kWhite, look_windcutter, 1.0f, 0, flight_windcutter, nullptr, 0.f, nullptr, 0.f, kNone },
    { "gb_banki_beam", { 1.f, 0.9f, 0.5f }, 0.1f, 1.2f, kWhite, { 0.9f, 0.1f, 0.12f }, look_banki_beam, 1.1f, 0, flight_banki_beam, beam_banki_beam, 0.9f, nullptr, 0.f, kNoBeamLine },
    { "gb_banki_beam2", { 1.f, 0.9f, 0.5f }, 0.1f, 1.2f, kWhite, { 0.9f, 0.1f, 0.12f }, look_banki_beam2, 1.1f, 1, flight_banki_beam, beam_banki_beam2, 0.9f, nullptr, 0.f, kNoBeamLine },
    { "gb_seiran_beam", { 0.45f, 0.7f, 1.f }, 0.1f, 1.3f, { 0.8f, 0.92f, 1.f }, kWhite, look_seiran_beam, 1.2f, 0, flight_seiran_beam, beam_seiran_beam, 0.7f, nullptr, 0.f, kNoBeamLine },
    { "gb_rainbow", { 1.f, 0.9f, 0.7f }, 0.05f, 1.3f, kWhite, kWhite, look_rainbow, 1.4f, 0, flight_rainbow, beam_rainbow, 0.9f, all_rainbow, 1.25f, kNoBeamLine },
    { "gb_lunatic_torch", { 1.f, 0.3f, 0.4f }, 0.3f, 1.2f, { 0.7f, 0.2f, 0.95f }, kWhite, look_lunatic_torch, 1.2f, 0, flight_lunatic_torch, nullptr, 0.f, nullptr, 0.f, kNone },
    { "gb_redeye", { 1.f, 0.2f, 0.2f }, 0.1f, 1.2f, { 1.f, 0.6f, 0.5f }, kWhite, look_redeye, 1.4f, 0, nullptr, nullptr, 0.f, nullptr, 0.f, kNone },
    { "gb_kyuttoshitedoka_n", { 1.f, 0.5f, 0.7f }, 0.1f, 1.3f, { 0.8f, 0.7f, 1.f }, kWhite, look_kyuttoshitedoka_n, 1.2f, 0, flight_kyuttoshitedoka_n, nullptr, 0.f, nullptr, 0.f, kNone },
    { "gb_ryuuri", { 0.35f, 0.55f, 1.f }, 0.2f, 1.2f, { 0.7f, 0.9f, 1.f }, kWhite, look_ryuuri, 1.5f, 0, nullptr, beam_ryuuri, 1.0f, nullptr, 0.f, kNoBeamLine },
    { "gb_slow_twine", { 0.5f, 0.5f, 0.2f }, 0.2f, 0.8f, { 0.3f, 0.65f, 0.2f }, kWhite, look_slow_twine, 1.5f, 0, nullptr, nullptr, 0.f, nullptr, 0.f, kNone },
    { "gb_possession", { 1.f, 0.5f, 0.55f }, 0.2f, 1.2f, { 1.f, 0.85f, 0.9f }, kWhite, look_possession, 1.5f, 0, nullptr, nullptr, 0.f, nullptr, 0.f, kNone },
    { "gb_soulsteal", { 1.f, 0.3f, 0.25f }, 0.2f, 1.2f, { 1.f, 0.6f, 0.5f }, kWhite, look_soulsteal, 1.4f, 0, nullptr, nullptr, 0.f, nullptr, 0.f, kNone },
    { "gb_nightmare", { 0.3f, 0.35f, 0.9f }, 0.3f, 1.0f, { 0.6f, 0.7f, 1.f }, kWhite, look_nightmare, 1.7f, 0, nullptr, nullptr, 0.f, nullptr, 0.f, kNone },
    { "gb_train", { 0.4f, 1.f, 0.6f }, 0.3f, 1.1f, { 0.8f, 1.f, 0.85f }, kWhite, look_train, 1.3f, 0, flight_train, beam_train, 1.0f, nullptr, 0.f, kNoBeamLine },
    { "gb_northern", { 0.8f, 0.92f, 1.f }, 0.2f, 1.1f, kWhite, kWhite, look_northern, 1.7f, 0, nullptr, nullptr, 0.f, nullptr, 0.f, kNone },
    { "gb_n_e_p", { 1.f, 1.f, 1.f }, 0.0f, 1.4f, { 0.6f, 1.f, 1.f }, kWhite, look_n_e_p, 1.2f, 0, nullptr, nullptr, 0.f, all_n_e_p, 0.8f, kNone },
    { "gb_houtou", { 1.f, 0.85f, 0.3f }, 0.05f, 1.3f, { 1.f, 0.95f, 0.6f }, kWhite, look_houtou, 1.3f, 0, nullptr, beam_houtou, 0.9f, nullptr, 0.f, kNoBeamLine },
    { "gb_satori", { 0.85f, 0.4f, 1.f }, 0.1f, 1.1f, { 1.f, 0.5f, 0.8f }, kWhite, look_satori, 1.7f, 0, nullptr, nullptr, 0.f, nullptr, 0.f, kNone },
    { "gb_kokoro", { 1.f, 0.7f, 0.6f }, 0.1f, 1.1f, { 1.f, 1.f, 0.9f }, kWhite, look_kokoro, 1.5f, 0, nullptr, nullptr, 0.f, nullptr, 0.f, kNone },
    { "gb_moses", { 0.3f, 0.6f, 1.f }, 0.1f, 1.1f, { 0.7f, 0.9f, 1.f }, kWhite, look_moses, 1.8f, 0, nullptr, beam_moses, 1.3f, nullptr, 0.f, kNoBeamLine },
    { "gb_redmagic", { 1.f, 0.2f, 0.2f }, 0.4f, 1.2f, { 0.8f, 0.05f, 0.12f }, kWhite, look_redmagic, 1.6f, 0, nullptr, nullptr, 0.f, nullptr, 0.f, kNone },
    { "gb_gungnir", { 1.f, 0.2f, 0.25f }, 0.1f, 1.3f, { 1.f, 0.8f, 0.8f }, kWhite, look_gungnir, 1.4f, 0, flight_gungnir, nullptr, 0.f, nullptr, 0.f, kNone },
    { "gb_throw_plate", { 0.35f, 0.8f, 0.45f }, 0.1f, 1.0f, { 1.f, 1.f, 0.95f }, kWhite, look_throw_plate, 1.3f, 0, flight_throw_plate, nullptr, 0.f, nullptr, 0.f, kNone },
    { "gb_blazingstar", { 1.f, 0.95f, 0.3f }, 0.1f, 1.4f, { 0.35f, 0.6f, 1.f }, { 0.4f, 1.f, 0.5f }, look_blazingstar, 1.2f, 0, flight_blazingstar, nullptr, 0.f, nullptr, 0.f, kNone },
    { "gb_soulsculpture", { 1.f, 0.3f, 0.3f }, 0.1f, 1.2f, { 1.f, 0.9f, 0.9f }, kWhite, look_soulsculpture, 1.1f, 0, nullptr, nullptr, 0.f, nullptr, 0.f, kNone },
    { "gb_yaku", { 0.6f, 0.08f, 0.25f }, 0.3f, 1.0f, { 1.f, 0.25f, 0.45f }, kWhite, look_yaku, 1.6f, 0, nullptr, nullptr, 0.f, nullptr, 0.f, kNone },
    { "gb_special_shot", { 1.f, 1.f, 0.9f }, 0.1f, 1.3f, { 1.f, 0.7f, 0.3f }, kWhite, look_special_shot, 0.9f, 0, flight_special_shot, beam_special_shot, 0.4f, nullptr, 0.f, kNoBeamLine },
    { "gb_youmu", { 0.55f, 0.7f, 1.f }, 0.1f, 1.3f, { 1.f, 0.35f, 0.35f }, kWhite, look_youmu, 1.1f, 0, nullptr, nullptr, 0.f, nullptr, 0.f, kNone },
    { "gb_brain_finger_print", { 0.4f, 1.f, 0.5f }, 0.1f, 1.2f, { 0.8f, 1.f, 0.8f }, kWhite, look_brain_finger_print, 1.4f, 0, nullptr, nullptr, 0.f, nullptr, 0.f, kNone },
    { "gb_kanameishi", { 0.6f, 0.58f, 0.55f }, 0.5f, 0.9f, { 0.95f, 0.95f, 0.9f }, kWhite, look_kanameishi, 1.7f, 0, nullptr, nullptr, 0.f, nullptr, 0.f, kNone },
    { "gb_steam", { 1.f, 1.f, 1.f }, 0.5f, 1.1f, { 0.75f, 0.85f, 0.95f }, kWhite, look_steam, 1.7f, 0, flight_steam, nullptr, 0.f, nullptr, 0.f, kNone },
    { "gb_super_ego", { 0.4f, 0.6f, 1.f }, 0.1f, 1.2f, { 1.f, 0.95f, 0.6f }, kWhite, look_super_ego, 1.4f, 0, nullptr, nullptr, 0.f, nullptr, 0.f, kNone },
    { "gb_dig_oil", { 0.15f, 0.1f, 0.05f }, 0.5f, 0.8f, { 0.8f, 0.5f, 0.15f }, kWhite, look_dig_oil, 1.9f, 0, nullptr, nullptr, 0.f, nullptr, 0.f, kNone },
    { "gb_alcohol", { 1.f, 0.8f, 0.4f }, 0.1f, 1.1f, { 1.f, 0.55f, 0.55f }, kWhite, look_alcohol, 1.6f, 0, nullptr, nullptr, 0.f, nullptr, 0.f, kNone },
    { "gb_pit_fall", { 0.5f, 0.38f, 0.25f }, 0.5f, 0.8f, { 0.35f, 0.28f, 0.2f }, kWhite, look_pit_fall, 1.5f, 0, nullptr, nullptr, 0.f, nullptr, 0.f, kNone },
    { "gb_berserk", { 1.f, 0.2f, 0.15f }, 0.2f, 1.2f, { 1.f, 0.6f, 0.25f }, kWhite, look_berserk, 1.3f, 0, nullptr, nullptr, 0.f, nullptr, 0.f, kNone },
    { "gb_activ_trap", { 1.f, 0.85f, 0.3f }, 0.1f, 1.2f, { 1.f, 1.f, 0.7f }, kWhite, look_activ_trap, 0.9f, 0, nullptr, nullptr, 0.f, nullptr, 0.f, kNone },
};

} // namespace

void fx_register_gensoband(FxStyleRegistry &registry)
{
    looks::register_entries(registry, kEntries, sizeof(kEntries) / sizeof(kEntries[0]));
}

} // namespace hd2d
