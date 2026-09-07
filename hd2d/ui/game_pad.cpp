/*!
 * @file game_pad.cpp
 * @brief `game_pad.h` の実装。
 */
#include "ui/game_pad.h"

#include "i18n/lang.h"

#include "ui/key_binds.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace hd2d {

namespace {

/*!
 * @name スティックの死区（約束 3）
 * @details **入るときと出るときで違う値**にする。同じ値だと境目でがたつき、
 * 1 回のつもりの傾けが数回の入力になる。
 * @{
 */
constexpr int kStickEnter = 18000; //!< ここを越えたら「倒した」
constexpr int kStickLeave = 11000; //!< ここを下回ったら「戻した」
/*! @} */

//! トリガは軸（0〜32767）。半分より深ければ押している。
constexpr int kTriggerDown = 16000;
constexpr int kTriggerUp = 9000;

/*! @name 方向のリピート @{ */
constexpr std::uint32_t kFirstRepeatMs = 260; //!< 押してから 2 歩目まで
constexpr std::uint32_t kNextRepeatMs = 90; //!< 以降
/*! @} */

int latch(int current, int value)
{
    if (value > kStickEnter) {
        return 1;
    }
    if (value < -kStickEnter) {
        return -1;
    }
    if (std::abs(value) < kStickLeave) {
        return 0;
    }
    return current; // 出入りの間は前の状態のまま（ヒステリシス）
}

} // namespace

const char *pad_input_name(PadInput input)
{
    switch (input) {
    case PadInput::A:
        return "A";
    case PadInput::B:
        return "B";
    case PadInput::X:
        return "X";
    case PadInput::Y:
        return "Y";
    case PadInput::LeftShoulder:
        return "LB";
    case PadInput::RightShoulder:
        return "RB";
    case PadInput::LeftTrigger:
        return "LT";
    case PadInput::RightTrigger:
        return "RT";
    case PadInput::Back:
        return "Back";
    case PadInput::Start:
        return "Start";
    case PadInput::LeftStick:
        return "LS";
    case PadInput::RightStick:
        return "RS";
    default:
        return "?";
    }
}

bool parse_pad_input(const std::string &name, PadInput &out)
{
    for (int i = 0; i < kPadInputCount; ++i) {
        const auto input = static_cast<PadInput>(i);
        if (name == pad_input_name(input)) {
            out = input;
            return true;
        }
    }
    return false;
}

bool pad_input_is_fixed(PadInput input)
{
    return (input == PadInput::A) || (input == PadInput::B) || (input == PadInput::Back);
}

bool pad_input_is_modifier(PadInput input)
{
    return (input == PadInput::LeftShoulder) || (input == PadInput::RightShoulder);
}

int pad_modifier_of(PadInput input)
{
    if (input == PadInput::LeftShoulder) {
        return kPadModCtrl;
    }
    if (input == PadInput::RightShoulder) {
        return kPadModAlt;
    }
    return 0;
}

bool PadChord::press(PadInput input, int &mods_out)
{
    const int modifier = pad_modifier_of(input);
    if (modifier != 0) {
        const int slot = (modifier == kPadModCtrl) ? 0 : 1;
        this->held_[slot] = true;
        /*
         * 修飾どうしを重ねたときは**どちらも単独ではなくなる**（LB＋RB は
         * `control-alt-` の層）。ここで印を付けないと、層を作ったつもりで指を離した
         * 瞬間に階段を上り下りすることになる。
         */
        const int other = 1 - slot;
        if (this->held_[other]) {
            this->used_[slot] = true;
            this->used_[other] = true;
        }
        return true; //!< 押した瞬間には何も起こさない（約束 1）
    }

    mods_out = this->mask();
    //! 押している修飾は「使われた」＝離しても単独発火しない（約束 2）。
    for (int slot = 0; slot < 2; ++slot) {
        if (this->held_[slot]) {
            this->used_[slot] = true;
        }
    }
    return false;
}

bool PadChord::release(PadInput input, PadInput &out)
{
    const int modifier = pad_modifier_of(input);
    if (modifier == 0) {
        return false; //!< 修飾でないボタンの離しは使わない（押した瞬間に出している）
    }
    const int slot = (modifier == kPadModCtrl) ? 0 : 1;
    const bool solo = this->held_[slot] && !this->used_[slot];
    this->held_[slot] = false;
    this->used_[slot] = false;
    if (!solo) {
        return false;
    }
    out = input;
    return true;
}

int PadChord::mask() const
{
    int mods = 0;
    if (this->held_[0]) {
        mods |= kPadModCtrl;
    }
    if (this->held_[1]) {
        mods |= kPadModAlt;
    }
    return mods;
}

void PadChord::forget()
{
    for (int slot = 0; slot < 2; ++slot) {
        this->held_[slot] = false;
        this->used_[slot] = false;
    }
}

const char *pad_mods_tag(int mods)
{
    switch (mods) {
    case kPadModCtrl:
        return "C";
    case kPadModAlt:
        return "A";
    case kPadModCtrl | kPadModAlt:
        return "CA";
    default:
        return "";
    }
}

int parse_pad_mods(const std::string &tag)
{
    for (int mods = 0; mods < kPadModLayerCount; ++mods) {
        if (tag == pad_mods_tag(mods)) {
            return mods;
        }
    }
    return -1;
}

