/*!
 * @file dcss_tabs.cpp
 * @brief `dcss_tabs.h` の実装。
 */
#include "dcss/dcss_tabs.h"

#include "app/ui_probe.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace hd2d::dcss {

namespace {

using json = nlohmann::json;

//! 持ち物の枠の数（原作 `ENDOFPACK`）。
constexpr int kEndOfPack = 127;
//! 呪文・能力の欄の最大数（原作 `SpellRegion::update` の 22、`AbilityRegion::get_max_slots` の 3 + 6 + 6）。
constexpr int kMaxSpellSlots = 22;
constexpr int kMaxAbilitySlots = 15;
//! 原作 `tilesdl.cc` の `m_tab_margin` と同じく、見出しの絵の大きさ。
constexpr int kIcon = 20;
//! 画面の側で足した命令（画面上のキーボードの切り替え）の番号。コアの命令の番号（`command_type`）とは重ならない値。
constexpr int kLocalKeyboardIdx = 90001;

enum Rank : int { kRankFloor = 0, kRankWall, kRankFeat, kRankPlayer, kRankMain, kRankGui, kRankIcons };

int rank_of_page(char page)
{
    switch (page) {
    case 'f': return kRankFloor;
    case 'w': return kRankWall;
    case 'e': return kRankFeat;
    case 'p': return kRankPlayer;
    case 'm': return kRankMain;
    case 'g': return kRankGui;
    default: return kRankIcons;
    }
}

const char *page_file(char page)
{
    switch (page) {
    case 'f': return "floor.png";
    case 'w': return "wall.png";
    case 'e': return "feat.png";
    case 'p': return "player.png";
    case 'm': return "main.png";
    case 'g': return "gui.png";
    default: return "icons.png";
    }
}

constexpr const char *kTabLabelFiles[kTabCount] = {
    "tab_label_item", "tab_label_spell", "tab_label_memorise", "tab_label_ability", "tab_label_monster",
    "tab_label_skill", "tab_label_command", "tab_label_metacommands", "tab_label_navigation",
};

int jint(const json &o, const char *key, int fallback = 0)
{
    const auto it = o.find(key);
    if ((it == o.end()) || !it->is_number()) {
        return fallback;
    }
    return static_cast<int>(it->get<long long>());
}

std::string jstr(const json &o, const char *key)
{
    const auto it = o.find(key);
    if ((it == o.end()) || !it->is_string()) {
        return std::string();
    }
    return it->get<std::string>();
}

DcssTabTile parse_tile(const json &o)
{
    DcssTabTile t;
    t.idx = jint(o, "i", -1);
    t.tile = jint(o, "t");
    t.base = jint(o, "b");
    t.quantity = jint(o, "q", -1);
    t.flag = static_cast<unsigned>(jint(o, "f"));
    t.special = jint(o, "s");
    t.special2 = jint(o, "s2");
    t.aptitude = jint(o, "a");
    t.name = jstr(o, "n");
    return t;
}

std::vector<DcssTabTile> parse_tiles(const json &root, const char *key)
{
    std::vector<DcssTabTile> out;
    const auto it = root.find(key);
    if ((it == root.end()) || !it->is_array()) {
        return out;
    }
    for (const json &e : *it) {
        if (e.is_object()) {
            out.push_back(parse_tile(e));
        }
    }
    return out;
}

bool same_tile(const DcssTabTile &a, const DcssTabTile &b)
{
    return (a.idx == b.idx) && (a.tile == b.tile) && (a.base == b.base) && (a.quantity == b.quantity) && (a.flag == b.flag)
        && (a.special == b.special) && (a.special2 == b.special2) && (a.aptitude == b.aptitude) && (a.name == b.name);
}

bool same_tiles(const std::vector<DcssTabTile> &a, const std::vector<DcssTabTile> &b)
{
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!same_tile(a[i], b[i])) {
            return false;
        }
    }
    return true;
}

bool same_monsters(const std::vector<DcssTabMonster> &a, const std::vector<DcssTabMonster> &b)
{
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if ((a[i].name != b[i].name) || (a[i].rel_x != b[i].rel_x) || (a[i].rel_y != b[i].rel_y) || (a[i].in_sight != b[i].in_sight)
            || (a[i].under != b[i].under) || (a[i].layers != b[i].layers) || (a[i].icons != b[i].icons)) {
            return false;
        }
    }
    return true;
}

} // namespace

// ------------------------------------------------------------ 画像ページ

bool DcssAtlas::load(const std::string &dir, std::string &err)
{
    this->loaded_ = false;
    this->tiles_.clear();
    this->consts_.clear();
    this->dir_ = dir;
    std::ifstream in(dir + "/tiles.tsv", std::ios::binary);
    if (!in) {
        err = "tiles.tsv がありません: " + dir;
        return false;
    }
    std::string line;
    int max_tile = 0;
    std::vector<std::pair<int, DcssAtlasTile>> rows;
    while (std::getline(in, line)) {
        if (line.empty() || (line[0] == '#')) {
            continue;
        }
        std::istringstream ss(line);
        std::string first;
        ss >> first;
        if (first == "const") {
            std::string name;
            int value = 0;
            ss >> name >> value;
            this->consts_[name] = value;
            continue;
        }
        DcssAtlasTile t;
        char page = 0;
        const int idx = std::atoi(first.c_str());
        ss >> page >> t.w >> t.h >> t.ox >> t.oy >> t.sx >> t.sy >> t.ex >> t.ey;
        if (!ss) {
            continue;
        }
        t.page = page;
        rows.emplace_back(idx, t);
        max_tile = std::max(max_tile, idx);
    }
    this->tiles_.assign(static_cast<std::size_t>(max_tile) + 1, DcssAtlasTile{});
    for (const auto &row : rows) {
        this->tiles_[static_cast<std::size_t>(row.first)] = row.second;
    }
    this->loaded_ = !rows.empty();
    if (!this->loaded_) {
        err = "tiles.tsv が空です";
    }
    return this->loaded_;
}

const DcssAtlasTile *DcssAtlas::info(int tile) const
{
    if ((tile < 0) || (static_cast<std::size_t>(tile) >= this->tiles_.size())) {
        return nullptr;
    }
    const DcssAtlasTile &t = this->tiles_[static_cast<std::size_t>(tile)];
    return (t.page != 0) ? &t : nullptr;
}

int DcssAtlas::constant(const std::string &name, int fallback) const
{
    const auto it = this->consts_.find(name);
    return (it == this->consts_.end()) ? fallback : it->second;
}

