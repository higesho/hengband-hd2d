/*!
 * @file hd2d_app.h
 * @brief `HengbandHd2d.exe` の本体（窓・GL 4.6・駆動ループ）。
 *
 * **いまは P0**（コアと繋がって窓が出る。まだ 3D は描かない）。
 */
#pragma once

#include "ui/ui_layout.h"

#include <string>
#include <vector>

namespace hd2d {

/*!
 * @brief `run()` の戻り値のうち、**「コア選択からやり直す」**を表すもの（2026-08-23）。
 *
 * @details 「ゲーム終了後、コアの起動画面に遷移させて」と決めた。
 * コアが**自分から**終わった（利用者が遊びを終えた）ときだけこれを返す。
 * 窓の × で終わらせたとき（`quit_request` を送ったとき）や、コアが落ちたときは返さない
 * ——どちらも「やり直したい」ではない。
 *
 * 呼び手（`platform/windows/hd2d_main.cpp` と `platform/android/hd2d_entry_android.cpp`）は
 * これが返る限り `run()` を呼び直す。**やり直しは `run()` ごと**なので、
 * コアごとの覚え（タイル目録・プレハブライブラリ・`pad_commands`・サブパネルの種類…）は
 * 全部作り直しになる。中で環を回すより安い——**写し忘れが起きようがない**。
 *
 * @note `--core-path=` で直に起こしたときは返らない（選ぶ画を飛ばしている以上、
 * 戻る先が無い）。
 */
constexpr int kRunRestart = -2;

/*!
 * @brief `run()` の戻り値のうち、**「同じコアを起こし直す」**を表すもの（2026-08-23）。
 *
 * @details 遊んでいる途中で**言語を替えた**とき。
 * コアによっては言語が起動列の中で決まるので、替えるには起こし直すしかない。
 * **どのコアがそうなのかは画面側では決めない**——コアが `hello_ack.features` に
 * `"lang-restart"` を名乗る（必守制約 1）。名乗らないコアではこの値は返らない。
 *
 * `kRunRestart` との違いは**行き先**である。あちらはコア選択の画へ戻すが、
 * こちらは**同じコアをそのまま起こし直し、直前に保存した人物を開く**
 * （コアへ `--resume` を渡す）。呼び手は `take_core_relaunch()` で申し送りを取り、
 * それを `AppOptions::relaunch` に入れて `run()` を呼び直す。
 */
constexpr int kRunRelaunchCore = -3;

/*!
 * @brief コアを起こし直すときの申し送り（`kRunRelaunchCore`）。
 * @details `core_path` が空なら「起こし直しではない」＝ふつうの起動。
 */
struct CoreRelaunch {
    //! 起こし直すコアの exe（前の回で選ばれていたもの）。
    std::string core_path;
    //! コアへ `--resume` を渡す（直前に保存した人物を開かせる）。
    bool resume{ false };
    /*!
     * @brief 前の回で**コア選択の画を出していたか**。
     * @details そのまま次の回へ持ち越す。持ち越さないと、起こし直したあとに
     * 遊びを終えても `kRunRestart` が返らず、コア選択へ戻れなくなる。
     */
    bool core_select_shown{ false };
};

/*!
 * @brief `run()` が置いた起こし直しの申し送りを取り出す（**取ると空になる**）。
 * @details `run()` が `kRunRelaunchCore` を返したときだけ中身がある。
 */
CoreRelaunch take_core_relaunch();

//! 起動時に決まるもの。
struct AppOptions {
    int window_w{ 1600 };
    int window_h{ 900 };
    //! `--protocol-log=` / `--core-protocol-log=`（v1 §11.2）。
    std::string protocol_log_path;
    std::string core_protocol_log_path;
    //! 自分が解釈しなかった起動引数。**全部そのままコアへ渡す。**
    std::vector<std::string> forwarded_args;
    /*!
     * @brief 起こすコアの exe（`--core-path=`）。空なら既定の選び方。
     * @details : **指定されたら選択画面を飛ばす。**
     * スクリプトから特定のコアを叩きたいとき（検査・ボット）に要る。
     */
    std::string core_path;
    /*!
     * @brief コア選択画面だけを出して確かめる（`--core-select-check`）。**コアを起こさない。**
     * @details 一覧を stderr へ出し、`--shot=` が有ればその画を書いて 0 で終わる。
     * 登録が 1 件でも画面を出す（普段は出さない条件なので、そこを確かめる手段が要る）。
     * 他の `--*-check` と同じ立ち位置。
     */
    bool core_select_check{ false };
#if !defined(_WIN32)
    //! コアスレッドの入口（`CoreLinkOptions::core_thread_main` へそのまま渡す。
    //! Android の入口 `hd2d_entry_android.cpp` が差す）。
    int (*core_thread_main)(void *in, void *out, const std::string &protocol_log_path,
        const std::vector<std::string> &forwarded_args){ nullptr };
    using CoreThreadEntry = int (*)(void *, void *, const std::string &, const std::vector<std::string> &);
    /*!
     * @brief コア名（.so の名）→ コアスレッド入口の解決（多コア形）。
     * @details コア選択の「在るものだけ」判定と、選ばれたコアの接続の両方で使う。
     * dlopen は platform 層の仕事なので関数ポインタで差す（Windows は exe 起動なので不使用）。
     * nullptr なら core_thread_main の直結だけで動く（後方互換）。
     */
    CoreThreadEntry (*core_thread_resolve)(const std::string &path){ nullptr };
    /*!
     * @brief その .so が**在るか**だけを答える（**開かない**）。
     * @details 2026-08-23 に決めた「コアの選択画面時点ではコアの起動はしないように」。
     * コア選択の「在るものだけ」判定はこちらを使う——`core_thread_resolve` は
     * `dlopen(RTLD_NOW)` なので、選ぶ前に呼ぶと**コアを 4 本とも読み込んで**しまう
     * （静的初期化まで走る＝実質「全部起こす」）。nullptr なら従来どおり
     * `core_thread_resolve` に落ちる（差していない入口でも一覧は出る）。
     */
    bool (*core_thread_available)(const std::string &path){ nullptr };
#endif
    /*!
     * @brief 窓を出さずに GL の素性だけ調べて終わる（`--gl-probe`）。
     * @details 「4.6 core が取れる機械か」を CI や別スレッドから確かめるための手段。
     * ここが通らない環境ではこの exe は成立しない（設計書 §3）。
     */
    bool gl_probe_only{ false };
    /*!
     * @brief プレハブを 1 個だけ見る（`--prefab=<名前>`）。**コアを起こさない。**
     * @details P1 の完了条件「試作の店が画面に出る」を確かめる手段。素材を作る側の
     * 見直しにもそのまま使える（P10 の「AI が作る → 人が見て指摘 → AI が直す」）。
     */
    std::string prefab_name;
    //! 窓を出さずにメッシュ化だけして三角形の数を出す（`--prefab-check=<名前>`）。
    bool prefab_check_only{ false };
    /*!
     * @brief 実体の板の粒（`voxels_per_cell`）を 8〜512 の 7 段すべてで通す（`--slab-ladder-check`）。
     * @details 2026-08-21 に決めた「人物・アイテム・モンスターは 8/16/32/64/128/256/512 を
     * 受け入れられるように。コアは問わずエンジンとして」。窓も GL もコアも要らない。
     * **エディタ入りのビルドだけ**（書く側が `hd2d/edit/prefab_writer.cpp` にあるため）。
     */
    bool slab_ladder_check{ false };
    //! プレハブの置き場（`--voxel-dir=`）。既定は cwd の `assets/voxel`。
    std::string voxel_dir{ "assets/voxel" };
    /*!
     * @brief 数フレーム描いてから自分でフレームを書き出して終わる（`--shot=<path.bmp>`）。
     * @details 外部キャプチャが黒く返る罠（記憶 `hengband-hd2d-screenshot-is-stale`）を
     * 避けるための手段。`glReadPixels` で自分のフレームバッファから直接読む。
     */
    std::string shot_path;
    /*!
     * @brief `--shot` を撮るまでに待つフレーム数（`--shot-after=`）。
     * @details **地形が出てから**数える。起動からの通し番号だと、セーブを読み込む操作に
     * かかる時間しだいでタイトル画面が写る。スクリプトでダンジョンまで潜らせるときは大きくする。
     * @note **負の値を渡すと駆動ループの通し番号で数える**（絶対値）。
     * タイトル画には地形が 1 フレームも無いので、そこを撮るにはこちらが要る。
     */
    int shot_after_frames{ 60 };
    /*!
     * @brief **一人称（FPS モード）で始める**（`--fps`）。おまけ機能（`ui/fps_mode.h`）。
     * @details 実行中は割り当てた F キー（既定 F7）で切り替えられるが、そのキーは
     * この exe が食うので**スクリプト（`HENGBAND_SDL2_INJECT_FILE`）では押せない**。
     * 一人称の絵を `--shot=` で撮るにはここから入る道が要る。
     */
    bool first_person{ false };
    //! `--fps=<度>` の初期方位（0 = 北・時計回り）。検査で 4 方向を撮り分けるのに使う。
    float first_person_yaw_deg{ 0.f };
    /*!
     * @brief **見下ろしの視点回転**の初期値（`--turn=0..3`）。0 = 北・1 段で 90° 時計回り。
     * @details -7。実行中は F5／F6 で回せるが、
     * **そのキーはこの exe が食う**のでスクリプト（`HENGBAND_SDL2_INJECT_FILE`）からは押せない。
     * `--shot=` で 4 方位を撮り分けるにはここから入る道が要る（`--fps` と同じ理由）。
     */
    int camera_turn{ 0 };
    /*!
     * @brief **VR ジオラマの盤の回転**の初期値（`--board-turn=0..3`）。
     * @details。実行中は右スティック横のフリックと F5／F6 だが、
     * **どちらもスクリプトから出せない**（UI が食う・スティックは注入できない）。
     * `--vr-fake --board-turn=N --shot=` で 4 方位を撮り分けるための手段である。
     * フラットの `camera_turn` とは**別の値**（VR の間だけ効く）。
     */
    int vr_board_turn{ 0 };
    /*!
     * @brief **実体の表現**。
     * @details 空なら cfg（と既定）に従う。`--shot=` でアスキー実体の絵を撮る手段。
     */
    std::string entity_style;
    /*!
     * @brief **BGM に何を流すか**。
     * @details 空なら cfg の値。**素材や規則を差し替えて聴き比べるための手段**で、
     * `--look=` や `--dof=` と同じ立場（cfg を書き換えずに「今回だけ」試せる）。
     */
    std::string bgm_mode;
    /*!
     * @brief **画調**。
     * @details 空なら cfg（と既定）に従う。実行中は機能メニューで回せるが、
     * **メニューはスクリプトから開けない**ので `--shot=` で撮るにはここから入る道が要る
     * （`--entity=` と同じ理由）。
     */
    std::string scene_look;
    /*!
     * @brief TRON の空（`--tron-sky` / `--tron-sky=0`）。**−1 なら cfg に従う。**
     * @details 3 値なのは「打たれなかった」と「0 を打たれた」を分けるため
     * （bool 2 つに分けるより取り違えにくい）。
     */
    int tron_sky{ -1 };
    /*!
     * @brief **面の汚し**の強さ（`--wear=off` / `--wear=1.4`）。空なら cfg に従う。
     * @details 中身は `render/surface_wear.h`。**メニューからも回せる**が、
     * `--shot=` で撮り比べるには引数が要る（`--look=` と同じ理由）。
     */
    std::string wear;
    /*!
     * @brief **材質ごとに汚しを掛けるか**（`--wear-materials=stone,wood` / `all` / `none`）。
     * @details 空なら cfg に従う。実物を見ながら決めるための口。
     */
    std::string wear_materials;
    /*!
     * @name **画面全体の色処理**（撮り比べる口。空なら cfg／既定に従う）
     * @details 中身は `render/post_process.h`。`--vignette=0.5` `--sepia=0.7`
     * `--hdr=0.8` `--exposure=1.6`。負なら「打たれていない」。
     * @{
     */
    float vignette_strength{ -1.f };
    float sepia{ -1.f };
    float hdr{ -1.f };
    float exposure{ -1.f };
    //! 空中に浮かぶ埃の量（`--dust=0.6`）。負なら cfg／既定に従う。
    float dust{ -1.f };
    /*! @} */
    /*!
     * @brief **木の葉**の細かさ（`--leaf=off` / `--leaf=1.4`）。空なら cfg に従う。
     * @details 中身は `render/leaf_detail.h`。`--prefab=` で 1 本だけ撮り比べるのに要る。
     */
    std::string leaf;
    /*!
     * @brief `--material-check`: ライブラリぜんぶの材質の推定を数える（GL 不要）。
     * @details 色から材質を引いている以上、**同じ色を別の材に使っていれば外れる**。
     * 外れの量を数で見るための手段（`run_material_check`）。
     */
    bool material_check{ false };
    /*!
     * @brief **メインパネルを 2D のアスキー地図で始める**。
     * @details 実行中は F4 で切り替えられるが、そのキーはこの exe が食うのでスクリプトからは押せない。
     */
    bool original_panel{ false };
    /*!
     * @brief 3D の地形の上に**文字の地図も重ねる**（`--text`）。
     * @details P0 の見え方（`ascii_fallback` の格子）を残してある。3D で出ているものと
     * コアが送ってきたものが食い違っていないかを目で突き合わせるのに使う。
     */
    bool text_map{ false };
    /*!
     * @brief 合成フレームで地形を描き、往復検査と四隅の欠けを見る（`--terrain-check`）。
     * @details コアを起こさない。実プレイに頼るとセーブの中身しだいで結果が変わる。
     */
    bool terrain_check{ false };
    /*!
     * @brief 戦闘の見せ場を**画で**確かめる（`--combat-fx-check`）。`--terrain-check` を含む。
     * @details `combat_fx` の演出は 180〜260ms しか出ない（`combat_fx_view.h` の `BOLT_MS` ほか）。
     * 実プレイで `--shot=` を撮っても「N 枚目の 1 枚」しか撮れないので、**まず当たらない**
     * （R4 で 16 通りの N を振って 1 度も当たらなかった）。そこで合成の縁を積んで
     * `draw_combat_fx()` ——**本番と同じ関数**——を呼び、四隅の欠けを数えた後に重ねて撮る。
     */
    bool combat_fx_check{ false };
    /*!
     * @brief 種と配置の決定性を確かめる（`--world-check`）。窓も GL も要らない。
     * @details 設計書 §9.3。**カメラを動かしても配置が変わらない**ことと、
     * **フロアが変われば必ず変わる**ことの両方を見る（後者が無いと「常に同じ」でも通る）。
     */
    bool world_check{ false };
    /*!
     * @brief **実データの町**を読んで敷地の分解を確かめる（`--town-check`）。窓も GL も要らない。
     * @details 設計書 §10。合成フレームには町の絵が 1 つ（建物 1 棟）しか無く、
     * 罠 64（検査の絵に無い条件は検出できない）をそのまま踏むので、
     * **`lib/edit/towns/` の実物**を読んで組み立てる。既定は辺境の地（Outpost）。
     */
    bool town_check{ false };
    //! `--town-data=<path>`。省略時は `lib/edit/towns/01_Outpost_Full.txt`。
    std::string town_data_path;
    /*!
     * @brief **実データの町をその場で描く**（`--town-view`）。コアは起こさない。
     * @details 町ごとの意匠（`TownStyle`）を自分で見るために要る。辺境の地以外の町へ
     * 実機で行く道が無い（`lib/save/PLAYER` は辺境の地にいる）ので、これが無いと
     * **テルモラとモリバントの絵を 1 枚も出せない**。`--town-data=` で町を、
     * `--town-at=x,y` で見る場所を、`--shot=` で撮って終わる。
     */
    bool town_view{ false };
    //! `--town-at=<x>,<y>`。省略時は門のある敷地のうち、いちばん建物の多い所。
    int town_at_x{ -1 };
    int town_at_y{ -1 };
    /*!
     * @brief 合成フレームの `dungeon_id` を差し替える（`--dungeon=<N>`）。0 なら触らない。
     * @details P10 第 4 期。**ダンジョンごとの意匠を自分で見るために要る**
     * ——町の `--town-view` と同じ理由である。実機でイークの洞穴やオークの洞窟へ
     * 潜るには何十手もかかるし、深い所（金鉱の 75 階）へは事実上行けない。
     * `--terrain-check` の合成フロアに N 番の顔をさせて `--shot=` で撮る。
     * @note 種にも混ざる（`cell_seed`）ので、番号を変えると**配置も変わる**。
     * 材と小物を見る手段であって、同じ絵の色違いを見る手段ではない。
     */
    int synthetic_dungeon_id{ 0 };
    /*!
     * @brief 合成フレームの `dun_level` を差し替える（`--dun-level=<N>`）。**負なら触らない**。
     * @details `make_synthetic_frame` の既定は 5 階。`dungeon_styles.jsonc` の
     * **階の帯**は
     * 階でしか切り替わらないので、これが無いと帯を検分する道が無い
     * （浅間浄穢山は 50 階と 51 階で材が入れ替わる）。
     * @note 0 も指定できる（＝地上と同じ階番号）ので、既定は 0 ではなく **-1** である。
     */
    int synthetic_dun_level{ -1 };
    /*!
     * @brief `--terrain-check` で要求する可視窓を上下左右 N マス狭める（`--shrink-view=N`）。
     * @details **検査の検査**（設計書 §14-4）。狭めれば四隅に必ず欠けが出るはずで、
     * 出なければ「欠けを検出できない検査」だったということになる。
     */
    int shrink_view{ 0 };
    /*!
     * @brief `--terrain-check` で影の直方体を四方 N マス狭める（`--shrink-shadow=N`）。
     * @details **検査の検査**（設計書 §14-4）。`--shrink-view` と同じ考え方で、
     * 狭めれば可視範囲がはみ出すはずである。はみ出さなければ、
     * 「影が可視範囲を覆えているか」を見られない検査だったということになる。
     */
    int shrink_shadow{ 0 };
    /*!
     * @brief 動きの時刻を固定する（`--motion-time=<秒>`）。P6。
     * @details 閲覧・検査モード（`app/checks/view_modes.cpp`）で撮るときに、
     * 時刻が止まっていないと 2 枚を比べられないので入れてある。
     *
     * @note **遊ぶ経路（`run()`）には効かない。**あちらの動きの時刻
     * （`world_seconds`）は 0 から実時間を積むだけで、ここを見ていない。
     * 結線していないのは、`run()` の絵は**そもそも時刻を止めても揃わない**
     * ためである——コアが別プロセスで実時間で走るので、撮る周までに届いている
     * フレームの数が毎回違う（`tools/hd2d_verify/playthrough.py` の
     * `image_health()` に実測を書いた）。遊ぶ経路の絵の突き合わせは
     * `tools/hd2d_verify/replay.py` が担当する。
     */
    float motion_time{ 0.f };
    /*!
     * @brief `--shot=` を**連番で N 枚**撮る（`--motion-frames=N`）。P6。
     * @details 動きの可否は静止画では判断できない（「揺れすぎ／足りない」は動いて初めて分かる）。
     * GIF を作るための手段である。**1 回の起動で撮る**のは、プレハブのメッシュ化と GPU への
     * 転送を N 回繰り返さずに済ませるため（`mill` は 18 万ボクセルある）。
     * 書き出し名は `<shot>` の拡張子の前に `_000` を挟む。
     */
    int motion_frames{ 1 };
    /*!
     * @brief 連番で撮る時間の幅（秒。`--motion-span=<秒>`）。
     * @details **動きの周期をここに入れると GIF が繋がる。**水車は 1/速さ 秒、
     * 振り子は `period` 秒。風は 2 つの正弦を無理数比で重ねてあるので厳密には繋がらない。
     */
    float motion_span{ 2.f };
    /*!
     * @brief プレハブを **N×N 並べて**見る（`--field=N`）。P6。
     * @details 1 個だけ見ても「隣とどう見えるか」は分からない。草原や林で
     * **同期して揺れるべきか／ばらばらか／隣に影響されるべきか**を判断するための手段。
     * 間隔はプレハブの大きさから決める（木は 3 マス、草は 1 マス）。
     */
    int field{ 1 };
    /*!
     * @brief 同時押しと割り当ての検査（`--pad-check`）。**窓も GL もコアも要らない。**
     * @details 見るのは 2 つ。
     * 1. `PadChord` の状態機械（LB／RB が修飾と単独押しを兼ねる所）
     * 2. ボタン → マクロのトリガー列（`lib/pref/pref-xxx.prf` と対になっている符号）
     *
     * どちらも**押してみるまで分からない**類で、実機のコントローラーが無いと
     * 手では試せない。人が触れないものは機械で確かめておかないと、壊れても誰も気付かない。
     */
    bool pad_check{ false };

