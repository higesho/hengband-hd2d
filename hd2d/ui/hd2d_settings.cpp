/*!
 * @file hd2d_settings.cpp
 * @brief `hd2d_settings.h` の実装。
 */
#include "ui/hd2d_settings.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace hd2d {

/*!
 * @brief 1 行動あたりの秒数。
 * @details **`SdlUiOptions::realtime_seconds_per_turn` と同じ表**にしてあること。
 * 片方だけ直すと、同じ添字で違う速さになる。
 */
float Hd2dSettings::realtime_seconds_per_turn(int index)
{
    static constexpr float table[kRealtimeSpeedCount] = {
        0.2f, 0.3f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f
    };
    return table[std::clamp(index, 0, kRealtimeSpeedCount - 1)];
}

const char *Hd2dSettings::realtime_self_span_label(int index)
{
    static constexpr const char *labels[kRealtimeSelfSpanCount] = {
        "1.0 倍（世界が伸縮）", "2.0 倍まで", "3.0 倍まで", "5.0 倍まで", "制限なし",
    };
    return labels[std::clamp(index, 0, kRealtimeSelfSpanCount - 1)];
}

/*!
 * @brief 音量の段（0〜10）→ プロトコルの音量添字（0 ＝ 100% … 9 ＝ 10%）。
 * @details **向きが逆で、段の数も違う**（こちらは 0 を含む 11 段、あちらは 10 段）。
 * 換えるのはここ 1 か所だけにする——2 か所で換えると、片方だけ表を直したときに
 * 「音楽と効果音で同じ数字なのに音量が違う」になる。
 * @note 0（無音）はいちばん小さい段と同じ添字を返す。**鳴らさないのは入切のほう**で
 * 潰す（`build_ui_state`）ので、ここは値を作れれば足りる。
 */
int Hd2dSettings::volume_step_to_index(int step)
{
    return kVolumeMax - std::clamp(step, 1, kVolumeMax);
}

namespace {

//! サブパネルの割り付けが 1 つでも違うか（`differs_from` の一部）。
bool sub_split_differ(const SubSplit &a, const SubSplit &b)
{
    if ((a.bottom_count != b.bottom_count) || (a.right_count != b.right_count)) {
        return true;
    }
    for (int i = 0; i < (kSubBottomMax - 1); ++i) {
        if (a.bottom_w20[i] != b.bottom_w20[i]) {
            return true;
        }
    }
    for (int i = 0; i < (kSubRightMax - 1); ++i) {
        if (a.right_h20[i] != b.right_h20[i]) {
            return true;
        }
    }
    return false;
}

//! サブパネルの中身が 1 枚でも違うか。**出ていない枠も見る**（戻したときに気づけるように）。
bool sub_kinds_differ(const int *a, const int *b)
{
    for (int i = 0; i < kUiSubPanels; ++i) {
        if (a[i] != b[i]) {
            return true;
        }
    }
    return false;
}

//! 一人称で出す部位が 1 つでも違うか（`differs_from` の一部。書き戻すかの判定）。
bool vr_fps_show_differ(const bool *a, const bool *b)
{
    for (int i = 0; i < static_cast<int>(xr::PanelSlot::Count); ++i) {
        if (a[i] != b[i]) {
            return true;
        }
    }
    return false;
}

//! `key=value` の 1 行を切る。`#` から先と前後の空白は落とす。
bool split_line(const std::string &line, std::string &key, std::string &value)
{
    const std::size_t hash = line.find('#');
    const std::string body = (hash == std::string::npos) ? line : line.substr(0, hash);
    const std::size_t equals = body.find('=');
    if (equals == std::string::npos) {
        return false;
    }
    const auto trim = [](std::string text) {
        const std::size_t first = text.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            return std::string();
        }
        const std::size_t last = text.find_last_not_of(" \t\r\n");
        return text.substr(first, (last - first) + 1);
    };
    key = trim(body.substr(0, equals));
    value = trim(body.substr(equals + 1));
    return !key.empty();
}

/*!
 * @brief `vr_fps_hide=minimap,sub5` を読む（設計書 §22）。
 * @details **書いてある名前を「出さない」**にする。全部出すのが既定なので、
 * 何も書かなければ何も起きない。知らない名前は黙って飛ばす（版が進んだ cfg でも落ちない）。
 */
void parse_vr_fps_hide(const std::string &value, Hd2dSettings &out)
{
    for (bool &show : out.vr_fps_show) {
        show = true;
    }
    std::size_t at = 0;
    while (at <= value.size()) {
        const std::size_t comma = value.find(',', at);
        const std::string name = value.substr(at, (comma == std::string::npos) ? std::string::npos : (comma - at));
        for (int i = 0; i < static_cast<int>(xr::PanelSlot::Count); ++i) {
            if (name == xr::panel_slot_key(static_cast<xr::PanelSlot>(i))) {
                out.vr_fps_show[i] = false;
                break;
            }
        }
        if (comma == std::string::npos) {
            break;
        }
        at = comma + 1;
    }
}

/*!
 * @brief 古い cfg の `key_feature_menu=10` 形式（F キーの番号）を新しい表へ移す。
 * @details 割り当ては「操作ごと」に持ち方を変えた（2026-08-11 に決めた）。
 * **古い cfg を黙って捨てない**（F10 を F5 へ変えていた人の設定が消えると気づけない）。
 */
void migrate_fkey(KeyBinds &binds, int action, int fkey)
{
    if ((fkey < 1) || (fkey > 12)) {
        return;
    }
    (void)binds.assign(action, key_from_fkey(fkey), 0);
}

/*!
 * @brief `cores=変愚蛮怒|HengbandCore.exe,幻想蛮怒|GensobandCore.exe` を読む（設計 §6.1）。
 * @details 区切りは項目が `,`・名前と道が `|`。`|` が無い項目は道だけとみなし、
 * 名前はパスそのものにする（cfg を手で書いた人が困らないように）。
 * 空の項目と道の無い項目は捨てる。
 */
