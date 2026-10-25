/*!
 * @file feature_menu.cpp
 * @brief `feature_menu.h` の実装。
 */
#include "ui/feature_menu.h"
#include "ui/external_binds.h"
#include "app/ui_probe.h"
#include "ui/menu_scale.h"
#include "ui/menu_frame.h"
#include "ui/modern_skin.h"
#include "ui/text_wrap.h"

#include "i18n/lang.h"
#include "render/text_overlay.h"
#include "ui/ui_paint.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace hd2d {

namespace {

const TextColor kLabelColor{ 0.86f, 0.88f, 0.92f, 1.f };
const TextColor kValueColor{ 0.72f, 0.84f, 1.f, 1.f };
const TextColor kPickedColor{ 1.f, 0.95f, 0.55f, 1.f };
const TextColor kHintColor{ 0.60f, 0.64f, 0.70f, 1.f };
//! 見出しと「変えられない行」。**本文より暗く**（触れないことが色で分かるように）。
const TextColor kHeadingColor{ 0.70f, 0.78f, 0.62f, 1.f };
const TextColor kFixedColor{ 0.52f, 0.55f, 0.60f, 1.f };
//! 割り当てが無いマス。
const TextColor kEmptyColor{ 0.45f, 0.47f, 0.52f, 1.f };

std::string on_off(bool on)
{
    return on ? i18n::tr("hd2d.ui.feature-menu.on") : i18n::tr("hd2d.ui.feature-menu.off");
}

/*!
 * @brief 音量の段の値の欄（決めたこと 2026-08-21「0〜10」）。
 * @details 数字だけだと**上限が読めない**（7 が大きいのか小さいのか分からない）ので
 * 「7 / 10」と出す。**0 は無音**、入切が切のときは「効かない」ことをその場で言う
 * ——行を出し入れするとカーソルが飛ぶので、効かないことは値の欄で言う流儀
 * （`TronSky` と同じ）。
 */
std::string volume_value(int step, bool enabled)
{
    char buf[64]{};
    if (!enabled) {
        std::snprintf(buf, sizeof(buf), i18n::tr("hd2d.ui.feature-menu.d-d-off"), step,
            Hd2dSettings::kVolumeMax);
        return buf;
    }
    if (step <= 0) {
        return i18n::tr("hd2d.ui.feature-menu.0-silent");
    }
    std::snprintf(buf, sizeof(buf), "%d / %d", step, Hd2dSettings::kVolumeMax);
    return buf;
}

//! 割り当てが無いマスに出す印。**空白にしない**（描き漏れと区別がつかなくなる）。
const char *const kUnassigned = "—";

//! コアが `realtime` を申告したか（`feature_menu.h` の `set_core_supports_realtime`）。
bool g_core_supports_realtime = true;

//! コアが `audio` を申告したか（`feature_menu.h` の `set_core_supports_audio`）。
bool g_core_supports_audio = true;

//! HD2D の側でコマンドの画面・持ち物一覧を出さないか（`feature_menu.h` の `set_core_hides_commands`。DCSS の D32）。
bool g_core_hides_commands = false;

/*!
 * @brief その枠がいまの割り付けで**出るか**。
 * @details 出ない枠の行は値の欄でそう言う（行そのものは出し入れしない）。
 * `Tall` に右列が無いことも**ここ 1 か所**で見る——2 か所に書くと必ず食い違う。
 */
bool sub_panel_shown(const Hd2dSettings &settings, int slot, bool portrait)
{
    if (slot < kSubRightSlot) {
        return slot < settings.sub_split.bottom();
    }
    if (settings.layout_for(portrait) == LayoutMode::Tall) {
        return false;
    }
    return (slot - kSubRightSlot) < settings.sub_split.right();
}

//! その仕切りより後ろに残る 1/20 単位（値の欄に「残り」として出す）。
int sub_split_rest(const SubSplit &split, bool bottom, int at)
{
    const int *units = bottom ? split.bottom_w20 : split.right_h20;
    int used = 0;
    for (int i = 0; i <= at; ++i) {
        used += units[i];
    }
    return kSubBottomUnits - used;
}

} // namespace

void set_core_supports_realtime(bool on)
{
    g_core_supports_realtime = on;
}

bool core_supports_realtime()
{
    return g_core_supports_realtime;
}

void set_core_supports_audio(bool on)
{
    g_core_supports_audio = on;
}

bool core_supports_audio()
{
    return g_core_supports_audio;
}

void set_core_hides_commands(bool on)
{
    g_core_hides_commands = on;
}

bool core_hides_commands()
{
    return g_core_hides_commands;
}

namespace {

//! ミニマップを置く所の名前（決めたこと 2026-08-19 の 5 つ。並びは `MinimapCorner` と同じ）。
const char *minimap_corner_label(MinimapCorner corner)
{
    switch (corner) {
    case MinimapCorner::BottomRight:
        return i18n::tr("hd2d.ui.feature-menu.bottom-right");
    case MinimapCorner::TopLeft:
        return i18n::tr("hd2d.ui.feature-menu.top-left");
    case MinimapCorner::BottomLeft:
        return i18n::tr("hd2d.ui.feature-menu.bottom-left");
    case MinimapCorner::Centre:
        return i18n::tr("hd2d.ui.feature-menu.screen-centre");
    case MinimapCorner::TopRight:
    default:
        return i18n::tr("hd2d.ui.feature-menu.top-right");
    }
}

} // namespace

const char *FeatureMenu::page_label(Page page)
{
    switch (page) {
    case Page::Camera:
        return i18n::tr("hd2d.ui.feature-menu.camera");
    case Page::Screen:
        return i18n::tr("hd2d.ui.feature-menu.screen");
    case Page::SubPanels:
        return i18n::tr("hd2d.ui.feature-menu.sub-panels");
    case Page::Look:
        return i18n::tr("hd2d.ui.feature-menu.visuals");
    case Page::Effects:
        return i18n::tr("hd2d.ui.feature-menu.effects");
    case Page::FirstPerson:
        return i18n::tr("hd2d.ui.feature-menu.first-person");
    case Page::Commands:
        return i18n::tr("hd2d.ui.feature-menu.commands");
    case Page::Binds:
        return i18n::tr("hd2d.ui.feature-menu.key-bindings");
    case Page::VirtualPad:
        return i18n::tr("hd2d.app.hd2d-app.virtual-pad");
    case Page::VpadButtons:
        return i18n::tr("hd2d.ui.feature-menu.button-label-settings");
    case Page::Minimap:
        return i18n::tr("hd2d.ui.feature-menu.minimap");
    case Page::Sound:
        return i18n::tr("hd2d.ui.feature-menu.sound");
    case Page::Realtime:
        return i18n::tr("hd2d.ui.feature-menu.game-progression");
    case Page::Vr:
        return "VR";
    case Page::FpsPanels:
        return i18n::tr("hd2d.ui.feature-menu.panels-shown-in-first-person");
    case Page::Root:
    default:
        return i18n::tr("hd2d.ui.feature-menu.feature-menu");
    }
}

FeatureMenu::Page FeatureMenu::page_of(Item item)
{
    switch (item) {
    //! 入口に直接並ぶ値の項目（2026-08-19）。**節ではない。**
    case Item::Language:
        return Page::Root;
    case Item::ModernMode:
    case Item::ModernTheme:
    case Item::ModernFontSize:
    case Item::WindowMode:
    case Item::Layout:
    case Item::MainPanelItem:
    case Item::AsciiPanelSize:
    case Item::StatusColSideItem:
    case Item::Subs:
    case Item::MinimapEnter:
        return Page::Screen;
    case Item::MinimapCornerItem:
    case Item::MinimapSize:
    case Item::MinimapCellPx:
    case Item::MinimapOpacity:
    case Item::MinimapRelative:
        return Page::Minimap;
    case Item::SceneLookItem:
    case Item::TronSky:
    case Item::TronFaceGlyph:
    case Item::SurfaceWear:
    case Item::LeafDetail:
    case Item::EntityStyleItem:
    case Item::EntitySlabFacing:
    case Item::EntityGlyphSize:
    case Item::GlyphFallback:
    case Item::Backdrops:
    case Item::DungeonLight:
    case Item::EffectsEnter:
        return Page::Look;
    case Item::PostFog:
    case Item::PostDof:
    case Item::PostDofStrength:
    case Item::PostBloom:
    case Item::PostGrade:
    case Item::PostVignette:
    case Item::PostVignetteStrength:
    case Item::PostSepia:
    case Item::PostHdr:
    case Item::PostExposure:
        return Page::Effects;
    case Item::CameraPitch:
    case Item::CameraFov:
    case Item::CameraCellPx:
    case Item::MoveSmooth:
    case Item::Cutaway:
        return Page::Camera;
    case Item::FpsVerticalLook:
    case Item::FpsWallUpper:
    case Item::FpsCeiling:
    case Item::FpsSlabTurn:
    case Item::FpsStep:
    case Item::FpsCellM:
    case Item::FpsPanelsEnter:
        return Page::FirstPerson;
    case Item::VrPanelStatus:
    case Item::VrPanelSub1:
    case Item::VrPanelSub2:
    case Item::VrPanelSub3:
    case Item::VrPanelSub4:
    case Item::VrPanelSub5:
    case Item::VrPanelMinimap:
    case Item::VrPanelMessage:
    case Item::VrPanelPrompt:
    case Item::VrPanelBottom:
        return Page::FpsPanels;
    case Item::SubBottomCount:
    case Item::SubRightCount:
    case Item::SubBottomW1:
    case Item::SubBottomW2:
    case Item::SubBottomW3:
    case Item::SubRightH1:
    case Item::SubRightH2:
    case Item::SubKind1:
    case Item::SubKind2:
    case Item::SubKind3:
    case Item::SubKind4:
    case Item::SubKind5:
    case Item::SubKind6:
    case Item::SubKind7:
        return Page::SubPanels;
    case Item::BgmModeItem:
    case Item::MusicVolume:
    case Item::SoundEnabled:
    case Item::SoundVolume:
        return Page::Sound;
    case Item::VpadShow:
    case Item::VpadOpacity:
    case Item::VpadButtonsEnter:
    case Item::VpadLayoutEdit:
    case Item::VpadResetAll:
        return Page::VirtualPad;
    case Item::VpadBtnA:
    case Item::VpadBtnB:
    case Item::VpadBtnX:
    case Item::VpadBtnY:
    case Item::VpadBtnR1:
    case Item::VpadBtnR2:
    case Item::VpadBtnR3:
    case Item::VpadBtnL1:
    case Item::VpadBtnL2:
    case Item::VpadBtnL3:
    case Item::VpadBtnStart:
    case Item::VpadBtnSelect:
    case Item::VpadBtnRS:
    case Item::VpadBtnLS:
        return Page::VpadButtons;
    case Item::RealtimeEnabled:
    case Item::RealtimeSpeed:
    case Item::DamageFlash:
    case Item::DamageShake:
    case Item::FxScreenShake:
    case Item::RealtimePrompt:
    case Item::RealtimeSelfSpan:
        return Page::Realtime;
    case Item::VrEnter:
    case Item::VrTileM:
    case Item::VrTableHeight:
    case Item::VrPanelSize:
    case Item::VrPanelLift:
    case Item::VrPanelFollow:
    case Item::VrRenderScale:
        return Page::Vr;
    case Item::Count:
    default:
        /*
         * **書き忘れをどこかの節へ流さない。**前はここが `return Page::Vr;` で、
         * 組み替えのときにカメラの 5 つを書き落としたら**丸ごと VR の節に出た**
         * （2026-08-19）。画面は普通に描け、はみ出しの検査も通るので、
         * 人が全部の節を開くまで誰も気づけない。`Page::Count` はどの画面でもないので、
         * 落ちた項目は**どこにも並ばなくなり**、`structure_report()` が空の節として捕まえる。
         */
        return Page::Count;
    }
}

std::vector<FeatureMenu::Page> FeatureMenu::root_pages()
{
#if defined(__ANDROID__)
    /*
     * バーチャルパッドは**タッチ画面の実行体にだけ**並べる（`feature_menu.h` の
     * 「この実行体に無いものは入れない」）。Windows で試すときは cfg の `vpad_show=1`。
     */
    std::vector<Page> pages{ Page::Commands, Page::Screen, Page::Look, Page::Camera, Page::FirstPerson,
        Page::SubPanels, Page::Binds, Page::VirtualPad };
#else
    std::vector<Page> pages{ Page::Commands, Page::Screen, Page::Look, Page::Camera, Page::FirstPerson,
        Page::SubPanels, Page::Binds };
#endif
    /*
     * 音は**コアが申告したときだけ**（`realtime` と同じ扱い）。
     * **並べる場所はサブパネルの後ろ**——「どこに置くか・どう描くか・どこから見るか」を
     * 見終えたところで「どう聞こえるか」、その次が操作、という順にする。
     */
    if (core_supports_audio()) {
        //! 番号で入れない（節を 1 つ足した日に、まったく別の場所へ潜り込む）。
        pages.insert(std::find(pages.begin(), pages.end(), Page::Binds), Page::Sound);
    }
    /*
     * ゲーム進行（リアルタイム）は**コアが申告したときだけ**（設計 §6.3・
     * `set_core_supports_realtime()`）。申告しないコアで並べても、触った値の行き先が無い。
     */
    if (core_supports_realtime()) {
        pages.push_back(Page::Realtime);
    }
    /*
     * **VR の節はこの実行体に VR があるなら常に並べる**（決めたこと 2026-08-14
     * 「ゲーム内から VR モードを起動できないか？」）。始める口がここにある以上、
     * 平らな画面のときこそ入口が要る。電話の Android には VR が無いので並べないが、
     * **Quest（`HENGBAND_QUEST`）は Android でも並べる**——拡大率・視点高さ・字の
     * 大きさ・板の角度は実機で詰めるものなので、機の中から触れなければ話にならない
     *
     */
#if !defined(__ANDROID__) || defined(HENGBAND_QUEST)
    pages.push_back(Page::Vr);
#endif
    //! DCSS（D32）: コマンドは原作の画面で出すので、入口に「コマンド」の節を並べない。
    //! サブパネル（HD2D の画面の下・右の小さな欄）もこのコアの画面には描かれないので、節を並べない（D33）。
    if (g_core_hides_commands) {
        pages.erase(std::remove(pages.begin(), pages.end(), Page::Commands), pages.end());
        pages.erase(std::remove(pages.begin(), pages.end(), Page::SubPanels), pages.end());
    }
    return pages;
}

bool FeatureMenu::item_is_absent(Item item)
{
    if(item==Item::ModernMode)return true; // The unified design selector owns this choice.
#if defined(__ANDROID__)
    /*
     * **Android に窓モードは無い**（`hd2d_app.cpp` の `create_window`）。押しても
     * ステータスバーが地図の上へ戻るだけなので、項目ごと並べない
     * （「押しても何も良いことが起きない項目」を出さないのがこの画面の方針）。
     */
    if (item == Item::WindowMode) {
        return true;
    }
#endif
#if defined(HENGBAND_QUEST)
    /*
     * **Quest では「VR で遊ぶ／やめる」を出さない**。
     * 畳んだ先は機内の誰にも見えない 2D パネルで、**戻ってくる道が無い**
     * （メニューを開くにもコントローラの ☰ が要るが、その画面が見えない）。
     * ここは起動と同時に VR で入る実行体なので、そもそも切る意味も無い。
     */
    if (item == Item::VrEnter) {
        return true;
    }
#endif
#if defined(__ANDROID__) && !defined(HENGBAND_QUEST)
    /*
     * **電話の Android に VR は無い**（`root_pages()` が `Page::Vr` を並べないのと同じ条件）。
     * この 2 つは VR のときしか効かないので、そこでは「（VR のときだけ）」と出し続ける
     * だけの行になる——`WindowMode` と同じ理屈で、項目ごと並べない。
     */
    if ((item == Item::FpsCellM) || (item == Item::FpsPanelsEnter)) {
        return true;
    }
#endif
    (void)item;
    return false;
}

const std::vector<FeatureMenu::SubPageLink> &FeatureMenu::sub_page_links()
{
    static const std::vector<SubPageLink> links{
        { Page::Minimap, Page::Screen, Item::MinimapEnter },
        { Page::Effects, Page::Look, Item::EffectsEnter },
        { Page::FpsPanels, Page::FirstPerson, Item::FpsPanelsEnter },
        { Page::VpadButtons, Page::VirtualPad, Item::VpadButtonsEnter },
    };
    return links;
}

std::vector<FeatureMenu::Item> FeatureMenu::items_of(Page page)
{
    std::vector<Item> items;
    //! `Binds` は `Item` ではなく `BindRow` で並ぶ（行がコアから届くので数が動く）。
    //! `Commands` も同じ（`CommandRow`）。**どちらも `Item` を 1 つも持たない。**
    if ((page == Page::Binds) || (page == Page::Commands)) {
        return items;
    }
    for (int i = 0; i < static_cast<int>(Item::Count); ++i) {
        const auto item = static_cast<Item>(i);
        if (item_is_absent(item)) {
            continue;
        }
        if (page_of(item) == page) {
            items.push_back(item);
        }
    }
    return items;
}

std::vector<FeatureMenu::Item> FeatureMenu::page_items() const
{
    return items_of(this->page_);
}

std::vector<FeatureMenu::Item> FeatureMenu::root_items()
{
    return items_of(Page::Root);
}

int FeatureMenu::root_item_count()
{
    return static_cast<int>(root_items().size());
}

const char *FeatureMenu::item_label(Item item)
{
    switch (item) {
    case Item::CameraPitch:
        return i18n::tr("hd2d.app.hd2d-app.camera-pitch");
    case Item::CameraFov:
        return i18n::tr("hd2d.ui.feature-menu.field-of-view");
    case Item::CameraCellPx:
        return i18n::tr("hd2d.ui.feature-menu.size-of-one-cell");
    case Item::FpsVerticalLook:
        return i18n::tr("hd2d.ui.feature-menu.look-up-down");
    case Item::FpsWallUpper:
        return i18n::tr("hd2d.ui.feature-menu.wall-second-tier");
    case Item::FpsCeiling:
        return i18n::tr("hd2d.ui.feature-menu.ceiling");
    case Item::Language:
        return i18n::tr("hd2d.ui.feature-menu.language");
    case Item::MainPanelItem:
        return i18n::tr("hd2d.ui.feature-menu.main-panel");
    case Item::AsciiPanelSize:
        return i18n::tr("hd2d.ui.feature-menu.map-glyph-size");
    case Item::StatusColSideItem:
        return i18n::tr("hd2d.ui.feature-menu.status-col-side");
    case Item::ModernMode: return i18n::current()=="ja" ? "ウインドウ操作" : "Window controls";
    case Item::ModernFontSize: return i18n::current()=="ja"?"文字サイズ（メニュー・パネル）":"Text size (menus / panels)";
    case Item::ModernTheme: return i18n::current()=="ja" ? "ウィンドウのデザイン" : "Window design";
    case Item::Layout:
        return i18n::tr("hd2d.app.hd2d-app.layout");
    case Item::Subs:
        return i18n::tr("hd2d.ui.feature-menu.sub-panels");
    case Item::WindowMode:
        return i18n::tr("hd2d.ui.feature-menu.screen-mode");
    case Item::Backdrops:
        return i18n::tr("hd2d.ui.feature-menu.back-art");
    case Item::MoveSmooth:
        return i18n::tr("hd2d.ui.feature-menu.movement-smoothing");
    case Item::EntityStyleItem:
        return i18n::tr("hd2d.ui.feature-menu.entity-style");
    case Item::EntitySlabFacing:
        return i18n::tr("hd2d.ui.feature-menu.voxel-slab-facing");
    case Item::EntityGlyphSize:
        return i18n::tr("hd2d.ui.feature-menu.entity-glyph-size");
    case Item::GlyphFallback:
        return i18n::tr("hd2d.ui.feature-menu.glyph-fallback");
    case Item::SceneLookItem:
        return i18n::tr("hd2d.ui.feature-menu.scene-look");
    case Item::TronSky:
        return i18n::tr("hd2d.ui.feature-menu.tron-sky");
    case Item::TronFaceGlyph:
        return i18n::tr("hd2d.ui.feature-menu.tron-face-glyph");
    case Item::SurfaceWear:
        return i18n::tr("hd2d.ui.feature-menu.surface-wear");
    case Item::LeafDetail:
        return i18n::tr("hd2d.ui.feature-menu.leaf-detail");
    case Item::MinimapEnter:
        return i18n::tr("hd2d.ui.feature-menu.minimap-2");
    case Item::EffectsEnter:
        return i18n::tr("hd2d.ui.feature-menu.effects-2");
    case Item::MinimapCornerItem:
        return i18n::tr("hd2d.ui.feature-menu.minimap-position");
    case Item::MinimapSize:
        return i18n::tr("hd2d.ui.feature-menu.minimap-size");
    case Item::MinimapCellPx:
        return i18n::tr("hd2d.ui.feature-menu.minimap-scale");
    case Item::MinimapOpacity:
        return i18n::tr("hd2d.ui.feature-menu.minimap-opacity");
    case Item::MinimapRelative:
        return i18n::tr("hd2d.ui.feature-menu.minimap-heading");
    case Item::BgmModeItem:
        return i18n::tr("hd2d.ui.feature-menu.bgm");
    case Item::MusicVolume:
        return i18n::tr("hd2d.ui.feature-menu.music-volume");
    case Item::SoundEnabled:
        return i18n::tr("hd2d.ui.feature-menu.sound-effects");
    case Item::SoundVolume:
        return i18n::tr("hd2d.ui.feature-menu.sound-effects-volume");
    case Item::RealtimeEnabled:
        return i18n::tr("hd2d.ui.feature-menu.real-time-progression");
    case Item::RealtimeSpeed:
        return i18n::tr("hd2d.ui.feature-menu.speed-of-progression");
    case Item::DamageFlash:
        return i18n::tr("hd2d.ui.feature-menu.damage-flash");
    case Item::DamageShake:
        return i18n::tr("hd2d.ui.feature-menu.damage-shake");
    case Item::FxScreenShake:
        return i18n::tr("hd2d.ui.feature-menu.screen-shake-from-attack-effects");
    case Item::RealtimePrompt:
        return i18n::tr("hd2d.ui.feature-menu.behind-the-pop-up");
    case Item::RealtimeSelfSpan:
        return i18n::tr("hd2d.ui.feature-menu.cap-on-your-own-speed");
    case Item::VrEnter:
        return i18n::tr("hd2d.ui.feature-menu.play-in-vr");
    case Item::VrTileM:
        return i18n::tr("hd2d.ui.feature-menu.diorama-scale");
    case Item::VrTableHeight:
        return i18n::tr("hd2d.ui.feature-menu.diorama-viewing-height");
    case Item::VrPanelSize:
        return i18n::tr("hd2d.ui.feature-menu.text-size-on-the-panel");
    case Item::VrPanelLift:
        return i18n::tr("hd2d.ui.feature-menu.panel-elevation-angle");
    case Item::VrPanelFollow:
        return i18n::tr("hd2d.ui.feature-menu.angle-before-the-panel-follows");
    case Item::VrRenderScale:
        return i18n::tr("hd2d.ui.feature-menu.render-resolution");
    case Item::FpsCellM:
        return i18n::tr("hd2d.ui.feature-menu.one-cell-in-metres");
    case Item::FpsStep:
        return i18n::tr("hd2d.ui.feature-menu.walking-pace");
    case Item::FpsPanelsEnter:
        return i18n::tr("hd2d.ui.feature-menu.panels-shown");
    case Item::SubBottomCount:
        return i18n::tr("hd2d.ui.feature-menu.bottom-row-panels");
    case Item::SubRightCount:
        return i18n::tr("hd2d.ui.feature-menu.right-column-panels");
    case Item::SubBottomW1:
        return i18n::tr("hd2d.ui.feature-menu.width-of-bottom-1");
    case Item::SubBottomW2:
        return i18n::tr("hd2d.ui.feature-menu.width-of-bottom-2");
    case Item::SubBottomW3:
        return i18n::tr("hd2d.ui.feature-menu.width-of-bottom-3");
    case Item::SubRightH1:
        return i18n::tr("hd2d.ui.feature-menu.height-of-right-1");
    case Item::SubRightH2:
        return i18n::tr("hd2d.ui.feature-menu.height-of-right-2");
    case Item::SubKind1:
        return i18n::tr("hd2d.ui.feature-menu.bottom-1");
    case Item::SubKind2:
        return i18n::tr("hd2d.ui.feature-menu.bottom-2");
    case Item::SubKind3:
        return i18n::tr("hd2d.ui.feature-menu.bottom-3");
    case Item::SubKind4:
        return i18n::tr("hd2d.ui.feature-menu.bottom-4");
    case Item::SubKind5:
        return i18n::tr("hd2d.ui.feature-menu.right-1");
    case Item::SubKind6:
        return i18n::tr("hd2d.ui.feature-menu.right-2");
    case Item::SubKind7:
        return i18n::tr("hd2d.ui.feature-menu.right-3");
    case Item::PostFog:
        return i18n::tr("hd2d.ui.feature-menu.fog");
    case Item::PostDof:
        return i18n::tr("hd2d.ui.feature-menu.depth-of-field");
    case Item::PostBloom:
        return i18n::tr("hd2d.ui.feature-menu.bloom");
    case Item::PostGrade:
        return i18n::tr("hd2d.ui.feature-menu.colour-grading-lut");
    case Item::PostVignette:
        return i18n::tr("hd2d.ui.feature-menu.vignette");
    case Item::PostVignetteStrength:
        return i18n::tr("hd2d.ui.feature-menu.vignette-strength");
    case Item::PostSepia:
        return i18n::tr("hd2d.ui.feature-menu.sepia");
    case Item::PostHdr:
        return i18n::tr("hd2d.ui.feature-menu.hdr-look");
    case Item::PostExposure:
        return i18n::tr("hd2d.ui.feature-menu.exposure");
    case Item::PostDofStrength:
        return i18n::tr("hd2d.ui.feature-menu.blur-strength");
    case Item::DungeonLight:
        return i18n::tr("hd2d.ui.feature-menu.ambient-brightness");
    case Item::FpsSlabTurn:
        return i18n::tr("hd2d.ui.feature-menu.slab-facing");
    case Item::Cutaway:
        return i18n::tr("hd2d.ui.feature-menu.cutaway");
    case Item::VpadShow:
        return i18n::tr("hd2d.ui.feature-menu.show-the-virtual-pad");
    case Item::VpadOpacity:
        return i18n::tr("hd2d.ui.feature-menu.opacity");
    case Item::VpadButtonsEnter:
        return i18n::tr("hd2d.ui.feature-menu.button-label-settings-2");
    case Item::VpadLayoutEdit:
        return i18n::tr("hd2d.ui.feature-menu.button-layout");
    case Item::VpadResetAll:
        return i18n::tr("hd2d.ui.feature-menu.restore-the-defaults");
    default:
        //! ボタンの入切はコントロール名そのもの（`A` `R1` `START` …）。
        if (vpad_button_index(item) >= 0) {
            return vpad_control_name(static_cast<VpadControl>(vpad_button_index(item)));
        }
        //! VR の部位は `xr::panel_slot_label()` が正本（綴りを 2 か所に置かない）。
        if (vr_panel_index(item) >= 0) {
            return xr::panel_slot_label(static_cast<xr::PanelSlot>(vr_panel_index(item)));
        }
        return "";
    }
}

