/*!
 * @file voxel_renderer.h
 * @brief メッシュ化したプレハブを GL 4.6 で描く（P1 の最小のシェーダ）。
 *
 * 段取りは P1 ⑤。
 *
 * ## いまやること／やらないこと
 * | | |
 * |---|---|
 * | やる | 方向光 1 灯 ＋ シャドウマップ（P5）＋ 点光源（P5）＋ 環境光 ＋ 作った AO |
 * | やらない | 風（P6）・ポスト処理（P7） |
 *
 * 光の式は**ここには書かない**。`render/lighting.h` の `kLightingGlsl` を連結して使う
 * （ビルボードと同じ式でなければ、同じ場所に立つ壁と人物の明るさが食い違う）。
 *
 * ## 影のパスは同じ幾何を 2 回描く
 * 設計書 §13 の注のとおり。`draw_instanced_depth()` が深度だけのプログラムである。
 *
 * **頂点シェーダは本描画とまったく同じ文字列を使う**（`init()` が同じ `kVertexSource` から
 * 2 つのプログラムを作る）。設計書 §8.2 は「落とすと必ず破綻するもの」の筆頭に
 * **「影のパスにも同じ変形を適用する。忘れると『揺れているのに影が揺れない』」**を挙げている。
 * 写した 2 本を並べて持つと、片方だけ直す事故がいつか必ず起きるので、**構造的に起こせなく**した。
 * P6 で風を足したときも、足した先は 1 か所しかない。
 */
#pragma once

#include "render/gl_core.h"
#include "render/lighting.h"
#include "render/math3d.h"
#include "render/scene_look.h"
#include "render/leaf_detail.h"
#include "render/surface_wear.h"
#include "voxel/greedy_mesher.h"
#include "voxel/prefab.h"

#include <string>
#include <vector>