const UiImage *DcssAtlas::image(char page)
{
    const auto found = this->images_.find(page);
    if (found != this->images_.end()) {
        return found->second.valid() ? &found->second : nullptr;
    }
    if (this->failed_[page]) {
        return nullptr;
    }
    UiImage &img = this->images_[page];
    std::string err;
    if (!img.load(this->dir_ + "/" + page_file(page), err)) {
        this->failed_[page] = true;
        this->images_.erase(page);
        std::fprintf(stderr, "[hd2d] DCSS のタイルの画像ページを読めません: %s\n", err.c_str());
        return nullptr;
    }
    return &img;
}

void DcssAtlas::shutdown()
{
    for (auto &entry : this->images_) {
        entry.second.unload();
    }
    this->images_.clear();
    this->failed_.clear();
}

// ------------------------------------------------------------ 取り込み

DcssTabs::DcssTabs()
{
    this->last_clicked_.fill(-1);
}

DcssTabs::~DcssTabs()
{
    this->shutdown();
}

void DcssTabs::shutdown()
{
    this->atlas_.shutdown();
    this->tab_selected_.unload();
    this->tab_unselected_.unload();
    this->tab_mouseover_.unload();
    for (UiImage &image : this->tab_labels_) {
        image.unload();
    }
    this->heading_images_loaded_ = false;
    this->heading_images_failed_ = false;
}

bool DcssTabs::load_atlas(const std::string &dir, std::string &err)
{
    this->atlas_ok_ = this->atlas_.load(dir, err);
    return this->atlas_ok_;
}

bool DcssTabs::ingest_json(const std::string &payload)
{
    const json m = json::parse(payload, nullptr, false);
    if (m.is_discarded() || !m.is_object()) {
        return false;
    }
    DcssTabData d;
    d.received = true;
    d.map_display = jint(m, "map") != 0;
    d.floor_tile = jint(m, "floor_tile");
    d.floor_count = std::max(1, jint(m, "floor_n", 1));
    d.inv = parse_tiles(m, "inv");
    d.floor = parse_tiles(m, "fl");
    d.spells = parse_tiles(m, "spl");
    d.memorise = parse_tiles(m, "mem");
    d.abilities = parse_tiles(m, "abl");
    d.skills = parse_tiles(m, "skl");
    if (const auto it = m.find("mon"); (it != m.end()) && it->is_array()) {
        for (const json &e : *it) {
            if (!e.is_object()) {
                continue;
            }
            DcssTabMonster mon;
            mon.name = jstr(e, "n");
            mon.rel_x = jint(e, "x");
            mon.rel_y = jint(e, "y");
            mon.in_sight = jint(e, "los") != 0;
            if (const auto u = e.find("u"); (u != e.end()) && u->is_array()) {
                for (const json &v : *u) {
                    mon.under.push_back(static_cast<int>(v.get<long long>()));
                }
            }
            if (const auto l = e.find("lay"); (l != e.end()) && l->is_array()) {
                for (const json &v : *l) {
                    if (v.is_array() && (v.size() >= 4)) {
                        mon.layers.push_back({ static_cast<int>(v[0].get<long long>()), static_cast<int>(v[1].get<long long>()),
                            static_cast<int>(v[2].get<long long>()), static_cast<int>(v[3].get<long long>()) });
                    }
                }
            }
            if (const auto ic = e.find("ic"); (ic != e.end()) && ic->is_array()) {
                for (const json &v : *ic) {
                    mon.icons.push_back(static_cast<int>(v.get<long long>()));
                }
            }
            d.monsters.push_back(std::move(mon));
        }
    }
    if (const auto it = m.find("cmd"); (it != m.end()) && it->is_array()) {
        for (std::size_t i = 0; (i < 3) && (i < it->size()); ++i) {
            if ((*it)[i].is_array()) {
                for (const json &e : (*it)[i]) {
                    if (e.is_object()) {
                        d.commands[i].push_back(parse_tile(e));
                    }
                }
            }
        }
    }
    if (this->android_extras_) {
        // 原作の Android 版の命令の欄の末尾（システム命令の 13 番目）。コアは Android の枝を外して組んでいるので、画面の側で足す。
        DcssTabTile t;
        t.idx = kLocalKeyboardIdx;
        t.tile = 9550; // TILEG_CMD_KEYBOARD
        t.name = this->japanese_ ? "画面上のキーボードを表示/非表示にする" : "Toggle on-screen keyboard";
        d.commands[1].push_back(std::move(t));
    }
    const bool same = this->data_.received && (this->data_.map_display == d.map_display) && (this->data_.floor_tile == d.floor_tile)
        && (this->data_.floor_count == d.floor_count) && same_tiles(this->data_.inv, d.inv) && same_tiles(this->data_.floor, d.floor)
        && same_tiles(this->data_.spells, d.spells) && same_tiles(this->data_.memorise, d.memorise)
        && same_tiles(this->data_.abilities, d.abilities) && same_tiles(this->data_.skills, d.skills)
        && same_monsters(this->data_.monsters, d.monsters) && same_tiles(this->data_.commands[0], d.commands[0])
        && same_tiles(this->data_.commands[1], d.commands[1]) && same_tiles(this->data_.commands[2], d.commands[2]);
    if (same) {
        return false;
    }
    this->data_ = std::move(d);
    ++this->serial_;
    return true;
}

// ------------------------------------------------------------ 並べ方（原作の update()）