int FeatureMenu::vr_panel_index(Item item)
{
    const int at = static_cast<int>(item) - static_cast<int>(Item::VrPanelStatus);
    return ((at >= 0) && (at < static_cast<int>(xr::PanelSlot::Count))) ? at : -1;
}

int FeatureMenu::sub_kind_index(Item item)
{
    const int at = static_cast<int>(item) - static_cast<int>(Item::SubKind1);
    return ((at >= 0) && (at < kUiSubPanels)) ? at : -1;
}

int FeatureMenu::sub_bottom_divider_index(Item item)
{
    const int at = static_cast<int>(item) - static_cast<int>(Item::SubBottomW1);
    return ((at >= 0) && (at < (kSubBottomMax - 1))) ? at : -1;
}

int FeatureMenu::sub_right_divider_index(Item item)
{
    const int at = static_cast<int>(item) - static_cast<int>(Item::SubRightH1);
    return ((at >= 0) && (at < (kSubRightMax - 1))) ? at : -1;
}

int FeatureMenu::vpad_button_index(Item item)
{
    const int at = static_cast<int>(item) - static_cast<int>(Item::VpadBtnA);
    return ((at >= 0) && (at < kVpadControlCount)) ? at : -1;
}

std::vector<FeatureMenu::BindRow> FeatureMenu::bind_rows() const
{
    std::vector<BindRow> rows;
    rows.reserve(this->pad_commands_.size() + 16U);

    const auto heading = [&rows](const char *text) {
        BindRow row;
        row.label = text;
        rows.push_back(std::move(row));
    };
    const auto fixed = [&rows](const char *label, const char *keyboard, const char *pad) {
        BindRow row;
        row.label = label;
        row.fixed_keyboard = keyboard;
        row.fixed_pad = pad;
        row.fixed = true;
        rows.push_back(std::move(row));
    };

    //! コアごとの割り当て（DCSS。D33）: 変えられない行・画面まわりの操作・コアのコマンドの行を、入口の向こうから並べる。HD2D のパッドの割り当て・マクロの案内は出さない。
    if (this->ext_binds_ != nullptr) {
        heading(i18n::tr("hd2d.ui.feature-menu.system-cannot-be-changed"));
        for (const ExternalFixedRow &f : this->ext_binds_->fixed_rows()) {
            BindRow row;
            row.label = f.label;
            row.fixed_keyboard = f.keyboard;
            row.fixed_pad = f.pad;
            row.fixed = true;
            rows.push_back(std::move(row));
        }
        heading(i18n::tr("hd2d.ui.feature-menu.around-this-screen"));
        for (const int action : ui_action_ids()) {
            if (this->ext_binds_->hides_ui_action(action)) {
                continue;
            }
            BindRow row;
            row.action = action;
            row.label = ui_action_label(action);
            row.no_pad = !this->ext_binds_->ui_has_pad(action);
            rows.push_back(std::move(row));
        }
        for (const ExternalBindRow &er : this->ext_binds_->rows()) {
            BindRow row;
            row.action = er.id;
            row.label = er.label;
            row.external = (er.id != 0);
            row.open_row = er.open_row;
            row.no_key = er.no_key;
            row.no_pad = er.no_pad;
            rows.push_back(std::move(row));
        }
        return rows;
    }

    /*
     * **システムが押さえているものを最初に出す。**割り当てられないことを画面で言っておかないと、
     * 「決定を別のキーにしたいのにカーソルが止まらない」を不具合だと思われる。
     * 綴りは `ui/key_binds.h` の表と同じ（あちらが判定の正本）。
     */
    heading(i18n::tr("hd2d.ui.feature-menu.system-cannot-be-changed"));
    fixed(i18n::tr("hd2d.ui.feature-menu.choose-advance"), i18n::tr("hd2d.ui.feature-menu.arrow-keys-numpad"), i18n::tr("hd2d.ui.feature-menu.d-pad-left-stick"));
    fixed(i18n::tr("hd2d.ui.feature-menu.confirm"), "Enter", "A");
    fixed(i18n::tr("hd2d.ui.feature-menu.cancel"), "ESC", "B");
    /*
     * **軸はこの表では割り当てられない。**表の値は「押したボタン」（`PadInput`）で、
     * スティックの傾きはそこに無い。並べるのは、割り当てられなくても
     * 「何がその操作をするのか」は知りたいからである（決めたこと 2026-08-11 の
     * 「見下ろし角の調整／キーボード 割り当てなし／コントローラー 右スティック上下」）。
     */
    fixed(i18n::tr("hd2d.ui.feature-menu.adjust-the-camera-pitch"), kUnassigned, i18n::tr("hd2d.ui.feature-menu.right-stick-up-and-down"));
    fixed(i18n::tr("hd2d.ui.feature-menu.first-person-look"), i18n::tr("hd2d.ui.feature-menu.mouse"), i18n::tr("hd2d.ui.feature-menu.right-stick"));

    heading(i18n::tr("hd2d.ui.feature-menu.around-this-screen"));
    for (const int action : ui_action_ids()) {
        if (g_core_hides_commands && (action == kActionCommandMenu)) {
            continue; //!< DCSS（D32）: コマンドメニューは無いので、割り当ての行も出さない
        }
        BindRow row;
        row.action = action;
        row.label = ui_action_label(action);
        rows.push_back(std::move(row));
    }

    /*
     * コアのコマンド。**分類（`group_utf8`）ごとに見出しを挟む。**60 を超える行を
     * 素で並べるとスクロールしても現在地が分からない。順序はコアが送ってきたままで、
     * 並べ替えはしない（コアの `menu_info` の並びが遊ぶ人の記憶と一致している）。
     */
    std::string last_group;
    for (const auto &command : this->pad_commands_) {
        if (command.group_utf8 != last_group) {
            last_group = command.group_utf8;
            heading(("― " + (last_group.empty() ? std::string(i18n::tr("hd2d.ui.feature-menu.other")) : last_group)).c_str());
        }
        BindRow row;
        row.action = command.id;
        //! **出せないものは出せないと書く**（黙って何も起きないのがいちばん困る）。
        row.label = command.keys.empty() ? (command.label_utf8 + i18n::tr("hd2d.ui.feature-menu.not-available-on-the-current-keyboard"))
                                         : command.label_utf8;
        rows.push_back(std::move(row));
    }
    if (this->pad_commands_.empty() && !g_core_hides_commands) {
        //! 握手の直後はコアの一覧がまだ来ていない。**空の表を黙って出さない。**
        heading(i18n::tr("hd2d.ui.feature-menu.game-controls-the-list-from-the-core"));
    }

    /*
     * **割り当ての無いボタンはマクロのトリガーになる**。
     * ここに出さないと、この道は画面のどこにも現れず**誰にも見つけられない**。
     * 空いているボタンとその綴りを並べる——`@` の「トリガーキー:」で押したときに
     * 出るのと同じ綴りなので、見比べれば迷わない。
     */
    heading(i18n::tr("hd2d.ui.feature-menu.macros"));
    heading(i18n::tr("hd2d.ui.feature-menu.any-button-with-no-binding-becomes-a"));
    heading(i18n::tr("hd2d.ui.feature-menu.press-that-button-at-trigger-key-under"));
    return rows;
}

std::string FeatureMenu::bind_cell_value(const BindRow &row, int col, const Hd2dSettings &settings) const
{
    if (row.fixed) {
        return (col == 0) ? row.fixed_keyboard : row.fixed_pad;
    }
    if (row.action == kActionNone) {
        return std::string();
    }
    if (this->ext_binds_ != nullptr) {
        if (row.external) {
            return this->ext_binds_->cell(row.action, col);
        }
        if (col == 1) {
            //! 画面まわりの操作のコントローラーの欄は、コアの割り当てから出す（HD2D のパッドの割り当ては効かない）。
            return this->ext_binds_->ui_has_pad(row.action) ? this->ext_binds_->ui_pad_cell(row.action) : std::string();
        }
    }
    if (col == 0) {
        const KeyBindEntry entry = settings.key_binds.find(row.action);
        if (entry.keycode != 0) {
            return key_display_name(entry.keycode, entry.mods);
        }
        /*
         * 割り当てが無い。コアのコマンドは**コア自身のキーで出せる**ので、そちらを出す。
         * 「未割り当て」とだけ書くと、`拾う` が `g` で出ることを知っている人が
         * 「壊れた」と読む。UI 自身の操作にはコアの既定が無いので `—`。
         */
        for (const auto &command : this->pad_commands_) {
            if (command.id != row.action) {
                continue;
            }
            const std::string core = core_key_sequence_name(command.keys);
            return core.empty() ? std::string(kUnassigned) : (core + i18n::tr("hd2d.ui.feature-menu.default"));
        }
        return kUnassigned;
    }
    const PadPress bound = settings.pad.input_for(row.action);
    if (bound.input != PadInput::Count) {
        //! 同時押しは `LB＋X` と出す（綴りは帯と同じもの。2 か所に書かない）。
        return pad_chord_display_name(bound.input, bound.mods);
    }
    if (row.action == kActionFeatureMenu) {
        //! `Back` は固定（`pad_input_is_fixed`）。**キーボードを全部消しても戻れる逃げ道**である。
        return i18n::tr("hd2d.ui.feature-menu.back-fixed");
    }
    return kUnassigned;
}

// 単純な ON/OFF 項目の設定先。表示と変更は必ず同じ定義を使う。
bool Hd2dSettings::*FeatureMenu::toggle_setting(Item item)
{
    switch (item) {
    case Item::Backdrops: return &Hd2dSettings::backdrops;
    case Item::SoundEnabled: return &Hd2dSettings::sound_enabled;
    case Item::RealtimeEnabled: return &Hd2dSettings::realtime_enabled;
    case Item::DamageFlash: return &Hd2dSettings::damage_flash;
    case Item::DamageShake: return &Hd2dSettings::damage_shake;
    case Item::FxScreenShake: return &Hd2dSettings::fx_screen_shake;
    default: return nullptr;
    }
}

