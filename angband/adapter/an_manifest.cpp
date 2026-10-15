#include "an_manifest.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

namespace an {

namespace {

//! UTF-8 の 1 符号位置を読んで、次の読み出し位置を返す。
std::size_t next_utf8(const std::string &s, std::size_t pos, uint32_t &cp)
{
    const unsigned char c0 = static_cast<unsigned char>(s[pos]);
    if (c0 < 0x80u) {
        cp = c0;
        return pos + 1;
    }
    if ((c0 & 0xE0u) == 0xC0u && pos + 1 < s.size()) {
        cp = ((c0 & 0x1Fu) << 6) | (static_cast<unsigned char>(s[pos + 1]) & 0x3Fu);
        return pos + 2;
    }
    if ((c0 & 0xF0u) == 0xE0u && pos + 2 < s.size()) {
        cp = ((c0 & 0x0Fu) << 12) | ((static_cast<unsigned char>(s[pos + 1]) & 0x3Fu) << 6)
            | (static_cast<unsigned char>(s[pos + 2]) & 0x3Fu);
        return pos + 3;
    }
    if ((c0 & 0xF8u) == 0xF0u && pos + 3 < s.size()) {
        cp = ((c0 & 0x07u) << 18) | ((static_cast<unsigned char>(s[pos + 1]) & 0x3Fu) << 12)
            | ((static_cast<unsigned char>(s[pos + 2]) & 0x3Fu) << 6)
            | (static_cast<unsigned char>(s[pos + 3]) & 0x3Fu);
        return pos + 4;
    }
    cp = c0;
    return pos + 1;
}

//! ラテン文字の分音記号を落とす（U+00C0-U+00FF の範囲。Sméagol・Grishnákh で足りる）。
char strip_diacritic(uint32_t cp)
{
    static const char *const kBase =
        "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPsaaaaaaaceeeeiiiionooooo/ouuuuypy";
    if ((cp >= 0xC0u) && (cp <= 0xFFu)) {
        return kBase[cp - 0xC0u];
    }
    return 0;
}

//! CSV の 1 行を引用符つきで分割する（RFC4180 相当。`""` はエスケープされた `"`）。
std::vector<std::string> split_csv_line(const std::string &line)
{
    std::vector<std::string> fields;
    std::string field;
    bool in_quotes = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (in_quotes) {
            if (c == '"') {
                if ((i + 1 < line.size()) && (line[i + 1] == '"')) {
                    field.push_back('"');
                    ++i;
                } else {
                    in_quotes = false;
                }
            } else {
                field.push_back(c);
            }
        } else {
            if (c == '"') {
                in_quotes = true;
            } else if (c == ',') {
                fields.push_back(field);
                field.clear();
            } else {
                field.push_back(c);
            }
        }
    }
    fields.push_back(field);
    return fields;
}

/*!
 * @brief Angband の生の tval（`src/list-tvals.h` の `TV_*`）→ 変愚の `type_value`
 * （`src/object/tval-types.h` の `ItemKindType`）。**規則の訂正**（本スレッドの
 * 判断。2026-09-23）で、8 分類だけだった対応表を tval 全種類へ広げた。
 * @details `TV_*` は `list-tvals.h` を `#define TV(a,b) TV_##a,` で展開した
 * 列挙子で、`TV_NULL` = 0 起点の**出現順**（`obj-properties.h:35-39`）。
 * 数値はこのファイルではなく Angband 側のヘッダを直接見られないので、
 * `list-tvals.h` を読んで手で数えた（0 起点で 1 つずつ + 1）:
 * NULL=0, CHEST=1, SHOT=2, ARROW=3, BOLT=4, BOW=5, DIGGING=6, HAFTED=7,
 * POLEARM=8, SWORD=9, BOOTS=10, GLOVES=11, HELM=12, CROWN=13, SHIELD=14,
 * CLOAK=15, SOFT_ARMOR=16, HARD_ARMOR=17, DRAG_ARMOR=18, LIGHT=19,
 * AMULET=20, RING=21, STAFF=22, WAND=23, ROD=24, SCROLL=25, POTION=26,
 * FLASK=27, FOOD=28, MUSHROOM=29, MAGIC_BOOK=30, PRAYER_BOOK=31,
 * NATURE_BOOK=32, SHADOW_BOOK=33, OTHER_BOOK=34, GOLD=35。
 * 変愚側は `ItemKindType`（`tval-types.h`）の値をそのまま使う。
 * `TV_MUSHROOM` は変愚で独立した分類が無く `FOOD`（80）に含まれる
 * （`lib/edit/BaseitemDefinitions.jsonc` のキノコの項目も `type_value: 80`）。
 * 呪文書の 5 分類（Angband の魔法・祈祷・自然・影・その他）は、変愚の
 * 9 realm（生命・仙術・自然・カオス・暗黒・トランプ・秘術・匠・悪魔・破邪・
 * 歌・武術・呪術）と数も名前も対応しない——書名も両者でまったく別物
 * （例: Angband の "Magic for Beginners" と変愚の「妖術入門」）なので、
 * 名前が一致すること自体がほぼ起こらず、分類の選び方が結果へ影響しない。
 * ここでは代表として近い実践（僧侶=祈祷→生命、自然=自然のまま、
 * 暗黒魔術=影→死霊）だけ当て、当てにくい魔法一般・その他は表に入れず
 * `-1`（従来どおり名前だけの索引）へ落とす——名前が衝突しない前提なので
 * 実害は無い。
 */
int hengband_type_value_for_tval(int angband_tval)
{
    switch (angband_tval) {
        case 1: return 7;   // TV_CHEST -> CHEST
        case 2: return 16;  // TV_SHOT -> SHOT
        case 3: return 17;  // TV_ARROW -> ARROW
        case 4: return 18;  // TV_BOLT -> BOLT
        case 5: return 19;  // TV_BOW -> BOW
        case 6: return 20;  // TV_DIGGING -> DIGGING
        case 7: return 21;  // TV_HAFTED -> HAFTED
        case 8: return 22;  // TV_POLEARM -> POLEARM
        case 9: return 23;  // TV_SWORD -> SWORD
        case 10: return 30; // TV_BOOTS -> BOOTS
        case 11: return 31; // TV_GLOVES -> GLOVES
        case 12: return 32; // TV_HELM -> HELM
        case 13: return 33; // TV_CROWN -> CROWN
        case 14: return 34; // TV_SHIELD -> SHIELD
        case 15: return 35; // TV_CLOAK -> CLOAK
        case 16: return 36; // TV_SOFT_ARMOR -> SOFT_ARMOR
        case 17: return 37; // TV_HARD_ARMOR -> HARD_ARMOR
        case 18: return 38; // TV_DRAG_ARMOR -> DRAG_ARMOR
        case 19: return 39; // TV_LIGHT -> LITE
        case 20: return 40; // TV_AMULET -> AMULET
        case 21: return 45; // TV_RING -> RING
        case 22: return 55; // TV_STAFF -> STAFF
        case 23: return 65; // TV_WAND -> WAND
        case 24: return 66; // TV_ROD -> ROD
        case 25: return 70; // TV_SCROLL -> SCROLL
        case 26: return 75; // TV_POTION -> POTION
        case 27: return 77; // TV_FLASK -> FLASK
        case 28: return 80; // TV_FOOD -> FOOD
        case 29: return 80; // TV_MUSHROOM -> FOOD（変愚に独立の分類が無い）
        case 31: return 90; // TV_PRAYER_BOOK -> LIFE_BOOK（僧侶 ≈ 生命）
        case 32: return 92; // TV_NATURE_BOOK -> NATURE_BOOK（名前も一致）
        case 33: return 94; // TV_SHADOW_BOOK -> DEATH_BOOK（暗黒魔術 ≈ 死霊）
        case 35: return 127; // TV_GOLD -> GOLD
        // TV_NULL(0)・TV_MAGIC_BOOK(30)・TV_OTHER_BOOK(34) は対応が定まらない
        // ので表に入れず、名前だけの索引へ落とす（上の理由）。
        default: return -1;
    }
}

//! `object_by_name_and_type_` の鍵。
std::string name_and_type_key(const std::string &normalized_name, int type_value)
{
    return normalized_name + "|" + std::to_string(type_value);
}

/*!
 * @brief `BaseitemDefinitions.jsonc` から `id → type_value` を読む（1b 直し）。
 * @details jsonc は各アイテムの項目ごとに `"id": N` が 1 度、その後に
 * `"itemkind": { "type_value": M, ... }` が 1 度だけ出る（本スレッドで
 * 643 個ずつと数を合わせて確かめた）ので、出現順に対で拾うだけでよい
 * ——`allocations` などほかの入れ子には `id`/`type_value` という鍵は無い。
 * 総合的な JSON 構文解析はしない（jsonc はコメント・末尾カンマ入りで
 * 標準の JSON パーサをそのまま使えないため。tilework の CSV 読みと同じ
 * 「必要な分だけ拾う」流儀）。
 */
bool load_id_to_type_value(const std::string &path, std::unordered_map<long, int> &out)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();