std::string pad_chord_display_name(PadInput input, int mods)
{
    std::string name;
    if ((mods & kPadModCtrl) != 0) {
        name += pad_input_name(PadInput::LeftShoulder);
        name += i18n::tr("hd2d.ui.game-pad.x08ae1b");
    }
    if ((mods & kPadModAlt) != 0) {
        name += pad_input_name(PadInput::RightShoulder);
        name += i18n::tr("hd2d.ui.game-pad.x08ae1b");
    }
    name += pad_input_name(input);
    return name;
}

int pad_input_trigger_scancode(PadInput input)
{
    /*
     * **`lib/pref/pref-xxx.prf` の `T:Pad_*` と対になっている。**
     * あちらが `\[Pad_X]` という名前を、こちらが押したときの符号を受け持つ。
     * 片方だけ変えると、登録した名前と別の符号が飛んで**押しても反応しない**。
     *
     * 0xF0 以降なのは、DirectInput のキーボード走査符号が 0xED までしか使っておらず、
     * 実在のキーと衝突しないため。
     */
    switch (input) {
    case PadInput::X:
        return 0xF0;
    case PadInput::Y:
        return 0xF1;
    case PadInput::LeftShoulder:
        return 0xF2;
    case PadInput::RightShoulder:
        return 0xF3;
    case PadInput::LeftTrigger:
        return 0xF4;
    case PadInput::RightTrigger:
        return 0xF5;
    case PadInput::Start:
        return 0xF6;
    case PadInput::LeftStick:
        return 0xF7;
    case PadInput::RightStick:
        return 0xF8;
    default:
        //! A・B・Back は選択／取消／機能メニューで固定。トリガーは持たせない。
        return 0;
    }
}

std::vector<int> pad_input_trigger_sequence(PadInput input, int mods)
{
    const int scancode = pad_input_trigger_scancode(input);
    if (scancode == 0) {
        return {};
    }
    static const char kHexDigits[] = "0123456789ABCDEF";
    /*
     * 綴りは `pref-xxx.prf` の雛型 `&x#` そのもの。`&` の位置に修飾文字が入る。
     * **並びは `macro_modifier_chr`（"CSA"）の順**でなければならない
     * （コアの `trigger_ascii_to_text` はこの順に読む）。
     */
    std::vector<int> sequence;
    sequence.push_back(31);
    if ((mods & kPadModCtrl) != 0) {
        sequence.push_back('C');
    }
    if ((mods & kPadModAlt) != 0) {
        sequence.push_back('A');
    }
    sequence.push_back('x');
    sequence.push_back(kHexDigits[(scancode / 16) & 0x0F]);
    sequence.push_back(kHexDigits[scancode & 0x0F]);
    sequence.push_back(13);
    return sequence;
}

void GamePad::open(int joystick_index)
{
    if (this->controller_ != nullptr) {
        return; // 1 本だけ持つ（2 本目は無視する）
    }
    if (SDL_IsGameController(joystick_index) == SDL_FALSE) {
        return;
    }
#if defined(HENGBAND_QUEST)
    /*
     * **Quest では机のパッドを開かない**。
     *
     * Horizon OS は Touch を **Android のゲームパッドとしても**見せる。開いてしまうと
     * 「実機のパッドが繋がっていればそちらを優先」の合流（`right_stick_x/y`）が
     * **無入力の幽霊**に食われ、右スティックだけが死ぬ。☰ の 1 押しも XR と SDL の
     * 両方から届いて 2 縁になり、機能メニューが開いてすぐ閉じる。
     *
     * この機の Touch は `feed_external`（XR の actions）から届くので、
     * **入口で開かないのが正しい。**名前は残す——同じ病気が別の機で再発したとき、
     * logcat のこの 1 行が最初の手がかりになる。
     */
    const char *const ghost = SDL_GameControllerNameForIndex(joystick_index);
    std::fprintf(stderr, "[hd2d] パッド: %s は開きません（Quest。Touch は XR から届きます）\n",
        (ghost != nullptr) ? ghost : i18n::tr("hd2d.ui.game-pad.no-name"));
    return;
#else
    SDL_GameController *const pad = SDL_GameControllerOpen(joystick_index);
    if (pad == nullptr) {
        std::fprintf(stderr, i18n::tr("hd2d.ui.game-pad.hd2d-could-not-open-the-pad-s-n"), SDL_GetError());
        return;
    }
    this->controller_ = pad;
    SDL_Joystick *const joystick = SDL_GameControllerGetJoystick(pad);
    this->joystick_id_ = (joystick != nullptr) ? SDL_JoystickInstanceID(joystick) : -1;
    const char *const name = SDL_GameControllerName(pad);
    this->name_ = (name != nullptr) ? name : i18n::tr("hd2d.ui.game-pad.no-name");
    //! **挿した直後は「握ったまま」を走らせない**（約束 2）。
    this->forget_hold();
    /*
     * **開いたら名前を 1 行出す**。
     * 幽霊のパッドを開いてしまう病気（罠 Q-12）は症状が経路ごとに変わって読みにくいが、
     * この行が出ているかどうかは一目で分かる。
     */
    std::fprintf(stderr, i18n::tr("hd2d.ui.game-pad.hd2d-pad-s-n"), this->name_.c_str());
#endif
}

