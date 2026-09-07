/*!
 * @file sdl_ui_options.h
 * @brief SDL2 UI 共通オプション（タイル倍率・音量）。presentation/frame 専属。
 *
 * ui がメニューで書き換え、presentation／platform が読み取る。
 * コア型・SDL 依存なし。
 */
#pragma once

/*!
 * @note 既定値は **`coon` のセーブで実際に遊んでいる設定**に合わせてある（2026-07-30）。
 * `sdl2_ui_options.cfg` が無い環境（配布セットの初回起動）でも、作者が遊んでいる見た目で
 * 始まるようにするため。**環境で変わる値（ウインドウの大きさ）だけは持ち込まない**
 * （`window_w`/`window_h` は 0＝未設定のまま。デスクトップから画面側が決める）。
 */
struct SdlUiOptions {
    /*!
     * @brief タイル表示サイズの段数。**2026-07-31 に 4 → 6**（小さい側へ 2 段）。
     * @details 従来の下限 1/2 は HD タイル（640px の自作タイル）に合わせた大きさで、
     * 「もっと広く見たい」に応えられなかった。8px／16px 版やアスキーアート版
     * （§36）では 1 マスを小さくするほど地図が広く見えるので、下へ 1/3・1/4 を足した。
     * @note **足したのは並びの先頭**（昇順を崩さないため）。したがって
     * 保存済みの `tile_scale_index` は 2 だけずれる。移行は `load_sdl_ui_options` が
     * `options_version` を見て行う（そこの注記を読むこと）。
     */
    static constexpr int kTileScaleCount = 6;
    /*!
     * @brief `sdl2_ui_options.cfg` の版。**意味の変わる索引を足したら上げる。**
     * @details 版が無い cfg（= 1 と見なす）は `tile_scale_index` を 2 段ずらして読む。
     */
    static constexpr int kOptionsVersion = 2;
    //! マップ表示スタイルの段数（HD タイル／16px／8px／アスキーアート）。
    static constexpr int kMapStyleCount = 4;
    static constexpr int kVolumeLevels = 10; //!< 100%..10%（既存 Win 表と同順）
    static constexpr int kMinimapZoomCount = 5; //!< 1 格子あたり 1..5 px
    //! 表示領域。**従来の最大（短辺 46%）を「基本」に据え、そこから 2 段階拡大できる。**
    static constexpr int kMinimapSizeCount = 3;
    static constexpr int kMinimapOpacityCount = 5; //!< 40/55/70/85/100%
    static constexpr int kHd2dPitchCount = 4; //!< 見下ろし角 34/42/50/58 度（版 2.2）
    static constexpr int kHd2dWallHeightCount = 3; //!< 壁の高さ 0.7/1.0/1.4 マス
    //! TiltShift（OFF/弱/中/強）とほこり（OFF/少/中/多）で共通の 4 段。
    static constexpr int kHd2dLevelCount = 4;
    //! サブパネルの枚数（Sub1〜Sub7）。`kSubPanelCount`（game_frame.h）と同じ値。
    static constexpr int kSubPanelCount = 7;
    //! 文字サイズの段数（極小／小／標準／大／特大）。**メインパネルとサブパネルで同じ表**を使う。
    static constexpr int kSubPanelFontCount = 5;
    //! 同上（メインパネル側から読むときの名前。値は同じ 5 段）。
    static constexpr int kMainFontCount = kSubPanelFontCount;
    //! バーチャルパッドの表示濃度（10%…100% の 10 段）。
    static constexpr int kVirtualPadOpacityCount = 10;
    //! バーチャルパッドの大きさ（小／標準／大）。
    static constexpr int kVirtualPadSizeCount = 3;

    /*!
     * @name 下段パネル（Sub1／Sub2／Sub3）の横幅
     * @details 左ブロックの幅を **20 等分**した単位で持つ。3 つの合計は常に
     * `kSubBottomUnits`。20 分割なのは人間の指示（5/20・5/20・10/20）に素直に乗るから。
     * @{
     */
    static constexpr int kSubBottomUnits = 20;
    //! 1 枚の下限。0 にすると枠だけが残って掴めなくなるので 2/20 で止める。
    static constexpr int kSubBottomUnitMin = 2;
    /*! @} */