void parse_cores(const std::string &value, Hd2dSettings &out)
{
    out.cores.clear();
    std::size_t at = 0;
    while (at <= value.size()) {
        const std::size_t comma = value.find(',', at);
        const std::string item = value.substr(at, (comma == std::string::npos) ? std::string::npos : (comma - at));
        if (!item.empty()) {
            const std::size_t bar = item.find('|');
            CoreEntry entry;
            if (bar == std::string::npos) {
                entry.path = item;
                entry.name = item;
            } else {
                entry.name = item.substr(0, bar);
                entry.path = item.substr(bar + 1);
                if (entry.name.empty()) {
                    entry.name = entry.path;
                }
            }
            if (!entry.path.empty()) {
                out.cores.push_back(std::move(entry));
            }
        }
        if (comma == std::string::npos) {
            break;
        }
        at = comma + 1;
    }
}

//! 上の逆。空なら行ごと書かない（空行を読むと「1 件も無い」＝初回扱いになる）。
std::string cores_to_line(const std::vector<CoreEntry> &cores)
{
    std::string line;
    for (const CoreEntry &entry : cores) {
        if (!line.empty()) {
            line.push_back(',');
        }
        line += entry.name;
        line.push_back('|');
        line += entry.path;
    }
    return line;
}

//! `BgmMode` → cfg の綴り。**読む側（`load_settings`）と 1 対 1** にしてあること。
const char *bgm_mode_word(Hd2dSettings::BgmMode mode)
{
    switch (mode) {
    case Hd2dSettings::BgmMode::Ambience:
        return "ambience";
    case Hd2dSettings::BgmMode::Off:
        return "off";
    case Hd2dSettings::BgmMode::Music:
    default:
        return "music";
    }
}

bool cores_differ(const std::vector<CoreEntry> &a, const std::vector<CoreEntry> &b)
{
    if (a.size() != b.size()) {
        return true;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if ((a[i].name != b[i].name) || (a[i].path != b[i].path)) {
            return true;
        }
    }
    return false;
}

} // namespace

bool Hd2dSettings::differs_from(const Hd2dSettings &other) const
{
    return (this->lang != other.lang)
        || cores_differ(this->cores, other.cores) || (this->last_core != other.last_core)
        || (this->camera_pitch_deg != other.camera_pitch_deg) || (this->camera_fov_deg != other.camera_fov_deg)
        || (this->camera_cell_px != other.camera_cell_px) || (this->layout != other.layout)
        || (this->layout_portrait != other.layout_portrait)
        || (this->subs_open != other.subs_open) || (this->windowed != other.windowed)
        || (this->cutaway_radius != other.cutaway_radius) || (this->dof_strength != other.dof_strength)
        || (this->vignette_strength != other.vignette_strength) || (this->sepia != other.sepia)
        || (this->hdr != other.hdr) || (this->exposure != other.exposure)
        || (this->dust != other.dust)
        || (this->dungeon_light != other.dungeon_light)
        || (this->fps_slab_turn != other.fps_slab_turn)
        || (this->vr_tile_m != other.vr_tile_m) || (this->vr_table_forward_m != other.vr_table_forward_m)
        || (this->vr_table_height_m != other.vr_table_height_m)
        || (this->vr_panel_deg_per_cell != other.vr_panel_deg_per_cell)
        || (this->vr_panel_dist_m != other.vr_panel_dist_m)
        || (this->vr_panel_lift_deg != other.vr_panel_lift_deg)
        || (this->vr_panel_follow_deg != other.vr_panel_follow_deg)
        || (this->vr_fps_cell_m != other.vr_fps_cell_m) || vr_fps_show_differ(this->vr_fps_show, other.vr_fps_show)
        || (this->vr_render_scale != other.vr_render_scale)
        || (this->move_smoothing != other.move_smoothing)
        || (this->main_panel != other.main_panel)
        || (this->status_col_side != other.status_col_side)
        || (this->entity_glyph_fallback != other.entity_glyph_fallback)
        || (this->ascii_panel_px != other.ascii_panel_px)
        || (this->entity_style != other.entity_style)
        || (this->entity_glyph_pct != other.entity_glyph_pct)
        || (this->scene_look != other.scene_look) || (this->tron_sky != other.tron_sky)
        || (this->tron_face_glyph != other.tron_face_glyph)
        || (this->wear_pct != other.wear_pct)
        || (this->wear_materials != other.wear_materials)
        || (this->leaf_pct != other.leaf_pct)
        || (this->minimap_corner != other.minimap_corner)
        || (this->minimap_size_pct != other.minimap_size_pct)
        || (this->minimap_cell_px != other.minimap_cell_px)
        || (this->minimap_opacity_pct != other.minimap_opacity_pct)
        || (this->minimap_relative != other.minimap_relative)
        || (this->post.fog != other.post.fog)
        || (this->post.dof != other.post.dof) || (this->post.bloom != other.post.bloom)
        || (this->post.grade != other.post.grade) || (this->post.vignette != other.post.vignette)
        || (this->fps_fov_deg != other.fps_fov_deg)
        || (this->fps_vertical_look != other.fps_vertical_look)
        || (this->fps_wall_upper != other.fps_wall_upper) || (this->fps_ceiling != other.fps_ceiling)
        || (this->fps_step_ms != other.fps_step_ms)
        || (this->pad_defaults_applied != other.pad_defaults_applied)
        || (this->key_binds.to_line() != other.key_binds.to_line())
        || sub_split_differ(this->sub_split, other.sub_split)
        || sub_kinds_differ(this->sub_panel_kind, other.sub_panel_kind)
        || (this->backdrops != other.backdrops) || (this->pad.to_line() != other.pad.to_line())
        || (this->realtime_enabled != other.realtime_enabled)
        || (this->realtime_speed_index != other.realtime_speed_index)
        || (this->damage_flash != other.damage_flash) || (this->damage_shake != other.damage_shake)
        || (this->realtime_prompt_live != other.realtime_prompt_live)
        || (this->realtime_self_span_index != other.realtime_self_span_index)
        || (this->bgm_mode != other.bgm_mode) || (this->sound_enabled != other.sound_enabled)
        || (this->music_volume != other.music_volume) || (this->sound_volume != other.sound_volume)
        || this->vpad.differs_from(other.vpad);
}

const char *default_settings_path()
{
    return "hd2d.cfg";
}