    const std::string id_key = "\"id\":";
    const std::string type_key = "\"type_value\":";

    long pending_id = -1;
    bool have_pending_id = false;

    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t id_pos = text.find(id_key, pos);
        const std::size_t type_pos = text.find(type_key, pos);
        if ((id_pos == std::string::npos) && (type_pos == std::string::npos)) {
            break;
        }
        if ((type_pos == std::string::npos) || ((id_pos != std::string::npos) && (id_pos < type_pos))) {
            // 次に来るのは "id"。
            std::size_t p = id_pos + id_key.size();
            while ((p < text.size()) && std::isspace(static_cast<unsigned char>(text[p]))) ++p;
            std::size_t q = p;
            while ((q < text.size()) && (std::isdigit(static_cast<unsigned char>(text[q])) || (text[q] == '-'))) ++q;
            if (q > p) {
                pending_id = std::stol(text.substr(p, q - p));
                have_pending_id = true;
            }
            pos = q;
        } else {
            // 次に来るのは "type_value"。
            std::size_t p = type_pos + type_key.size();
            while ((p < text.size()) && std::isspace(static_cast<unsigned char>(text[p]))) ++p;
            std::size_t q = p;
            while ((q < text.size()) && (std::isdigit(static_cast<unsigned char>(text[q])) || (text[q] == '-'))) ++q;
            if ((q > p) && have_pending_id) {
                const int type_value = std::stoi(text.substr(p, q - p));
                out[pending_id] = type_value;
                have_pending_id = false;
            }
            pos = q;
        }
    }
    return !out.empty();
}