std::string FeatureMenu::item_value(Item item, const Hd2dSettings &settings) const
{
    if (const auto field = toggle_setting(item)) {
        return on_off(settings.*field);
    }

    char buf[64]{};
    const int kind_index = sub_kind_index(item);
    if (kind_index >= 0) {
        /*
         * **出ていない枠はそう言う**（`TronSky` と同じ流儀。2026-08-19）。行は枚数で
         * 出し入れしない——枚数を変えた瞬間に下の行がずれ、カーソルの居場所が飛ぶ。
         */
        if (!sub_panel_shown(settings, kind_index, this->portrait_)) {
            return i18n::tr("hd2d.ui.feature-menu.not-shown-with-this-split");
        }
        const int flag = settings.sub_panel_kind[kind_index];
        /*
         * コアから種類一覧がまだ来ていない場合でも「種類 6」のような内部番号だけを
         * 遊ぶ人へ見せない。変愚系の既知番号は実際の内容名で出す。
         * -1 は UI 側の固定内容なので、場所ごとの実際の中身を表示する。
         */
        if (flag < 0) {
            switch (kind_index) {
            case 0: return i18n::tr("hd2d.ui.feature-menu.panel-content-messages");
            case 1: return i18n::tr("hd2d.ui.feature-menu.panel-content-equipment");
            case 2: return i18n::tr("hd2d.ui.feature-menu.panel-content-enemies");
            case 3: return i18n::tr("hd2d.ui.feature-menu.panel-content-character");
            case 4: return i18n::tr("hd2d.ui.feature-menu.panel-content-inventory");
            default: return i18n::tr("hd2d.ui.feature-menu.ui-default");
            }
        }
        for (const auto &choice : this->kind_choices_) {
            if (choice.flag == flag) {
                return choice.label_utf8;
            }
        }
        switch (flag) {
        case 0: return i18n::tr("hd2d.ui.feature-menu.panel-content-inventory");
        case 1: return i18n::tr("hd2d.ui.feature-menu.panel-content-equipment");
        case 2: return i18n::tr("hd2d.ui.feature-menu.panel-content-magic");
        case 3: return i18n::tr("hd2d.ui.feature-menu.panel-content-status");
        case 4: return i18n::tr("hd2d.ui.feature-menu.panel-content-enemies");
        case 6: return i18n::tr("hd2d.ui.feature-menu.panel-content-messages");
        case 7: return i18n::tr("hd2d.ui.feature-menu.panel-content-surroundings");
        case 8: return i18n::tr("hd2d.ui.feature-menu.panel-content-monster-memory");
        default: return i18n::tr("hd2d.ui.feature-menu.unknown-panel-content");
        }
    }
    /*
     * 仕切り。**「残り」も出す**——1/20 単位の数字だけでは、動かした結果どこが
     * 広がったのかが読めない（下段の中央を触ると右が縮む）。
     */
    if (const int bottom_at = sub_bottom_divider_index(item); bottom_at >= 0) {
        if (bottom_at >= (settings.sub_split.bottom() - 1)) {
            return i18n::tr("hd2d.ui.feature-menu.only-with-more-bottom-panels");
        }
        std::snprintf(buf, sizeof(buf), i18n::tr("hd2d.ui.feature-menu.d-d-rest-d-d"),
            settings.sub_split.bottom_w20[bottom_at], kSubBottomUnits,
            sub_split_rest(settings.sub_split, true, bottom_at), kSubBottomUnits);
        return buf;
    }
    if (const int right_at = sub_right_divider_index(item); right_at >= 0) {
        if (settings.layout_for(this->portrait_) == LayoutMode::Tall) {
            return i18n::tr("hd2d.ui.feature-menu.no-right-column-in-this-layout");
        }
        if (right_at >= (settings.sub_split.right() - 1)) {
            return i18n::tr("hd2d.ui.feature-menu.only-with-more-right-panels");
        }
        std::snprintf(buf, sizeof(buf), i18n::tr("hd2d.ui.feature-menu.d-d-rest-d-d"),
            settings.sub_split.right_h20[right_at], kSubRightUnits,
            sub_split_rest(settings.sub_split, false, right_at), kSubRightUnits);
        return buf;
    }
    switch (item) {
    case Item::CameraPitch:
        std::snprintf(buf, sizeof(buf), i18n::tr("hd2d.ui.feature-menu.0f-degrees"), static_cast<double>(settings.camera_pitch_deg));
        return buf;
    case Item::CameraFov:
        std::snprintf(buf, sizeof(buf), i18n::tr("hd2d.ui.feature-menu.0f-degrees"), static_cast<double>(settings.camera_fov_deg));
        return buf;
    case Item::CameraCellPx:
        std::snprintf(buf, sizeof(buf), "%.0f px", static_cast<double>(settings.camera_cell_px));
        return buf;
    case Item::FpsVerticalLook:
        //! **何が変わるかを言う**（「入／切」だけだと一人称の話だと読めない）。
        return settings.fps_vertical_look ? i18n::tr("hd2d.ui.feature-menu.on-pitch-up-and-down-as-well") : i18n::tr("hd2d.ui.feature-menu.off-horizontal-only");
    case Item::FpsWallUpper:
        //! こちらも同じ。**ダンジョンだけ**の話であることも添える。
        return settings.fps_wall_upper ? i18n::tr("hd2d.ui.feature-menu.on-dungeon-walls-are-two-tiers") : i18n::tr("hd2d.ui.feature-menu.off-the-usual-single-tier");
    case Item::FpsCeiling:
        return settings.fps_ceiling ? i18n::tr("hd2d.ui.feature-menu.on-the-dungeon-has-a-ceiling") : i18n::tr("hd2d.ui.feature-menu.off-no-ceiling");
    case Item::ModernMode: return settings.modern_ui ? (i18n::current()=="ja"?"新UI（試作）":"Modern (preview)") : (i18n::current()=="ja"?"従来UI":"Original");
    case Item::ModernFontSize: {
        constexpr const char *percent[]={"100","112.5","125","150","175"};const int level=std::clamp(settings.modern_font_level,0,4);
        return std::to_string(level+1)+" / 5 ("+std::string(percent[level])+"%)";
    }
    case Item::ModernTheme:
        if(!settings.modern_ui)return i18n::current()=="ja"?"オリジナル":"Original";
        return settings.modern_theme==1?"B":(settings.modern_theme==2?"C":"A");
    case Item::Layout: {
        /*
         * **いま向いている側の作りを出す**（縦横で別に持つ。決めたこと 2026-08-12）。
         * どちらを触っているかを添えないと、回した先で「さっき選んだ物と違う」に見える。
         */
        const char *name = i18n::tr("hd2d.ui.feature-menu.hybrid");
        switch (settings.layout_for(this->portrait_)) {
        case LayoutMode::Full:
            name = i18n::tr("hd2d.ui.feature-menu.full-screen-3d");
            break;
        case LayoutMode::Split:
            name = i18n::tr("hd2d.ui.feature-menu.split-screen");
            break;
        case LayoutMode::Tall:
            name = i18n::tr("hd2d.ui.feature-menu.portrait-split");
            break;
        default:
            break;
        }
        std::snprintf(buf, sizeof(buf), i18n::tr("hd2d.ui.feature-menu.s-s"), name, this->portrait_ ? i18n::tr("hd2d.ui.feature-menu.portrait") : i18n::tr("hd2d.ui.feature-menu.landscape"));
        return buf;
    }
    case Item::Language:
        /*
         * **その言語自身での呼び名**を出す（"English" / "日本語"）。日本語で
         * 「英語」と書くと、英語しか読めない人が自分の言語を見つけられない。
         */
        return i18n::current_endonym();
    case Item::MainPanelItem:
        /*
         * **何が変わるかを書く。**「オリジナル」だけだと画面全部が変わると読めてしまう
         * ——変わるのはメインパネルの中身だけで、周りの UI はそのまま効く（§6）。
         */
        return (settings.main_panel == Hd2dSettings::MainPanel::Ascii)
            ? i18n::tr("hd2d.ui.feature-menu.2d-ascii-map")
            : i18n::tr("hd2d.ui.feature-menu.3d-voxels");
    case Item::AsciiPanelSize: {
        if(settings.modern_ui)return i18n::current()=="ja"?"共通の文字サイズに連動":"Uses shared text size";
        if (settings.main_panel != Hd2dSettings::MainPanel::Ascii) {
            return i18n::tr("hd2d.ui.feature-menu.2d-ascii-only");
        }
        /*
         * **マスの数を添える。**回す人が知りたいのは px ではなく「何マス見えるか」である
         * （本家の地図領域は 66×22 マスほど。そこに寄せたい人がいる）。
         */
        char grid[48]{};
        if ((this->ascii_cols_ > 0) && (this->ascii_rows_ > 0)) {
            std::snprintf(grid, sizeof(grid), i18n::tr("hd2d.ui.feature-menu.d-d-cells"), this->ascii_cols_,
                this->ascii_rows_);
        }
        if (settings.ascii_panel_px <= 0) {
            std::snprintf(buf, sizeof(buf), "%s%s", i18n::tr("hd2d.ui.feature-menu.same-as-the-ui"), grid);
        } else {
            std::snprintf(buf, sizeof(buf), "%dpx%s", settings.ascii_panel_px, grid);
        }
        return buf;
    }
    case Item::StatusColSideItem:
        /*
         * **自動のときはどちらに出るかまでは書かない**——答えはコアの申告で、
         * この画面は settings しか見ていない（フレームを持ち込まない）。
         * `自動` の意味は「コアの申告に従う」で、それ自体が値の説明である。
         */
        switch (settings.status_col_side) {
        case Hd2dSettings::StatusColSide::Left:
            return i18n::tr("hd2d.ui.feature-menu.status-col-left");
        case Hd2dSettings::StatusColSide::Right:
            return i18n::tr("hd2d.ui.feature-menu.status-col-right");
        case Hd2dSettings::StatusColSide::Auto:
        default:
            return i18n::tr("hd2d.ui.feature-menu.status-col-auto");
        }
    case Item::Subs:
        return settings.subs_open ? i18n::tr("hd2d.ui.feature-menu.open") : i18n::tr("hd2d.ui.feature-menu.closed");
    case Item::WindowMode:
        return settings.windowed ? i18n::tr("hd2d.ui.feature-menu.windowed") : i18n::tr("hd2d.ui.feature-menu.fullscreen");
    case Item::MoveSmooth:
        //! 何がなめらかかを言う（「入／切」だと**何が変わるか**が読めない）。
        return (settings.move_smoothing == MoveSmoothing::All) ? i18n::tr("hd2d.ui.feature-menu.everything") : i18n::tr("hd2d.ui.feature-menu.entities-only");
    case Item::EntityStyleItem:
        /*
         * **入／切で出す**（決めたこと 2026-08-19「アスキーキャラクター ON/OFF」）。
         * 前は「タイルの板／アスキー文字」と両方の名前を出していたが、行の名前が
         * `実体の表現` だったので、どちらが既定でどちらが切なのかが読めなかった。
         * 名前を `アスキーキャラクター` にしたぶん、値は入切だけでよい。
         */
        return on_off(settings.entity_style == Hd2dSettings::EntityStyle::Ascii);
    case Item::EntitySlabFacing:
        if (settings.entity_style != Hd2dSettings::EntityStyle::Slab) {
            return i18n::tr("hd2d.ui.feature-menu.slab-only");
        }
        return settings.entity_slab_screen_aligned
            ? i18n::tr("hd2d.ui.feature-menu.screen-aligned")
            : i18n::tr("hd2d.ui.feature-menu.world-oriented");
    case Item::EntityGlyphSize:
        /*
         * **板のときは効かないことを言う。**回しても絵が変わらない項目を黙って出すと、
         * 「壊れている」と読まれる（この画面の方針）。
         */
        if (settings.entity_style != Hd2dSettings::EntityStyle::Ascii) {
            std::snprintf(buf, sizeof(buf), i18n::tr("hd2d.ui.feature-menu.d-ascii-only"),
                settings.entity_glyph_pct);
            return buf;
        }
        std::snprintf(buf, sizeof(buf), "%d%%", settings.entity_glyph_pct);
        return buf;
    case Item::GlyphFallback:
        /*
         * アスキー実体のときは効かない（あちらは最初から全部が字）。
         * 回しても絵が変わらない項目にはそう言う（この画面の方針）。
         */
        if (settings.entity_style == Hd2dSettings::EntityStyle::Ascii) {
            return i18n::tr("hd2d.ui.feature-menu.slab-only");
        }
        return on_off(settings.entity_glyph_fallback);
    case Item::SceneLookItem:
        /*
         * **何が変わるかを書く。**「TRON」だけだと何が起きるか読めないので、
         * 見えの言葉で出す（`MainPanelItem` と同じ流儀）。
         */
        return (settings.scene_look == SceneLookKind::Tron)
            ? i18n::tr("hd2d.ui.feature-menu.tron-neon-lines")
            : i18n::tr("hd2d.ui.feature-menu.standard-look");
    case Item::TronSky:
        /*
         * **標準の画調では効かないことを言う。**回しても絵が変わらない項目を黙って出すと
         * 「壊れている」と読まれる（この画面の方針。`EntityGlyphSize` と同じ）。
         */
        if (settings.scene_look != SceneLookKind::Tron) {
            return i18n::tr("hd2d.ui.feature-menu.tron-only");
        }
        return settings.tron_sky ? i18n::tr("hd2d.ui.feature-menu.tron-sky-daylight")
                                 : i18n::tr("hd2d.ui.feature-menu.tron-sky-black");
    case Item::TronFaceGlyph:
        if (settings.scene_look != SceneLookKind::Tron) {
            return i18n::tr("hd2d.ui.feature-menu.tron-only");
        }
        return on_off(settings.tron_face_glyph);
    case Item::SurfaceWear:
        //! 0 は「掛けない」と言う（`0%` だと切ってあるのか壊れているのか読めない）。
        if (settings.wear_pct <= 0) {
            return i18n::tr("hd2d.ui.feature-menu.off");
        }
        std::snprintf(buf, sizeof(buf), "%d%%", settings.wear_pct);
        return buf;
    case Item::LeafDetail:
        if (settings.leaf_pct <= 0) {
            return i18n::tr("hd2d.ui.feature-menu.off");
        }
        std::snprintf(buf, sizeof(buf), "%d%%", settings.leaf_pct);
        return buf;
    case Item::MinimapEnter:
        /*
         * **小節への入口は「詳細を開く」で揃える**。
         * ここは `右上` のように**いまの状態**を出していたが、値の欄が普通の設定と
         * 同じ顔をするので、**下に画面があることが読めなかった**。状態は入った先で見える。
         */
        return i18n::tr("hd2d.ui.feature-menu.open-details");
    case Item::MinimapCornerItem:
        return minimap_corner_label(settings.minimap_corner);
    case Item::MinimapSize:
        std::snprintf(buf, sizeof(buf), "%d%%", settings.minimap_size_pct);
        return buf;
    case Item::MinimapCellPx:
        //! 縮尺は「1 マス何画素か」で出す（%だと何に対する%か読めない）。
        std::snprintf(buf, sizeof(buf), i18n::tr("hd2d.ui.feature-menu.d-px-per-cell"), settings.minimap_cell_px);
        return buf;
    case Item::MinimapOpacity:
        std::snprintf(buf, sizeof(buf), "%d%%", settings.minimap_opacity_pct);
        return buf;
    case Item::MinimapRelative:
        /*
         * **何が起きるかを書く。**「相対」だけだと何に対する相対か読めないので、
         * 地図が回るのかどうかを添える（決めたこと 2026-08-19「地図ごと回すかどうか」）。
         */
        return settings.minimap_relative ? i18n::tr("hd2d.ui.feature-menu.relative-map-turns")
                                         : i18n::tr("hd2d.ui.feature-menu.absolute-north-up");
    case Item::BgmModeItem:
        /*
         * **誰が鳴らすかまでは書かない。**遊ぶ人が選ぶのは「何が流れるか」であって、
         * コア側か画面側かは実装の都合である。
         */
        switch (settings.bgm_mode) {
        case Hd2dSettings::BgmMode::Ambience:
            return i18n::tr("hd2d.ui.feature-menu.ambience");
        case Hd2dSettings::BgmMode::Off:
            return i18n::tr("hd2d.ui.feature-menu.off");
        case Hd2dSettings::BgmMode::Music:
        default:
            return i18n::tr("hd2d.ui.feature-menu.music");
        }
    case Item::MusicVolume:
        //! 音量は音楽にも環境音にも同じ段で効く。「切」のときだけ効かないと言う。
        return volume_value(settings.music_volume, settings.bgm_mode != Hd2dSettings::BgmMode::Off);
    case Item::SoundVolume:
        return volume_value(settings.sound_volume, settings.sound_enabled);
    case Item::RealtimeSpeed:
        //! 通常速度のときの 1 行動あたり。加速すればこれより速く動ける。
        std::snprintf(buf, sizeof(buf), i18n::tr("hd2d.ui.feature-menu.2f-s-per-action"),
            static_cast<double>(Hd2dSettings::realtime_seconds_per_turn(settings.realtime_speed_index)));
        return buf;
    case Item::RealtimePrompt:
        //! 「入／切」だと**何が入るのか**が読めないので、起きることを書く。
        return settings.realtime_prompt_live ? i18n::tr("hd2d.ui.feature-menu.run-you-get-hit-while-choosing") : i18n::tr("hd2d.ui.feature-menu.pause");
    case Item::RealtimeSelfSpan:
        return Hd2dSettings::realtime_self_span_label(settings.realtime_self_span_index);
    /* ---- VR（§22）。**単位まで書く**——cm と度と ms が並ぶので、数字だけでは読めない ---- */
    case Item::VrEnter:
        //! **いまの状態を書く**（「入／切」だと押した後どうなるか分からない）。
        return this->vr_available_ ? i18n::tr("hd2d.ui.feature-menu.playing-confirm-to-stop") : i18n::tr("hd2d.ui.feature-menu.confirm-to-start");
    case Item::VrTileM: {
        char buf[48]{};
        std::snprintf(buf, sizeof(buf), i18n::tr("hd2d.ui.feature-menu.one-cell-is-1fcm"), static_cast<double>(settings.vr_tile_m) * 100.0);
        return buf;
    }
    case Item::VrTableHeight: {
        if (settings.vr_table_height_m <= 0.f) {
            return i18n::tr("hd2d.ui.feature-menu.automatic-from-the-head-height");
        }
        char buf[48]{};
        std::snprintf(buf, sizeof(buf), "%.2fm", static_cast<double>(settings.vr_table_height_m));
        return buf;
    }
    case Item::VrPanelSize: {
        char buf[48]{};
        std::snprintf(buf, sizeof(buf), i18n::tr("hd2d.ui.feature-menu.one-column-2f-degrees"), static_cast<double>(settings.vr_panel_deg_per_cell));
        return buf;
    }
    case Item::VrPanelLift: {
        char buf[48]{};
        std::snprintf(buf, sizeof(buf), i18n::tr("hd2d.ui.feature-menu.0f-degrees"), static_cast<double>(settings.vr_panel_lift_deg));
        return buf;
    }
    case Item::VrPanelFollow: {
        if (settings.vr_panel_follow_deg <= 0.f) {
            return i18n::tr("hd2d.ui.feature-menu.0-degrees-stuck-to-the-head");
        }
        if (settings.vr_panel_follow_deg >= 180.f) {
            return i18n::tr("hd2d.ui.feature-menu.180-degrees-fixed-in-space");
        }
        char buf[48]{};
        std::snprintf(buf, sizeof(buf), i18n::tr("hd2d.ui.feature-menu.0f-degrees"), static_cast<double>(settings.vr_panel_follow_deg));
        return buf;
    }
    case Item::VrRenderScale:
        /*
         * **「次回起動から」を値に書く**。
         * swapchain はセッションを立てる瞬間にしか作られないので、回しても
         * いまの絵は変わらない——書いておかないと「効かない設定」に見える。
         *
         * @note 頭の `buf` をそのまま使う（近くの VR の行は自分で `buf` を建てているが、
         * あれは外側を隠すので C4456 が出る。真似しないこと）。
         */
        std::snprintf(buf, sizeof(buf), i18n::tr("hd2d.ui.feature-menu.2f-times-from-the-next-launch"),
            static_cast<double>(settings.vr_render_scale));
        return buf;
    case Item::FpsCellM: {
        /*
         * **VR のときだけ効くことを言う**（`TronSky` と同じ流儀。この項目は一人称の節に
         * あるが、平らな画面には「1 マス何メートル」という概念が無い）。行を出し入れ
         * しないのは、VR を始めた瞬間にカーソルが飛ぶのを避けるためである。
         */
        if (!this->vr_available_) {
            return i18n::tr("hd2d.ui.feature-menu.vr-only");
        }
        if (settings.vr_fps_cell_m <= 0.f) {
            return i18n::tr("hd2d.ui.feature-menu.automatic-from-your-height");
        }
        char buf[48]{};
        std::snprintf(buf, sizeof(buf), "%.1fm", static_cast<double>(settings.vr_fps_cell_m));
        return buf;
    }
    case Item::FpsStep: {
        //! **平らな画面でも効く**（`fps_drives_movement()`）ので、断りは付けない。
        char buf[48]{};
        std::snprintf(buf, sizeof(buf), i18n::tr("hd2d.ui.feature-menu.dms-per-step"), settings.fps_step_ms);
        return buf;
    }
    case Item::FpsPanelsEnter:
        return this->vr_available_ ? i18n::tr("hd2d.ui.feature-menu.open-details")
                                   : i18n::tr("hd2d.ui.feature-menu.vr-only");
    case Item::EffectsEnter:
        //! 同じく「詳細を開く」。前は `4/5 入` と出していたが、下に画面があることが読めない。
        return i18n::tr("hd2d.ui.feature-menu.open-details");
    case Item::SubBottomCount:
        std::snprintf(buf, sizeof(buf), i18n::tr("hd2d.ui.feature-menu.d-panels"), settings.sub_split.bottom());
        return buf;
    case Item::SubRightCount:
        /*
         * **縦画面分割には右列が無い**（`ui_layout.cpp` の `Tall`）。回しても絵が
         * 変わらない値を黙って出さない——この画面の方針（`EntityGlyphSize` と同じ）。
         */
        if (settings.layout_for(this->portrait_) == LayoutMode::Tall) {
            return i18n::tr("hd2d.ui.feature-menu.no-right-column-in-this-layout");
        }
        std::snprintf(buf, sizeof(buf), i18n::tr("hd2d.ui.feature-menu.d-panels"), settings.sub_split.right());
        return buf;
    case Item::PostFog:
        return on_off(settings.post.fog);
    case Item::PostDof:
        return on_off(settings.post.dof);
    case Item::PostBloom:
        return on_off(settings.post.bloom);
    case Item::PostGrade:
        return on_off(settings.post.grade);
    case Item::PostVignette:
        return on_off(settings.post.vignette);
    case Item::PostVignetteStrength:
        //! **入切が切ってあるときはそう言う**（値だけ出すと「効かない」と読まれる）。
        if (!settings.post.vignette) {
            return i18n::tr("hd2d.ui.feature-menu.off");
        }
        std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(settings.vignette_strength));
        return buf;
    case Item::PostSepia:
        if (settings.sepia <= 0.001f) {
            return i18n::tr("hd2d.ui.feature-menu.off");
        }
        std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>((settings.sepia * 100.f) + 0.5f));
        return buf;
    case Item::PostHdr:
        if (settings.hdr <= 0.001f) {
            return i18n::tr("hd2d.ui.feature-menu.off");
        }
        std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(settings.hdr));
        return buf;
    case Item::PostExposure:
        std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(settings.exposure));
        return buf;
    case Item::FpsSlabTurn:
        return (settings.fps_slab_turn == Hd2dSettings::SlabTurn::Snap8) ? i18n::tr("hd2d.ui.feature-menu.snap-to-8-directions") : i18n::tr("hd2d.ui.feature-menu.free-360-degrees");
    case Item::DungeonLight:
        std::snprintf(buf, sizeof(buf), i18n::tr("hd2d.ui.feature-menu.2fx-s"), static_cast<double>(settings.dungeon_light),
            (settings.dungeon_light > 1.001f) ? i18n::tr("hd2d.ui.feature-menu.bright")
                                              : ((settings.dungeon_light < 0.999f) ? i18n::tr("hd2d.ui.feature-menu.dark") : ""));
        return buf;
    case Item::PostDofStrength:
        if (settings.dof_strength <= 0.001f) {
            return i18n::tr("hd2d.ui.feature-menu.off");
        }
        std::snprintf(buf, sizeof(buf), "%.2f%s", static_cast<double>(settings.dof_strength),
            (settings.dof_strength > 1.f) ? i18n::tr("hd2d.ui.feature-menu.strong") : "");
        return buf;
    case Item::Cutaway:
        if (settings.cutaway_radius <= 0.f) {
            return i18n::tr("hd2d.ui.feature-menu.off");
        }
        std::snprintf(buf, sizeof(buf), "%.0f px", static_cast<double>(settings.cutaway_radius));
        return buf;
    case Item::VpadShow:
        return on_off(settings.vpad.show);
    case Item::VpadOpacity:
        //! 100% ＝ 既定の濃さ。下げると地図が透け、上げるとくっきり出る。
        std::snprintf(buf, sizeof(buf), "%d%%",
            static_cast<int>(std::lround(static_cast<double>(settings.vpad.opacity) * 100.0)));
        return buf;
    case Item::VpadButtonsEnter:
        //! ここも「詳細を開く」で揃える（前は `表示 14/14`。同 2026-08-19）。
        return i18n::tr("hd2d.ui.feature-menu.open-details");
    case Item::VpadLayoutEdit: {
        //! **いま向いている側の配置**（縦横で別。編集画面もこの側だけを触る）。
        const int orient = this->portrait_ ? kVpadPortrait : kVpadLandscape;
        std::snprintf(buf, sizeof(buf), i18n::tr("hd2d.ui.feature-menu.s-s"),
            settings.vpad.layout_line(orient).empty() ? i18n::tr("hd2d.ui.feature-menu.default-layout") : i18n::tr("hd2d.ui.feature-menu.changed"),
            this->portrait_ ? i18n::tr("hd2d.ui.feature-menu.portrait") : i18n::tr("hd2d.ui.feature-menu.landscape"));
        return buf;
    }
    case Item::VpadResetAll:
        //! 値の欄は 24 桁弱。長い説明は右端で切れる（実測）ので短く言う。
        return i18n::tr("hd2d.ui.feature-menu.reset-everything-to-the-defaults");
    default:
        if (vpad_button_index(item) >= 0) {
            return on_off(settings.vpad.visible[vpad_button_index(item)]);
        }
        if (vr_panel_index(item) >= 0) {
            return on_off(settings.vr_fps_show[vr_panel_index(item)]);
        }
        return "";
    }
}

