// DCSS コア: 原作のタイル（怪物・アイテム・プレイヤーの人形）をボクセルの板（.vox + .jsonc）にして、画面が目録で引けるようにする。
//
// 決めたこと:
//   - 怪物・アイテム・プレイヤーは原作のタイルをボクセルの板にする。32 ドットを 2 倍の 64 にする（1 画素 = 2x2 ボクセル）。
//   - プレイヤーの見た目に装備を映す。原作が送る「重ねるタイルの番号の並び」（t.doll・t.mcache）を重ねて 1 枚にし、すぐ板に変える。
//   - 板のファイルを書き終えてから、絵の目録（asset_manifest）に新しい番号を足して送り直す。同じ並びは同じ番号を使い回す。
//
// **全部を前もって作るのではなく、出たものだけその場で作る**（出どころ: 怪物 1,500・アイテム 1,100 のほか、プレイヤーの装備の組み合わせが
// 数十万通りになり、合成は前もって作れない。実行時の作り方が必要なので、単独のタイルも同じ道に通して、作り方を 1 つにした）。
// 作った板は <データの根>/dcss/lib/slabs/ に内容のハッシュを名前にして書く。起動の間はキャッシュとして使い回し、
// **次の起動のはじめに消す**（起動をまたいでは残さない。Bank::init の clear_slab_dir）。
//
// 画の材料は、原作のタイル版が使う画像ページ（tools/dcss/dcss_tile_atlas.py が作る main.png・player.png と tiles.tsv）。
// 描き方は原作のブラウザ側（webserver/game_data/static/cell_renderer.js の draw_tile）と同じ:
//   位置 x = ofs_x + info.ox + (16 - info.w / 2)、y = ofs_y + info.oy + (32 - info.h)。背の高いタイル（32x48）は上へはみ出す。
//   y_max があれば、その行から下は描かない。
// 板の作り方は tools/voxel/tile_to_slab.py と同じ（アルファのしきい値 110・厚み 4・接地・色は 255 まで）。`DcssCore.exe --dcss-slab-bake` が
// 同じ絵から両方で作った .vox のバイト列の一致を見る。
//
// このヘッダは dcss_frame.cpp だけが取り込む（C++20。原作のヘッダは引かない。ビルドの定義に足す物は無い）。
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

//! 原作が付属の zlib（contrib/zlib）を同じ実行ファイルに入れているので、その uncompress を使う（PNG の IDAT を伸ばすだけ）。
extern "C" int uncompress(unsigned char *dest, unsigned long *destLen, const unsigned char *source, unsigned long sourceLen);

