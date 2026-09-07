# x64-windows（vcpkg 既定の写し）＋ SDL2_ttf だけ /O1 で組ませる。
#
# **なぜ**: SDL_ttf 2.24 の `NORMAL_MONO`（1bpp の埋め込みビットマップを 8bit の
# 被覆率へ展開する道。`TTF_RenderUTF8_Blended` が通る）を、MSVC 18.8.2 が
# **x64 の /O2 でだけ誤って組む**。1 バイト 8 画素の**最後の 1 画素にそのバイトの
# 最上位ビットが入る**ので、MS ゴシック 16px の横棒に穴が開き、亠 の縦棒が消える。
#
#   x86 /O2:  .#############..   （正）
#   x64 /O2:  .######.######.#   （誤）
#   x64 /O1:  .#############..   （正）
#
# FreeType が返す 1bpp のバイト列は x86 と x64 で**完全に同じ**であることを確認済み
# つまり字の形の出どころは無事で、
# 展開の 1 ループだけが化ける。だからライブラリ側を 1 段弱い最適化で組み直せば直る。
# 字は 1 度焼いてアトラスに貯めるので、/O1 による速さの損は測れない。
#
# 使い方:
#   vcpkg install sdl2-ttf:x64-windows --overlay-triplets=<repo>\tools\vcpkg\triplets
#
# 直ったかの確かめ方は「同じ字が x86 と x64 で同じ点に焼けるか」。
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)

# **`VCPKG_C_FLAGS` では効かない。** そちらは `CMAKE_C_FLAGS` へ入るので、後から
# 並ぶ `CMAKE_C_FLAGS_RELEASE` の `/O2` に負ける。構成ごとの口へ足すこと。
if(PORT STREQUAL "sdl2-ttf")
    set(VCPKG_C_FLAGS_RELEASE "/O1")
    set(VCPKG_CXX_FLAGS_RELEASE "/O1")
endif()

# **OpenAL Soft は 1 段古いツールセットで組ませる**。
#
# **なぜ**: openal-soft 1.25.1 の `alc/alc.cpp:1763` が
# `std::ranges::for_each(rng | std::views::transform(&T::operator*) | std::views::join, f)`
# を使っており、**MSVC 14.52（VS 18.8）の STL がこれを受け付けない**（C3889。
# `_Transform_fn` / `_For_each_fn` に一致する呼び出し演算子が無い）。
# 上流の版はこの MSVC より前に出ているので、**古い STL なら通る**。
# 直すべきは当方ではなく上流／port なので、こちらは組む道具のほうを戻す。
#
# 使い方:
#   vcpkg install openal-soft:x64-windows --overlay-triplets=<repo>/tools/vcpkg/triplets
if(PORT STREQUAL "openal-soft")
    set(VCPKG_PLATFORM_TOOLSET_VERSION 14.44)
endif()