std::vector<DcssTabTile> DcssTabs::arrange_inventory(const DcssTabData &data, int mx, int my, bool small, int &page)
{
    std::vector<DcssTabTile> items;
    if ((mx * my) <= 0) {
        return items;
    }
    // XX in principle this should check specific commands in tile_command_not_applicable
    const bool disable_all = data.map_display;
    const int max_pack_items = kEndOfPack;

    for (const DcssTabTile &t : data.inv) {
        if (static_cast<int>(items.size()) >= max_pack_items) {
            break;
        }
        DcssTabTile d = t;
        if (disable_all) {
            d.flag |= kTfInvalid;
        }
        items.push_back(std::move(d));
    }

    const int num_ground = static_cast<int>(data.floor.size());
    // ensure we don't end up stuck on a later page.
    // can store mx*my items on first page, but lose 2 for each following page (next & prev buttons)
    const int total_items = static_cast<int>(items.size()) + num_ground;
    const int max_page = std::max(0, total_items - 2) / std::max(1, (mx * my) - 2);
    page = std::min(page, max_page);

    const int remaining = (mx * my) - static_cast<int>(items.size());
    const int empty_on_this_row = mx - (static_cast<int>(items.size()) % mx);

    // If we're not on the last row...
    if (static_cast<int>(items.size()) < (mx * (my - 1))) {
        const auto empty_tile = [&]() {
            DcssTabTile d;
            if (disable_all || (static_cast<int>(items.size()) >= max_pack_items)) {
                d.flag |= kTfInvalid;
            }
            return d;
        };
        if (num_ground > (remaining - empty_on_this_row)) {
            // Fill out part of this row.
            const int fill = remaining - num_ground;
            for (int i = 0; i < fill; ++i) {
                items.push_back(empty_tile());
            }
        } else {
            // Fill out the rest of this row.
            while ((static_cast<int>(items.size()) % mx) != 0) {
                items.push_back(empty_tile());
            }
            // Add extra rows, if needed.
            const int ground_rows = std::max(((num_ground - 1) / mx) + 1, 1);
            while (((static_cast<int>(items.size()) / mx) + ground_rows < my) && (static_cast<int>(items.size()) < max_pack_items)) {
                for (int i = 0; i < mx; ++i) {
                    items.push_back(empty_tile());
                }
            }
        }
        if (small) {
            // Leave only one row of floor items for small layout
            while (static_cast<int>(items.size()) < (mx * (my - 1))) {
                items.push_back(DcssTabTile{});
            }
        }
    }

    // Then, as many ground items as we can fit.
    for (const DcssTabTile &t : data.floor) {
        if (static_cast<int>(items.size()) >= (mx * my * (page + 1))) {
            break;
        }
        DcssTabTile d = t;
        if (disable_all) {
            d.flag |= kTfInvalid;
        }
        items.push_back(std::move(d));
    }

    if (!small) {
        while (static_cast<int>(items.size()) < (mx * my)) {
            // let's not do this for p2 either
            DcssTabTile d;
            d.flag = kTfFloor;
            if (disable_all) {
                d.flag |= kTfInvalid;
            }
            items.push_back(std::move(d));
        }
    }
    return items;
}

// ------------------------------------------------------------ 配置

void DcssTabs::set_state(int input_mode, bool map_screen, bool japanese)
{
    this->input_mode_ = input_mode;
    this->japanese_ = japanese;
    if (map_screen != this->map_screen_) {
        this->prev_map_screen_ = this->map_screen_;
        this->map_screen_ = map_screen;
        // 原作 `do_map_display`・`set_map_display(false)`: 全体地図の画面へ入ると「地図の移動」、出ると「持ち物」を開く（小さい配置では替えない）。
        if (this->have_layout_ && !this->layout_.small_layout && !this->regions_.empty()) {
            Region &main = this->regions_[0];
            const int want = map_screen ? kTabNavigation : kTabItem;
            if (this->tab_enabled_in_main(want)) {
                main.active = want;
                main.deactivated = false;
                if (want == kTabItem) {
                    this->inv_page_ = 0;
                }
                ++this->serial_;
            }
        }
    }
}

bool DcssTabs::tab_enabled_in_main(int tab) const
{
    if ((tab < 0) || (tab >= kTabCount)) {
        return false;
    }
    return this->layout_.small_layout || this->layout_.tab_enabled[static_cast<std::size_t>(tab)];
}

void DcssTabs::set_layout(const DcssLayout &layout, int origin_x, int origin_y)
{
    const bool first = !this->have_layout_;
    const bool changed = first || (origin_x != this->origin_x_) || (origin_y != this->origin_y_) || !(layout.ok == this->layout_.ok)
        || (layout.tabs.x != this->layout_.tabs.x) || (layout.tabs.y != this->layout_.tabs.y) || (layout.tabs.w != this->layout_.tabs.w)
        || (layout.tabs.h != this->layout_.tabs.h) || (layout.tabs.cols != this->layout_.tabs.cols) || (layout.tabs.rows != this->layout_.tabs.rows)
        || (layout.tabs.cell_w != this->layout_.tabs.cell_w) || (layout.small_layout != this->layout_.small_layout)
        || (layout.tab_enabled != this->layout_.tab_enabled) || (layout.fixed_tabs.size() != this->layout_.fixed_tabs.size());
    bool fixed_changed = false;
    if (!changed) {
        for (std::size_t i = 0; i < layout.fixed_tabs.size(); ++i) {
            const DcssFixedTab &a = layout.fixed_tabs[i];
            const DcssFixedTab &b = this->layout_.fixed_tabs[i];
            if ((a.tab_index != b.tab_index) || (a.region.x != b.region.x) || (a.region.y != b.region.y) || (a.region.rows != b.region.rows)
                || (a.region.cols != b.region.cols)) {
                fixed_changed = true;
            }
        }
    }
    if (!changed && !fixed_changed) {
        return;
    }
    const bool was_small = this->have_layout_ && this->layout_.small_layout;
    this->layout_ = layout;
    this->origin_x_ = origin_x;
    this->origin_y_ = origin_y;
    this->have_layout_ = layout.ok;
    if (!layout.ok) {
        return;
    }
    // 主の欄の選んだ見出し。無効になった（固定表示へ移った）なら原作 `disable_tab` と同じく次へ。
    int active = this->regions_.empty() ? kTabItem : this->regions_[0].active;
    bool deactivated = this->regions_.empty() ? false : this->regions_[0].deactivated;
    this->rebuild_regions();
    Region &main = this->regions_[0];
    if (first || (was_small != layout.small_layout)) {
        // 原作 `set_small_layout`: 小さい配置への切り替えで `activate_tab(0)` → 小さい配置なら閉じる。
        active = kTabItem;
        deactivated = layout.small_layout;
    }
    if (!layout.small_layout && !layout.tab_enabled[static_cast<std::size_t>(std::clamp(active, 0, kTabCount - 1))]) {
        const int next = (active + 1) % kTabCount;
        active = (next == active) ? -1 : next;
    }
    main.active = active;
    main.deactivated = deactivated;
    ++this->serial_;
}