    /*!
     * @brief 動きの検査（`--motion-check`）。窓も GL も要らない。
     * @details 設計書 §8.2 の 2 つ目「**当たり判定は動かさない**」を毎回確かめる。
     * 回転・振り子・状態連動が実際に動くこと（＝検査が空振りしていないこと）も見る。
     */
    bool motion_check{ false };

    /*!
     * @name ポスト処理（P7）
     * @{
     */
    /*!
     * @brief 効かせる効果（`--post=`）。`off` / `all` / `bloom,fog` / `-dof` のように書く。
     * @details **1 つずつ切れることが要点。**「絵が良くなった」は複数の効果の合計としてしか
     * 見えないので、並べて比べる手段が無いと人は判断できない（`PostFlags::parse`）。
     */
    std::string post_spec;
    //! カラーグレーディングの `.cube` を読む（`--lut=`）。無ければ `GradeParams` から焼く。
    std::string lut_path;
    //! いまの `GradeParams` を `.cube` に書き出して終わる（`--lut-export=`）。画像ツールで詰める手段。
    std::string lut_export_path;
    /*!
     * @brief カットアウェイの半径（画素。`--cutaway=`）。**0 で無効。**
     * @details 既定は 0 ではなく、注視点まわりの「入口が隠れる範囲」を覆う大きさ。
     * `--cutaway=0` にすると遮蔽が戻るので、効いているかを目で比べられる。
     *
     * **190 → 260**（2026-08-09 に決めた「透過処理の範囲をもう 1 周り広げよう」）。
     * P10 で世界に実物の高さのものが入り、とくに大木（最大 7 マス）が近くに立つと
     * 190px では抜けきらなかった。P7 の宿題「建物が実高さを持つ P10 で詰め直す」の答えでもある。
     */
    float cutaway_radius{ 260.f };
    /*!
     * @brief ポスト処理の検査（`--post-check`）。コアを起こさない。
     * @details 効果を 1 つずつ入れて**実際に画素が変わること**と、全部切れば
     * P5・P6 と同じ式（トーンマップだけ）になることを見る。
     */
    bool post_check{ false };
    /*!
     * @brief カットアウェイの検査（`--cutaway-check`）。コアを起こさない。
     * @details 北向き入口を模した合成フレーム（建物の南壁の内側にプレイヤ）で、
     * **切ると見えず・入れると見える**ことを画素の数で確かめる。
     */
    bool cutaway_check{ false };
    /*!
     * @brief `--cutaway-check` の半径を N 画素だけ狭める（`--shrink-cutaway=N`）。
     * @details **検査の検査**（設計書 §14-4）。狭めればプレイヤは見えなくなるはずで、
     * 見えたままなら「見えていないことを検出できない検査」だったということになる。
     */
    int shrink_cutaway{ 0 };
    /*! @} */

