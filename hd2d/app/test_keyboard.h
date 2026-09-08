// 明示的に有効にしたテストでだけ、SDL のキーボードイベントを生成する。
#pragma once
#include <SDL2/SDL.h>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace hd2d {
// JSON 1 行を KEYDOWN / TEXTINPUT / KEYUP に変換する。失敗時は出力を空にする。
bool compile_test_key(const std::string &line, std::vector<SDL_Event> &events, std::string &error);

class TestKeyboard {
public:
    // 同じパスでの再設定は何もしない。コア再起動で入力を先頭から再実行しない。
    bool configure(const std::string &path, std::string &error);
    // 1 フレームに最大 1 行。末尾の未完成行は改行されるまで実行しない。
    void poll(const char *phase);
private:
    std::string path_;
    std::ifstream input_;
    std::ofstream ack_;
    std::string pending_;
    std::uint64_t offset_{ 0 };
    std::uint64_t line_{ 0 };
    bool enabled_{ false };
};

TestKeyboard &test_keyboard();
} // namespace hd2d
