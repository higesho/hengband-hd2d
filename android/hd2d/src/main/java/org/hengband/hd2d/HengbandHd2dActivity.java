package org.hengband.hd2d;

import android.os.Bundle;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

/**
 * 変愚蛮怒 ボクセル HD2D Android 版のエントリ Activity。
 *
 * <p>ロジックはすべてネイティブ側（platform/android/hd2d_entry_android.cpp）にある。
 * ここは共有ライブラリの読み込み、画面の消灯抑止、原作 ZIP のファイル選択とコピーを担う。
 * 旧 SDL2 UI 版（org.hengband.android.HengbandActivity）と同じ形。
 */
public class HengbandHd2dActivity extends SDLActivity {

    private static final int CORE_SOURCE_ZIP = 4401;
    private static native void coreSourceZipSelected(String path, String displayName);

    public String getCoreCompilerDirectory() {
        return getApplicationInfo().nativeLibraryDir;
    }

    public void requestCoreSourceZip() {
        runOnUiThread(() -> {
            android.content.Intent intent = new android.content.Intent(android.content.Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(android.content.Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            intent.putExtra(android.content.Intent.EXTRA_MIME_TYPES, new String[]{"application/zip", "application/x-zip-compressed", "application/octet-stream"});
            try {
                startActivityForResult(intent, CORE_SOURCE_ZIP);
            } catch (android.content.ActivityNotFoundException error) {
                coreSourceZipSelected("!No ZIP document picker is available on this device", "");
            }
        });
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, android.content.Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != CORE_SOURCE_ZIP || resultCode != RESULT_OK || data == null || data.getData() == null) return;
        final android.net.Uri uri = data.getData();
        // ContentProvider の URI はネイティブ側へ直接渡さず、サイズを制限してアプリ領域へコピーする。
        new Thread(() -> {
            java.io.File target = null;
            try {
                target = java.io.File.createTempFile("core-source-", ".zip", getCacheDir());
                try (java.io.InputStream input = getContentResolver().openInputStream(uri);
                     java.io.OutputStream output = new java.io.FileOutputStream(target)) {
                    if (input == null) throw new java.io.IOException("Cannot open selected ZIP");
                    byte[] buffer = new byte[65536];
                    long total = 0;
                    for (int n; (n = input.read(buffer)) != -1;) {
                        total += n;
                        if (total > 512L * 1024 * 1024) throw new java.io.IOException("Source ZIP exceeds 512 MiB");
                        output.write(buffer, 0, n);
                    }
                }
                String displayName = "source.zip";
                try (android.database.Cursor cursor = getContentResolver().query(uri,
                        new String[]{android.provider.OpenableColumns.DISPLAY_NAME}, null, null, null)) {
                    if (cursor != null && cursor.moveToFirst() && !cursor.isNull(0)) displayName = cursor.getString(0);
                } catch (Exception ignored) { /* 名前を取得できなくても ZIP は取り込める。 */ }
                coreSourceZipSelected(target.getAbsolutePath(), displayName);
            } catch (Exception error) {
                if (target != null) target.delete();
                coreSourceZipSelected("!" + error.getMessage(), "");
            }
        }, "core-source-copy").start();
    }

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
