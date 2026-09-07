/*!
 * @file post_process.h
 * @brief ポスト処理（P7）— 画面の色と深度を取り、ブルーム・被写界深度・フォグ・
 *   カラーグレーディング・ビネットを掛けてから既定のフレームバッファへ出す。
 *
 * 段取りは P7。
 *
 * ## 何が変わったか（P6 まで／P7 から）
 *
 * | | P6 まで | P7 から |
 * |---|---|---|
 * | 物のシェーダが書くもの | トーンマップ済みの色 | **線形の HDR** |
 * | 描く先 | 既定のフレームバッファ | **場面のレンダーターゲット（RGBA16F ＋ 深度テクスチャ）** |
 * | トーンマップ | ボクセルとビルボードに 1 つずつ | **合成の 1 か所だけ** |
 * | 文字（UI） | 3D の後 | **合成の後**（ポスト処理を通さない） |
 *
 * **トーンマップを動かしたのは気分ではない。**ブルームの閾値抽出は
 * トーンマップ**前**の値を相手にしなければ意味を持たない（1.0 で頭打ちにした後では
 * 「白い紙」と「燃えている松明」が同じ値になる）。設計書 §13 の「閾値抽出 → 縮小 →
 * ぼかし → 加算」はその前提の上に書かれている。
 *
 * **効果を全部切れば P5・P6 の見えと同一になる**（掛ける場所が変わっただけ）。
 * `--post-check` の (1) がそれを毎回確かめる。
 *
 * ## 順序（`composite` の中）
 *
 * ```
 * HDR ──▶ 被写界深度（premultiplied CoC のピラミッドから、錯乱円に合う段を引く）
 *      ──▶ 深度フォグ
 *      ──▶ ブルームを足す
 *      ──▶ トーンマップ            ← ここで [0,1] へ
 *      ──▶ 3D LUT カラーグレーディング
 *      ──▶ ビネット
 * ```
 *
 * **背景（幾何が 1 つも無い画素）にフォグは掛けない。**被写界深度は、レビュー 9 の
 * 追記から**「ぼけた幾何のかぶり」だけ**背景へ被せる（手前のブロックが未踏破の闇と
 * 接する輪郭にもぼけの裾を出すため。背景自体の色をぼかすわけではない）。
 * 「画面の隅に背景色が見えているか」を数える検査（`--terrain-check` の四隅の欠け）は
 * `view_projection` を渡さない経路（＝被写界深度が不発）なので、これで盲目にはならない。
 *
 * ## コスト
 * 全部入りで 1600x900 のとき 0.6ms 前後（`HD2D_GPU_SYNC=1` で測ること。§14-9）。
 */
#pragma once

#include "render/gl_core.h"
#include "render/math3d.h"

#include <string>
#include <vector>

namespace hd2d {

/*!
 * @brief 効果の入切。**1 つずつ切れること**が要点である。
 * @details 「絵が良くなった」は複数の効果の合計としてしか見えないので、
 * どれが効いているのかを人が判断できない。並べて比べる手段が無い変更は承認できない。
 */
struct PostFlags {
    bool fog{ true };
    bool dof{ true };
    bool bloom{ true };
    bool grade{ true };
    bool vignette{ true };

    bool any() const;
    /*!
     * @brief `--post=` / `HD2D_POST=` の綴りから読む。
     * @param spec `off`（全部切る）/ `all` / `bloom,fog` のような並び / `-dof` で 1 つだけ落とす。
     * @param[out] err 綴りが読めなかったときの理由。
     * @return 読めたら true。
     */
    static bool parse(const std::string &spec, PostFlags &out, std::string &err);
    //! 画面と記録に出す 1 行（`bloom+ dof- ...`）。
    std::string to_line() const;
};

/*!
 * @brief 効果の強さ。**自分で決めた P5 の色と P6 の揺れを壊さない範囲**が既定値。
 * @details 引き継ぎ §4 の「カラーグレーディングはその見えを基準に足すこと」に従い、
 * どれも「言われないと気づかないが、切ると物足りない」量にしてある。
 */
/*!
 * @name 深度フォグの既定（**マス**）
 * @details 名前を付けてあるのは VR のためである。VR の視点空間は**メートル**なので、
 * 呼ぶ側が 1 マスのメートル数を掛けて渡し直す。
 * 数を 2 か所に書くと、片方だけ直された状態に必ずなる。
 * @{
 */
inline constexpr float kFogStartCells = 16.f;
inline constexpr float kFogRangeCells = 48.f;
/*! @} */

struct PostParams {
    /*! @name ブルーム（閾値抽出 → 縮小 → ぼかし → 加算）
     *
     * @details 閾値は**トーンマップ前**の輝度。**この値は実測から決めた**（P7）。
     *
     * | 絵 | いちばん明るい所（HDR） |
     * |---|---|
     * | 地上の町・正午 | **0.95**（＝ 1 を超えるものが 1 つも無い） |
     * | 地下・松明だけ | 0.83（同上） |
     * | 地下・溶岩あり | **3.15**（自発光。積み残し #6 を P7 で入れた） |
     *
     * つまり**この世界で「1 を超えて光っているもの」は光源そのものしかない。**
     * 閾値 1.10 はそれだけを拾う値である。
     *
     * **膝（knee）を広げてはならない。**効き始めは実質 `閾値 − 膝` なので、
     * 0.55 にすると正午の町が丸ごと（画素の 93%）にじむ。0.20 なら 0.90 からで、
     * 日向のいちばん明るい面にわずかな縁が出るだけになる。1 度そうして「白くて綺麗だが
     * 何も読めない画面」を作ったので、値を動かすときは `--post-check` の (0) を見ること。
     * @{ */
    float bloom_threshold{ 1.10f };
    float bloom_knee{ 0.20f }; //!< しきい値の当たりを柔らかくする幅（0 で硬い切り口）
    float bloom_intensity{ 0.42f };
    /*! @} */