    //! 0=1/4, 1=1/3, 2=1/2, 3=1, 4=1.5, 5=2（基準 cell_px に対する倍率）。
    //! 既定 4＝1.5 倍（coon の設定。段を足す前は 2 番だったもの）。
    int tile_scale_index{4};

    /*!
     * @brief マップの絵の出し方。**すべて 2D タイル版の派生**（0 が従来）。
     * @details
     * ```
     *   0 = HD タイル      tilework/sfc の 640px 版（既定。従来どおり）
     *   1 = 16px タイル    lib/xtra/graf/16x16.bmp（Adam Bolt。mask.bmp で透過）
     *   2 = 8px タイル     lib/xtra/graf/8x8.bmp （オリジナル。マスク無し＝不透明）
     *   3 = アスキーアート  記号と Term 色をそのままマス目へ
     * ```
     * **変わるのは MainMap の 1 マスに何を描くかだけ**で、マス目の大きさ・レイアウト・
     * ミニマップ・クリック移動・なめらかスクロール・カーソル枠は 0〜3 で共通。
     * 「コアの画面をそのまま映す」のとは別物（それは Term ミラーの仕事）。
     *
     * 1／2 のときだけ Bridge がコアのグラフィックモード（`use_graphics` /
     * `ANGBAND_GRAF`）を立て、`map_info` から**タイル面の行・列**を受け取る。
     * 索引の持ち主はコアの prf（`graf-new.prf` / `graf-xxx.prf`）なので、
     * UI 側に対応表を持たない。
     * @note 0 以外は **SDL2 経路専用**。HD2D は HD タイルを立体に置く作りなので、
     * 派生スタイルを選んだら描画経路を SDL2 へ落としていた（旧 2D UI。いまの画面に SDL2 経路は無い）。
     */
    int map_style_index{0};
    bool sound_enabled{true};
    bool music_enabled{true};
    //! 0=100% … 9=10%（SOUND_VOLUME_TABLE / music VOLUME_TABLE と同インデックス）
    int sound_volume_index{0};
    int music_volume_index{0};

    //! ミニマップ（メイン画面右上に常時表示）
    bool minimap_enabled{true};
    //! 0..4 → 1 格子 1..5 px。既定 4＝5px（coon の設定）。
    int minimap_zoom_index{4};
    //! 0..2 → MainMap 短辺に対する占有率（基本 46% / 拡大 58% / 最大 70%）。既定 2＝最大（coon の設定）。
    int minimap_size_index{2};
    //! 0..4 → 40/55/70/85/100%。ドットと下地の不透明度に一律で掛ける。
    int minimap_opacity_index{2};

    /*!
     * @brief K-20: カーソル操作モード。**全ての操作をカーソル＋決定で行えるようにする。**
     * @details 真のとき presentation が毎フレーム、コアの
     * `use_menu`（`io/input-key-requester.h`）と `command_menu`
     * （`game-option/input-options.h`）を立てる。コアはこの 2 つが真だと
     *   - Enter でコマンドメニュー（`inkey_from_menu`）を開き
     *   - 持ち物・床・呪文・特殊能力・超能力・ペット・ターゲットの各選択で
     *     自前のカーソル `》` を出す
     * ようになる。**コアが元から持っている「メニュー操作」を常時 ON にするのが本体**で、
     * UI 側はそれを検出して枠を描き（`GameFrame::menu_core_cursor`）、コアにカーソルが
     * 無い画面（建物・店）だけ UI 側カーソル（K-16）で補う。
     * @note 偽にすると従来（文字キー主体）に戻る。呪文選択だけはコアの実装上、
     * メニュー中に文字キーを受け付けない（`cmd-spell.cpp:405`）ので、
     * 文字で選びたい利用者のために切れるようにしてある。
     */
    bool cursor_mode_enabled{true};

