# Sil-Q の取り込み元

This directory is a copy of the Sil-Q source and data. See the table below for the
origin, and the last section for how to update it.

このディレクトリは Sil-Q のソースとデータの写しです。元は下のリポジトリで、
原則として手を加えていません。

| 項目 | 内容 |
| --- | --- |
| 取り込み元 | <https://github.com/sil-quirk/sil-q> |
| コミット | `d0355bf97e873ba10e6b364aa127bc5ebbbd05f1`（2026-08-05、既定ブランチの先頭） |
| バージョン | 1.5.1.0-beta2（`src/defines.h` の `VERSION_STRING`） |
| 取り込んだ日 | 2026-08-20 |
| 文字コード | ASCII |
| ライセンス | GPL v2 または Angband 使用許諾（`silq/LICENSE.md`） |

## 取り込んだもの

| 元 | ここ | 備考 |
| --- | --- | --- |
| `src/*.c` `src/*.h` | `silq/src/` | 直下のみ。`cocoa/` などは入れていません |
| `lib/edit/*.txt` | `silq/lib/edit/` | ゲームデータ（13 件） |
| `lib/pref/*` | `silq/lib/pref/` | 色とキーの既定値 |
| `lib/xtra/tutorial` | `silq/lib/xtra/` | チュートリアル用のセーブデータ |
| `lib/docs/` の PDF 2 冊 | `silq/lang/ja/help/manual.en.txt` | 本文だけをテキストに抜き出したものです（下記） |
| `LICENSE.md` | `silq/LICENSE.md` | 原著者が派生版でも残すよう求めているので、そのまま入れています |

## 取り込んでいないもの

- `.git/`
- `lib/xtra/font/` `sound/` `graf/` … ライセンスが本体と別のため。この派生版では使いません
- `lib/docs/` の PDF そのもの、変更履歴、スクリーンショット … 大きいため。本文だけをテキストにしています
- `src/cocoa/` など macOS 用のファイルと、アイコンなどのリソース
- `msvc2022/` `CMakeLists.txt` など元のビルド設定 … こちらは `VisualStudio/SilCore/SilCore.vcxproj` でビルドします
- 元のリリースに含まれるビルド済みの実行ファイル

## マニュアルについて

Sil-Q にはゲーム内ヘルプのファイル（`lib/help/`）がなく、`?` キーで出る画面はプログラムが
直接描いています。詳しい説明は `lib/docs/` の PDF 2 冊（Sil 1.3 Manual と Sil-Q 1.4.2 Manual）に
あります。Sil-Q 版は Sil からの差分だけを書いているので、2 冊で 1 組です。

日本語化のときに、この 2 冊の本文を抜き出して 1 冊の日本語マニュアル
（`silq/lang/ja/help/manual.ja.txt`）にまとめました。抜き出した英文（`manual.en.txt`）は
翻訳の元にしたものです。

## こちらで追加したもの

| 追加 | 理由 |
| --- | --- |
| `adapter/` | この派生版のためのコードです。写しではありません。ASCII で書いています |
| `lang/` | 日本語化のデータとマニュアルです |
| `lib/{data,apex,save,user}/.keepalive` | 起動時に必要な空のディレクトリを作るためです。中身は追跡しません |

## 新しい版を取り込むとき

1. 元のリポジトリを clone し、上の「取り込んだもの」だけを上書きコピーします
2. `git diff` で `//SQ:` の印が付いた行が消えていないか確認します。いまのところ
   `//SQ:` の変更はありません
3. `VisualStudio/SilCore/SilCore.vcxproj` の `ClCompile` の一覧に増減した `.c` を反映します
4. この文書の表を更新します
