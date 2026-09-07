<#
.SYNOPSIS
    ボクセル HD2D の配布物を作る（Windows・x64）。

.DESCRIPTION
    画面（HengbandHd2d.exe）と 5 本のコア、要る DLL、素材とデータを 1 つの箱へ入れて zip にする。
    案内は日本語（はじめに.txt）と英語（README.txt）の 2 つを入れる。

    **上流の `Build-Windows-Release-Package.ps1` とは別物である。**あちらは 2D の
    `Hengband.exe` を配る道具で、こちらはボクセル HD2Dを配る。

    DLL の一覧は当てずっぽうではなく、`dumpbin /dependents` の推移閉包で確かめたものである
    （コアは第三者の DLL を 1 つも要らない。要るのは画面だけ）。

.PARAMETER Version
    箱と zip の名前に付ける版（既定は今日の日付）。

.PARAMETER Rebuild
    組み直してから詰める。既定は「いま在る exe をそのまま詰める」。

.PARAMETER OutDir
    出力先（既定 `Dist`）。**zip の名前は版によらず固定**（2026-09-02 に決めた
    「今回から配布用ファイルは Dist フォルダに以下の名前で出力するように。
    今後ファイル名は固定で」）——置き場を指す先が動かないほうが配りやすいため。
    版は zip の**中の箱の名前**に残す。

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools/package/Build-Hd2dPackage.ps1 -Version 2026-08-23
#>
Param(
    [string]$Version = (Get-Date -Format 'yyyy-MM-dd'),
    [switch]$Rebuild,
    [string]$OutDir = 'Dist'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location $root

if ($Rebuild) {
    $msbuild = 'C:' + [char]92 + 'Program Files' + [char]92 + 'Microsoft Visual Studio' + [char]92 + '18' + [char]92 + 'Community' + [char]92 + 'MSBuild' + [char]92 + 'Current' + [char]92 + 'Bin' + [char]92 + 'MSBuild.exe'
    & $msbuild .\VisualStudio\Hengband.sln /t:Build /p:Configuration=Release /p:Platform=x64 /m /v:minimal
    if ($LASTEXITCODE -ne 0) { throw '組み立てに失敗しました' }
}

# --- 詰めるもの ---------------------------------------------------------------
# 画面（x64）と 5 本のコア（Win32）。**コアは単体では動かない**（画面が子として起こす）。
# 画面。箱の直下に置く（遊ぶときに起動するのはこれ）。
$executables = @(
    'HengbandHd2d.exe'
)

# コアは**1 階層深い `cores\` へ入れる**（2026-09-06 に決めた「間違えて各コアを
# 実行してしまう」）。コアは単体では遊べない——画面が子として起こし、パイプで話す。
# 画面は `cores\` を先に見て、無ければ自分の隣を見る（`hd2d_app.cpp` の
# `resolve_core_path`）ので、開発のツリーは横並びのままでよい。
$coreExecutables = @(
    'HengbandCore.exe',   # 変愚蛮怒
    'TangbandCore.exe',   # 短愚蛮怒
    'GensobandCore.exe',  # 幻想蛮怒
    'SilCore.exe',        # Sil-Q
    'FroxCore.exe' # FroxComposband
)

# `dumpbin /dependents` の推移閉包（2026-08-23 実測）。**画面だけが要る。**
$libraries = @(
    'SDL2.dll', 'SDL2_image.dll', 'SDL2_ttf.dll',
    'freetype.dll', 'libpng16.dll', 'z.dll', 'bz2.dll',
    'brotlidec.dll', 'brotlicommon.dll',
    'openxr_loader.dll', 'OpenAL32.dll', 'fmt.dll', 'jsoncpp.dll'
)

# 素材とデータ。`lib/save` と `lib/user` は**中身を入れない**（遊んだ跡なので）。
# **`user` は 5 コアぶん全部を空にする**（2026-08-25）。以前は変愚の `lib/user` しか
# 掃除しておらず、`gensoband/lib/user` の `playrecord-*.txt` や `autodump_*.txt`
# （遊んだ人物の名前が入る）がそのまま配布物へ入っていた。Android 側の同じ穴は
# `tools/build_android_hd2d_assets.py` の `LIB_SKIP_DIRS` で塞いである。
$dataDirs = @(
    'assets', 'lib', 'tangband/lib', 'gensoband/lib', 'silq/lib', 'frox/lib',
    # **訳の層は `lib` の外にある**（2026-08-25 に足した）。ここまでが 1 組で、
    # 入れ忘れると**遊べはするが全部英語（Sil-Q・Frox）／日本語（幻想）に戻る**
    # ——コアは訳が無ければ原文へ落ちる作りなので、**落ちも警告も出ない**。
    # `lib-ja` / `lib-en` は生成物なので、無ければ下の $generated が作り方を言う。
    'silq/lib-ja', 'silq/lib-en', 'silq/lang/ja',
    'frox/lib-ja', 'frox/lang/ja',
    'gensoband/lib-en', 'gensoband/lang/en'
)

#: 追跡していない生成物の木と、その作り方（`tools/build_android_hd2d_assets.py` と同じ表）。
$generated = @{
    'silq/lib-ja'     = 'python tools/silq/build_ja_edit.py && python tools/silq/build_ja_help.py --all'
    'silq/lib-en'     = 'python tools/silq/build_ja_help.py --all'
    'frox/lib-ja'     = 'python tools/frox/fc_build_ja_edit.py'
    'gensoband/lib-en' = 'python tools/gensoband/build_en_edit.py && python tools/gensoband/build_en_file.py'
}

$name = "HengbandHd2d-$Version-win-x64"
$stage = Join-Path $OutDir $name
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null

foreach ($f in ($executables + $libraries)) {
    if (-not (Test-Path $f)) { throw "見つかりません: $f（先に組んでください）" }
    Copy-Item $f $stage
}

# コアは cores\ の下へ。
$coreStage = Join-Path $stage 'cores'
New-Item -ItemType Directory -Force $coreStage | Out-Null
foreach ($f in $coreExecutables) {
    if (-not (Test-Path $f)) { throw "見つかりません: $f（先に組んでください）" }
    Copy-Item $f $coreStage
}

foreach ($d in $dataDirs) {
    if (-not (Test-Path $d)) {
        $hint = $generated[$d]
        $tail = if ($hint) { "`n  先にこれを走らせること: $hint" } else { '' }
        throw "$d がありません$tail"
    }
    $dst = Join-Path $stage $d
    New-Item -ItemType Directory -Force (Split-Path -Parent $dst) | Out-Null
    Copy-Item -Recurse $d $dst
}

# 遊んだ跡は配らない。**印の 1 枚は残す**——`delete.me`（変愚・短愚・幻想）と
# `.keepalive`（Sil-Q・Frox）・`.gitkeep`（幻想の `save`）で、これを消すと空になり、
# `Compress-Archive` が空のものを詰めないので**箱から消える**。`frox` の `user` は
# コアが**在ることを求める**（`fc_validate_dir(ANGBAND_DIR_USER, 1)`）ので、
# 消えると配布物が起動しない。
$playedDirs = @('lib', 'tangband/lib', 'gensoband/lib', 'silq/lib', 'frox/lib')
foreach ($sub in ($playedDirs | ForEach-Object { "$_/save"; "$_/user" })) {
    $p = Join-Path $stage $sub
    if (Test-Path $p) {
        Get-ChildItem $p -Force | Where-Object { $_.Name -notin @('delete.me', '.keepalive', '.gitkeep') } | Remove-Item -Recurse -Force
    }
}

# **Frox の作り直しの印は箱へ入れない**（`frox/lib-ja/edit/help_upd.txt`）。
# コアは起動時にこの印の版を見て、実行体と一致していたら `generate_spoilers()` を
# **飛ばす**（`frox/src/init2.c` の `init_help_files()`）。種族・職業・性格など
# 13 本の本はここで作られるので、**中身が英語のまま印だけ合っていると、
# 遊ぶ人が何度日本語で起動しても英語のままになり、遊ぶ側では直せない**。
# `fc_build_ja_help.py` は訳ファイルの無いこの 13 本を英語で写す作りなので、
# 走らせた後にゲームを起動し直さないとその状態になる。
# 印を入れなければ**初回の日本語起動で必ず作り直される**ので、詰める順番に
# 依らなくなる。英語側（`frox/lib/edit/help_upd.txt`）はそのままでよい
# ——あちらは英語の本を英語で作り直すだけである。
$froxMark = Join-Path $stage 'frox/lib-ja/edit/help_upd.txt'
if (Test-Path $froxMark) { Remove-Item -Force $froxMark }

foreach ($f in @('readme.md', 'THIRD-PARTY-NOTICES.txt', 'readme_angband')) {
    if (Test-Path $f) { Copy-Item $f $stage }
}

# --- 遊び方の紙 ---------------------------------------------------------------
$notes = @"
ボクセル HD2D（$Version・Windows x64）

はじめかた
  HengbandHd2d.exe を起動する。コアが 2 つ以上在るときは選択画面が出る。
  **コアの exe は cores\ の中に入れてある。直接起動しても動かない**——
  画面が子として起こし、パイプで話す作りである。

要るもの
  * 64 ビットの Windows と、OpenGL 4.6 が動く映像機器
  * Microsoft Visual C++ 再頒布可能パッケージ（x64）
    入っていないと起動前に落ちる（窓も出ない）。

入っているもの
  画面 1 つとコア 5 本（変愚蛮怒・短愚蛮怒・幻想蛮怒・Sil-Q・FroxComposband）。
  セーブは各コアの lib/save へ書かれる。

設定
  hd2d.cfg が実行ファイルの隣にできる。画面の中の機能メニューからも同じものを回せる。

English
  See README.txt.
"@
$notesPath = Join-Path $stage 'はじめに.txt'
[System.IO.File]::WriteAllText($notesPath, $notes, (New-Object System.Text.UTF8Encoding $true))

# 英語の案内（2026-09-06 に決めた）。**上の日本語と同じ 5 つの見出し**にしてある。
# 片方だけ直すと食い違うので、書き換えるときは必ず両方を直すこと。
$notesEn = @"
Voxel HD2D ($Version, Windows x64)

Getting started
  Run HengbandHd2d.exe. If more than one core is present, a chooser appears.
  **The core executables live in cores\. Running them directly does nothing**
  -- the screen starts one as a child process and talks to it over a pipe.

Requirements
  * 64-bit Windows and a graphics device that supports OpenGL 4.6
  * Microsoft Visual C++ Redistributable (x64)
    Without it the program dies before it opens a window.

What is inside
  One screen and five cores (Hengband, Tangband, Gensoband, Sil-Q, FroxComposband).
  Save files are written under each core's lib/save directory.

Settings
  hd2d.cfg is created next to the executable. The in-game feature menu changes
  the same settings.

Japanese
  See はじめに.txt.
"@
$notesEnPath = Join-Path $stage 'README.txt'
[System.IO.File]::WriteAllText($notesEnPath, $notesEn, (New-Object System.Text.UTF8Encoding $true))

# --- zip ----------------------------------------------------------------------
# **名前は固定**（2026-09-02 に決めた）。版は $name（zip の中の箱）に残る。
$zip = Join-Path $OutDir 'HengbandHd2d-win.zip'
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path $stage -DestinationPath $zip

$size = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Write-Output "できました: $zip（$size MB）"
