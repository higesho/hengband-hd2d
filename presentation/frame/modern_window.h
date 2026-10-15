#pragma once
#include <string>
#include <vector>

// Optional modern window data. A missing version keeps the original UI.
struct ModernAction {
    std::string id,label,reason;
    bool enabled{true};
    int key{};
};
struct ModernRow {
    std::string id;
    std::string label;
    std::string weight;
    int count{};
    int price{};
    int key{};
    int tab{},slot{},tile{},selection{-1};
    bool available{true},price_known{true},equipped{false};
    std::string reason,description;
    std::vector<std::string> effects;
    std::vector<ModernAction> actions;
    int hotkey{},glyph_color{1};
    std::string glyph;
};
struct ModernCell {
    std::string label,text;
    bool selected{false};
    std::string target;
    std::string color; // Optional RGB colour for a symbol legend.
};
struct ModernBlock {
    std::string kind,title;
    std::vector<ModernCell> cells;
};
struct ModernWindow {
    bool can_preview{};
    std::string preview_target; // Read-only focus; never a native selection/commit.
    std::string auto_select; // An inventory action already selected this item; complete only its native selector.
    // Content is interpreted in the adapter, independently of native geometry.
    std::string content_type,input_label,language{"en"},cursor_target;
    std::vector<ModernBlock> content;
    std::vector<ModernAction> actions;
    bool message_only{false},text_ascii_only{false},text_fullwidth_kana{false},input_ready{true};
    std::string text_edit_mode{"full"}; // full, basic, grid, editor, append
    int default_answer{-1};
    //! Row id for the first cursor position when a choices window newly appears (empty: the first row).
    //! Used only when `cursor` is negative (the native screen has no cursor of its own), e.g. Frox character creation puts it on "Next screen".
    std::string initial_target;
    int input_caret{-1}; // UTF-8 byte offset within input; never a Term column.
    int version{};
    int revision{};
    int request_id{};
    std::string result;
    std::string stage;
    std::string title,subtitle;
    std::string venue_header; // Store/building identity, fixed above the detail pane.
    std::vector<ModernRow> rows;
    bool previous{};
    bool next{};
    std::string kind{"store"},body;
    int page{},page_size{12},cursor{-1};
    // UTF-8 byte range in the displayed body row. Optional for older cores.
    int cursor_span_begin{-1},cursor_span_len{};
    bool can_toggle{false};
    bool text_confirm_action{false};
    bool capture_keys{false};
    //! 取消が効かない窓（原作に戻る道が無い画面。Umoria のキャラ作成）。画面側は案内から「取消: 戻る」を外す。
    bool no_cancel{false};
    std::string input;
    int prev_key{},next_key{},continue_key{};
};
