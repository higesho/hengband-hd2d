/*!
 * @file gl_program.h
 * @brief GLSL の組み立て（compile → link）と後始末。
 *
 * @details のとおりシェーダは正攻法で書く。
 * ここはその入口で、**失敗したら必ず理由（info log）を持って帰る**ことだけを引き受ける。
 * 黙って 0 を返すと、後で「何も出ない」の原因がシェーダなのか行列なのか分からなくなる。
 */
#pragma once

#include "render/gl_core.h"

#include <string>

namespace hd2d {

/*!
 * @brief 頂点・フラグメントの 2 本から program を作る。
 * @param[out] err 失敗の理由（GLSL の info log そのまま）。
 * @return program 名。失敗したら 0。
 * @note 成功したら shader オブジェクトはその場で delete する（program が参照を持つ）。
 */
gl::GLuint compile_program(const char *vertex_source, const char *fragment_source, std::string &err);

} // namespace hd2d
