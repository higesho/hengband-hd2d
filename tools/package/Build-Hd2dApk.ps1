<#
.SYNOPSIS
    ボクセル HD2D の配布物を作る（Android・arm64-v8a）。

.DESCRIPTION
    素材束を作り直してから APK を組み、`Dist` へ**固定の名前**で置く
    （`HengbandHD-android.apk`。決めたこと 2026-09-02。2026-10-06 に `HengbandHd2d-android.apk` から改めた——「HD2D」は登録商標）。

    **素材束（`android/build-assets-hd2d/`）は追跡外**なので、`.vox` や `lib/` を直した日は
    必ず作り直すこと——古いままだと「起動はするが中身が前の日のもの」という APK ができる。
    ここが既定で作り直す側に倒してあるのはそのためである（`-SkipAssets` で飛ばせる）。

    **`preview` を組む。**`release` は署名しないので、そのままでは端末に入らない
    （Google Play へ出すときに遊ぶ人が正式鍵で署名する）。

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
    if ($LASTEXITCODE -ne 0) { throw '原作ソースの準備に失敗しました（Angband・Rogue 3.6/5.4・RogueClone2 の差分入りソースもここで再構成されます）' }
}


if (-not $SkipAssets) {
    Write-Output '素材束を作り直しています（数分）…'
    if ($WithoutCores) {
        & python tools/build_android_hd2d_assets.py --without-rogue-licenses
    } else {
        & python tools/build_android_hd2d_assets.py
    }
    if ($LASTEXITCODE -ne 0) { throw '素材束の組み立てに失敗しました' }
}

