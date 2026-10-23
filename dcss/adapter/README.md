# DCSS コアのアダプタ（M0: Windows で組み、ウェブタイル版の JSON を外へ出す）

最終更新: 2026-10-21。§1〜§8 は「Windows で DCSS のコアの exe を組み、ウェブタイル版の JSON を外へ出せるようにする」担当の記録（仮の入出力）。
**§9 以降が段 M0 の仕上げ**（画面 HengbandHd2d.exe とプロトコル v1 で話し、画面の中で遊べる）の記録。地形の名寄せ・絵は別の担当（`dcss/terrain/`・`dcss/assets/`）。

出どころの書き分け: 「確認」は実際に組む・起こすなどして確かめたこと。「推測」は所見。「未確認」は試していないこと。

## 1. 結論

- **確認**: 原作（日本語版フォーク crawl-ja の `0.34.1-ja2`。2026-10-22 に `0.34.1-ja4` へ上げ、同じ確かめを通した。 4.6）の `.cc` を 1 行も書き換えずに、Windows（MSVC・x64）で
  ウェブタイル版（`USE_TILE_WEB`）を組めた。`DcssCore.exe` は 14.2 MB（プログラムのデータベース 96 MB は別）。
- **確認**: 起こして、起動メニュー → 名前 → 種族 → 職業 → 武器 → ゲームに入る → 数歩歩く → 所持品 → 標準入力を閉じて
  保存・終了、まで通した。出る JSON は UTF-8 で、日本語が入っている（記録は `samples/m0_boot_to_walk.jsonl`）。
- **確認**: 原作の書き換えが要る所は、いまのところ **0 行**（§4）。

## 2. 組み方

必要な道具（どれも PATH に無い。場所は  と §7）:

| 道具 | 用途 | この PC の場所 |
| --- | --- | --- |
| Python 3 + PyYAML | `util/gen-all.py` 以下の生成スクリプト | PyYAML の入った Python（`python`。PyYAML 6.0.1 で確認） |
| Perl | `gen_ver.pl`・`art-data.pl` など 8 本 | `C:\Program Files\Git\usr\bin\perl.exe`（Git for Windows 同梱でよい） |
| MSVC（cl・MSBuild） | 組む | `C:\Program Files\Microsoft Visual Studio\18\Community`（`vcvars64.bat`・`MSBuild\Current\Bin\amd64\MSBuild.exe`）。ツールセットは v145 |
| git | 版の取得（`git describe`） | PATH にある |

手順は 2 つ。

```powershell
# 1. 生成物を作る（原作の写しを汚さない）。作業フォルダは短くする（§6 の罠）
python dcss\adapter\prepare_gen.py --work %TEMP%\dcss_w
# 2. 組む（既定の原作の置き場はリポジトリの隣の ..\roguelike-cores\build-sources\dcss\crawl-ref\source。`python tools/core_sources/prepare.py` が作る）
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" `
  VisualStudio\DcssCore\DcssCore.vcxproj /m /p:Configuration=Release /p:Platform=x64 `
  /p:DcssGenRoot=%TEMP%\dcss_w\gen
```

- `prepare_gen.py`: 原作を `<work>\mirror` に複製し（数秒）、`tilegen` を `cl` で組み、`build.h`・`compflag.h`・`config.h`・
  `util/gen-all.py` の生成部分・タイルの表を作って、**生成物だけ**を `<work>\gen` に集める。全部で約 38 秒（確認）。
  `mirror` は実行時のデータ置き場（`dat/`・`docs/`）にもなる。
- `DcssCore.vcxproj` のプロパティ（`/p:` で替えられる）:
  `DcssSourceRoot`（原作の `crawl-ref/source`。`.cc` をここから読む。あとで `build-sources` へ移すときはこの 1 つを替える）、
  `DcssGenRoot`（生成物）、`OutDir`（既定はリポジトリ直下 `DcssCore.exe`）。
- かかった時間（確認。論理コア 24）: 全部作り直しで約 51 秒、変更なしの再ビルドで約 2 秒。
- **`DcssGenRoot` に `mirror` 全体を指してはいけない。**同名のヘッダが原作と複製の両方から読まれ、二重定義になる（実際に踏んだ）。
- 試した結果: 原作の `MSVC\crawl.vcxproj` の `Release Console|x64` は**そのままでは使えない**（確認）。出力先が原作のツリーの中、
  組む前の処理が `git describe` と PNG（`rltiles/main.png`）を要求して止まる、`SDL2.lib`・`libpng.lib` などの付属ライブラリが同梱されていない。
  このため独自の `DcssCore.vcxproj` を作った。`Hengband.sln` にはまだ載せていない。

## 3. 差し替えの方法（原作を書き換えずに）

原作のウェブタイル版は Unix の前提が 2 層ある。どちらも「同名のファイルを別の場所に置く・別のファイルで置き換える」で処理した。

| 原作の依存 | 差し替え | 場所 |
| --- | --- | --- |
| `tileweb.cc` の UNIX ドメインの `SOCK_DGRAM` ソケット（`socket`・`bind`・`setsockopt`・`sendto`・`recvfrom`・`select`・`pselect`・`sigprocmask`・`gettimeofday`） | MSVC に無い `<sys/socket.h>`・`<sys/un.h>`・`<sys/time.h>` を、同じ名前の互換ヘッダとしてインクルードパスの先頭に置く。関数の宣言はそこ、実体は標準入出力の仮の実装 | `shim\sys\*.h`、`dcss_posix_shim.cpp` |
| 端末の層（Unix は curses の `libunix.cc`、Windows は `libw32c.cc`）。ウェブ版の送受信は `libunix.cc` の中にだけある | `libconsole.h` の関数を `libunix.cc` の「-headless」の分岐と同じ振る舞いで実装し直す（描画せず `tiles.*` に伝えるだけ）。両ファイルは組まない | `libhd2d.cpp` |
| `AppHdr.cc` が `#pragma comment(lib, "pcre.lib")` などを持つ（プリコンパイル済みヘッダを作る役も兼ねる） | 同じ役だけを持つ `dcss_pch.cpp` を使い、付属ライブラリ（lua・sqlite・pcre・zlib）は C のソースを直接この exe に入れる | `dcss_pch.cpp`、`DcssCore.vcxproj` |
| `main.cc` が `nl_langinfo(CODESET)` で UTF-8 のロケールを確かめる（`<langinfo.h>` は Windows ではインクルードされない） | `main.cc` だけに強制インクルード（`/FI`）で互換の定義を差す（常に `"UTF-8"`）。強制インクルードはプリコンパイル済みヘッダと両立しないので `main.cc` だけ使わない | `shim\dcss_force_langinfo.h` |
| `windows.h`（`threads.h` 経由で `crash.cc` がインクルードする）が `PURE` を `= 0` に再定義し、`coord.h` の宣言を壊す | プロジェクトの定義 `WIN32_LEAN_AND_MEAN` で `ole2.h`（`basetyps.h`）を引き込ませない | `DcssCore.vcxproj` |
| `tileweb.cc` と `windows.h` の `winsock.h`（`timeval`・`select` の二重宣言） | `tileweb.cc` と `dcss_posix_shim.cpp` にだけ `_WINSOCKAPI_` を定義し、プリコンパイル済みヘッダを使わない | `DcssCore.vcxproj` |