void DcssTabs::rebuild_regions()
{
    const DcssLayout &L = this->layout_;
    this->regions_.clear();
    const bool small = L.small_layout;

    Region main;
    main.fixed = false;
    main.sx = this->origin_x_ + L.tabs.x;
    main.sy = this->origin_y_ + L.tabs.y;
    main.ox = L.tabs.margin_x;
    main.oy = L.tabs.margin_y;
    main.dx = std::max(1, L.tabs.cell_w);
    main.mx = L.tabs.cols;
    main.my = L.tabs.rows;
    main.wy = L.tabs.h;
    main.wx = L.tabs.w;
    if (!small) {
        // 左端に見出しを縦に積む（有効なものだけ。20 画素 + 1 画素の余白）。
        int y = main.sy;
        for (int i = 0; i < kTabCount; ++i) {
            if (!L.tab_enabled[static_cast<std::size_t>(i)]) {
                continue;
            }
            main.headings.push_back({ i, 0, main.sx, y, kIcon, kIcon + 1 });
            y += kIcon + 1;
        }
    } else {
        // 小さい配置: 見出しだけを右端に、窓の高さへ均等に拡大して並べる（原作 `set_small_layout`）。当たりの高さは 2 倍（半分の大きさの見出しを広げて置く）。
        // 見出しの絵を出さないとき（原作の Android の既定。`tile_icons` の余白 `ox` = 0）は、並べない（原作 `TabbedRegion::render` の `if (ox > 0)`・`get_mouseover_tab`）。
        const int unit = std::max(1, L.tabs.cell_h);
        const int size = std::max(kIcon, (kIcon * unit) / 32);
        const int pitch = (((kIcon * 2) + 1) * unit) / 32;
        for (int i = 0; (i < kTabCount) && (L.tabs.margin_x > 0); ++i) {
            const int y = this->origin_y_ + L.tabs.y + (i * pitch);
            const int x = this->origin_x_ + L.tabs.x + L.tabs.margin_x - size;
            main.headings.push_back({ i, 0, x, y, size, pitch });
        }
        // 中身は左端（窓の左）から格子で置く。
        main.sx = this->origin_x_;
        main.ox = 0;
        main.oy = 0;
        main.sy = this->origin_y_ + L.tabs.y;
    }
    this->regions_.push_back(std::move(main));

    for (const DcssFixedTab &f : L.fixed_tabs) {
        Region r;
        r.fixed = true;
        r.fixed_tab = f.tab_index;
        r.sx = this->origin_x_ + f.region.x;
        r.sy = this->origin_y_ + f.region.y;
        r.ox = f.region.margin_x;
        r.oy = f.region.margin_y;
        r.dx = std::max(1, f.region.cell_w);
        r.mx = f.region.cols;
        r.my = f.region.rows;
        r.wy = f.region.h;
        r.wx = f.region.w;
        r.active = f.tab_index;
        const int idx = static_cast<int>(this->regions_.size());
        r.headings.push_back({ f.tab_index, idx, r.sx, r.sy, kIcon, kIcon + 1 });
        this->regions_.push_back(std::move(r));
    }
    for (std::size_t i = 0; i < this->regions_.size(); ++i) {
        for (Heading &h : this->regions_[i].headings) {
            h.region = static_cast<int>(i);
        }
    }
}

// ------------------------------------------------------------ 中身

const std::vector<DcssTabTile> *DcssTabs::list_for(int tab) const
{
    switch (tab) {
    case kTabSpell: return &this->data_.spells;
    case kTabMemorise: return &this->data_.memorise;
    case kTabAbility: return &this->data_.abilities;
    case kTabSkill: return &this->data_.skills;
    case kTabCommand: return &this->data_.commands[0];
    case kTabSystemCommand: return &this->data_.commands[1];
    case kTabNavigation: return &this->data_.commands[2];
    default: return nullptr;
    }
}

std::vector<DcssTabTile> DcssTabs::items_for(int tab, int mx, int my, bool small, int &page)
{
    std::vector<DcssTabTile> out;
    if ((mx * my) <= 0) {
        return out;
    }
    if (tab == kTabItem) {
        return arrange_inventory(this->data_, mx, my, small, page);
    }
    if (tab == kTabMonster) {
        const int n = std::min(mx * my, static_cast<int>(this->data_.monsters.size()));
        for (int i = 0; i < n; ++i) {
            DcssTabTile t;
            t.idx = i;
            out.push_back(std::move(t));
        }
        return out;
    }
    const std::vector<DcssTabTile> *list = this->list_for(tab);
    if (list == nullptr) {
        return out;
    }
    std::size_t cap = list->size();
    if (tab == kTabSpell) {
        cap = std::min<std::size_t>(cap, static_cast<std::size_t>(std::min(kMaxSpellSlots, mx * my)));
    } else if (tab == kTabMemorise) {
        cap = std::min<std::size_t>(cap, static_cast<std::size_t>(mx * my));
    } else if (tab == kTabAbility) {
        cap = std::min<std::size_t>(cap, static_cast<std::size_t>(std::min(kMaxAbilitySlots, mx * my)));
    }
    out.assign(list->begin(), list->begin() + static_cast<std::ptrdiff_t>(cap));
    return out;
}

void DcssTabs::put(std::vector<Op> &ops, int tile, int cx, int cy, int ofs_x, int ofs_y, bool centre, int ymax) const
{
    const DcssAtlasTile *info = this->atlas_.info(tile);
    if (info == nullptr) {
        return;
    }
    ops.push_back(Op{ rank_of_page(info->page), tile, cx, cy, ofs_x, ofs_y, centre, ymax });
}

void DcssTabs::put_number(std::vector<Op> &ops, int cx, int cy, int num) const
{
    // 原作 `GridRegion::draw_number`: 数字の絵（8×14）を 4 桁まで並べる。
    if (num > 9999) {
        num = 9999;
    }
    const int num0 = this->atlas_.constant("TILEI_NUM0");
    int offset_x = (num > 1999) ? 0 : 3;
    const int offset_y = 1;
    const auto add_char = [&](int digit) {
        const int tile = num0 + digit;
        this->put(ops, tile, cx, cy, offset_x, offset_y, false);
        const DcssAtlasTile *info = this->atlas_.info(tile);
        return (info != nullptr) ? info->w : 0;
    };
    int help = num;
    const int c1000 = help / 1000;
    help -= c1000 * 1000;
    if (c1000 != 0) {
        offset_x += add_char(c1000);
    }
    const int c100 = help / 100;
    help -= c100 * 100;
    if ((c100 != 0) || (c1000 != 0)) {
        offset_x += add_char(c100);
    }
    const int c10 = help / 10;
    if ((c10 != 0) || (c100 != 0) || (c1000 != 0)) {
        offset_x += add_char(c10);
    }
    add_char(help % 10);
}

