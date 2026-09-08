# HD2D の回帰検査

リポジトリ直下で、ビルド済みの Windows 版に対して実行する。
通常のゲームと検査を同じ作業フォルダーで同時に起動しないこと。

```powershell
python tools/hd2d_verify/test_runtime.py
python tools/hd2d_verify/golden.py --check
python tools/hd2d_verify/replay.py --check
python tools/hd2d_verify/playthrough.py --check
```

- `golden`: 検査モード 16 件の出力を比較する。
- `replay`: 保存した通信を再生し、3 場面の画像ハッシュと UI の送信内容を比較する。
- `playthrough`: 実コアの起動・読み込み・パッド入力を確認する。送受信は各方向の順番・件数・内容を比較する。両方向の交錯と OS の音声再生完了通知は実時間で変わるため除く。
- `test_runtime`: ダミーのセーブとプロセスを用い、準備失敗・復元・タイムアウト・無関係なプロセスの保護を検査する。

## 入力とコアの寿命の追加検査

実行中の UI へキーを送る場合は、`--test-input-file=` と `send_test_key.py` を使う。
Enter / Esc / F キー / Ctrl 併用を OS の入力注入に頼らず送れる。
起動前に `send_test_key.py --file input.jsonl --init` で入力ファイルを作り、UI に `--test-input-file=input.jsonl` を渡す。起動後は `send_test_key.py --file input.jsonl --key Enter` のように送る。Ctrl 併用は `--key x --ctrl`。返る応答は SDL キューへの登録を示し、ゲーム側の完了確認は別に行う。
`python tools/hd2d_verify/test_keyboard_live.py` で、コア選択から通常の保存終了まで確認できる。

`--ui-check` の追加モードを環境変数で選ぶ。SDL 入力の検査はコアを起動しない。
コア寿命の検査は、直下の既存 5 コアをそれぞれ 2 回起動し、起動失敗 20 回とハンドル数も確認する。
GUI 実行体の終了を確実に待つため、PowerShell ではパイプで出力を受ける。

```powershell
$env:HD2D_SDL_INPUT_CHECK='1'
try { .\HengbandHd2d.exe --ui-check 2>&1 | Out-Host } finally { Remove-Item Env:HD2D_SDL_INPUT_CHECK }
$env:HD2D_CORE_LINK_CHECK='1'
try { .\HengbandHd2d.exe --ui-check 2>&1 | Out-Host } finally { Remove-Item Env:HD2D_CORE_LINK_CHECK }
```

## セーブ・設定・プロセス

検査用セーブは `fixtures/DBG` と `fixtures/DBG.sdl2panels`。旧 `tools/diff_test/fixtures/` から移した検査素材で、同じバイト列を使う。
実コアの検査では `lib/save` を同じ親ディレクトリの `.hd2d-save-*/original` へ退避し、終了時に戻す。設定も復元する。
記録再生はコアを起動しないので、セーブの退避は行わない。

検査用プロセスと子孫は専用の Windows Job に所属する。タイムアウト等でも、その Job だけを終了する。
検査プロセスそのものの強制終了や電源断ではセーブの自動復元ができない。その場合は、ゲームが終了していることを確認して退避元を戻す。
復元エラーで残った退避元を削除しないこと。

既存の基準は意図した動作変更がある場合だけ更新する。環境差を理由に `--record` で上書きしない。

## Windows 配布物の検査

ZIP を新しいフォルダーへ展開し、次を実行する。5 コアの新規作成・ロード・保存と、
変愚の UI キー操作を確認する。検査用設定以外のデータは開発ツリーから補わない。

```powershell
python tools/hd2d_verify/package.py D:/test/HengbandHd2d-version-win-x64
```

セーブと設定は復元する。検査はゲーム内時間を進め、キャッシュを生成するため、検査用の展開先を使う。

## 原作 ZIP の UI インポート

```powershell
python tools/hd2d_verify/test_core_import.py D:/sources/silq-source.zip
python tools/hd2d_verify/test_core_import.py D:/sources/silq-source.zip --app-dir D:/test/HengbandHd2d-version-win-x64
```

Sil-Q の対応版 ZIP を指定する。コアがない配布物でも、ZIP 選択・ビルド・登録・起動を確認する。
保存先には日本語・空白・絵文字を含め、セーブと設定は復元する。検査結果は scratch_old/core-import-ui-* に保存する。
テスト入力には `--drop-file` も使用できる。


複数ZIPと選択解除を確認する例（2番目のZIPからSil-Qを生成する）:

```powershell
python tools/hd2d_verify/test_core_import.py D:/sources/hengband-source.zip --extra-zip D:/sources/silq-source.zip --check-clear
```

範囲再構成の共通検査は `tools/core_sources/test_reconstruction.py`。旧レシピの拒否、範囲外、原作・追加・出力のハッシュ、CP932、改行・BOMの不変条件を確認する。