/*!
 * @brief 地形名の当て込み表（設計 §6.2 の「一致しない 6 件」）。
 * @details 正規化した Angband の地形名 → tile_mapping.csv の F 行の名前（同じく正規化）。
 * 見つかった対応は `tools/angband` の scratch で 24 件全数を照合して決めた
 * （の「一致しない 6 件」と同じ内訳）。
 */
const std::unordered_map<std::string, std::string> &terrain_overrides()
{
    static const std::unordered_map<std::string, std::string> table = {
        { normalize_name("unknown grid"), normalize_name("unknown grid (not detected)") },
        { normalize_name("closed door"), normalize_name("door") },
        { normalize_name("Weapon Smiths"), normalize_name("Weaponsmith's Shop") },
        { normalize_name("Bookseller"), normalize_name("Bookstore") },
        { normalize_name("lava"), normalize_name("shallow lava") },
        // "pile of passable rubble" はここで "pile of rubble"（F49）へ丸めていた
        // ——通れる瓦礫と通れない瓦礫が同じ地形番号で送られ、画面側も同じ扱いに
        // なっていた（作業1で発見）。**丸めをやめ**、`angband/tilework/mapping.csv`
        // に F40001 として別番号を登録して見分ける（`town_styles.jsonc` の
        // Angband の節が `terrain_id: 40001` で受ける）。
    };
    return table;
}

/*!
 * @brief モンスターの絵の当て直し表（2026-09-29。 の AH-46）。
 * @details 正規化した Angband の種族名 → 当てる絵の行の名前（同じく正規化）。
 * **名前の一致より先に引く**——同じ名前の別物（変愚の `Chimaera` はギンザメ、
 * `Ghast` は絵がカンガルー）を名前の一致が掴んでしまうのを止めるため。
 * 行き先は `tilework/tile_mapping.csv`（変愚）か `angband/tilework/mapping.csv`
 * （Angband 固有・借りた絵）の R 行の `name_en`。全数の見比べで決めた。
 * `tools/angband/an_build_tile_todo.py` の `MONSTER_OVERRIDES` と同じ表。
 */
