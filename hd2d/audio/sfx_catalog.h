#pragma once
/*!
 * @file sfx_catalog.h
 * @brief 音の名前 → wav。**表引きだけの純関数。**
 *
 * ## なぜ名前で引くのか
 * コアは音を**名前**で言ってくる（`SoundEvent::name`）。番号を運ぶと、同じ番号が
 * 変種ごとに別の音を指すので**画面側に変種ごとの対応表**が要る＝コア名の分岐が生える
 * （設計 §1 制約 1 違反）。名前なら表は 1 つで済む。
 *
 * ## 素材はどこに在るか
 * `assets/audio/sfx.jsonc` の `dir` が指す所。**既定は `lib/xtra/sound`**——
 * 変愚の CC0 の束（OpenGameArt。`lib/xtra/sound/readme.txt` に出典 20 本）が
 * **既にこの機体に在る**ので、素材の決定を待たずに端から端まで確かめられる。
 *
 * ```jsonc
 * { "dir": "lib/xtra/sound",
 *   "sounds": { "hit": ["hit.wav", "hit1.wav"], "kill": ["kill1.wav"] } }
 * ```
 *
 * 候補が複数あれば**そのつど 1 つ選ぶ**（同じ音が続くと機械的に聞こえる。変愚も同じ）。
 * 名前が表に無ければ**空**を返す＝鳴らさない。**知らない音で落ちない**こと。
 */

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace hd2d::audio {

class SfxCatalog {
public:
    /*!
     * @brief `assets/audio/sfx.jsonc` を読む。
     * @return 読めたら true。**無いのは失敗ではない**（効果音が鳴らないだけ）
     */
    bool load(const std::string &dir, std::string *log = nullptr);
    /*!
     * @brief その名前の wav への道。**候補が複数なら 1 つ選ぶ。**
     * @param pick 選ぶ種（`0..n-1` を作るための数。呼び出し側が回す）
     * @return 道（表に無ければ空）
     */
    std::string path_for(const std::string &name, std::uint32_t pick) const;
    //! 表に載っている名前の数（検査が「空でない」を見るのに使う）。
    std::size_t name_count() const { return this->table_.size(); }
    //! 素材の置き場（jsonc の `dir`）。
    const std::string &sound_dir() const { return this->dir_; }
    //! 表を直に入れる（検査用）。
    void set_entry(const std::string &name, std::vector<std::string> files);

private:
    std::string dir_;
    std::map<std::string, std::vector<std::string>> table_;
};

} // namespace hd2d::audio