namespace dcss_slab {

//! 板の作り方を変えたら上げる（置き場の古い板が使われなくなる）。
constexpr int kBakeVersion = 2;
constexpr int kCell = 32;          //!< 原作のタイル 1 枚の画素（TILE_X・TILE_Y）
constexpr int kSide = 64;          //!< 板の一辺（ボクセル）。1 画素 = 2x2
constexpr int kDepth = kSide / 16; //!< 厚み（ボクセル）
constexpr int kAlphaCutoff = 110;  //!< tile_to_slab.py の ALPHA_CUTOFF
constexpr int kTopLimit = -32;     //!< 上へはみ出してよい画素（2 マス分まで）

struct Layer {
    int tile = 0;  //!< 原作のタイル番号（TILE_* / TILEP_*。旗は落としたもの）
    int ofs_x = 0; //!< mcache の x のずれ
    int ofs_y = 0;
    int y_max = 0; //!< 0 = 切らない。それ以外はこの行（セル上端から数えて）から下は描かない
    //! 真なら重ね物（状態のアイコン・悪魔の等級。段 3）。板の接地（いちばん下の不透明な行）は重ね物を除いて決める
    //! （重ね物が足元より下へはみ出しても、怪物の板が浮かない。はみ出した分は落とす）。
    bool overlay = false;
};

struct TileInfo {
    int w = 0, h = 0, ox = 0, oy = 0, sx = 0, sy = 0, ex = 0, ey = 0;
    char page = 0; //!< 'm' main.png / 'p' player.png / 'i' icons.png（状態のアイコン）
};

struct Image {
    int w = 0, h = 0;
    std::vector<std::uint8_t> rgba;
};

//! 合成した 1 枚の絵（幅は kCell。高さは上へはみ出した分だけ増える）。
struct Canvas {
    int w = kCell;
    int h = 0;
    int top = 0; //!< 最初の行の、セル上端から数えた位置（0 以下）
    int ground_row = -1; //!< 重ね物を除いた、いちばん下の不透明な行（この絵の中の行。-1 = 重ね物が無いので全体から決める）
    int fade = 0; //!< 半透明の種類（kFadeTrans・kFadeSubmerged の和。0 = 不透明）。`bake` がボクセルの間引きで出す
    std::vector<std::uint8_t> exempt; //!< 画素ごとの 1 = 半透明にしない（重ね物＝状態のアイコン。fade が 0 のときは空）
    std::vector<std::uint8_t> rgba;
};

//! 8 ビット RGBA・飛び越しなしの PNG だけを読む（tools/dcss/dcss_tile_atlas.py が書く画像ページの形）。読めなければ false。
inline bool decode_png(const std::string &bytes, Image &out, std::string &err)
{
    static const unsigned char kSig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    if (bytes.size() < 33 || std::memcmp(bytes.data(), kSig, 8) != 0) {
        err = "PNG ではない";
        return false;
    }
    const auto be32 = [&](std::size_t at) {
        return (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at])) << 24) | (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + 1])) << 16)
            | (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + 2])) << 8) | static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + 3]));
    };
    int w = 0, h = 0;
    std::string idat;
    std::size_t at = 8;
    while (at + 12 <= bytes.size()) {
        const std::uint32_t len = be32(at);
        const std::string type = bytes.substr(at + 4, 4);
        if (at + 12 + len > bytes.size()) break;
        if (type == "IHDR") {
            w = static_cast<int>(be32(at + 8));
            h = static_cast<int>(be32(at + 12));
            const unsigned char depth = static_cast<unsigned char>(bytes[at + 16]);
            const unsigned char color = static_cast<unsigned char>(bytes[at + 17]);
            const unsigned char interlace = static_cast<unsigned char>(bytes[at + 20]);
            if (depth != 8 || color != 6 || interlace != 0) {
                err = "8 ビット RGBA・飛び越しなしの PNG ではない";
                return false;
            }
        } else if (type == "IDAT") {
            idat.append(bytes, at + 8, len);
        } else if (type == "IEND") {
            break;
        }
        at += 12 + static_cast<std::size_t>(len);
    }
    if (w <= 0 || h <= 0 || idat.empty()) {
        err = "PNG の中身が無い";
        return false;
    }
    const std::size_t stride = static_cast<std::size_t>(w) * 4;
    std::vector<unsigned char> raw((stride + 1) * static_cast<std::size_t>(h));
    unsigned long raw_len = static_cast<unsigned long>(raw.size());
    if (uncompress(raw.data(), &raw_len, reinterpret_cast<const unsigned char *>(idat.data()), static_cast<unsigned long>(idat.size())) != 0 || raw_len != raw.size()) {
        err = "PNG を伸ばせない";
        return false;
    }
    out.w = w;
    out.h = h;
    out.rgba.assign(stride * static_cast<std::size_t>(h), 0);
    for (int y = 0; y < h; ++y) {
        const unsigned char filter = raw[static_cast<std::size_t>(y) * (stride + 1)];
        const unsigned char *src = &raw[static_cast<std::size_t>(y) * (stride + 1) + 1];
        unsigned char *dst = &out.rgba[static_cast<std::size_t>(y) * stride];
        const unsigned char *up = y > 0 ? dst - stride : nullptr;
        for (std::size_t i = 0; i < stride; ++i) {
            const int a = i >= 4 ? dst[i - 4] : 0;
            const int b = up != nullptr ? up[i] : 0;
            const int c = (i >= 4 && up != nullptr) ? up[i - 4] : 0;
            int pred = 0;
            switch (filter) {
            case 0: pred = 0; break;
            case 1: pred = a; break;
            case 2: pred = b; break;
            case 3: pred = (a + b) / 2; break;
            case 4: {
                const int p = a + b - c;
                const int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
                pred = (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
                break;
            }
            default:
                err = "PNG のフィルタが不明";
                return false;
            }
            dst[i] = static_cast<unsigned char>(src[i] + pred);
        }
    }
    return true;
}

inline bool load_png_file(const std::filesystem::path &path, Image &out, std::string &err)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        err = "画像がありません: " + path.string();
        return false;
    }
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (!decode_png(bytes, out, err)) {
        err += ": " + path.string();
        return false;
    }
    return true;
}