### 3.1 仮のプロトコル（M0 だけ）

- **送る**: `tileweb.cc` が組んだ JSON を、1 メッセージ 1 行（改行区切り）で**標準出力**へ、または環境変数 `DCSS_JSON_OUT` のファイルへ書く。
  原作は 2048 バイトごとに分割して `sendto` するので、行末の改行までを連結すると元の 1 行に戻る（確認）。
  行頭が `*` の行は原作ではウェブサーバ宛ての制御メッセージ（`exit_reason`・`dump`・`milestone` など）。
- **受ける**: **標準入力の 1 行 = 1 制御メッセージ**（例 `{"msg":"key","keycode":13}`、`{"msg":"text_input","text":"Taro"}`、
  `{"msg":"click_cell",...}`）。読むのは専用のスレッド。
- **接続の印**: 原作はウェブサーバが `{"msg":"attach","primary":true}` を送ってくるまで何も送らない。ソケットの `bind()` の直後に、
  こちらでその 1 行を先頭に足す。起動の引数に **`-webtiles-socket <名前> -await-connection` が必須**（名前は使われない。
  付けないと `m_sock_name` が空になり、入力待ちが空回りする。推測: 画面が固まる）。
- **終了**: 標準入力が閉じたら、原作の SIGHUP と同じ扱い（`crawl_state.seen_hups`）にして Esc を返し続ける。原作が保存して終わる
  （確認: 閉じてから 0.2 秒で終了コード 0、`saves\*.cs` ができる、`exit_reason`・`dump` の行が出る）。
- 画面側の正式な枠（`CORE_PROTOCOL_V1.md` の長さ付き JSON）への置き換えは M1 以降。差し替える所は `dcss_posix_shim.cpp` の
  `sendto`/`recvfrom` と、`libhd2d.cpp` の起動処理だけの見込み（推測）。

### 3.2 起こし方

```powershell
DcssCore.exe -dir <保存先> -rc <保存先>\init.txt -webtiles-socket x -await-connection
```

- 言語は `init.txt` に `language = ja`。
- データ（`dat/`・`docs/`）は、exe の親フォルダ、または `-dir` / `CRAWL_DIR` の下から探される（`files.cc` の `_get_base_dirs`、確認）。
  検査では exe の隣にジャンクションを置いた。
- 初回は説明文のデータベースと地形定義の索引を保存先に作る（`db\`・`des\`）。起動から最初のメニューまで、初回 4.3 秒、2 回目以降 1.8 秒（確認）。
- `m0_check.py` が一連の確かめを自動で行う（§5）。

## 4. 原作の書き換えが要る所

**いまは 0 行。**§3 の差し替えだけで、`.cc` もヘッダも書き換えずに組めて、遊びの筋書きが通った。
次は「いずれ書き換えたくなるかもしれない所」であって、今は要らない（推測を含む）。書く前に遊ぶ人に聞く。

| 所 | 今の状態 | 書き換えたくなる場面 |
| --- | --- | --- |
| `libunix.cc` の `delay()` に相当する待ち | `libhd2d.cpp` の `delay()` は `{"msg":"delay","t":ms}` を送るだけで**眠らない**。原作は送ってから `usleep` する | ビームの演出を画面側が受け取って描くとき、コアが先に進みすぎないようにするなら、待ちの方針を決める（コア側のアダプタで足りる見込み） |
| `end.cc` の `exit()` | そのまま（プロセスが終わる。Windows の別プロセスの形では問題ない） | Android で 1 プロセスに入れるとき（`DCSS_CORE_STUDY.md` §5.6）。ほかのコアの `rl_exit` と同じ差し替えが要る |
| `bolt::draw`・`bolt::explode` | 触っていない。外へ出るのは重ねたタイルと色だけ（`HD2D_STUDY.md` §3.6） | 魔法の弾・爆発を `projections` で出すとき。フォークなので書き換えられるが、遊ぶ人の「原作に手を入れない」が最優先の約束 |

## 5. 確かめた事実: 出た JSON の種類

`python -u dcss\adapter\m0_check.py --exe <exe> --data-root <work>\mirror\crawl-ref\source --run <検査用の一時フォルダ>`
（合格・不合格を返す。起こした exe は自分の PID だけを止める）。記録は `samples\m0_boot_to_walk.jsonl`（138 行、約 70 KB）。

起動から終了までに出た `msg` の種類と数（確認）:

`version` 1、`exit_reason` 3、`options` 2、`layout` 2、`menu` 2、`txt` 6、`ui-push` 3、`ui-pop` 3、`ui-state` 3、`ui-state-sync` 4、
`ui_state` 3、`close_menu` 2、`close_all_menus` 2、`milestone` 5、`player` 13、`msgs` 3、`map` 19、`cursor` 1、`input_mode` 36、`flush_messages` 24、`dump` 1。

| 種類 | 出る場面と中身（確認） |
| --- | --- |
| `version` | 起動直後。`{"msg":"version","text":"Dungeon Crawl Stone Soup 0.34.1-ja4"}`（`samples/` の記録は ja2 のときの物） |
| `options` / `layout` | 起動直後と、ゲームに入るとき。HP/MP の色の閾値などの設定、メッセージ欄の高さ |
| `menu` + `txt` | 起動メニュー。`{"msg":"menu","type":"crt",...}` に続いて `{"msg":"txt","id":"menu_txt","lines":{"3":"<span class=\"fg7 bg0\">名前を入力: ..."}}`。行ごとの HTML（色は `fgN bgN`）。入力に応じて変わった行だけが再送される |
| `ui-push` / `ui-pop` / `ui-state` | キャラクター作成の選択（`"type":"newgame-choice"`）。`main-items.menu_id` が `species-main`・`background-main`・`weapon-main`。`buttons[]` に `hotkey`・`description`・`labels`（日本語）・`tile`（タイル番号）。例: ノール `a`、人間 `j` |
| `menu`（`"tag":"inventory"`） | `i` キーの所持品。`items[]` に `text`（`" a - レイピア (+0) (武器)"`）・`hotkeys`・`tiles`。`title.text`・`more`（キー案内）つき |
| `player` | 能力値・HP・装備や所持品の一覧（`inv`）・称号・位置（`pos`）。歩くたびに 1 回 |
| `map` | `cells[]`。最初の `x`,`y` だけ座標で、続くセルは座標を省いて順に並ぶ。`f`（地形の列挙値）、`mf`（マップの印）、`g`（字）、`col`、`t.bg` / `t.ov`（タイル番号）。差分で送られる（歩くと変わったセルだけ） |
| `msgs` | メッセージ欄。`messages[].text` は色のタグ付きの日本語、例 `"<yellow>ようこそ、人間の戦士『Taro』よ。<lightgrey>"` |
| `input_mode` | 入力待ちの種類。`{"msg":"input_mode","mode":1}`（0 と 1 が出た。値の意味は原作の `MOUSE_MODE_*`、未突合） |
| `milestone` / `exit_reason` / `dump` | サーバ宛ての制御メッセージ（行頭が `*`）。終了時は `exit_reason` が `saved`、続いて `disconnect` + `"message":"ゲームを保存しました。"`、`dump` が `{"type":"save","filename":"..."}` |

- 日本語は UTF-8 で入っている（記録全体が UTF-8 として有効、日本語の文字 約 4,000 個）。`language = ja` の設定でフォークの訳が出た。
- 歩いた位置の列: `(0,0) → (1,0) → (2,0) → ...`（`player.pos`）。キー `l`・`h`・`j`・`k` で歩ける。
- 床のアイテムは `t.ov` のタイル番号だけで、種類の値は無い（`DCSS_CORE_STUDY.md` §4.3 のとおり。今回は未確認）。

## 6. 罠

- **作業フォルダは短くする。**原作の最長のパスは 118 文字（`rltiles/mon/tentacles/...`）。`tilegen` は PNG の有無を調べるだけでも
  通常のパス（260 文字まで）で開くので、作業フォルダが深いと `couldn't load image` で止まる。`prepare_gen.py` が 255 文字を超えそうなら止める。
  このセッションの作業用フォルダは深かったため、一時フォルダの `dcss_w`（`%TEMP%\dcss_w`）を使った。