bool load_settings(const std::string &path, Hd2dSettings &out)
{
    std::ifstream file(path);
    if (!file) {
        return false; // 無いのは**失敗ではない**（初回起動）
    }
    std::string line;
    while (std::getline(file, line)) {
        std::string key;
        std::string value;
        if (!split_line(line, key, value)) {
            continue;
        }
        if (key == "lang") {
            out.lang = value;
        } else if (key == "cores") {
            parse_cores(value, out); // 設計 §6.1。空なら初回扱いのまま
        } else if (key == "last_core") {
            out.last_core = value;
        } else if (key == "camera_pitch") {
            out.camera_pitch_deg = std::clamp(static_cast<float>(std::atof(value.c_str())), 10.f, 85.f);
        } else if (key == "camera_fov") {
            out.camera_fov_deg = std::clamp(static_cast<float>(std::atof(value.c_str())), 10.f, 80.f);
        } else if (key == "camera_cell_px") {
            out.camera_cell_px = std::clamp(static_cast<float>(std::atof(value.c_str())), 24.f, 400.f);
        } else if (key == "layout") {
            (void)parse_layout_mode(value, out.layout); // 読めない綴りは既定のまま
        } else if (key == "layout_portrait") {
            //! 縦持ちの作り。**古い cfg には無い**ので、無ければ既定（`Tall`）のままになる。
            (void)parse_layout_mode(value, out.layout_portrait);
        } else if (key == "subs_open") {
            out.subs_open = (std::atoi(value.c_str()) != 0);
        } else if (key == "windowed") {
            out.windowed = (std::atoi(value.c_str()) != 0);
        } else if (key == "cutaway") {
            out.cutaway_radius = std::max(0.f, static_cast<float>(std::atof(value.c_str())));
        } else if (key == "fps_slab_turn") {
            out.fps_slab_turn = (std::atoi(value.c_str()) == 1) ? Hd2dSettings::SlabTurn::Snap8
                                                                : Hd2dSettings::SlabTurn::Free;
        } else if (key == "dungeon_light") {
            out.dungeon_light = std::clamp(static_cast<float>(std::atof(value.c_str())), 0.5f, 3.f);
        } else if (key == "dof_strength") {
            out.dof_strength = std::clamp(static_cast<float>(std::atof(value.c_str())), 0.f, 2.4f);
        } else if (key == "vignette_strength") {
            out.vignette_strength = std::clamp(static_cast<float>(std::atof(value.c_str())), 0.f, 0.9f);
        } else if (key == "sepia") {
            out.sepia = std::clamp(static_cast<float>(std::atof(value.c_str())), 0.f, 1.f);
        } else if (key == "hdr") {
            out.hdr = std::clamp(static_cast<float>(std::atof(value.c_str())), 0.f, 1.5f);
        } else if (key == "exposure") {
            out.exposure = std::clamp(static_cast<float>(std::atof(value.c_str())), 0.5f, 2.5f);
        } else if (key == "dust") {
            out.dust = std::clamp(static_cast<float>(std::atof(value.c_str())), 0.f, 1.f);
        } else if (key == "vr_tile_m") {
            out.vr_tile_m = std::clamp(static_cast<float>(std::atof(value.c_str())), 0.002f, 1.f);
        } else if (key == "vr_table_forward_m") {
            out.vr_table_forward_m = std::clamp(static_cast<float>(std::atof(value.c_str())), 0.3f, 3.f);
        } else if (key == "vr_table_height_m") {
            //! 0 は「頭の高さから導く」。それ以外は膝から胸までの範囲で締める。
            const float v = static_cast<float>(std::atof(value.c_str()));
            out.vr_table_height_m = (v <= 0.f) ? 0.f : std::clamp(v, 0.05f, 1.4f);
        } else if (key == "vr_panel_deg_per_cell") {
            out.vr_panel_deg_per_cell = std::clamp(static_cast<float>(std::atof(value.c_str())), 0.1f, 2.f);
        } else if (key == "vr_panel_dist_m") {
            out.vr_panel_dist_m = std::clamp(static_cast<float>(std::atof(value.c_str())), 0.3f, 8.f);
        } else if (key == "vr_panel_lift_deg") {
            out.vr_panel_lift_deg = std::clamp(static_cast<float>(std::atof(value.c_str())), -45.f, 80.f);
        } else if (key == "vr_panel_follow_deg") {
            out.vr_panel_follow_deg = std::clamp(static_cast<float>(std::atof(value.c_str())), 0.f, 180.f);
        } else if (key == "vr_fps_hide") {
            parse_vr_fps_hide(value, out);
        } else if (key == "fps_step_ms") {
            out.fps_step_ms = std::clamp(std::atoi(value.c_str()), 60, 600);
        } else if (key == "vr_fps_cell_m") {
            out.vr_fps_cell_m = std::clamp(static_cast<float>(std::atof(value.c_str())), 0.f, 12.f);
        } else if (key == "vr_render_scale") {
            //! 下限 0.5 は「半分より粗くしても読めない」から（§5）。上は推奨値どまり。
            out.vr_render_scale = std::clamp(static_cast<float>(std::atof(value.c_str())), 0.5f, 1.f);
        } else if (key == "move_smoothing") {
            out.move_smoothing = (value == "all") ? MoveSmoothing::All : MoveSmoothing::Entities;
        } else if (key == "post") {
            std::string err;
            (void)PostFlags::parse(value, out.post, err); // 綴りが読めなければ既定のまま
        } else if (key == "sub_split") {
            //! `3,2` の形（下段の枚数, 右列の枚数）。
            int n = out.sub_split.bottom_count;
            int m = out.sub_split.right_count;
            if (std::sscanf(value.c_str(), "%d,%d", &n, &m) == 2) {
                out.sub_split.bottom_count = std::clamp(n, 2, kSubBottomMax);
                out.sub_split.right_count = std::clamp(m, 1, kSubRightMax);
            }
        } else if (key == "sub_bottom_w20") {
            /*
             * `5,5,5` の形（最後の 1 枚は引き算なので書かない）。
             * **2 個しか無い古い cfg も読む**——下段が 3 枚だった頃のものなので、
             * 3 個目は既定のままでよい（3 枚のときは使わない値である）。
             */
            int u[kSubBottomMax - 1]{ out.sub_split.bottom_w20[0], out.sub_split.bottom_w20[1],
                out.sub_split.bottom_w20[2] };
            const int got = std::sscanf(value.c_str(), "%d,%d,%d", &u[0], &u[1], &u[2]);
            for (int i = 0; i < got; ++i) {
                out.sub_split.bottom_w20[i]
                    = std::clamp(u[i], kSubBottomW20Min, kSubBottomUnits - kSubBottomW20Min);
            }
        } else if (key == "sub_right_h20") {
            int u[kSubRightMax - 1]{ out.sub_split.right_h20[0], out.sub_split.right_h20[1] };
            const int got = std::sscanf(value.c_str(), "%d,%d", &u[0], &u[1]);
            for (int i = 0; i < got; ++i) {
                out.sub_split.right_h20[i] = std::clamp(u[i], kSubRightH20Min, kSubRightUnits - kSubRightH20Min);
            }
        } else if (key == "sub_panel_kind") {
            /*
             * `6,8,1,3,0` の形。**-1 は UI 既定**。読めた個数だけ入れる。
             *
             * **5 個の古い cfg は並べ替えて読む**（2026-08-19 に枠が 5 → 7 になった）。
             * 昔の 4・5 個目は右列で、いまの右列は添字 4 から始まる（`kSubRightSlot`）。
             * そのまま入れると右上のステータスが下段の 4 枚目へ移り、
             * 「開いたら中身が入れ替わっていた」になる。
             */
            int kinds[kUiSubPanels]{ -1, -1, -1, -1, -1, -1, -1 };
            const int got = std::sscanf(value.c_str(), "%d,%d,%d,%d,%d,%d,%d",
                &kinds[0], &kinds[1], &kinds[2], &kinds[3], &kinds[4], &kinds[5], &kinds[6]);
            static constexpr int kLegacyOrder[5] = { 0, 1, 2, kSubRightSlot, kSubRightSlot + 1 };
            for (int i = 0; i < got; ++i) {
                const int slot = (got == 5) ? kLegacyOrder[i] : i;
                //! 知らない番号は「UI 既定」へ倒す（コアが増えた版の cfg でも落ちない）。
                out.sub_panel_kind[slot] = ((kinds[i] >= 0) && (kinds[i] < 16)) ? kinds[i] : -1;
            }
        } else if (key == "pad_bind") {
            out.pad = PadBinds{};
            out.pad.parse(value);
        } else if (key == "backdrops") {
            out.backdrops = (std::atoi(value.c_str()) != 0);
        } else if (key == "fps_fov") {
            out.fps_fov_deg = std::clamp(static_cast<float>(std::atof(value.c_str())), kFpsFovMinDeg, kFpsFovMaxDeg);
        } else if (key == "fps_vertical_look") {
            out.fps_vertical_look = (std::atoi(value.c_str()) != 0);
        } else if (key == "fps_wall_upper") {
            out.fps_wall_upper = (std::atoi(value.c_str()) != 0);
        } else if (key == "fps_ceiling") {
            out.fps_ceiling = (std::atoi(value.c_str()) != 0);
        } else if (key == "pad_defaults") {
            out.pad_defaults_applied = (std::atoi(value.c_str()) != 0);
        } else if (key == "realtime") {
            out.realtime_enabled = (std::atoi(value.c_str()) != 0);
        } else if (key == "realtime_prompt") {
            out.realtime_prompt_live = (std::atoi(value.c_str()) != 0);
        } else if (key == "realtime_self_span") {
            out.realtime_self_span_index = std::clamp(std::atoi(value.c_str()), 0, Hd2dSettings::kRealtimeSelfSpanCount - 1);
        } else if (key == "damage_flash") {
            out.damage_flash = (std::atoi(value.c_str()) != 0);
        } else if (key == "damage_shake") {
            out.damage_shake = (std::atoi(value.c_str()) != 0);
        } else if (key == "realtime_speed") {
            out.realtime_speed_index = std::clamp(std::atoi(value.c_str()), 0, Hd2dSettings::kRealtimeSpeedCount - 1);
        } else if (key == "bgm") {
            //! 綴りで書く（数字だと cfg を読んだ人に意味が分からない）。
            out.bgm_mode = (value == "ambience") ? Hd2dSettings::BgmMode::Ambience
                : (value == "off")               ? Hd2dSettings::BgmMode::Off
                                                 : Hd2dSettings::BgmMode::Music;
        } else if (key == "music") {
            //! **古い綴り**（2026-08-21 の朝の版。入切しか無かった）。読めるままにしておく。
            out.bgm_mode = (std::atoi(value.c_str()) != 0) ? Hd2dSettings::BgmMode::Music
                                                           : Hd2dSettings::BgmMode::Off;
        } else if (key == "music_volume") {
            out.music_volume = std::clamp(std::atoi(value.c_str()), 0, Hd2dSettings::kVolumeMax);
        } else if (key == "sound") {
            out.sound_enabled = (std::atoi(value.c_str()) != 0);
        } else if (key == "sound_volume") {
            out.sound_volume = std::clamp(std::atoi(value.c_str()), 0, Hd2dSettings::kVolumeMax);
        } else if (key == "vpad_show") {
            out.vpad.show = (std::atoi(value.c_str()) != 0);
        } else if (key == "main_panel") {
            //! 綴りで書く（数字だと cfg を読んだ人に意味が分からない）。
            out.main_panel = (value == "ascii") ? Hd2dSettings::MainPanel::Ascii
                                                : Hd2dSettings::MainPanel::Hd2d;
        } else if (key == "ascii_panel_px") {
            const int v = std::atoi(value.c_str());
            //! 0 は「UI と同じ」なので、締めの外でも 0 は 0 のまま通す。
            out.ascii_panel_px = (v <= 0) ? 0
                                          : std::clamp(v, Hd2dSettings::kAsciiPanelPxMin,
                                                Hd2dSettings::kAsciiPanelPxMax);
        } else if (key == "status_col_side") {
            //! 綴りで書く（auto / left / right。数字だと cfg を読んだ人に意味が分からない）。
            out.status_col_side = (value == "left")
                ? Hd2dSettings::StatusColSide::Left
                : ((value == "right") ? Hd2dSettings::StatusColSide::Right
                                      : Hd2dSettings::StatusColSide::Auto);
        } else if (key == "entity_glyph_fallback") {
            out.entity_glyph_fallback = (std::atoi(value.c_str()) != 0);
        } else if (key == "entity_style") {
            //! 綴りで書く（数字だと cfg を読んだ人に意味が分からない）。
            out.entity_style = (value == "ascii") ? Hd2dSettings::EntityStyle::Ascii
                                                  : Hd2dSettings::EntityStyle::Slab;
        } else if (key == "look") {
            //! 画調。**綴りで書く**（他の軸と同じ流儀）。
            SceneLookKind look = Hd2dSettings{}.scene_look;
            if (parse_scene_look(value, look)) {
                out.scene_look = look;
            }
        } else if (key == "tron_sky") {
            out.tron_sky = (std::atoi(value.c_str()) != 0);
        } else if (key == "tron_face_glyph") {
            out.tron_face_glyph = (std::atoi(value.c_str()) != 0);
        } else if (key == "wear") {
            //! 面の汚し（`render/surface_wear.h`）。百分率。
            out.wear_pct = std::clamp(std::atoi(value.c_str()),
                Hd2dSettings::kWearPctMin, Hd2dSettings::kWearPctMax);
        } else if (key == "wear_materials") {
            //! 材質ごとの入切（`render/surface_wear.h`）。読めない綴りは**丸ごと無視**。
            std::uint32_t bits = Hd2dSettings{}.wear_materials;
            if (parse_wear_materials(value, bits)) {
                out.wear_materials = bits;
            }
        } else if (key == "leaf") {
            //! 木の葉（`render/leaf_detail.h`）。百分率。
            out.leaf_pct = std::clamp(std::atoi(value.c_str()),
                Hd2dSettings::kLeafPctMin, Hd2dSettings::kLeafPctMax);
        } else if (key == "entity_glyph") {
            out.entity_glyph_pct = std::clamp(std::atoi(value.c_str()),
                Hd2dSettings::kEntityGlyphPctMin, Hd2dSettings::kEntityGlyphPctMax);
        } else if (key == "minimap_corner") {
            const int v = std::atoi(value.c_str());
            out.minimap_corner = static_cast<MinimapCorner>(
                std::clamp(v, 0, static_cast<int>(MinimapCorner::Count) - 1));
        } else if (key == "minimap_size") {
            out.minimap_size_pct = std::clamp(std::atoi(value.c_str()),
                Hd2dSettings::kMinimapSizePctMin, Hd2dSettings::kMinimapSizePctMax);
        } else if (key == "minimap_cell_px") {
            out.minimap_cell_px = std::clamp(std::atoi(value.c_str()),
                Hd2dSettings::kMinimapCellPxMin, Hd2dSettings::kMinimapCellPxMax);
        } else if (key == "minimap_opacity") {
            out.minimap_opacity_pct = std::clamp(std::atoi(value.c_str()),
                Hd2dSettings::kMinimapOpacityPctMin, Hd2dSettings::kMinimapOpacityPctMax);
        } else if (key == "minimap_relative") {
            out.minimap_relative = (std::atoi(value.c_str()) != 0);
        } else if (key == "vpad_opacity") {
            out.vpad.opacity = std::clamp(static_cast<float>(std::atof(value.c_str())), kVpadOpacityMin, kVpadOpacityMax);
        } else if (key == "vpad_visible") {
            out.vpad.parse_visible(value);
        } else if (key == "vpad_layout") {
            //! **横持ちの配置**（綴りは前からのまま。古い cfg はこれだけ持っている）。
            out.vpad.parse_layout(kVpadLandscape, value); //!< 空なら「全部既定の置き場所」の意味になる
        } else if (key == "vpad_layout_portrait") {
            //! 縦持ちの配置（2026-08-12 に決めた。無ければ縦は既定のまま）。
            out.vpad.parse_layout(kVpadPortrait, value);
        } else if (key == "key_bind") {
            //! **既定を捨ててから読む。**足すだけだと「消した割り当て」が毎回復活する。
            out.key_binds = KeyBinds{};
            out.key_binds.parse(value);
        } else if (key == "key_feature_menu") {
            migrate_fkey(out.key_binds, kActionFeatureMenu, std::atoi(value.c_str()));
        } else if (key == "key_layout_cycle") {
            migrate_fkey(out.key_binds, kActionLayoutCycle, std::atoi(value.c_str()));
        } else if (key == "key_subs_toggle") {
            migrate_fkey(out.key_binds, kActionSubsToggle, std::atoi(value.c_str()));
        } else if (key == "key_fps_toggle") {
            migrate_fkey(out.key_binds, kActionFpsToggle, std::atoi(value.c_str()));
        }
        // 知らないキーは黙って飛ばす（版が進んだ cfg でも落ちない）
    }
    return true;
}