class Atlas {
public:
    bool load(const std::string &dir, std::string &err)
    {
        ok_ = false;
        std::ifstream in(std::filesystem::path(dir) / "tiles.tsv", std::ios::binary);
        if (!in) {
            err = "tiles.tsv がありません: " + dir;
            return false;
        }
        std::map<std::string, int> consts;
        std::vector<std::pair<int, TileInfo>> rows;
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream ss(line);
            std::string first;
            ss >> first;
            if (first == "const") {
                std::string name;
                int value = 0;
                ss >> name >> value;
                consts[name] = value;
                continue;
            }
            TileInfo t;
            char page = 0;
            const int idx = std::atoi(first.c_str());
            ss >> page >> t.w >> t.h >> t.ox >> t.oy >> t.sx >> t.sy >> t.ex >> t.ey;
            if (!ss) continue;
            t.page = page;
            rows.emplace_back(idx, t);
        }
        for (const char *k : { "TILE_FEAT_MAX", "TILE_MAIN_MAX", "TILEP_PLAYER_MAX", "TILEP_PLAYER", "TILEP_MCACHE_START" }) {
            if (consts.find(k) == consts.end()) {
                err = std::string("tiles.tsv に ") + k + " がありません";
                return false;
            }
        }
        feat_max = consts["TILE_FEAT_MAX"];
        main_max = consts["TILE_MAIN_MAX"];
        player_max = consts["TILEP_PLAYER_MAX"];
        tilep_player = consts["TILEP_PLAYER"];
        mcache_start = consts["TILEP_MCACHE_START"];
        parchment_low = consts.count("TILE_PARCHMENT_LOW") ? consts["TILE_PARCHMENT_LOW"] : 0;
        parchment_high = consts.count("TILE_PARCHMENT_HIGH") ? consts["TILE_PARCHMENT_HIGH"] : 0;
        info_.assign(static_cast<std::size_t>(std::max(0, player_max - feat_max)), TileInfo{});
        for (const auto &[idx, t] : rows) {
            if (idx >= feat_max && idx < player_max) info_[static_cast<std::size_t>(idx - feat_max)] = t;
        }
        if (!load_png_file(std::filesystem::path(dir) / "main.png", main_, err)) return false;
        if (!load_png_file(std::filesystem::path(dir) / "player.png", player_, err)) return false;
        load_icons(dir); // 状態のアイコン（任意。無ければアイコンは焼き込まない）
        ok_ = true;
        return true;
    }

    //! `icons.tsv`（アイコンのページの切り出しと `const`）・`icons.png`・`status_icons.tsv`（状態のアイコンの幅）を読む。どれか欠けたら全部無かったことにする。
    bool load_icons(const std::string &dir)
    {
        icons_ok_ = false;
        icon_consts_.clear();
        icon_sizes_.clear();
        icon_info_.clear();
        std::string err;
        std::ifstream in(std::filesystem::path(dir) / "icons.tsv", std::ios::binary);
        if (!in) return false;
        std::vector<std::pair<int, TileInfo>> rows;
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream ss(line);
            std::string first;
            ss >> first;
            if (first == "const") {
                std::string name;
                int value = 0;
                ss >> name >> value;
                icon_consts_[name] = value;
                continue;
            }
            TileInfo t;
            char page = 0;
            const int idx = std::atoi(first.c_str());
            ss >> page >> t.w >> t.h >> t.ox >> t.oy >> t.sx >> t.sy >> t.ex >> t.ey;
            if (!ss) continue;
            t.page = 'i';
            rows.emplace_back(idx, t);
        }
        if (!icon_consts_.count("TILEG_GUI_MAX") || !icon_consts_.count("TILEI_ICONS_MAX")) return false;
        gui_max = icon_consts_["TILEG_GUI_MAX"];
        icons_max = icon_consts_["TILEI_ICONS_MAX"];
        icon_info_.assign(static_cast<std::size_t>(std::max(0, icons_max - gui_max)), TileInfo{});
        for (const auto &[idx, t] : rows) {
            if (idx >= gui_max && idx < icons_max) icon_info_[static_cast<std::size_t>(idx - gui_max)] = t;
        }
        std::ifstream sizes(std::filesystem::path(dir) / "status_icons.tsv", std::ios::binary);
        if (!sizes) return false;
        while (std::getline(sizes, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream ss(line);
            int tile = 0, size = 0;
            if (ss >> tile >> size) icon_sizes_[tile] = size;
        }
        if (!load_png_file(std::filesystem::path(dir) / "icons.png", icons_, err)) return false;
        icons_ok_ = true;
        return true;
    }
    bool icons_ok() const { return icons_ok_; }
    //! `TILEI_*` の番号（`icons.tsv` の `const`）。無ければ -1。
    int icon(const std::string &name) const
    {
        const auto it = icon_consts_.find(name);
        return it == icon_consts_.end() ? -1 : it->second;
    }
    //! 原作の `status_icon_size`。表に無ければ -1（描かない）。
    int status_icon_size(int tile) const
    {
        const auto it = icon_sizes_.find(tile);
        return it == icon_sizes_.end() ? -1 : it->second;
    }

    bool ok() const { return ok_; }
    const TileInfo *info(int tile) const
    {
        if (icons_ok_ && tile >= gui_max && tile < icons_max) {
            const TileInfo &t = icon_info_[static_cast<std::size_t>(tile - gui_max)];
            return t.w > 0 ? &t : nullptr;
        }
        if (tile < feat_max || tile >= player_max) return nullptr;
        const TileInfo &t = info_[static_cast<std::size_t>(tile - feat_max)];
        return t.w > 0 ? &t : nullptr;
    }
    const Image &page(char p) const { return p == 'i' ? icons_ : (p == 'm' ? main_ : player_); }

    int gui_max = 0, icons_max = 0;
    int feat_max = 0, main_max = 0, player_max = 0, tilep_player = 0, mcache_start = 0, parchment_low = 0, parchment_high = 0;

private:
    bool ok_ = false;
    bool icons_ok_ = false;
    std::vector<TileInfo> info_;
    std::vector<TileInfo> icon_info_;
    std::map<std::string, int> icon_consts_;
    std::map<int, int> icon_sizes_;
    Image main_, player_, icons_;
};

// ------------------------------------------------------------ 半透明（ボクセルの間引き）

//! 半透明の種類（`resolve` の `fade`。足して使う）。
constexpr int kFadeTrans = 1;     //!< 半透明の怪物（t.trans。プレイヤーの幽霊など）
constexpr int kFadeSubmerged = 2; //!< 水に浸かっている（bg が水で、飛んでいない）。足元に近い下の部分が薄くなる
constexpr int kSubmergedRow = 20; //!< 水に浸かった絵の、薄くなり始める行（原作の set_submerged_clip。セル上端から数える）

//! 原作の `cell_renderer.js` が掛ける透明度（`globalAlpha`）。行はセル上端から数える（上へはみ出した行は負）。
//! 半透明の怪物 0.55／水に浸かる: 上 1.0（半透明なら 0.5）・下 0.3（半透明なら 0.1）。
inline double fade_alpha(int fade, int row_in_cell)
{
    const bool trans = (fade & kFadeTrans) != 0;
    if ((fade & kFadeSubmerged) != 0) {
        const bool lower = row_in_cell >= kSubmergedRow;
        if (trans) return lower ? 0.1 : 0.5;
        return lower ? 0.3 : 1.0;
    }
    return trans ? 0.55 : 1.0;
}

