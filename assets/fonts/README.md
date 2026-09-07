# assets/fonts

このディレクトリにフォントファイルは入れていません。画面の文字は次の順にフォントを探し、
最初に見つかったものを使います（`hd2d/render/text_overlay.cpp` と `hd2d/render/glyph_atlas.cpp`）。

1. `assets/fonts/placeholder_mono.ttf`（このディレクトリに置いたもの）
2. Windows: Consolas、Lucida Console、MS ゴシック
3. Android: Droid Sans Mono、Roboto Mono、Droid Sans

等幅フォントを先に探します。文字の幅が揃っていると、文字グリッドで描く画面
（オープニングやメニューなど）の枠や桁がずれないためです。

好きなフォントを使いたい場合は、等幅の TTF を `placeholder_mono.ttf` という名前でここに
置いてください。半角が全角のちょうど半分の幅になっているフォントを選ぶと、日本語の桁も揃います。