bool save_settings(const std::string &path, const Hd2dSettings &settings)
{
    std::ofstream file(path, std::ios::trunc);
    if (!file) {
        return false;
    }
    file << "# HengbandHd2d.exe の設定（機能メニューが書きます）\n";
    file << "# 起動引数のほうが強い。ここに書いてあっても --camera= などで上書きできます。\n";
    file << "# 画面の言語（assets/lang/<コード>.json があるものだけ選べます）。\n";
    file << "lang=" << settings.lang << '\n';
    /*
     * 起動できるコア。`表示名|exe への相対のパス` を `,` で並べる。
     * **空の行は書かない**——空を読むと「1 件も無い＝初回」に見え、次の起動で自動登録が
     * やり直されてしまう（消したはずのコアが復活する）。
     */
    {
        const std::string cores_line = cores_to_line(settings.cores);
        if (!cores_line.empty()) {
            file << "# 起動時に選べるゲームコア。`表示名|exe へのパス` を `,` で並べます。\n";
            file << "cores=" << cores_line << '\n';
            file << "# 前回選んだコア（次の起動でカーソルがここに載ります）。\n";
            file << "last_core=" << settings.last_core << '\n';
        }
    }
    file << "camera_pitch=" << settings.camera_pitch_deg << '\n';
    file << "camera_fov=" << settings.camera_fov_deg << '\n';
    file << "camera_cell_px=" << settings.camera_cell_px << '\n';
    file << "# 画面の作り。**縦と横で別に持つ**（回すと自動で切り替わる。full/hybrid/split/tall）。\n";
    file << "layout=" << layout_mode_name(settings.layout) << '\n';
    file << "layout_portrait=" << layout_mode_name(settings.layout_portrait) << '\n';
    file << "subs_open=" << (settings.subs_open ? 1 : 0) << '\n';
    file << "windowed=" << (settings.windowed ? 1 : 0) << '\n';
    file << "cutaway=" << settings.cutaway_radius << '\n';
    file << "dof_strength=" << settings.dof_strength << '\n';
    file << "# 画面全体の色処理（2026-08-23 に決めた）。ビネットの強さ・セピア調・HDR 風・露出。\n";
    file << "vignette_strength=" << settings.vignette_strength << '\n';
    file << "sepia=" << settings.sepia << '\n';
    file << "hdr=" << settings.hdr << '\n';
    file << "exposure=" << settings.exposure << '\n';
    file << "# 空中に浮かぶ埃の量（0 = 出さない / 1 = 目一杯）。\n";
    file << "dust=" << settings.dust << '\n';
    file << "dungeon_light=" << settings.dungeon_light << '\n';
    file << "fps_slab_turn=" << static_cast<int>(settings.fps_slab_turn) << '\n';
    /*
     * VR の盤・卓・板。**どれも仮の値**なので
     * 実機で回して決め直す。卓は畳 1 畳で固定、動かせるのは縮尺と置き場所である。
     */
    file << "# VR。卓は畳 1 畳（1.82×0.91m）で固定。動かせるのは縮尺と高さと板の置き場所。\n";
    file << "vr_tile_m=" << settings.vr_tile_m << '\n';
    file << "vr_table_forward_m=" << settings.vr_table_forward_m << '\n';
    file << "# 天板の高さ。0 なら頭の高さから導く（座れば約 0.70m・立てば約 1.15m）。\n";
    file << "vr_table_height_m=" << settings.vr_table_height_m << '\n';
    file << "# 板の字の大きさ。1 桁の見込み角（度）。Quest 2 では 0.55° ＝ 11.3 画素。\n";
    file << "vr_panel_deg_per_cell=" << settings.vr_panel_deg_per_cell << '\n';
    file << "vr_panel_dist_m=" << settings.vr_panel_dist_m << '\n';
    file << "# 板を目線より上へ何度置くか（見上げる位置に置く）。\n";
    file << "vr_panel_lift_deg=" << settings.vr_panel_lift_deg << '\n';
    file << "# 板が付いてくるまでの遊びの角。0 で頭に貼り付き、180 で空間に固定。\n";
    file << "vr_panel_follow_deg=" << settings.vr_panel_follow_deg << '\n';
    file << "vr_fps_cell_m=" << settings.vr_fps_cell_m << '\n';
    file << "# 目の描き先をランタイムの推奨値の何倍で作るか(0.5〜1.0)。重い機で落とす口。\n";
    file << "vr_render_scale=" << settings.vr_render_scale << '\n';
    file << "# 一人称で歩き続けるときの 1 歩の間隔(ms)。短いほど速い。VR では速すぎると酔う。\n";
    file << "fps_step_ms=" << settings.fps_step_ms << '\n';
    /*
     * 一人称で**出さない**部位（`vr_fps_hide=minimap,sub5`）。全部出すのが既定なので、
     * 消したものだけを書く。**1 つも消していなければ行そのものを書かない**
     * ——空の行を書くと「全部消す」と読めてしまう。
     */
    {
        std::string hidden;
        for (int i = 0; i < static_cast<int>(xr::PanelSlot::Count); ++i) {
            if (settings.vr_fps_show[i]) {
                continue;
            }
            if (!hidden.empty()) {
                hidden += ",";
            }
            hidden += xr::panel_slot_key(static_cast<xr::PanelSlot>(i));
        }
        if (!hidden.empty()) {
            file << "# 一人称のとき板に描かない部位（status sub1..sub5 minimap message prompt bottom）\n";
            file << "vr_fps_hide=" << hidden << '\n';
        }
    }
    file << "move_smoothing=" << ((settings.move_smoothing == MoveSmoothing::All) ? "all" : "entities") << '\n';
    //! `PostFlags::parse` が読み戻せる綴りで書く（`bloom,dof` の形）。
    {
        std::string spec;
        const auto add = [&spec](bool on, const char *name) {
            if (!on) {
                return;
            }
            if (!spec.empty()) {
                spec += ",";
            }
            spec += name;
        };
        add(settings.post.fog, "fog");
        add(settings.post.dof, "dof");
        add(settings.post.bloom, "bloom");
        add(settings.post.grade, "grade");
        add(settings.post.vignette, "vignette");
        file << "post=" << (spec.empty() ? "off" : spec) << '\n';
    }
    file << "# サブパネルの枚数（下段 2〜4, 右列 1〜3）と仕切り（1/20 単位。最後の 1 枚は引き算）。\n";
    file << "sub_split=" << settings.sub_split.bottom_count << ',' << settings.sub_split.right_count << '\n';
    file << "sub_bottom_w20=";
    for (int i = 0; i < (kSubBottomMax - 1); ++i) {
        file << ((i == 0) ? "" : ",") << settings.sub_split.bottom_w20[i];
    }
    file << '\n';
    file << "sub_right_h20=";
    for (int i = 0; i < (kSubRightMax - 1); ++i) {
        file << ((i == 0) ? "" : ",") << settings.sub_split.right_h20[i];
    }
    file << '\n';
    file << "# サブパネルに映すコアのサブウインドウ（-1 = UI 既定）。**7 個で 1 組**——\n";
    file << "# 前の 4 つが下段（左から）、後ろの 3 つが右列（上から）。5 個の古い形も読める。\n";
    file << "# 0=所持品 1=装備品 2=魔法 3=ステータス 4=視界内モンスター 6=メッセージ 8=モンスターの思い出\n";
    file << "sub_panel_kind=";
    for (int i = 0; i < kUiSubPanels; ++i) {
        file << ((i == 0) ? "" : ",") << settings.sub_panel_kind[i];
    }
    file << '\n';
    file << "backdrops=" << (settings.backdrops ? 1 : 0) << '\n';
    file << "fps_fov=" << settings.fps_fov_deg << '\n';
    file << "fps_vertical_look=" << (settings.fps_vertical_look ? 1 : 0) << '\n';
    file << "# 一人称のダンジョンだけ。見下ろしは常に 1 段・天井なし。\n";
    file << "fps_wall_upper=" << (settings.fps_wall_upper ? 1 : 0) << '\n';
    file << "fps_ceiling=" << (settings.fps_ceiling ? 1 : 0) << '\n';
    file << "# 割り当ては「操作ごと」に持つ（操作の番号 ＝ 正:コアのコマンド id / 負:UI 自身の操作）。\n";
    file << "# パッド（`X:12,LB:-4` の形）。A=決定 / B=取消 / Back=機能メニュー は固定なので書かない。\n";
    file << "pad_bind=" << settings.pad.to_line() << '\n';
    file << "# 既定の割り当てを入れ終わったか。**0 に戻すと次回の起動で既定へ戻る**\n";
    file << "# （コアのコマンド id は実行時にしか分からないので、握手のあとで入れている）。\n";
    file << "pad_defaults=" << (settings.pad_defaults_applied ? 1 : 0) << '\n';
    file << "# ゲーム進行。1 = 実時間で進む。既定 0＝ターン制。\n";
    file << "# realtime_speed は 1 行動あたりの秒数の表への添字（0=0.2 秒 … 4=1.0 秒 … 7=3.0 秒）。\n";
    file << "realtime=" << (settings.realtime_enabled ? 1 : 0) << '\n';
    file << "realtime_speed=" << settings.realtime_speed_index << '\n';
    file << "# 被弾の見せ方。flash は属性で色が変わる全面の閃き、shake は画面の揺れ（酔う人向けに別々）。\n";
    file << "damage_flash=" << (settings.damage_flash ? 1 : 0) << '\n';
    file << "damage_shake=" << (settings.damage_shake ? 1 : 0) << '\n';
    file << "# 小窓（持ち物・足元の選択）の裏でも世界を進めるか。1 = 進む（選びながら殴られる）。\n";
    file << "realtime_prompt=" << (settings.realtime_prompt_live ? 1 : 0) << '\n';
    file << "# 速さの振れ幅（0=自分の拍は不変・世界が伸縮 … 4=制限なし）。はみ出た分は世界の拍へ載る。\n";
    file << "realtime_self_span=" << settings.realtime_self_span_index << '\n';
    file << "# 音（機能メニュー ＞ 音）。音量は 0（無音）〜10（最大）。0 にすると入のままでも鳴らない。\n";
    file << "# bgm は music（コアが曲を鳴らす）/ ambience（画面側が環境音を鳴らす）/ off。\n";
    file << "bgm=" << bgm_mode_word(settings.bgm_mode) << '\n';
    file << "music_volume=" << settings.music_volume << '\n';
    file << "sound=" << (settings.sound_enabled ? 1 : 0) << '\n';
    file << "sound_volume=" << settings.sound_volume << '\n';
    file << "# バーチャルパッド（Android の既定は 1。Windows でもタッチ画面なら 1 で使える）。\n";
    file << "vpad_show=" << (settings.vpad.show ? 1 : 0) << '\n';
    //! メインパネルの中身。**読む側の綴りと 1 対 1。**
    file << "main_panel=" << ((settings.main_panel == Hd2dSettings::MainPanel::Ascii) ? "ascii" : "hd2d") << '\n';
    file << "ascii_panel_px=" << settings.ascii_panel_px << '\n';
    //! 状態列の位置。**読む側の綴りと 1 対 1。**
    file << "# 状態列の位置（auto = コアの申告に従う / left / right）。\n";
    file << "status_col_side="
         << ((settings.status_col_side == Hd2dSettings::StatusColSide::Left)
                    ? "left"
                    : ((settings.status_col_side == Hd2dSettings::StatusColSide::Right) ? "right" : "auto"))
         << '\n';
    file << "# タイルが無い実体を字の板で描くか（1 = 描く・既定）。\n";
    file << "entity_glyph_fallback=" << (settings.entity_glyph_fallback ? 1 : 0) << '\n';
    //! 実体の表現。**読む側の綴りと 1 対 1。**
    file << "entity_style=" << ((settings.entity_style == Hd2dSettings::EntityStyle::Ascii) ? "ascii" : "slab")
         << '\n';
    file << "entity_glyph=" << settings.entity_glyph_pct << '\n';
    //! 画調。**読む側の綴りと 1 対 1。**
    file << "# 画調（standard = 従来の絵 / tron = 黒い面にネオンの線）。\n";
    file << "look=" << ((settings.scene_look == SceneLookKind::Tron) ? "tron" : "standard") << '\n';
    file << "# TRON のときに時刻に応じた空を出すか（0 = 真っ黒・既定 / 1 = 出す）。標準の画調では効かない。\n";
    file << "tron_sky=" << (settings.tron_sky ? 1 : 0) << '\n';
    file << "# TRON のときにブロックの面へマスの記号を出すか（1 = 出す・既定）。\n";
    file << "tron_face_glyph=" << (settings.tron_face_glyph ? 1 : 0) << '\n';
    file << "# 面の汚し（0 = 掛けない / 100 = 既定 / 200 = 倍）。画調とは別の軸で、標準でも TRON でも効く。\n";
    file << "wear=" << settings.wear_pct << '\n';
    file << "# 材質ごとに汚しを掛けるか（all / none / stone,wood,… の並び）。何がどれかは。\n";
    file << "wear_materials=" << wear_materials_text(settings.wear_materials) << '\n';
    file << "# 木の葉の細かさ（0 = 描かない / 100 = 既定 / 200 = 倍）。葉の面にだけ効く。\n";
    file << "leaf=" << settings.leaf_pct << '\n';
    //! ミニマップ（2026-08-19 に決めた）。**読む側の綴りと 1 対 1。**
    file << "minimap_corner=" << static_cast<int>(settings.minimap_corner) << '\n';
    file << "minimap_size=" << settings.minimap_size_pct << '\n';
    file << "minimap_cell_px=" << settings.minimap_cell_px << '\n';
    file << "minimap_opacity=" << settings.minimap_opacity_pct << '\n';
    file << "minimap_relative=" << (settings.minimap_relative ? 1 : 0) << '\n';
    file << "vpad_opacity=" << settings.vpad.opacity << '\n';
    file << "vpad_visible=" << settings.vpad.visible_line() << '\n';
    file << "# 置き場所の上書き（`名前:中心x,中心y,倍率`。x/y は画面に対する 0〜1。空 = 全部既定）。\n";
    file << "# **縦と横で別に持つ**（2026-08-12 に決めた。表示・濃度・ボタンごとの表示は共通）。\n";
    file << "vpad_layout=" << settings.vpad.layout_line(kVpadLandscape) << '\n';
    file << "vpad_layout_portrait=" << settings.vpad.layout_line(kVpadPortrait) << '\n';
    /*
     * キーボード（`操作:SDL キーコード:修飾` の形）。**番号で書く**のは、キーの綴りに
     * `,` や `:` そのものが含まれうるため（区切りと見分けがつかなくなる）。
     * 人が読めるように、いまの中身を注釈でも並べておく。
     */
    file << "# キーボード（`操作:SDL キーコード:修飾` の形。修飾は 1=Shift 2=Ctrl 4=Alt）。\n";
    file << "#   いまの中身:";
    for (const auto &entry : settings.key_binds.entries) {
        const std::string shown = key_display_name(entry.keycode, entry.mods);
        if (shown.empty()) {
            continue;
        }
        const std::string label = ui_action_label(entry.action);
        file << ' ' << (label.empty() ? ("コマンド " + std::to_string(entry.action)) : label) << '=' << shown;
    }
    file << '\n';
    file << "key_bind=" << settings.key_binds.to_line() << '\n';
    return static_cast<bool>(file);
}