    /*!
     * @brief K-49: ボット用 JSON 出力（`src/bot/bot-json-output.cpp`）。**既定 OFF**。
     * @details 真にすると、コアがプレイヤーの入力を待つ直前に状態のスナップショットを
     * JSON Lines で 1 行書く。出力先は `arg_bot_json_output_path`（既定 `bot-state.jsonl`。
     * `--bot-json-output=<path>` / `HENGBAND_BOT_JSON=<path>` で変えられる）。
     * @note **1 行あたり数 MB ある**（既知マスを全部出すため）。普通に遊ぶときは要らないので
     * 既定は OFF。コマンドラインや環境変数で入れた場合は、この設定に関わらず ON になる
     * （`presentation/bootstrap/sdl_game_bootstrap.cpp` の `parse_bot_json_output_args`）。
     */
    bool bot_json_enabled{false};

    /*!
     * @brief K-26: 各サブパネル（Sub1〜Sub5）に映す**コアのサブウインドウの種類**。
     * @details 値はコアの `SubWindowRedrawingFlag` の番号（`window_flag_desc` と同じ順）。
     * **-1 は「UI 既定」**で、従来の作り込み表示（Sub1=メッセージ / Sub2=装備 /
     * Sub3=視界モンスター / Sub4=ステータス / Sub5=持ち物）を出す。
     * presentation が毎フレーム `g_window_flags[1+i]` へ写し、コアの `fix_*` が
     * 専用 Term に描いた内容を Bridge が `GameFrame::sub_panels` へ読み取る。
     * @note 既定は人間が実機で決めた構成。番号は `SubWindowRedrawingFlag` の
     * MESSAGE=6 / EQUIPMENT=1 / MONSTER_LORE=8 / PLAYER=3 / INVENTORY=0。
     * ```
     *   Sub1 下段・左   = メッセージ          (6)   幅 5/20
     *   Sub2 下段・中央 = モンスターの思い出  (8)   幅 5/20
     *   Sub3 下段・右   = 装備/持ち物一覧     (1)   幅 10/20  ← 1 行が長いので広く取る
     *   Sub4 右上       = キャラクタ情報      (3)
     *   Sub5 右下       = 持ち物/装備一覧     (0)
     * ```
     * **`sub_bottom_w20` の既定（5/5/10）とこの並びは対応している。** 片方だけ変えると
     * 「広い枠に短い本文」「狭い枠に長い本文」になるので、直すときは両方見ること。
     * @note **キャラクターごとの構成はセーブの隣の `<セーブ名>.sdl2panels` が持ち主**
     * （`presentation/term/sdl_sub_window_terms.h`）。それが**在れば優先**され、
     * 無いときにここの値が使われる。つまりこの既定が効くのは
     * 「新規キャラクター」と「まだ構成を触っていないキャラクター」。
     */
    //! 既定は従来の 5 枚ぶんだけ埋め、増えた 6・7 枚目は「UI 既定」（-1）にしておく。
    int sub_panel_kind[kSubPanelCount]{ 6, 8, 1, 3, 0, -1, -1 };

    /*!
     * @brief サブパネルの文字サイズ（0=極小 … 4=特大）。**既定 0＝極小 (16pt)**（coon の設定）。
     * @details コアのサブウインドウを映すパネルでは、**文字が小さいほど Term の桁・行が増える**
     * （パネルの実寸 ÷ 字送りで Term サイズを決めているため）。持ち物や思い出をもっと入れたい／
     * 大きく読みたい、を利用者が選べるようにするための設定。既定を極小にしてあるのは、
     * 装備一覧の 1 行（名前＋重量＋部位）が折り返さずに収まる桁数が要るから。
     */
    int sub_panel_font_index{ 0 };

    /*!
     * @brief メインパネル（MainMap に映すコアの Term ミラー）の文字サイズ。
     * @details オープニング・店・建物・持ち物・オプションなど、**コアが 80×24 の文字で
     * 描く画面はすべてここを通る**。小さくすると 1 行に入る桁が増え、80 桁の画面が
     * 横に切れにくくなる（`truncate_utf8_for_width` で切っている）。
     * 段はサブパネルと同じ 5 段（16/19/22/26/30pt）。
     * @note **既定 1＝小 (19pt)**。従来は 22pt 固定だったので、そこから 1 段小さい。
     */
    int main_font_index{ 1 };