    /*!
     * @name UI（P8）
     * @{
     */
    /*!
     * @brief 画面の作り（`--layout=full|hybrid|split`）。
     * @details **3 つとも使う**（2026-08-08 に決めた「全画面 3D と HengbandUi、
     * 折衷を 3 種切り替えられるように」）。実行中は **F9** で回せる。
     * **既定は画面分割**（同・2026-08-08「規定は HengbandUI」）。
     */
    LayoutMode layout{ LayoutMode::Split };
    /*!
     * @name 「起動引数で明示されたか」（P8）
     * @details `hd2d.cfg`（機能メニューが覚える設定）より**起動引数のほうが強い**。
     * 既定値と「利用者が既定値と同じ値を打った」は区別できないので、打たれたことを
     * こちらで覚える。ここが偽の項目だけ cfg で埋める。
     * @{
     */
    bool camera_from_args{ false };
    bool layout_from_args{ false };
    bool post_from_args{ false };
    bool cutaway_from_args{ false };
    bool dof_from_args{ false };
    /*! @} */
    /*!
     * @brief 被写界深度の強さ（`--dof=`）。**並べて比べるための手段**（P10 レビュー 5）。
     * @details 0 = 掛けない 〜 2.4 = 最大。機能メニューの「ぼけの強さ」と同じ値で、
     * 引数のほうが強い（`hd2d.cfg` を上書きする）。`--post-check --shot=` にも効くので、
     * **強さ違いの絵を並べられる**（設計書 §14「並べる手段が無い変更は承認できない」）。
     */
    float dof_strength{ 0.80f };
    /*!
     * @brief UI の検査（`--ui-check`）。コアを起こさない。
     * @details 3 つの作りで**矩形が重ならず・画面からはみ出さず・3D の矩形が空でない**ことと、
     * 合成フレームの中身が実際に画素になることを見る。
     */
    bool ui_check{ false };
    /*!
     * @brief `--ui-check` で 3D の矩形を N 画素だけ広げる（`--grow-scene=N`）。
     * @details **検査の検査**（設計書 §14-4）。広げれば必ずどれかのパネルと重なるはずで、
     * 重ならなければ「重なりを検出できない検査」だったということになる。
     */
    int grow_scene{ 0 };
    /*!
     * @brief `--ui-check` を**実際に全画面へ入れて**回す（`--fullscreen`）。
     * @details 窓の大きさ（`SDL_GetWindowSize`）と描画面の大きさ（`SDL_GL_GetDrawableSize`）は
     * 一致するとは限らない。食い違っていると**画面の右と下に空白が残る**ので、
     * 実際に全画面へ入れて両方を測る手段が要る（実機で踏んだ）。
     */
    bool ui_check_fullscreen{ false };
    /*! @} */