void FeatureMenu::adjust(Item item, int delta, Hd2dSettings &settings) const
{
    if (const auto field = toggle_setting(item)) {
        settings.*field = !(settings.*field);
        return;
    }

    const auto step = static_cast<float>(delta);
    /*
     * 仕切り。**後ろの枚が潰れないところで止める**（掴んで動かすときと同じ規則。
     * 判定を 2 か所に置くと、メニューでだけ潰せる形が残る）。
     */
    if (const int bottom_at = sub_bottom_divider_index(item); bottom_at >= 0) {
        const int count = settings.sub_split.bottom();
        if (bottom_at >= (count - 1)) {
            return; // いまの枚数では効かない仕切り
        }
        int before = 0;
        for (int i = 0; i < bottom_at; ++i) {
            before += settings.sub_split.bottom_w20[i];
        }
        settings.sub_split.bottom_w20[bottom_at] = std::clamp(settings.sub_split.bottom_w20[bottom_at] + delta,
            kSubBottomW20Min, kSubBottomUnits - before - (kSubBottomW20Min * (count - 1 - bottom_at)));
        return;
    }
    if (const int right_at = sub_right_divider_index(item); right_at >= 0) {
        const int count = settings.sub_split.right();
        if (right_at >= (count - 1)) {
            return;
        }
        int before = 0;
        for (int i = 0; i < right_at; ++i) {
            before += settings.sub_split.right_h20[i];
        }
        settings.sub_split.right_h20[right_at] = std::clamp(settings.sub_split.right_h20[right_at] + delta,
            kSubRightH20Min, kSubRightUnits - before - (kSubRightH20Min * (count - 1 - right_at)));
        return;
    }
    const int kind_index = sub_kind_index(item);
    if (kind_index >= 0) {
        if (this->kind_choices_.empty()) {
            return; // 一覧が来ていない。**当てずっぽうで番号を動かさない**
        }
        const int count = static_cast<int>(this->kind_choices_.size());
        int at = 0;
        for (int i = 0; i < count; ++i) {
            if (this->kind_choices_[static_cast<std::size_t>(i)].flag == settings.sub_panel_kind[kind_index]) {
                at = i;
                break;
            }
        }
        at = ((at + delta) + count) % count;
        settings.sub_panel_kind[kind_index] = this->kind_choices_[static_cast<std::size_t>(at)].flag;
        return;
    }
    switch (item) {
    case Item::CameraPitch:
        settings.camera_pitch_deg = std::clamp(settings.camera_pitch_deg + step, 10.f, 85.f);
        break;
    case Item::CameraFov:
        settings.camera_fov_deg = std::clamp(settings.camera_fov_deg + step, 10.f, 80.f);
        break;
    case Item::CameraCellPx:
        settings.camera_cell_px = std::clamp(settings.camera_cell_px + (step * 5.f), 24.f, 400.f);
        break;
    case Item::FpsVerticalLook:
        settings.fps_vertical_look = !settings.fps_vertical_look;
        break;
    case Item::FpsWallUpper:
        settings.fps_wall_upper = !settings.fps_wall_upper;
        break;
    case Item::FpsCeiling:
        settings.fps_ceiling = !settings.fps_ceiling;
        break;
    case Item::ModernMode: settings.modern_ui=!settings.modern_ui;break;
    case Item::ModernFontSize: settings.modern_font_level=std::clamp(settings.modern_font_level+(delta>0?1:-1),0,4);break;
    case Item::ModernTheme: {
        const int selected=settings.modern_ui?std::clamp(settings.modern_theme,0,2):3;
        const int next=(selected+(delta>0?1:3))%4;
        settings.modern_ui=next!=3;if(next!=3)settings.modern_theme=next;
        break;
    }
    case Item::MainPanelItem:
        settings.main_panel = (settings.main_panel == Hd2dSettings::MainPanel::Ascii)
            ? Hd2dSettings::MainPanel::Hd2d
            : Hd2dSettings::MainPanel::Ascii;
        break;
    case Item::StatusColSideItem: {
        //! 3 択を回す（自動 → 左 → 右 → 自動）。左回しは逆順。
        const int cur = static_cast<int>(settings.status_col_side);
        const int next = ((cur + ((delta >= 0) ? 1 : 2)) % 3);
        settings.status_col_side = static_cast<Hd2dSettings::StatusColSide>(next);
        break;
    }
    case Item::GlyphFallback:
        settings.entity_glyph_fallback = !settings.entity_glyph_fallback;
        break;
    case Item::AsciiPanelSize:
        if(settings.modern_ui){settings.modern_font_level=std::clamp(settings.modern_font_level+(delta>0?1:-1),0,4);break;}
        /*
         * 0（UI と同じ）を**下限の下に置く**。左へ回しきると「UI と同じ」へ戻り、
         * そこからさらに左へは動かない——0 を並びの真ん中に挟むと、
         * 回している途中で意味が飛ぶ（px → UI と同じ → px）ので読めなくなる。
         */
        if (settings.ascii_panel_px <= 0) {
            settings.ascii_panel_px = (delta > 0) ? Hd2dSettings::kAsciiPanelPxMin : 0;
            break;
        }
        settings.ascii_panel_px += delta * 2;
        if (settings.ascii_panel_px < Hd2dSettings::kAsciiPanelPxMin) {
            settings.ascii_panel_px = 0; //!< UI と同じへ戻る
        } else if (settings.ascii_panel_px > Hd2dSettings::kAsciiPanelPxMax) {
            settings.ascii_panel_px = Hd2dSettings::kAsciiPanelPxMax;
        }
        break;
    case Item::Language: {
        /*
         * 読めたカタログだけを順に回す（`i18n::available()`）。**訳の無い言語は
         * 並べない**ので、左右で回している限り画面が ID だらけにはならない。
         * 切り替えは即座に効く（`tr()` が次の描画から新しい表を引く）。
         */
        const auto &langs = i18n::available();
        if (langs.empty()) {
            break;
        }
        const int count = static_cast<int>(langs.size());
        int index = 0;
        for (int i = 0; i < count; ++i) {
            if (i18n::current() == langs[static_cast<std::size_t>(i)].code) {
                index = i;
                break;
            }
        }
        const auto &next = langs[static_cast<std::size_t>((((index + delta) % count) + count) % count)];
        i18n::set_language(next.code);
        settings.lang = next.code; //!< cfg へ残す（次の起動でも同じ言語で開く）
        break;
    }
    case Item::Layout: {
        /*
         * 4 つを順に回す。**どれかを既定にして他を捨てるのではない**。
         * 動かすのは**いま向いている側**だけ（縦横で別に持つ。同 2026-08-12）。
         */
        static constexpr LayoutMode kOrder[] = { LayoutMode::Full, LayoutMode::Hybrid, LayoutMode::Split,
            LayoutMode::Tall };
        const int count = static_cast<int>(sizeof(kOrder) / sizeof(kOrder[0]));
        LayoutMode &mode = settings.layout_ref(this->portrait_);
        int index = 0;
        for (int i = 0; i < count; ++i) {
            if (kOrder[i] == mode) {
                index = i;
                break;
            }
        }
        mode = kOrder[(((index + delta) % count) + count) % count];
        break;
    }
    case Item::Subs:
        settings.subs_open = !settings.subs_open;
        break;
    case Item::WindowMode:
        settings.windowed = !settings.windowed;
        break;
    case Item::MoveSmooth:
        settings.move_smoothing = (settings.move_smoothing == MoveSmoothing::All) ? MoveSmoothing::Entities
                                                                                  : MoveSmoothing::All;
        break;
    case Item::EntityStyleItem:
        settings.entity_style = (settings.entity_style == Hd2dSettings::EntityStyle::Ascii)
            ? Hd2dSettings::EntityStyle::Slab
            : Hd2dSettings::EntityStyle::Ascii;
        break;
    case Item::EntitySlabFacing:
        settings.entity_slab_screen_aligned = !settings.entity_slab_screen_aligned;
        break;
    case Item::EntityGlyphSize:
        settings.entity_glyph_pct = std::clamp(settings.entity_glyph_pct + (delta * 5),
            Hd2dSettings::kEntityGlyphPctMin, Hd2dSettings::kEntityGlyphPctMax);
        break;
    case Item::SceneLookItem:
        settings.scene_look
            = (settings.scene_look == SceneLookKind::Tron) ? SceneLookKind::Standard : SceneLookKind::Tron;
        break;
    case Item::TronSky:
        settings.tron_sky = !settings.tron_sky;
        break;
    case Item::TronFaceGlyph:
        settings.tron_face_glyph = !settings.tron_face_glyph;
        break;
    case Item::SurfaceWear:
        //! 10% 刻み。**0 まで下げられる**（切る口をここ以外に持たせない）。
        settings.wear_pct = std::clamp(settings.wear_pct + (delta * 10),
            Hd2dSettings::kWearPctMin, Hd2dSettings::kWearPctMax);
        break;
    case Item::LeafDetail:
        settings.leaf_pct = std::clamp(settings.leaf_pct + (delta * 10),
            Hd2dSettings::kLeafPctMin, Hd2dSettings::kLeafPctMax);
        break;
    case Item::MinimapEnter:
        break; //!< 節への入口。**左右では動かさない**（決定だけ。誤爆すると値が変わって見える）
    case Item::MinimapCornerItem: {
        //! 5 つを巡る（端で止めずに回す——5 個なら回したほうが早い）。
        const int count = static_cast<int>(MinimapCorner::Count);
        const int now = static_cast<int>(settings.minimap_corner);
        settings.minimap_corner = static_cast<MinimapCorner>(((now + delta) % count + count) % count);
        break;
    }
    case Item::MinimapSize:
        settings.minimap_size_pct = std::clamp(settings.minimap_size_pct + (delta * 10),
            Hd2dSettings::kMinimapSizePctMin, Hd2dSettings::kMinimapSizePctMax);
        break;
    case Item::MinimapCellPx:
        settings.minimap_cell_px = std::clamp(settings.minimap_cell_px + delta,
            Hd2dSettings::kMinimapCellPxMin, Hd2dSettings::kMinimapCellPxMax);
        break;
    case Item::MinimapOpacity:
        settings.minimap_opacity_pct = std::clamp(settings.minimap_opacity_pct + (delta * 5),
            Hd2dSettings::kMinimapOpacityPctMin, Hd2dSettings::kMinimapOpacityPctMax);
        break;
    case Item::MinimapRelative:
        settings.minimap_relative = !settings.minimap_relative;
        break;
    case Item::BgmModeItem: {
        //! 3 つを巡る（端で止めない——3 個なら回したほうが早い。ミニマップの位置と同じ）。
        const int count = static_cast<int>(Hd2dSettings::BgmMode::Count);
        const int now = static_cast<int>(settings.bgm_mode);
        settings.bgm_mode = static_cast<Hd2dSettings::BgmMode>(((now + delta) % count + count) % count);
        break;
    }
    case Item::MusicVolume:
        settings.music_volume = std::clamp(settings.music_volume + delta, 0, Hd2dSettings::kVolumeMax);
        break;
    case Item::SoundVolume:
        settings.sound_volume = std::clamp(settings.sound_volume + delta, 0, Hd2dSettings::kVolumeMax);
        break;
    case Item::RealtimeSpeed:
        settings.realtime_speed_index = std::clamp(settings.realtime_speed_index + delta,
            0, Hd2dSettings::kRealtimeSpeedCount - 1);
        break;
    case Item::RealtimePrompt:
        settings.realtime_prompt_live = !settings.realtime_prompt_live;
        break;
    /* ---- VR（§22）。刻みは「1 回押して分かる」大きさにしてある ---- */
    case Item::VrEnter:
        break; //!< 入切は決定だけ（左右で走ると、隣を触るつもりで HMD が起きる）
    case Item::VrTileM:
        //! 5mm 刻み。0.5cm 〜 30cm（決めたこと「ブロックをもっと拡大したい」）。
        settings.vr_tile_m = std::clamp(settings.vr_tile_m + (static_cast<float>(delta) * 0.005f), 0.005f, 0.30f);
        break;
    case Item::VrTableHeight:
        //! 0 は「頭の高さから」。下限を下回ったら自動へ戻す（切り替える手段をここに置く）。
        settings.vr_table_height_m += static_cast<float>(delta) * 0.05f;
        //! **床（0.05m）まで下げられる**。そこを割ったら自動へ。
        if (settings.vr_table_height_m < 0.05f) {
            settings.vr_table_height_m = 0.f;
        } else if (settings.vr_table_height_m > 1.40f) {
            settings.vr_table_height_m = 1.40f;
        }
        break;
    case Item::VrPanelSize:
        settings.vr_panel_deg_per_cell
            = std::clamp(settings.vr_panel_deg_per_cell + (static_cast<float>(delta) * 0.02f), 0.10f, 1.00f);
        break;
    case Item::VrPanelLift:
        settings.vr_panel_lift_deg
            = std::clamp(settings.vr_panel_lift_deg + (static_cast<float>(delta) * 2.f), -20.f, 70.f);
        break;
    case Item::VrPanelFollow:
        settings.vr_panel_follow_deg
            = std::clamp(settings.vr_panel_follow_deg + (static_cast<float>(delta) * 5.f), 0.f, 180.f);
        break;
    case Item::VrRenderScale:
        /*
         * 0.05 刻み・0.5〜1.0。**幅は cfg の読み（`load_settings`）と同じ**にすること
         * ——メニューで出せる値が cfg で弾かれると、書いたそばから戻る。
         */
        settings.vr_render_scale
            = std::clamp(settings.vr_render_scale + (static_cast<float>(delta) * 0.05f), 0.5f, 1.f);
        break;
    case Item::FpsCellM:
        //! 0 は「身長から」。下限を下回ったら自動へ戻す。
        settings.vr_fps_cell_m += static_cast<float>(delta) * 0.25f;
        if (settings.vr_fps_cell_m < 1.f) {
            settings.vr_fps_cell_m = 0.f;
        } else if (settings.vr_fps_cell_m > 12.f) {
            settings.vr_fps_cell_m = 12.f;
        }
        break;
    case Item::FpsStep:
        settings.fps_step_ms = std::clamp(settings.fps_step_ms + (delta * 20), 60, 600);
        break;
    case Item::FpsPanelsEnter:
    case Item::EffectsEnter:
        break; //!< 決定でサブ画面へ入る（左右では何もしない）
    case Item::RealtimeSelfSpan:
        settings.realtime_self_span_index = std::clamp(settings.realtime_self_span_index + delta,
            0, Hd2dSettings::kRealtimeSelfSpanCount - 1);
        break;
    case Item::SubBottomCount:
        settings.sub_split.bottom_count = std::clamp(settings.sub_split.bottom() + delta, 2, kSubBottomMax);
        break;
    case Item::SubRightCount:
        settings.sub_split.right_count = std::clamp(settings.sub_split.right() + delta, 1, kSubRightMax);
        break;
    case Item::PostFog:
        settings.post.fog = !settings.post.fog;
        break;
    case Item::PostDof:
        settings.post.dof = !settings.post.dof;
        break;
    case Item::PostBloom:
        settings.post.bloom = !settings.post.bloom;
        break;
    case Item::PostGrade:
        settings.post.grade = !settings.post.grade;
        break;
    case Item::PostVignetteStrength:
        settings.vignette_strength = std::clamp(settings.vignette_strength + (step * 0.04f), 0.f, 0.9f);
        break;
    case Item::PostSepia:
        settings.sepia = std::clamp(settings.sepia + (step * 0.1f), 0.f, 1.f);
        break;
    case Item::PostHdr:
        settings.hdr = std::clamp(settings.hdr + (step * 0.1f), 0.f, 1.5f);
        break;
    case Item::PostExposure:
        //! 既定 1.25 が従来のトーンマップの係数。下げると暗く、上げると明るく飛びやすい。
        settings.exposure = std::clamp(settings.exposure + (step * 0.05f), 0.5f, 2.5f);
        break;
    case Item::PostVignette:
        settings.post.vignette = !settings.post.vignette;
        break;
    case Item::FpsSlabTurn:
        settings.fps_slab_turn = (settings.fps_slab_turn == Hd2dSettings::SlabTurn::Free)
            ? Hd2dSettings::SlabTurn::Snap8
            : Hd2dSettings::SlabTurn::Free;
        break;
    case Item::DungeonLight:
        //! 環境光の倍率。**方向光と点光源には掛からない**（`hd2d_settings.h` の注記）。
        settings.dungeon_light = std::clamp(settings.dungeon_light + (step * 0.1f), 0.5f, 3.f);
        break;
    case Item::PostDofStrength:
        //! 上限 2.4（強さで往復と半径が上がる。1.6 超は 7 往復・半径 5.0 ＝ ミニチュアの接写）。
        settings.dof_strength = std::clamp(settings.dof_strength + (step * 0.2f), 0.f, 2.4f);
        break;
    case Item::Cutaway:
        settings.cutaway_radius = std::clamp(settings.cutaway_radius + (step * 10.f), 0.f, 600.f);
        break;
    case Item::VpadShow:
        settings.vpad.show = !settings.vpad.show;
        break;
    case Item::VpadOpacity:
        //! 25% 刻み。範囲は描画側と同じ定数（`kVpadOpacityMin/Max`。別々に持つと必ずずれる）。
        settings.vpad.opacity = std::clamp(settings.vpad.opacity + (step * 0.25f), kVpadOpacityMin, kVpadOpacityMax);
        break;
    default:
        //! ボタンの入切。**サブ画面へ入る・配置を開く・全初期化は決定だけ**（`handle()` が先に食う。
        //! 左右で「デフォルトに戻す」が走る作りにすると、隣の項目を触るつもりで初期化が走る）。
        if (vpad_button_index(item) >= 0) {
            settings.vpad.visible[vpad_button_index(item)] = !settings.vpad.visible[vpad_button_index(item)];
        }
        //! VR の一人称で出す部位（§22）。切ったぶんは板に描かれない。
        if (vr_panel_index(item) >= 0) {
            settings.vr_fps_show[vr_panel_index(item)] = !settings.vr_fps_show[vr_panel_index(item)];
        }
        break; // キー割り当ては左右では動かない（決定で入る）
    }
}

void FeatureMenu::open(const Hd2dSettings &settings)
{
    command_key_prefix_.clear();
    command_tabs_enabled_=settings.modern_ui;
    features_from_tab_=false;
    this->open_ = true;
    this->page_ = Page::Root;
    /*
     * **開いた直後は最初の分類に合わせる**（`feature_menu.h`）。入口の先頭は言語で、
     * そこにカーソルを置くと開いて → を押しただけで言語が変わる——いちばん触って
     * ほしくない項目がいちばん触りやすい所に来る。読めない人は ↑ 1 回で届く
     * （上で回り込むので、下まで降りる必要は無い）。
     */
    this->cursor_ = root_item_count();
    this->root_cursor_ = this->cursor_;
    this->bind_col_ = 0;
    this->bind_scroll_ = 0;
    this->stop_waiting();
}

void FeatureMenu::open_commands(const Hd2dSettings &settings)
{
    /*
     * **コマンドの画面を直に開く**（`kActionCommandMenu`）。触りだけの機体では
     * 「機能メニューを開いて 1 つ潜る」の 1 手が重いので、ボタン 1 つで届く道を作る。
     * 入口へ戻ったときに `コマンド …` が選ばれているように `root_cursor_` も置く。
     */
    if (g_core_hides_commands) {
        return; //!< DCSS（D32）: コマンドの画面は開かない（開く入口はキー・パッド・歯車のどれも機能メニューへ向く）
    }
    this->open(settings);
    const std::vector<Page> pages = root_pages();
    const auto at = std::find(pages.begin(), pages.end(), Page::Commands);
    if (at == pages.end()) {
        return; //!< 並べていない実行体（いまのところ無い）。入口のまま開けておく
    }
    this->root_cursor_ = root_item_count() + static_cast<int>(std::distance(pages.begin(), at));
    this->page_ = Page::Commands;
    this->command_group_.clear();
    this->command_group_cursor_ = 0;
    this->command_subgroup_.clear();
    this->command_subgroup_cursor_ = 0;
    //! **直に開いた**印。取消で入口へ落とさず、パネルごと閉じるため（`handle()` の取消）。
    this->commands_direct_ = true;
    this->cursor_ = this->next_command_row(this->command_rows(), -1, 1);
    this->command_scroll_ = 0;
    enter_command_tabs();
}

void FeatureMenu::close()
{
    command_key_prefix_.clear();
    if(features_from_tab_) {feature_resume_page_=page_;feature_resume_cursor_=cursor_;}
    if(showing_command_tabs() && !command_categories_)command_tabs_.remember(cursor_,command_scroll_);
    this->open_ = false;
    this->stop_waiting();
}

void FeatureMenu::stop_waiting()
{
    this->waiting_ = false;
    this->wait_action_ = kActionNone;
    this->wait_col_ = 0;
    this->wait_external_ = false;
    this->pending_key_sym_ = 0;
    this->wait_since_ms_ = 0;
    this->wait_note_.clear();
}

void FeatureMenu::update(std::uint32_t now_ms)
{
    this->wait_now_ms_ = now_ms;
    if (!this->waiting_) {
        this->pending_key_sym_ = 0;
        return;
    }
    if ((this->pending_key_sym_ != 0) && ((now_ms - this->pending_key_since_ms_) >= 60U) && (this->ext_binds_ != nullptr)
        && this->wait_external_ && (this->wait_col_ == 0)) {
        /*
         * 印字キーを押したのに `SDL_TEXTINPUT` が来ない（Android の外付けキーボードなど。文字入力を止めてある）。
         * キーの字から割り当てる。Shift つきの英字は大文字。Shift つきの記号は配列で字が変わるので受け取らない。
         */
        const int sym = this->pending_key_sym_;
        const int mods = this->pending_key_mods_;
        this->pending_key_sym_ = 0;
        const bool letter = (sym >= 'a') && (sym <= 'z');
        if (((mods & kModShift) != 0) && !letter) {
            this->wait_note_ = (i18n::current() == "ja") ? "Shift つきの記号はここでは受け取れません（Shift なしのキーを押してください）"
                                                           : "Shifted symbols cannot be read here (press the key without Shift)";
        } else {
            const int ch = (letter && ((mods & kModShift) != 0)) ? (sym - 'a' + 'A') : sym;
            const std::string note = this->ext_binds_->assign_key(this->wait_action_, ch);
            if (note.empty()) {
                this->stop_waiting();
                return;
            }
            this->wait_note_ = note;
        }
    }
    /*
     * **15 秒未入力で降りる**。ESC を「割り当ての消去」に
     * 使っているので、変更モードから何も変えずに降りる道が時間しか無い。
     * 引き算は符号なしで回り込むが、`now_ms` は単調に増えるので差は正しく出る。
     */
    if ((now_ms - this->wait_since_ms_) >= kBindWaitTimeoutMs) {
        this->stop_waiting();
    }
}

/*
 * ============================================================ コマンドの一覧（2026-08-22）
 *
 * 決めたこと「変愚の通常メニューを UI 側で作成してすべてのコマンドを実行できるようにしよう」。
 *
 * **並べるものは既に線の上にある。** 変愚コアは自分の通常メニュー
 * （`src/cmd-io/cmd-menu-content-table.cpp` の `menu_info`）を平坦化して `pad_commands` で
 * 送っており（`platform/windows/core_main.cpp` の `build_pad_commands_payload`）、
 * 分類も名札もあちらの字のまま届く。**このパネルは並べて実行するだけ**である。
 */
/*!
 * @brief キー列を**人が読める綴り**にする（`^S` `Tab` `Space` …）。
 * @param keys 解決済みのキー列（`PadCommand::keys`）
 * @return 綴り。1 つでも綴れないバイトが混じっていたら**空**（嘘を出さない）
 *
 * @details 制御キーは `^S` の形で出す——`0x13` を生で出すと画面が壊れるし、
 * `19` と出しても読む側には何のことか分からない。
 *
 * 0x7F 以上は綴らない。あそこから先は**符号が決まっていない**（コアが送るのは
 * バイトであって文字ではない）ので、当てずっぽうの字を出すより黙るほうがよい。
 */
static std::string key_sequence_label(const std::vector<int> &keys)
{
    std::string out;
    for (const int raw : keys) {
        const int key = raw & 0xFF;
        /*
         * **2 打以上は空白で区切る**（`? m` のように読ませる）。区切らないと
         * `?m` になって 1 つの綴りに見える——制御キーの `^S` と紛らわしい。
         */
        if (!out.empty()) {
            out += ' ';
        }
        if (key == 0x09) {
            out += "Tab";
        } else if ((key == 0x0D) || (key == 0x0A)) {
            out += "Enter";
        } else if (key == 0x1B) {
            out += "Esc";
        } else if (key == 0x20) {
            out += "Space";
        } else if ((key >= 0x01) && (key <= 0x1A)) {
            out += '^';
            out += static_cast<char>('A' + key - 1);
        } else if ((key > 0x20) && (key < 0x7F)) {
            out += static_cast<char>(key);
        } else {
            return std::string(); //!< 綴れないバイトが 1 つでもあれば、何も出さない
        }
    }
    return out;
}


static std::string command_display_label(const std::vector<int> &keys,const std::string &label) {
    return (keys.empty()?std::string():key_sequence_label(keys)+") ")+label;
}

bool FeatureMenu::handle_native_key(int key,Hd2dSettings &) {
    if(!open_||page_!=Page::Commands||waiting_)return false;
    if(command_requested_present_)return true;
    if((key==27||key==8)&&!command_key_prefix_.empty()){command_key_prefix_.clear();return true;}
    if(key==13&&command_key_prefix_.empty())return false;
    if(key!=13)command_key_prefix_.push_back(key);
    const auto rows=command_rows();int exact=-1,matches=0;
    const auto search=[&](){exact=-1;matches=0;for(size_t i=0;i<rows.size();++i) {
        const auto &r=rows[i];if(r.heading||!r.available||r.keys.size()<command_key_prefix_.size())continue;
        if(!std::equal(command_key_prefix_.begin(),command_key_prefix_.end(),r.keys.begin()))continue;
        ++matches;if(r.keys.size()==command_key_prefix_.size())exact=exact==-1?static_cast<int>(i):-2;
    }};
    search();
    if(!matches){command_key_prefix_.clear();return false;}
    if(exact>=0&&(matches==1||key==13)) {
        cursor_=exact;command_requested_=rows[exact].id;command_requested_present_=true;command_key_prefix_.clear();
        if(command_tabs_enabled_)command_tabs_.remember(cursor_,command_scroll_);
    }
    return true;
}
bool FeatureMenu::click_row(int x,int y,Hd2dSettings &settings) {
    if(!open_||waiting_||command_requested_present_)return false;
    if(click_tab_header(x,y,settings))return true;
    x=int(x/painted_scale_);y=int(y/painted_scale_);
    if(settings.modern_ui&&settings_editing_) {
        if(settings_minus_.contains(x,y))handle(MenuNav::Left,settings);
        else if(settings_plus_.contains(x,y))handle(MenuNav::Right,settings);
        return true;
    }
    if(settings.modern_ui&&!showing_commands()&&!showing_bindings()&&settings_details_rect_.contains(x,y)){settings_details_focus_=true;return true;}
    if(showing_command_tabs()&&command_details_rect_.contains(x,y)){command_details_focus_=true;return true;}
    for(const auto &hit:painted_rows_)if(hit.first.contains(x,y)) {
        command_details_focus_=false;command_details_scroll_=0;settings_details_focus_=false;settings_details_scroll_=0;
        command_key_prefix_.clear();cursor_=hit.second;
        if(showing_command_tabs()&&!command_categories_)command_tabs_.remember(cursor_,command_scroll_);
        return true;
    }
    return false;
}
void FeatureMenu::scroll_at(int x,int y,int direction,Hd2dSettings &settings) {
    if(waiting_)return;
    if(showing_command_tabs())command_details_focus_=command_details_rect_.contains(int(x/painted_scale_),int(y/painted_scale_));
    else if(settings.modern_ui&&!showing_bindings()) {
        if(settings_details_rect_.contains(int(x/painted_scale_),int(y/painted_scale_))) {
            settings_details_scroll_=std::max(0,settings_details_scroll_+direction);if(!settings_editing_)settings_details_focus_=true;return;
        }
        if(settings_editing_)return;settings_details_focus_=false;
    }
    handle(direction<0?MenuNav::Up:MenuNav::Down,settings);
}

bool FeatureMenu::click_tab_header(int x,int y,Hd2dSettings &settings) {
    x=int(x/painted_scale_);y=int(y/painted_scale_);
    if(!tab_navigation_active())return false;
    if(header_layout_.previous.contains(x,y)){handle(MenuNav::PreviousTab,settings);return true;}
    if(header_layout_.next.contains(x,y)){handle(MenuNav::NextTab,settings);return true;}
    for(size_t i=0;i<header_layout_.tabs.size();++i)if(header_layout_.tabs[i].contains(x,y)) {
        const int target=header_first_+static_cast<int>(i);
        /*
         * **目当てのタブへ一度で移る**（2026-10-07）。以前は 1 つずつ「次のタブ」を繰り返していて、
         * 途中のタブの `restore_feature_tab()` が走った——「持ち物一覧」を通ると一覧を開く要求が
         * 立ったまま残り、「機能」を押したのに持ち物の一覧が開いた（Android のエミュレータで見た）。
         * Rogue 系では途中の「設定」を通ると原作の設定画面のコマンドまで送られうる。
         * 1 つずつ移るとき（`handle_command_tabs` の NextTab）と同じ後始末をして、着いたタブだけ処理する。
         */
        jump_to_command_tab(target);
        return true;
    }
    return false;
}