void DcssTabs::pack_tab(std::vector<Op> &ops, int tab, const std::vector<DcssTabTile> &items_in, int mx, int my, bool small, int page, const Cursor &cur)
{
    std::vector<DcssTabTile> items = items_in;
    const int tile_mesh = this->atlas_.constant("TILEI_MESH");
    const int tile_cursor = this->atlas_.constant("TILEI_CURSOR");
    const int slot = this->atlas_.constant("TILE_ITEM_SLOT");
    // カーソル（原作 `place_cursor`: 見つかった枠の旗に立てる）
    if ((cur.cx >= 0) && (cur.cy >= 0)) {
        const std::size_t ci = static_cast<std::size_t>(cur.cx + (cur.cy * mx) + (page * mx * my) - (page * 2));
        if (ci < items.size()) {
            items[ci].flag |= kTfCursor;
        }
    }
    const auto main_tile = [&](int tile, int base, int cx, int cy) {
        // 原作 `add_main_tile`: 下地があれば先に重ねる
        if (base != 0) {
            this->put(ops, base, cx, cy);
        }
        this->put(ops, tile, cx, cy);
    };

    if (tab == kTabItem) {
        const int num_floor = std::max(1, this->data_.floor_count);
        std::size_t i = static_cast<std::size_t>(page * mx * my) - static_cast<std::size_t>(page * 2);
        for (int y = 0; y < my; ++y) {
            for (int x = 0; x < mx; ++x) {
                if (i >= items.size()) {
                    break;
                }
                const DcssTabTile &item = items[i++];
                if ((item.flag & kTfFloor) != 0) {
                    if ((i > static_cast<std::size_t>(mx * my * (page + 1))) && (item.tile != 0)) {
                        break;
                    }
                    this->put(ops, this->data_.floor_tile + static_cast<int>(i % static_cast<std::size_t>(num_floor)), x, y);
                } else if (!small) {
                    this->put(ops, slot, x, y);
                }
            }
        }
        const auto real_count = [&]() {
            int total = 0;
            for (const DcssTabTile &t : items) {
                if (t.idx != -1) {
                    ++total;
                }
            }
            return total;
        };
        const auto is_next_button = [&](int idx) { return (idx == (((mx * my) - 2) * (page + 1)) - 1 + 2) && (real_count() >= (mx * my)); };
        i = static_cast<std::size_t>(page * mx * my) - static_cast<std::size_t>(page * 2);
        for (int y = 0; y < my; ++y) {
            for (int x = 0; x < mx; ++x) {
                if (i >= items.size()) {
                    break;
                }
                const DcssTabTile &item = items[i++];
                if (is_next_button(static_cast<int>(i) - 1)) {
                    this->put(ops, this->atlas_.constant("TILE_ITEM_PAGE_NEXT"), x, y);
                    continue;
                }
                if ((y == 0) && (x == 0) && (page > 0)) {
                    this->put(ops, this->atlas_.constant("TILE_ITEM_PAGE_PREVIOUS"), x, y);
                    continue;
                }
                if ((item.flag & kTfEquip) != 0) {
                    if ((item.flag & kTfCurse) != 0) {
                        this->put(ops, this->atlas_.constant("TILE_ITEM_SLOT_EQUIP_CURSED"), x, y);
                    } else {
                        this->put(ops, this->atlas_.constant("TILE_ITEM_SLOT_EQUIP"), x, y);
                    }
                    if ((item.flag & kTfMelded) != 0) {
                        this->put(ops, tile_mesh, x, y);
                    }
                } else if ((item.flag & kTfCurse) != 0) {
                    this->put(ops, this->atlas_.constant("TILE_ITEM_SLOT_CURSED"), x, y);
                }
                if ((item.flag & 0x0100) != 0) {
                    this->put(ops, this->atlas_.constant("TILEI_ITEM_SLOT_SELECTED"), x, y);
                }
                if ((item.flag & kTfCursor) != 0) {
                    this->put(ops, tile_cursor, x, y);
                }
                if ((item.flag & kTfChaotic) != 0) {
                    this->put(ops, this->atlas_.constant("TILE_MODIFIER_CHAOTIC"), x, y, 0, 0, false);
                }
                if (item.tile != 0) {
                    main_tile(item.tile, item.base, x, y);
                }
                if (item.quantity != -1) {
                    this->put_number(ops, x, y, item.quantity);
                }
                if (item.special != 0) {
                    this->put(ops, item.special, x, y, 0, 0, false);
                }
                if (item.special2 != 0) {
                    this->put(ops, item.special2, x, y, 0, 0, false);
                }
                if (((item.flag & kTfInvalid) != 0) && !small) {
                    this->put(ops, tile_mesh, x, y);
                }
            }
        }
        return;
    }

    // 呪文・覚える呪文・能力・技能・命令（原作 `SpellRegion::pack_buffers` ほか）
    std::size_t i = 0;
    for (int y = 0; y < my; ++y) {
        if (i >= items.size()) {
            break;
        }
        for (int x = 0; x < mx; ++x) {
            if (i >= items.size()) {
                break;
            }
            const DcssTabTile &item = items[i++];
            if ((item.flag & kTfInvalid) != 0) {
                this->put(ops, tile_mesh, x, y);
            }
            if (tab == kTabSkill) {
                if (item.quantity > 0) {
                    this->put_number(ops, x, y, item.quantity);
                }
                const int apt = item.aptitude;
                int apt_tile = this->atlas_.constant("TILEI_NUM_ZERO");
                if ((apt >= -5) && (apt <= -1)) {
                    apt_tile = this->atlas_.constant("TILEI_NUM_MINUS5") + (apt + 5);
                } else if ((apt >= 1) && (apt <= 11)) {
                    apt_tile = this->atlas_.constant("TILEI_NUM_PLUS1") + (apt - 1);
                }
                this->put(ops, apt_tile, x, y);
                if (item.tile != 0) {
                    this->put(ops, item.tile, x, y);
                }
            } else if ((tab == kTabCommand) || (tab == kTabSystemCommand) || (tab == kTabNavigation)) {
                if (item.tile != 0) {
                    this->put(ops, item.tile, x, y);
                }
            } else {
                // 呪文・覚える呪文・能力
                if ((item.flag & kTfCursor) != 0) {
                    this->put(ops, tile_cursor, x, y);
                }
                if (((tab == kTabSpell) || (tab == kTabMemorise)) && ((item.flag & kTfEquip) != 0)) {
                    this->put(ops, this->atlas_.constant("TILE_ITEM_SLOT_VEHUMET"), x, y); // Vehumet gift
                }
                if (tab == kTabAbility) {
                    if (item.quantity > 0) {
                        this->put_number(ops, x, y, item.quantity); // mp cost
                    }
                } else if (item.quantity != -1) {
                    this->put_number(ops, x, y, item.quantity);
                }
                if (item.tile != 0) {
                    this->put(ops, item.tile, x, y);
                }
            }
            if ((tab == kTabSkill) || (tab == kTabCommand) || (tab == kTabSystemCommand) || (tab == kTabNavigation)) {
                if ((item.flag & kTfCursor) != 0) {
                    this->put(ops, tile_cursor, x, y);
                }
            }
        }
    }
}