void GamePad::close()
{
    if (this->controller_ != nullptr) {
        SDL_GameControllerClose(static_cast<SDL_GameController *>(this->controller_));
        this->controller_ = nullptr;
    }
    this->joystick_id_ = -1;
    this->name_.clear();
    this->forget_hold();
}

void GamePad::forget_hold()
{
    this->held_dx_ = 0;
    this->held_dy_ = 0;
    this->next_repeat_ms_ = 0;
    this->rearm_pending_ = true;
    this->stick_x_ = 0;
    this->stick_y_ = 0;
    for (bool &down : this->dpad_) {
        down = false;
    }
    for (bool &down : this->trigger_down_) {
        down = false;
    }
    this->pressed_.clear();
    //! 修飾を握ったままにしない（焦点が戻った瞬間に同時押しが成立すると気味が悪い）。
    this->chord_.forget();
    //! VR の Touch も同じ（前回の状態を残すと、戻った瞬間に離しの縁が立つ）。
    for (bool &down : this->external_prev_) {
        down = false;
    }
    this->external_right_x_ = 0.f;
    this->external_right_y_ = 0.f;
}

bool GamePad::on_event(const SDL_Event &event)
{
    switch (event.type) {
    case SDL_CONTROLLERDEVICEADDED:
        this->open(event.cdevice.which);
        return true;
    case SDL_CONTROLLERDEVICEREMOVED:
        if (event.cdevice.which == this->joystick_id_) {
            this->close();
        }
        return true;
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP: {
        const bool down = (event.type == SDL_CONTROLLERBUTTONDOWN);
        PadInput input = PadInput::Count;
        switch (event.cbutton.button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            this->dpad_[0] = down;
            return true;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            this->dpad_[1] = down;
            return true;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            this->dpad_[2] = down;
            return true;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            this->dpad_[3] = down;
            return true;
        case SDL_CONTROLLER_BUTTON_A:
            input = PadInput::A;
            break;
        case SDL_CONTROLLER_BUTTON_B:
            input = PadInput::B;
            break;
        case SDL_CONTROLLER_BUTTON_X:
            input = PadInput::X;
            break;
        case SDL_CONTROLLER_BUTTON_Y:
            input = PadInput::Y;
            break;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
            input = PadInput::LeftShoulder;
            break;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
            input = PadInput::RightShoulder;
            break;
        case SDL_CONTROLLER_BUTTON_BACK:
            input = PadInput::Back;
            break;
        case SDL_CONTROLLER_BUTTON_START:
            input = PadInput::Start;
            break;
        case SDL_CONTROLLER_BUTTON_LEFTSTICK:
            input = PadInput::LeftStick;
            break;
        case SDL_CONTROLLER_BUTTON_RIGHTSTICK:
            input = PadInput::RightStick;
            break;
        default:
            return true; // 知らないボタンは食って捨てる
        }
        if (input == PadInput::Count) {
            return true;
        }
        /*
         * **同時押しは離しも見る**（`PadChord`）。修飾（LB/RB）は押した瞬間には出さず、
         * 単独だったと確定する離しで初めて出す。ほかのボタンは今までどおり押した瞬間。
         */
        if (down) {
            int mods = 0;
            if (!this->chord_.press(input, mods)) {
                this->pressed_.push_back(PadPress{ input, mods });
            }
        } else {
            PadInput solo = PadInput::Count;
            if (this->chord_.release(input, solo)) {
                this->pressed_.push_back(PadPress{ solo, 0 });
            }
        }
        return true;
    }
    case SDL_CONTROLLERAXISMOTION:
        switch (event.caxis.axis) {
        case SDL_CONTROLLER_AXIS_LEFTX:
            this->stick_x_ = latch(this->stick_x_, event.caxis.value);
            return true;
        case SDL_CONTROLLER_AXIS_LEFTY:
            this->stick_y_ = latch(this->stick_y_, event.caxis.value);
            return true;
        case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
        case SDL_CONTROLLER_AXIS_TRIGGERRIGHT: {
            const int which = (event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT) ? 0 : 1;
            //! **立ち上がりでだけ**発火させる（約束 5。押しっぱなしで連射しない）。
            if (!this->trigger_down_[which] && (event.caxis.value > kTriggerDown)) {
                this->trigger_down_[which] = true;
                const PadInput input = (which == 0) ? PadInput::LeftTrigger : PadInput::RightTrigger;
                int mods = 0;
                //! トリガは修飾ではないので `press` は必ず偽を返す。同時押しの層だけ受け取る。
                (void)this->chord_.press(input, mods);
                this->pressed_.push_back(PadPress{ input, mods });
            } else if (this->trigger_down_[which] && (event.caxis.value < kTriggerUp)) {
                this->trigger_down_[which] = false;
            }
            return true;
        }
        default:
            return true;
        }
    default:
        return false;
    }
}