    /*!
     * @brief 下段パネル（Sub1=左／Sub2=中央／Sub3=右）の横幅。単位は左ブロックの 1/20。
     * @details 既定は **5 / 5 / 10**（右を半分、左と中央を 1/4 ずつ）。人間の指示。
     * 右が広いのは、そこに置く**装備/持ち物一覧が 1 行の長い**（名前＋重量＋部位）ため。
     * 左のメッセージと中央のモンスターの思い出は折り返しが効くので 1/4 で足りる。
     * 合計が `kSubBottomUnits` から外れた cfg を読んだときは
     * `normalize_sub_bottom_widths()` が均等割りへ戻す（壊れた設定で画面が消えないため）。
     * @note 変更は機能メニュー「下段パネルの幅」と、**境界を指／マウスで掴んで動かす**
     * 操作（画面側）の両方から行える。どちらも同じ値を書く。
     */
    int sub_bottom_w20[3]{ 5, 5, 10 };

    /*!
     * @brief タッチ操作用のバーチャルコントローラーを画面に出すか（Android）。
     * @details 左に 3×3 の方向パッド、右に A / B / X / Y / Start / Select を置く。
     * 各ボタンの役割は**実機パッドと同じ割り当て表**（`sdl2_pad_binds.cfg`）を使うので、
     * 機能メニューの「キーバインド」でそのまま変えられる。
     * @note 物理キーボードやゲームパッドを繋いだときは邪魔になるので切れるようにしてある。
     * Windows ビルドでも設定自体は保持する（同じ cfg を行き来させても失わないため）。
     * @note **既定はタッチ端末（Android）だけ ON**。Windows・Linux はキーボードと実機パッドが
     * 前提なので、出しても地図を覆うだけで邪魔になる（機能メニュー「タッチパッド」で出せる）。
     * cfg に `virtual_pad` が書かれていればそちらが優先される（設定は失わない）。
     */
#if defined(__ANDROID__)
    bool virtual_pad_enabled{ true };
#else
    bool virtual_pad_enabled{ false };
#endif

    /*!
     * @brief バーチャルコントローラーの表示濃度（0..9 → 10%…100%）。
     * @details 地図の上に重ねるので、薄くすれば下が透けて見え、濃くすれば押す位置が分かる。
     * **押した瞬間だけは濃く**光らせる（薄い設定でも反応が分かるように）。
     * 既定は index 5（60%）。
     */
    int virtual_pad_opacity_index{ 5 };

    //! バーチャルコントローラーの大きさ（0=小 / 1=標準 / 2=大）。画面短辺に対する比で決める。
    int virtual_pad_size_index{ 1 };

    /*!
     * @brief K-34: 画面モード。false = 全画面（既定・従来）／true = ウインドウ。
     * @details 全画面は `SDL_WINDOW_FULLSCREEN_DESKTOP`（デスクトップ解像度のまま覆う）。
     * ウインドウは `SDL_WINDOW_RESIZABLE` で、**枠を掴んで自由に伸縮できる**。
     * レイアウト（`UiLayout::recompute`）も地図の可視範囲（`before_capture`）も
     * 毎フレーム実寸から引き直しているので、伸縮にはそのまま追随する。
     * @note 切替は**窓を作り直さない**（`SDL_SetWindowFullscreen` / `SDL_SetWindowResizable`）。
     * HD2D の切替と違って GL 属性が変わらないため、窓ごと作る必要が無い。
     */
    bool window_mode_enabled{false};

    /*!
     * @brief ウインドウモードのときの窓の大きさ（px）。**0 は「未設定」**。
     * @details 前回ウインドウで遊んだ大きさを覚えておき、次の起動と次の切替で復元する。
     * 未設定のときは画面側がデスクトップから既定を決める。
     * 下限（1280×720）は画面側の窓の最小寸法と同じ値で丸める。
     */
    int window_w{0};
    int window_h{0};

