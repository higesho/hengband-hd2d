/*!
 * @file xr_input.cpp
 * @brief `xr_input.h` の実装。
 *
 * @note **プラットフォームで違うのは include 節だけ**。
 * actions・suggested bindings・`xrSyncActions` は OpenXR のコア API なので、Windows と
 * Quest で 1 行も変わらない。`openxr_platform.h` は入力には要らない。
 */
#include "xr/xr_input.h"

//! 傘の導出は `xr_session.cpp` と**同じ式**（§3）。片方だけ直すと組み合わせが壊れる。
#if defined(_WIN32) || defined(HENGBAND_QUEST)
#define HENGBAND_XR_OPENXR 1
#endif

#if defined(HENGBAND_XR_OPENXR)
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <unknwn.h>
#define XR_USE_PLATFORM_WIN32 1
#define XR_USE_GRAPHICS_API_OPENGL 1
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#else /* Quest（Android） */
//! `openxr_platform.h` は要らない（入力に platform 依存の APIは無い）。
#include <openxr/openxr.h>
#endif /* _WIN32 */
#endif /* HENGBAND_XR_OPENXR */

#include <cstdio>
#include <cstring>

namespace hd2d::xr {

namespace {

/*!
 * @brief Touch → `PadInput` の既定（§8）。**割り当て UI で振り直せる**ので、
 * ここは「最初の 1 回を出すための並び」でしかない。
 *
 * 物理ボタンが 1 個足りない（右手の Oculus ボタンはシステム予約）。この並びでは
 * `RightStick` 押し込みが欠番になり、代わりに右スティック押し込みを `Start` に当てている。
 */
constexpr TouchBinding kTouchBindings[] = {
    { "右手 A", "/user/hand/right/input/a/click", PadInput::A },
    { "右手 B", "/user/hand/right/input/b/click", PadInput::B },
    { "左手 X", "/user/hand/left/input/x/click", PadInput::X },
    { "左手 Y", "/user/hand/left/input/y/click", PadInput::Y },
    { "左グリップ", "/user/hand/left/input/squeeze/value", PadInput::LeftShoulder },
    { "右グリップ", "/user/hand/right/input/squeeze/value", PadInput::RightShoulder },
    { "左トリガ", "/user/hand/left/input/trigger/value", PadInput::LeftTrigger },
    { "右トリガ", "/user/hand/right/input/trigger/value", PadInput::RightTrigger },
    { "左手 ☰（メニュー）", "/user/hand/left/input/menu/click", PadInput::Back },
    { "右スティック押し込み", "/user/hand/right/input/thumbstick/click", PadInput::Start },
    { "左スティック押し込み", "/user/hand/left/input/thumbstick/click", PadInput::LeftStick },
};
constexpr int kTouchBindingCount = static_cast<int>(sizeof(kTouchBindings) / sizeof(kTouchBindings[0]));

//! グリップとトリガは軸で来る。ここを越えたら「押した」。
constexpr float kAnalogDown = 0.6f;
//! ここを下回ったら「離した」（出入りで別のしきい値。がたつき防止）。
constexpr float kAnalogUp = 0.35f;

} // namespace

const TouchBinding *touch_bindings(int &count)
{
    count = kTouchBindingCount;
    return kTouchBindings;
}

#if !defined(HENGBAND_XR_OPENXR)

struct Input::Impl {
};
Input::~Input() = default;
bool Input::init(void *, void *, std::string &err)
{
    err = "VR はこの環境では組み込まれていません。";
    return false;
}
void Input::shutdown() {}
bool Input::sync(ExternalPadState &) { return false; }

#else /* HENGBAND_XR_OPENXR */

struct Input::Impl {
    XrInstance instance{ XR_NULL_HANDLE };
    XrSession session{ XR_NULL_HANDLE };
    XrActionSet action_set{ XR_NULL_HANDLE };
    //! ボタン（`kTouchBindings` と同じ並び）。真偽のものと軸のものが混ざる。
    XrAction actions[kTouchBindingCount]{};
    bool is_analog[kTouchBindingCount]{};
    //! 軸のヒステリシス（`kAnalogDown` / `kAnalogUp`）。
    bool analog_down[kTouchBindingCount]{};
    XrAction stick_left{ XR_NULL_HANDLE };
    XrAction stick_right{ XR_NULL_HANDLE };
};

Input::~Input()
{
    this->shutdown();
}

bool Input::init(void *instance_handle, void *session_handle, std::string &err)
{
    this->shutdown();
    auto *impl = new Impl();
    this->impl_ = impl;
    impl->instance = static_cast<XrInstance>(instance_handle);
    impl->session = static_cast<XrSession>(session_handle);
    if ((impl->instance == XR_NULL_HANDLE) || (impl->session == XR_NULL_HANDLE)) {
        err = "セッションが立っていません。";
        this->shutdown();
        return false;
    }

    XrActionSetCreateInfo set_info{ XR_TYPE_ACTION_SET_CREATE_INFO };
    std::strncpy(set_info.actionSetName, "gameplay", XR_MAX_ACTION_SET_NAME_SIZE - 1);
    std::strncpy(set_info.localizedActionSetName, "Gameplay", XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE - 1);
    if (XR_FAILED(xrCreateActionSet(impl->instance, &set_info, &impl->action_set))) {
        err = "action set を作れませんでした。";
        this->shutdown();
        return false;
    }

    /*
     * action は**入力パスごとに 1 つ**作る。1 つの action に左右を束ねる（subaction path）
     * 作りもあるが、こちらは「ボタン 1 個 = `PadInput` 1 個」で素直に対応させたいので
     * 束ねない。数は 13 個で、束ねて得られるものより読みやすさを採った。
     */
    std::vector<XrActionSuggestedBinding> suggested;
    for (int i = 0; i < kTouchBindingCount; ++i) {
        const TouchBinding &binding = kTouchBindings[i];
        impl->is_analog[i] = (std::strstr(binding.path, "/value") != nullptr);

        XrActionCreateInfo info{ XR_TYPE_ACTION_CREATE_INFO };
        info.actionType = impl->is_analog[i] ? XR_ACTION_TYPE_FLOAT_INPUT : XR_ACTION_TYPE_BOOLEAN_INPUT;
        std::snprintf(info.actionName, XR_MAX_ACTION_NAME_SIZE, "btn_%d", i);
        std::snprintf(info.localizedActionName, XR_MAX_LOCALIZED_ACTION_NAME_SIZE, "%s",
            pad_input_name(binding.input));
        if (XR_FAILED(xrCreateAction(impl->action_set, &info, &impl->actions[i]))) {
            err = "action を作れませんでした。";
            this->shutdown();
            return false;
        }
        XrPath path = XR_NULL_PATH;
        if (XR_FAILED(xrStringToPath(impl->instance, binding.path, &path))) {
            continue; //!< そのランタイムが知らないパスは黙って飛ばす（結べないだけ）
        }
        suggested.push_back(XrActionSuggestedBinding{ impl->actions[i], path });
    }

    const auto make_vector2 = [&](const char *name, const char *path, XrAction &out) {
        XrActionCreateInfo info{ XR_TYPE_ACTION_CREATE_INFO };
        info.actionType = XR_ACTION_TYPE_VECTOR2F_INPUT;
        std::snprintf(info.actionName, XR_MAX_ACTION_NAME_SIZE, "%s", name);
        std::snprintf(info.localizedActionName, XR_MAX_LOCALIZED_ACTION_NAME_SIZE, "%s", name);
        if (XR_FAILED(xrCreateAction(impl->action_set, &info, &out))) {
            return false;
        }
        XrPath xr_path = XR_NULL_PATH;
        if (XR_SUCCEEDED(xrStringToPath(impl->instance, path, &xr_path))) {
            suggested.push_back(XrActionSuggestedBinding{ out, xr_path });
        }
        return true;
    };
    if (!make_vector2("stick_left", "/user/hand/left/input/thumbstick", impl->stick_left)
        || !make_vector2("stick_right", "/user/hand/right/input/thumbstick", impl->stick_right)) {
        err = "スティックの action を作れませんでした。";
        this->shutdown();
        return false;
    }

    XrPath profile = XR_NULL_PATH;
    if (XR_FAILED(xrStringToPath(impl->instance, "/interaction_profiles/oculus/touch_controller", &profile))) {
        err = "Touch の interaction profile を引けませんでした。";
        this->shutdown();
        return false;
    }
    XrInteractionProfileSuggestedBinding suggest{ XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
    suggest.interactionProfile = profile;
    suggest.countSuggestedBindings = static_cast<std::uint32_t>(suggested.size());
    suggest.suggestedBindings = suggested.data();
    if (XR_FAILED(xrSuggestInteractionProfileBindings(impl->instance, &suggest))) {
        err = "Touch への割り当てをランタイムが受け取りませんでした。";
        this->shutdown();
        return false;
    }

    XrSessionActionSetsAttachInfo attach{ XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
    attach.countActionSets = 1;
    attach.actionSets = &impl->action_set;
    if (XR_FAILED(xrAttachSessionActionSets(impl->session, &attach))) {
        err = "action set をセッションへ付けられませんでした。";
        this->shutdown();
        return false;
    }
    this->profile_name_ = "oculus/touch_controller";
    std::fprintf(stderr, "[hd2d] XR_INPUT %s（%d 個のボタン + スティック 2 本）\n",
        this->profile_name_.c_str(), kTouchBindingCount);
    return true;
}

void Input::shutdown()
{
    Impl *const impl = this->impl_;
    if (impl == nullptr) {
        return;
    }
    /*
     * action は action set の子なので、set を消せば道連れになる。
     * **セッションへ付けた action set は外せない**（OpenXR には detach が無い）ので、
     * ここで消すのはセッションを畳む前でなければならない。
     */
    if (impl->action_set != XR_NULL_HANDLE) {
        (void)xrDestroyActionSet(impl->action_set);
    }
    delete impl;
    this->impl_ = nullptr;
}

bool Input::sync(ExternalPadState &out)
{
    Impl *const impl = this->impl_;
    if ((impl == nullptr) || (impl->action_set == XR_NULL_HANDLE)) {
        return false;
    }
    XrActiveActionSet active{ impl->action_set, XR_NULL_PATH };
    XrActionsSyncInfo sync_info{ XR_TYPE_ACTIONS_SYNC_INFO };
    sync_info.countActiveActionSets = 1;
    sync_info.activeActionSets = &active;
    const XrResult result = xrSyncActions(impl->session, &sync_info);
    if (result == XR_SESSION_NOT_FOCUSED) {
        return false; //!< 焦点が無い（ダッシュボードが出ている等）。**握ったままにしない**
    }
    if (XR_FAILED(result)) {
        return false;
    }

    out = ExternalPadState{};
    for (int i = 0; i < kTouchBindingCount; ++i) {
        const int slot = static_cast<int>(kTouchBindings[i].input);
        XrActionStateGetInfo get{ XR_TYPE_ACTION_STATE_GET_INFO };
        get.action = impl->actions[i];
        if (impl->is_analog[i]) {
            XrActionStateFloat state{ XR_TYPE_ACTION_STATE_FLOAT };
            if (XR_FAILED(xrGetActionStateFloat(impl->session, &get, &state)) || (state.isActive == XR_FALSE)) {
                impl->analog_down[i] = false;
                continue;
            }
            //! **出入りで別のしきい値**（`game_pad.h` の約束 3 と同じ考え方）。
            if (impl->analog_down[i]) {
                impl->analog_down[i] = (state.currentState > kAnalogUp);
            } else {
                impl->analog_down[i] = (state.currentState > kAnalogDown);
            }
            out.button[slot] = impl->analog_down[i];
        } else {
            XrActionStateBoolean state{ XR_TYPE_ACTION_STATE_BOOLEAN };
            if (XR_FAILED(xrGetActionStateBoolean(impl->session, &get, &state)) || (state.isActive == XR_FALSE)) {
                continue;
            }
            out.button[slot] = (state.currentState == XR_TRUE);
        }
    }

    const auto read_stick = [&](XrAction action, float &x, float &y) {
        XrActionStateGetInfo get{ XR_TYPE_ACTION_STATE_GET_INFO };
        get.action = action;
        XrActionStateVector2f state{ XR_TYPE_ACTION_STATE_VECTOR2F };
        if (XR_FAILED(xrGetActionStateVector2f(impl->session, &get, &state)) || (state.isActive == XR_FALSE)) {
            x = 0.f;
            y = 0.f;
            return;
        }
        x = state.currentState.x;
        /*
         * **上下を反転する。** XR のスティックは前が +y、`GamePad` は「下が正」
         * （画面の南）で受ける。ここを揃えないと前へ倒すと南へ歩く。
         */
        y = -state.currentState.y;
    };
    read_stick(impl->stick_left, out.left_x, out.left_y);
    read_stick(impl->stick_right, out.right_x, out.right_y);
    return true;
}

#endif /* HENGBAND_XR_OPENXR */

} // namespace hd2d::xr