namespace {

//! 右スティックの 1 軸を −1〜+1 へ。**死区の扱いは 2 軸で同じ**（別々に書くと片方だけ直る）。
float right_stick_axis(void *controller, SDL_GameControllerAxis axis)
{
    if (controller == nullptr) {
        return 0.f;
    }
    const int raw = SDL_GameControllerGetAxis(static_cast<SDL_GameController *>(controller), axis);
    if (std::abs(raw) < kStickLeave) {
        return 0.f; //!< 死区。左スティックの「戻した」しきい値と揃える
    }
    // 死区の外側を 0〜1 へ引き伸ばす（死区の縁で急に最高速へ飛ばないように）。
    const float span = 32767.f - static_cast<float>(kStickLeave);
    const float over = static_cast<float>(std::abs(raw) - kStickLeave) / ((span > 1.f) ? span : 1.f);
    const float clamped = (over > 1.f) ? 1.f : over;
    return (raw < 0) ? -clamped : clamped;
}

} // namespace

float GamePad::right_stick_x() const
{
    if (this->controller_ != nullptr) {
        return right_stick_axis(this->controller_, SDL_CONTROLLER_AXIS_RIGHTX);
    }
    return this->external_right_x_;
}

float GamePad::right_stick_y() const
{
    if (this->controller_ != nullptr) {
        return right_stick_axis(this->controller_, SDL_CONTROLLER_AXIS_RIGHTY);
    }
    return this->external_right_y_;
}

/*!
 * @brief SDL 以外の入力（VR の Touch）を流し込む。
 * @details **縁を取るのはここだけ**にする。同じ仕掛けを XR 側に書くと、
 * ヒステリシスも連射も同時押しも二重になって必ず片方だけ直る。
 */
void GamePad::feed_external(const ExternalPadState &state)
{
    this->external_connected_ = true;
    for (int i = 0; i < kPadInputCount; ++i) {
        const bool down = state.button[i];
        if (down == this->external_prev_[i]) {
            continue;
        }
        this->external_prev_[i] = down;
        const PadInput input = static_cast<PadInput>(i);
        //! 同時押しの約束は SDL の道と同じ（`PadChord`）。
        if (down) {
            int mods = 0;
            if (!this->chord_.press(input, mods)) {
                this->pressed_.push_back(PadPress{ input, mods });
            }
        } else {
            PadInput solo = PadInput::Count;
            if (this->chord_.release(input, solo)) {
                this->pressed_.push_back(PadPress{ solo, 0 });
            }
        }
    }
    //! 死区の出入りは SDL と同じしきい値を使う（約束 3）。
    const auto to_raw = [](float v) {
        return static_cast<int>(std::clamp(v, -1.f, 1.f) * 32767.f);
    };
    this->stick_x_ = latch(this->stick_x_, to_raw(state.left_x));
    this->stick_y_ = latch(this->stick_y_, to_raw(state.left_y));
    this->external_right_x_ = std::clamp(state.right_x, -1.f, 1.f);
    this->external_right_y_ = std::clamp(state.right_y, -1.f, 1.f);
}

void GamePad::current_direction(int &dx, int &dy) const
{
    dx = 0;
    dy = 0;
    if (this->dpad_[0]) {
        dy -= 1;
    }
    if (this->dpad_[1]) {
        dy += 1;
    }
    if (this->dpad_[2]) {
        dx -= 1;
    }
    if (this->dpad_[3]) {
        dx += 1;
    }
    //! 十字が中立ならスティックを見る（両方入っているときは十字を優先）。
    if ((dx == 0) && (dy == 0)) {
        dx = this->stick_x_;
        dy = this->stick_y_;
    }
}

bool GamePad::poll_direction(std::uint32_t now_ms, bool focused, int &dx, int &dy, int walk_repeat_ms)
{
    /*
     * **外から流し込まれた状態（VR の Touch）も見る。**ここを
     * `controller_ == nullptr` だけで弾いていたので、机のパッドを挿していない機械では
     * **Touch のスティックで 1 歩も歩けなかった**（2026-08-14 に気づいた）。
     */
    if ((this->controller_ == nullptr) && !this->external_connected_) {
        return false;
    }
    if (!focused) {
        //! **裏に回した窓のパッドで歩かせない**（約束 1）。戻ったら中立から数え直す。
        this->forget_hold();
        return false;
    }

    int now_dx = 0;
    int now_dy = 0;
    this->current_direction(now_dx, now_dy);

    if ((now_dx == 0) && (now_dy == 0)) {
        this->held_dx_ = 0;
        this->held_dy_ = 0;
        this->rearm_pending_ = false; //!< 中立を見た。ここから受け付ける（約束 2）
        return false;
    }
    if (this->rearm_pending_) {
        return false; // まだ一度も中立を見ていない（挿した直後・焦点が戻った直後）
    }

    /*
     * 歩調。**歩いているときは 1 歩目のあとの待ちを置かない**（2026-08-14 に決めた）。
     * 待ちがあると 1 歩ごとに止まって見え、一人称では酔いに直結する。
     * メニューのカーソルなど、それ以外は今までどおり「1 歩目 → 待つ → 連射」。
     */
    const bool walking = (walk_repeat_ms > 0);
    const std::uint32_t first_ms = walking ? static_cast<std::uint32_t>(walk_repeat_ms) : kFirstRepeatMs;
    const std::uint32_t next_ms = walking ? static_cast<std::uint32_t>(walk_repeat_ms) : kNextRepeatMs;

    const bool changed = (now_dx != this->held_dx_) || (now_dy != this->held_dy_);
    if (changed) {
        /*
         * **入れ替えでも間隔を置く**（約束 4）。斜めへ倒す途中の一瞬（真横 → 斜め）を
         * 1 歩として数えると、狙った向きと違う方へ 1 歩出る。
         */
        this->held_dx_ = now_dx;
        this->held_dy_ = now_dy;
        this->next_repeat_ms_ = now_ms + first_ms;
        dx = now_dx;
        dy = now_dy;
        return true;
    }
    if (now_ms < this->next_repeat_ms_) {
        return false;
    }
    this->next_repeat_ms_ = now_ms + next_ms;
    dx = now_dx;
    dy = now_dy;
    return true;
}