void FeatureMenu::jump_to_command_tab(int target) {
    if(target<0||target>=static_cast<int>(command_tabs_.tabs().size())||command_tabs_.index()==target)return;
    if(!command_categories_)command_tabs_.remember(cursor_,command_scroll_);
    command_details_focus_=false;command_details_scroll_=0;
    command_key_prefix_.clear();
    command_tabs_.select(target);command_categories_=false;
    cursor_=command_tabs_.cursor();command_scroll_=command_tabs_.scroll();restore_feature_tab();
}

void FeatureMenu::restore_feature_tab() {
    if(!command_categories_&&!command_tabs_.tabs().empty()&&command_tabs_.tabs()[command_tabs_.index()].id=="@inventory") {
        inventory_requested_=true;return;
    }
    if(!command_categories_&&!command_tabs_.tabs().empty()&&command_tabs_.tabs()[command_tabs_.index()].id=="設定") {
        /*
         * Rogue の原作オプションは、タブの中に「原作の設定」という入口をもう一段
         * 置かない。大項目の「設定」を選んだ時点で original `o` を送り、各設定値を
         * 左右で直接変更できる Modern settings list を開く。
         */
        const auto option=std::find_if(pad_commands_.begin(),pad_commands_.end(),[](const auto &command) {
            return command.group_utf8=="設定"&&command.command=='o'&&!command.keys.empty();
        });
        if(option!=pad_commands_.end()) {
            command_requested_=option->id;command_requested_present_=true;return;
        }
    }
    if(!command_categories_ && !command_tabs_.tabs().empty() && command_tabs_.tabs()[command_tabs_.index()].id=="@ui-features"
        && feature_resume_page_!=Page::Commands) {
        features_from_tab_=true;page_=feature_resume_page_;cursor_=feature_resume_cursor_;
    }
}
void FeatureMenu::open_command_tab(int index,const Hd2dSettings &settings) {
    open_commands(settings);inventory_requested_=false;features_from_tab_=false;page_=Page::Commands;command_categories_=false;
    command_tabs_.select(index);cursor_=command_tabs_.cursor();command_scroll_=command_tabs_.scroll();restore_feature_tab();
}
void FeatureMenu::open_features(const Hd2dSettings &settings) {
    /*
     * **新 UI ではコマンドメニューと機能メニューは同じ 1 つの窓**（決めたこと
     * 2026-09-25）。F10 / パッド Back も F1 / Start（`open_commands()`）と同じ道を
     * 通す——**以前はここで最後の「機能」タブへ強制的に移していたが、それをやめた**。
     * `open_commands()` はすでに前回のタブと位置を復元する
     * （`enter_command_tabs()` → `MenuTabs::cursor()/scroll()`。`close()` が
     * `command_tabs_.remember()` で覚える）ので、F10 で開き直しても F1 で開き直しても
     * 同じ所へ戻る。前回が持ち物一覧タブだったときにそこで一覧を開く要求
     * （`restore_feature_tab()` の `inventory_requested_=true`）も、いまはここで
     * 打ち消さない——F1 で戻ってきたときと同じに扱う。
     */
    if (this->ext_binds_ != nullptr) {
        this->ext_binds_->on_open(); //!< 操作の割り当ての最新の中身を問い合わせる（DCSS。D33）
    }
    //! DCSS（D32）は新 UI でもコマンドのタブを持たず、入口（機能メニューそのものの項目）を直に開く。
    if(!settings.modern_ui || g_core_hides_commands) {open(settings);return;}
    open_commands(settings);
}

void FeatureMenu::refresh_command_tabs()
{
    std::vector<MenuTab> tabs;
    for(const auto &command:pad_commands_) {
        auto it=std::find_if(tabs.begin(),tabs.end(),[&](const auto &tab){return tab.id==command.group_utf8;});
        if(it==tabs.end()) {
            tabs.push_back({command.group_utf8,command.group_utf8.empty()?i18n::tr("hd2d.ui.feature-menu.other"):command.group_utf8,{}});
            it=tabs.end()-1;
        }
        it->items.push_back(std::to_string(command.id));
    }
    if(!pad_commands_.empty())tabs.push_back({"@inventory",i18n::current()=="ja"?"持ち物一覧":"Inventory",{"inventory"}});
    MenuTab features{"@ui-features",i18n::current()=="ja"?"機能":"Features",{"language"}};
    for(const auto page:root_pages())if(page!=Page::Commands)features.items.push_back(std::to_string(static_cast<int>(page)));
    tabs.push_back(std::move(features));
    command_tabs_.reset(std::move(tabs));
    if(showing_command_tabs() && !command_categories_) {
        cursor_=command_tabs_.cursor();command_scroll_=command_tabs_.scroll();
    }
}
void FeatureMenu::enter_command_tabs()
{
    if(!command_tabs_enabled_)return;
    command_categories_=false;
    refresh_command_tabs();
    /*
     * Rogue の「設定」タブは、選んだ時点で原作の o をコアへ送る。前回そのタブのまま
     * 設定画面を閉じた場合、メニューを開き直しただけで o が送られ、メニューの代わりに
     * 設定画面が開いてしまう。開き直すときは先頭のタブから始める。
     */
    if(!command_tabs_.tabs().empty()&&command_tabs_.tabs()[command_tabs_.index()].id=="設定")command_tabs_.select(0);
    cursor_=command_tabs_.cursor();command_scroll_=command_tabs_.scroll();
    restore_feature_tab();
}
std::vector<std::string> FeatureMenu::command_tab_labels() const
{
    std::vector<std::string> labels;
    for(const auto &tab:command_tabs_.tabs())labels.push_back(tab.label);
    return labels;
}
void FeatureMenu::handle_command_tabs(MenuNav nav, Hd2dSettings &settings)
{
    if(!command_categories_)command_tabs_.remember(cursor_,command_scroll_);
    if(nav==MenuNav::Categories) {
        command_categories_=!command_categories_;
        cursor_=command_categories_?command_tabs_.index():command_tabs_.cursor();
        command_scroll_=command_categories_?0:command_tabs_.scroll();return;
    }
    if(nav==MenuNav::PreviousTab || nav==MenuNav::NextTab) {
        command_details_focus_=false;command_details_scroll_=0;
        command_key_prefix_.clear();
        command_tabs_.step(nav==MenuNav::PreviousTab?-1:1);command_categories_=false;
        cursor_=command_tabs_.cursor();command_scroll_=command_tabs_.scroll();restore_feature_tab();return;
    }
    if(nav==MenuNav::PreviousPane){command_details_focus_=false;return;}
    if(nav==MenuNav::NextPane){command_details_focus_=true;return;}
    if(nav==MenuNav::TogglePane){command_details_focus_=!command_details_focus_;return;}
    if(command_details_focus_) {
        if(nav==MenuNav::Up)command_details_scroll_=std::max(0,command_details_scroll_-1);
        if(nav==MenuNav::Down)++command_details_scroll_;
        if(nav==MenuNav::Cancel||nav==MenuNav::Confirm)command_details_focus_=false;
        return;
    }
    if(nav==MenuNav::Left||nav==MenuNav::Right)return;
    if(nav==MenuNav::Cancel) {
        if(command_categories_) {command_categories_=false;cursor_=command_tabs_.cursor();command_scroll_=command_tabs_.scroll();restore_feature_tab();return;}
        if(commands_direct_)close();else {page_=Page::Root;cursor_=root_cursor_;}
        return;
    }
    const auto rows=command_rows();
    if(rows.empty())return;
    cursor_=std::clamp(cursor_,0,static_cast<int>(rows.size())-1);
    if(nav==MenuNav::Up){cursor_=(cursor_-1+static_cast<int>(rows.size()))%static_cast<int>(rows.size());command_details_scroll_=0;}
    if(nav==MenuNav::Down){cursor_=(cursor_+1)%static_cast<int>(rows.size());command_details_scroll_=0;}
    if(!command_categories_ && !command_tabs_.tabs().empty() && command_tabs_.tabs()[command_tabs_.index()].id=="@ui-features"
        && cursor_==0 && (nav==MenuNav::Left || nav==MenuNav::Right)) {adjust(Item::Language,nav==MenuNav::Left?-1:1,settings);return;}
    if(nav==MenuNav::Confirm) {
        if(command_categories_) {
            command_tabs_.select(cursor_);command_categories_=false;
            cursor_=command_tabs_.cursor();command_scroll_=command_tabs_.scroll();restore_feature_tab();return;
        }
        const auto &row=rows[cursor_];
        const auto &tab=command_tabs_.tabs()[command_tabs_.index()];
        if(tab.id=="@ui-features") {
            feature_root_cursor_=cursor_;
            if(row.id==-1) {adjust(Item::Language,1,settings);return;}
            page_=static_cast<Page>(-100-row.id);features_from_tab_=true;cursor_=0;
            feature_resume_page_=page_;feature_resume_cursor_=0;return;
        }
        if(!row.heading && row.available && row.id>=0) {
            command_requested_=row.id;command_requested_present_=true;
        }
    }
    if(!command_categories_)command_tabs_.remember(cursor_,command_scroll_);
}

std::vector<FeatureMenu::CommandRow> FeatureMenu::command_rows() const
{
    std::vector<CommandRow> rows;
    rows.reserve(this->pad_commands_.size() + 12U);
    if (this->pad_commands_.empty() && !command_tabs_enabled_) {
        //! 握手の直後はまだ届いていない。**空のパネルを黙って出さない。**
        CommandRow row;
        row.heading = true;
        row.label = i18n::tr("hd2d.ui.feature-menu.the-core-has-not-sent-the-command-list");
        rows.push_back(std::move(row));
        return rows;
    }
    const auto group_label = [](const std::string &name) {
        return name.empty() ? std::string(i18n::tr("hd2d.ui.feature-menu.other")) : name;
    };
    /*
     * 命令の 1 行。**出せないものは選ばせない。**いまのキー配列にそのコマンドが無いと
     * `keys` が空で届く（`emit_core_command` もそこで諦める）。押しても何も
     * 起きない行を選べる状態で置くのがいちばん困る、というのが `bind_rows()` の
     * 判断でもある（あちらは注記を足している）。ここでは選べなくもする。
     */
    const auto command_row = [](const PadCommand &command) {
        CommandRow row;
        row.id = command.id;row.keys=command.keys;
        row.label = command.label_utf8;
        row.available = !command.keys.empty();
        if (!row.available) {
            row.label += i18n::tr("hd2d.ui.feature-menu.not-available-on-this-core");
            return row;
        }
        /*
         * **キーボードの綴りを添える**（例は「拾う（`,`）」）。
         *
         * 綴りは決め打ちしない——**コアごとに違う**（変愚の拾うは `,`、Sil-Q は `g`）し、
         * 同じコアでもキー配列（original / rogue）で変わる。`PadCommand::keys` は
         * **いま有効な配列で解決した結果**（`hd2d_app.cpp` の `seq_original` /
         * `seq_rogue` の選び分け）なので、これをそのまま綴れば必ず合う。
         *
         * 解決できない行（`keys` が空）には出さない。上で選べなくしてある。
         */

        return row;
    };

    if(command_tabs_enabled_) {
        const auto &tabs=command_tabs_.tabs();
        if(command_categories_) {
            for(const auto &tab:tabs) {CommandRow row;row.label=tab.label;row.available=true;rows.push_back(std::move(row));}
        } else if(!tabs.empty()) {
            if(tabs[command_tabs_.index()].id=="@inventory") {
                CommandRow loading;loading.heading=true;loading.label=i18n::current()=="ja"?"持ち物を読み込み中…":"Loading inventory...";rows.push_back(std::move(loading));return rows;
            }
            if(tabs[command_tabs_.index()].id=="@ui-features") {
                CommandRow language;language.id=-1;language.label=std::string(i18n::current()=="ja"?"表示言語: ":"Language: ")+i18n::current_endonym();rows.push_back(language);
                for(const auto page:root_pages())if(page!=Page::Commands) {
                    CommandRow row;row.id=-100-static_cast<int>(page);row.label=page_label(page);rows.push_back(row);
                }
                return rows;
            }
            for(const auto &command:pad_commands_) {
                if(command.group_utf8!=tabs[command_tabs_.index()].id)continue;
                auto row=command_row(command);
                if(!command.subgroup_utf8.empty())row.label=command.subgroup_utf8+" / "+row.label;
                rows.push_back(std::move(row));
            }
        }
        return rows;
    }
    /* ------------------------------------------------------ 1 段目: 分類の一覧 */
    if (this->command_group_.empty()) {
        /*
         * **並び替えはしない。** コアが送ってきた順がそのまま遊ぶ人の記憶の順である
         * （変愚なら通常メニューの並び）。出てきた順に 1 つずつ拾う。
         */
        std::vector<std::string> seen;
        for (const auto &command : this->pad_commands_) {
            if (std::find(seen.begin(), seen.end(), command.group_utf8) != seen.end()) {
                continue;
            }
            seen.push_back(command.group_utf8);
            CommandRow row;
            row.group = command.group_utf8;
            //! 分類は入口の節と同じ綴り（`… …`）で「潜る」ことを示す。
            row.label = group_label(command.group_utf8) + " …";
            /*
             * **中身が 1 つも出せない分類は選ばせない。** 潜った先が全部灰色のパネルは
             * 「壊れている」に見える。判断はここで済ませる。
             */
            row.available = false;
            for (const auto &one : this->pad_commands_) {
                if ((one.group_utf8 == command.group_utf8) && !one.keys.empty()) {
                    row.available = true;
                    break;
                }
            }
            if (!row.available) {
                row.label += i18n::tr("hd2d.ui.feature-menu.not-available-on-this-core");
            }
            rows.push_back(std::move(row));
        }
        return rows;
    }

    /* ---------------------------------- 2 段目: その分類の小分類と、束ねない命令 */
    if (this->command_subgroup_.empty()) {
        /*
         * **小分類は分類と同じ作りで並べる**（届いた順・重複は 1 つに畳む）。
         * 小分類を持たない命令は**その場に並べる**——分類ごとに段の数が変わってよい
         * （変愚の 9 分類は 1 つも小分類を送らないので、あちらは今までどおり 2 段）。
         */
        std::vector<std::string> seen;
        for (const auto &command : this->pad_commands_) {
            if (command.group_utf8 != this->command_group_) {
                continue;
            }
            if (command.subgroup_utf8.empty()) {
                rows.push_back(command_row(command));
                continue;
            }
            if (std::find(seen.begin(), seen.end(), command.subgroup_utf8) != seen.end()) {
                continue;
            }
            seen.push_back(command.subgroup_utf8);
            CommandRow row;
            row.subgroup = command.subgroup_utf8;
            row.label = group_label(command.subgroup_utf8) + " …";
            //! 分類の行と同じで、**中身が 1 つも出せない小分類は選ばせない**。
            row.available = false;
            for (const auto &one : this->pad_commands_) {
                if ((one.group_utf8 == this->command_group_) && (one.subgroup_utf8 == command.subgroup_utf8)
                    && !one.keys.empty()) {
                    row.available = true;
                    break;
                }
            }
            if (!row.available) {
                row.label += i18n::tr("hd2d.ui.feature-menu.not-available-on-this-core");
            }
            rows.push_back(std::move(row));
        }
        return rows;
    }

    /* ------------------------------------------- 3 段目: その小分類の命令 */
    for (const auto &command : this->pad_commands_) {
        if ((command.group_utf8 != this->command_group_) || (command.subgroup_utf8 != this->command_subgroup_)) {
            continue;
        }
        rows.push_back(command_row(command));
    }
    return rows;
}

std::vector<std::string> FeatureMenu::command_labels() const
{
    std::vector<std::string> out;
    for (const CommandRow &row : this->command_rows()) {
        out.push_back(command_display_label(row.keys,row.label));
    }
    return out;
}

/*
 * `from` の**次**から探す（`next_bindable_row` と同じ約束）。
 *
 * **入った所を決めるときは `from = -1` を渡すこと。** 割り当ての一覧は先頭が必ず
 * 見出しなので `0` から探して正しかったが、**コマンドの分類の一覧は先頭が選べる行**で、
 * `0` から探すと 1 つ目の分類を飛ばして 2 つ目に入る（2 段にした日に踏んだ）。
 */
int FeatureMenu::next_command_row(const std::vector<CommandRow> &rows, int from, int dir) const
{
    const int count = static_cast<int>(rows.size());
    if ((count <= 0) || (dir == 0)) {
        return from;
    }
    //! 一周して戻ってきたら諦める（選べる行が 1 つも無い表でも止まらない）。
    for (int step = 1; step <= count; ++step) {
        const int at = (((from + (dir * step)) % count) + count) % count;
        const CommandRow &row = rows[static_cast<std::size_t>(at)];
        //! 分類の行も命令の行も止まれる。止まれないのは案内の行と、出せないもの。
        if (!row.heading && row.available) {
            return at;
        }
    }
    return from;
}

/*
 * ------------------------------------------------------------ 2 列の動き（2026-08-23）
 *
 * 決めたこと「メニューは変愚同様 2 列にして縦長になりすぎないように」。
 * 並べ方は変愚と同じ**行優先**（`input-key-requester.cpp:401` の
 * `base_y + num / 2` / `base_x + (num % 2) * 24`）なので、
 * **↓ は添字 +2・→ は +1** になる。描く側（`draw_commands`）も同じ式で置く。
 *
 * **←→ は「潜る／戻る」ではなくなった。** 2 列にした以上、横は列を選ぶ手である。
 * 潜るのは決定、戻るのは取消——これも変愚の通常メニューと同じ約束である。
 */
int FeatureMenu::next_command_row_vertical(const std::vector<CommandRow> &rows, int from, int dir) const
{
    const int count = static_cast<int>(rows.size());
    if ((count <= 0) || (dir == 0)) {
        return from;
    }
    /*
     * 2 つずつ飛ぶ（＝同じ列を上下する）。**マスの数が偶数なら列から出ない**が、
     * 奇数だと 1 周したときに隣の列へ回り込む——変愚も同じ振る舞いで、
     * 端の 1 マスだけの段で詰まないためにこれでよい。
     */
    for (int step = 1; step <= count; ++step) {
        const int at = (((from + (dir * 2 * step)) % count) + count) % count;
        const CommandRow &row = rows[static_cast<std::size_t>(at)];
        if (!row.heading && row.available) {
            return at;
        }
    }
    //! 同じ列に止まれるマスが 1 つも無いときだけ、1 つずつの探し方へ落とす。
    return this->next_command_row(rows, from, dir);
}

int FeatureMenu::next_command_row_horizontal(const std::vector<CommandRow> &rows, int from, int dir) const
{
    const int count = static_cast<int>(rows.size());
    if ((count <= 0) || (dir == 0) || (from < 0) || (from >= count)) {
        return from;
    }
    /*
     * **同じ段の中だけ**動く（回り込まない）。押した向きへだけ動かす——変愚は
     * ←→ のどちらでも隣のマスへ跳ぶ（`process_right_left_cursor()`）が、
     * こちらの ←→ は入口の節では値を回す手なので、向きを無視すると
     * 「→ を押したのに左へ動いた」が混ざる。
     */
    const int at = (dir > 0) ? (((from % 2) == 0) ? (from + 1) : from) : (((from % 2) == 0) ? from : (from - 1));
    if ((at == from) || (at < 0) || (at >= count)) {
        return from;
    }
    const CommandRow &row = rows[static_cast<std::size_t>(at)];
    if (row.heading || !row.available) {
        return from; //!< 隣が選べないマスなら動かない（黙って別の段へ跳ばない）
    }
    return at;
}

void FeatureMenu::draw_command_tab_header(UiPaint &paint, TextOverlay &text, const UiLayout &layout,const RectPx &box,int &top_y) const {
    const auto skin=modern_skin(paint_theme_);
    const auto kLabelColor=paint_theme_>=0?skin.text:hd2d::kLabelColor;
    const auto kValueColor=paint_theme_>=0?skin.text:hd2d::kValueColor;
    const auto kPickedColor=paint_theme_>=0?skin.strong:hd2d::kPickedColor;
    const auto kHintColor=paint_theme_>=0?skin.muted:hd2d::kHintColor;
    const auto kHeadingColor=paint_theme_>=0?skin.strong:hd2d::kHeadingColor;
    const auto kFixedColor=paint_theme_>=0?skin.muted:hd2d::kFixedColor;
    const auto kEmptyColor=paint_theme_>=0?skin.muted:hd2d::kEmptyColor;

    const int cell_w=layout.cell_w,cell_h=layout.cell_h;
    if(command_tabs_enabled_) {
        const auto &tabs=command_tabs_.tabs();
        const int current=command_tabs_.index();
        std::vector<std::string> all_labels;for(const auto &tab:tabs)all_labels.push_back(tab.label);
        const auto [first,show]=menu_header_visible_range(text,box,all_labels,current,header_first_);
        std::vector<std::string> labels(all_labels.begin()+first,all_labels.begin()+first+show);
        header_first_=first;
        header_layout_=draw_menu_header(paint,text,box,top_y-6,labels,current-first,paint_theme_);
        top_y+=cell_h*2;
        if(command_categories_) {text.draw(box.x+cell_w,top_y,i18n::current()=="ja"?"分類を選択":"Choose category",kPickedColor);top_y+=cell_h*2;}
    }
}

