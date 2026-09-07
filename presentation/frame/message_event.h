/*!
 * @file message_event.h
 * @brief メッセージイベント（設計書 §4.5）。text は Bridge が UTF-8 化して格納。
 */
#pragma once

#include <cstdint>
#include <string>

struct MessageEvent {
    uint32_t seq{};
    uint8_t color{};
    std::string text_utf8;
};