bool GamePad::take_pressed(PadPress &out)
{
    if (this->pressed_.empty()) {
        return false;
    }
    out = this->pressed_.front();
    this->pressed_.erase(this->pressed_.begin());
    return true;
}

PadPress PadBinds::input_for(int action) const
{
    if (action == 0) {
        return PadPress{};
    }
    for (int mods = 0; mods < kPadModLayerCount; ++mods) {
        for (int i = 0; i < kPadInputCount; ++i) {
            if (this->command[mods][i] == action) {
                return PadPress{ static_cast<PadInput>(i), mods };
            }
        }
    }
    return PadPress{};
}

bool PadBinds::assign(int action, PadInput input, int mods)
{
    if ((action == 0) || (input == PadInput::Count)) {
        return false;
    }
    if ((mods < 0) || (mods >= kPadModLayerCount)) {
        return false;
    }
    /*
     * **固定（A / B / Back）が固定なのは「単独押しのとき」だけ**（2026-08-14 に気づいた
     * 「同時押しなら AB は割り当ててよいのでは？」）。あの 3 つを押さえているのは
     * 決定・取消・メニューという**単独押しの役**で、層の中では誰も使っていない
     * ——同時押しはメニュー操作へ回さない作りにしてあるので（`hd2d_app.cpp` の
     * `route_pad_press`）、`LB＋A` を割り当てても決定が奪われることはない。
     * 枠を 3 つ×3 層ぶん遊ばせておく理由が無い。
     */
    if ((mods == 0) && pad_input_is_fixed(input)) {
        return false;
    }
    /*
     * 修飾そのもの（LB/RB）は**層 0 でだけ**受け付ける。`LB＋LB` は押しようがなく、
     * `LB＋RB` の層に RB を置くのも同じ（あの層は 2 つ握っている状態そのもの）。
     * 受け付けると、二度と押せない枠に割り当てが吸い込まれる。
     */
    if ((mods != 0) && pad_input_is_modifier(input)) {
        return false;
    }
    //! **先客から外す。**同じ操作が 2 つの枠に乗っている表は画面に出せない。
    this->clear(action);
    this->command[mods][static_cast<int>(input)] = action;
    return true;
}

void PadBinds::clear(int action)
{
    if (action == 0) {
        return;
    }
    for (int mods = 0; mods < kPadModLayerCount; ++mods) {
        for (int i = 0; i < kPadInputCount; ++i) {
            if (this->command[mods][i] == action) {
                this->command[mods][i] = 0;
            }
        }
    }
}

std::string PadBinds::to_line() const
{
    std::string line;
    for (int mods = 0; mods < kPadModLayerCount; ++mods) {
        for (int i = 0; i < kPadInputCount; ++i) {
            if (this->command[mods][i] == 0) {
                continue;
            }
            if (!line.empty()) {
                line += ",";
            }
            //! **層 0 は綴りを付けない。**前の版が書いた cfg と同じ形のままにする。
            const std::string tag = pad_mods_tag(mods);
            if (!tag.empty()) {
                line += tag + "+";
            }
            line += std::string(pad_input_name(static_cast<PadInput>(i))) + ":"
                + std::to_string(this->command[mods][i]);
        }
    }
    return line;
}

void PadBinds::parse(const std::string &line)
{
    std::size_t at = 0;
    while (at < line.size()) {
        const std::size_t comma = line.find(',', at);
        const std::string item = line.substr(at, (comma == std::string::npos) ? std::string::npos : (comma - at));
        const std::size_t colon = item.find(':');
        if (colon != std::string::npos) {
            std::string name = item.substr(0, colon);
            /*
             * `C+X` の形なら層つき。**`+` が無ければ層 0** なので、前の版が書いた
             * `pad_bind=X:23,Y:21,...` がそのまま読める。
             */
            int mods = 0;
            if (const std::size_t plus = name.find('+'); plus != std::string::npos) {
                mods = parse_pad_mods(name.substr(0, plus));
                name = name.substr(plus + 1);
            }
            PadInput input = PadInput::Count;
            if ((mods >= 0) && parse_pad_input(name, input)) {
                this->command[mods][static_cast<int>(input)] = std::atoi(item.substr(colon + 1).c_str());
            }
            // 知らない綴りは黙って飛ばす（版が進んだ cfg でも落ちない）
        }
        if (comma == std::string::npos) {
            break;
        }
        at = comma + 1;
    }
}