void FeatureMenu::draw_commands(UiPaint &paint, TextOverlay &text, const UiLayout &layout,
    const RectPx &box, int top_y) const
{
    const auto skin=modern_skin(paint_theme_);
    const auto kLabelColor=paint_theme_>=0?skin.text:hd2d::kLabelColor;
    const auto kValueColor=paint_theme_>=0?skin.text:hd2d::kValueColor;
    const auto kPickedColor=paint_theme_>=0?skin.strong:hd2d::kPickedColor;
    const auto kHintColor=paint_theme_>=0?skin.muted:hd2d::kHintColor;
    const auto kHeadingColor=paint_theme_>=0?skin.strong:hd2d::kHeadingColor;
    const auto kFixedColor=paint_theme_>=0?skin.muted:hd2d::kFixedColor;
    const auto kEmptyColor=paint_theme_>=0?skin.muted:hd2d::kEmptyColor;

    const int cell_w = layout.cell_w;
    const int cell_h = layout.cell_h;
    const std::vector<CommandRow> rows = this->command_rows();
    const int count = static_cast<int>(rows.size());
    draw_command_tab_header(paint,text,layout,box,top_y);
    const int label_x = box.x + cell_w;
    //! 右端は送りの印（▲▼）の場所。`draw_binds()` と同じ空け方にする。
    const int gutter = cell_w * 3;
    const int full_width = (box.x + box.w) - gutter - label_x;
    const int list_w = command_tabs_enabled_?full_width*55/100:full_width;

    /*
     * **2 列に置く**（決めたこと 2026-08-23「メニューは変愚同様 2 列にして
     * 縦長になりすぎないように」）。並べ方は変愚と同じ**行優先**で、
     * 添字 `i` は段 `i / 2`・列 `i % 2` に置く（`input-key-requester.cpp:401`）。
     *
     * 案内の行（一覧が届いていないとき）だけは**1 列で幅いっぱい**に出す——
     * 文が長いので半分の幅では切れる。
     */
    const bool two_columns = !command_tabs_enabled_ && !rows.empty() && !((count == 1) && rows.front().heading);
    const int columns = two_columns ? 2 : 1;
    const int col_w = list_w / columns;

    //! 下の 2 行は操作説明に取っておく（一覧に食われると、出方が分からなくなる）。
    const int list_bottom = (box.y + box.h) - (cell_h * (paint_theme_>=0?5:3)) - (paint_theme_>=0?8:0);
    const int visible = std::max(1, (list_bottom - top_y) / cell_h);
    this->command_visible_rows_ = visible;
    //! **送りは「段」で数える**（マスではない）。2 列なら 1 段に 2 件入る。
    const int lines = (count + columns - 1) / columns;
    /*
     * **`cursor_` は選べる行が 1 つも無いと `-1` のままになる**
     * （`next_command_row()` は一周しても止まれる行が無ければ `from` をそのまま返す。
     * `open_commands()` は `from = -1` で呼ぶので、一覧が空で案内の 1 行だけのときはこれになる）。
     * ここで `-1` のまま割ると `cursor_line` も負になり、下の `scroll` の調整が
     * 負のまま素通りして、負の添字で `rows[]` を読みに行ってしまう
     * （`rows[static_cast<std::size_t>(-1)]` は範囲外——ここが実際に落ちていた道）。
     * 選べる行が無いときは先頭（0 段目）扱いにしておけば、以降の式は素直に 0 へ収まる。
     */
    const int cursor_line = std::max(this->cursor_, 0) / columns;

    //! **カーソルが窓の中に入るまで送る**（高さを知っているのはここだけ。`draw_binds()` と同じ）。
    int scroll = std::clamp(this->command_scroll_, 0, std::max(0, lines - visible));
    scroll = std::min(scroll, cursor_line);
    if (cursor_line >= (scroll + visible)) {
        scroll = cursor_line - visible + 1;
    }
    //! `draw_binds()` と同じ。位置が負になる経路が今後できても描画を守る。
    scroll = std::clamp(scroll, 0, std::max(0, lines - visible));
    this->command_scroll_ = scroll;
    command_details_rect_={};
    if(command_tabs_enabled_) {
        command_details_rect_={label_x+list_w+16,top_y,std::max(1,full_width-list_w-16),std::max(cell_h,list_bottom-top_y)};
        paint_modern_inset(paint,command_details_rect_,paint_theme_);
        paint.frame(command_details_focus_?command_details_rect_:RectPx{label_x,top_y,list_w,std::max(cell_h,list_bottom-top_y)},skin.trim,2);
        if(cursor_>=0&&cursor_<count) {
            const auto &chosen=rows[cursor_];std::string detail=chosen.label;
            if(!chosen.keys.empty())detail+="\n\n"+std::string(i18n::current()=="ja"?"キー: ":"Keys: ")+key_sequence_label(chosen.keys);
            if(!command_categories_&&!command_tabs_.tabs().empty())detail+="\n\n"+std::string(i18n::current()=="ja"?"分類: ":"Category: ")+command_tabs_.tabs()[command_tabs_.index()].label;
            const auto wrapped=wrap_ui_text(detail,std::max(1,command_details_rect_.w-24),[&](std::string_view part,int width){return text.fit_bytes(part,width);});
            const int n=std::max(1,(command_details_rect_.h-20)/cell_h);
            command_details_scroll_=std::clamp(command_details_scroll_,0,std::max(0,int(wrapped.size())-n));
            for(int i=command_details_scroll_;i<std::min<int>(wrapped.size(),command_details_scroll_+n);++i)
                text.draw(command_details_rect_.x+12,top_y+10+(i-command_details_scroll_)*cell_h,wrapped[i],kLabelColor,command_details_rect_.w-24);
            if(int(wrapped.size())>n){const int thumb=std::max(10,command_details_rect_.h*n/int(wrapped.size()));paint.rect({command_details_rect_.x+command_details_rect_.w-4,top_y+(command_details_rect_.h-thumb)*command_details_scroll_/std::max(1,int(wrapped.size())-n),3,thumb},skin.trim);}
        }
    }

    if(paint_theme_>=0){paint_modern_rule(paint,{label_x,top_y-5,list_w,1},paint_theme_);paint_modern_rule(paint,{label_x,list_bottom+4,list_w,1},paint_theme_);}
    int y = top_y;
    for (int line = scroll; (line < lines) && (line < (scroll + visible)); ++line) {
        for (int col = 0; col < columns; ++col) {
            const int i = (line * columns) + col;
            if (i >= count) {
                break;
            }
            const CommandRow &row = rows[static_cast<std::size_t>(i)];
            const bool picked = (i == this->cursor_);
            const int cell_x = label_x + (col * col_w);
            if(!row.heading) {
                painted_rows_.push_back({{cell_x-(cell_w/2),y,col_w,cell_h},i});
                painted_items_.push_back(ProbeItem{row.id>0?std::to_string(row.id):std::string(),command_display_label(row.keys,row.label),{},row.available,picked});
                if(ui_probe().enabled()) {
                    const RectPx r=menu_scaled_rect(painted_rows_.back().first,painted_scale_);
                    ui_probe().button(painted_items_.back().label,ProbeRect{r.x,r.y,r.w,r.h},row.available,picked,"menu-row",false,painted_items_.back().id);
                }
            }
            if (picked) {
                //! **マスの帯**（行いっぱいではない）。2 列で行を塗ると隣のマスまで光る。
                if(paint_theme_>=0)paint_modern_selection(paint,{cell_x-(cell_w/2),y,col_w,cell_h},paint_theme_);
                else paint.rect(RectPx{ cell_x - (cell_w / 2), y, col_w, cell_h }, PaintColor{ 0.95f, 0.85f, 0.30f, 0.18f });
            }
            const TextColor color = row.heading
                ? kHintColor
                : (picked ? kPickedColor : (row.available ? kLabelColor : kHintColor));
            //! 字はマスの幅で切る（隣の列へ食い込ませない）。
            text.draw(row.heading ? cell_x : (cell_x + cell_w), y, command_display_label(row.keys,row.label), color,
                col_w - (row.heading ? 0 : cell_w));
        }
        y += cell_h;
    }
    //! 送りの印。**上下に続きがあるかを出す**（無いと一覧の端が分からない）。
    if (scroll > 0) {
        text.draw(label_x+list_w-cell_w, top_y, "▲", kHintColor);
    }
    if ((scroll + visible) < lines) {
        text.draw(label_x+list_w-cell_w, y - cell_h, "▼", kHintColor);
    }
    /*
     * 下の案内は**いまの段に潜る行があるか**で選ぶ（段の数では選ばない）。
     * 小分類のある分類とない分類が混ざるので、「2 段目だから潜る」とは限らない。
     */
    bool can_descend = false;
    for (const auto &row : rows) {
        if (!row.group.empty() || !row.subgroup.empty()) {
            can_descend = true;
            break;
        }
    }
    if(command_tabs_enabled_) {
        if(!command_key_prefix_.empty())text.draw(label_x,(box.y+box.h)-(cell_h*5),
            (i18n::current()=="ja"?"キー入力: ":"Keys: ")+key_sequence_label(command_key_prefix_)+" ...",kHintColor,list_w);
        text.draw(label_x,(box.y+box.h)-(cell_h*4),command_categories_
            ? (i18n::current()=="ja"?"上下: 選択  決定: 分類へ移動  取消: 戻る":"Up/Down: select  Confirm: open category  Cancel: back")
            : (i18n::current()=="ja"?"上下: 選択 / スクロール  Tab・L2/R2: 一覧 / 説明  決定: 開始":"Up/Down: select / scroll  Tab/L2/R2: list / details  Confirm: run"),kHintColor,full_width);
        std::string hint=i18n::current()=="ja"?"分類切替: 左Shift/右Shift・L1/R1   分類一覧: パッドX・F2":"Change tab: Left/Right Shift or L1/R1   Categories: gamepad X or F2";
        for(int line=0;line<2&&!hint.empty();++line) {
            const auto take=text.fit_bytes(hint,full_width);if(!take)break;
            text.draw(label_x,box.y+box.h-cell_h*(3-line),hint.substr(0,take),kHintColor,full_width);hint.erase(0,take);
        }
        return;
    }
    text.draw(label_x, (box.y + box.h) - (cell_h * 2),
        can_descend ? i18n::tr("hd2d.ui.feature-menu.up-down-to-choose-enter-to-open")
                    : i18n::tr("hd2d.ui.feature-menu.up-down-to-choose-enter-to-run"),
        kHintColor);
}

int FeatureMenu::next_bindable_row(const std::vector<BindRow> &rows, int from, int dir) const
{
    const int count = static_cast<int>(rows.size());
    if ((count <= 0) || (dir == 0)) {
        return from;
    }
    //! 一周して戻ってきたら諦める（選べる行が 1 つも無い表でも止まらない）。
    for (int step = 1; step <= count; ++step) {
        const int at = (((from + (dir * step)) % count) + count) % count;
        if ((rows[static_cast<std::size_t>(at)].action != kActionNone) && !rows[static_cast<std::size_t>(at)].fixed) {
            return at;
        }
    }
    return from;
}

bool FeatureMenu::handle_bind_key(int keycode, int mods, Hd2dSettings &settings)
{
    if (!this->open_ || !this->waiting_ || (this->wait_action_ == kActionNone)) {
        return false;
    }
    if (key_is_reserved(keycode)) {
        /*
         * 十字・Enter・ESC。**ESC だけは「割り当ての消去」として食う**。
         * ほかは変更モードのまま無視する。ここで黙って降りると、押し間違えた人には
         * 「割り当てたつもりが変わっていない」に見える。
         */
        if (!key_is_escape(keycode)) {
            return true;
        }
        if (this->ext_binds_ != nullptr && this->wait_external_) {
            this->ext_binds_->clear(this->wait_action_, this->wait_col_); //!< コアごとの割り当て: 足した分を消して既定へ戻す
        } else if (this->ext_binds_ != nullptr && this->wait_col_ != 0) {
            this->ext_binds_->clear_ui_pad(this->wait_action_);
        } else if (this->wait_col_ == 0) {
            settings.key_binds.clear(this->wait_action_);
        } else {
            settings.pad.clear(this->wait_action_);
        }
        this->stop_waiting();
        return true;
    }
    if (this->wait_col_ != 0) {
        //! コントローラーのマスを狙っている最中のキー入力。**キーボードへ書き換えない。**
        return true;
    }
    if (this->wait_external_ && (this->ext_binds_ != nullptr)) {
        /*
         * コアごとの割り当て（DCSS。D33）のキーボードの列: コアへ渡るキーのコードで受ける。
         * Ctrl+英字は 1〜26。印字文字は、続いて届く `SDL_TEXTINPUT`（`handle_bind_text`）で受ける
         * （Shift や記号の解決を SDL に任せる）。それ以外（ファンクションキーなど）は受け取らず、変更モードのまま。
         */
        if (((mods & kModCtrl) != 0) && (keycode >= 'a') && (keycode <= 'z')) {
            const int code = (keycode - 'a') + 1;
            if ((code == 9) || (code == 10) || (code == 13)) {
                this->wait_note_ = (i18n::current() == "ja" ? "このキーは割り当てられません（字・数字・記号・Ctrl+英字だけ）" : "This key cannot be bound (letters, digits, symbols and Ctrl+letter only)");
                return true;
            }
            const std::string note = this->ext_binds_->assign_key(this->wait_action_, code);
            if (note.empty()) {
                this->stop_waiting();
            } else {
                this->wait_note_ = note;
            }
            return true;
        }
        if ((keycode > 0x20) && (keycode < 0x7f) && ((mods & (kModCtrl | kModAlt)) == 0)) {
            //! 字は `handle_bind_text` が受ける。`SDL_TEXTINPUT` が来なければ `update()` が、このキーの字から割り当てる。
            this->pending_key_sym_ = keycode;
            this->pending_key_mods_ = mods;
            this->pending_key_since_ms_ = this->wait_now_ms_;
            return true;
        }
        this->wait_note_ = (i18n::current() == "ja" ? "このキーは割り当てられません（字・数字・記号・Ctrl+英字だけ）" : "This key cannot be bound (letters, digits, symbols and Ctrl+letter only)");
        return true;
    }
    (void)settings.key_binds.assign(this->wait_action_, keycode, mods);
    this->stop_waiting();
    return true;
}

bool FeatureMenu::handle_bind_pad(PadInput input, int mods, Hd2dSettings &settings)
{
    if (!this->open_ || !this->waiting_ || (this->wait_action_ == kActionNone)) {
        return false;
    }
    if (this->wait_col_ == 0) {
        /*
         * キーボードのマスを狙っている最中のボタン。**捨てるが、黙って捨てない**
         * （気づいたこと 2026-08-14「割り当てで同時押しが入らない」。捨てていることが
         * 画面に出ていないと、押しが届いていないのと見分けが付かない）。
         */
        this->wait_note_ = pad_chord_display_name(input, mods) + i18n::tr("hd2d.ui.feature-menu.received")
            + i18n::tr("hd2d.ui.feature-menu.move-to-the-controller-column-with-the");
        return true;
    }
    if (this->ext_binds_ != nullptr) {
        //! コアごとの割り当て（DCSS。D33）: コアの枠（ボタン × 層）へ割り当てる。固定の枠・押しようのない枠は理由を返す。
        const std::string note = this->wait_external_ ? this->ext_binds_->assign_pad(this->wait_action_, input, mods)
                                                      : this->ext_binds_->assign_ui_pad(this->wait_action_, input, mods);
        if (!note.empty()) {
            this->wait_note_ = pad_chord_display_name(input, mods) + note;
            return true;
        }
        this->stop_waiting();
        return true;
    }
    /*
     * 固定のボタン（A / B / Back）は受け取らない。**変更モードのまま**にする
     * （B を押しても割り当たらないが、この画面では B が取消でもある。取消は
     * `handle()` の側が先に食うので、ここへは来ない）。
     */
    /*
     * **固定なのは単独押しのときだけ**（`PadBinds::assign` の注記）。
     * `LB＋A` は空いている枠なので受け取る。
     */
    if ((mods == 0) && pad_input_is_fixed(input)) {
        this->wait_note_ = pad_chord_display_name(input, mods)
            + i18n::tr("hd2d.ui.feature-menu.cannot-be-changed-on-its-own-it-can");
        return true;
    }
    /*
     * **割り当たらなかったら変更モードのまま**にする（`assign` が偽を返すのは
     * `LB＋LB` のような押しようのない枠を狙ったとき）。降りてしまうと、
     * 割り当たっていないのに割り当てたつもりになる。
     */
    if (!settings.pad.assign(this->wait_action_, input, mods)) {
        //! `LB＋LB` のような押しようのない枠。**なぜ入らないかを出す。**
        this->wait_note_ = pad_chord_display_name(input, mods) + i18n::tr("hd2d.ui.feature-menu.cannot-be-bound-to")
            + i18n::tr("hd2d.ui.feature-menu.lb-and-rb-cannot-be-the-other-half");
        return true;
    }
    this->stop_waiting();
    return true;
}

bool FeatureMenu::handle_bind_text(int ch, Hd2dSettings &)
{
    if (!this->open_ || !this->waiting_ || (this->ext_binds_ == nullptr) || !this->wait_external_ || (this->wait_col_ != 0)) {
        return false;
    }
    if ((ch <= 0x20) || (ch >= 0x7f)) {
        return false;
    }
    this->pending_key_sym_ = 0;
    const std::string note = this->ext_binds_->assign_key(this->wait_action_, ch);
    if (note.empty()) {
        this->stop_waiting();
    } else {
        this->wait_note_ = note;
    }
    return true;
}

bool FeatureMenu::focus_item(const std::string &label)
{
    for (int i = 0; i < static_cast<int>(Item::Count); ++i) {
        const auto item = static_cast<Item>(i);
        if (std::string(item_label(item)).find(label) == std::string::npos) {
            continue;
        }
        //! **その分類へ入る。**入口に居たままでは項目を触れない。
        this->page_ = page_of(item);
        const std::vector<Item> items = this->page_items();
        for (int at = 0; at < static_cast<int>(items.size()); ++at) {
            if (items[static_cast<std::size_t>(at)] == item) {
                this->cursor_ = at;
                return true;
            }
        }
        return false;
    }
    /*
     * 割り当ての一覧（行がコアから届くので `Item` に無い）。**見出しには止めない。**
     * **`Item` より後に探す。**「画面の作り」（`Item::Layout`）と
     * 「画面の作りを回す」（割り当ての行）のように、片方が片方を含む名前があるため。
     */
    {
        const std::vector<BindRow> rows = this->bind_rows();
        for (int at = 0; at < static_cast<int>(rows.size()); ++at) {
            const BindRow &row = rows[static_cast<std::size_t>(at)];
            if ((row.action == kActionNone) || row.fixed || (row.label.find(label) == std::string::npos)) {
                continue;
            }
            this->page_ = Page::Binds;
            this->cursor_ = at;
            this->bind_col_ = 0;
            return true;
        }
    }
    //! 分類そのものを指していることもある。
    const std::vector<Page> pages = root_pages();
    for (int at = 0; at < static_cast<int>(pages.size()); ++at) {
        if (std::string(page_label(pages[static_cast<std::size_t>(at)])).find(label) != std::string::npos) {
            this->page_ = Page::Root;
            this->cursor_ = at;
            return true;
        }
    }
    return false;
}