namespace hd2d {

/*!
 * @brief 風（P6 ②・設計書 §8.1）。
 *
 * @details 式は設計書のとおり。
 * ```
 * ずらし量 = 風向 × 振幅
 *          × sin(時刻×周期 + 世界座標·k)   ← 世界座標なので隣の草と揃い、風が波として渡る
 *          × しなやかさ(頂点)               ← メッシュ化時に焼く（`VoxelVertex::flex`）
 *          × wind_k(パーツ)
 * ```
 * **世界座標を使うのが要点。**インスタンスごとに位相を配ると、同じ描画呼び出しで
 * 木 8,409 本がそれぞれ違う揺れ方をしつつ、隣どうしは繋がって見える。
 */
//! 風の位相の作り方。**設計書の答えは `Wave`。**残り 2 つは見比べるための手段である。
enum class WindPhase : int {
    /*!
     * @brief **波**（設計書 §8.1）。位相 = 時刻 ＋ 世界座標·k。
     * @details 隣どうしは近い位相になるので繋がって見え、離れるほどずれるので
     * 風が野を渡っていくように見える。インスタンスごとに何も渡さなくてよい。
     */
    Wave = 0,
    /*!
     * @brief **同期**。位相は時刻だけ。
     * @details 一面が同じ拍で動く。**草原では「布」に見える**（1 枚の旗のよう）。
     */
    Synced = 1,
    /*!
     * @brief **ばらばら**。位相をインスタンス原点のハッシュから引く。
     * @details 隣との繋がりが消えるので、**風ではなく個体の痙攣**に見える。
     */
    Scattered = 2,
};

struct WindParams {
    //! 風向（水平・正規化されている前提）と振幅（マス）。
    float dir_x{ 1.f };
    float dir_y{ 0.f };
    float amplitude{ 0.f }; //!< 0 なら無風
    //! 時間の周期（ラジアン/秒）と、世界座標に掛ける波数（ラジアン/マス）。
    float frequency{ 1.6f };
    float wave_k{ 0.55f };
    WindPhase phase{ WindPhase::Wave };
};

/*!
 * @brief カットアウェイ（P7・設計書 §13）。**遮蔽している建物をディザで抜く。**
 *
 * @details 町の入口の 35%（44 箇所）は北を向いていて、通りの南から見下ろすカメラでは
 * 建物自身がその入口を必ず隠す（§10.2）。カメラを回しても直らない構造的なものなので、
 * 「手前にあって、プレイヤの近くを覆っているもの」を網目で抜く。
 *
 * ## 3 つの約束
 * | | 理由 |
 * |---|---|
 * | **深度はプレイヤの位置を投影して作る** | 深度バッファのその画素は「隠している壁」の深度なので、基準にすると常に空振りする |
 * | **床の板には掛けない**（`scale = 0`） | 手前の地面まで抜けて地面に穴が開く |
 * | **影のパスには掛けない** | 物はそこに在る。抜くのは見え方だけで、影は落ち続ける（風の §8.2-1 とは逆に、ここは 2 つのパスが**違わなければならない**） |
 */
struct Cutaway {
    //! プレイヤを投影した画素。**`gl_FragCoord` に合わせて左下原点**（`Camera::project` は左上原点）。
    float centre_x{ 0.f };
    float centre_y{ 0.f };
    //! 抜く半径（画素）。**0 なら無効。**
    float radius{ 0.f };
    /*!
     * @name **カメラ側の半平面**（対象とカメラの間にあるものだけ抜く）
     *
     * @details 2026-08-11 に決めた（・言い直し）は「透過すべきはキャラクターより**南**にある
     * オブジェクトで視界に被るオブジェクト。**真横のオブジェクトを透過しない**」だった。
     * 深度（手前にある）と円（近くにある）だけだと**同じ行の真横の壁**まで抜けてしまう
     * ——真横の壁の面は対象とほぼ同じ深度なので、深度では区別できない。要るのは世界の側の条件で、
     * 「隠しうるのはカメラと対象の**間**にあるものだけ」である。
     *
     * ## 「南」を捨てた理由（2026-08-19・実機の指摘）
     * 初版はカメラが**必ず真南**にあることを使って `world.y` の 1 本で切っていた。
     * 見下ろしの 90° 視点回転を入れた途端、
     * **回した先で透過が起きなくなった**（気づいたこと:「通常以外の方向の場合の透過の判定が
     * されなかった。おそらく南側のみ透過というのが効いてしまっている」）。カメラが東に居るのに
     * 「南にあるか」を見ていたのだから当然である。
     *
     * そこで軸を**カメラの方位から作る**: 法線 `(face_x, face_y)` は水平面内で
     * **対象からカメラへ向かう向き**、`face_min` はその向きに測った対象の位置＋余白。
     * 判定は `dot(world.xy, face) > face_min`＝**カメラ側**。
     *
     * @note **既定は南 (0, 1) と −1e9** で、これは「判定を掛けない」である。
     * さらに `yaw = 0` では法線が厳密に `(0, 1)`（`sin 0 == 0` / `cos 0 == 1`）になるので、
     * **回していないときの絵は 1 ビットも変わらない**。
     * @{
     */
    float face_x{ 0.f };
    float face_y{ 1.f };
    float face_min{ -1e9f };
    /*! @} */
    /*!
     * @brief **高さの境**（world の z。マス単位。これ以下は抜かない）。
     *
     * @details 屋根だけを外すための軸（2026-08-18 に決めた:「屋根自体を透過させキャラの
     * 周囲を一定範囲**高さ 1 ブロックの構造物を残し**屋根（建造物）を透過させる」）。
     * `1.f` を入れると高さ 1 マスより上だけが抜け、床と腰までの壁が残る。
     *
     * 既定の −1e9 は「高さの判定を掛けない」＝**従来の抜きと 1 画素も違わない**
     * （半平面 `face_min` と同じ流儀）。
     *
     * @note 実体（プレイヤ・モンスター）は z 0〜1 に立っているので、
     * **1.f を入れた穴は実体を巻き込まない**（除外の処理は要らない）。
     */
    float roof_min_z{ -1e9f };
    //! プレイヤの位置の窓深度（[0,1]）。これより手前のものが抜ける対象になる。
    float depth{ 1.f };
    /*!
     * @brief 抜きの強さ（P10 レビュー 4）。1 で従来、大きいほどよく抜ける。
     * @details 半径とは別の軸である。半径は「どこまで抜くか」、これは「どれだけ薄くするか」。
     * 2026-08-09 に決めた「透過率はもっと上げた方が見やすい」への答えで、
     * 既定は 2.2（同じ半径でも網目がはっきり粗くなる）。
     */
    float power{ 2.2f };
};

/*!
 * @brief インスタンス 1 つ（位置と色）。
 * @details 位置は**マス単位の世界座標**。色はプレハブのパレット色に掛かる
 * （§7.1-4「インスタンスごとにパレットを少しずらす」の最も素朴な形）。
 */
struct InstanceData {
    float x{};
    float y{};
    float z{};
    //! プレハブ空間での拡大率（1 = そのまま）。**同じプレハブで別の大きさの物を置くため。**
    float sx{ 1.f };
    float sy{ 1.f };
    float sz{ 1.f };
    float r{ 1.f };
    float g{ 1.f };
    float b{ 1.f };
    /*!
     * @name 自発光（P7。積み残し #6）
     * @details **光を受けずに足される色**（線形の HDR）。溶岩や発光地形の「面そのものが
     * 光っている」を作る。P5 で点光源としては効いていたが、面は暗いままだった
     * （＝溶岩の池のそばは明るいのに、池自体は黒い）。
     *
     * **ブルームが掴む相手はこれである。**トーンマップ後の 1.0 で頭打ちになった値からは
     * 「白い床」と「燃えている溶岩」を区別できないので、1 を超える値をここへ入れる。
     * @{
     */
    float er{ 0.f };
    float eg{ 0.f };
    float eb{ 0.f };
    /*! @} */
    /*!
     * @brief **z 軸まわりの向き**（ラジアン。0 = そのまま＝面は南を向く）。
     *
     * @details 設計書 §11.5-6 に「`InstanceData` に回転が無い」と挙げていた穴を、
     * 一人称のために埋めたもの（2026-08-15 に決めた「FPS モードの際は板は
     * キャラに向けて回転させる」）。
     *
     * **回す中心はプレハブ空間の (0.5, 0.5)**——1 マスのプレハブの真ん中である。
     * 実体の板はちょうど 1 マスなのでこれで合う。地形のプレハブは 0 のままなので
     * 中心がどこでも関係しない。**拡大の前に回す**ので、縮めた物（アイテムの 0.7）も
     * 中心を保ったまま回る。
     *
     * 見下ろしの本線では **0 のまま**（2026-08-15 に決めた「見下ろし表示は
     * 南側固定で OK」）。回すのは一人称のときだけである。
     */
    float yaw{ 0.f };
};

//! GPU に載せたパーツ 1 つ。
struct GpuPart {
    gl::GLuint vao{ 0 };
    gl::GLuint vbo{ 0 };
    gl::GLuint ebo{ 0 };
    gl::GLuint atlas{ 0 };
    gl::GLuint instance_vbo{ 0 };
    std::size_t instance_capacity{ 0 };
    gl::GLsizei index_count{ 0 };
    //! パーツのローカル（ボクセル） → プレハブ空間（**マス単位**）。縮尺 1/`voxels_per_cell` を含む。
    Mat4 model{ Mat4::identity() };
    /*!
     * @brief 元の `Prefab::parts` での添字。**動きの行列を引くのに要る**（P6）。
     * @details `upload()` は中身が空のパーツを飛ばすので、`GpuPrefab::parts` の並びは
     * `Prefab::parts` と一致しない。番号を持たずに順番で引くと、空のパーツを持つ
     * プレハブだけ静かに 1 つずれる。
     */
    int prefab_part_index{ -1 };
    //! 風のしなやかさの係数（`PrefabPart::wind_k`）。0 なら風で曲がらない。
    float wind_k{ 0.f };
};

//! GPU に載せたプレハブ 1 つ。
struct GpuPrefab {
    gl::GLuint palette{ 0 };
    std::vector<GpuPart> parts;
    std::size_t quad_count{ 0 };
    std::size_t naive_face_count{ 0 };
    std::size_t triangle_count{ 0 };
    int atlas_side{ 0 };
};

/*!
 * @brief メッシュ化だけ済ませた中間。**GL に触らない。**
 *
 * @details `upload()` を「メッシュ化」と「GPU へ載せる」に割るために足した（2026-08-23）。
 * 割った理由は**コストの内訳**である——プレハブ 2,514 個で読み込み 465 ms に対し
 * `upload()` が 5,980 ms、そのうち `mesh_model()`（貪欲メッシュ化とアトラス作り）が
 * **5,755 ms** で GL の呼び出しは 225 ms しかない。メッシュ化は純粋な CPU 仕事で
 * プレハブごとに独立なので、**別スレッドで先に回せる**（`PrefabLibrary::upload_gpu`）。
 *
 * @note **中身が空のパーツは入らない**（`upload()` の従来の規律。`prefab_part_index` を
 * 持っているのはそのため）。
 */
struct PrefabMesh {
    struct Part {
        VoxelMesh mesh;
        Mat4 model{ Mat4::identity() };
        int prefab_part_index{ -1 };
        float wind_k{ 0.f };
    };
    std::vector<Part> parts;
    std::size_t quad_count{ 0 };
    std::size_t naive_face_count{ 0 };
    std::size_t triangle_count{ 0 };
    int atlas_side{ 0 };

