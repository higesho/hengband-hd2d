#pragma once

#include <string>

namespace umoria {

//! 怪物の思い出の日本語の本文（原作 recall.cpp の memoryRecall と同じ条件・同じ順で、知っていることだけを書く）。
//! 先頭は見出し（例「コボルド:」）。改行で段落を分けてよい。折り返しは呼び手がする。
std::string recall_japanese(int monster_id);

} // namespace umoria