void FeatureMenu::handle(MenuNav nav, Hd2dSettings &settings)
{
    if (!this->open_) {
        return;
    }
    command_key_prefix_.clear();
    painted_rows_.clear();
    if (ui_break_enabled("menu")) {
        return; // `HD2D_BREAK_UI=menu`。**検査が FAIL になるのが正しい。**
    }
    if (this->waiting_) {
        /*
         * 割り当ての変更モード中。**ここでは何もしない。**
         * 十字も決定も「割り当てたいキー」でありうるので、
         * 受け取るのは `handle_bind_key()` / `handle_bind_pad()` だけにしてある
         * （ESC ＝ 消去・15 秒 ＝ 取消。`update()`）。
         */
        return;
    }
    if(!settings.modern_ui){settings_editing_=false;settings_details_focus_=false;}
    if(settings.modern_ui&&page_!=Page::Binds&&page_!=Page::Commands) {
        settings_editing_=false;
        const auto items=page_items();const bool on_item=cursor_>=0&&cursor_<int(items.size());
        if(nav==MenuNav::PreviousPane){settings_details_focus_=false;return;}
        if(nav==MenuNav::NextPane){settings_details_focus_=true;return;}
        if(nav==MenuNav::TogglePane){settings_details_focus_=!settings_details_focus_;return;}
        if(settings_details_focus_) {
            if(nav==MenuNav::Up)settings_details_scroll_=std::max(0,settings_details_scroll_-1);
            if(nav==MenuNav::Down)++settings_details_scroll_;
            if(nav==MenuNav::Cancel||nav==MenuNav::Confirm)settings_details_focus_=false;
            if(nav!=MenuNav::PreviousTab&&nav!=MenuNav::NextTab&&nav!=MenuNav::Categories)return;
            settings_details_focus_=false;
        }
        if(nav==MenuNav::Up||nav==MenuNav::Down)settings_details_scroll_=0;
        if(nav==MenuNav::Left||nav==MenuNav::Right) {
            if(on_item) {
                const auto item=items[cursor_];
                const bool action=item==Item::VpadButtonsEnter||item==Item::MinimapEnter||item==Item::EffectsEnter||item==Item::VrEnter
                    ||item==Item::FpsPanelsEnter||item==Item::VpadLayoutEdit||item==Item::VpadResetAll;
                if(!action&&!item_value(item,settings).empty())adjust(item,nav==MenuNav::Left?-1:1,settings);
            }
            return;
        }
        if(nav==MenuNav::Confirm&&on_item) {
            const auto item=items[cursor_];
            const bool action=item==Item::VpadButtonsEnter||item==Item::MinimapEnter||item==Item::EffectsEnter||item==Item::VrEnter
                ||item==Item::FpsPanelsEnter||item==Item::VpadLayoutEdit||item==Item::VpadResetAll;
            if(!action&&!item_value(item,settings).empty())return;
        }
    }

    /* ------------------------------------------------------ コマンドの一覧 */
    if(features_from_tab_) {
        if(nav==MenuNav::PreviousTab || nav==MenuNav::NextTab || nav==MenuNav::Categories) {
            feature_resume_page_=page_;feature_resume_cursor_=cursor_;
            features_from_tab_=false;page_=Page::Commands;cursor_=feature_root_cursor_;
            handle_command_tabs(nav,settings);return;
        }
        if(nav==MenuNav::Cancel && std::none_of(sub_page_links().begin(),sub_page_links().end(),[&](const auto &link){return link.child==page_;})) {
            features_from_tab_=false;feature_resume_page_=Page::Commands;page_=Page::Commands;cursor_=feature_root_cursor_;return;
        }
    }
    if(showing_command_tabs()) {handle_command_tabs(nav,settings);return;}
    if (this->page_ == Page::Commands) {
        const std::vector<CommandRow> rows = this->command_rows();
        const int count = static_cast<int>(rows.size());
        if (count <= 0) {
            this->page_ = Page::Root;
            return;
        }
        this->cursor_ = std::clamp(this->cursor_, 0, count - 1);
        {
            const CommandRow &now = rows[static_cast<std::size_t>(this->cursor_)];
            if (now.heading || !now.available) {
                //! 見出しに乗っていたら、まず選べる行へ降ろす（開いた直後がここ）。
                this->cursor_ = this->next_command_row(rows, this->cursor_, 1);
            }
        }
        switch (nav) {
        //! **2 列なので上下は 2 つずつ**（`next_command_row_vertical`）。
        case MenuNav::Up:
            this->cursor_ = this->next_command_row_vertical(rows, this->cursor_, -1);
            break;
        case MenuNav::Down:
            this->cursor_ = this->next_command_row_vertical(rows, this->cursor_, 1);
            break;
        //! 左右は**同じ段の隣の列**。潜るのは決定・戻るのは取消（変愚の通常メニューと同じ）。
        case MenuNav::Left:
            this->cursor_ = this->next_command_row_horizontal(rows, this->cursor_, -1);
            break;
        case MenuNav::Right:
            this->cursor_ = this->next_command_row_horizontal(rows, this->cursor_, 1);
            break;
        case MenuNav::Confirm: {
            const CommandRow &row = rows[static_cast<std::size_t>(this->cursor_)];
            if (row.heading || !row.available) {
                break;
            }
            if (!row.group.empty()) {
                //! **分類へ潜る**（1 段目 → 2 段目）。戻ったときのために位置を覚える。
                this->command_group_cursor_ = this->cursor_;
                this->command_group_ = row.group;
                this->command_subgroup_.clear();
                this->command_subgroup_cursor_ = 0;
                this->cursor_ = this->next_command_row(this->command_rows(), -1, 1);
                this->command_scroll_ = 0;
                break;
            }
            if (!row.subgroup.empty()) {
                //! **小分類へ潜る**（2 段目 → 3 段目。2026-08-23）。
                this->command_subgroup_cursor_ = this->cursor_;
                this->command_subgroup_ = row.subgroup;
                this->cursor_ = this->next_command_row(this->command_rows(), -1, 1);
                this->command_scroll_ = 0;
                break;
            }
            if (row.id <= 0) {
                break; //!< 命令でも分類でもない行（届いていないとき）は何もしない
            }
            /*
             * **ここでは実行しない。**送るのは `hd2d_app`（`take_command_request()`）。
             * パネルを閉じるのもあちら——コアへ流したあとで閉じないと、閉じた拍子の
             * 描き直しに紛れて「押したのに何も起きなかった」ように見える。
             */
            this->command_requested_ = row.id;
            break;
        }
        case MenuNav::Cancel:
            if (!this->command_subgroup_.empty()) {
                //! **1 段戻る**（3 段目 → 2 段目）。
                this->command_subgroup_.clear();
                this->cursor_ = this->command_subgroup_cursor_;
                this->command_scroll_ = 0;
                break;
            }
            if (!this->command_group_.empty()) {
                //! **1 段戻る**（2 段目 → 1 段目）。入口まで一気に戻さない。
                this->command_group_.clear();
                this->cursor_ = this->command_group_cursor_;
                this->command_scroll_ = 0;
                break;
            }
            if (this->commands_direct_) {
                /*
                 * **ボタン 1 つで直に開いたパネルは、取消で閉じる**（気づいたこと 2026-08-23
                 * 「メニューをキャンセルで抜けた際に機能メニューが立ち上がってしまう」）。
                 * 入口へ落とすと、命令を選ぶのをやめただけなのに**設定の画面が
                 * 立ち上がったように見える**——押していないものが出てくるのが困る。
                 */
                this->close();
                break;
            }
            this->page_ = Page::Root;
            this->cursor_ = this->root_cursor_;
            break;
        case MenuNav::None:
        default:
            break;
        }
        return;
    }

    /* ---------------------------------------------------- 割り当ての一覧 */
    if (this->page_ == Page::Binds) {
        const std::vector<BindRow> rows = this->bind_rows();
        const int count = static_cast<int>(rows.size());
        if (count <= 0) {
            this->page_ = Page::Root;
            return;
        }
        this->cursor_ = std::clamp(this->cursor_, 0, count - 1);
        if ((rows[static_cast<std::size_t>(this->cursor_)].action == kActionNone)
            || rows[static_cast<std::size_t>(this->cursor_)].fixed) {
            //! 見出し・固定行に乗っていたら、まず選べる行へ降ろす（開いた直後がここ）。
            this->cursor_ = this->next_bindable_row(rows, this->cursor_, 1);
        }
        switch (nav) {
        case MenuNav::Up:
            this->cursor_ = this->next_bindable_row(rows, this->cursor_, -1);
            break;
        case MenuNav::Down:
            this->cursor_ = this->next_bindable_row(rows, this->cursor_, 1);
            break;
        case MenuNav::Left:
            this->bind_col_ = 0;
            break;
        case MenuNav::Right:
            this->bind_col_ = 1;
            break;
        case MenuNav::Confirm: {
            const BindRow &row = rows[static_cast<std::size_t>(this->cursor_)];
            if ((row.action == kActionNone) || row.fixed) {
                break;
            }
            if (row.open_row) {
                //! 原作の割り当ての画面を開く行（DCSS。D33）。開いたらメニューは閉じる。
                if ((this->ext_binds_ != nullptr) && this->ext_binds_->open_screen(row.action)) {
                    this->close();
                }
                break;
            }
            if (((this->bind_col_ == 0) && row.no_key) || ((this->bind_col_ == 1) && row.no_pad)) {
                break; //!< その列は割り当てられない
            }
            this->waiting_ = true;
            this->wait_action_ = row.action;
            this->wait_external_ = row.external;
            this->wait_col_ = this->bind_col_;
            this->wait_since_ms_ = this->wait_now_ms_;
            this->wait_note_.clear(); //!< 前の変更のときの説明を持ち越さない
            break;
        }
        case MenuNav::Cancel:
            this->page_ = Page::Root;
            this->cursor_ = this->root_cursor_;
            break;
        case MenuNav::None:
        default:
            break;
        }
        return;
    }

    /* ------------------------------------------------------------ 入口 */
    if (this->page_ == Page::Root) {
        /*
         * 入口は**「値の項目（言語）→ 分類」の順**に並ぶ（2026-08-19）。
         * 「終了」はもう並ばない（終わらせる道はコアの通常の終了だけ）。
         */
        const std::vector<Item> root_values = root_items();
        const std::vector<Page> pages = root_pages();
        const int value_count = static_cast<int>(root_values.size());
        const int count = value_count + static_cast<int>(pages.size());
        this->cursor_ = std::clamp(this->cursor_, 0, count - 1);
        //! 値の行に居るか（居るなら左右は「値を動かす」であって「分類へ入る」ではない）。
        const bool on_value = (this->cursor_ < value_count);
        switch (nav) {
        case MenuNav::Up:
            this->cursor_ = ((this->cursor_ - 1) + count) % count;
            break;
        case MenuNav::Down:
            this->cursor_ = (this->cursor_ + 1) % count;
            break;
        case MenuNav::Left:
            if (on_value) {
                this->adjust(root_values[static_cast<std::size_t>(this->cursor_)], -1, settings);
            }
            break;
        case MenuNav::Right:
            if (on_value) {
                this->adjust(root_values[static_cast<std::size_t>(this->cursor_)], 1, settings);
                break;
            }
            [[fallthrough]];
        case MenuNav::Confirm:
            if (on_value) {
                if(!settings.modern_ui)this->adjust(root_values[static_cast<std::size_t>(this->cursor_)], 1, settings);
                break;
            }
            this->root_cursor_ = this->cursor_;
            this->page_ = pages[static_cast<std::size_t>(this->cursor_ - value_count)];
            this->cursor_ = 0; //!< 分類へ入ったら先頭から
            if (this->page_ == Page::Binds) {
                /*
                 * 一覧の先頭は見出し（「― システム」）なので、**入った所で選べる行まで降ろす**。
                 * 降ろさないと、最初の 1 回だけ見出しに帯が掛かった絵になる。
                 */
                const std::vector<BindRow> rows = this->bind_rows();
                this->cursor_ = this->next_bindable_row(rows, 0, 1);
                this->bind_col_ = 0;
                this->bind_scroll_ = 0;
            }
            if (this->page_ == Page::Commands) {
                //! **必ず 1 段目（分類の一覧）から**。前に開いた分類を覚えていると迷う。
                command_tabs_enabled_=settings.modern_ui;
                this->command_group_.clear();
                this->command_group_cursor_ = 0;
                this->command_subgroup_.clear();
                this->command_subgroup_cursor_ = 0;
                //! **入口から潜って来た。**取消は入口へ戻す（パネルごと閉じるのは直に開いたときだけ）。
                this->commands_direct_ = false;
                this->cursor_ = this->next_command_row(this->command_rows(), -1, 1);
                this->command_scroll_ = 0;
                enter_command_tabs();
            }
            break;
        case MenuNav::Cancel:
            this->close();
            break;
        case MenuNav::None:
        default:
            break;
        }
        return;
    }

    /* ---------------------------------------------------------- 分類の中 */
    const std::vector<Item> items = this->page_items();
    if (items.empty()) {
        this->page_ = Page::Root;
        return;
    }
    const int count = static_cast<int>(items.size());
    this->cursor_ = std::clamp(this->cursor_, 0, count - 1);
    const Item item = items[static_cast<std::size_t>(this->cursor_)];

    switch (nav) {
    case MenuNav::Up:
        this->cursor_ = ((this->cursor_ - 1) + count) % count;
        break;
    case MenuNav::Down:
        this->cursor_ = (this->cursor_ + 1) % count;
        break;
    case MenuNav::Left:
        this->adjust(item, -1, settings);
        break;
    case MenuNav::Right:
        this->adjust(item, 1, settings);
        break;
    case MenuNav::Confirm:
        //! **サブ画面と一回きりの操作は決定だけ**（左右で走ると誤爆する）。
        if (item == Item::VpadButtonsEnter) {
            this->page_ = Page::VpadButtons;
            this->cursor_ = 0;
            break;
        }
        if (item == Item::MinimapEnter) {
            this->page_ = Page::Minimap;
            this->cursor_ = 0;
            break;
        }
        if (item == Item::EffectsEnter) {
            this->page_ = Page::Effects;
            this->cursor_ = 0;
            break;
        }
        if (item == Item::VrEnter) {
            //! 立てる／畳むのは `hd2d_app.cpp`（メニューはセッションを知らない）。
            this->vr_toggle_requested_ = true;
            break;
        }
        if (item == Item::FpsPanelsEnter) {
            this->page_ = Page::FpsPanels;
            this->cursor_ = 0;
            break;
        }
        if (item == Item::VpadLayoutEdit) {
            this->vpad_edit_requested_ = true; //!< 開くのは `hd2d_app.cpp`（実体を知らない）
            break;
        }
        if (item == Item::VpadResetAll) {
            settings.vpad.reset_all();
            break;
        }
        if(!settings.modern_ui)this->adjust(item, 1, settings); // original UI keeps confirm-to-cycle
        break;
    case MenuNav::Cancel: {
        /*
         * 小節（ミニマップ・効果・出すパネル・ボタン表示設定）は**親の分類へ**、
         * それ以外は入口へ戻る。**入った項目を選んだ状態で戻す**（戻った先で
         * カーソルが先頭に飛ぶと、いま何を触っていたか分からなくなる）。
         *
         * @note 対を 1 か所の表にしてある。小節を足すたびに同じ形の `if` を継ぎ足すと、
         * 片方だけ書き忘れて「戻ると入口まで飛ぶ」が起きる（実際に起きうる形だった）。
         */
        for (const auto &sub : sub_page_links()) {
            if (this->page_ != sub.child) {
                continue;
            }
            this->page_ = sub.parent;
            this->cursor_ = 0;
            const std::vector<Item> parent_items = this->page_items();
            for (int i = 0; i < static_cast<int>(parent_items.size()); ++i) {
                if (parent_items[static_cast<std::size_t>(i)] == sub.enter) {
                    this->cursor_ = i;
                    break;
                }
            }
            return;
        }
        //! **入口へ戻る**（メニューごと閉じない）。閉じたいならもう一度取消。
        this->page_ = Page::Root;
        this->cursor_ = this->root_cursor_;
        break;
    }
    case MenuNav::None:
    default:
        break;
    }
}

/*!
 * @brief この頁の中身が要る幅（画素）。**言語ごとに変わるので実測する。**
 * @details 見出し・項目の名前・値・操作説明のうち**いちばん長いもの**に合わせる。
 * 名前と値は同じ行に並ぶので足し算、他は単独で比べる。
 */
int FeatureMenu::content_width(TextOverlay &text, const UiLayout &layout, const Hd2dSettings &settings) const
{
    const int cell_w = layout.cell_w;
    const int gap = cell_w * 2; //!< 名前と値のあいだ
    int widest = 0;
    const auto note = [&](int w) { widest = std::max(widest, w); };

    const bool root = (this->page_ == Page::Root);
    note(text.measure(root ? std::string(i18n::tr("hd2d.ui.feature-menu.feature-menu"))
                           : (std::string(i18n::tr("hd2d.ui.feature-menu.feature-menu-2")) + page_label(this->page_))));
    /*
     * 値は設定で変わるので、**いま出る値**ではなく `item_value()` を実際に呼んで測る。
     * 選ばれている行は `◀ 値 ▶` になるぶんだけ広いので、その飾りも足しておく
     * （測らずに済ませると、カーソルを合わせた行だけ溢れる）。
     *
     * @note **入口にも値の項目がある**（言語）。ここを分類の一覧だけで測っていると、
     * 入口の 1 行目が溢れても検査が黙って通る。
     */
    /*
     * **名前と値は別々の列である。**行ごとに「その行の名前＋その行の値」で測ると、
     * 名前の長い行と値の長い行が別々のときに足りなくなる——`draw()` は値の欄の左端を
     * **いちばん長い値**から取るので、名前の長い行はそこまでしか使えない
     * （`1 マスのメートル数` と `入（ダンジョンの壁が 2 段）` が別の行にあって溢れた。
     * `--ui-check` が捕まえた 2026-08-19）。**いちばん長い名前＋いちばん長い値**で測る。
     */
    const int decor = text.measure("◀  ▶");
    int widest_label = 0;
    int widest_value = 0;
    for (const auto item : this->page_items()) {
        widest_label = std::max(widest_label, text.measure(item_label(item)));
        widest_value = std::max(widest_value, text.measure(this->item_value(item, settings)));
    }
    if (widest_label > 0) {
        note(widest_label + gap + (widest_value > 0 ? widest_value + decor : 0));
    }
    if (root) {
        for (const auto page : root_pages()) {
            note(text.measure(std::string(page_label(page)) + " …"));
        }
    }
    note(text.measure(root ? i18n::tr("hd2d.ui.feature-menu.up-down-choose-left-right-change-enter")
                           : i18n::tr("hd2d.ui.feature-menu.up-down-to-choose-left-right-to-change")));
    return widest + (cell_w * 2); //!< 左右の余白
}

std::string FeatureMenu::structure_report()
{
    /*
     * **入れる先のある分類が空でないこと。**`page_of()` の書き忘れはここに出る
     * （落ちた項目は `Page::Count` へ行き、元の節が空になる）。
     * `Binds` は `Item` ではなく `BindRow` で並ぶので数えない。
     */
    std::vector<Page> pages = root_pages();
    /*
     * 小節は**入口の項目が並んでいるものだけ**見る。電話の Android には VR が無く、
     * `出すパネル …` ごと並べないので、`FpsPanels` が空なのは正しい
     * （ここを無条件に見ると、あちらで嘘の FAIL が出る）。
     */
    for (const auto &sub : sub_page_links()) {
        if (!item_is_absent(sub.enter)) {
            pages.push_back(sub.child);
        }
    }
    pages.push_back(Page::Root); //!< 入口の値の項目（言語）も 1 つは要る
    for (const Page page : pages) {
        if ((page == Page::Binds) || (page == Page::Commands)) {
            continue; //!< どちらも行がコアから届くので `Item` を持たない
        }
        if (items_of(page).empty()) {
            return std::string("分類「") + page_label(page) + "」に項目が 1 つもありません";
        }
    }
    /*
     * **迷子がいないこと。**上の検査は「節が空になる」形しか捕まえないので、
     * 節に 1 つでも残っていると素通りする。項目の総数でも突いておく。
     */
    int placed = 0;
    for (int i = 0; i < static_cast<int>(Page::Count); ++i) {
        placed += static_cast<int>(items_of(static_cast<Page>(i)).size());
    }
    //! **この実行体にある項目は全部どこかに並ぶ**こと（無いものは数から外す）。
    int expected = 0;
    for (int i = 0; i < static_cast<int>(Item::Count); ++i) {
        expected += item_is_absent(static_cast<Item>(i)) ? 0 : 1;
    }
    if (placed != expected) {
        char buf[192]{};
        std::snprintf(buf, sizeof(buf),
            "項目 %d 件のうち分類に並んだのは %d 件です（`page_of()` の書き忘れ）", expected, placed);
        return buf;
    }
    return {};
}

int FeatureMenu::page_count()
{
    return static_cast<int>(Page::Count);
}

bool FeatureMenu::focus_page(int index)
{
    if ((index < 0) || (index >= page_count())) {
        return false;
    }
    this->page_ = static_cast<Page>(index);
    this->cursor_ = 0;
    if (this->page_ == Page::Root) {
        //! 入口は「値の項目 → 分類」の順（`handle()` の入口の枝と同じ勘定）。
        this->cursor_ = root_item_count();
        return true;
    }
    if (this->page_ == Page::Binds) {
        //! 一覧の先頭は見出しなので、選べる行まで降ろす（`handle()` の入口の枝と同じ）。
        this->cursor_ = this->next_bindable_row(this->bind_rows(), 0, 1);
        this->bind_col_ = 0;
        this->bind_scroll_ = 0;
    }
    if (this->page_ == Page::Commands) {
        this->command_group_.clear();
        this->command_group_cursor_ = 0;
        this->command_subgroup_.clear();
        this->command_subgroup_cursor_ = 0;
        this->commands_direct_ = false; //!< 検査から名指しで開いた画。取消は入口へ
        this->cursor_ = this->next_command_row(this->command_rows(), -1, 1);
        this->command_scroll_ = 0;
    }
    return true;
}

std::string FeatureMenu::overflow_report(TextOverlay &text, const UiLayout &layout, const Hd2dSettings &settings) const
{
    if ((this->page_ == Page::Binds) || (this->page_ == Page::Commands)) {
        return {}; //!< 一覧は枠で決まり、行は送って見せる（溢れるのが前提）
    }
    const RectPx box = this->panel_rect(layout, &text, &settings);
    const int cell_w = layout.cell_w;
    const int x = box.x + cell_w;
    const int right = box.x + box.w - cell_w;

    const auto over = [&](const char *what, const std::string &s, int avail) -> std::string {
        const int w = text.measure(s);
        if (w <= avail) {
            return {};
        }
        char buf[256]{};
        std::snprintf(buf, sizeof(buf), "%s「%s」が %d 画素はみ出しています（幅 %d / 使える %d）",
            what, s.c_str(), w - avail, w, avail);
        return buf;
    };

    const auto items = this->page_items();
    if (this->page_ == Page::Root) {
        for (const auto page : root_pages()) {
            if (auto e = over("分類", std::string(page_label(page)) + " …", right - x); !e.empty()) {
                return e;
            }
        }
        //! **入口の値の項目（言語）も見る。**分類の一覧だけ見ていると 1 行目を素通りする。
        for (const auto item : items) {
            if (auto e = over("項目", item_label(item), right - x); !e.empty()) {
                return e;
            }
            if (auto e = over("値", "◀ " + this->item_value(item, settings) + " ▶", right - x); !e.empty()) {
                return e;
            }
        }
        return over("案内", i18n::tr("hd2d.ui.feature-menu.up-down-choose-left-right-change-enter"), right - x);
    }

    int widest_value = 0;
    for (const auto item : items) {
        widest_value = std::max(widest_value, text.measure(this->item_value(item, settings)));
    }
    if (widest_value > 0) {
        widest_value += text.measure("◀  ▶");
    }
    const int value_x = std::max(box.x + box.w - cell_w - widest_value, box.x + (box.w / 3));
    for (const auto item : items) {
        if (auto e = over("項目", item_label(item), value_x - x); !e.empty()) {
            return e;
        }
        const auto value = this->item_value(item, settings);
        if (value.empty()) {
            continue;
        }
        if (auto e = over("値", "◀ " + value + " ▶", right - value_x); !e.empty()) {
            return e;
        }
    }
    return over("案内", i18n::tr("hd2d.ui.feature-menu.up-down-to-choose-left-right-to-change"), right - x);
}

RectPx FeatureMenu::panel_rect(const UiLayout &layout, TextOverlay *text, const Hd2dSettings *settings) const
{
    const int cell_w = layout.cell_w;
    const int cell_h = layout.cell_h;
    if(command_tabs_enabled_)return fixed_menu_rect(layout);

    /*
     * **行がコアから届く 2 つ（`Binds` と `Commands`）は下の「枠で決める」道を通る。**
     * ここを通すと `page_items()` が空なのでパネルが 4 行ぶんに縮み、一覧が 1 行も見えない
     * （コマンドメニューを足した日に実際にそうなった）。
     */
    if ((this->page_ != Page::Binds) && (this->page_ != Page::Commands)) {
        /*
         * **1 画面に必ず収める。**分類ごとに分けたのは、項目を全部並べると低い窓で
         * 下が見切れたためである（遊ぶ人・2026-08-08）。ここで高さを実測して丸めておけば、
         * 分類が増えても「入らない」が起きない。
         */
        //! **入口は「値の項目＋分類」の 2 段**（2026-08-19）。片方だけ数えるとパネルが足りない。
        const int count = static_cast<int>(this->page_items().size())
            + ((this->page_ == Page::Root) ? static_cast<int>(root_pages().size()) : 0);
        const int rows = count + 4; // 見出し 1 ＋ 空き 1 ＋ 操作説明 2
        /*
         * **幅は中身を測って決める**（`text_overlay.h`「呼び出し側で何文字で切るかを
         * 決めると必ず溢れるか余る」）。前は 56 桁の直書きで、日本語なら収まっていたが
         * 英語にすると値が切れた（`Full-screen 3D (Landsca` で途切れた。遊ぶ人の画面）。
         * 同じ文でも言語で字数も字幅も変わるので、桁数を書いた時点で必ずどれかが溢れる。
         * 測る手段が無い（検査が寸法だけ見る）ときは、従来の 56 桁へ落とす。
         */
        /*
         * **検査の検査。**`HD2D_BREAK_MENU_WIDTH` を立てると、実測をやめて
         * 昔の「56 桁固定」へ戻す。`--ui-check` が FAIL になるのが正しい——
         * ここが PASS のままなら、はみ出しの検査が何も見ていないということ。
         */
        static const bool break_width = (std::getenv("HD2D_BREAK_MENU_WIDTH") != nullptr);
        const bool can_measure = (text != nullptr) && (settings != nullptr) && !break_width;
        const int width = std::min(layout.screen_w - (cell_w * 4),
            can_measure ? this->content_width(*text, layout, *settings) : (cell_w * 56));
        const int height = std::min(layout.screen_h - (cell_h * 2), (rows + 2) * cell_h);
        //! **原点を足す**（切り欠きを避けた範囲の真ん中へ。`ui_layout.h` の注記）。
        return RectPx{ layout.origin_x + ((layout.screen_w - width) / 2),
            layout.origin_y + ((layout.screen_h - height) / 2), width, height };
    }

    /*
     * 割り当ての一覧と**コマンドの一覧**。**メインの地図の枠に収める**（決めたこと 2026-08-11
     * 「操作割り当ての画面が大きすぎる。メインのマップパネルに収まるようにして」）。
     * 行はコアのコマンドの数だけあってどう畳んでも 1 画面には入らないので、
     * パネルの大きさは中身ではなく**枠**で決め、入らないぶんを送る（スクロール）。
     *
     * 置き場所を持っているのは `UiLayout` なので、**そこから採って自分では決めない**
     * （`Split` の地図の矩形をここで数え直すとずれる）。
     *
     * **枠いっぱいには広げない。**`Full` / `Hybrid` では `scene` が窓そのものなので、
     * 枠に合わせるだけだと全画面のパネルになる（＝「大きすぎる」が直らない）。上限を置いて、
     * 3 つの作りのどれでも同じくらいの大きさのパネルが地図の中に載るようにする。
     */
    const RectPx &area = layout.scene.empty()
        ? RectPx{ layout.origin_x, layout.origin_y, layout.screen_w, layout.screen_h }
        : layout.scene;
    /*
     * 最下段の帯には掛けない。下敷き（`UiPaint`）は文字より先に流すので、
     * パネルを重ねても下の**字はパネルの上に出てしまう**（パネルで隠せるのは色だけ）。
     */
    const int floor_y = layout.bottom_bar.empty() ? (layout.origin_y + layout.screen_h) : layout.bottom_bar.y;
    const int top = area.y + (cell_h / 2);
    const int bottom = std::min(area.y + area.h, floor_y) - (cell_h / 2);
    /*
     * 上限 26 行・78 桁。一覧が 20 行ほど見えて、地図がまだ周りに見えている大きさ。
     * **桁を削りすぎない**のも要件で、78 を下回ると下の案内（「割り当てたいキーを
     * 押してください（ESC で割り当てを消す / 15 秒で取消）」）が右端で切れる。
     */
    const int room_h = std::max(cell_h * 8, bottom - top);
    int h = std::min(room_h, cell_h * 26);
    /*
     * **コマンドの一覧は入るぶんだけに縮める**（決めたこと 2026-08-23
     * 「縦長になりすぎないように」）。2 列にして段が半分になったので、
     * ほとんどの段は 26 行の枠より小さい——枠のまま出すと、下半分が
     * 何も無い黒いパネルになる（実際にそうなった）。
     *
     * 入らないときは今までどおり枠いっぱいで、はみ出すぶんを送る。
     * `draw_commands()` は上下に 3 行ずつ（見出しと操作説明）取るので、
     * `段 + 6` 行あれば全部見える。**この式を変えるならあちらも変える。**
     */
    if (this->page_ == Page::Commands) {
        const int lines = command_tabs_enabled_ ? static_cast<int>(command_rows().size())+2+(command_categories_?2:0) : (static_cast<int>(this->command_rows().size()) + 1) / 2;
        h = std::clamp((lines + 6) * cell_h, cell_h * 8, h);
    }
    const int w = std::min(std::max(area.w - (cell_w * 2), cell_w * 40), cell_w * 78);
    return RectPx{ area.x + ((area.w - w) / 2), top + ((room_h - h) / 2), w, h };
}