    /*!
     * @brief 抱えている大きさ（バイト）。**先回りの上限を測るのに使う。**
     * @details `PrefabLibrary` はメッシュ化を先回りさせるが、載せるまでは記憶に残る。
     * 個数で数えると 64×64 の小物も 1024×1024 のアトラス（2 MB）も同じ 1 個になり、
     * 大物が続いたときだけ峰が跳ねる。だから**バイトで数える**。
     */
    std::size_t bytes() const
    {
        std::size_t total = 0;
        for (const Part &part : this->parts) {
            total += part.mesh.atlas.size();
            total += part.mesh.vertices.size() * sizeof(VoxelVertex);
            total += part.mesh.indices.size() * sizeof(std::uint32_t);
        }
        return total;
    }
};

class VoxelRenderer {
public:
    bool init(std::string &err);
    void shutdown();

    /*!
     * @brief プレハブをメッシュ化して GPU へ載せる（`build_mesh` → `upload_meshed`）。
     * @param[out] err 失敗の理由。
     * @note メッシュ化はここで行う（設計書 §6「読み込み時にメッシュへ焼く」）。
     */
    bool upload(const Prefab &prefab, GpuPrefab &out, std::string &err);
    /*!
     * @brief メッシュ化だけ。**GL を触らないので、どのスレッドから呼んでもよい。**
     * @param verbose 1 パーツ 1 行の内訳を stderr へ出すか。ライブラリの一括では**切る**
     * （2,615 行になる。Android では stderr がパイプ経由で logcat へ流れるので特に高い）。
     */
    static bool build_mesh(const Prefab &prefab, PrefabMesh &out, std::string &err, bool verbose = true);
    //! `build_mesh()` の結果を GPU へ。**GL を触るので描画スレッドから。**
    bool upload_meshed(const Prefab &prefab, const PrefabMesh &meshed, GpuPrefab &out, std::string &err);
    void release(GpuPrefab &prefab);

