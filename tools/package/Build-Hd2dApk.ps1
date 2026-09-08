<#
.SYNOPSIS
    ボクセル HD2D の配布物を作る（Android・arm64-v8a）。

.DESCRIPTION
    素材束を作り直してから APK を組み、`Dist` へ**固定の名前**で置く
    （`HengbandHd2d-android.apk`。実装方針 2026-09-02）。

    **素材束（`android/build-assets-hd2d/`）は追跡外**なので、`.vox` や `lib/` を直した日は
    必ず作り直すこと——古いままだと「起動はするが中身が前の日のもの」という APK ができる。
    ここが既定で作り直す側に倒してあるのはそのためである（`-SkipAssets` で飛ばせる）。

    **`preview` を組む。**`release` は署名しないので、そのままでは端末に入らない
    （Google Play へ出すときに利用者が正式鍵で署名する）。

.PARAMETER Version
    zip と APK の名前に付ける版（既定は今日の日付）。

.PARAMETER Abi
    `arm64-v8a`（実機・既定）／`x86_64`（エミュレータ）。

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools/package/Build-Hd2dApk.ps1 -Version 2026-08-23
#>
Param(
    [string]$Version = (Get-Date -Format 'yyyy-MM-dd'),
    [string]$Abi = 'arm64-v8a',
    [switch]$SkipAssets,
    [switch]$WithoutCores,
    [string]$OutDir = 'Dist'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location $root
$coreSources = if ($env:HENGBAND_CORE_SOURCE_ROOT) { $env:HENGBAND_CORE_SOURCE_ROOT } else { Join-Path (Split-Path $root -Parent) 'roguelike-cores/build-sources' }
if (-not $WithoutCores) {
    & python tools/core_sources/prepare.py --output $coreSources
    if ($LASTEXITCODE -ne 0) { throw '原作ソースの準備に失敗しました' }
}


if (-not $SkipAssets) {
    Write-Output '素材束を作り直しています（数分）…'
    & python tools/build_android_hd2d_assets.py
    if ($LASTEXITCODE -ne 0) { throw '素材束の組み立てに失敗しました' }
}

# --- CP932 の変換ツリーを先に更新する（2026-08-29 に足した）-------------------
#
#  旧 C の 2 コア（幻想蛮怒・Sil-Q）は `android/build-src-sjis/` の変換ツリーを組む。
#  CMake は**組む最中**にも更新する（`hb_transcode_sjis`）が、**ninja は走り始めの
#  時刻で staleness を決めている**ので、その回では新しい中身が使われない。
#
#  2026-08-29 にそれを踏んだ——`gensoband/src/tables.c` を直した日に組んだ APK の
#  `libgensocore.so` に、その日足した字が 1 つも入っていなかった（2 度目で入った）。
#  **gradle を呼ぶ前に更新しておけば 1 度で済む。**同じ中身のファイルは触らないので、
#  変えていない日に走らせても組み直しは起きない。
Write-Output 'CP932 の変換ツリーを更新しています…'
$sjisTrees = @(
    @{ From = 'gensoband/src';     To = 'gensoband/src';     Enc = 'cp932'; Ext = '.c,.h' },
    @{ From = 'gensoband/adapter'; To = 'gensoband/adapter'; Enc = 'utf8';  Ext = '.c,.cpp,.h' },
    @{ From = 'silq/adapter';      To = 'silq/adapter';      Enc = 'utf8';  Ext = '.c,.cpp,.h' }
)
foreach ($t in $(if ($WithoutCores) { @() } else { $sjisTrees })) {
    & python tools/transcode_cp932_src.py --from $t.Enc --ext $t.Ext --quiet `
        $(if ($t.From -like '*/src') { Join-Path $coreSources $t.From } else { $t.From }) (Join-Path $coreSources ('android-sjis/' + $t.To))
    if ($LASTEXITCODE -ne 0) { throw ('変換ツリーを作れませんでした: ' + $t.From) }
}

$env:JAVA_HOME = 'C:\Android\jdk17'
$env:ANDROID_HOME = 'C:\Android\sdk'
$env:Path = 'C:\Android\gradle-8.9\bin;C:\Android\jdk17\bin;' + $env:Path

Push-Location android
try {
    & 'C:\Android\gradle-8.9\bin\gradle.bat' ":hd2d:assemblePreview" "-PhengbandAbi=$Abi" "-PhengbandBuildCores=$(if ($WithoutCores) { 'false' } else { 'true' })" --console=plain
    if ($LASTEXITCODE -ne 0) { throw 'gradle が失敗しました' }
} finally {
    Pop-Location
}

$apk = 'android/hd2d/build/outputs/apk/preview/hd2d-preview.apk'
if (-not (Test-Path $apk)) { throw "APK ができていません: $apk" }
& python tools/core_import/audit_apk.py $apk
if ($LASTEXITCODE -ne 0) { throw 'APK のアセット・SDK 検査に失敗しました' }
<#
  **BUILD SUCCESSFUL は組み直した証拠にならない**（2026-08-22 に踏んだ。古い `.so` を
  詰めた APK ができた）。ただし**見分け方を 2 度外した**ので、経緯ごと書いておく。

  1. 時刻で見る → 誤検知する。gradle が `.so` を貼り直しても、中身が同じなら packaging は
     動かず、APK のほうが古いままになる（それは正しい状態である）
  2. 組んだ `.so` と APK の中身を比べる → **必ず食い違う**。APK に入るのは
     `stripPreviewDebugSymbols` が symbol を落とした版で、38MB が 2.5MB になっている

  そこで**剥がした後の `.so`**（`stripped_native_libs/`）と APK の中身を突き合わせる。
#>
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::OpenRead((Resolve-Path $apk))
try {
    $coreLibraries = @('libhengcore.so','libtangcore.so','libgensocore.so','libsilcore.so','libfroxcore.so')
    $libraries = @('libmain.so','libhbclang.so','libhblld.so','libhblink.so')
    if (-not $WithoutCores) { $libraries += $coreLibraries }
    elseif ($zip.Entries | Where-Object { $_.Name -in $coreLibraries }) { throw 'UI-only APK unexpectedly contains a game core' }
    foreach ($library in $libraries) {
        $stripped = Get-ChildItem "android/hd2d/build/intermediates/stripped_native_libs/preview/*/out/lib/$Abi/$library" -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if (-not $stripped) { throw "剥がした後の $library が見つかりません（$Abi）" }
        $entry = $zip.Entries | Where-Object { $_.FullName -eq "lib/$Abi/$library" }
        if (-not $entry) { throw "APK に lib/$Abi/$library がありません" }
        $stream = $entry.Open()
        try { $inApk = (Get-FileHash -InputStream $stream -Algorithm SHA256).Hash } finally { $stream.Dispose() }
        $onDisk = (Get-FileHash $stripped.FullName -Algorithm SHA256).Hash
        if ($inApk -ne $onDisk) { throw "APK の $library が、いま組んだものと違います" }
        Write-Output "$library のハッシュが一致（SHA-256 $($inApk.Substring(0, 12))…）"
    }
} finally {
    $zip.Dispose()
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
# **名前は固定**（実装方針 2026-09-02）。版と ABI は下の刷り出しに残す。
$dst = Join-Path $OutDir 'HengbandHd2d-android.apk'
Copy-Item $apk $dst -Force
$size = [math]::Round((Get-Item $dst).Length / 1MB, 1)
Write-Output "できました: $dst（$size MB）"