bool emit_core_command(int command_id, const std::vector<PadCommand> &commands,
    presentation::InputEventsMessage &out)
{
    if (command_id <= 0) {
        return false; // 0 = 割り当て無し / 負 = この exe 自身の操作（呼び出し側が処理済み）
    }
    for (const auto &command : commands) {
        if (command.id != command_id) {
            continue;
        }
        if (command.keys.empty()) {
            /*
             * **解決不能**（いまのキー配列にそのコマンドが無い）。黙って何も起きないより、
             * 割り当てが死んでいることが分かるほうがよい。
             */
            std::fprintf(stderr, i18n::tr("hd2d.ui.game-pad.hd2d-the-bound-s-cannot-be-produced-on"),
                command.label_utf8.c_str());
            return false;
        }
        for (const int key : command.keys) {
            if ((key <= 0) || (key > 255)) {
                continue; // 値域の外は捨てる（`KeysMessage` の約束と同じ）
            }
            /*
             * **制御文字は `chr` に入れて送れない**（2026-08-11。ロール確認の Enter と
             * 同じ病＝罠 137）。復号側（`protocol_messages.cpp`）は `chr` を印字文字
             * （0x20〜0x7E）に限っていて、範囲外は**黙って捨てる**。
             *
             * コアのキー配列はコマンドを制御文字で持つことがある——ローグライク配列の
             * 「掘る」は `^t`（0x14）で、ここが `chr=0x14` のまま送ると**ボタンを押しても
             * 何も起きない**。制御キーは専用イベントか、Ctrl フラグ付きの英字で送る
             * （アダプタが `ctrl && 英字` を `c & 0x1F` へ戻す。`translate_key_down` と同じ道）。
             */
            presentation::InputEventWire wire;
            if (key == 0x1B) {
                wire.e = "cancel";
            } else if ((key == 0x0D) || (key == 0x0A)) {
                wire.e = "confirm";
            } else if (key == 0x09) {
                wire.e = "key";
                wire.name = "tab";
            } else if (key == 0x08) {
                wire.e = "key";
                wire.name = "backspace";
            } else if (key == 0x7F) {
                wire.e = "key";
                wire.name = "delete";
            } else if ((key >= 0x01) && (key <= 0x1A)) {
                //! Ctrl+英字（`^a`=0x01 〜 `^z`=0x1A）。
                wire.e = "key";
                wire.chr = std::string(1, static_cast<char>('a' + (key - 0x01)));
                wire.ctrl = true;
            } else if ((key >= 0x20) && (key <= 0x7E)) {
                wire.e = "key";
                wire.chr = std::string(1, static_cast<char>(key));
            } else {
                //! 送れない形は**黙って捨てない**（捨てると「押しても何も起きない」になる）。
                std::fprintf(stderr, i18n::tr("hd2d.ui.game-pad.hd2d-s-has-key-0x-02x-which-cannot"),
                    command.label_utf8.c_str(), static_cast<unsigned>(key));
                continue;
            }
            out.events.push_back(wire);
        }
        return true;
    }
    return false;
}

PadMacroFlags make_pad_macro_flags(const std::vector<std::vector<int>> &macro_triggers)
{
    PadMacroFlags flags;
    for (int mods = 0; mods < kPadModLayerCount; ++mods) {
        for (int i = 0; i < kPadInputCount; ++i) {
            const std::vector<int> sequence = pad_input_trigger_sequence(static_cast<PadInput>(i), mods);
            if (sequence.empty()) {
                continue; // トリガーを持たないボタン（A・B・Back）
            }
            for (const auto &pattern : macro_triggers) {
                if (pattern == sequence) {
                    flags.has[mods][i] = true;
                    break;
                }
            }
        }
    }
    return flags;
}

bool emit_pad_macro_trigger(PadInput input, int mods, presentation::InputEventsMessage &out)
{
    const std::vector<int> sequence = pad_input_trigger_sequence(input, mods);
    if (sequence.empty()) {
        return false;
    }
    presentation::InputEventWire wire;
    wire.e = "keyseq";
    wire.bytes = sequence;
    out.events.push_back(std::move(wire));
    return true;
}