//! 4x4 の並び順ディザの閾値（0〜15）。
inline int bayer4(int x, int y)
{
    static const int kBayer[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
    return kBayer[(y & 3) * 4 + (x & 3)];
}

// ------------------------------------------------------------ 重ねる

//! 原作の draw_tile と同じ位置へ、層を下から順に重ねて 1 枚にする。不透明な画素が 1 つも無ければ false。
//! `fade` は半透明の種類（`bake` が掛ける）。重ね物（状態のアイコン）の画素は掛けない（原作はアイコンを別に描く）ので、`exempt` に覚える。
inline bool compose(const Atlas &atlas, const std::vector<Layer> &layers, Canvas &out, int fade = 0)
{
    struct Placed {
        const TileInfo *info;
        const Layer *layer;
        int x0, y0;
    };
    bool any_overlay = false;
    for (const Layer &l : layers) any_overlay = any_overlay || l.overlay;
    std::vector<Placed> placed;
    int top = 0;
    for (const Layer &l : layers) {
        const TileInfo *t = atlas.info(l.tile);
        if (t == nullptr) continue;
        // JS: 32 / 2 - info.w / 2（小数）に ofs と info.ox を足して Math.floor
        const int x0 = static_cast<int>(std::floor(static_cast<double>(l.ofs_x + t->ox) + 16.0 - t->w / 2.0));
        const int y0 = l.ofs_y + t->oy + (kCell - t->h);
        placed.push_back({ t, &l, x0, y0 });
        top = std::min(top, y0);
    }
    if (placed.empty()) return false;
    top = std::max(top, kTopLimit);
    out.w = kCell;
    out.top = top;
    out.h = kCell - top;
    out.ground_row = -1;
    out.rgba.assign(static_cast<std::size_t>(out.w) * out.h * 4, 0);
    bool ground_fixed = !any_overlay;
    out.fade = fade;
    if (fade != 0) out.exempt.assign(static_cast<std::size_t>(out.w) * out.h, 0);
    for (const Placed &p : placed) {
        if (p.layer->overlay && !ground_fixed) {
            // 重ね物に入る前に、いまの絵（重ね物を除く）のいちばん下の不透明な行を覚える。
            ground_fixed = true;
            for (int row = out.h - 1; row >= 0 && out.ground_row < 0; --row) {
                for (int col = 0; col < out.w; ++col) {
                    if (out.rgba[(static_cast<std::size_t>(row) * out.w + col) * 4 + 3] >= kAlphaCutoff) {
                        out.ground_row = row;
                        break;
                    }
                }
            }
        }
        const Image &img = atlas.page(p.info->page);
        const int tw = p.info->ex - p.info->sx;
        const int th = p.info->ey - p.info->sy;
        for (int ty = 0; ty < th; ++ty) {
            const int dy = p.y0 + ty;
            if (p.layer->y_max > 0 && dy >= p.layer->y_max) break;
            if (dy < top || dy >= kCell) continue;
            for (int tx = 0; tx < tw; ++tx) {
                const int dx = p.x0 + tx;
                if (dx < 0 || dx >= kCell) continue;
                const int sx = p.info->sx + tx;
                const int sy = p.info->sy + ty;
                if (sx < 0 || sy < 0 || sx >= img.w || sy >= img.h) continue;
                const std::uint8_t *s = &img.rgba[(static_cast<std::size_t>(sy) * img.w + sx) * 4];
                const int sa = s[3];
                if (sa == 0) continue;
                if (fade != 0 && p.layer->overlay) out.exempt[static_cast<std::size_t>(dy - top) * out.w + dx] = 1; // 重ね物の画素は半透明にしない
                std::uint8_t *d = &out.rgba[(static_cast<std::size_t>(dy - top) * out.w + dx) * 4];
                const int da = d[3];
                if (sa == 255 || da == 0) {
                    std::memcpy(d, s, 4);
                    continue;
                }
                // 通常の「上に重ねる」（アルファは 0〜255）
                const int oa = sa + da * (255 - sa) / 255;
                for (int c = 0; c < 3; ++c) {
                    d[c] = static_cast<std::uint8_t>((s[c] * sa + d[c] * da * (255 - sa) / 255) / std::max(1, oa));
                }
                d[3] = static_cast<std::uint8_t>(oa);
            }
        }
    }
    bool any = false;
    for (std::size_t i = 3; i < out.rgba.size(); i += 4) {
        if (out.rgba[i] >= kAlphaCutoff) {
            any = true;
            break;
        }
    }
    return any;
}

// ------------------------------------------------------------ 板にする（tools/voxel/tile_to_slab.py と同じ規則）

namespace detail {

using Rgb = std::array<std::uint8_t, 3>;

struct ColorCount {
    Rgb c;
    int n;
};

//! 255 色へ詰める（メディアンカット。個数で重みを付ける）。戻りは元の色（昇順）ごとの代表の色。
inline std::vector<Rgb> median_cut(const std::vector<ColorCount> &colors, int k)
{
    std::vector<std::vector<ColorCount>> boxes;
    boxes.push_back(colors);
    const auto widest = [](const std::vector<ColorCount> &box, int &axis) {
        int best = -1;
        axis = 0;
        for (int a = 0; a < 3; ++a) {
            int lo = 255, hi = 0;
            for (const ColorCount &c : box) {
                lo = std::min<int>(lo, c.c[static_cast<std::size_t>(a)]);
                hi = std::max<int>(hi, c.c[static_cast<std::size_t>(a)]);
            }
            if (hi - lo > best) {
                best = hi - lo;
                axis = a;
            }
        }
        return best;
    };
    while (static_cast<int>(boxes.size()) < k) {
        int pick = -1, pick_range = 0, pick_axis = 0;
        for (std::size_t i = 0; i < boxes.size(); ++i) {
            if (boxes[i].size() < 2) continue;
            int axis = 0;
            const int range = widest(boxes[i], axis);
            if (range > pick_range) {
                pick = static_cast<int>(i);
                pick_range = range;
                pick_axis = axis;
            }
        }
        if (pick < 0) break;
        std::vector<ColorCount> box = std::move(boxes[static_cast<std::size_t>(pick)]);
        std::sort(box.begin(), box.end(), [&](const ColorCount &a, const ColorCount &b) {
            return a.c[static_cast<std::size_t>(pick_axis)] < b.c[static_cast<std::size_t>(pick_axis)];
        });
        long long total = 0;
        for (const ColorCount &c : box) total += c.n;
        long long run = 0;
        std::size_t cut = 1;
        for (std::size_t i = 0; i < box.size(); ++i) {
            run += box[i].n;
            if (run * 2 >= total) {
                cut = std::min(box.size() - 1, i + 1);
                break;
            }
        }
        cut = std::max<std::size_t>(1, cut);
        boxes[static_cast<std::size_t>(pick)] = std::vector<ColorCount>(box.begin(), box.begin() + static_cast<std::ptrdiff_t>(cut));
        boxes.push_back(std::vector<ColorCount>(box.begin() + static_cast<std::ptrdiff_t>(cut), box.end()));
    }
    std::map<std::uint32_t, Rgb> rep;
    for (const auto &box : boxes) {
        long long sum[3] = { 0, 0, 0 }, total = 0;
        for (const ColorCount &c : box) {
            for (int a = 0; a < 3; ++a) sum[a] += static_cast<long long>(c.c[static_cast<std::size_t>(a)]) * c.n;
            total += c.n;
        }
        Rgb mean{};
        for (int a = 0; a < 3; ++a) mean[static_cast<std::size_t>(a)] = static_cast<std::uint8_t>((sum[a] + total / 2) / std::max<long long>(1, total));
        for (const ColorCount &c : box) rep[(static_cast<std::uint32_t>(c.c[0]) << 16) | (static_cast<std::uint32_t>(c.c[1]) << 8) | c.c[2]] = mean;
    }
    std::vector<Rgb> out;
    out.reserve(colors.size());
    for (const ColorCount &c : colors) out.push_back(rep[(static_cast<std::uint32_t>(c.c[0]) << 16) | (static_cast<std::uint32_t>(c.c[1]) << 8) | c.c[2]]);
    return out;
}

inline void put_i32(std::string &s, std::int32_t v)
{
    for (int i = 0; i < 4; ++i) s.push_back(static_cast<char>((static_cast<std::uint32_t>(v) >> (8 * i)) & 0xFF));
}

inline void put_str(std::string &s, const std::string &t)
{
    put_i32(s, static_cast<std::int32_t>(t.size()));
    s += t;
}

inline std::string chunk(const char *id, const std::string &content, const std::string &children = std::string())
{
    std::string s(id, 4);
    put_i32(s, static_cast<std::int32_t>(content.size()));
    put_i32(s, static_cast<std::int32_t>(children.size()));
    s += content;
    s += children;
    return s;
}

} // namespace detail

struct Baked {
    std::string vox;
    std::string jsonc;
    int voxels = 0;
    int colors = 0;
    int reduced_from = 0; //!< 255 色を超えて詰めたときの元の色数（0 = 詰めていない）
};

//! 絵（幅 32・高さ h）から .vox と .jsonc の中身を作る。作れなければ false。
inline bool bake(const Canvas &canvas, const std::string &name, Baked &out)
{
    const int W = canvas.w * 2;
    const int H = canvas.h * 2;
    // 1 画素 = 2x2。アルファは 110 でしきい値を振り、透明な画素の色は 0 にする。
    std::vector<std::uint8_t> px(static_cast<std::size_t>(W) * H * 4, 0);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const std::uint8_t *s = &canvas.rgba[(static_cast<std::size_t>(y / 2) * canvas.w + x / 2) * 4];
            if (s[3] < kAlphaCutoff) continue;
            std::uint8_t *d = &px[(static_cast<std::size_t>(y) * W + x) * 4];
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
            d[3] = 255;
        }
    }
    // 半透明: 間引く前の絵で接地の行を決め（板が沈まない）、そのあと 1 ボクセルずつ並び順ディザで間引く（間引く位置は 4 層とも同じなので向こうが透ける）。
    int bottom_before_fade = -1;
    if (canvas.fade != 0) {
        for (int y = H - 1; y >= 0 && bottom_before_fade < 0; --y) {
            for (int x = 0; x < W; ++x) {
                if (px[(static_cast<std::size_t>(y) * W + x) * 4 + 3] != 0) {
                    bottom_before_fade = y;
                    break;
                }
            }
        }
        for (int y = 0; y < H; ++y) {
            const double alpha = fade_alpha(canvas.fade, y / 2 + canvas.top);
            if (alpha >= 1.0) continue;
            const int keep_below = static_cast<int>(alpha * 16.0 + 0.5); // 0〜16。並びの値がこれ未満のボクセルを残す
            for (int x = 0; x < W; ++x) {
                if (!canvas.exempt.empty() && canvas.exempt[static_cast<std::size_t>(y / 2) * canvas.w + x / 2] != 0) continue;
                if (bayer4(x, y) < keep_below) continue;
                std::uint8_t *d = &px[(static_cast<std::size_t>(y) * W + x) * 4];
                d[0] = d[1] = d[2] = d[3] = 0;
            }
        }
    }
    // 色。固有の色を昇順（r, g, b）に並べる（np.unique と同じ）。
    std::map<std::uint32_t, int> count;
    int bottom = -1;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const std::uint8_t *s = &px[(static_cast<std::size_t>(y) * W + x) * 4];
            if (s[3] == 0) continue;
            count[(static_cast<std::uint32_t>(s[0]) << 16) | (static_cast<std::uint32_t>(s[1]) << 8) | s[2]]++;
            bottom = y;
        }
    }
    if (count.empty()) return false;
    // 重ね物（状態のアイコン）があるときは、接地を重ね物を除いた絵のいちばん下の行にする（行 r は 2 倍の絵の行 2r・2r+1）。
    if (canvas.ground_row >= 0) bottom = canvas.ground_row * 2 + 1;
    else if (bottom_before_fade >= 0) bottom = bottom_before_fade;
    std::vector<detail::ColorCount> colors;
    colors.reserve(count.size());
    for (const auto &[rgb, n] : count) colors.push_back({ { static_cast<std::uint8_t>(rgb >> 16), static_cast<std::uint8_t>(rgb >> 8), static_cast<std::uint8_t>(rgb) }, n });
    std::vector<detail::Rgb> rep; // 固有の色ごとの代表の色
    out.reduced_from = 0;
    if (colors.size() > 255) {
        out.reduced_from = static_cast<int>(colors.size());
        rep = detail::median_cut(colors, 255);
    } else {
        for (const auto &c : colors) rep.push_back(c.c);
    }
    // パレット（代表の色の固有値を昇順）。索引は 1 から。
    std::map<std::uint32_t, int> palette_index;
    for (const auto &c : rep) palette_index[(static_cast<std::uint32_t>(c[0]) << 16) | (static_cast<std::uint32_t>(c[1]) << 8) | c[2]] = 0;
    int next = 1;
    for (auto &kv : palette_index) kv.second = next++;
    std::map<std::uint32_t, int> color_to_index; // 元の色 -> パレット索引
    for (std::size_t i = 0; i < colors.size(); ++i) {
        const auto &c = colors[i].c;
        const auto &r = rep[i];
        color_to_index[(static_cast<std::uint32_t>(c[0]) << 16) | (static_cast<std::uint32_t>(c[1]) << 8) | c[2]]
            = palette_index[(static_cast<std::uint32_t>(r[0]) << 16) | (static_cast<std::uint32_t>(r[1]) << 8) | r[2]];
    }
    out.colors = static_cast<int>(palette_index.size());

    // ボクセル。x = 絵の列、z = bottom - 行（接地）、厚みは奥（-y）へ。前面は 1 マスの中で中央に揃える。
    const int y_face = (kSide + kDepth) / 2;
    struct V {
        int x, y, z;
        std::uint8_t c;
    };
    std::vector<V> vox;
    int lo[3] = { 1 << 30, 1 << 30, 1 << 30 }, hi[3] = { -1, -1, -1 };
    for (int j = 0; j < H; ++j) {
        for (int i = 0; i < W; ++i) {
            const std::uint8_t *s = &px[(static_cast<std::size_t>(j) * W + i) * 4];
            if (s[3] == 0) continue;
            const int z = bottom - j;
            if (z < 0 || z >= H) continue;
            const std::uint8_t c = static_cast<std::uint8_t>(color_to_index[(static_cast<std::uint32_t>(s[0]) << 16) | (static_cast<std::uint32_t>(s[1]) << 8) | s[2]]);
            for (int y = y_face - kDepth; y < y_face; ++y) {
                vox.push_back({ i, y, z, c });
                lo[0] = std::min(lo[0], i);
                lo[1] = std::min(lo[1], y);
                lo[2] = std::min(lo[2], z);
                hi[0] = std::max(hi[0], i);
                hi[1] = std::max(hi[1], y);
                hi[2] = std::max(hi[2], z);
            }
        }
    }
    if (vox.empty()) return false;
    const int size[3] = { hi[0] - lo[0] + 1, hi[1] - lo[1] + 1, hi[2] - lo[2] + 1 };
    // np.argwhere の順（x → y → z の昇順）
    std::sort(vox.begin(), vox.end(), [](const V &a, const V &b) {
        if (a.x != b.x) return a.x < b.x;
        if (a.y != b.y) return a.y < b.y;
        return a.z < b.z;
    });
    out.voxels = static_cast<int>(vox.size());

    using namespace detail;
    std::string models;
    {
        std::string sz;
        put_i32(sz, size[0]);
        put_i32(sz, size[1]);
        put_i32(sz, size[2]);
        std::string xyzi;
        put_i32(xyzi, static_cast<std::int32_t>(vox.size()));
        for (const V &v : vox) {
            xyzi.push_back(static_cast<char>(v.x - lo[0]));
            xyzi.push_back(static_cast<char>(v.y - lo[1]));
            xyzi.push_back(static_cast<char>(v.z - lo[2]));
            xyzi.push_back(static_cast<char>(v.c));
        }
        models = chunk("SIZE", sz) + chunk("XYZI", xyzi);
    }
    std::string graph;
    {
        std::string a;
        put_i32(a, 0);
        put_i32(a, 0); // 空の辞書
        put_i32(a, 1);
        put_i32(a, -1);
        put_i32(a, 0);
        put_i32(a, 1);
        put_i32(a, 0);
        graph += chunk("nTRN", a);
        std::string g;
        put_i32(g, 1);
        put_i32(g, 0);
        put_i32(g, 1); // 子の数
        put_i32(g, 2);
        graph += chunk("nGRP", g);
        std::string t;
        put_i32(t, 2);
        put_i32(t, 1); // 辞書 1 組: _name
        put_str(t, "_name");
        put_str(t, "main");
        put_i32(t, 3);
        put_i32(t, -1);
        put_i32(t, 0);
        put_i32(t, 1);
        put_i32(t, 1); // 辞書 1 組: _t（模型の中心の位置）
        put_str(t, "_t");
        put_str(t, std::to_string(lo[0] + size[0] / 2) + " " + std::to_string(lo[1] + size[1] / 2) + " " + std::to_string(lo[2] + size[2] / 2));
        graph += chunk("nTRN", t);
        std::string shp;
        put_i32(shp, 3);
        put_i32(shp, 0);
        put_i32(shp, 1);
        put_i32(shp, 0);
        put_i32(shp, 0);
        graph += chunk("nSHP", shp);
    }
    std::string pal(256 * 4, '\0');
    for (int i = 0; i < 255; ++i) pal[static_cast<std::size_t>(i) * 4 + 3] = static_cast<char>(255); // 使わない索引も (0, 0, 0, 255)（export_vox.py と同じ）
    for (const auto &[rgb, idx] : palette_index) {
        const std::size_t at = static_cast<std::size_t>(idx - 1) * 4;
        pal[at] = static_cast<char>(rgb >> 16);
        pal[at + 1] = static_cast<char>(rgb >> 8);
        pal[at + 2] = static_cast<char>(rgb);
        pal[at + 3] = static_cast<char>(255);
    }
    const std::string body = models + graph + chunk("RGBA", pal);
    std::string file = "VOX ";
    put_i32(file, 150);
    file += chunk("MAIN", std::string(), body);
    out.vox = std::move(file);

    // 接地するマス（z = 0 の層が占める 64 ボクセル四方のマス）。画面は .vox と読み合わせる。
    std::vector<std::pair<int, int>> cells;
    for (const V &v : vox) {
        if (v.z != 0) continue;
        const std::pair<int, int> c{ v.x / kSide, v.y / kSide };
        if (std::find(cells.begin(), cells.end(), c) == cells.end()) cells.push_back(c);
    }
    std::sort(cells.begin(), cells.end());
    std::ostringstream js;
    js << "// " << name << " — プレハブ定義（設計書 §7）。`" << name << ".vox` と対で使う。\n"
       << "// dcss/adapter/dcss_slab.h が実行時に作った物（tools/voxel/tile_to_slab.py と同じ規則）。\n"
       << "// 原作のタイルを重ねて 32 ドットを 2 倍の 64 にした板。一辺 " << kSide << " / 厚み " << kDepth << " / flat\n"
       << "{\n  \"name\": \"" << name << "\",\n  \"voxels_per_cell\": " << kSide << ",\n  \"footprint\": [\n";
    for (std::size_t i = 0; i < cells.size(); ++i) {
        js << "    [\n      " << cells[i].first << ",\n      " << cells[i].second << "\n    ]" << (i + 1 < cells.size() ? "," : "") << "\n";
    }
    js << "  ],\n  \"anchor\": [\n    0,\n    0\n  ],\n  \"parts\": [\n    {\n      \"name\": \"main\",\n      \"voxels\": \"main\",\n"
       << "      \"grounded\": true,\n      \"motion\": {\n        \"kind\": \"static\"\n      },\n      \"wind_k\": 0.0\n    }\n  ],\n  \"variants\": []\n}\n";
    out.jsonc = js.str();
    return true;
}