void default_sub_panel_kinds(const std::string &core_name, int out[kUiSubPanels])
{
    /*
     * **Sil-Q（2026-08-25 に決めた。画のとおり）。**
     * 下段は 前のメッセージ / 見えている敵 / 戦いの目 / 敵の地形の記憶、
     * 右列は 冒険者 / 装備 / 持ち物。**右列の下 2 枚は 2026-08-23 の版と入れ替えてある**
     * （装備を上・持ち物を下。持ち物のほうが行数が多いので、いちばん高い枠へ入れる）。
     * 番号は `silq/adapter/sq_sub_terms.c` の `sq_flag_has_fix()` が通す 7 つ:
     * 0 = 持ち物 / 1 = 装備 / 2 = 冒険者 / 4 = 戦いの目 / 5 = 敵の地形の記憶 /
     * 7 = 前のメッセージ / 9 = 見えている敵。
     *
     * **コアのヘッダは引かない**（必守制約 4）ので、値だけをここに写している。
     * 向こうの表が動いたら空の枠が並ぶだけなので、変えるときは必ず突き合わせること。
     */
    static const int kSilq[kUiSubPanels] = { 7, 9, 4, 5, 2, 1, 0 };
    /*
     * **変愚蛮怒系（変愚・短愚）。**`Hd2dSettings::sub_panel_kind` の初期値と
     * 同じ数である——コアが申告する前に描くフレームはそちらを使うので、2 か所に要る。
     * 番号は `src/system/redrawing-flags-updater.h` の `SubWindowRedrawingFlag`。
     *
     * **2026-09-01 に手元の設定へ合わせた**（こう決めた——「全てのコアについて
     * 現状のサブパネル設定を現状の設定をデフォルトにして」）。`hd2d.cfg` は
     * 追跡していない（`.gitignore` の「機械ごとの好み」）ので、**焼かないと配れない**。
     */
    static const int kHengband[kUiSubPanels] = { 6, 8, 4, 1, 3, 0, 0 };
    //! **幻想蛮怒。**`hd2d-GensobandCore.cfg` の 2026-09-01 の中身。
    static const int kGensoband[kUiSubPanels] = { 6, 8, 1, 3, 3, 0, -1 };
    //! **FroxComposband。**`hd2d-FroxCore.cfg` の 2026-09-01 の中身。
    static const int kFrox[kUiSubPanels] = { 6, 8, 1, 3, 1, 0, -1 };

    const int *src = kHengband;
    if (core_name == "silq") {
        src = kSilq;
    } else if (core_name == "gensoband") {
        src = kGensoband;
    } else if (core_name == "frox") {
        src = kFrox;
    }
    //! `hengband` と `tangband` は同じ（2026-09-01 に決めた「短愚が変愚と同じにして」）。
    for (int i = 0; i < kUiSubPanels; ++i) {
        out[i] = src[i];
    }
}

