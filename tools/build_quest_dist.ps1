# Meta Quest ネイティブ版の配布物を組む。
#
# やること: :quest:assemblePreview を組み、APK と現地手順の README を
# dist\HengbandQuest\ へ置く。Windows 版は tools\package\Build-Hd2dPackage.ps1 が作る。
#
# 前提: アセット生成済み（android\build-assets-hd2d\assets.manifest がある）。
# 無ければ先に  python tools\build_android_hd2d_assets.py  を回すこと。

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$apkSrc = Join-Path $repo 'android\quest\build\outputs\apk\preview\quest-preview.apk'
$distDir = Join-Path $repo 'dist\HengbandQuest'
$apkName = 'hengband-quest-3.0.2.3.apk'   # versionName（android\quest\build.gradle）と揃える

# 1. ビルド（変更が無ければ gradle が up-to-date で即返す）
$env:JAVA_HOME = 'C:\Android\jdk17'
$env:ANDROID_HOME = 'C:\Android\sdk'
$env:Path = 'C:\Android\gradle-8.9\bin;C:\Android\jdk17\bin;' + $env:Path
Push-Location (Join-Path $repo 'android')
try {
    gradle :quest:assemblePreview --console=plain
    if ($LASTEXITCODE -ne 0) { throw "gradle が失敗しました（exit $LASTEXITCODE）" }
} finally {
    Pop-Location
}
if (-not (Test-Path $apkSrc)) { throw "APK がありません: $apkSrc" }

# 2. 置く
New-Item -ItemType Directory -Force $distDir | Out-Null
Copy-Item $apkSrc (Join-Path $distDir $apkName) -Force

# 3. 現地手順（Windows 版 dist の README と同じ思想: 空のフォルダを渡さない）
$readme = @"
変愚蛮怒 ボクセル HD2D — Meta Quest ネイティブ版（野良 APK）
============================================================

対象: Quest 2 / Quest Pro / Quest 3 / Quest 3S
前提: 機側で開発者モードが有効なこと（スマホの Meta Horizon アプリ →
      デバイス → ヘッドセットの設定 → 開発者モード。初回のみ）

1. インストール（USB で繋ぎ、機内の「USB デバッグを許可」に OK してから）
     adb install -r $apkName

2. 起動
     ライブラリ → 絞り込み「提供元不明のアプリ」 → 変愚蛮怒
     初回はゲームデータの展開（約 250MB）で 1 分ほど待つ。
     展開の後に**ゲームコアの選択**が出る（変愚蛮怒／短愚蛮怒）。
     スティックか十字で選んで A。次からは前回選んだコアにカーソルが載る。

3. 操作（Quest Touch。割り当ては 機能メニュー > 割り当て で変更可）
     左スティック = 移動 ／ A = 決定 ／ B = 取消 ／ 左☰ = 機能メニュー
     見え方の調整は 機能メニュー > VR（拡大率・視点高さ・字の大きさ・描画解像度ほか）。

4. 困ったとき
     VR を立てず 2D パネルで起動（切り分け用）:
       adb shell setprop debug.hengband.vr 0
     戻すとき:
       adb shell setprop debug.hengband.vr 1
     ログ（XR の接続・性能の実測）:
       adb logcat -d | findstr /C:"XR_" /C:"vr-perf" /C:"hengband"

基準はリポジトリの。
"@
Set-Content -Path (Join-Path $distDir 'README.txt') -Value $readme -Encoding UTF8

$apk = Get-Item (Join-Path $distDir $apkName)
Write-Output ("配布物: {0}  ({1:N1} MB / {2})" -f $apk.FullName, ($apk.Length / 1MB), $apk.LastWriteTime)
Write-Output ("        {0}" -f (Join-Path $distDir 'README.txt'))
