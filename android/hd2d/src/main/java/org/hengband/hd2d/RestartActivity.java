package org.hengband.hd2d;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;

/**
 * アプリのプロセスを起こし直すための、画面を持たない活動（2026-10-07。 の AH-09）。
 *
 * <p>ゲームコアの .so は開いたままで大域の状態が残るので、同じプロセスで同じコアを 2 度目に起こすと
 * 握手の前に止まる。言語を替えて同じコアを起こし直すとき（ネイティブの kRunRelaunchCore）は、
 * プロセスごと起こし直して「1 プロセスでコアは 1 回だけ」の前提を守る。
 *
 * <p>この活動は**別のプロセス**（AndroidManifest の android:process=":restart"）で動く。元のプロセスを
 * 終わらせてから本体の活動を開き直し、自分のプロセスも終わる。終わりかけのプロセスから自分を開き直すと
 * 裏からの起動の制限に掛かることがあるので、前に出ているこの活動から開く。起こし直しの中身
 * （どのコアを、続きから始めるか）は、ネイティブが基準ディレクトリのファイルに書いて次のプロセスへ渡す。
 */
public class RestartActivity extends Activity {

    /** 終わらせる元のプロセスの番号。 */
    static final String EXTRA_MAIN_PID = "org.hengband.hd2d.MAIN_PID";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        final int mainPid = getIntent().getIntExtra(EXTRA_MAIN_PID, -1);
        if (mainPid > 0 && mainPid != android.os.Process.myPid()) {
            android.os.Process.killProcess(mainPid);
        }
        final Intent intent = new Intent(this, HengbandHd2dActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
        startActivity(intent);
        finish();
        Runtime.getRuntime().exit(0);
    }
}