void DcssTabs::pack_monsters(std::vector<Op> &ops, int mx, int my, bool small, const Cursor &cur)
{
    const int num_floor = std::max(1, this->data_.floor_count);
    const int tile_mesh = this->atlas_.constant("TILEI_MESH");
    const int tile_cursor = this->atlas_.constant("TILEI_CURSOR");
    const int max_mons = std::min(mx * my, static_cast<int>(this->data_.monsters.size()));
    int i = 0;
    for (int y = 0; y < my; ++y) {
        for (int x = 0; x < mx; ++x) {
            const bool cursor = (cur.cx == x) && (cur.cy == y) && (i < max_mons);
            const DcssTabMonster *mon = (i < max_mons) ? &this->data_.monsters[static_cast<std::size_t>(i)] : nullptr;
            ++i;
            if ((mon != nullptr) && mon->in_sight) {
                for (const int t : mon->under) {
                    this->put(ops, t, x, y);
                }
                for (const auto &l : mon->layers) {
                    // 原作 `draw_dolls`: 人形の部品は y_max から下を切る（0 = 切らない）。
                    this->put(ops, l[0], x, y, l[1], l[2], true, (l[3] > 0) ? l[3] : -1);
                }
                for (const int t : mon->icons) {
                    this->put(ops, t, x, y, 0, 0, true);
                }
                if (cursor) {
                    this->put(ops, tile_cursor, x, y);
                }
                continue;
            }
            // Fill the rest of the space with out of sight floor tiles.
            if (!small) {
                this->put(ops, this->data_.floor_tile + (i % num_floor), x, y);
                this->put(ops, tile_mesh, x, y);
            }
        }
    }
}

void DcssTabs::draw_ops(const UiImagePainter &images, std::vector<Op> &ops, int ox, int oy, int dx)
{
    std::stable_sort(ops.begin(), ops.end(), [](const Op &a, const Op &b) { return a.rank < b.rank; });
    const double scale = static_cast<double>(dx) / 32.0;
    for (const Op &op : ops) {
        const DcssAtlasTile *info = this->atlas_.info(op.tile);
        if (info == nullptr) {
            continue;
        }
        const UiImage *img = this->atlas_.image(info->page);
        if (img == nullptr) {
            continue;
        }
        // 原作 `TilesTexture::get_coords`
        const int size_ox = op.centre ? (16 - (info->w / 2)) : 0;
        const int size_oy = op.centre ? (32 - info->h) : 0;
        const int pos_sy_adjust = op.ofs_y + info->oy + size_oy;
        const int pos_ey_adjust = pos_sy_adjust + (info->ey - info->sy);
        const int sy = pos_sy_adjust;
        int ey = pos_ey_adjust;
        if (op.ymax >= 0) {
            ey = std::min(op.ymax, ey);
        }
        if (sy >= ey) {
            continue;
        }
        const int x0 = op.ofs_x + info->ox + size_ox;
        const int src_x = info->sx;
        const int src_w = info->ex - info->sx;
        const int src_y = info->sy + (sy - pos_sy_adjust);
        const int src_h = ey - sy;
        const int cell_x = ox + (op.cx * dx);
        const int cell_y = oy + (op.cy * dx);
        if (dx == 32) {
            images.draw_crop(*img, RectPx{ cell_x + x0, cell_y + sy, src_w, src_h }, src_x, src_y, src_w, src_h, 1.f);
        } else {
            const int px0 = cell_x + static_cast<int>(std::lround(x0 * scale));
            const int py0 = cell_y + static_cast<int>(std::lround(sy * scale));
            const int px1 = cell_x + static_cast<int>(std::lround((x0 + src_w) * scale));
            const int py1 = cell_y + static_cast<int>(std::lround((sy + src_h) * scale));
            const float iw = static_cast<float>(img->width());
            const float ih = static_cast<float>(img->height());
            images.draw_region(*img, RectPx{ px0, py0, std::max(1, px1 - px0), std::max(1, py1 - py0) }, static_cast<float>(src_x) / iw,
                static_cast<float>(src_y) / ih, static_cast<float>(src_x + src_w) / iw, static_cast<float>(src_y + src_h) / ih, 1.f);
        }
    }
    ops.clear();
}

// ------------------------------------------------------------ 描く

std::string DcssTabs::tab_name(int tab) const
{
    static const char *const ja[kTabCount] = { "所持品", "呪文", "呪文の記憶", "能力", "モンスター", "スキル", "コマンド", "システムコマンド", "マップ移動" };
    static const char *const en[kTabCount] = { "Inventory", "Spells", "Memorisation", "Abilities", "Monsters", "Skills", "Commands", "System Commands", "Navigation" };
    if ((tab < 0) || (tab >= kTabCount)) {
        return std::string();
    }
    return this->japanese_ ? ja[tab] : en[tab];
}

void DcssTabs::load_heading_images()
{
    if (this->heading_images_loaded_ || this->heading_images_failed_) {
        return;
    }
    std::string err;
    bool ok = this->tab_selected_.load("assets/ui/dcss/tab_selected.png", err) && this->tab_unselected_.load("assets/ui/dcss/tab_unselected.png", err)
        && this->tab_mouseover_.load("assets/ui/dcss/tab_mouseover.png", err);
    for (int i = 0; ok && (i < kTabCount); ++i) {
        ok = this->tab_labels_[i].load(std::string("assets/ui/dcss/") + kTabLabelFiles[i] + ".png", err);
    }
    if (ok) {
        this->heading_images_loaded_ = true;
    } else {
        this->heading_images_failed_ = true;
        std::fprintf(stderr, "[hd2d] DCSS のタブの見出しの絵を読めません: %s\n", err.c_str());
    }
}

void DcssTabs::draw_tab_headings(const UiImagePainter &images, const Region &r)
{
    if (!this->heading_images_loaded_) {
        return;
    }
    for (const Heading &h : r.headings) {
        const bool selected = r.fixed || ((r.active == h.tab) && true);
        const bool over = (r.mouse_tab == h.tab);
        // 原作 `TabbedRegion::pack_buffers`: 選んだ > マウスを載せた > 選んでいない
        const UiImage &frame = selected ? this->tab_selected_ : (over ? this->tab_mouseover_ : this->tab_unselected_);
        const UiImage &label = this->tab_labels_[static_cast<std::size_t>(h.tab)];
        const RectPx dst{ h.x, h.y, h.size, h.size };
        if (h.size == kIcon) {
            images.draw_crop(frame, dst, 0, 0, kIcon, kIcon, 1.f);
            images.draw_crop(label, dst, 0, 0, kIcon, kIcon, 1.f);
        } else {
            images.draw_region(frame, dst, 0.f, 0.f, 1.f, 1.f, 1.f);
            images.draw_region(label, dst, 0.f, 0.f, 1.f, 1.f, 1.f);
        }
    }
}

