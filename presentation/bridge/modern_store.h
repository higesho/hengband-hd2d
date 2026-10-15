#pragma once
#include "frame/game_frame.h"
#include "frame/protocol_messages.h"
#include <vector>
class PlayerType;
namespace presentation {
void set_modern_store_enabled(bool enabled);
bool modern_store_enabled();
void set_modern_store_language(const std::string &language);
void modern_store_reject_queued_request(int request_id);
void capture_modern_store(GameFrame &frame, PlayerType *player);
// Called only on the core thread at an input boundary, never the receiver thread.
bool modern_store_input(const InputEventWire &event, std::vector<int> &keys);
}
