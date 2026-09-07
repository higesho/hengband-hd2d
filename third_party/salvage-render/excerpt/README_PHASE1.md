# salvage-render excerpt — Phase 1 概念メモ

| 項目 | 内容 |
|------|------|
| 目的 | Phase 1 で参照した salvage 概念の手移植メモ（**コンパイル対象外**） |
| 正典 | / §7、|
| 参照ソース | `C:\Project\XBAND\salvage-render\`（読取のみ） |

## 禁止（絶対）

- `sdl-term.*` / `sdl-init.*` / `sdl-hd2d.*` の **ファイル丸ごとコピー**
- `HengBand_Controler` からのソースコピー
- `sdl-layout.h` をリポジトリへコピーしてリンクすること
- CHAR 45% / PROMPT 42px / `RIGHT_SPLIT_RATIO=0.50` の採用
- `stb_image` 等の資産配置（Phase 1 不要。`.gitkeep` のまま）
- `#include "term/..."`、識別子 `PlayerType` / `angband_terms` / `term_type` / `TERM_XTRA_`

**数値は設計書 §3 が正。** 構成図 PNG・salvage 旧定数は見た目参考のみ。

## Phase 1 でやること（色分けのみ・コード移植は最小）

実装はすべて新規ファイル（`ui/layout/*`、`ui/render/sdl2_renderer.*`）に手書きする。  
`excerpt/` に salvage の `.cpp/.h` を置かない。

### Renderer 生成方針（salvage `sdl-init` 付近の概念）

- Windows では `SDL_HINT_RENDER_DRIVER` の強制は必須にしない。
- まず `SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC)` を試す。
- 失敗時は flags=0 で再試行する。
- 実装本体: `ui/render/sdl2_renderer.cpp`（本 README はメモのみ）。

### 矩形 → 描画座標

- Phase 1 は `SDL_RenderFillRect` のみ。
- 描画先矩形 `dst` = パネルの `RectPx`（`UiLayout::rect(PanelId)`）。
- 永続テクスチャ blit / TTF / PNG は Phase 1 対象外（コピーしない）。

### レイアウト定数

- salvage `sdl-layout.h` の `RIGHT=1/3`・`BOTTOM=1/3` は設計と一致するが、CHAR/PROMPT/等分は捨てる。
- 本設計の定数は `ui/layout/layout_constants.h` に新規定義。