std::string DcssTabs::description_for(int tab, const Cursor &cur, int mx, int my, bool small, int page)
{
    if ((cur.cx < 0) || (cur.cy < 0)) {
        return std::string();
    }
    int pg = page;
    const std::vector<DcssTabTile> items = this->items_for(tab, mx, my, small, pg);
    const int base = (tab == kTabItem) ? ((pg * mx * my) - (pg * 2)) : 0;
    const int ci = cur.cx + (cur.cy * mx) + base;
    if ((ci < 0) || (static_cast<std::size_t>(ci) >= items.size())) {
        return std::string();
    }
    const DcssTabTile &t = items[static_cast<std::size_t>(ci)];
    if (t.empty()) {
        return std::string();
    }
    if (tab == kTabMonster) {
        return (t.idx < static_cast<int>(this->data_.monsters.size())) ? this->data_.monsters[static_cast<std::size_t>(t.idx)].name : std::string();
    }
    if (tab == kTabItem) {
        int real = 0;
        for (const DcssTabTile &it : items) {
            if (it.idx != -1) {
                ++real;
            }
        }
        const bool next_button = (ci == ((((mx * my) - 2) * (pg + 1)) - 1 + 2)) && (real >= (mx * my));
        const bool prev_button = (pg > 0) && (ci == ((mx * my * pg) - (2 * pg)));
        if (next_button) {
            return this->japanese_ ? "次のページ" : "Next page";
        }
        if (prev_button) {
            return this->japanese_ ? "前のページ" : "Previous page";
        }
    }
    return t.name;
}

void DcssTabs::draw(const UiImagePainter &images, TextOverlay &label, int label_w, int label_h)
{
    (void)label_w;
    (void)label_h;
    if (!this->have_layout_) {
        return;
    }
    this->load_heading_images();
    const bool small = this->layout_.small_layout;
    std::vector<Op> ops;
    const TextColor white{ 1.f, 1.f, 1.f, 1.f };
    for (std::size_t ri = 0; ri < this->regions_.size(); ++ri) {
        Region &r = this->regions_[ri];
        this->draw_tab_headings(images, r);
        const int tab = r.fixed ? r.fixed_tab : r.active;
        const bool shown = (tab >= 0) && (tab < kTabCount) && !(r.deactivated && !r.fixed) && (r.fixed || this->tab_enabled_in_main(tab));
        std::string desc;
        if (shown && this->atlas_ok_ && this->data_.received) {
            int page = this->inv_page_;
            const Cursor cur = this->cursors_[static_cast<std::size_t>(tab)];
            if (tab == kTabMonster) {
                this->pack_monsters(ops, r.mx, r.my, small, cur);
            } else {
                const std::vector<DcssTabTile> items = this->items_for(tab, r.mx, r.my, small, page);
                if (tab == kTabItem) {
                    this->inv_page_ = page;
                }
                this->pack_tab(ops, tab, items, r.mx, r.my, small, page, cur);
            }
            this->draw_ops(images, ops, r.sx + r.ox, r.sy + r.oy, r.dx);
            desc = this->description_for(tab, cur, r.mx, r.my, small, page);
        }
        if ((r.mouse_tab != -1) && (r.mouse_tab >= 0) && (r.mouse_tab < kTabCount)) {
            // 原作 `TabbedRegion::draw_tag`: 見出しに載せたら、その名前を格子の上の 1 行に出す。
            desc = this->tab_name(r.mouse_tab);
        }
        if (!desc.empty()) {
            // 原作 `GridRegion::draw_desc`: 格子の頭の半マス上。小さい配置は区画の下端。
            const int x = r.sx + r.ox;
            const int y = small ? (this->origin_y_ + r.wy) : (r.sy + r.oy - (r.dx / 2));
            const UiProbeLayer probe_layer("dcss-tabdesc");
            label.draw(x, y - 1, desc, white);
        }
    }
}

// ------------------------------------------------------------ マウス

std::string DcssTabs::action_json(const char *a, int idx, int button, int mods)
{
    char buf[160];
    std::snprintf(buf, sizeof(buf), "{\"t\":\"dcss_action\",\"a\":\"%s\",\"i\":%d,\"b\":%d,\"m\":%d}", a, idx, button, mods);
    return buf;
}

bool DcssTabs::mouse_in_grid(const Region &r, int x, int y, int &cx, int &cy) const
{
    // 原作 `Region::mouse_pos`
    int px = x - r.ox - r.sx;
    int py = y - r.oy - r.sy;
    bool valid = (px >= 0) && (py >= 0);
    px /= std::max(1, r.dx);
    py /= std::max(1, r.dx);
    valid = valid && (px < r.mx) && (py < r.my);
    cx = px;
    cy = py;
    return valid;
}

int DcssTabs::mouseover_heading(const Region &r, int x, int y) const
{
    for (const Heading &h : r.headings) {
        if ((x >= h.x) && (x <= (h.x + h.size)) && (y >= h.y) && (y <= (h.y + h.hit_h))) {
            return h.tab;
        }
    }
    return -1;
}

void DcssTabs::activate_tab(Region &r, int tab, std::vector<DcssTabOutgoing> *out)
{
    if ((tab < 0) || (tab >= kTabCount)) {
        return;
    }
    const bool small = this->layout_.small_layout;
    if (!r.fixed && !this->tab_enabled_in_main(tab)) {
        return;
    }
    if (!r.fixed && (r.active == tab) && small && !r.deactivated) {
        // 原作 `deactivate_tab`
        r.deactivated = true;
        r.active = kTabItem;
        ++this->serial_;
        return;
    }
    if (!r.fixed) {
        r.active = tab;
    }
    r.deactivated = false;
    if (tab == kTabItem) {
        this->inv_page_ = 0; // switch to first page on activation
    }
    this->cursors_[static_cast<std::size_t>(tab)] = Cursor{};
    ++this->serial_;
    // 原作 `reg->activate()`: 持っていないときのメッセージ（持ち物・呪文・覚える呪文・能力）。コアが出す。
    if ((out != nullptr) && (tab >= kTabItem) && (tab <= kTabAbility)) {
        out->push_back(DcssTabOutgoing{ false, action_json("act", tab, 1, 0) });
    }
}