const std::unordered_map<std::string, std::string> &monster_overrides()
{
    static const std::unordered_map<std::string, std::string> table = {
        // 生成した AR8 は荷物（爆弾のような箱）を背負っていた。4.2 の野犬はジャッカルの後継。
        { normalize_name("wild dog"), normalize_name("Jackal") },
        // 変愚の "Chimaera"（R1251）はギンザメ。獅子・山羊・竜のキメラは R341 "Chimera"。
        { normalize_name("chimaera"), normalize_name("Chimera") },
        // 変愚の "Ghast"（R327）の絵はカンガルー。食屍鬼の親類なので Ghoul（R418）。
        { normalize_name("ghast"), normalize_name("Ghoul") },
        // 生成した AR72 は白髭の老人。変愚の Ice troll（R454）は日本語名も「雪トロル」。
        { normalize_name("snow troll"), normalize_name("Ice troll") },
        // 熊の姿のビヨルン。生成した AR74 は紫の猫だった。
        { normalize_name("Beorn, the Mountain Bear"), normalize_name("Grizzly bear") },
        // 生成した AR5 は緑の顔の兵士。変愚の Santa Claus（R733）。
        { normalize_name("Father Christmas"), normalize_name("Santa Claus") },
        // Angband の gorgon は蛇の髪の女（base:naga）。変愚の Gorgon（R475）は牛頭の戦士。
        // Rogue 用に生成したメデューサ（RLmedusa）を借りる（mapping.csv の借りた行）。
        { normalize_name("gorgon"), normalize_name("medusa (borrowed from Rogue)") },
        // 2026-09-29 に個別のプロンプトで作り直した 9 体（AR501〜AR509。`mapping.csv` の
        // 「(regenerated)」行）。迷っていた・不適切だった絵の当て直し。
        { normalize_name("nightcrawler"), normalize_name("nightcrawler (regenerated)") },
        { normalize_name("Gorlim, Betrayer of Barahir"), normalize_name("Gorlim, Betrayer of Barahir (regenerated)") },
        { normalize_name("The Balrog of Moria"), normalize_name("The Balrog of Moria (regenerated)") },
        { normalize_name("giant black louse"), normalize_name("giant black louse (regenerated)") },
        { normalize_name("giant red frog"), normalize_name("giant red frog (regenerated)") },
        { normalize_name("Maia of Mandos"), normalize_name("Maia of Mandos (regenerated)") },
        { normalize_name("winged horror"), normalize_name("winged horror (regenerated)") },
        { normalize_name("Gilim, the Giant of Eruman"), normalize_name("Gilim, the Giant of Eruman (regenerated)") },
        { normalize_name("Beorn, the Shape-Changer"), normalize_name("Beorn, the Shape-Changer (regenerated)") },
    };
    return table;
}

} // namespace

std::string normalize_name(const std::string &name)
{
    std::string lowered;
    lowered.reserve(name.size());
    for (std::size_t i = 0; i < name.size();) {
        uint32_t cp = 0;
        const std::size_t next = next_utf8(name, i, cp);
        if (cp < 0x80u) {
            lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(cp))));
        } else if (const char base = strip_diacritic(cp)) {
            lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(base))));
        }
        // それ以外の非 ASCII は読み捨てる（一致すべき名前は全部ラテン文字で足りる）。
        i = next;
    }

    // "armour" -> "armor"（このあと空白・記号を落とすので、境界を気にせず置換してよい）。
    std::string armor_fixed;
    armor_fixed.reserve(lowered.size());
    for (std::size_t i = 0; i < lowered.size();) {
        if ((lowered.compare(i, 6, "armour") == 0)) {
            armor_fixed += "armor";
            i += 6;
        } else {
            armor_fixed.push_back(lowered[i]);
            ++i;
        }
    }

    std::string out;
    out.reserve(armor_fixed.size());
    for (const char c : armor_fixed) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            out.push_back(c);
        }
    }
    return out;
}

/*!
 * @brief `csv_path` の 1 本ぶんを読んで既存のマップへ**継ぎ足す**（M2）。
 * @details `load()` から 2 回（変愚の共有 CSV → Angband 固有の追加 CSV の順で）
 * 呼ぶ共通処理。`next_index` は呼び出し元が持ち回して 2 本のファイルを通した
 * 1 本の通し番号にする（`tile_index` を 2 系統に分けない）。
 * @return 1 行以上読めたら真（ファイルが開けない／中身が空でも呼び出し元は
 * 致命にしなくてよい——`load()` 側で判断する）。
 */