    /*!
     * @name 被写界深度 — **キャラからの前後の隔たり**で効く（P10 レビュー 8 で確定）
     *
     * @details 測る軸はレビュー 8 の指示のまま（2026-08-09）:
     * > 横方向はぼかしの強度を変えない。**前後の距離のみでぼかし強度を変化させるのが
     * > TiltShift だ。**一様に帯でもなく、前後の奥行、キャラの前後の距離に比例、左右は一定
     *
     * ここまで 3 回外している:
     * | 版 | 何で測っていたか | なぜ違ったか |
     * |---|---|---|
     * | P7〜レビュー 5 | カメラからの**深度** | 同じ深度のものが画面のどこでも鮮明なまま |
     * | レビュー 6 | **画面の横帯** | キャラの真横の木が、画面の上に写っただけでぼけた |
     * | レビュー 7 | キャラからの**水平距離** | 同心円になり、**真横に離れた木までぼけた** |
     * | **レビュー 8** | **前方向へ射影した隔たり** | 左右には効かず、前後だけで変わる |
     *
     * **掛け方はレビュー 9 で作り直した**:
     * > 木のオブジェクトのふちがくっきり残ってしまっている。位置でぼかすことが
     * > 確定であればふちまでぼかして。また一定以上の距離のぼかしが均一になっているので、
     * > 画面表示される範囲ではどこまでも比例してぼかせないか
     *
     * | 指摘 | 前の作り（なぜそうなったか） | いまの作り |
     * |---|---|---|
     * | 縁が残る | `mix(元絵, 写し, coc×強さ)` の重みが 1 に届かず、**鮮明な元絵が必ず透けた** | 重みは 0→1 の立ち上がりだけ。**ぼけの量は重みではなくピラミッドの段（lod）が運ぶ** |
     * | 遠方が均一 | 錯乱円を固定のマス数（fade 7）で正規化 ＋ 下ごしらえのぼかしが一様 | **画面に実際に見えている最遠の隔たり**で正規化し、lod で半径そのものを変える |
     *
     * 写しは premultiplied CoC（色×錯乱円, 錯乱円）のミップピラミッドで持つ。
     * 焦点帯の色は写しにほぼ入らない（×0）ので、鮮明な色がぼけへ滲む色漏れも同時に減る。
     * 縁は「近所の錯乱円」をピラミッドの粗い段から拾って溶かす（輪郭の外へも中へも滲む）。
     *
     * 画素ごとに深度からワールド座標を戻して測るので、**同じブロックでも手前の縁と
     * 奥の縁でぼけ方が変わる**（マス単位で切り替わらない）。
     * @{
     */
    float dof_inner{ 2.2f }; //!< キャラからこの**前後の**隔たりまでは鮮明（マス）
    /*!
     * @brief 画面のいちばん遠くでの錯乱円の半径（**フル解像度の画素**。強さ 1 のとき）。
     * @details 錯乱円は「前後の隔たり ÷ 画面に見えている最遠の隔たり」に比例させる。
     * 固定のマス数で割ると遠方が頭打ちになる（レビュー 9 の指摘そのもの）。
     */
    float dof_max_radius{ 20.f };
    /*!
     * @brief ぼけの強さ（0 〜 2.4）。機能メニューの「ぼけの強さ」がここへ入る。
     * @details 錯乱円の半径の倍率と、鮮明な帯（`dof_inner`）の狭まりを決める。
     * 0.01 以下は「切った」と同じ（メニューの 0 がそれ）。
     */
    float dof_strength{ 0.80f };
    /*! @} */