    /*!
     * @brief このフレームの視点と光。
     * @param light_view_projection 影の地図を作った行列（`ShadowFit::view_projection`）。
     * @param shadow_texture 影の深度テクスチャ。**0 なら影を落とさない**（`--prefab` 等）。
     */
    void begin(const Mat4 &view_projection, const SceneLighting &lighting,
        const Mat4 &light_view_projection, gl::GLuint shadow_texture, int shadow_side);

    /*!
     * @brief このフレームの動き（P6）。**`begin()` と同じく 1 フレーム 1 回。**
     * @param wind 風。`begin_motion` を呼ばなければ風は止まる（既定は無風）。
     * @param time 秒。頂点シェーダの位相に使う。
     * @details 剛体の動き（`part_motions`）は描くときに渡す。**同じ値を影のパスへも渡すこと**
     * （§8.2）。渡し忘れは `--motion-check` が捕まえる。
     */
    void begin_motion(const WindParams &wind, float time);

    /*!
     * @brief このフレームの**画調**。1 フレーム 1 回でよい。
     * @details 既定は `LookParams{}`（`enabled = 0` ＝ 標準）なので、
     * **1 度も呼ばなければ従来の絵と 1 ビットも変わらない**。
     * @note **ビルボードにも同じものを渡すこと。**片方だけ TRON にすると、
     * 同じ場所に立つ壁と人物で画調が食い違う（`lighting.h` の「式を 1 か所に」と同じ理由）。
     */
    void set_look(const LookParams &look);
    /*!
     * @brief 面のアスキー文字の材。
     * @details 呼ばなければ字は出ない（線と黒い面だけになる）。**テクスチャ単位 3・4 を使う**
     * ——0（面アトラス）・1（パレット）・2（影）は既に埋まっている。
     */
    void set_look_glyphs(const LookGlyphSource &glyphs);