bool TileMapping::load_rows_from(const std::string &csv_path,
    const std::unordered_map<long, int> &id_to_type_value, bool have_type_values,
    std::uint16_t &next_index)
{
    std::ifstream file(csv_path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    bool any = false;
    std::string line;
    bool first = true;
    while (std::getline(file, line)) {
        if (!line.empty() && (line.back() == '\r')) {
            line.pop_back();
        }
        if (first) {
            first = false;
            continue; // header
        }
        if (line.empty() || (line[0] == '#')) {
            continue;
        }
        const std::vector<std::string> fields = split_csv_line(line);
        if (fields.size() < 15) {
            continue;
        }
        const std::string &kind = fields[1];
        const std::string &id_text = fields[2];
        const std::string &name_en = fields[4];
        const std::string &src_640 = fields[13];
        const std::string &tile_32 = fields[14];
        const std::string &generated = fields.back();
        if (kind.empty() || id_text.empty() || name_en.empty() || (generated != "yes")) {
            continue;
        }
        // 3D（`terrain_id`/`monster_id`/`object_id`）と 2D（`tile_index`）は別の採番
        // なので、パス（`path`）が無い行でも 3D 側の名寄せは続ける（下の name/id 登録）。
        // ただし asset_manifest（v1 §8.2）には「絵が実在する行」だけを載せる。
        const std::string &path = !tile_32.empty() ? tile_32 : src_640;

        long id = 0;
        try {
            id = std::stol(id_text);
        } catch (...) {
            continue;
        }
        if ((id < 0) || (id > 0xFFFF)) {
            continue;
        }
        const std::string key = normalize_name(name_en);
        if (key.empty()) {
            continue;
        }

        std::uint16_t tile_index = 0;
        if (!path.empty() && ((kind == "F") || (kind == "R") || (kind == "K") || (kind == "P"))) {
            presentation::AssetManifestWireEntry wire;
            wire.index = next_index;
            wire.kind = kind;
            wire.id = static_cast<int>(id);
            wire.path = path;
            this->message_.assets.push_back(std::move(wire));
            tile_index = next_index;
            ++next_index;
        }

        // 同じ名前が複数行にまたがる区間（共有アイコンのアイテムなど）は最初の 1 件を正とする
        // （設計 §6.2 は「変愚の地形番号」1 個を求めており、複数候補を混ぜる理由が無い）。
        const TileLookup lookup{ static_cast<std::uint16_t>(id), tile_index };
        if (kind == "F") {
            this->terrain_by_name_.emplace(key, lookup);
        } else if (kind == "R") {
            this->monster_by_name_.emplace(key, lookup);
        } else if (kind == "K") {
            this->object_by_name_.emplace(key, lookup);
            bool typed = false;
            if (have_type_values) {
                const auto tv_it = id_to_type_value.find(id);
                if (tv_it != id_to_type_value.end()) {
                    this->object_by_name_and_type_.emplace(name_and_type_key(key, tv_it->second), lookup);
                    typed = true;
                }
            }
            /*
             * Angband 固有の K 行（`angband/tilework/mapping.csv`。id は 30000 台）は
             * 変愚の jsonc に無いので、上では分類つきの索引に載らない。載らないと
             * `lookup_object()` は（tval が対応表にある限り）名前だけの索引へ落ちないので、
             * **生成したアイテムの絵が 1 枚も出ていなかった**（2026-09-29 に実測。
             * 回復の薬・帰還の巻物ほか 215 行ぜんぶ 0）。追加 CSV は `generated` の
             * 手前に `an_tval`（Angband の生の tval）の列を持つので、そこから引く。
             */
            if (!typed && (fields.size() >= 17)) {
                try {
                    const int type_value = hengband_type_value_for_tval(std::stoi(fields[15]));
                    if (type_value >= 0) {
                        this->object_by_name_and_type_.emplace(name_and_type_key(key, type_value), lookup);
                    }
                } catch (...) {
                    // an_tval が空（プレイヤー種族など）なら名前だけの索引のまま。
                }
            }
            // 正体の分からないアイテムの代表タイル用（1 直し）。同じ id が複数行に
            // またがることは無い（CSV は K 行を id で 1 行にまとめてある）ので上書きでよい。
            if (tile_index != 0) {
                this->object_tile_index_by_bi_id_[static_cast<std::uint16_t>(id)] = tile_index;
            }
        } else if (kind == "P") {
            this->player_by_name_.emplace(key, lookup);
        }
        any = true;
    }
    return any;
}

bool TileMapping::load(const std::string &csv_path, const std::string &asset_root,
    const std::string &baseitem_jsonc_path, const std::string &extra_csv_path)
{
    // K 行の `id` → `type_value`（1b 直し）。読めなくても致命ではない
    // ——分類ぬきの名前だけの照合（従来どおり）へ落ちるだけ。
    std::unordered_map<long, int> id_to_type_value;
    bool have_type_values = false;
    if (!baseitem_jsonc_path.empty()) {
        have_type_values = load_id_to_type_value(baseitem_jsonc_path, id_to_type_value);
    }

    this->message_.root = asset_root;
    std::uint16_t next_index = 1;

    const bool main_ok = this->load_rows_from(csv_path, id_to_type_value, have_type_values, next_index);
    if (!main_ok) {
        this->error_ = "cannot open or no rows in " + csv_path;
        return false;
    }

    // M2: Angband 固有（変愚の絵を流用できない実体）の追加 CSV。
    // 無くても致命にしない——まだ絵を用意していない段階でも変愚の絵だけで動く。
    if (!extra_csv_path.empty()) {
        const bool extra_ok = this->load_rows_from(extra_csv_path, id_to_type_value, have_type_values, next_index);
        std::fprintf(stderr, "[an_manifest] extra tile mapping %s: %s\n", extra_csv_path.c_str(),
            extra_ok ? "loaded" : "not found or empty (skipped)");
    }

    if (this->terrain_by_name_.empty() && this->monster_by_name_.empty() && this->object_by_name_.empty()) {
        this->error_ = "no F/R/K rows read from " + csv_path;
        return false;
    }
    return true;
}

TileLookup TileMapping::lookup_terrain(const std::string &angband_name) const
{
    const std::string key = normalize_name(angband_name);
    auto it = this->terrain_by_name_.find(key);
    if (it != this->terrain_by_name_.end()) {
        return it->second;
    }
    const auto override_it = terrain_overrides().find(key);
    if (override_it != terrain_overrides().end()) {
        it = this->terrain_by_name_.find(override_it->second);
        if (it != this->terrain_by_name_.end()) {
            return it->second;
        }
    }
    return TileLookup{};
}

TileLookup TileMapping::lookup_monster(const std::string &angband_name) const
{
    const std::string key = normalize_name(angband_name);
    const auto override_it = monster_overrides().find(key);
    if (override_it != monster_overrides().end()) {
        const auto target = this->monster_by_name_.find(override_it->second);
        if (target != this->monster_by_name_.end()) {
            return target->second;
        }
    }
    const auto it = this->monster_by_name_.find(key);
    return (it != this->monster_by_name_.end()) ? it->second : TileLookup{};
}

TileLookup TileMapping::lookup_object(const std::string &angband_name, int angband_tval) const
{
    const std::string key = normalize_name(angband_name);
    const int type_value = hengband_type_value_for_tval(angband_tval);
    if (type_value >= 0) {
        // 規則の訂正（本スレッドの判断。2026-09-23）: 対応する type_value が
        // 定まる tval なら、名前だけの索引へは**絶対に落ちない**。ここで
        // 見つからなければ「無い」——`Speed`・`Light`・`Healing` のような
        // 同名で分類だけ違う組が、フォールバックのせいで別の分類の絵を
        // 掴んでいた不具合（1b 直しの取り違え）を再発させないため。
        const auto typed_it = this->object_by_name_and_type_.find(name_and_type_key(key, type_value));
        return (typed_it != this->object_by_name_and_type_.end()) ? typed_it->second : TileLookup{};
    }
    // tval が対応表に無い（呪文書の一部・TV_NULL）ときだけ、名前だけの索引を使う
    // ——このとき名前が衝突する既知の組は無い（`hengband_type_value_for_tval` の註）。
    const auto it = this->object_by_name_.find(key);
    return (it != this->object_by_name_.end()) ? it->second : TileLookup{};
}

TileLookup TileMapping::lookup_player(const std::string &angband_race_name) const
{
    const auto it = this->player_by_name_.find(normalize_name(angband_race_name));
    return (it != this->player_by_name_.end()) ? it->second : TileLookup{};
}

std::uint16_t TileMapping::lookup_unaware_representative(presentation::UnawareItemCategory category) const
{
    const std::uint16_t bi_id = presentation::unaware_item_representative_bi_id(category);
    const auto it = this->object_tile_index_by_bi_id_.find(bi_id);
    return (it != this->object_tile_index_by_bi_id_.end()) ? it->second : 0;
}

} // namespace an