// ------------------------------------------------------------ ファイル・番号

inline std::uint64_t hash_canvas(const Canvas &c)
{
    std::uint64_t h = 1469598103934665603ULL;
    const auto mix = [&](std::uint8_t b) {
        h ^= b;
        h *= 1099511628211ULL;
    };
    mix(static_cast<std::uint8_t>(kBakeVersion));
    mix(static_cast<std::uint8_t>(c.w));
    mix(static_cast<std::uint8_t>(c.h));
    mix(static_cast<std::uint8_t>(c.ground_row + 1));
    mix(static_cast<std::uint8_t>(c.fade));
    for (const std::uint8_t b : c.exempt) mix(b);
    for (const std::uint8_t b : c.rgba) mix(b);
    return h;
}

inline bool write_file_atomic(const std::filesystem::path &path, const std::string &bytes)
{
    namespace fs = std::filesystem;
    const fs::path tmp = path.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.flush();
        if (!out) return false;
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

//! 板のファイル（.vox と .jsonc）を置き場へ書く。同じ内容の物が既にあれば何もしない（前回までに作った物を使う）。書き終えてから true を返す。
inline bool write_slab(const std::string &dir, const std::string &name, const Canvas &canvas, std::string &err)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path vox = fs::path(dir) / (name + ".vox");
    const fs::path jsonc = fs::path(dir) / (name + ".jsonc");
    if (fs::exists(vox, ec) && fs::exists(jsonc, ec) && fs::file_size(vox, ec) > 0 && fs::file_size(jsonc, ec) > 0) return true;
    Baked baked;
    if (!bake(canvas, name, baked)) {
        err = "不透明な画素が無い";
        return false;
    }
    fs::create_directories(dir, ec);
    // 画面は .vox を先に読む。どちらも書き終えてから目録に載せるので、順は問わないが、ここでは .jsonc を先にする（読めたら両方そろっている）。
    if (!write_file_atomic(jsonc, baked.jsonc) || !write_file_atomic(vox, baked.vox)) {
        err = "書き込めません: " + dir;
        return false;
    }
    return true;
}

