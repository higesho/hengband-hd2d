#pragma once
#include "frame/game_frame.h"
#include "frame/protocol_messages.h"
#include "ui/ui_cursor.h"
#include "ui/ui_layout.h"
#include "ui/ui_image.h"
#include "ui/menu_tabs.h"
#include "ui/menu_header.h"
#include <algorithm>
#include <cstddef>
#include <unordered_map>
#include <chrono>
#include <memory>
namespace hd2d {
class UiPaint;class TextOverlay;class TileCatalog;class SlabLibrary;class VoxelRenderer;
struct ModernPaintedCell {RectPx rect;std::string text;bool selected;std::string target;};
inline size_t message_suffix_prefix_overlap(const std::vector<std::string> &previous,const std::vector<std::string> &current) {
    size_t overlap=std::min(previous.size(),current.size());
    while(overlap>0&&!std::equal(previous.end()-static_cast<std::ptrdiff_t>(overlap),previous.end(),current.begin()))--overlap;
    return overlap;
}
class ModernStoreUi {
public:
    void sync(const GameFrame &frame,bool requested);
    void set_font_level(int level){font_level_=std::clamp(level,0,4);}
    bool active() const {return enabled_&&window_.version>=2&&!window_.stage.empty()&&!(held_&&std::chrono::steady_clock::now()>=hold_until_);}
    bool enabled() const {return enabled_;}
    /*!
     * 文脈メニューを出すアンカー（地図のクリック・長押しの画面の点。窓の座標の画素。AH-58）。
     * コアが `menu_over_map`（地図の上に重なる小さな箱）と言った選択肢の窓は、アンカーがあれば窓いっぱいではなく、
     * その点の近くのポップアップで出す（画面の端で切れないよう寄せる）。アンカーを渡さないコア（地図のクリックを
     * 申告しないコア）の窓は今までどおり。
     */
    void set_context_anchor(bool valid,int x,int y){context_anchor_valid_=valid;context_anchor_x_=x;context_anchor_y_=y;}
    //! いまの窓をアンカーの近くのポップアップで描いているか（アンカーがあり、コアが `menu_over_map` と言う選択肢の窓）。
    bool context_popup_active() const;
    //! 小さな窓の矩形（窓の座標の画素。描いた直後の値。出していなければ空）。検査用。
    RectPx context_popup_box() const;
    bool item_window() const {return window_.kind=="store"||window_.kind=="home"||window_.kind=="museum"||window_.kind=="inventory";}
    void open_inventory(presentation::InputEventsMessage &,std::vector<std::string> categories,int index);
    int take_inventory_tab_request(){const int index=inventory_tab_request_;inventory_tab_request_=-1;return index;}
    /*!
     * 持ち物一覧を開くと送ってから、コアの最初の一覧が届くまでの間か（中身の無い窓）。
     * この間は窓を描かない——タブの切り替えや i で、空の窓が 1〜3 フレーム挟まっていた
     * （気づいたこと 2026-09-26「一度別の画面を一瞬挟んでないか」）。
     */
    bool opening_placeholder() const {return local_placeholder_&&pending_&&active();}
    void set_parent_command_tabs(std::vector<std::string> categories,int index);
    bool parent_tab_navigation_active() const {return active()&&window_.content_type=="settings_list_rogue"&&!parent_categories_.empty()&&parent_category_>=0;}
    int take_parent_tab_request();
    /*!
     * @brief 足の欄に取消の半分を出さない窓か（コアが `no_cancel` と言った窓。Umoria のキャラ作成）。
     * @details 決定だけを全幅にして、取消の部品も出さず、右半分のクリックも決定にする。
     * 操作の一覧・コマンドの一覧が開いている間は取消が一覧を閉じるので、今までどおり出す。
     * パッドの B・Esc は `cancel()` を通るので、ここでは変えない。
     */
    bool footer_without_cancel() const {return active()&&window_.no_cancel&&!operations_&&!commands_open_;}
    //! 設定一覧からほかのタブへ移ると決めて、窓が閉じるのを待っている間。
    bool leaving_parent_tab() const {return parent_tab_request_>=0;}
    //! 「設定」タブを選んで原作の設定画面を待っている間（その間の Shift は後で回す）。
    bool awaiting_parent_child() const {return !active()&&!parent_categories_.empty()&&parent_category_>=0&&parent_tab_request_<0;}
    bool text_mode() const {return window_.stage=="text"&&!commands_open_;}
    bool captures_keys() const {return active()&&window_.capture_keys;}
    void advance(presentation::InputEventsMessage &out);
    bool handle(CursorNav nav,presentation::InputEventsMessage &out);
    void cancel(presentation::InputEventsMessage &out);
    void text(const std::string &value);
    bool handle_native_key(int key,presentation::InputEventsMessage &out);
    void backspace();
    void edit_text_key(int key);
    void edit_text_boundary(bool end);
    RectPx text_edit_rect(const UiLayout &layout) const;
    void tab(int direction);
    void page(int direction,presentation::InputEventsMessage &out);
    void scroll_at(const UiLayout &,int x,int y,int direction,presentation::InputEventsMessage &out,bool horizontal=false);
    void scroll_horizontal(int direction){document_x_=std::max(0,document_x_+direction*32);}
    void toggle_details();
    void focus_details(bool on);
    bool show_help(presentation::InputEventsMessage &out);
    void toggle_commands();
    bool document_mode()const{return window_.content_type=="document"||window_.content_type=="quick_help";}
    bool search_document(bool previous,presentation::InputEventsMessage &out);
    void click(const UiLayout &layout,int x,int y,presentation::InputEventsMessage &out);
    void draw(UiPaint &paint,TextOverlay &text,const UiLayout &layout,const GameFrame &frame,int theme) const;
    void draw_voxels(VoxelRenderer &,SlabLibrary &,const TileCatalog &,const UiLayout &,TextOverlay *fallback_text=nullptr);
    void shutdown();
    RectPx input_rect(const UiLayout &layout) const;
    std::string status() const;
    const std::vector<ModernPaintedCell> &painted_content()const{return content_trace_;}
    //! auto_select の間、直前の一覧を描いているか（検査用）。
    bool showing_auto_backdrop() const {return static_cast<bool>(auto_backdrop_);}
    //! 検査用：カーソルのある行の識別子（無ければ空）と、窓の行の数（操作の行を含む）。
    std::string selected_row_id() const {return cursor_>=0&&cursor_<int(window_.rows.size())?window_.rows[cursor_].id:std::string();}
    //! 画面に出している窓の題（自己検査用）。
    std::string window_title() const {return window_.title;}
    int row_count() const {return int(window_.rows.size());}
    bool take_scope_changed(){const bool changed=scope_changed_;scope_changed_=false;return changed;}
    //! コアへ送った操作の返事を待っている間か（`app/hd2d_app.cpp` が、返事が届いた回にすぐ次の操作を送るかの判定に使う）。
    bool pending() const {return pending_;}
private:
    int font_level_{0};
    void draw_logical(UiPaint &,TextOverlay &,const UiLayout &,const GameFrame &,int) const;
    //! 検査用の問い合わせ（`app/ui_probe.h`）：開いている窓と、押せる部品（描いた矩形そのもの）を記録する。
    void probe_record(const UiLayout &logical,float scale,const GameFrame &) const;
    void click_logical(const UiLayout &,int,int,presentation::InputEventsMessage &);
    RectPx text_edit_rect_logical(const UiLayout &) const;
    RectPx panel(const UiLayout &layout) const;
    RectPx list_rect(const UiLayout &layout) const;
    RectPx description_rect(const UiLayout &layout) const;
    RectPx popup_rect(const UiLayout &layout) const;
    void prepare_popup(TextOverlay &,const UiLayout &) const;
    void draw_operations(UiPaint &,TextOverlay &,const UiLayout &,int) const;
    void click_operation(const UiLayout &,int,int);
    std::vector<const ModernRow*> rows() const;
    const ModernRow *selected() const;
    int keyboard_key(const ModernRow &row) const;
    std::vector<std::string> tab_labels() const;
    RectPx row_icon_rect(const UiLayout &layout,int row) const;
    void item_icon(UiPaint &,const RectPx &,const ModernRow &) const;
    RectPx choice_list_rect(const UiLayout &) const;
    int choice_row_height(const UiLayout &l) const {return window_.content_type=="items"&&choice_list_rect(l).w<360?72:(birth_layout_?30:36);}
    int item_row_height(const UiLayout &l) const {return item_window()&&list_rect(l).w<360?72:36;}
    bool form_active() const {return window_.stage=="text"||window_.stage=="number"||window_.stage=="confirm"||window_.stage=="message"||window_.message_only;}
    void draw_choices(UiPaint &,TextOverlay &,const UiLayout &,int,bool backdrop=false) const;
    void draw_poker(UiPaint &,TextOverlay &,const UiLayout &,int) const;
    RectPx draw_venue_header(UiPaint &,TextOverlay &,RectPx,int) const;
    void draw_commands(UiPaint &,TextOverlay &,const UiLayout &,int) const;
    RectPx commands_button(const UiLayout &)const;
    int dialog_answer_count(const UiLayout &)const;
    int dialog_controls_top(const UiLayout &)const;
    void draw_content(UiPaint &,TextOverlay &,RectPx,const std::vector<ModernBlock> &,int,bool) const;
    std::string bottom_message() const;
    void draw_message(UiPaint &,TextOverlay &,RectPx,int) const;
    void draw_dialog(UiPaint &paint,TextOverlay &text,const UiLayout &layout,const GameFrame &frame,int theme) const;
    void send(const std::string &action,presentation::InputEventsMessage &out,int value=0);
    /*!
     * 原作がスクロールで出す一覧の端（案 A）。端でさらに上下を押したら、画面の中で回り込まずに原作へ上下を送って
     * スクロールさせる（`sync()` の説明と `scroll_edge()` の定義を参照）。処理した（原作へ送った・操作の行へ進んだ）ら true。
     */
    bool scroll_edge(int step,presentation::InputEventsMessage &out);
    //! 端を探る（原作へ上下を 1 回送り、次のフレームで何も変わらなければそこが本当の端）。
    void begin_probe(int step,int selection,presentation::InputEventsMessage &out);
    //! 本当の端から反対の端へ回り込む（原作のページ送り・上下を、変わらなくなるまで送る）。`toward` は +1＝末尾・-1＝先頭。
    void begin_wrap(int toward);
    //! いまの原作のカーソルの行を表す字（行の識別子。核のカーソルの行番号）。端の探りの前後比べに使う。
    std::string cursor_token() const;
    //! 原作の行（操作の行を除く）が 1 つ以上あり、原作のカーソルが `index` 番目の行にあるか。
    bool native_cursor_at(int index) const;
    //! 原作のカーソルがある行の列（行の `selection`。見つからなければ 0）。
    int native_cursor_selection() const;
    //! 知識のブラウザの上下（同じ列の次の行の添字）。
    int browser_column_step(int step) const;
    ModernWindow window_;
    NumericInput numeric_;PromptBar prompt_;
    MenuTabs tabs_;
    bool enabled_{false},operations_{false},pending_{false},replace_number_{true},details_focus_{false},workflow_started_{false},raw_pending_{false},cancel_after_navigation_{false};
    bool birth_layout_{false};
    //! 文字入力の欄をまだ編集していないか（修-58。最初の Backspace・Delete で既定の字を全部消す）と、欄を見分ける鍵。
    bool text_fresh_{false};std::string text_field_scope_;
    bool clear_fresh_text();
    bool retained_choices_{false};
    bool navigation_page_{false};
    mutable int requested_section_{-1};
    //! メッセージ履歴の窓（修-18）：新しく出た・頁を送って中身が変わったとき、次に描くときに本文の末尾まで送る（最新の行を見せる）。
    //! `history_signature_` は直近に末尾へ送った中身の見分け。同じ内容の再送では位置を動かさない。
    mutable bool scroll_to_end_{false};
    std::string history_signature_;
    mutable int popup_width_{140};
    mutable int operation_scroll_{0};
    mutable std::vector<int> operation_heights_;
    mutable int message_scroll_{0},message_height_{56},message_max_scroll_{0};
    mutable std::string message_signature_;
    mutable RectPx message_rect_;
    mutable RectPx detail_image_rect_;
    std::vector<std::string> inventory_categories_;
    int inventory_category_{0},inventory_tab_request_{-1};
    mutable int inventory_header_first_{0};
    std::vector<std::string> parent_categories_;
    int parent_category_{-1},parent_tab_request_{-1};
    mutable int parent_header_first_{0};
    bool parent_tabs_seen_{false};
    //! 受けられない間に押されたタブ移動（向き）。受けられるようになった時点で 1 回だけ回す。
    int deferred_tab_{0};
    std::chrono::steady_clock::time_point deferred_tab_at_{};
    bool inventory_open_queued_{false};
    //! `open_inventory()` が自分で組んだ、中身の無い一覧（コアの最初の返事が来るまで）。
    bool local_placeholder_{false};
    std::string message_context_;
    std::uint64_t message_sequence_{};
    std::vector<std::string> recent_messages_,previous_frame_messages_;
    int cursor_{0},operation_{0},answer_{0},serial_{0},sent_revision_{0},workflow_steps_{0};
    int selection_column_{0};
    mutable int group_scroll_{0},group_x_{0},group_max_scroll_{0};mutable std::string group_focus_;
    mutable int scroll_{0},document_scroll_{0},document_x_{0};mutable int detail_scroll_{0};mutable int document_max_scroll_{};
    bool commands_open_{false};int command_cursor_{};mutable int command_scroll_{};
    bool scope_changed_{false};
    std::string dialog_signature_,focus_target_;
    std::string requested_preview_target_;
    int preview_request_{};
    std::unordered_map<std::string,std::pair<int,int>> choice_positions_;
    std::unordered_map<std::string,std::pair<int,std::string>> choice_focus_;
    mutable std::string document_focus_,trace_signature_,follow_target_;
    mutable std::vector<ModernPaintedCell> content_trace_,content_navigation_;
    int focus_steps_{};
    /*!
     * 原作のカーソルを目標の行へ 1 手ずつ動かしている間（`focus` の返事待ちを含む）か（調-08）。
     * 2 列に並べて見せる一覧（幻想蛮怒の登場作品）は、隣の行へ動かすのに原作のカーソルを十数回動かす。
     * その間に押された上下は目標の行を付け替え、決定は歩き終えてから送る（捨てない）。
     */
    bool walking_focus() const;
    bool confirm_after_focus_{false};
    std::string confirm_target_,confirm_scope_;
    std::chrono::steady_clock::time_point confirm_after_focus_at_{};
    std::string window_scope() const {return window_.kind+"|"+window_.title+"|"+window_.content_type;}
    //! 原作がスクロールで出す一覧か（`sync()` が決める）。原作の行の数・署名（行の識別子をつないだ字）。
    bool scroll_list_{false};
    int native_rows_{0},primary_selection_{0};
    std::string native_sig_;
    //! 端の探り（原作へ送った上下の要求番号と、送る前の署名・カーソル）。
    struct EdgeProbe {int serial{0},step{0},selection{0};std::string sig,cursor;};
    EdgeProbe probe_;
    //! 端まで確かめた列（`selection`）。行の署名が変わる（スクロールする）たびに消す。
    std::string confirmed_sig_;
    std::vector<int> confirmed_top_,confirmed_bottom_;
    //! 端の確認のあと、押されたままの上下をもう 1 回処理し直す（向き。0＝無い）と、その時の署名。
    int deferred_step_{0};
    std::string deferred_sig_;
    //! 回り込み。`toward` へ向けて、まず主の列（名前の列）へカーソルを移し（phase 0）、次にページ送り（phase 1）、次に 1 行ずつ（phase 2）、変わらなくなるまで原作へ送る。
    struct EdgeWrap {bool active{false};int toward{0},phase{0},count{0},serial{0};std::string sig,cursor;};
    EdgeWrap wrap_;
    //! 回り込みの着地。-2＝末尾（操作の行を含む最後の行）・-1＝指定なし。
    int place_cursor_{-1};
    //! Sil-Q の能力値・技能の窓（`sil_allocation`）で、左右キーや ± の行の押下を送る前に、原作のカーソルをその項目へ着かせる間、送る行の識別子。
    std::string press_after_focus_;
    void press_allocation(int row,presentation::InputEventsMessage &out);
    std::string target_,sent_stage_,sent_action_,number_,notice_,workflow_,workflow_target_,text_line_;
    presentation::InputEventsMessage buffered_;
    std::chrono::steady_clock::time_point sent_at_;
    //! 持ち物の命令の後、窓の無い画が来ても少しの間は一覧を出し続ける（sync の説明を参照）。
    std::chrono::steady_clock::time_point inventory_command_at_{},hold_until_{};
    bool held_{false};
    struct ItemIcon {RectPx rect;int tile;std::string glyph;std::uint8_t color;bool available;};
    mutable std::vector<ItemIcon> item_icons_;
    mutable MenuHeaderLayout header_layout_;
    mutable int painted_theme_{0};
    mutable int venue_header_height_{0};
    //! 持ち物一覧で命令を選んだ後、auto_select で原作の選択肢を確定している間に描く、直前の一覧の写し。
    std::shared_ptr<ModernStoreUi> auto_backdrop_;
    int auto_revision_{0};
    //! 文脈メニューのポップアップ（AH-58）。アンカーは `set_context_anchor()`、`menu_over_map` と見出しは `sync()` が写す。
    bool context_anchor_valid_{false},menu_over_map_{false};
    //! クリックした点が無くても小さな箱で出す（`GameFrame::menu_popup`）。
    bool menu_popup_{false};
    int context_anchor_x_{0},context_anchor_y_{0};
    std::string context_heading_;
    mutable RectPx context_rect_;
    mutable int context_anchor_lx_{0},context_anchor_ly_{0};
    void draw_context_popup(UiPaint &,TextOverlay &,const UiLayout &,int) const;
    void click_context_popup(int x,int y,presentation::InputEventsMessage &out);
};
}