- **名前の欄は前回の名前が入っている**（起動メニューの名前欄に `Taro` を送ったら `TaroTaro` になった。推測: `saves\start-ns.prf` に前回の名前が残る）。
  検査は保存先を空にして起こす。
- ログに出る日本語は、Windows の端末にそのまま出すと化ける。ファイルへ書いて UTF-8 で読むこと。
- 入力が 4096 バイトを超える制御メッセージは受けられない（原作の `buf[4096]`）。
- 標準出力は JSON 専用。原作が起動時の異常を `printf` するとプロトコルに混ざる（未確認）。標準エラーが空であることは確認した。
- `usleep` の引数は 1000 の倍数でなければならない（原作の `syscalls.cc` の `ASSERT`）。`tileweb.cc` の呼び出しは満たしている。

## 7. 残り

- Debug 構成・Win32 の確認（Release x64 だけ確認した）。`Hengband.sln` への追加、`tools/core_sources` の `manifest.json`・レシピへの登録。
- 画面側のプロトコル v1（`CORE_PROTOCOL_V1.md`）の枠（`hello`・`hello_ack`・`core_name`・`features`）への置き換えと、コア選択画面への登録。
- 組み立ての前処理を `tools/core_sources/` の流れ（`prepare.py` → `build-sources`）に載せる。いまの `prepare_gen.py` は単独で動く。
- セーブの再開（保存した `.cs` からの続き）、死亡・勝利、ターゲット選択、マウス、ウィンドウの大きさ変更（端末の大きさは 80×24 の固定）、
  長時間の連続プレイは未確認。
- 端末の大きさ・`reduce_animations`・`use_animations` などの既定値の決め（演出を切るか）。
- `-dir` / `CRAWL_DIR` を `HENGBAND_DATA_ROOT` の下へ向ける（保存先）と、`dat/` の置き場の整理。
- 地形の名寄せ（221 種）、タイルの絵、怪物の合成の絵、HUD の写しは別の担当（`dcss/terrain/`・`dcss/tiles/` ほか）。
- ライセンス: 原作は GPL 第 2 版以降。アダプタを同じ exe にリンクしているので、配布の前に `DISTRIBUTION_POLICY.md` との整合を確かめる。

## 8. ファイル

| ファイル | 役 |
| --- | --- |
| `prepare_gen.py` | 生成物（ヘッダ・タイルの表）を作り、`gen` に集める |
| `libhd2d.cpp` | 端末の層（`libconsole.h`）の差し替え実装 |
| `dcss_posix_shim.cpp` / `.h` | `tileweb.cc` が使う POSIX のソケットまわりの仮の実装 |
| `shim\sys\{socket,un,time}.h` | 互換ヘッダ（`tileweb.cc` / `tileweb.h` がインクルードする） |
| `shim\dcss_force_langinfo.h` | `main.cc` 用の強制インクルード |
| `dcss_pch.cpp` | プリコンパイル済みヘッダを作る |
| `m0_check.py` | M0 の確かめ（起こして JSON を記録し、種類・UTF-8・終了を判定） |
| `samples\m0_boot_to_walk.jsonl` | 出た JSON の記録 |
| `..\..\VisualStudio\DcssCore\DcssCore.vcxproj` | 組み立て |

---

## 9. 段 M0 の仕上げ: HD2D の画面の中で遊べる（2026-10-21）

### 9.1 結論（出どころ: 確認＝実際に組んで画面から遊んだ）

- **確認**: `DcssCore.exe --ui-protocol=stdio`（画面が起こす形）で、握手（`hello` / `hello_ack`。`core_name=dcss`・`features=["lang-restart"]`）→ 起動メニュー → キャラクター作成
  → ゲームに入る → 歩く → 所持品 → 保存して終了 → 起こし直して続きから、までを**画面の操作だけ**で通した（`tools/dcss/dc_m0_check.py`）。
- 引数 `--ui-protocol=stdio` が無い起こし方は、従来の仮の入出力（§3.1）のまま（`m0_check.py` が使う）。
- 原作の書き換えは **0 行**のまま。原作の `main()` だけ、組み立てで名前を替える（vcxproj の `main.cc` の定義 `main=dcss_original_main`）。

### 9.2 層の構成

| ファイル | 役 |
| --- | --- |
| `dcss_main.cpp` | 本物の `main()`。標準出力をプロトコル専用にし（CRT の標準出力は標準エラーへ）、握手・命令表（`pad_commands`）・サブパネルの種類を送る。受信スレッドが画面の入力（`keys`・`input_event`・`ui_state`・`quit_request`）を原作の制御メッセージ（`{"msg":"key","keycode":N}`）に直して渡す。原作の `main` を呼び、終わったら `exit` を送る |
| `dcss_frame.cpp` / `.h` | 原作（`tileweb.cc`）が出す JSON を取り込んで状態を持ち、`GameFrame` に組む。地図（差分・原点）・プレイヤー・メッセージ・窓の積み（`menu`・`ui-push`・`txt`）・入力待ちの種類（`input_mode`） |
| `dcss_text.h` | 原作の色つきの字（`<red>…</red>`）と文字画面の HTML（`<span class="fgN bgN">`）を、字と色の区間に直す。DCSS の色 16 色 → `TERM_*` |
| `dcss_terrain_table.inc` | DCSS の地形の値 → HD2D の地形番号・旗・ミニマップの種別。`tools/dcss/gen_terrain_table.py` が `dcss/terrain/feature_map.csv`（別の担当の物。読むだけ）から作る |
| `dcss_posix_shim.cpp` | §3 の仮のソケットに、差し込み口を足した（`dcss_shim_set_sink`：原作の送信を受ける／`set_idle_callbacks`：入力待ちに入るとき・入力の有無を見に来たとき／`disable_stdin_reader`／`signal_eof`） |