    //! true = HD2D（OpenGL）経路で描く。**旧 2D UI では `HENGBAND_SDL2_HD2D=1` でビルドした
    //! 実行ファイルでのみ意味を持つ**（0 のビルドでは切り替えても SDL2 のまま）。
    //! 設計書 §9 で**即時反映**に変更した（画面側が窓ごと作り直す）。
    //! **既定 true**。GL を用意できない環境では旧 2D UI が SDL2 経路へ退避し、
    //! 理由を機能メニューのステータス行に出していた（G-3）。**いまの画面に退避先は無い。**
    bool hd2d_enabled{true};

    //! HD2D 表示設定（設計書 §10）。HD2D 非搭載ビルドでも値は保持・保存する
    //! （同じ cfg を HD2D 付きビルドと行き来させても設定を失わないため）。
    //! 0..3 → 見下ろし角 34/42/50/58 度（設計書 版 2.1 で 1 点透視向けに引き直した表）。
    //! 1 点透視では地面の縦横比が `tanθ` に固定されるため、旧表の 75 度（3.73）・
    //! 60 度（1.73）は床が横縞に潰れて実用外だった。実用域を 4 段へ割り直してある。
    //! **既定は index 2（50 度）**（coon の設定）。42 度より奥行きが素直に読め、
    //! 壁 1.4 と組んでもかきわりの足元が隠れすぎない。
    int hd2d_pitch_index{2};
    //! 0..2 → 壁ブロックの高さ 0.7/1.0/1.4 マス。既定 2＝1.4 マス（coon の設定）。
    int hd2d_wall_height_index{2};
    //! 0..3 → TiltShift OFF/弱/中/強。既定 3＝強（coon の設定）。
    int hd2d_tiltshift_index{3};
    //! 0..3 → ほこり OFF/少/中/多。既定 3＝多（coon の設定）。
    int hd2d_dust_index{3};
    //! 遮蔽物の裏のスプライト・階段を半透明で透かす（要件 R7）。
    bool hd2d_xray_enabled{true};
    /*!
     * @brief K-40: 転移（帰還・テレポート）でほこりが青く収束して弾ける演出。
     * @details ほこりが OFF でも演出の間だけ粒を出す（転移の合図が消えてしまわないように）。
     * 演出そのものが目に障る場合はここで止める。
     */
    bool hd2d_teleport_fx_enabled{true};

    /*!
     * @name リアルタイムモード
     * @details ここに置くのは**セーブのオプションビットに触らないため**。
     * コアの `option-types-table.cpp` に足すと割り当てが動く。
     * 値は起動時と機能メニューの操作時に `RealtimeClock` へ写す。
     * @{
     */
    //! true = 実時間でゲームが進む。**既定 OFF**（従来どおりのターン制）。
    bool realtime_enabled{ false };
    /*!
     * @brief 進行の速さ。1 行動（通常速度で 10 刻み）あたりの秒数の表への添字。
     * @details 既定 index 4 ＝ 1.0 秒（「1 秒 1 ターン」と決めた）。
     * **加速すればこれより速く動ける**（実時間に固定されるのは刻みであって行動ではない）。
     */
    int realtime_speed_index{ 4 };
    //! 小窓の裏でも世界を進めるか（設計書 §7。既定は進む）。
    bool realtime_prompt_live{ true };
    /*!
     * @brief 速さの振れ幅の段（設計書 §4-2）。**自分の行動間隔がここまでしか変わらない。**
     * @details はみ出た分は世界の拍へ載るので、**どれを選んでも有利不利は変わらない**。
     * 既定 1 ＝ 2.0 倍（加速しても自分は最速 2 倍まで、残りは世界がスローになる）。
     */
    int realtime_self_span_index{ 1 };
    static constexpr int kRealtimeSelfSpanCount = 5;
    //! 段 → 振れ幅（1.0 = 自分の拍は不変・世界だけ伸縮）。
    static double realtime_self_span(int index);
    static const char *realtime_self_span_label(int index);
    static constexpr int kRealtimeSpeedCount = 8;
    //! 添字 → 1 行動あたりの秒数。
    static double realtime_seconds_per_turn(int index);
    //! 添字 → 機能メニューに出す表示（"1.0 秒/行動" など）。
    static const char *realtime_speed_label(int index);
    /*! @} */

