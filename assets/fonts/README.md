# assets/fonts

Phase 2 のフォントはプレースホルダ運用です。

- `FontFace::open("assets/fonts/placeholder.ttf", pt)` を最初に試します。
- 見つからない場合は Windows システムフォント（`meiryo.ttc` → `YuGothM.ttc` → `msgothic.ttc` → `consola.ttf` → `arial.ttf`）に自動フォールバックします。
- 日本語 UTF-8 ヒント（ControllerBar 等）を表示するため、フォールバック先は日本語対応フォントを優先します。

任意で日本語対応 TTF/TTC を `placeholder.ttf` として配置すると、そのフォントが使われます。
バイナリフォントはリポジトリに含めないため、既定ではフォールバックで動作します。

## Term ミラー用（等幅）

Term ミラー（オープニング・新規作成・コマンドメニュー等）は**文字グリッド**なので、
可変幅フォントで 1 行まるごと描くと枠や 2 カラム目が行ごとにずれます。
そのため `FontFace::open_monospace("assets/fonts/placeholder_mono.ttf", pt)` を使い、
フォールバックは**等幅のみ**（`msgothic.ttc` → `consola.ttf` → `cour.ttf`）に絞っています。

任意で等幅の日本語 TTF/TTC を `placeholder_mono.ttf` として置けば、そちらが使われます。
半角 W・全角ちょうど 2W の等幅であることが条件です（そうでないとカラムが揃いません）。