inline std::string name_of(std::uint64_t hash)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "dcss_%016llx", static_cast<unsigned long long>(hash));
    return buf;
}

//! 置き場のファイルのうち、このコアが作った板（name_of の名前 + .vox / .jsonc、書きかけの .tmp を含む）だけを真にする。
inline bool is_own_slab_file(const std::string &file)
{
    static const char *const kSuffixes[] = { ".vox", ".jsonc", ".vox.tmp", ".jsonc.tmp" };
    constexpr std::size_t kHead = 5 + 16; // "dcss_" + 16 桁
    if (file.size() <= kHead || file.compare(0, 5, "dcss_") != 0) return false;
    for (std::size_t i = 5; i < kHead; ++i) {
        const char c = file[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    const std::string rest = file.substr(kHead);
    for (const char *s : kSuffixes) {
        if (rest == s) return true;
    }
    return false;
}

//! 前回までの起動で作った板を消す（決めたこと 2026-10-21「起動時に前回生成したファイルは消去」）。
//! 板はその起動の間だけのキャッシュで、起動をまたいでは残さない。置き場の直下で、このコアが作った名前のファイルだけを消す。
inline void clear_slab_dir(const std::string &dir)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    std::vector<fs::path> doomed;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec) && is_own_slab_file(it->path().filename().string())) doomed.push_back(it->path());
    }
    for (const fs::path &p : doomed) fs::remove(p, ec);
}

