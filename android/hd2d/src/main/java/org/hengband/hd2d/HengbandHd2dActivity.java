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

    /**
     * プロセスを起こし直す（ネイティブから呼ぶ。2026-10-07。AH-09）。
     * 別のプロセスの {@link RestartActivity} を開き、そちらがこのプロセスを終わらせて本体を開き直す。
     */
    public void restartProcess() {
        runOnUiThread(() -> {
            final android.content.Intent intent = new android.content.Intent(this, RestartActivity.class);
            intent.putExtra(RestartActivity.EXTRA_MAIN_PID, android.os.Process.myPid());
            startActivity(intent);
        });
    }

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

    /**
     * ゲームが終わって活動を閉じるときは、プロセスごと終わらせる（2026-10-07。AH-09）。
     *
     * <p>SDL はゲーム（main）が終わると活動を閉じるだけで、プロセスは残す。Android は残ったプロセスを
     * 次の起動で使い回し、同じプロセスで main をもう一度呼ぶ。ゲームコアの .so は開いたままで
     * 大域の状態が前回のまま残っているので、2 度目は握手の前に止まった（気づいたこと
     * 「Frox をセーブして終了したあと、再開すると hello_ack を読めませんでしたで起動できない」）。
     * 1 プロセスでコアは 1 回だけ、という前提へ戻す。
     *
     * <p>super.onDestroy() はゲームのスレッドの終わりを待ってから返る（SDLActivity）ので、
     * ここではセーブも済んでいる。向きなどの変化で作り直すとき（閉じるのではない）は終わらせない。
     */
    @Override
    protected void onDestroy() {
        final boolean finishing = isFinishing();
        super.onDestroy();
        if (finishing) {
            android.os.Process.killProcess(android.os.Process.myPid());
        }
    }
}
