<#
.SYNOPSIS
    Android ビルドに要る SDL2 群を third_party/android へ展開する。

.DESCRIPTION
    Android 版は SDL2・SDL2_image・SDL2_ttf・SDL2_mixer を**ソースから**同梱ビルドする
    （android/app/src/main/cpp/CMakeLists.txt が add_subdirectory する）。
    ここで落とすのは libsdl.org の公式リリース tarball だけで、改変はしない。

    版は Windows 版（vcpkg）と**揃えてある**。とくに SDL2_ttf は 2.22 と 2.24 で
    `TTF_Font` の前方宣言が `struct _TTF_Font` / `struct TTF_Font` と食い違い、
    `ui/assets/font_face.h` の前方宣言と衝突する。版を合わせるのが唯一の穏当な解。

.PARAMETER Force
    既に展開済みでも取り直す。

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\fetch_android_deps.ps1
#>
param([switch]$Force)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$repoRoot = Split-Path -Parent $PSScriptRoot
$dest     = Join-Path $repoRoot 'third_party\android'
$cache    = Join-Path $dest '_downloads'
New-Item -ItemType Directory -Force -Path $dest, $cache | Out-Null

$deps = @(
    @{ name = 'SDL2';       version = '2.30.11'; dir = 'SDL2-2.30.11';
       url  = 'https://github.com/libsdl-org/SDL/releases/download/release-2.30.11/SDL2-2.30.11.tar.gz' },
    @{ name = 'SDL2_image'; version = '2.8.5';   dir = 'SDL2_image-2.8.5';
       url  = 'https://github.com/libsdl-org/SDL_image/releases/download/release-2.8.5/SDL2_image-2.8.5.tar.gz' },
    @{ name = 'SDL2_ttf';   version = '2.24.0';  dir = 'SDL2_ttf-2.24.0';
       url  = 'https://github.com/libsdl-org/SDL_ttf/releases/download/release-2.24.0/SDL2_ttf-2.24.0.tar.gz' },
    @{ name = 'SDL2_mixer'; version = '2.8.0';   dir = 'SDL2_mixer-2.8.0';
       url  = 'https://github.com/libsdl-org/SDL_mixer/releases/download/release-2.8.0/SDL2_mixer-2.8.0.tar.gz' }
)

function Say($m) { Write-Host ("[fetch-android-deps] {0}" -f $m) }

foreach ($dep in $deps) {
    $target = Join-Path $dest $dep.name
    $stamp  = Join-Path $target '.version'

    if (-not $Force -and (Test-Path $stamp) -and ((Get-Content $stamp -Raw).Trim() -eq $dep.version)) {
        Say ("{0} {1} は展開済み" -f $dep.name, $dep.version)
        continue
    }

    $tgz = Join-Path $cache ("{0}-{1}.tar.gz" -f $dep.name, $dep.version)
    if (-not (Test-Path $tgz)) {
        Say ("取得: {0}" -f $dep.url)
        & curl.exe -L --fail --retry 3 --retry-delay 5 -o $tgz $dep.url
        if ($LASTEXITCODE -ne 0) { throw ("ダウンロードに失敗: {0}" -f $dep.url) }
    }

    Say ("展開: {0} {1}" -f $dep.name, $dep.version)
    $stage = Join-Path $dest ('_stage_' + $dep.name)
    if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $stage | Out-Null

    # Xcode の framework と android-project-ant はシンボリックリンクを含み、
    # Windows の tar では作れない。Android ビルドでは使わないので除外する。
    # （`--exclude` を行継続で並べると PowerShell が `--` を単項演算子と読むため、
    #   引数は配列にしてから渡す。）
    $tarArgs = @(
        '-xzf', $tgz, '-C', $stage,
        '--exclude=*/android-project-ant', '--exclude=*/android-project-ant/*',
        '--exclude=*/Xcode', '--exclude=*/Xcode/*',
        '--exclude=*/Xcode-iOS', '--exclude=*/Xcode-iOS/*'
    )
    & tar.exe @tarArgs
    # tar は除外したリンクについて警告を出すが、本体の展開は成功している。
    # ここでは「目的のディレクトリができたか」だけを合否にする。
    $extracted = Join-Path $stage $dep.dir
    if (-not (Test-Path (Join-Path $extracted 'CMakeLists.txt'))) {
        throw ("展開に失敗: {0}（{1} が無い）" -f $dep.name, $extracted)
    }

    if (Test-Path $target) { Remove-Item $target -Recurse -Force }
    Move-Item $extracted $target
    Remove-Item $stage -Recurse -Force
    Set-Content -Path $stamp -Value $dep.version -Encoding ascii
    Say ("完了: {0}" -f $target)
}

Say '全て揃いました。'
