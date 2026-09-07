# 短愚蛮怒（Tangband）の取り込み元

Tangband shares the Hengband source (`src/`) and is built with `TANGBAND` defined.
This directory only holds the Tangband data (`lib/`). See below for the origin and
how to update it.

短愚蛮怒は変愚蛮怒と同じソース（`src/`）を `TANGBAND` を定義してビルドしたものです。
このディレクトリには短愚蛮怒用のデータ（`lib/`）だけがあります。

| 項目 | 内容 |
| --- | --- |
| 取り込み元 | <https://github.com/tanguband/tangband> ブランチ `tang26` |
| 取り込んだ版 | v26.0.4（`8d8e1f2cd`、2026-03-22） |
| 元の分岐点 | Hengband `develop` の `fca19de548c823d4f03f23f1cd2b58a8fa28185c`（2026-03-02、3.0.2.2-Beta の 56 コミット後） |
| 取り込んだ日 | 2026-08-18 |
| 方法 | ソースツリーは持ち込まず、ソースの差分は `#ifdef TANGBAND` で `src/` に入れています。`lib/` のデータは、こちらの `lib/`（3.0.2.4 相当）に短愚蛮怒の差分を 3 方向マージしたものです |

## この `lib/` に入っているもの

- `lib/edit/` … こちらの `lib/edit/` に短愚蛮怒の差分をマージしたものです
- `lib/help/` … 短愚蛮怒の表記に合わせたヘルプです
- `save/` `apex/` `bone/` `data/` `user/` … 空です。実行時に作られ、変愚蛮怒のものとは分かれています
- `file/` `pref/` `xtra/` `script/` `info/` … 持ちません。変愚蛮怒の `lib/` を共有します
  （起動処理が、`TANGBAND` のときに `edit` `data` `save` `apex` `bone` `user` `help` だけを
  こちらのディレクトリに差し替えます）

## マージで手作業で判断した箇所

取り込み直すときは、ここを必ず見直してください。

| 箇所 | 判断 |
| --- | --- |
| `MonraceDefinitions.jsonc` の id 237 / 373 / 481 / 882 の DROP のダイス | Hengband 3.0.2.4 で整理された値を採用しました（短愚蛮怒側は `DROP_GREAT` を足しただけで、ダイスは分岐時のままでした） |
| 同 id 1034 のフラグ配列 | 内容は同じです。Hengband の複数行の書式を採用しました |
| 同 終末のサーペント | 短愚蛮怒の id 1395 は Hengband が分岐後に別のモンスターに使っていたため、id 1417 に変えて末尾に追加しました。`src` 側の `spawn_doomsday_serpents()` も 1417 を指します |
| `towns/01` の「噂を聞く」 | 短愚蛮怒は日本語 0:0・英語 5:5 と不揃いだったので、両方 0:0 にしました |
| `quests` 001 / 034 | 短愚蛮怒は旧形式の `.txt` を編集していますが、Hengband は分岐後に `.jsonc` に変わっているため、`.jsonc` に手で移しました |

## 英語表示について

実行時の日英切り替えはそのまま使えます。短愚蛮怒側に由来する英語の粗さ
（ランダムアーティファクトの銘、勝利の日記に残る "Hengband" の表記、英語ヘルプが未改訂）は
元のままにしています。

## 新しい版を取り込むとき

1. 新しい分岐点と差分を取り直します
2. `lib/edit` は 3 方向マージをやり直し、上の表の箇所を必ず見直します
3. `src` 側の変更箇所は `grep -rn "TANGBAND" src presentation platform` で全て見つかります
4. この文書の表を更新します