    /*!
     * @name ボクセルエディタ（P9・R13）
     * @details **リリース用ビルド（`/p:HengbandHd2dEditor=0`）には入らない。**そのとき
     * これらの口は「この exe にエディタは入っていない」と言って終わる（黙って無視しない）。
     * @{
     */
    //! 既存のプレハブを開く（`--edit=<名前>`）。**コアを起こさない。**
    std::string edit_name;
    //! 新しいプレハブを作る（`--edit-new=<名前>`）。既にあれば断る（打ち間違いで雛形を作らない）。
    std::string edit_new_name;
    //! エディタの検査（`--edit-check`）。窓も GL も要らない。
    bool edit_check{ false };
    /*! @} */

    /*!
     * @name カメラの既定値（`--camera=見下ろし,水平画角,1マスのpx`）
     *
     * @details **実物を見て決めた値である**（2026-08-11: 見下ろし角 28 度・
     * 画角 70 度・1 マス 65px）。計画 §4-1 の「実物を見て人に決める」がここで済んだ。
     * 設計書 §4.3 の暫定値（46 / 40 / 110）から**3 つとも動いている**ので、
     * 設計書の数をそのまま信じないこと。
     *
     * **`ui/hd2d_settings.h` の既定値と必ず揃えること。**引数を打たなければ
     * そちらが使われる（`hd2d.cfg` も無い初回起動）ので、片方だけ直すと
     * 「引数の有無で絵が変わる」ことになる。
     * @{
     */
    float camera_pitch_deg{ 29.f };
    float camera_fov_deg{ 70.f };
    //! 注視点での 1 マスの見かけの大きさ。
    float camera_cell_px{ 65.f };
    /*! @} */