    /*!
     * @brief このフレームの**面の汚し**（`render/surface_wear.h`）。1 フレーム 1 回でよい。
     * @details 既定は `WearParams{}`（`amount = 0`）なので、**1 度も呼ばなければ
     * 従来の絵と 1 ビットも変わらない**。
     * @note ビルボード（かきわり）へは渡さない——実体の板は描き込んであるタイルで、
     * 上から汚すと絵師の描いた濃淡と喧嘩する。
     */
    void set_wear(const WearParams &wear);

    /*!
     * @brief このフレームの**木の葉**（`render/leaf_detail.h`）。1 フレーム 1 回でよい。
     * @details 既定は `LeafParams{}`（`amount = 0`）なので、呼ばなければ従来どおり。
     * 掛かるのは材質が `MaterialClass::Leaf` の面だけ。
     */
    void set_leaf(const LeafParams &leaf);

    /*!
     * @brief このフレームのカットアウェイ（P7）。**`begin()` と同じく 1 フレーム 1 回。**
     * @details 呼ばなければ半径 0 ＝ 無効。影のパスは**これを読まない**（`Cutaway` の注記）。
     */
    void begin_cutaway(const Cutaway &cutaway);
    /*!
     * @brief 抜く所を**複数**渡す（P10 第 4 期。2026-08-10 に気づいた）。
     * @details 「視界の範囲内にあるモンスター、アイテムが壁ブロック等の 1 マス北に
     * ある場合（隠れて見えない場合）は透過して見えるように」。プレイヤのぶんに加えて、
     * **隠れている実体 1 体につき 1 つ**穴が要る。
     *
     * 半径 0 の要素は捨てて詰める。`kMaxCutaways` を超えたぶんは**入れない**
     * （多いほうから選ぶ仕組みは持たせていない。呼ぶ側が並べ替えて渡す）。
     */
    void begin_cutaways(const Cutaway *list, std::size_t count);
    //! 一度に抜ける所の数。シェーダの配列と同じ値でなければならない。
    static constexpr std::size_t kMaxCutaways = 12;
    /*!
     * @brief 次の描画にカットアウェイを掛けるか（0 = 掛けない / 1 = 掛ける）。
     * @details **床の板は 0 にすること。**掛けると手前の地面まで抜けて穴が開く。
     */
    void set_cutaway_scale(float scale);
    /*!
     * @brief 次の描画が影を受けるか（1 = 受ける / 0 = 受けない）。P10 レビュー 10。
     *
     * @details **抜いた底面（`TerrainView::under_slabs`）を 0 にすること。**
     * 底面は壁の真下にあるので、壁自身の影で必ず真っ暗になる。ふだんは壁に隠れていて
     * 誰も困らないが、**カットアウェイが壁を抜いた瞬間、そこだけ黒く沈んで
     * 「何があったか」の色（床＝茶／壁＝灰／岩＝黒に近い灰）が読めなくなる**
     * （2026-08-09 に気づいた:「手前の面の影が黒く残っていて底面にかぶり判別しづらい。
     * 底面だけを描画するようにして」）。
     *
     * 底面は**物理的な地面ではなく「そこに何があったか」の印**なので、影を受けない。
     * 抜いていないときは壁に隠れて見えないから、この扱いで絵が変わるのは抜けた所だけである。
     */
    void set_shadow_scale(float scale);

    /*!
     * @brief 1 個だけ描く（プレハブ見物用）。位置はプレハブ空間の原点。
     * @param part_motions `Prefab::parts` と同じ並びの動きの行列。`nullptr` なら静止。
     */
    void draw(const GpuPrefab &prefab, const Mat4 *part_motions = nullptr, std::size_t motion_count = 0);
    /*!
     * @brief 同じプレハブを `count` 個まとめて描く。
     * @details 地形はマスの数だけ同じ箱を置くので、1 マス 1 回の描画呼び出しにしない。
     * P3 の本格的なインスタンス描画（LOD・カリング付き）の土台でもある。
     */
    void draw_instanced(GpuPrefab &prefab, const InstanceData *instances, std::size_t count,
        const Mat4 *part_motions = nullptr, std::size_t motion_count = 0);