    /*! @name 深度フォグ（空気遠近）
     * @details 色は**その時刻の空の色**を渡すこと（`SceneLighting::sky_color`）。
     * 固定色にすると夕焼けの中で 1 か所だけ昼のままになる。
     * @{ */
    Vec3 fog_color{ 0.58f, 0.68f, 0.90f };
    float fog_start{ kFogStartCells }; //!< ここから掛かり始める（マス）
    float fog_range{ kFogRangeCells };
    float fog_density{ 0.42f }; //!< いちばん奥でどれだけ空の色に寄せるか
    /*! @} */

    /*! @name ビネット（四隅の落ち）
     * @{ */
    float vignette_strength{ 0.28f };
    float vignette_radius{ 0.62f }; //!< ここから外が落ち始める（画面中心からの正規化距離）
    /*! @} */

    /*!
     * @name 露出と HDR 風の見え（2026-08-23。「HDR 効果も追加したい」と決めた）
     *
     * @details **表示機への HDR 出力ではない**（あちらは交換鎖と OS の話で、別物である）。
     * ここでやるのは**写真の HDR 合成の見え**——影が開き、白飛びが戻り、色が締まる。
     *
     * | | |
     * |---|---|
     * | `exposure` | トーンマップの係数。**既定 1.25 は従来の値そのもの**（`hd2d_tonemap`） |
     * | `hdr` | 0 で掛けない。上げるほど影が開き、ハイライトが寝て、彩度が締まる |
     *
     * 掛かる場所は**トーンマップの後・グレーディングの前**である。前に掛けると
     * 発散した値の上で影を持ち上げることになり、光源の周りだけが濁る。
     * @{
     */
    float exposure{ 1.25f };
    float hdr{ 0.f };
    /*! @} */
};

/*!
 * @brief カラーグレーディングの元になる値。**ここから 3D LUT を焼く。**
 *
 * @details 設計書 §13 は「セピア寄りの色調」を 3D LUT で作れと言っている。LUT を
 * 画像として持つと**人が直せない**（32³ の格子を手で触ることはできない）ので、
 * 意味のある値からその場で焼く形にした。`--lut-export=` で `.cube` に書き出せるので、
 * 画像ツールで詰めたものを `--lut=` で読み戻すこともできる。
 *
 * 掛かる場所は**トーンマップの後**（値が [0,1] に収まっている所）である。前に掛けると
 * 「明るい所を暖色へ」が発散した値の上で起きて、白飛びの色が転ぶ。
 */
struct GradeParams {
    float lift{ 0.020f }; //!< 黒の持ち上げ。フィルムの黒は真っ黒ではない
    float contrast{ 1.06f }; //!< 0.5 を軸に
    float saturation{ 0.94f }; //!< わずかに落とす（彩度が高いままだと写真に見えない）
    /*!
     * @brief 影と光の色分け（split toning）。**「セピア寄り」の実体はここ**である。
     * @details 全体を茶色くすると汚れて見えるだけなので、**影を寒色・光を暖色**へ振る。
     * 参考画像の「夕方の街」の印象はこの分離から来ている。
     */
    Vec3 shadow_tint{ 0.965f, 0.985f, 1.060f };
    Vec3 highlight_tint{ 1.055f, 1.005f, 0.930f };
};

/*!
 * @brief **セピア調**へ寄せた値を作る（2026-08-23。「セピア調も追加したい」と決めた）。
 *
 * @param base   元の値（`GradeParams{}` か画調が決めたもの）
 * @param amount 0 = そのまま／1 = 目一杯セピア
 *
 * @details 全体を茶色く掛けるのではなく、**彩度を落として影と光の色分けを茶へ振る**。
 * 一様に茶色を掛けると「汚れた画面」にしか見えない（`GradeParams::shadow_tint` の注記と
 * 同じ理由）。古写真は**影が冷たく残り、光が黄褐へ寄る**。
 */
GradeParams sepia_grade(const GradeParams &base, float amount);

/*!
 * @brief 被写界深度が使う 3 つの数（**ポスト処理と、粒を描く側で同じものを使う**）。
 *
 * @details 2026-08-23 に外へ出した。埃（`render/dust_motes.h`）は
 * **自分でぼけた大きさに広がってから**描く必要がある（点のままだと、後からぼかしても
 * 中央に鮮明な芯が残る）。そのとき**同じ錯乱円の式**を使わないと、粒だけ違うぼけ方をする。
 */
struct DofView {
    float inner{ 2.2f }; //!< ここまでは鮮明（マス）
    float span{ 8.f }; //!< 正規化の分母（マス）。画面に見えている最遠 − `inner`
    float radius_px{ 0.f }; //!< いちばん遠くでの錯乱円の半径（フル解像度の画素）
    float forward_x{ 0.f }; //!< 奥行きを測る軸（正規化済み）
    float forward_y{ 1.f };
};

/*!
 * @brief `DofView` を測る。**`PostChain::resolve()` が使っているものと同じ計算**である。
 * @param params 強さと内側の帯
 * @param view_projection いまの視点
 * @param focus_x, focus_y 焦点（キャラの水平位置。マス）
 * @param forward_x, forward_y カメラの水平前方向（正規化していなくてよい）
 * @details 強さが 0 なら `radius_px` が 0 になる（＝どこもぼけない）。
 */
DofView measure_dof(const PostParams &params, const Mat4 &view_projection, float focus_x, float focus_y,
    float forward_x, float forward_y);

//! LUT の一辺。32 なら 32³ × 4 バイト ＝ 128KB。これ以上は目で違いが出ない。
constexpr int kLutSize = 32;

/*!
 * @brief `GradeParams` から LUT を焼く（RGBA8・`kLutSize`³・赤が最も速く回る）。
 * @param[out] rgba `size*size*size*4` バイト。
 */
void bake_lut(const GradeParams &grade, int size, std::vector<unsigned char> &rgba);

//! `.cube` として書き出す（人が画像ツールで詰めるための手段）。
bool write_lut_cube(const std::string &path, const GradeParams &grade, int size, std::string &err);

/*!
 * @brief `.cube` を読む。
 * @param[out] size 一辺。
 * @param[out] rgba `size³*4` バイト。
 * @details `LUT_3D_SIZE` と `DOMAIN_MIN/MAX` だけ見る。**赤が最も速く回る**のが `.cube` の約束。
 */
bool read_lut_cube(const std::string &path, int &size, std::vector<unsigned char> &rgba, std::string &err);

/*!
 * @brief 画面の色（HDR）と深度を取る FBO と、そこへ掛ける処理の鎖。
 *
 * @details `shadow_map.*` と同じ作りだが、**色の添付を持つ**のと**大きさが窓に追従する**のが違う。
 * 用意に失敗しても致命ではない（`ready()` が偽なら呼び出し側は既定のフレームバッファへ
 * 直に描けばよい）……**とはしていない。**トーンマップがここにしか無いので、
 * 失敗したら絵が白く飛ぶ。P5 の影と違って**無いと成立しない**ので、失敗は起動の失敗にする。
 */
class PostChain {
public:
    bool init(std::string &err);
    void shutdown();

