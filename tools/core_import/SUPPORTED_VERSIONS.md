# 対応バージョンの追加方針

`supported_versions.json` は、遊ぶ人が後から原作ソース ZIP を取り込むときに対応できる原作バージョンを列挙する。

## 基本方針

- 既存の項目は、新しい原作バージョンへ書き換えない。
- 新しい原作バージョンへ対応するときは、`targets` に新しい項目を追加する。
- `id` は取り込み定義を識別するための値で、全項目で一意にする。
- `core` は同じ系統のコアを表す。同じ `core` を持つ複数バージョンを登録してよい。
- `install_id` は実行時に登録するコアの位置を表す。同じ系統の複数バージョンで同じ `install_id` を使ってよい。
- `core` と `version` の組み合わせは重複させない。
- 既存バージョンの `commit`、共有ソース、変換方法は固定する。新バージョン対応のために旧版の値を変更しない。

## 新しいバージョンを追加するとき

現在の項目が例えば次のような場合:

```json
{
  "id": "rogue2",
  "core": "rogue2",
  "install_id": "rogue2",
  "version": "6.0"
}
```

6.1 対応では 6.0 の項目を変更せず、例えば次を追加する。

```json
{
  "id": "rogue2-6.1",
  "core": "rogue2",
  "install_id": "rogue2",
  "version": "6.1"
}
```

旧版の単純な `id` は互換性のため残す。将来追加する版は `コア名-バージョン` の形式を基本とする。

## 変換処理について

原作ソースの構造が変わり、版ごとに異なる変換が必要になった場合は、既存の変換処理を書き換えず、新しい範囲コピーの定義（`tools/core_sources/recipes/` と `manifest.json` の項目）を追加する。

配布用の取り込み定義を生成するときは、`supported_versions.json` に残っているすべての版を生成対象とする。このため、共通アダプタの変更で旧版が取り込めなくなった場合は、配布物生成時に検出して旧版対応を直す。

## 削除について

対応済みバージョンは原則として削除しない。削除が必要な場合は、原作側の配布停止、法的理由、重大な安全上の理由など、明確な理由を記録してから行う。

### 削除の記録

| 日付 | 外した項目 | 理由 |
|---|---|---|
| 2026-10-05 | `hengband`（3.0.2.4-Beta） | アプリの `lib/` を変愚 3.0.2.5 へ上げた（町の定義が `.jsonc` に変わった）ため、3.0.2.4 のコアは組めても起動で止まる。原作ソースの一覧（`manifest.json`）の変愚の項目も 3.0.2.5 へ進み、3.0.2.4 の範囲コピーの定義は残っていない。3.0.2.4 を残すには、原作ソースの写し・組み立ての一覧・3.0.2.4 の `lib/` を別に持つ必要があり、決めたことで外した（「キットは a の方針で」）。代わりに `hengband-3.0.2.5-Beta` を足した。経緯は。短愚の項目 `tangband` が共有ソースとして使う変愚 3.0.2.4 の ZIP はそのまま |

## 現在の項目（2026-10-05）

| id | 版 | 出力 | 遊ぶ人が選ぶ ZIP |
|---|---|---|---|
| hengband-3.0.2.5-Beta | 3.0.2.5-Beta | HengbandCore.exe | hengband の固定コミット（`install_id` は前と同じ `hengband`） |
| tangband | 26.0.4 | TangbandCore.exe | tangband ＋ hengband の固定コミット（2026-10-05 に `manifest_source` を `hengband` から短愚専用の `tangband` へ移した。中身は変愚 3.0.2.4 の写しで同じ。変愚の版上げで短愚を変えないため。`id`・`install_id`・版・コミット・共有ソースは変えていない） |
| gensoband | 2.1.6 | GensobandCore.exe | gensoband の固定コミット |
| silq | 1.5.1.0-beta2 | SilCore.exe | sil-q の固定コミット |
| frox | 7.3.pipari.2 | FroxCore.exe | froxcomposband の固定コミット |
| rogue2 | 6.0 | Rogue2Core.exe | Rogue2.Official の固定コミット（旧 `patch_profile: rogue2-6.0` は範囲コピーの定義へ置き換えた。`id`・`install_id`・版・コミットは変えていない） |
| angband | 4.2.6 | AngbandCore.exe | angband の固定コミット（タグ 4.2.6） |
| rogue36 | 3.6.4 | Rogue36Core.exe | usotopr/newrogue3.6 の固定コミット |
| rogue54 | 5.4.4 | Rogue54Core.exe | Davidslv/rogue の固定コミット |

`manifest_source` は `tools/core_sources/manifest.json` の項目名で、その `commit` は各項目の `commit`（短愚のように共有ソースを持つ項目は、共有ソースのどれかの `commit`）と一致させる。キット生成時にすべての項目で検査する（2026-10-05 までは範囲レシピのコアだけを検査していた）。