    /*!
     * @brief 影の地図へ深度だけ書く（P5 ①）。
     * @param light_view_projection 光の側の行列。
     * @param part_motions **本描画へ渡すのと同じものを渡すこと**（§8.2）。
     * @details 色も光も要らないので深度だけのプログラムで描くが、**頂点シェーダは同じ**。
     *
     * **表面ではなく裏面を残す**（`glCullFace(GL_FRONT)`）。中の詰まった箱では、
     * 光から遠い側の深度を書くほうが下駄が要らず、平らな面の縞（shadow acne）が出にくい。
     */
    void draw_instanced_depth(GpuPrefab &prefab, const Mat4 &light_view_projection,
        const InstanceData *instances, std::size_t count,
        const Mat4 *part_motions = nullptr, std::size_t motion_count = 0);

    //! `draw()` に対する影のパス（1 個だけ。プレハブ見物用）。
    void draw_depth_single(const GpuPrefab &prefab, const Mat4 &light_view_projection,
        const Mat4 *part_motions = nullptr, std::size_t motion_count = 0);

private:
    void bind_common(const GpuPrefab &prefab);
    //! パーツの合成行列（動き × 静的な配置）を選んだプログラムへ送る。
    void set_part_model(gl::GLint location, const GpuPart &part, const Mat4 *part_motions, std::size_t motion_count);
    //! 風のユニフォームを送る。**本描画と影のパスで同じ値**（§8.2）。
    void upload_wind(gl::GLint loc_wind_vector, gl::GLint loc_wind_wave, gl::GLint loc_time, gl::GLint loc_phase);

    gl::GLuint program_{ 0 };
    gl::GLint loc_view_projection_{ -1 };
    gl::GLint loc_model_{ -1 };
    gl::GLint loc_atlas_{ -1 };
    gl::GLint loc_palette_{ -1 };
    gl::GLint loc_wind_vector_{ -1 };
    gl::GLint loc_wind_wave_{ -1 };
    gl::GLint loc_time_{ -1 };
    gl::GLint loc_part_wind_k_{ -1 };
    gl::GLint loc_wind_phase_{ -1 };
    gl::GLint loc_cutaway_centre_{ -1 };
    gl::GLint loc_cutaway_radius_{ -1 };
    gl::GLint loc_cutaway_face_{ -1 };
    gl::GLint loc_cutaway_min_z_{ -1 };
    gl::GLint loc_cutaway_depth_{ -1 };
    gl::GLint loc_cutaway_count_{ -1 };
    gl::GLint loc_cutaway_scale_{ -1 };
    gl::GLint loc_cutaway_power_{ -1 };
    LightUniforms light_loc_;
    LookUniforms look_loc_;
    WearUniforms wear_loc_;
    LeafUniforms leaf_loc_;

    gl::GLuint depth_program_{ 0 };
    gl::GLint depth_loc_view_projection_{ -1 };
    gl::GLint depth_loc_model_{ -1 };
    gl::GLint depth_loc_wind_vector_{ -1 };
    gl::GLint depth_loc_wind_wave_{ -1 };
    gl::GLint depth_loc_time_{ -1 };
    gl::GLint depth_loc_part_wind_k_{ -1 };
    gl::GLint depth_loc_wind_phase_{ -1 };

    WindParams wind_;
    //! 画調（`set_look`）。**既定は標準**なので、呼ばなければ従来どおり。
    LookParams look_;
    //! 面の字の材（`set_look_glyphs`）。既定は空＝字を出さない。
    LookGlyphSource look_glyphs_;
    //! 面の汚し（`set_wear`）。**既定は掛けない。**
    WearParams wear_;
    //! 木の葉（`set_leaf`）。**既定は描かない。**
    LeafParams leaf_;
    float time_{ 0.f };
    std::vector<Cutaway> cutaways_;
    float cutaway_scale_{ 0.f };
    //! 影を受ける割合（P10 レビュー 10。抜いた底面だけ 0 にする）。
    float shadow_scale_{ 1.f };
    Mat4 view_projection_{ Mat4::identity() };
    Mat4 light_view_projection_{ Mat4::identity() };
    SceneLighting lighting_;
    gl::GLuint shadow_texture_{ 0 };
    int shadow_side_{ 0 };
};

} // namespace hd2d