void default_sub_split(const std::string &core_name, SubSplit &out)
{
    /*
     * **枚数と仕切りの既定もコアごと**（2026-09-01 に決めた。
     * 遊びながら組み直した `hd2d-*.cfg` を写した）。下段の仕切り（`4,4,5`）だけは
     * 4 コアとも同じだった。
     */
    const bool silq = (core_name == "silq");
    const bool frox = (core_name == "frox");
    //! 下段の枚数——変愚系と幻想は 3 枚、Frox と Sil-Q は 4 枚。
    out.bottom_count = (silq || frox) ? 4 : 3;
    //! 右列の枚数——Sil-Q だけ 3 枚。
    out.right_count = silq ? 3 : 2;
    for (int i = 0; i < (kSubBottomMax - 1); ++i) {
        out.bottom_w20[i] = kSubBottomW20Default[i];
    }
    /*
     * **右列の仕切りは変愚だけ違う**（上を厚く。`8,6`）。
     * 短愚も同じ（決めたことで変愚に合わせる）。
     */
    const bool heng_family = !(silq || frox) && (core_name != "gensoband");
    out.right_h20[0] = heng_family ? 8 : kSubRightH20Default[0];
    for (int i = 1; i < (kSubRightMax - 1); ++i) {
        out.right_h20[i] = kSubRightH20Default[i];
    }
}

std::string settings_path_for_core(const std::string &core_path)
{
    if (core_path.empty()) {
        return default_settings_path();
    }
    //! 道から名前だけを採る（`bin/SilCore.exe` → `SilCore`）。
    const std::size_t slash = core_path.find_last_of("/" "\\");
    std::string name = (slash == std::string::npos) ? core_path : core_path.substr(slash + 1);
    const std::size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) {
        name = name.substr(0, dot);
    }
    //! ファイル名にできない字は落とす（cfg は人が手で直せる所に置く）。
    std::string safe;
    for (const char c : name) {
        safe += (std::strchr("\/:*?\"<>|", c) == nullptr) ? c : '_';
    }
    if (safe.empty()) {
        return default_settings_path();
    }
    return "hd2d-" + safe + ".cfg";
}

} // namespace hd2d