bool apply_default_pad_binds(PadBinds &binds, const std::vector<PadCommand> &commands,
    const std::string &core_name)
{
    /*
     * **コアごとの既定**（2026-09-01 に決めた。遊びながら組み直した `hd2d-*.cfg` を写した）。
     *
     * **番号（`id`）ではなく字で持つ**のがこの関数の要点である——`id` はコアが名乗る
     * 表の並び順で動くので、焼くと表が 1 つ増えた日に黙ってずれる。実際、
     * 2026-09-01 の朝に採った cfg では、幻想蛮怒で決めた番号を引き継いだ変愚が
     * `X` に「助言を見る」・`Y` に「アイテムを壊す」を指していた。
     *
     * 画面側の操作（`kAction*`）は**コアに依らない**ので番号のまま持つ。
     */
    struct Row {
        PadInput input;
        int mods;        //!< `kPadModCtrl`（LB 押し）／`kPadModAlt`（RB 押し）。0 = 単独
        int command;     //!< Angband のコマンド 1 文字。**0 なら `action` を使う**
        int action;      //!< 画面側の操作（`kAction*`）。`command` が 0 のときだけ見る
        const char *note;
    };
    /*
     * **変愚蛮怒系（変愚・短愚・幻想）と Frox は同じ並びである**
     * ——3 コアから採った字が 1 つも違わなかった（2026-09-01 に実測）。
     * 短愚は決めたことで変愚に合わせる。
     */
    static const Row kHengband[] = {
        { PadInput::X, 0, 'g', 0, i18n::tr("hd2d.app.hd2d-app.pick-up") },
        { PadInput::Y, 0, 'f', 0, i18n::tr("hd2d.app.hd2d-app.fire-a-missile") },
        { PadInput::LeftShoulder, 0, '<', 0, i18n::tr("hd2d.app.hd2d-app.go-up-the-stairs") },
        { PadInput::RightShoulder, 0, '>', 0, i18n::tr("hd2d.app.hd2d-app.go-down-the-stairs") },
        //! **`s`（探す）であって `S`（探索モードの ON/OFF）ではない**（`game_pad.h` の注記）。
        { PadInput::LeftTrigger, 0, 's', 0, i18n::tr("hd2d.app.hd2d-app.search") },
        { PadInput::RightTrigger, 0, 'T', 0, i18n::tr("hd2d.app.hd2d-app.tunnel") },
        { PadInput::Start, 0, 0, kActionCommandMenu, i18n::tr("hd2d.app.hd2d-app.command-menu") },
        { PadInput::LeftStick, 0, 0, kActionFpsToggle,
            i18n::tr("hd2d.app.hd2d-app.toggle-first-person") },
        { PadInput::X, kPadModCtrl, 'z', 0, i18n::tr("hd2d.app.hd2d-app.zap-a-rod") },
        { PadInput::Y, kPadModCtrl, 'A', 0, i18n::tr("hd2d.app.hd2d-app.activate") },
        { PadInput::A, kPadModAlt, 'o', 0, i18n::tr("hd2d.app.hd2d-app.open-a-door") },
        { PadInput::B, kPadModAlt, 'c', 0, i18n::tr("hd2d.app.hd2d-app.close-a-door") },
        { PadInput::X, kPadModAlt, 'a', 0, i18n::tr("hd2d.app.hd2d-app.aim-a-wand") },
        { PadInput::Y, kPadModAlt, 'u', 0, i18n::tr("hd2d.app.hd2d-app.use-a-staff") },
    };
    /*
     * **Sil-Q は別の並び。** `LT` は割り当てが無い（そう組んである）。
     * **`Start` はコアの命令**（`m` 主メニュー）で、画面側の操作ではない。
     * `RS` の `S` は**隠密の構え**である——変愚の `S`（探索モードの ON/OFF）とは別物で、
     * 上の注記と逆に見えるが矛盾しない。Sil-Q には「探す」が無い（探索が自動）。
     */
    static const Row kSilq[] = {
        { PadInput::X, 0, '/', 0, i18n::tr("hd2d.app.hd2d-app.alter") },
        { PadInput::Y, 0, 'g', 0, i18n::tr("hd2d.app.hd2d-app.pick-up") },
        { PadInput::LeftShoulder, 0, '<', 0, i18n::tr("hd2d.app.hd2d-app.go-up-the-stairs") },
        { PadInput::RightShoulder, 0, '>', 0, i18n::tr("hd2d.app.hd2d-app.go-down-the-stairs") },
        { PadInput::RightTrigger, 0, 'z', 0, i18n::tr("hd2d.app.hd2d-app.stay") },
        { PadInput::Start, 0, 'm', 0, i18n::tr("hd2d.app.hd2d-app.main-menu") },
        { PadInput::LeftStick, 0, 0, kActionFpsToggle,
            i18n::tr("hd2d.app.hd2d-app.toggle-first-person") },
        { PadInput::RightStick, 0, 'S', 0, i18n::tr("hd2d.app.hd2d-app.stealth-mode") },
        { PadInput::A, kPadModCtrl, 'w', 0, i18n::tr("hd2d.app.hd2d-app.wield") },
        { PadInput::B, kPadModCtrl, 'r', 0, i18n::tr("hd2d.app.hd2d-app.take-off") },
        { PadInput::Y, kPadModCtrl, 'd', 0, i18n::tr("hd2d.app.hd2d-app.drop-an-item") },
        { PadInput::B, kPadModAlt, 'c', 0, i18n::tr("hd2d.app.hd2d-app.close-a-door") },
        { PadInput::X, kPadModAlt, 'f', 0, i18n::tr("hd2d.app.hd2d-app.fire-a-missile") },
        { PadInput::Y, kPadModAlt, 'F', 0, i18n::tr("hd2d.app.hd2d-app.fire-second-quiver") },
    };

    const Row *rows = kHengband;
    std::size_t row_count = sizeof(kHengband) / sizeof(kHengband[0]);
    if (core_name == "silq") {
        rows = kSilq;
        row_count = sizeof(kSilq) / sizeof(kSilq[0]);
    }

    bool any = false;
    for (std::size_t ri = 0; ri < row_count; ++ri) {
        const Row &row = rows[ri];
        //! 画面側の操作はコアの表を要らない。**先に片づける。**
        if (row.command == 0) {
            if (binds.assign(row.action, row.input, row.mods)) {
                any = true;
            }
            continue;
        }
        const PadCommand *found = nullptr;
        for (const auto &command : commands) {
            if (command.command == row.command) {
                found = &command;
                break;
            }
        }
        if (found == nullptr) {
            //! **黙って諦めない。**版によって無いこともあるので、消えたことが分かるように残す。
            std::fprintf(stderr, i18n::tr("hd2d.ui.game-pad.hd2d-default-binding-s-c-is-not-in"),
                row.note, static_cast<char>(row.command));
            continue;
        }
        if (binds.assign(found->id, row.input, row.mods)) {
            any = true;
        }
    }
    return any;
}