    /*!
     * @brief 画面の大きさに合わせる。**同じ大きさなら何もしない**ので毎フレーム呼んでよい。
     * @details ブルームの段（半分・1/4 …）もここで作り直す。
     */
    bool resize(int width, int height, std::string &err);
    bool ready() const { return this->scene_fbo_ != 0; }
    int width() const { return this->width_; }
    int height() const { return this->height_; }

    //! 画面用の FBO を結んで消す。**以降の 3D はここへ描かれる。**
    void begin_scene(float clear_r, float clear_g, float clear_b);
    /*!
     * @brief 出す先のフレームバッファへ戻す（まだ何も出さない）。
     * @param viewport_x,viewport_y 出す先の矩形の原点。**GL の約束どおり左下が原点**である
     *   （UI の矩形は左上原点なので、渡す前に `screen_h - rect.y - rect.h` へ直すこと）。
     * @param target_fbo 出す先。**既定の 0 は窓**。VR では目の swapchain の FBO を渡す
     * @details 既定の 0,0 は「窓いっぱい」。P8 の `Split`（3D が MainMap 矩形の中）でだけ
     * 0 以外になる。検査の経路は全部いっぱいなので既定のまま呼んでいる。
     */
    void end_scene(int viewport_w, int viewport_h, int viewport_x = 0, int viewport_y = 0,
        gl::GLuint target_fbo = 0);

