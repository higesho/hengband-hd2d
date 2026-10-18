/*!
 * @file tb_crash.h
 * @brief 落ちたときに呼ばれていた関数の並びを stderr へ書く（`tb_crash.cpp`）。
 */
#pragma once

//! 起動の最初に 1 回呼ぶ。Windows 以外では何もしない。
void tb_install_crash_handler();
