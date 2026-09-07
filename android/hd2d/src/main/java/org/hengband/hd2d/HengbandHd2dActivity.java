package org.hengband.hd2d;

import android.os.Bundle;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

/**
 * 変愚蛮怒 ボクセル HD2D Android 版のエントリ Activity。
 *
 * <p>ロジックはすべてネイティブ側（platform/android/hd2d_entry_android.cpp）にある。
 * ここは「どの共有ライブラリを、どの順で読むか」と、プレイ中に画面を消させないことだけを担う。
 * 旧 SDL2 UI 版（org.hengband.android.HengbandActivity）と同じ形。
 */
public class HengbandHd2dActivity extends SDLActivity {

    @Override
    protected String[] getLibraries() {
        // 依存の順に並べる。main は最後（SDL_main を持つ）。
        return new String[] {
            "SDL2",
            "SDL2_image",
            "SDL2_ttf",
            "SDL2_mixer",
            "main"
        };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // ローグライクは「考えている時間」が長い。放っておくと画面が消える。
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    }
}
