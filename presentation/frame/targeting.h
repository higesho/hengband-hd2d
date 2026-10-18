#pragma once
#include <string>
#include <vector>

// A targeting request is separate from the terminal/menu representation. All
// positions and eligibility/coverage decisions come from the active core.
struct TargetingPoint {
    int x{}, y{};
    bool operator==(const TargetingPoint &) const = default;
};
struct TargetingCandidate {
    int id{}; // Positive: monster; -1: the player. Zero is a free map point.
    TargetingPoint point;
    std::string name;
    // Added for mode "look" (design doc section 8.5.1). Empty kind keeps the
    // legacy meaning above (id sign decides monster/player/free point); other
    // cores never send these, so they stay empty and nothing changes for them.
    std::string kind; // "", monster, object, feature, trap, player
    std::string detail; // One-line description, as the original "look" shows it.
    bool operator==(const TargetingCandidate &) const = default;
};
struct TargetingPath {
    std::vector<TargetingPoint> points;
    bool possible{}; // The route depends on a random/conditional outcome.
    bool guide{}; // Desired continuation beyond a hard stop; not coverage.
    bool operator==(const TargetingPath &) const = default;
};
struct TargetingAreaCell {
    TargetingPoint point;
    bool possible{};
    bool operator==(const TargetingAreaCell &) const = default;
};
struct TargetingMarker {
    int id{};
    TargetingPoint point;
    bool creature{};
    bool selected{};
    bool affected{}; // May be affected, not a guarantee of a successful hit.
    bool valid{true};
    bool operator==(const TargetingMarker &) const = default;
};
// An operation the core offers inside an active targeting session besides moving, cycling,
// confirming and cancelling (Angband: '<' '>' 'x' 'p' 'm' 'g' 'k' of target_set_interactive()).
// `key` is the core's own key for it (sent back as "aim_extra"). Other cores never send any.
// `key` is a printable key (33..126) or a Ctrl+letter (1..26): with the roguelike keyset Angband's
// "ignore" is ^D, because 'k' moves the cursor up there (2026-10-07; targeting_codec.h checks the range).
struct TargetingExtra {
    int key{};
    std::string label;
    bool enabled{true};
    bool operator==(const TargetingExtra &) const = default;
};
// A short coloured text the core attaches to the cursor cell (TOband: the element values of the grid and the
// element of the monster on it, exactly what the original prints on the first line of its look/target prompt).
// The UI draws it as a small popup at the lower right of the cursor. `color` is a TERM_COLOR (0..15), converted
// with the frame's colour table. Other cores never send any, so `info` stays empty and nothing changes for them.
struct TargetingInfoSpan {
    std::string text;
    int color{1};
    bool operator==(const TargetingInfoSpan &) const = default;
};
struct TargetingInfoLine {
    std::vector<TargetingInfoSpan> spans;
    bool operator==(const TargetingInfoLine &) const = default;
};
struct TargetingState {
    bool active{};
    bool preview_only{}; // A highlighted native menu action; accepts no aiming input.
    int version{1}, scope{}, revision{}, request_id{}, selection_sequence{};
    int width{}, height{};
    std::string result;
    std::string mode{"point"}; // point, direction8, creature, fixed, look
    std::string kind{"neutral"}; // attack, support, neutral
    std::string label, target_name, reason;
    TargetingPoint source, selected;
    int selected_id{}, direction{}, distance{};
    bool can_confirm{}, stochastic{}, effect_known{true};
    // Already filtered and ordered by the core: distance, next opportunity,
    // then the core's processing order. The UI must not reorder this list.
    std::vector<TargetingCandidate> candidates;
    std::vector<TargetingPath> paths;
    std::vector<TargetingAreaCell> area;
    std::vector<TargetingMarker> markers;
    std::vector<TargetingExtra> extras;
    // The popup at the cursor (see TargetingInfoLine). Empty = no popup.
    std::vector<TargetingInfoLine> info;
    bool operator==(const TargetingState &) const = default;
};