    static int tile_scale_num(int index);
    static int tile_scale_den(int index);
    static const char *tile_scale_label(int index);

    /*! @name マップ表示スタイル（`map_style_index`） @{ */
    static const char *map_style_label(int index);
    /*!
     * @brief そのスタイルが使うコアのタイル面の一辺（px）。0＝コアのグラフィックモードを使わない。
     * @details 8／16 のときだけ Bridge が `use_graphics` を立てる。
     */
    static int map_style_graf_px(int index);
    //! `graf-*.prf` の切替に使う `ANGBAND_GRAF` の値（"ascii" ならグラフィックモードにしない）。
    static const char *map_style_graf_tag(int index);
    //! 記号を等幅で描くスタイル（アスキーアート）か。
    static bool map_style_is_ascii(int index);
    /*! @} */
    //! 表示用パーセント（100,90,…,10）
    static int volume_percent(int index);
    //! 1 格子あたりの画素数（1..5）
    static int minimap_px_per_grid(int index);
    static const char *minimap_size_label(int index);
    //! MainMap 短辺に対する占有率（百分率）
    static int minimap_size_percent(int index);
    //! 不透明度（百分率）。40/55/70/85/100
    static int minimap_opacity_percent(int index);

    //! サブパネルの文字サイズ（TTF の pt）。16/19/22/26/30
    static int sub_panel_font_pt(int index);
    static const char *sub_panel_font_label(int index);
    //! メインパネルの文字サイズ。**段はサブパネルと同じ表**（別々に持つと片方だけずれる）。
    static int main_font_pt(int index) { return sub_panel_font_pt(index); }
    static const char *main_font_label(int index) { return sub_panel_font_label(index); }

    /*!
     * @brief `sub_bottom_w20` を「合計 20・各 2 以上」へ丸める。
     * @details 読み込み直後と、幅を動かした直後に必ず通す。
     */
    void normalize_sub_bottom_widths();

    /*!
     * @brief 下段パネル `index`（0..2）の幅を `delta` 単位動かし、隣から融通する。
     * @param index 動かす枚（0=左 / 1=中央 / 2=右）
     * @param delta 増減（単位は 1/20）
     * @return 実際に動いたら true
     * @details **合計を 20 に保つ**のが要点。増やす側の相手は「右隣、無ければ左隣」。
     * 下限に当たっている相手からは取らない（掴めない枚を作らない）。
     */
    bool nudge_sub_bottom_width(int index, int delta);

    //! バーチャルパッドの不透明度（百分率）。10,20,…,100
    static int virtual_pad_opacity_percent(int index);
    //! バーチャルパッドの大きさ＝画面短辺に対する方向パッド一辺の割合（百分率）。
    static int virtual_pad_size_percent(int index);
    static const char *virtual_pad_size_label(int index);

    //! 見下ろし角（度）＝画面中央へ向かう視線の伏角。大きいほど強く見下ろす。
    //! 1 点透視では 90 度（真上）は原理的に取れない（真下は画像平面上で無限遠）。
    static double hd2d_pitch_degrees(int index);
    static const char *hd2d_pitch_label(int index);
    //! 壁ブロックの高さ（マス）。
    static double hd2d_wall_height(int index);
    static const char *hd2d_wall_height_label(int index);
    //! TiltShift の段（OFF/弱/中/強）。強度そのものは描画側が index から決める。
    static const char *hd2d_tiltshift_label(int index);
    //! ほこりの段（OFF/少/中/多）と粒子数（0/96/192/320）。
    static const char *hd2d_dust_label(int index);
    static int hd2d_dust_particle_count(int index);
};

SdlUiOptions &sdl_ui_options();

//! 設定変更後に presentation 側が登録する適用フック（音量同期・BGM 再選曲など）。
using SdlUiOptionsApplyFn = void (*)();
void set_sdl_ui_options_apply_hook(SdlUiOptionsApplyFn fn);
void apply_sdl_ui_options();

void load_sdl_ui_options(const char *path = "sdl2_ui_options.cfg");
void save_sdl_ui_options(const char *path = "sdl2_ui_options.cfg");