# --- CP932 の変換ツリーを先に更新する（2026-08-29 に足した）-------------------
#
#  旧 C の 4 コア（幻想蛮怒・Sil-Q・Frox・TOband）は `android/build-src-sjis/` の変換ツリーを組む。
#  CMake は**組む最中**にも更新する（`hb_transcode_sjis`）が、**ninja は走り始めの
#  時刻で staleness を決めている**ので、その回では新しい中身が使われない。
#
#  2026-08-29 にそれを踏んだ——`gensoband/src/tables.c` を直した日に組んだ APK の
#  `libgensocore.so` に、その日足した字が 1 つも入っていなかった（2 度目で入った）。
#  **gradle を呼ぶ前に更新しておけば 1 度で済む。**同じ中身のファイルは触らないので、
#  変えていない日に走らせても組み直しは起きない。
Write-Output 'CP932 の変換ツリーを更新しています…'
$sjisTrees = @(
    @{ From = 'gensoband/src';     To = 'gensoband/src';     Enc = 'cp932'; Ext = '.c,.h,.inl' },
    @{ From = 'gensoband/adapter'; To = 'gensoband/adapter'; Enc = 'utf8';  Ext = '.c,.cpp,.h,.inl,.inc' },
    @{ From = 'toband/src';        To = 'toband/src';        Enc = 'cp932'; Ext = '.c,.h,.inl' },
    @{ From = 'toband/adapter';    To = 'toband/adapter';    Enc = 'utf8';  Ext = '.c,.cpp,.h,.inl,.inc' },
    @{ From = 'silq/adapter';      To = 'silq/adapter';      Enc = 'utf8';  Ext = '.c,.cpp,.h,.inl,.inc' },
    @{ From = 'frox/adapter';      To = 'frox/adapter';      Enc = 'utf8';  Ext = '.c,.cpp,.h,.inl,.inc' }
)
foreach ($t in $(if ($WithoutCores) { @() } else { $sjisTrees })) {
    & python tools/transcode_cp932_src.py --from $t.Enc --ext $t.Ext --quiet `
        $(if ($t.From -like '*/src') { Join-Path $coreSources $t.From } else { $t.From }) (Join-Path $coreSources ('android-sjis/' + $t.To))
    if ($LASTEXITCODE -ne 0) { throw ('変換ツリーを作れませんでした: ' + $t.From) }
}

$env:JAVA_HOME = 'C:\Android\jdk17'
$env:ANDROID_HOME = 'C:\Android\sdk'
$env:Path = 'C:\Android\gradle-8.9\bin;C:\Android\jdk17\bin;' + $env:Path

# externalNativeBuild は `.so` を更新しても、AGP が `merge/strip/package` を
# UP-TO-DATE と誤判定することがある。2026-09-19 の x86_64 検証で、
# `libmain.so` / `libfroxcore.so` は 11:13 に再リンクされたのに、strip 後の
# `.so` と APK が 09:38 のまま残る実例を確認した。
#
# 配布ビルドでは、ソースではなく **preview の生成中間物だけ**を捨てて、
# native merge -> strip -> package を必ず現在の externalNativeBuild 出力から作る。
$generatedPreviewPaths = @(
    (Join-Path $root 'android/hd2d/build/intermediates/merged_native_libs/preview'),
    (Join-Path $root 'android/hd2d/build/intermediates/stripped_native_libs/preview'),
    (Join-Path $root 'android/hd2d/build/outputs/apk/preview')
)
foreach ($generatedPath in $generatedPreviewPaths) {
    $resolvedParent = [System.IO.Path]::GetFullPath((Split-Path -Parent $generatedPath))
    $androidBuildRoot = [System.IO.Path]::GetFullPath((Join-Path $root 'android/hd2d/build'))
    if (-not $resolvedParent.StartsWith($androidBuildRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "生成物削除先が android/hd2d/build の外です: $generatedPath"
    }
    if (Test-Path -LiteralPath $generatedPath) {
        Remove-Item -LiteralPath $generatedPath -Recurse -Force
    }
}

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
    $coreLibraries = @('libhengcore.so','libtangcore.so','libgensocore.so','libsilcore.so','libfroxcore.so','librogue2core.so','librogue36core.so','librogue54core.so','libangbandcore.so','libumoriacore.so','libtobandcore.so')
    # libopenal.so は画面側の音。
    # libmain.so が DT_NEEDED で引くので、入れ忘れると起動時に落ちる。
    $libraries = @('libmain.so', 'libopenal.so')
    # Core Import compiler binaries are currently distributed for arm64 only.
    # The x86_64 emulator build intentionally has no matching prebuilt toolchain;
    # require/check only the ABI-specific prebuilts that actually exist.
    $prebuiltCompilerDir = Join-Path $root ("android/core-import-jni/" + $Abi)
    if (Test-Path -LiteralPath $prebuiltCompilerDir) {
        foreach ($compilerLibrary in @('libhbclang.so','libhblld.so','libhblink.so')) {
            if (-not (Test-Path -LiteralPath (Join-Path $prebuiltCompilerDir $compilerLibrary))) {
                throw "Core Import の $compilerLibrary がありません（$Abi）"
            }
            $libraries += $compilerLibrary
        }
    }
    if (-not $WithoutCores) { $libraries += $coreLibraries }
    elseif ($zip.Entries | Where-Object { $_.Name -in $coreLibraries }) { throw 'UI-only APK unexpectedly contains a game core' }
    $cmakeLibraries = @('libmain.so', 'libopenal.so')
    if (-not $WithoutCores) { $cmakeLibraries += $coreLibraries }
    foreach ($library in $libraries) {
        $stripped = Get-ChildItem "android/hd2d/build/intermediates/stripped_native_libs/preview/*/out/lib/$Abi/$library" -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if (-not $stripped) { throw "剥がした後の $library が見つかりません（$Abi）" }
        if ($library -in $cmakeLibraries) {
            $native = Get-ChildItem "android/hd2d/build/intermediates/cxx/*/*/obj/$Abi/$library" -ErrorAction SilentlyContinue |
                Sort-Object LastWriteTime -Descending | Select-Object -First 1
            if (-not $native) { throw "いま組んだ $library が見つかりません（$Abi）" }
            if ($stripped.LastWriteTimeUtc -lt $native.LastWriteTimeUtc) {
                throw "strip 後の $library が native build より古いままです（AGP の stale output）"
            }
        }
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
# **名前は固定**。版と ABI は下の刷り出しに残す。
$dst = Join-Path $OutDir 'HengbandHD-android.apk'
Copy-Item $apk $dst -Force
$size = [math]::Round((Get-Item $dst).Length / 1MB, 1)
Write-Output "できました: $dst（$size MB）"