struct Entry {
    int index = 0; //!< 目録の tile_index（1 から）
    char kind = 'R';
    int id = 0;
    std::string name;
};

//! 出たものだけ作って番号を振る置き場。ゲームのスレッドだけが触る。
class Bank {
public:
    bool init(const std::string &atlas_dir, const std::string &slab_dir)
    {
        slab_dir_ = slab_dir;
        clear_slab_dir(slab_dir_);
        std::string err;
        if (!atlas_.load(atlas_dir, err)) {
            std::fprintf(stderr, "[dcss] 板の材料を読めません（怪物・アイテムは字で出します）: %s\n", err.c_str());
            return false;
        }
        return true;
    }
    bool ready() const { return atlas_.ok(); }
    const Atlas &atlas() const { return atlas_; }

    //! 層の並びに番号を振る。同じ並びは同じ番号。作れなければ 0（画面は字で出す）。作ったらファイルは書き終えている。
    //! `fade` は半透明の種類（kFadeTrans・kFadeSubmerged の和。0 = 不透明）。同じ並びでも半透明なら別の板になる。
    int resolve(char kind, int id, const std::vector<Layer> &layers, int fade = 0)
    {
        if (!atlas_.ok() || layers.empty()) return 0;
        std::string key(1, kind);
        if (fade != 0) key += " f" + std::to_string(fade);
        for (const Layer &l : layers) {
            key += ' ';
            key += std::to_string(l.tile) + ',' + std::to_string(l.ofs_x) + ',' + std::to_string(l.ofs_y) + ',' + std::to_string(l.y_max);
        }
        const auto found = by_key_.find(key);
        if (found != by_key_.end()) return found->second;
        int index = 0;
        Canvas canvas;
        if (compose(atlas_, layers, canvas, fade) && static_cast<int>(entries_.size()) + 1 < 0xFF00) {
            const std::string name = name_of(hash_canvas(canvas));
            std::string err;
            if (write_slab(slab_dir_, name, canvas, err)) {
                Entry e;
                e.index = static_cast<int>(entries_.size()) + 1;
                e.kind = kind;
                e.id = (kind == 'K') ? e.index : (kind == 'P' ? 0 : id);
                e.name = name;
                entries_.push_back(e);
                index = e.index;
                dirty_ = true;
            } else if (!warned_) {
                warned_ = true;
                std::fprintf(stderr, "[dcss] 板を作れません（字で出します）: %s\n", err.c_str());
            }
        }
        by_key_[key] = index;
        return index;
    }

    //! 前回の呼び出しのあとに新しい板が増えていれば、目録の全部を返す（画面は全置換で受ける）。
    bool take_manifest(std::vector<Entry> &all)
    {
        if (!dirty_) return false;
        dirty_ = false;
        all = entries_;
        return true;
    }
    std::size_t count() const { return entries_.size(); }

private:
    Atlas atlas_;
    std::string slab_dir_;
    std::unordered_map<std::string, int> by_key_;
    std::vector<Entry> entries_;
    bool dirty_ = false;
    bool warned_ = false;
};

} // namespace dcss_slab