共用の TU は他のコアと同じファイルを**そのまま**組む: `presentation/frame/{frame_codec,protocol_messages,protocol_transport}.cpp`・`presentation/bridge/input_event_adapter.cpp`・`platform/portable/legacy_os.cpp`
（`DcssCore.vcxproj` の「プロトコル v1 の層」の節。この 7 本だけ C++20・プリコンパイル済みヘッダなし。原作の C++ は C++14 のまま）。

### 9.3 流れ

- **フレームを出す時機**: 原作が入力待ちに入るとき（`tileweb.cc` の `await_input` が `pselect` で待つ所）に、その時点の状態から `GameFrame` を組み、前と同じ内容なら送らない（v1 §6.4）。
  走り・自動探索の最中に原作が入力の有無を見に来るとき（`try_await_input`）は 80 ミリ秒に 1 回まで。
- **画面側だけで変わる状態**（窓のカーソルの移動など）は、受信スレッドが原作へ知らないメッセージ `{"msg":"hd2d_refresh"}` を 1 本入れて、ゲームのスレッドに組み直させる（原作は知らないメッセージを黙って飛ばして入力待ちへ戻る）。
- **言語**: `--lang=` があればそれが勝ち、無ければ最初の `ui_state.lang`（3 秒まで待つ。来なければ日本語）。`init.txt` に `language = ja|en` を書いて起こす。
  `lang-restart` を名乗るので、言語を替えると画面がコアを保存して終わらせ、`--resume` つきで起こし直す。`--resume` は前回の人物の名前（`dcss/lib/hd2d_last_name.txt`）を `-name` で渡して開く。
- **保存先**: `<データの根>/dcss/lib`（`HENGBAND_DATA_ROOT`、無ければ実行ファイルの隣）を原作の `CRAWL_DIR` にする。読むデータ（`dat`・`docs`・`settings`）と、書く場所（`saves`・`morgue`、初回に説明文の SQLite を `saves/db`・`saves/des` へ）が同居する。
  データの根が実行ファイルの隣でなければ、隣の `dcss/lib` から写す。**非 ASCII のパスは未確認**（原作の `getenv`・`fopen` に渡る道）。
- **終了**: 画面が居なくなった（標準入力の EOF）・`quit_request` は原作の SIGHUP と同じ扱い（保存して終わる）。ゲームが終わると `exit`（code 0）を送る。画面は終わる（コアが終わったら画面も終わる規約）。
- **窓**: M0 は文字の画面（`menu_term_lines`）。メニュー（`menu`）・窓（`ui-push` の各種）・文字画面（`txt`）を字に起こす。キャラクター作成の格子（`newgame-choice`）は 1 ボタン 1 選択肢（`menu_choices`）。
  他の文字のコア（Umoria など）と同じ `NativeModernMenu` が、その字から新しい窓を作る（D10・D11 により、原作のタイル版の UI の再現（画面側の専用の部品。別の担当）に置き換わる）。
  起動メニューの名前の欄は、原作が文字画面の 1 行に映すだけなので、行の頭（「名前を入力」「Enter your name」）で見分けて文字入力の窓にする。
- **仮の表示であること**（設計書 D10・D11）: このコアの画面は、HD2D の今の作り（状態の欄・サブパネル・玉・新しい窓）を使わず、原作のタイル版の UI を再現して地図だけを立体にする（画面側の専用の部品は別の担当）。
  上の「窓」の作り（文字の画面と `NativeModernMenu`）は、M0 で遊べることを確かめるための**仮の表示**で、新しい窓へ写す作業はこれ以上しない。
- **ウェブタイル版の JSON をそのまま画面へ渡す経路**（コア側だけ。画面側は別の担当）: コアは `hello_ack.features` に `dcss-webtiles` を申告する。画面が `hello.features` にも `dcss-webtiles` を挙げたときだけ、
  原作が出した JSON を 1 メッセージずつ `{"t":"webtiles","line":"<JSON の字。先頭 * はサーバ宛の制御>"}` で送る（加工しない。従来の `frame` も送り続ける）。逆向きは `{"t":"webtiles_input","line":"<原作の制御メッセージの JSON の字>"}`
  を原作の入力へ渡す（例 `{"msg":"key","keycode":13}`・`{"msg":"text_input","text":"Wt"}`）。挙げないとどちらも無効（確認: 画面なしの駆動で、挙げないと 0 通・挙げると 11 通が届き、`text_input` で名前の欄に字が入った）。
- **命令表**: `i`（持ち物）と `e`（食べる）の `command` を字から外した。画面は命令表に `i` `e` があると新しい持ち物の窓を開く入力として横取りし、返事（`NativeInventoryMenu`）を待って止まる。持ち物の窓は M3。

### 9.4 起こし方・検査

```powershell
python -u tools\dcss\build_core.py                        # 生成物 → DcssCore.exe（リポジトリ直下）→ dcss/lib（データ）
python -u tools\dcss\dc_m0_check.py                       # 画面の操作だけで通す（3 回）。絵は --shots（既定は作業用の一時フォルダの dcss_m0）
python -u tools\dcss\dc_protocol_driver.py --exe DcssCore.exe --scratch <一時フォルダ>   # 画面なし。プロトコルだけで握手と最初のフレームを見る
```

- 検査用の環境変数: `DCSS_EXTRA_ARGS`（原作への追加の引数。例 `-seed 7`）、`DCSS_TRACE_JSON`（原作が出した JSON を 1 行ずつ書き出す）。
- 起動の前もっての読み込み（2026-10-23）: 最初の 1 回だけ、原作のマップの定義（.des）の読み込みを別プロセス（`--dcss-warm-cache`＝原作の `-builddb`）で成功するまで繰り返してから本番を起こす（読み込みの検査に乱数が入り、約 0.1% で原作が `end(1)` で終わるため。Windows・Linux。Android・Quest は 1 プロセスなので入れていない）。印は `dcss/lib/hd2d_des_ready.txt`、子の標準エラーは `dcss/lib/des_warm.log`。検査用の環境変数: `DCSS_EXIT_TRACE=1`（コアが終わるときの呼び出しの並びを標準エラーへ。Windows）、`DCSS_WARM_TEST_FAILS=N`（前もっての読み込みを N 回わざと失敗させる）、`DCSS_TEST_EXIT_FAIL=1`（原作が失敗で終わった印を立てて終わる）。確かめは `tools/dcss/dc_warm_check.py`。
- 画面を通さない確かめ（`dc_protocol_driver.py`）は、窓なし（`modern_ui` 偽）のとき Enter では原作のメニュー（はい/いいえ）を選べない。選択肢のキー（`Y`）を送ること。