    /*!
     * @name VR
     * @details **既定は OFF。フラット版の挙動は 1 ビットも変えない**（設計書 §10）。
     * @{
     */
    /*!
     * @brief OpenXR で起動する（`--vr`）。
     * @details 失敗しても致命にはしない——理由を stderr へ出して**フラットで続ける**。
     * HMD もランタイムも無い機械で起動できなくなるほうが困る。
     */
    bool vr{ false };
    /*!
     * @brief 疑似 HMD（`--vr-fake`）。**OpenXR を使わない。**`--vr` とは排他。
     * @details 固定の両眼ポーズ（IPD 64mm）で VR と同じ道を回し、窓へ**左右を並べて**出す
     * （設計書 §9）。`--shot=` がそのまま効くので、HMD が無くても両眼の絵を静止画で
     * 残せる。頭は動かないので、**頭の追従だけは実機でしか確かめられない**。
     */
    bool vr_fake{ false };
    /*!
     * @brief VR の検査（`--vr-check`）。**コアを起こさない。**
     * @details 窓と GL だけ作って OpenXR のセッションを立て、数フレーム回して
     * 「どのランタイムに繋がり・どの状態まで進んだか」を出して終わる。
     * HMD が無い機械でも loader → ランタイムの繋がりまでは確かめられるし、
     * SteamVR の null ドライバなら FOCUSED まで通る（設計書 §13）。
     */
    bool vr_check{ false };
    //! `--vr-check` で回すフレーム数（`--vr-frames=`）。既定は 3 秒ぶんの目安。
    int vr_check_frames{ 300 };
    /*!
     * @brief VR の座標の検査（`--vr-math-check`）。**窓も GL もコアも要らない。**
     * @details 世界（マス・左手系）→ ステージ（メートル・右手系）の写像が
     * 鏡像になっていないかを見る（`xr/xr_math.h`）。左手系の写像ミスは
     * 往復検査では捕まらないので、これは別枠で要る（設計書 §5・罠 9）。
     */
    bool vr_math_check{ false };
    /*! @} */

    /*!
     * @brief 起こし直しの申し送り（`kRunRelaunchCore` を受けた呼び手が入れる）。
     * @details 空なら**ふつうの起動**（コア選択の画から始まる）。
     */
    CoreRelaunch relaunch;
};

//! 実行する。戻り値はプロセスの終了コード。
int run(const AppOptions &options);

} // namespace hd2d
