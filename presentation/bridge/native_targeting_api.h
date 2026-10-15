#pragma once
#ifdef __cplusplus
extern "C" {
#endif

enum native_target_mode { NATIVE_TARGET_POINT, NATIVE_TARGET_DIRECTION8, NATIVE_TARGET_CREATURE, NATIVE_TARGET_FIXED, NATIVE_TARGET_LOOK };
/* "look" candidates only (design doc section 8.5.1): what hb_target_candidate_look()'s
 * kind argument names. Kept separate from native_target_mode/native_target_kind. */
enum native_target_candidate_kind { NATIVE_CANDIDATE_NONE, NATIVE_CANDIDATE_MONSTER, NATIVE_CANDIDATE_OBJECT,
    NATIVE_CANDIDATE_FEATURE, NATIVE_CANDIDATE_TRAP, NATIVE_CANDIDATE_PLAYER };
enum native_target_kind { NATIVE_TARGET_NEUTRAL, NATIVE_TARGET_ATTACK, NATIVE_TARGET_SUPPORT };
enum native_target_path_mode { NATIVE_PATH_REACHABLE, NATIVE_PATH_POSSIBLE, NATIVE_PATH_GUIDE };
enum native_target_reason { NATIVE_TARGET_OK, NATIVE_TARGET_OUTSIDE, NATIVE_TARGET_SELF_FORBIDDEN,
    NATIVE_TARGET_CREATURE_REQUIRED, NATIVE_TARGET_UNAVAILABLE, NATIVE_TARGET_OUT_OF_RANGE,
    NATIVE_TARGET_PROFILE_MISSING, NATIVE_TARGET_EFFECT_UNKNOWN, NATIVE_TARGET_PREVIEW_OVERFLOW };
enum native_target_shape { NATIVE_SHAPE_NONE, NATIVE_SHAPE_POINT, NATIVE_SHAPE_BOLT, NATIVE_SHAPE_BEAM,
    NATIVE_SHAPE_BALL, NATIVE_SHAPE_BREATH, NATIVE_SHAPE_SHOT, NATIVE_SHAPE_THROW,
    NATIVE_SHAPE_BLAST, NATIVE_SHAPE_LOS, NATIVE_SHAPE_ARC, NATIVE_SHAPE_QUAKE,
    NATIVE_SHAPE_DESTRUCTION, NATIVE_SHAPE_ROCKET, NATIVE_SHAPE_PUSH, NATIVE_SHAPE_SPARK, NATIVE_SHAPE_MELEE,
    NATIVE_SHAPE_PROJECT, NATIVE_SHAPE_SELF_SCATTER, NATIVE_SHAPE_MIRROR_RAY, NATIVE_SHAPE_ROOM_AREA };
enum native_target_shot_flag { NATIVE_SHOT_EXPLODE_ON_MISS=1, NATIVE_SHOT_AVOID_BIRDS=2 };
typedef struct native_target_effect {
    int shape, element, radius, range, flags, possible, arg1, arg2;
} native_target_effect;
typedef struct native_target_profile {
    const char *site;
    const char *label;
    int supported, kind, effect_known, count;
    const native_target_effect *effects;
} native_target_profile;
typedef struct native_target_request {
    int width,height,source_x,source_y,mode,kind;
    int previous_valid,previous_x,previous_y,previous_id;
    const char *label;
} native_target_request;
typedef struct native_target_choice {
    int x,y,id,direction,can_confirm;
    // Set when the user explicitly chose a creature. A ground point must not
    // start following an unrelated monster that later crosses that square.
    int track_creature;
    // Set by hb_target_choose() when an "extra" operation ended the session by itself
    // (Angband 'g': walk to the cursor). The result is then 0 (no target), and the caller
    // must not report it as "aborted". Zero for every other core.
    int ended_by_extra;
    // Set by hb_target_choose() when the last change of the cursor was a direction key (not a click and not
    // the candidate cycle). A core whose original asks "direction or target" (Angband) may then treat a grid
    // on an eight-way line as a plain direction. Zero for every other core.
    int cursor_by_direction_key;
} native_target_choice;
/* Return values of native_target_hooks::extra. */
enum native_target_extra_result { NATIVE_EXTRA_UNKNOWN=-1, NATIVE_EXTRA_NONE, NATIVE_EXTRA_MOVED, NATIVE_EXTRA_END };
typedef struct native_target_hooks {
    void (*candidates)(void *);
    void (*preview)(native_target_choice *,void *);
    int (*read_key)(void *);
    int (*direction_key)(int,void *);
    void (*refresh)(void *);
    // Added for mode "look" (design doc section 8.5.1, "detail"/aim_recall). Must
    // stay the LAST member: every existing hooks initializer either brace-lists
    // the first five fields (this one then value-initializes to null) or zero-
    // initializes the whole struct, so no other core's adapter needs to change.
    // Null unless the core supplies one; hb_target_choose() then just ignores
    // the "detail" action instead of crashing.
    void (*recall)(native_target_choice *,void *);
    // Optional extra operations (Angband '<' '>' 'x' 'p' 'm' 'g' 'k'). `extra(key,choice,data)`
    // returns a native_target_extra_result: UNKNOWN = not one of its keys (the key is ignored, as before),
    // NONE = nothing to do (the core may call hb_target_notice() to say why), MOVED = it changed
    // `choice`, END = it ended the session itself. `extra_screen(key)` is true when the operation opens a
    // core screen (a menu): the targeting state is closed while it runs and rebuilt afterwards.
    // Must stay after `recall`: older initializers leave both null (no extras).
    int (*extra)(int,native_target_choice *,void *);
    int (*extra_screen)(int);
} native_target_hooks;

/* Private read-only mode inserted before any native spell preamble. */
#define HB_TARGET_PLAN_MODE (-61011)
typedef void (*native_target_menu_builder)(void *);
typedef void (*native_target_item_builder)(void *actor,int action,const void *object);
typedef void (*native_target_plan_symbol)(void);
int hb_target_menu_begin(void);
int hb_target_item_menu_begin(int action,native_target_item_builder,void *actor);
void hb_target_item_menu_display(int enter);
void hb_target_item_menu_clear(void);
void hb_target_item_menu_row(const char *id,const void *object);
void hb_target_menu_end(int scope);
void hb_target_menu_clear_rows(void);
void hb_target_menu_action(int selecting_action);
void hb_target_menu_geometry(int x,int y,int width,int height,int legacy_columns);
void hb_target_menu_row(int key,int row,int column,const char *rendered,
    native_target_menu_builder builder,const void *context,unsigned int context_size);
void hb_target_menu_row_text(const char *name,const char *detail,int selected);
void hb_target_mark_plan(native_target_plan_symbol symbol);
int hb_target_has_plan(native_target_plan_symbol symbol);
void hb_target_preview_fixed(const native_target_request *,const native_target_hooks *,void *);

int hb_target_enabled(void);
/* New UI predicts registered effects. HD2D_TARGETING_PREVIEW=0 disables prediction for diagnostics. */
int hb_target_preview_enabled(void);
int hb_target_active(void);
void hb_target_profile_begin(const char *site);
void hb_target_profile_label(const char *label);
void hb_target_profile_plain(int kind);
void hb_target_profile_effect(int shape,int element,int radius,int range,int flags,int possible,int arg1,int arg2);
void hb_target_profile_known(int known);
const native_target_profile *hb_target_profile(void);
void hb_target_heng_wand_profile(void *player,int sval,int powerful,int magic);
void hb_target_heng_rod_profile(void *player,int sval,int powerful,int magic);
void hb_target_heng_staff_profile(void *player,int sval,int powerful,int magic);
void hb_target_heng_rod_self_profile(void *player,int sval,int powerful,int magic);
void hb_target_heng_item_preview(void *player,int action,const void *object);
void gb_target_item_preview(void *actor,int action,const void *object);
void gb_target_item_profile(int action,const void *object);
void gb_target_preview_fixed(void);
void fc_target_item_preview(void *actor,int action,const void *object);
void fc_target_device_profile(const void *effect);

// These emitters are called by the active core's read-only preview callbacks.
void hb_target_candidate(int id,int x,int y,int distance,int ticks,int order,const char *name,int eligible);
// mode "look" only (design doc section 8.5.1). kind is a native_target_candidate_kind.
// id must still be nonzero and unique among this candidate list (as hb_target_candidate
// above); Angband's adapter uses the monster index for monsters and a synthetic
// negative id for objects/features/traps.
void hb_target_candidate_look(int id,int x,int y,int distance,int ticks,int order,const char *name,int eligible,int kind,const char *detail);
// Offers one extra operation for the frame being built (only while the running request has an
// `extra` hook; otherwise ignored). `text_label` is translated like every other core string.
void hb_target_extra(int key,const char *text_label,int enabled);
// Says why the last extra operation did nothing (shown where the "reason" of the toolbar is).
void hb_target_notice(const char *text);
void hb_target_status(int can_confirm,int reason,int distance,const char *name,int stochastic,int effect_known);
void hb_target_path_begin(int possible);
void hb_target_path_cell(int x,int y);
void hb_target_area_cell(int x,int y,int possible);
void hb_target_affected(int id,int x,int y,int creature);
// Optional test diagnostics, called after the real action computes its area.
// Never enters a frame or supplies information to the targeting preview.
void hb_target_trace_area(int element,int x,int y);
void hb_target_trace_hit(int hit,int x,int y);
void hb_target_trace_invariant(const char *name,int unchanged);
/* 攻撃の視覚効果の記録。原作へ組み立てのたびに書き足す呼び出し（tools/targeting/overlay.py）が呼ぶ。
 * 受け取った数を積むだけで、原作の状態は読まない・変えない。変愚系の project()・射撃・投擲だけが呼ぶ。
 * path は「始点の次のマスから着弾点まで」の (y,x) の並び、area は (中心からの距離,y,x) の並び。 */
void hb_attack_fx_projection(int src_idx,int src_y,int src_x,int impact_y,int impact_x,int attr,int rad,int flags,int blind,int breath,
    const int *path,int fx_path_n,const int *area,int fx_area_n);
void hb_attack_fx_shot(int cur_dis,int x,int y,int prev_y,int prev_x,int item_id,int tval,int brand);
void hb_attack_fx_throw(int cur_dis,int x,int y,int prev_y,int prev_x,int item_id,int returns);
void hb_attack_fx_hit(void);
void hb_attack_fx_throw_return(void);
int hb_target_choose(const native_target_request *,const native_target_hooks *,void *,native_target_choice *);

#ifdef __cplusplus
}

#include <cstring>
// The registry compares identities only; it never invokes this erased pointer.
// Keep the native callable's type at every actual invocation.
template<class Function> native_target_plan_symbol hb_target_plan_key(Function function)
{
    static_assert(sizeof(function)==sizeof(native_target_plan_symbol));
    native_target_plan_symbol key;
    std::memcpy(&key,&function,sizeof(key));
    return key;
}
#endif