### 9.5 M0 の残り（できていないこと）

- Android・Quest（Android は 1 プロセス 2 スレッドの形が要る）。Linux は §10 で済んだ。
- 地図の絵: 怪物・アイテムは字の板（`ascii_fallback`）。怪物の種類 → 絵、床のアイテムの種類（JSON に無い）、雲、態度・脅威度、状態のアイコンは M1・M2。
- 持ち物・店・魔法の窓、狙い（カーソル位置だけ送る）、ビーム・爆発の演出（途中の地図の差分も出さない）、音は未着手。
- `status[]`（状態）は左の列の字で出すだけ（状態のアイコンの鍵は無い）。地名（`Dungeon`）は日本語に直していない。
- 死亡後の画面は出るが、`game_over` の窓の見た目・墓標の確かめは未実施。長時間の連続プレイ（soak）は 90 秒のランダム入力を数回流しただけ。
- 原作の `-more-` は `prompt` の末尾に `-more-` を付けて出す（HD2D の既存の判定に乗る）。実際に出る場面の確かめは未実施。

---

## 10. Linux（2026-10-21）

### 10.1 結論（出どころ: 確認＝実際に組んで駆動した）

- **確認**: `-DHENGBAND_BUILD_DCSS=ON` で `DcssCore`（Ubuntu 26.04・WSL・g++ 15.2・x86_64）が最後までリンクでき、プロトコル v1 の駆動器 `tools/dcss/dc_protocol_driver.py --flow` で
  握手 → 人物作成（人間・戦士・レイピア、`-seed 7`）→ 数歩 → 所持品 → 保存して終了（終了コード 0・`exit` を送る・セーブができる・標準エラーが空）→ 起こし直して続きから → もう一度保存して終了、の 13 項目が通った。
  同じ駆動器の同じ項目が Windows の `DcssCore.exe` でも通る。従来の仮の入出力（§3.1。`--ui-protocol=stdio` なし）も Linux で起動 → 名前 → 種族の選択まで出て、標準入力を閉じると終了コード 0 で終わった。
- 原作の書き換えは **0 行**のまま。画面（`HengbandHd2d`）を Linux で起こした確かめは**未実施**（コアだけを駆動した）。配布物（`build_linux_package.py`）への組み込みも未着手（既定は OFF のまま）。

### 10.2 Windows と分けた所