void FeatureMenu::draw(UiPaint &paint,TextOverlay &text,const UiLayout &layout,const Hd2dSettings &settings) const {
    MenuScaleScope scope(paint,text,settings.modern_ui?menu_font_scale(settings.modern_font_level):1.f);
    painted_scale_=scope.scale;
    painted_rows_.clear();
    painted_items_.clear();
    const UiProbeLayer probe_layer("menu");
    draw_logical(paint,text,menu_logical_layout(layout,scope.scale,&text),settings);
    if(ui_probe().enabled()&&open_) {
        //! 検査用の問い合わせ：開いている機能メニュー（コマンドの一覧・設定）。行の矩形は上で部品として記録済み。
        ProbeWindow w;
        w.kind="feature-menu";
        w.stage=showing_commands()?"commands":"settings";
        if(showing_command_tabs()&&!command_tabs_.tabs().empty())w.title=command_tabs_.tabs()[command_tabs_.index()].label;
        else w.title=page_label(page_);
        w.items=painted_items_;
        {const RectPx r=menu_scaled_rect(panel_rect(menu_logical_layout(layout,scope.scale,&text),&text,&settings),scope.scale);
         w.has_rect=true;w.rect=ProbeRect{r.x,r.y,r.w,r.h};}
        for(std::size_t i=0;i<painted_rows_.size();++i)if(painted_rows_[i].second==cursor_)w.selected=static_cast<int>(i);
        if(page_==Page::Binds)for(std::size_t i=0;i<painted_items_.size();++i)if(painted_items_[i].selected)w.selected=static_cast<int>(i);
        ui_probe().window(std::move(w));
    }
}
void FeatureMenu::draw_logical(UiPaint &paint, TextOverlay &text, const UiLayout &layout, const Hd2dSettings &settings) const
{
    paint_theme_=settings.modern_ui?settings.modern_theme:-1;
    const auto skin=modern_skin(paint_theme_);
    const auto kLabelColor=paint_theme_>=0?skin.text:hd2d::kLabelColor;
    const auto kValueColor=paint_theme_>=0?skin.text:hd2d::kValueColor;
    const auto kPickedColor=paint_theme_>=0?skin.strong:hd2d::kPickedColor;
    const auto kHintColor=paint_theme_>=0?skin.muted:hd2d::kHintColor;
    const auto kHeadingColor=paint_theme_>=0?skin.strong:hd2d::kHeadingColor;
    const auto kFixedColor=paint_theme_>=0?skin.muted:hd2d::kFixedColor;
    const auto kEmptyColor=paint_theme_>=0?skin.muted:hd2d::kEmptyColor;

    if (!this->open_) {
        return;
    }
    const int cell_h = layout.cell_h;
    const std::vector<Page> pages = root_pages();
    const std::vector<Item> items = this->page_items();
    const bool root = (this->page_ == Page::Root);
    const bool binds = (this->page_ == Page::Binds);
    //! 入口は**値の項目が先・分類が後**（`handle()` の入口の枝と同じ勘定）。
    const int item_count = static_cast<int>(items.size());
    const int count = item_count + (root ? static_cast<int>(pages.size()) : 0);

    //! パネルの大きさと置き場所は `panel_rect()`（検査もそこを見る。作り方を 2 か所に置かない）。
    const RectPx box = this->panel_rect(layout, &text, &settings);

    PanelStyle style = solid_panel_style();
    /*
     * **メニューだけは VR でも薄く敷く。**「VR の板は背景無し」（`panel_transparent()`）は
     * 遊んでいる最中の見え方の話で、設定を読む画面まで透かすと世界の絵と字が重なって
     * 選べない。透かして困るのはここだけなので、ここだけ例外にする。
     */
    style.fill = PaintColor{ 0.06f, 0.07f, 0.11f, panel_transparent() ? 0.72f : 0.96f };
    style.border = PaintColor{ 0.55f, 0.62f, 0.78f, 1.f };
    style.border_px = 2;
    if(paint_theme_>=0)paint_modern_window(paint,box,paint_theme_);else paint.panel(box, style);

    const int x = box.x + layout.cell_w;
    /*
     * 値の欄の左端。**いちばん長い値に合わせて右から取る。**
     * 前は「右から 24 桁」の直書きで、日本語なら収まるが英語では
     * `Full-screen 3D (Landscape)` が切れた（26 字）。字数も字幅も言語で変わる。
     * 選ばれている行の `◀ 値 ▶` の飾りぶんも足しておく。
     */
    int widest_value = 0;
    for (const auto item : items) {
        widest_value = std::max(widest_value, text.measure(this->item_value(item, settings)));
    }
    if (widest_value > 0) {
        widest_value += text.measure("◀  ▶");
    }
    const int pane_right=paint_theme_>=0&&!binds&&page_!=Page::Commands?box.x+box.w*55/100:box.x+box.w;
    const int value_x = std::max(pane_right - layout.cell_w - std::min(widest_value,(pane_right-box.x)/2),
        box.x + (box.w / 3)); //!< 名前の側が潰れない下限
    int y = box.y + cell_h;
    if(showing_command_tabs()) {draw_commands(paint,text,layout,box,y);return;}
    if(features_from_tab_)draw_command_tab_header(paint,text,layout,box,y);

    if (root) {
        text.draw(x, y, i18n::tr("hd2d.ui.feature-menu.feature-menu"), kPickedColor);
    } else {
        text.draw(x, y, std::string(i18n::tr("hd2d.ui.feature-menu.feature-menu-2")) + page_label(this->page_), kPickedColor);
        if (this->page_ == Page::SubPanels) {
            text.draw(x, y + cell_h, i18n::tr("hd2d.ui.feature-menu.sub-panels-help"), kHintColor,
                pane_right - x - layout.cell_w);
        }
    }
    y += cell_h * 2;

    if (binds) {
        this->draw_binds(paint, text, layout, settings, box, y);
        return;
    }
    if (this->page_ == Page::Commands) {
        /*
         * **いまどの分類に居るかを見出しに足す**（`機能メニュー ＞ コマンド ＞ 行動`）。
         * 2 段になった以上、これが無いと「戻る」で何段戻るのか分からない。
         */
        if (!this->command_group_.empty()) {
            const int title_y = y - (cell_h * 2);
            std::string trail = std::string(i18n::tr("hd2d.ui.feature-menu.feature-menu-2"))
                + page_label(this->page_) + i18n::tr("hd2d.ui.feature-menu.breadcrumb-separator")
                + this->command_group_;
            //! 3 段目まで潜ったら小分類も足す（`… ＞ コマンド ＞ 行動 ＞ 戦う`）。
            if (!this->command_subgroup_.empty()) {
                trail += i18n::tr("hd2d.ui.feature-menu.breadcrumb-separator") + this->command_subgroup_;
            }
            text.draw(x, title_y, trail, kPickedColor, box.w - (layout.cell_w * 2));
        }
        this->draw_commands(paint, text, layout, box, y);
        return;
    }
    settings_details_rect_={};settings_minus_={};settings_plus_={};
    if(paint_theme_>=0) {
        settings_details_rect_={pane_right+12,y,std::max(1,box.x+box.w-pane_right-28),std::max(cell_h,box.y+box.h-cell_h*4-y)};
        paint_modern_inset(paint,settings_details_rect_,paint_theme_);
        if(!settings_editing_)paint.frame(settings_details_focus_?settings_details_rect_:RectPx{box.x+8,y,pane_right-box.x-12,settings_details_rect_.h},skin.trim,2);
        std::string detail;
        if(cursor_>=0&&cursor_<item_count)detail=std::string(item_label(items[cursor_]))+"\n\n"+item_value(items[cursor_],settings);
        else if(root&&cursor_>=item_count&&cursor_-item_count<int(pages.size()))detail=page_label(pages[cursor_-item_count]);
        const auto lines=wrap_ui_text(detail,std::max(1,settings_details_rect_.w-24),[&](std::string_view part,int width){return text.fit_bytes(part,width);});
        const int n=std::max(1,(settings_details_rect_.h-20)/cell_h);
        settings_details_scroll_=std::clamp(settings_details_scroll_,0,std::max(0,int(lines.size())-n));
        for(int i=settings_details_scroll_;i<std::min<int>(lines.size(),settings_details_scroll_+n);++i)
            text.draw(settings_details_rect_.x+12,y+10+(i-settings_details_scroll_)*cell_h,lines[i],kLabelColor,settings_details_rect_.w-24);
        if(int(lines.size())>n){const int thumb=std::max(10,settings_details_rect_.h*n/int(lines.size()));paint.rect({settings_details_rect_.x+settings_details_rect_.w-4,y+(settings_details_rect_.h-thumb)*settings_details_scroll_/std::max(1,int(lines.size())-n),3,thumb},skin.trim);}
    }

    const int visible=paint_theme_>=0?std::max(1,(box.y+box.h-cell_h*4-y)/cell_h):count;
    page_scroll_=std::clamp(page_scroll_,0,std::max(0,count-visible));
    if(cursor_<page_scroll_)page_scroll_=cursor_;if(cursor_>=page_scroll_+visible)page_scroll_=cursor_-visible+1;
    if(paint_theme_>=0){paint_modern_rule(paint,{box.x+8,y-5,pane_right-box.x-16,1},paint_theme_);paint_modern_rule(paint,{box.x+8,y+std::min(count,visible)*cell_h+4,pane_right-box.x-16,1},paint_theme_);}
    for (int i = page_scroll_; i < std::min(count,page_scroll_+visible); ++i) {
        painted_rows_.push_back({{box.x+8,y,pane_right-box.x-16,cell_h},i});
        painted_items_.push_back(ProbeItem{std::string(),(root && (i >= item_count)) ? std::string(page_label(pages[static_cast<std::size_t>(i - item_count)]))
                                                              : std::string(item_label(items[static_cast<std::size_t>(i)])),{},true,i==this->cursor_});
        if(ui_probe().enabled()) {
            const RectPx r=menu_scaled_rect(painted_rows_.back().first,painted_scale_);
            ui_probe().button(painted_items_.back().label,ProbeRect{r.x,r.y,r.w,r.h},true,i==this->cursor_,"menu-row");
        }
        const bool picked = (i == this->cursor_);
        if (picked) {
            if(paint_theme_>=0)paint_modern_selection(paint,{box.x+8,y,pane_right-box.x-16,cell_h},paint_theme_);
            else paint.rect(RectPx{ box.x + 2, y, box.w - 4, cell_h }, PaintColor{ 0.95f, 0.85f, 0.30f, 0.18f });
        }
        if (root && (i >= item_count)) {
            const std::string label
                = std::string(page_label(pages[static_cast<std::size_t>(i - item_count)])) + " …";
            text.draw(x, y, label, picked ? kPickedColor : kLabelColor, pane_right-box.x - (layout.cell_w * 2));
            y += cell_h;
            continue;
        }
        const Item item = items[static_cast<std::size_t>(i)];
        text.draw(x, y, item_label(item), picked ? kPickedColor : kLabelColor, value_x - x);
        const std::string value = item_value(item, settings);
        if (!value.empty()) {
            const std::string shown = picked&&paint_theme_<0 ? ("◀ " + value + " ▶") : value;
            text.draw(value_x, y, shown, picked ? kPickedColor : kValueColor, pane_right - value_x - layout.cell_w);
        }
        y += cell_h;
    }

    y += cell_h / 2;
    if(paint_theme_>=0) {
        y=box.y+box.h-cell_h*3;
        if(settings_editing_&&cursor_>=0&&cursor_<item_count) {
            settings_minus_={box.x+16,y-6,40,cell_h+10};settings_plus_={box.x+box.w-56,y-6,40,cell_h+10};
            paint_modern_inset(paint,settings_minus_,paint_theme_);paint_modern_inset(paint,settings_plus_,paint_theme_);
            text.draw(settings_minus_.x+12,y,"−",kPickedColor,24);text.draw(settings_plus_.x+12,y,"+",kPickedColor,24);
            text.draw(box.x+68,y,item_value(items[cursor_],settings),kValueColor,box.w-136);
        }
        text.draw(x,box.y+box.h-cell_h,
            i18n::current()=="ja"?"上下: 選択 / スクロール  左右: 値選択  Tab・L2/R2: 一覧 / 説明  決定: 開く / 実行":"Up/Down: select / scroll  Left/Right: choose value  Tab/L2/R2: list / details  Confirm: open / run",
            kHintColor,box.w-32);
        return;
    }
    if (root) {
        //! **←→ も要る**（入口の 1 行目は言語という値の項目である。2026-08-19）。
        text.draw(x, y, i18n::tr("hd2d.ui.feature-menu.up-down-choose-left-right-change-enter"), kHintColor);
    } else {
        text.draw(x, y, i18n::tr("hd2d.ui.feature-menu.up-down-to-choose-left-right-to-change"), kHintColor);
        if (this->page_ == Page::SubPanels) {
            text.draw(x, y + cell_h, i18n::tr("hd2d.ui.feature-menu.the-bottom-row-dividers-can-also-be"), kHintColor);
        }
        if (this->page_ == Page::VirtualPad) {
            text.draw(x, y + cell_h, i18n::tr("hd2d.ui.feature-menu.button-layout-opens-an-editing-screen"), kHintColor);
        }
    }
}

void FeatureMenu::draw_binds(UiPaint &paint, TextOverlay &text, const UiLayout &layout,
    const Hd2dSettings &settings, const RectPx &box, int top_y) const
{
    const auto skin=modern_skin(paint_theme_);
    const auto kLabelColor=paint_theme_>=0?skin.text:hd2d::kLabelColor;
    const auto kValueColor=paint_theme_>=0?skin.text:hd2d::kValueColor;
    const auto kPickedColor=paint_theme_>=0?skin.strong:hd2d::kPickedColor;
    const auto kHintColor=paint_theme_>=0?skin.muted:hd2d::kHintColor;
    const auto kHeadingColor=paint_theme_>=0?skin.strong:hd2d::kHeadingColor;
    const auto kFixedColor=paint_theme_>=0?skin.muted:hd2d::kFixedColor;
    const auto kEmptyColor=paint_theme_>=0?skin.muted:hd2d::kEmptyColor;

    const int cell_w = layout.cell_w;
    const int cell_h = layout.cell_h;
    const std::vector<BindRow> rows = this->bind_rows();
    const int count = static_cast<int>(rows.size());

    /*
     * 列。**マスの左端を先に決めてから**中身を書く（枠と字を同じ矩形から作るので、
     * 「囲ってあるのに別の字が入っている」が起きない。必守制約 3 の親戚）。
     *
     * 幅は枠から割り出す（決め打ちにしない）。地図の枠に収めたぶん
     * 横が狭くなりうるので、22 桁を当て続けると操作名の欄が消える。
     *
     * 右端の `gutter` は**送りの印（▲▼）の場所**である。ここを空けておかないと
     * 印が「コントローラー」の見出しに重なる（実際に重なった）。
     */
    const int gutter = cell_w * 3;
    const int col_w = std::clamp((box.w - (cell_w * 2) - gutter) / 5, cell_w * 16, cell_w * 22);
    const int col_x[2] = { (box.x + box.w) - gutter - (col_w * 2), (box.x + box.w) - gutter - col_w };
    const int label_x = box.x + cell_w;
    const int label_w = col_x[0] - label_x - cell_w;

    text.draw(label_x, top_y, i18n::tr("hd2d.ui.feature-menu.action"), kHintColor, label_w);
    text.draw(col_x[0], top_y, i18n::tr("hd2d.ui.feature-menu.keyboard"), kHintColor, col_w);
    text.draw(col_x[1], top_y, i18n::tr("hd2d.ui.feature-menu.controller"), kHintColor, col_w);
    int y = top_y + (cell_h * 3 / 2);

    //! 下の 2 行は操作説明に取っておく（一覧に食われると、出方が分からなくなる）。
    const int list_bottom = (box.y + box.h) - (cell_h * (paint_theme_>=0?5:3)) - (paint_theme_>=0?8:0);
    const int visible = std::max(1, (list_bottom - y) / cell_h);
    this->bind_visible_rows_ = visible;

    /*
     * **カーソルが窓の中に入るまで送る。**行数はコアからの一覧で変わり、窓の高さも
     * 動くので、送り量をここで詰め直すのがいちばん確か（`handle()` は高さを知らない）。
     */
    if (this->cursor_ < this->bind_scroll_) {
        this->bind_scroll_ = this->cursor_;
    }
    if (this->cursor_ >= (this->bind_scroll_ + visible)) {
        this->bind_scroll_ = (this->cursor_ - visible) + 1;
    }
    this->bind_scroll_ = std::clamp(this->bind_scroll_, 0, std::max(0, count - visible));

    if(paint_theme_>=0){paint_modern_row_rules(paint,{box.x+8,y,box.w-16,visible*cell_h},cell_h,paint_theme_);paint_modern_rule(paint,{box.x+8,y-5,box.w-16,1},paint_theme_);paint_modern_rule(paint,{box.x+8,list_bottom+4,box.w-16,1},paint_theme_);}
    for (int i = this->bind_scroll_; (i < count) && (i < (this->bind_scroll_ + visible)); ++i) {
        const BindRow &row = rows[static_cast<std::size_t>(i)];
        const bool picked = (i == this->cursor_);
        const bool heading = (row.action == kActionNone) && !row.fixed;
        if (ui_probe().enabled() && !heading) {
            //! 検査用の問い合わせ: 見えている行（番号・名前・キーボードの欄・コントローラーの欄・選ばれているか）。
            painted_items_.push_back(ProbeItem{ std::to_string(row.action), row.label,
                { this->bind_cell_value(row, 0, settings), this->bind_cell_value(row, 1, settings) }, true, picked });
        }
        if (picked) {
            if(paint_theme_>=0)paint_modern_selection(paint,{box.x+8,y,box.w-16,cell_h},paint_theme_);
            else paint.rect(RectPx{ box.x + 2, y, box.w - 4, cell_h }, PaintColor{ 0.95f, 0.85f, 0.30f, 0.14f });
        }
        const TextColor label_color = heading ? kHeadingColor : (row.fixed ? kFixedColor : (picked ? kPickedColor : kLabelColor));
        text.draw(label_x, y, row.label, label_color, label_w);
        if (!heading) {
            for (int col = 0; col < 2; ++col) {
                const std::string value = this->bind_cell_value(row, col, settings);
                const bool empty = (value == kUnassigned);
                const TextColor value_color = row.fixed ? kFixedColor
                    : (empty ? kEmptyColor : (picked && (col == this->bind_col_) ? kPickedColor : kValueColor));
                text.draw(col_x[col] + (cell_w / 2), y, value, value_color, col_w - cell_w);
                if (picked && (col == this->bind_col_) && !row.fixed) {
                    /*
                     * **カーソルはマスを囲む枠**（決めたこと 2026-08-11「＞で表しているが
                     * 実機では□枠」）。変更モードの間は色を変えて、待っていることを示す。
                     */
                    const PaintColor frame_color = paint_theme_>=0?modern_skin(paint_theme_).trim:
                        (this->waiting_ ? PaintColor{ 1.f, 0.55f, 0.35f, 1.f } : PaintColor{ 0.95f, 0.85f, 0.30f, 1.f });
                    paint.frame(RectPx{ col_x[col], y - 1, col_w, cell_h + 2 }, frame_color, 2);
                }
            }
        }
        y += cell_h;
    }

    //! **端に続きがあることを出す**（無いとスクロールできることに気づけない）。
    if (this->bind_scroll_ > 0) {
        text.draw((box.x + box.w) - gutter, top_y, "▲", kHintColor);
    }
    if ((this->bind_scroll_ + visible) < count) {
        text.draw((box.x + box.w) - gutter, list_bottom, "▼", kHintColor);
    }

    int hint_y = (box.y + box.h) - (cell_h * 2) - (cell_h / 2);
    if (this->waiting_) {
        const std::uint32_t elapsed = this->wait_now_ms_ - this->wait_since_ms_;
        const int left = static_cast<int>((kBindWaitTimeoutMs - std::min(elapsed, kBindWaitTimeoutMs)) / 1000U);
        char buf[192]{};
        std::snprintf(buf, sizeof(buf),
            (this->wait_col_ == 0) ? i18n::tr("hd2d.ui.feature-menu.press-the-key-you-want-to-bind-esc")
                                   : i18n::tr("hd2d.ui.feature-menu.press-the-button-you-want-to-bind-esc"),
            left);
        text.draw(box.x + layout.cell_w, hint_y, buf, kPickedColor, box.w - (cell_w * 2));
        /*
         * **受け取ったのに入らなかったものを出す**。
         * 上の行の下へ重ねる（変更モードの間しか出ないので、割り付けは食わない）。
         */
        if (!this->wait_note_.empty()) {
            text.draw(box.x + layout.cell_w, hint_y + cell_h, this->wait_note_, kEmptyColor,
                box.w - (cell_w * 2));
        }
    } else {
        text.draw(box.x + layout.cell_w, hint_y,
            i18n::tr("hd2d.ui.feature-menu.up-down-to-choose-an-action-left-right"),
            kHintColor, box.w - (cell_w * 2));
    }
    hint_y += cell_h;
    if ((this->ext_binds_ != nullptr) && this->waiting_ && !this->wait_note_.empty()) {
        return; //!< 受け取れなかった理由を、下の行へ重ねて出している間は、パッドの案内の行を出さない（字が重なる）
    }
    //! **繋がっているかを必ず出す**（割り当てられるのに反応しない、が分からない）。
    const std::string pad_note = this->pad_connected_
        ? (i18n::tr("hd2d.ui.feature-menu.pad") + this->pad_name_)
        : std::string(i18n::tr("hd2d.ui.feature-menu.no-pad-is-connected"));
    text.draw(box.x + layout.cell_w, hint_y,
        i18n::tr("hd2d.ui.feature-menu.the-d-pad-enter-esc-a-b-and") + pad_note, kHintColor, box.w - (cell_w * 2));
}

} // namespace hd2d