bool DcssTabs::on_mouse(const DcssTabMouse &ev)
{
    if (!this->have_layout_ || !this->data_.received) {
        return false;
    }
    const bool press = (ev.kind == DcssTabMouse::Kind::Press);
    // 原作は、領域の並びの後ろ（固定表示のタブ）から先に渡し、最初に「処理した」ものが出たらやめる。
    for (int ri = static_cast<int>(this->regions_.size()) - 1; ri >= 0; --ri) {
        Region &r = this->regions_[static_cast<std::size_t>(ri)];
        // `TabbedRegion::handle_mouse`: 命令待ち（か全体地図の画面）でなければ何もしない。
        if (!this->command_mode()) {
            continue;
        }
        const int mouse_tab = this->mouseover_heading(r, ev.x, ev.y);
        if (mouse_tab != r.mouse_tab) {
            r.mouse_tab = mouse_tab;
            ++this->serial_;
        }
        if (mouse_tab != -1) {
            if (press) {
                this->activate_tab(r, mouse_tab, &this->outgoing_);
                return true; // prevent clicking on tab from 'bubbling up' to other regions
            }
            continue;
        }
        // Otherwise, pass input to the active tab.
        const int tab = r.fixed ? r.fixed_tab : r.active;
        const bool shown = (tab >= 0) && (tab < kTabCount) && !(r.deactivated && !r.fixed) && (r.fixed || this->tab_enabled_in_main(tab));
        if (!shown) {
            continue;
        }
        const bool small = this->layout_.small_layout;
        int cx = 0, cy = 0;
        Cursor &cur = this->cursors_[static_cast<std::size_t>(tab)];
        if (!this->mouse_in_grid(r, ev.x, ev.y, cx, cy)) {
            cur = Cursor{};
            continue;
        }
        if ((cx != cur.cx) || (cy != cur.cy)) {
            cur.cx = cx;
            cur.cy = cy;
            ++this->serial_;
        }
        if (!press) {
            continue;
        }
        int page = this->inv_page_;
        const std::vector<DcssTabTile> items = this->items_for(tab, r.mx, r.my, small, page);
        const int base = (tab == kTabItem) ? ((page * r.mx * r.my) - (page * 2)) : 0;
        const int ci = cx + (cy * r.mx) + base;
        if ((ci < 0) || (static_cast<std::size_t>(ci) >= items.size()) || items[static_cast<std::size_t>(ci)].empty()) {
            continue;
        }
        const DcssTabTile &it = items[static_cast<std::size_t>(ci)];
        const int button = ev.button;
        if (tab == kTabItem) {
            int real = 0;
            for (const DcssTabTile &t : items) {
                if (t.idx != -1) {
                    ++real;
                }
            }
            const bool next_button = (ci == ((((r.mx * r.my) - 2) * (page + 1)) - 1 + 2)) && (real >= (r.mx * r.my));
            if (next_button && (button == 1)) {
                ++this->inv_page_;
                ++this->serial_;
                return true;
            }
            if ((cx == 0) && (cy == 0) && (page > 0) && (button == 1)) {
                --this->inv_page_;
                ++this->serial_;
                return true;
            }
            if (this->data_.map_display) {
                return true;
            }
            const bool on_floor = (it.flag & kTfFloor) != 0;
            this->outgoing_.push_back(DcssTabOutgoing{ false, action_json(on_floor ? "floor" : "inv", it.idx, button, ev.mods) });
            return true;
        }
        if (tab == kTabMonster) {
            if (this->input_mode_ != 1) {
                continue; // `mouse_control::current_mode() != MOUSE_MODE_COMMAND`
            }
            if (it.idx >= static_cast<int>(this->data_.monsters.size())) {
                continue;
            }
            const DcssTabMonster &mon = this->data_.monsters[static_cast<std::size_t>(it.idx)];
            if (!mon.in_sight) {
                continue;
            }
            // そのマスへのクリックと同じ（左: 旅・攻撃、右: 見る）。ウェブタイル版の `click_cell`。
            char buf[128];
            std::snprintf(buf, sizeof(buf), "{\"msg\":\"click_cell\",\"x\":%d,\"y\":%d,\"button\":%d}", mon.rel_x, mon.rel_y, button == 3 ? 3 : 1);
            this->outgoing_.push_back(DcssTabOutgoing{ true, buf });
            return true;
        }
        if ((tab == kTabSystemCommand) && (it.idx == kLocalKeyboardIdx)) {
            if (button == 1) {
                this->keyboard_toggle_ = true; // コアへは送らない（画面のキーボードの出し入れ）
            }
            return true;
        }
        const char *a = nullptr;
        switch (tab) {
        case kTabSpell: a = "spl"; break;
        case kTabMemorise: a = "mem"; break;
        case kTabAbility: a = "abl"; break;
        case kTabSkill: a = "skl"; break;
        default: a = "cmd"; break;
        }
        this->outgoing_.push_back(DcssTabOutgoing{ false, action_json(a, it.idx, button, ev.mods) });
        return true;
    }
    return false;
}

std::vector<DcssTabOutgoing> DcssTabs::take_outgoing()
{
    std::vector<DcssTabOutgoing> out;
    out.swap(this->outgoing_);
    return out;
}

std::string DcssTabs::describe() const
{
    std::string s = "dcss-tabs atlas=" + std::string(this->atlas_ok_ ? "1" : "0") + " data=" + (this->data_.received ? "1" : "0");
    char buf[160];
    std::snprintf(buf, sizeof(buf), " inv=%zu floor=%zu spl=%zu mem=%zu abl=%zu mon=%zu skl=%zu cmd=%zu/%zu/%zu", this->data_.inv.size(), this->data_.floor.size(),
        this->data_.spells.size(), this->data_.memorise.size(), this->data_.abilities.size(), this->data_.monsters.size(), this->data_.skills.size(),
        this->data_.commands[0].size(), this->data_.commands[1].size(), this->data_.commands[2].size());
    s += buf;
    int eq = 0;
    for (const DcssTabTile &t : this->data_.inv) {
        if ((t.flag & kTfEquip) != 0) {
            ++eq;
        }
    }
    long long sklsum = 0;
    for (const DcssTabTile &t : this->data_.skills) {
        sklsum += static_cast<long long>(t.tile) * (1 + (t.flag & kTfInvalid ? 1 : 0));
    }
    std::snprintf(buf, sizeof(buf), " eq=%d sklsum=%lld page=%d map=%d", eq, sklsum, this->inv_page_, this->data_.map_display ? 1 : 0);
    s += buf;
    for (std::size_t i = 0; i < this->regions_.size(); ++i) {
        const Region &r = this->regions_[i];
        std::snprintf(buf, sizeof(buf), " | region%zu %s tab=%d grid=%d,%d %dx%d dx=%d deact=%d heads=", i, r.fixed ? "fixed" : "main", r.fixed ? r.fixed_tab : r.active,
            r.sx + r.ox, r.sy + r.oy, r.mx, r.my, r.dx, r.deactivated ? 1 : 0);
        s += buf;
        for (const Heading &h : r.headings) {
            std::snprintf(buf, sizeof(buf), "%d@%d,%d ", h.tab, h.x, h.y);
            s += buf;
        }
    }
    return s;
}

} // namespace hd2d::dcss