| 所 | Windows | Linux |
| --- | --- | --- |
| `tileweb.cc` のソケット（`socket`・`bind`・`setsockopt`・`sendto`・`recvfrom`・`select`・`pselect`） | 互換ヘッダ `shim/sys/*.h` が宣言し、`dcss_posix_shim.cpp` が同じ名前で実体を定義する | 互換ヘッダは**使わない**（本物の POSIX のヘッダ）。同じ実体を `__wrap_<名前>` で定義し、リンクの `-Wl,--wrap=<名前>`（`dcss_core.cmake`）で呼び出しをそこへ向ける。仮のソケットの番号（`/dev/null` を開いた番号）でない呼び出し（sqlite・lua など）は `__real_<名前>` で本物へ素通しする |
| `gettimeofday`・`sigprocmask`・`usleep` | 互換の実装 | 本物（そのまま） |
| 標準入力の読み取り・仮のソケットの番号・待ち | `_read`・`_open("NUL")`・`Sleep` | `read`（EINTR は再試行）・`open("/dev/null")`・`nanosleep` |
| プロトコル用の標準出力（`dcss_main.cpp`） | `DuplicateHandle` で複製し、`_dup2`・`SetStdHandle` で CRT の標準出力を標準エラーへ | `dup(1)` を `transport_from_fd` で包み、`dup2(2, 1)`。`SIGPIPE` を無視する（相手が閉じたパイプへの書き込みでプロセスごと落ちないため） |
| 環境変数・パス | `_putenv_s`・`\` | `setenv`・`portable::kPathSep`（`hd2d_last_name.txt` の道は両方とも `join()`） |
| `F1`〜`F12` のキー番号（`dcss_main.cpp`） | `-368`（`CK_F15 = -382` から） | `-265`（`CK_F15 = -279`、原作の `cio.h` が `TARGET_OS_WINDOWS` で分けている） |

共用の TU に `presentation/bridge/input_event_adapter.cpp` を足した（`NativeModernMenu` が使う `append_input_event_keys`。Windows の `DcssCore.vcxproj` には元からあった）。

### 10.3 組み方・確かめ方（WSL の Ubuntu。ビルド先は WSL の中）

```sh
# 先に Windows 側で python tools/core_sources/prepare.py を済ませておく（build-sources/dcss ができる）
cmake -S <リポジトリ>/platform/linux -B ~/hb-build/dcss-linux -G Ninja -DCMAKE_BUILD_TYPE=Release   -DHB_CORE_SOURCE_ROOT=<リポジトリの隣の roguelike-cores>/build-sources
cmake --build ~/hb-build/dcss-linux --target DcssCore -j 20          # 出力は ~/hb-build/dcss-linux/bin/cores/DcssCore
# 画面なしの駆動（置き場 ~/dcss-test に dcss/lib/{dat,docs,settings} への結びつけ（シンボリックリンク）を作る。開発ツリーのセーブには触らない）
python3 -u tools/dcss/dc_protocol_driver.py --exe ~/hb-build/dcss-linux/bin/cores/DcssCore --scratch ~/dcss-test --flow
```

- 初回の configure は約 160 秒、2 回目は約 130 秒（推測: 原作ソースを `/mnt/c` から読み、`tools/targeting` の上書きの複製を作るため。内訳は測っていない）。全部組む時間は測っていない（この PC・24 論理コアで 10 分前後）。
- `dcss/lib/{dat,docs,settings}` は Windows で `prepare_runtime_data.py` が作ったものを `/mnt/c` 経由で読んだ。Linux だけで準備する道（`prepare_gen.py` の Linux 版）は未整備。
- 初回の起動は説明文のデータベース（`saves/db`・`des`）を作るので遅い（従来の仮の入出力で確かめたとき、最初の入力を送るまで 6 秒待つと足りず、14 秒待つと進んだ）。駆動器の `--flow` の待ち（最初のフレームまで 90 秒）は足りている。
- 駆動器 `dc_protocol_driver.py` は Windows・Linux の両方で動くようにした（実行ファイル名は `DcssCore.exe` / `DcssCore`、データへの結びつけはジャンクション / シンボリックリンク）。`--flow` が上の合否を返す。

### 10.4 残り

- 画面（`HengbandHd2d`）を Linux で起こしてコア選択から遊ぶ確かめと、配布物への同梱（`build_linux_package.py --dcss`）は **2026-10-22 に済み**。`HENGBAND_BUILD_DCSS` を既定 ON にするかは決めたこと待ち。
- 非 ASCII のパスは未確認（§9.3 のとおり）。
- Android・Quest（1 プロセス 2 スレッド）は別。

---

## 11. 段 M1: 地図の地形をボクセルのプレハブで出す（2026-10-21）

**出どころ**: 「確認」は実際に組んで画面（`HengbandHd2d.exe`）から遊んで確かめたこと。

- **何をするか**: 原作（ウェブタイル版）が送る地形の値 `f` と、そのマスの背景のタイル `t.bg`・雲のタイル `t.cloud`（どちらも下位 16 ビットがタイル番号）から、HD2D の地形番号を選んで `cell.terrain_id` に載せる。
  規則は `assets/voxel/terrain_prefabs.jsonc` の `terrains_extra` の DCSS の節。画面のコードは変えていない（データだけ）。
- **番号の帯**: 2000〜2220 地形の値（`2000 + 値`）／2300〜2313 派生（店の種別・ゾットの門と像ほか。原作が選ぶタイルで絵が変わる物）／**2400〜2499 床のタイルの組**／**2500〜2649 壁のタイルの組**／**2650〜2699 雲のある床**。
- **ブランチごとの床・壁**: 原作は床・壁のマスに、そのブランチ・ヴォールトの指定に合わせて選んだ**実際のタイル**を `t.bg` で送る（確認: 迷宮の床 `FLOOR_GREY_DIRT_n`、オークの坑道の床 `FLOOR_ORC_n`、壁 `WALL_ORC`）。
  タイルの組 -> プレハブ（床 `dc_floor_*` 62 種・壁 `dc_wall_*` 55 種）を表にして、DNGN_FLOOR と壁のマスの番号に使う。表に無い組は地形の値の既定（`dc_floor_grey_dirt`・`dc_wall_rock` など）に落ちる。
  共通の部分に足す物は無い（番号を分けるだけ）。
- **扉・階段・祭壇・噴水などの物**: 地形の値ごとの規則（地面 = 灰色の土の床 `dc_floor_grey_dirt`〔樹木は草〕＋物。扉は東西向きの `_ew` つき）。**物の下の床**: 段 4 で、原作が `t.flv.f` に送る床のタイルを読み、物 × 床の組の番号（3000〜）にした（下の「段 4」）。
- **雲**: DNGN_FLOOR のマスに雲のタイルがあれば、雲の区分（火・冷気・毒・煙・闇・魔力）ごとの色つきの灰色の土の床にする（`tint`・`emissive`）。ほかの地形の上の雲は無視する。
- **ファイル**: `tools/dcss/gen_dcss_terrain_rules.py`（規則・表・プレハブの写しを作る。`--check` で照合）／`tools/dcss/dcss_tilefam.py`（`tilegen` の出力 `tiledef-*.h/.cc` からタイルの組を読む共通部分）／
  `dcss/adapter/dcss_tile_variants.inc`（生成物。範囲 -> 番号）／`dcss/adapter/dcss_terrain_table.inc`（地形の値 -> 番号。M0 の表を今の `feature_map.csv` から作り直した。以前の物は仕分け a・b の頃の変愚の番号を指していた）。
  壁の組のプレハブ 44 個は `tools/dcss/gen_dcss_terrain.py` に `wall_families()` を足して作った（`dcss/assets/voxel/`。ゾットの青以外の色の壁は青の絵の色の並びを入れ替えた）。
- **プレハブの置き場**: 画面は `assets/voxel/` の 1 か所だけを読む（TOband の `tb_*` も同じ）。規則が指す `dc_*` 604 ファイルを `dcss/assets/voxel/` から `assets/voxel/` へ写す（中身が同じなので git は 1 つの塊として持つ）。
- **生成物とタイル番号**: `dcss_tile_variants.inc` は組んだ `DcssCore.exe` と同じ `tilegen` の出力（`prepare_gen.py` の作業フォルダの `gen/rltiles`）から作る。原作を替えたら `gen_dcss_terrain_rules.py` と `dcss_frame.cpp` を作り直す。
- **段 4（物の下の床・半透明・祭壇・足元の輪。2026-10-21）**:。アダプタの変更は 3 つ。
  (1) `t.flv.f`（背景が透ける物のマスにだけ、変わったときに届く床のタイル）を `MapCell::flv_floor` に覚え、`with_floor_under` が物の地形番号を物 × 床の組の番号（`kDcssUnderBase`＋物の位置 × `kDcssUnderStride`＋床の番号）へ替える。
  (2) `t.trans`（半透明の怪物。doll があって無ければ偽へ戻す）と、水に浸かる条件（`bg` の `TILE_FLAG_WATER` で `fg` に `TILE_FLAG_FLYING` が無い）から `fade_of` が半透明の種類を決め、`dcss_slab.h` の `bake` が 4x4 のディザでボクセルを間引く（`Bank::resolve` の `fade`。確かめ用に `--dcss-slab-layers` が環境変数 `DCSS_SLAB_FADE` を読む）。
  (3) 光る祭壇に `CELL_FEAT_GLOW` を付けない（`dcss/terrain/build_feature_map.py`）。光り方は規則の `emissive`（`GLOW_EMISSIVE`）。
- **検査**: `python -u tools/dcss/dc_m1_check.py`（画面の操作だけで迷宮 3 階と 7 つの枝へ入り、画面が受けた地形番号を原作の JSON の記録から別に計算した値と突き合わせる。絵は `--shots`）。

---

## 12. 段 M2: 怪物・アイテム・プレイヤーを、原作のタイルのボクセルの板で出す（2026-10-21）

**出どころ**: 「確認」は実際に組んで画面（`HengbandHd2d.exe`）から遊び、撮った絵を目で見て確かめたこと。

- **何をするか**: 地図の怪物・床のアイテム・プレイヤーを、字（`@`・`k`・`$`）ではなく、原作のタイルをボクセルの板（32 ドットを 2 倍の 64。1 画素 = 2x2 ボクセル・厚み 4）にして出す。
  プレイヤーの見た目には装備が映る（D5。原作の人形 `t.doll` の部品の並びを重ねる）。武器を持った怪物・ドラコニアン（`t.mcache`）も同じ道。
- **板の作り方の判断（全部前もって作るか、出たものだけその場で作るか）**: **出たものだけ、コア（`DcssCore.exe`）がその場で作る。**
  - 単独のタイル（画像ページの 5,419 枚）を全部前もって作ると、`--dcss-slab-all` の実測で **板 5,310 枚（同じ絵を除く）・ファイル 10,620 本・128 MB・12 秒**。
    しかも**プレイヤーの装備の組み合わせは数十万通り**で、前もって作れない。実行時に作る道が D5 のために要るので、単独のタイルも同じ道に通して**作り方を 1 つ**にした（作り方が 2 つあると見た目がずれる）。
  - 配るのは原作のタイル版が使う画像ページだけ（`dcss/assets/atlas/`: `main.png` 1.2 MB・`player.png` 2.1 MB・`tiles.tsv` 約 0.2 MB。合計 約 3.5 MB）。
  - 作った板は `<データの根>/dcss/lib/slabs/` に**内容のハッシュ**を名前（`dcss_<16 桁>.vox` / `.jsonc`）にして残す。起動の間はキャッシュとして使い回し、**次の起動のはじめに消す**（`Bank::init` の `clear_slab_dir`。このコアが作った名前のファイルだけを消す）。
- **層**:

| 物 | 役 |
| --- | --- |
| `tools/dcss/dcss_tile_atlas.py` | 原作の `tilegen` を**画像つき（`USE_TILE`・libpng）で作業フォルダに別に組み**、画像ページ（`main.png`・`player.png`）とタイルごとの切り出しの表（`tiles.tsv`。原作の `tile_info`）を作る。原作の写しには書かない（複製して使う）。番号が組み立てに使った表と同じことを、`tiledef-*.h` のバイト一致で確かめる。新しい libpng で原作の `write_png` が異常終了する（`png_write_end` の二重呼び出し）ため画像の書き出しだけ素の RGBA に替え、`iCCP` を持つ絵が異常終了するので入力の PNG の付属情報を外す（画素は変えない） |
| `dcss/adapter/dcss_slab.h`（新規。`dcss_frame.cpp` だけが取り込む） | 画像ページの読み込み（8 ビット RGBA の PNG。付属の zlib の `uncompress`）・層の重ね（原作のブラウザ側 `cell_renderer.js` の `draw_tile` と同じ位置・同じ切り方）・板の作成（`tools/voxel/tile_to_slab.py` と同じ規則。色は 255 まで・メディアンカット）・ファイルの書き出し（一時ファイルに書いてから名前を替える）・番号の割り当て（`Bank`） |
| `dcss/adapter/dcss_frame.cpp` | `t.fg`（旗が 33 ビット目より上にあると `[下位, 上位]` の対で届く。怪物はほぼ常に対）・`t.base`・`t.doll`・`t.mcache`・`t.overlay1/2` をマスごとに覚え、見えている怪物・プレイヤー・床のアイテムのマスに `tile_index` を載せる。アイテムは `object_id` に同じ番号 |
| `dcss/adapter/dcss_main.cpp` | `hello_ack.asset_roots.slab` で置き場を申告。新しい板ができたら、目録（`asset_manifest`）の**全部**を送り直してからフレームを送る（下の「目録」） |

- **層の並び**（原作の `draw_foreground`・`draw_dolls` と同じ）: アイテム = 下地（`base`。薬の瓶など）→ `fg` →（呪文書の紙は流派の印 2 枚）。
  怪物・プレイヤー（`fg` が画像ページ `player` の範囲）= `doll` の部品を順に（`mcache` に同じ番号があればそのずれで）、`fg` が mcache の番号以上なら `mcache` の部品も順に。位置は
  `x = ofs_x + ox + (16 - w/2)`・`y = ofs_y + oy + (32 - h)`、`y_max` から下は切る。**背の高いタイル（32x48）は上へはみ出す**ので、板は 64 幅・96 高になる（1.5 マスの背丈。マスの横へのはみ出しは切る）。
- **目録（`asset_manifest`）**: 画面は `TileCatalog::apply()` が**全置換**で受ける（ほかの共通の部分の変更は要らない）。板のファイルを書き終えてから、目録（これまでの全部。`root` は置き場）を送り、**約 40 ミリ秒待ってから**その番号を使うフレームを送る。
  画面は番号を初めて描くときに板を読み、**読めなければ -1 を覚えて二度と読み直さない**ので、順を守る（目録 → 待ち → フレーム）。待つのは、画面の周回が目録を取り込む前にフレームが来ると、そのフレームだけ字の板になるため。
  同じ層の並びは同じ番号（`Bank::resolve` の鍵）。新しい組み合わせには必ず新しい番号を振る。**プロトコルの仕様書（コアプロトコル v1。別のリポジトリ）の §8.2 は「起動後に 1 回・再送なし」と書いており、この再送はその契約の外だった**（実装は再送を受けられる）。2026-10-22 に同じ仕様書の §3.1・§8.2 の追補・§8.5・§8.6 に、目録の送り直しと DCSS が足したメッセージを書いた。
- **作れなかったとき**: 画像ページが読めない・不透明な画素が無い・書き込めない場合は `tile_index = 0` のまま（画面は `ascii_fallback` の字の板で出す）。標準エラーに 1 回だけ理由を出す。
- **検査**: `python -u tools/dcss/dc_m2_check.py`（画面の操作だけ。下の「確かめた結果」）。板の作り方の突き合わせは `DcssCore.exe --dcss-slab-bake <PNG> <名前> <出力先>`（`tools/voxel/tile_to_slab.py` が作った試作の板 10 枚と `.vox` のバイト列が**一致**。残りの 3 枚は重ねの絵）。
  `--dcss-slab-layers <画像ページの置き場> <出力先> <タイル番号[,x,y,y_max]>...` は層の並びから 1 枚作り、`--dcss-slab-all` は単独のタイルを全部作って数と大きさを数える。
- **共通の部分の変更**: なし（`hd2d/`・`presentation/`・`platform/` は 1 行も触っていない）。触ったのは検査の道具だけ: `tools/gate/sandbox.py`（DCSS の `lib` に実行時の板の置き場 `slabs` を空の実ディレクトリで作り、開発ツリーの `dcss/lib/slabs` を見張りに足した）。
- **配置の注意**: `dcss/lib/tiles`（画像ページ）は `tools/dcss/prepare_runtime_data.py` が `dcss/assets/atlas` から写す（`build_core.py` が呼ぶ）。コアは `dcss/lib/tiles` が無くても遊べる（怪物・アイテム・プレイヤーは字で出る）。
  Linux の組み立て（`tools/core_sources/dcss_core.cmake`）は `dcss_frame.cpp` に取り込まれるヘッダだけを足したので変更不要だが、**未確認**。

### 12.1 確かめた結果（2026-10-21）

- **画面の操作だけの通し** `python -u tools/dcss/dc_m2_check.py`: **17/17 項目が通った**（新しいサンドボックス。ウィザードモード `-wizard -seed 5`）。
  プレイヤー 1 体・怪物 6 体（ジャッカル・オーク戦士・赤のドラコニアン・オーガ・アスモデウス〔背の高い〕・岩の巨人）・床のアイテム 6 個が、画面の `entity_list` で**すべて板の名前（`dcss_…`）を持つ**（字の板への落ちが 0）。
  怪物は 6 体で 6 種の板、アイテムは 6 個で 6 種の板。防具（スケイルメイル）を脱ぐとプレイヤーの板が別の名前に替わり、着直すと**元と同じ名前**に戻る（同じ並びは同じ板を使い回す）。武器を外すとまた別の板になる。
  作った板は 17 枚（`.vox` と `.jsonc` が対・書きかけの `.tmp` なし・合計 575,760 バイト）。画面の標準エラーに板の読み込みの失敗・字への落ちが無い。開発ツリーのセーブ・設定は前後で不変。
- **目で見た絵**（`dc_m2_check.py --shots`。作業用の一時フォルダの `scratchpad/dcss_m2/check/`）: プレイヤーは種族・鎧・盾・武器の人形、鎧を脱ぐと胴が素肌になり、素手にすると剣が消える。怪物は原作のタイルの形・色で立つ（アスモデウスは 1.5 マスの背丈）。
  アイテムは薬・巻物・指輪・杖・剣・本ほか。原作のタイルの黒い縁取りがそのまま板の輪郭になる。
- **板の作り方の突き合わせ**: `DcssCore.exe --dcss-slab-bake` で、M0 の試作の板 13 枚の元の絵（重ねの 3 枚は Python で重ねた絵）から作った `.vox` が、`tools/voxel/tile_to_slab.py` が作った物と**バイト単位で一致**（13/13）。
  `.jsonc` は注釈の行を除いて同じ。255 色を超える絵のメディアンカットは `tile_to_slab.py`（Pillow の中央値分割）と同じではない（単独のタイル 5,419 枚中 128 枚が該当。最大 732 色。見た目の確認は未）。
- **材料が無いとき**: `dcss/lib/tiles` を外して起こすと、標準エラーに 1 行（板の材料を読めません）を出し、目録は送らず、怪物・アイテム・プレイヤーは字（`ascii_fallback`）で出る。ゲームは遊べる。
- **キャッシュ**: 同じ層の並びを 2 回作ると、2 回目はファイルに触れない（更新時刻が変わらない）。
- **ほかの段の検査**（M2 のコアで）: `dc_m1_check.py --only d1,d2,orc1` が 24/24。`dc_m0_check.py --runs 1` は 26/26（最初に流した回だけ「十字キーでプレイヤーのマスが動く」が通らなかった: 開始位置のまま。この検査は種を固定しないので階が毎回違う。原因は調べていない。同じコアで流し直したら通った）。
- **画像ページの再現**: `tools/dcss/dcss_tile_atlas.py --check` が、追跡している `main.png`・`player.png`・`tiles.tsv` と作り直した物の一致を確かめる（一致）。

### 12.2 残り・未確認

- 原作のタイルの縁取り（`%rim`）は板にも付く。試作の板 13 枚（縁取りの無い元の絵から作った）とは見た目が違う。外すか残すかは見た目の判断（残すと原作のタイル版と同じ）。
- 水に浸かった怪物の薄さ（`t.trans`・水中の下半分）は、段 4 で対応した（§11 の「段 4」）。雲（D14）・状態のアイコン（D15）・足元の色の輪（D16）・タブの中身（U2）は予定どおり未着手。
- 怪物 6 種・アイテム 6 種までしか目で見ていない（原作は怪物約 1,500・アイテム約 1,100）。長時間の連続プレイでの板の増え方・Android・Quest・Linux は未確認。
- 板は降ろされない（画面の GPU に溜まる）。組み合わせが 0xFF00 枚を超えたら以降は字で出る。
- プロトコルの正本 §8.2 の追補（目録の再送）は別のリポジトリの文書なので書いていない。

## 13. 段 4: 攻撃の演出の材料（2026-10-21）

詳しい作りは の「段 4」。アダプタの側でしたこと:

- `marks` メッセージに `"f":[[gx, gy, [タイル番号…], プレイヤーの行動の最中か(0|1)], …]`（ビームの弾・矢・投げた物・爆発の絵。原作が `t.ov` に載せる画像ページ main のタイル）・`"fl":[[gx, gy, 色, 濃さ], …]`（閃光 `flc`・`fla`）・`"p":[gx, gy]`（プレイヤーのマス）を足した（`dcss_frame.cpp` の `marks_json`）。
- `libhd2d.cpp` の `dcss_adapter_world_reacting()`: 原作の `you.turn_is_over` を読む（撃ち手の手がかり。書かない）。
- 原作の rc（`init.txt`）に `reduce_animations = false`（`dcss_main.cpp`）。原作のウェブ版は途中の絵を省くので、手元のタイル版と同じに、ビームの弾を 1 マスずつ・爆発を 1 輪ずつ待つ。ルールには効かない。戻すにはこの 1 行を消す。
- 環境変数 `DCSS_DELAY_SCALE`（1〜100。既定 1）: 原作の待ちを何倍かに延ばす。検査用（飛んでいる途中の絵を撮る）。
- 原作のソース・レシピは書き換えていない（0 行）。

## 14. Android・Quest（2026-10-22）

記録の本体は。アダプタの側でしたこと:

- `dcss_main.cpp`: `run_protocol` を「標準入出力の準備」と本体 `run_protocol_common` に分け、`hengband_core_entry`（画面と同じプロセスの別スレッドの入口。`--lang=`・`--resume` を受ける）を足した。
  呼ばれるスレッドのスタックは小さい（bionic の既定 1 MB）ので、16 MB の専用スレッド（`pthread_create`）で走らせて終わりを待つ。原作は終わるとき `exit()` を呼ぶ（アプリごと終わる。保存は済んでいる）。
- `dcss_posix_shim.cpp`（`HENGBAND_ANDROID` のときだけ）: `__wrap_mkdir`・`__wrap_stat`・`__wrap_opendir`。Android のアプリの SELinux が、実在する上の階層（`/data` ほか）に `EACCES` を返し、原作の `_create_dirs`（`files.cc`）が保存先の下の階層を作れなかった。保存先（`CRAWL_DIR`）の祖先に当たる道だけ「在る」と返す。
- `android_compat/execinfo.h`: bionic に `backtrace` が入ったのは API 33 から（Quest は minSdk 32）。`crash.cc` にだけ、何も返さない代わりのヘッダを当てる。
- 検査用: 外部ストレージのアプリの場所（`/storage/emulated/0/Android/data/org.hengband.hd2d/files/`）に `dcss_trace.flag` があれば、原作の JSON を同じ場所の `dcss_trace.jsonl` へ残す（環境変数を渡せない Android で `DCSS_TRACE_JSON` の代わり。`adb shell touch` で作り、`adb pull` で読む）。
- 検査用: 同じ場所に `dcss_extra_args.txt` があれば、その 1 行目を環境変数 `DCSS_EXTRA_ARGS` の代わりに使う（例 `-wizard -seed 5 -extra-opt-first pickup_menu_limit=0`。`adb push` で置く。2026-10-23。マウス・タップの確かめ用）。
- 組み方: `tools/dcss/prepare_android_gen.py`（生成物をホストで先に作る）→ `tools/core_sources/dcss_android_core.cmake`（`libdcsscore.so`。原作の C++ は `-U__ANDROID__`）。手順は。
- 原作のソース・レシピは書き換えていない（0 行）。