    /*!
     * @brief 掛けて既定のフレームバッファへ出す。
     * @param z_near,z_far カメラの深度範囲（深度テクスチャを距離へ戻すのに要る）。
     * @param focus_distance 焦点までの距離（マス）。フォグの参考にだけ使う。
     * @param view_projection このフレームの視点行列。**被写界深度が画素をワールドへ戻すのに要る。**
     * @param focus_x,focus_y キャラの水平位置（マス）。ここを基準に前後の隔たりを測る。
     * @param forward_x,forward_y **カメラの水平前方向**（正規化していなくてよい）。
     *   ぼけはこの軸に沿った隔たりだけで決まり、**左右には効かない**。
     * @param screen_x,screen_y 出す先の矩形の原点（**左下原点**。`end_scene` と同じ約束）。
     * @param target_fbo 合成の出し先。**既定の 0 は窓**。VR では目の swapchain の FBO。
     * @note **`end_scene()` の後に呼ぶこと。**文字（UI）はさらにこの後に描く。
     */
    void resolve(const PostFlags &asked, const PostParams &params,
        float z_near, float z_far, float focus_distance, int screen_w, int screen_h,
        int screen_x = 0, int screen_y = 0,
        const Mat4 &view_projection = Mat4::identity(), float focus_x = 0.f, float focus_y = 0.f,
        float forward_x = 0.f, float forward_y = 1.f, gl::GLuint target_fbo = 0);

    gl::GLuint color_texture() const { return this->scene_color_; }
    gl::GLuint depth_texture() const { return this->scene_depth_; }

    //! `GradeParams` から LUT を焼いて GPU へ載せる。
    bool set_grade(const GradeParams &grade, std::string &err);
    //! `.cube` を読んで GPU へ載せる。
    bool load_lut_cube(const std::string &path, std::string &err);
    //! いま載っている LUT の出どころ（画面と記録に出す）。
    const std::string &lut_source() const { return this->lut_source_; }

private:
    struct Rt {
        gl::GLuint texture{ 0 };
        gl::GLuint fbo{ 0 };
        int w{ 0 };
        int h{ 0 };
    };

    bool make_color_rt(int w, int h, Rt &out, std::string &err);
    void release_rt(Rt &rt);
    void release_targets();
    void draw_fullscreen();
    bool upload_lut(int size, const std::vector<unsigned char> &rgba, std::string &err);

    int width_{ 0 };
    int height_{ 0 };

    gl::GLuint scene_fbo_{ 0 };
    gl::GLuint scene_color_{ 0 };
    gl::GLuint scene_depth_{ 0 };

    //! ブルームの段（[0] が半分の大きさ、以降は半分ずつ）。
    std::vector<Rt> bloom_;
    /*!
     * @name 被写界深度のピラミッド（P10 レビュー 9）
     * @details **1 枚のテクスチャにミップの段として持つ**（半分 → 1/4 → … 1/64）。
     * 段が別々のテクスチャだと、合成が錯乱円の大きさで段の**間**を滑らかに引けない
     * （`textureLod` のトライリニアが段をまたげるのは 1 枚のときだけ）。
     * 中身は premultiplied CoC（色×錯乱円, 錯乱円）。作業場（`dof_scratch_`）は
     * 縮小とぼかしの中継で、段ごとに同じ大きさを 1 枚ずつ持つ。
     * @{
     */
    gl::GLuint dof_pyr_tex_{ 0 };
    std::vector<gl::GLuint> dof_pyr_fbos_; //!< 段ごとの FBO（level i を貼る）
    std::vector<Rt> dof_scratch_;
    int dof_base_w_{ 0 }; //!< 段 0（半分の大きさ）の幅
    int dof_base_h_{ 0 };
    /*! @} */

    gl::GLuint vao_{ 0 }; //!< 頂点属性を持たない全画面三角形（`gl_VertexID` から作る）
    gl::GLuint lut_{ 0 };
    int lut_size_{ 0 };
    std::string lut_source_{ "(なし)" };
    /*!
     * @brief `HD2D_BREAK_POST` で**わざと落とす**効果（検査の検査。P6 の
     * `HD2D_BREAK_SHADOW_MOTION` と同じ考え方）。立っているものは `resolve()` が掛けない。
     */
    PostFlags broken_{ false, false, false, false, false };

    gl::GLuint prog_bright_{ 0 };
    gl::GLuint prog_down_{ 0 };
    gl::GLuint prog_up_{ 0 };
    gl::GLuint prog_blur_{ 0 };
    gl::GLuint prog_dof_prepare_{ 0 }; //!< 深度 → 錯乱円を測って premultiplied で書く（P10 レビュー 9）
    gl::GLuint prog_dof_down_{ 0 }; //!< ピラミッドの縮小（α も運ぶ・段を `textureLod` で指す）
    gl::GLuint prog_composite_{ 0 };
};

} // namespace hd2d