std::vector<std::string> build_pad_bind_items(const PadBinds &binds, const std::vector<PadCommand> &commands,
    const PadMacroFlags &macros, int chord_mask)
{
    std::vector<std::string> items;
    items.reserve(static_cast<std::size_t>(kPadInputCount) + 1U);
    /*
     * **修飾を押している間は同時押しの層だけを出す。**単独の割り当てを並べたままだと、
     * 押しても走らないものが並ぶ（同時押しは必ずマクロへ行く）。
     */
    if (chord_mask != 0) {
        std::string head = i18n::tr("hd2d.ui.game-pad.chord");
        if ((chord_mask & kPadModCtrl) != 0) {
            head += "LB";
        }
        if (chord_mask == (kPadModCtrl | kPadModAlt)) {
            head += i18n::tr("hd2d.ui.game-pad.x08ae1b");
        }
        if ((chord_mask & kPadModAlt) != 0) {
            head += "RB";
        }
        head += i18n::tr("hd2d.ui.game-pad.x8d4653");
        for (int i = 0; i < kPadInputCount; ++i) {
            const auto input = static_cast<PadInput>(i);
            /*
             * 相手にならないのは**修飾そのもの**だけ（`LB＋LB` は押しようがない）。
             * A / B / Back は**層の中では空いている**ので並べる（2026-08-14 に気づいた。
             * `PadBinds::assign` の注記）。並べないと、割り当てたのに帯に出ない。
             */
            if (pad_input_is_modifier(input)) {
                continue;
            }
            /*
             * **層でも規則は同じ**——割り当てがあればその名前、無ければマクロ、
             * どちらも無ければ `—`。単独押しと違う読み方をさせない。
             */
            std::string label;
            const int action = binds.command[chord_mask][i];
            if (action < 0) {
                label = ui_action_label(action);
            } else if (action > 0) {
                for (const auto &command : commands) {
                    if (command.id == action) {
                        label = command.keys.empty() ? (command.label_utf8 + i18n::tr("hd2d.ui.game-pad.not-available")) : command.label_utf8;
                        break;
                    }
                }
                if (label.empty()) {
                    label = i18n::tr("hd2d.ui.game-pad.command") + std::to_string(action);
                }
            } else if (macros(input, chord_mask)) {
                label = i18n::tr("hd2d.ui.game-pad.macro");
            } else {
                label = "—";
            }
            items.push_back(std::string(pad_input_name(input)) + ":" + label);
        }
        if (items.empty()) {
            items.push_back(head + i18n::tr("hd2d.ui.game-pad.there-is-no-button-available-to-bind"));
        } else {
            items.insert(items.begin(), head);
        }
        return items;
    }

    //! 方向は割り当ての対象外（斜めの合成と連射を担っている）ので固定の文。
    items.emplace_back(i18n::tr("hd2d.ui.game-pad.d-pad-left-stick-move-and-choose"));

    for (int i = 0; i < kPadInputCount; ++i) {
        const auto input = static_cast<PadInput>(i);
        std::string label;
        switch (input) {
        //! 固定の 3 つ（`pad_input_is_fixed`）。**割り当てを引かずに役をそのまま書く。**
        case PadInput::A:
            label = i18n::tr("hd2d.ui.feature-menu.confirm");
            break;
        case PadInput::B:
            label = i18n::tr("hd2d.ui.feature-menu.cancel");
            break;
        case PadInput::Back:
            label = i18n::tr("hd2d.ui.feature-menu.feature-menu");
            break;
        default:
            break;
        }
        if (label.empty()) {
            const int action = binds.command[0][i];
            if (action < 0) {
                label = ui_action_label(action);
            } else if (action > 0) {
                for (const auto &command : commands) {
                    if (command.id != action) {
                        continue;
                    }
                    //! **出せないものは出せないと書く**（黙って何も起きないのがいちばん困る）。
                    label = command.keys.empty() ? (command.label_utf8 + i18n::tr("hd2d.ui.game-pad.not-available")) : command.label_utf8;
                    break;
                }
                if (label.empty()) {
                    label = i18n::tr("hd2d.ui.game-pad.command") + std::to_string(action);
                }
            }
        }
        /*
         * 割り当てが無く、**そのボタンにマクロが登録されている**なら「マクロ」と出す
         * （2026-08-14 に決めた）。押せば走るのはマクロなので、ここが空欄のままだと
         * 「登録したのに反応しないのでは」と読めてしまう。
         *
         * **見るのは割り当ての有無**（`binds.command[i]`）であって、名前を引けたかどうかでは
         * ない。割り当て済みなのに名前が引けないとき（コアの表がまだ来ていない等）に
         * 「マクロ」と出すと、走るのは割り当ての方なので嘘になる。
         */
        if ((binds.command[0][i] == kActionNone) && !pad_input_is_fixed(input) && macros(input)) {
            label = i18n::tr("hd2d.ui.game-pad.macro");
        }
        if (label.empty()) {
            label = "—"; //!< 未割当も出す。「割り当てが無い」ことも利用者が知りたい情報
        }
        items.push_back(std::string(pad_input_name(input)) + ":" + label);
    }
    return items;
}

} // namespace hd2d
