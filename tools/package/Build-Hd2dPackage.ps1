<#
.SYNOPSIS
    ボクセル HD2D の配布物を作る（Windows・x64）。

.DESCRIPTION
    画面（HengbandHd2d.exe）と 11 本のコア、要る DLL、素材とデータを 1 つの箱へ入れて zip にする。
    案内は日本語（はじめに.txt）と英語（README.txt）の 2 つを入れる。

    **上流の `Build-Windows-Release-Package.ps1` とは別物である。**あちらは 2D の
    `Hengband.exe` を配る道具で、こちらはボクセル HD2Dを配る。

    DLL の一覧は当てずっぽうではなく、`dumpbin /dependents` の推移閉包で確かめたものである
    （コアは第三者の DLL を 1 つも要らない。要るのは画面だけ）。

.PARAMETER Version
    箱と zip の名前に付ける版（既定は今日の日付）。

.PARAMETER Rebuild
    組み直してから詰める。既定は「dist-noeditor の配布用UIと、いま在るコアを詰める」。

.PARAMETER OutDir
    出力先（既定 `Dist`）。**zip の名前は版によらず固定**（決めたこと 2026-09-02
    「今回から配布用ファイルは Dist フォルダに以下の名前で出力するように。
    今後ファイル名は固定で」）——置き場を指す先が動かないほうが配りやすいため。
    版は zip の**中の箱の名前**に残す。

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools/package/Build-Hd2dPackage.ps1 -Version 2026-08-23
#>
Param(
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]*$')][string]$Version = (Get-Date -Format 'yyyy-MM-dd'),
    [switch]$Rebuild,
    [switch]$WithoutCores,
    [switch]$IncludeTiles,
    [switch]$IncludeCompiler,
    [string]$OutDir = 'Dist',
    [string]$UiBinaryDir = 'dist-noeditor',
    [string]$CoreBinaryDir = '.'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location $root
if ($WithoutCores -and -not $IncludeCompiler) { throw '-WithoutCores requires -IncludeCompiler' }
if ($IncludeTiles) { throw '-IncludeTiles is disabled: distribution packages must not contain tile images' }

if ($Rebuild -and $CoreBinaryDir -ne '.') { throw '-Rebuild uses the standard core output; specify -CoreBinaryDir only for an already built candidate' }

if ($Rebuild) {
    if (-not $WithoutCores) {
        & python tools/core_sources/prepare.py
        if ($LASTEXITCODE -ne 0) { throw "原作ソースの準備に失敗しました" }
    }
    $msbuild = 'C:' + [char]92 + 'Program Files' + [char]92 + 'Microsoft Visual Studio' + [char]92 + '18' + [char]92 + 'Community' + [char]92 + 'MSBuild' + [char]92 + 'Current' + [char]92 + 'Bin' + [char]92 + 'MSBuild.exe'
    $project = if ($WithoutCores) { 'VisualStudio/HengbandHd2d/HengbandHd2d.vcxproj' } else { 'VisualStudio/Hengband.sln' }
    # **差分の組み立て（/t:Build）にしない。**エディタ無しの中間ファイル（Release-noeditor-x64）は配布のときしか
    # 組まないので古いまま残りやすく、2026-10-04 の配布物は 10-03 の古い hd2d_main.obj を使い回して、
    # .cpp の間で AppOptions の形が食い違い、起動直後に落ちていた（2026-10-06 に判明）。
    # ソリューション全体ではなく、**詰めるプロジェクトだけ**を組み直す。ソリューションには上流そのままの
    # Hengband.exe（Hengband.vcxproj。配布物には入らない）も入っていて、3.0.2.5 の取り込みでファイル一覧が
    # 古くなったまま組めなくなっていた（2026-10-06）。配布に要らない物の失敗で配布を止めない。
    $targets = if ($WithoutCores) { 'Rebuild' } else { (@('HengbandHd2d', 'HengbandCore', 'TangbandCore', 'GensobandCore', 'TobandCore', 'SilCore', 'FroxCore') | ForEach-Object { "${_}:Rebuild" }) -join ';' }
    & $msbuild $project "/t:$targets" /p:Configuration=Release /p:Platform=x64 /p:HengbandHd2dEditor=0 /m:2 /v:minimal
    if ($LASTEXITCODE -ne 0) { throw '組み立てに失敗しました' }
    if (-not $WithoutCores) {
        & python tools/rogue2/build_windows.py
        if ($LASTEXITCODE -ne 0) { throw 'RogueClone2 の Windows コア組み立てに失敗しました' }
        & python tools/rogue_legacy/build_windows.py --core all
        if ($LASTEXITCODE -ne 0) { throw 'Rogue 3.x/5.x の Windows コア組み立てに失敗しました' }
        & python tools/angband/build_windows.py
        if ($LASTEXITCODE -ne 0) { throw 'Angband の Windows コア組み立てに失敗しました' }
        & python tools/umoria/build_windows.py
        if ($LASTEXITCODE -ne 0) { throw 'Umoria の Windows コア組み立てに失敗しました' }
    }
}

# 配布用UIはエディターを除外した dist-noeditor のビルドを使用する。
# 開発用UIの実行ファイルを配布物へコピーしない。

# --- 詰めるもの ---------------------------------------------------------------
# 画面（x64）と 11 本のコア（Win32）。**コアは単体では動かない**（画面が子として起こす）。
# 画面。箱の直下に置く（遊ぶときに起動するのはこれ）。
$executables = @(
    'HengbandHd2d.exe'
)

# コアは**1 階層深い `cores\` へ入れる**（決めたこと 2026-09-06「間違えて各コアを
# 実行してしまう」）。コアは単体では遊べない——画面が子として起こし、パイプで話す。
# 画面は `cores\` を先に見て、無ければ自分の隣を見る（`hd2d_app.cpp` の
# `resolve_core_path`）ので、開発のツリーは横並びのままでよい。
$coreExecutables = @(
    'HengbandCore.exe',   # 変愚蛮怒
    'TangbandCore.exe',   # 短愚蛮怒
    'GensobandCore.exe',  # 幻想蛮怒
    'TobandCore.exe',     # TOband
    'SilCore.exe',        # Sil-Q
    'FroxCore.exe',       # FroxComposband
    'Rogue2Core.exe',     # RogueClone2
    'Rogue36Core.exe',    # Rogue 3.6.4
    'Rogue54Core.exe',    # Rogue 5.4.4
    'AngbandCore.exe',    # Angband
    'UmoriaCore.exe'      # Umoria 5.7
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
    'assets/audio', 'assets/fonts', 'assets/fx', 'assets/lang', 'assets/ui', 'assets/voxel',
    'lib', 'tangband/lib', 'gensoband/lib', 'toband/lib', 'silq/lib', 'frox/lib',
    # **訳の層は `lib` の外にある**（2026-08-25 に足した）。ここまでが 1 組で、
    # 入れ忘れると**遊べはするが全部英語（Sil-Q・Frox）／日本語（幻想）に戻る**
    # ——コアは訳が無ければ原文へ落ちる作りなので、**落ちも警告も出ない**。
    # `lib-ja` / `lib-en` は生成物なので、無ければ下の $generated が作り方を言う。
    'silq/lib-ja', 'silq/lib-en', 'silq/lang/ja',
    'frox/lib-ja', 'frox/lang/ja',
    'gensoband/lib-en', 'gensoband/lang/en',
    'toband/lib-en', 'toband/lang/en',
    # 実行時に接続コードが読むタイルと対応表。原画・原作ソースは含めない。
    'gensoband/assets/slab',
    'toband/assets/slab',
    'silq/assets/slab',
    'frox/assets/slab'
)

if ($IncludeCompiler) { $dataDirs += 'core-import' }

# RogueClone2 は Angband 系の lib/ 木を持たず、実行時に 1 枚のメッセージ表を読む。
$dataFiles = @('rogue2/mesg')

#: 追跡していない生成物の木と、その作り方（`tools/build_android_hd2d_assets.py` と同じ表）。
$generated = @{
    'silq/lib-ja'     = 'python tools/silq/build_ja_edit.py && python tools/silq/build_ja_help.py --all'
    'silq/lib-en'     = 'python tools/silq/build_ja_help.py --all'
    'frox/lib-ja'     = 'python tools/frox/fc_build_ja_edit.py'
    'gensoband/lib-en' = 'python tools/gensoband/build_en_edit.py && python tools/gensoband/build_en_file.py'
    'toband/lib-en'   = 'python tools/toband/build_en_edit.py && python tools/toband/build_en_file.py && python tools/toband/build_en_help.py'
}

$name = "HengbandHD-$Version-win-x64"
$OutDir = if ([IO.Path]::IsPathRooted($OutDir)) { [IO.Path]::GetFullPath($OutDir) } else { [IO.Path]::GetFullPath((Join-Path $root $OutDir)) }
$stage = [IO.Path]::GetFullPath((Join-Path $OutDir $name))
if ((Split-Path -Parent $stage) -ne $OutDir) { throw 'Package stage must be a child of OutDir' }
function Assert-PackagePath([string]$Path) {
    $resolved = [IO.Path]::GetFullPath($Path)
    if (-not ($resolved -eq $stage -or $resolved.StartsWith($stage.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase))) {
        throw "Cleanup outside package stage: $resolved"
    }
}
if (Test-Path $stage) { Assert-PackagePath $stage; Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Force $stage | Out-Null

# Restored Rogue 3.6.4 / 5.4.4 are redistributed under their upstream BSD-style
# license texts.  Copy the exact upstream files rather than maintaining a second
# hand-edited copy in the UI repository.
if (-not $WithoutCores) {
    # 原作の置き場。worktree から組むときは HENGBAND_UPSTREAM_ROOT で指す（prepare.py と同じ決まり）。
    $rogueUpstream = if ($env:HENGBAND_UPSTREAM_ROOT) { $env:HENGBAND_UPSTREAM_ROOT } else { Join-Path (Split-Path $root -Parent) 'roguelike-cores\upstream' }
    $licenseDir = Join-Path $stage 'licenses'
    New-Item -ItemType Directory -Force $licenseDir | Out-Null
    foreach ($spec in @(
        @{ Core = 'rogue36'; Name = 'Rogue36-LICENSE.txt' },
        @{ Core = 'rogue54'; Name = 'Rogue54-LICENSE.txt' }
    )) {
        $src = Join-Path (Join-Path $rogueUpstream $spec.Core) 'LICENSE.TXT'
        if (-not (Test-Path -LiteralPath $src)) { throw "Rogue license is missing: $src" }
        Copy-Item -LiteralPath $src -Destination (Join-Path $licenseDir $spec.Name)
    }
    # Angband の GPL v2／Angband licence（THIRD-PARTY-NOTICES.txt が参照する）。
    # 原作を外部へ分離した Rogue とは違い、UI リポジトリに git-tracked（`angband/LICENSE.md`）。
    $angbandLicense = Join-Path $root 'angband\LICENSE.md'
    if (-not (Test-Path -LiteralPath $angbandLicense)) { throw "Angband license is missing: $angbandLicense" }
    Copy-Item -LiteralPath $angbandLicense -Destination (Join-Path $licenseDir 'Angband-LICENSE.md')
    # Umoria の GPL v3。原作の LICENSE を build_windows.py が umoria/data/ へ写している。
    $umoriaLicense = Join-Path $root 'umoria\data\LICENSE'
    if (-not (Test-Path -LiteralPath $umoriaLicense)) { throw "Umoria license is missing: $umoriaLicense（python tools/umoria/build_windows.py を先に）" }
    Copy-Item -LiteralPath $umoriaLicense -Destination (Join-Path $licenseDir 'Umoria-LICENSE.txt')
    # TOband（LICENSE §1.3）。原作の使用許諾（jlicense.txt。原作は EUC-JP なので UTF-8 に写す）・readme_angband・
    # クエストのマップの元になった NetHack の使用許諾。Linux の詰め（build_linux_package.py）と同じ 3 つ。
    $tobandUpstream = Join-Path $rogueUpstream 'toband'
    foreach ($spec in @(
        @{ Rel = 'readme_angband'; Name = 'Toband-readme_angband.txt'; Euc = $false },
        @{ Rel = 'lib\file\nethack-license.txt'; Name = 'Toband-nethack-license.txt'; Euc = $false },
        @{ Rel = 'lib\help\jlicense.txt'; Name = 'Toband-jlicense.txt'; Euc = $true }
    )) {
        $src = Join-Path $tobandUpstream $spec.Rel
        if (-not (Test-Path -LiteralPath $src)) { throw "TOband license is missing: $src" }
        $dst = Join-Path $licenseDir $spec.Name
        if ($spec.Euc) {
            $text = [IO.File]::ReadAllText($src, [Text.Encoding]::GetEncoding(51932))
            [IO.File]::WriteAllText($dst, $text, (New-Object Text.UTF8Encoding($false)))
        } else {
            Copy-Item -LiteralPath $src -Destination $dst
        }
    }
}

# 使用許諾の文書。実行時ライブラリ（.dll）は -WithoutCores でも入るので、GPL v2 の全文とライブラリの原文は常に入れる。
# RogueClone2 は原作の配布条件 (2)「すべてのドキュメントを付属させること」のため、原作の資料を licenses\RogueClone2 に添える
# （コアを入れるときだけ）。作り方は tools/package/license_docs.py（Linux・Android の配布物と共通）。
$licenseDocsArgs = @('tools/package/license_docs.py', '--dest', (Join-Path $stage 'licenses'), '--windows-runtime')
if ($WithoutCores) { $licenseDocsArgs += '--no-rogue2' } else { $licenseDocsArgs += @('--upstream', $rogueUpstream) }
& python @licenseDocsArgs
if ($LASTEXITCODE -ne 0) { throw '使用許諾の文書の作成に失敗しました（tools/package/license_docs.py）' }

foreach ($f in ($executables + $libraries)) {
    $binary = Join-Path $UiBinaryDir $f
    if (-not (Test-Path -LiteralPath $binary)) { throw "見つかりません: $binary（-Rebuild で配布用UIを組んでください）" }
    Copy-Item -LiteralPath $binary -Destination $stage
}

# コアは cores\ の下へ。
$coreStage = Join-Path $stage 'cores'
New-Item -ItemType Directory -Force $coreStage | Out-Null
foreach ($f in $(if ($WithoutCores) { @() } else { $coreExecutables })) {
    $binary = Join-Path $CoreBinaryDir $f
    if (-not (Test-Path -LiteralPath $binary)) { throw "見つかりません: $binary（先に組んでください）" }
    Copy-Item -LiteralPath $binary -Destination $coreStage
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
foreach ($f in $(if ($WithoutCores) { @() } else { $dataFiles })) {
    if (-not (Test-Path -LiteralPath $f)) { throw "見つかりません: $f" }
    $dst = Join-Path $stage $f
    New-Item -ItemType Directory -Force (Split-Path -Parent $dst) | Out-Null
    Copy-Item -LiteralPath $f -Destination $dst
}
# **Angband だけ、実行時のデータ置き場が `cores\` 基準になる**（他 8 コアとは違う）。
# `an_main.cpp` の `angband_lib_dir()` は `portable::exe_dir()`（＝ exe 自身の在り処。
# ここでは cores\）をそのまま使っており、Gensoband/Sil-Q/Frox/RogueClone2 の
# ように `portable::data_dir()`（cores\ を 1 段外して箱の直下へ戻す）へは
# 揃えていない。だから `angband/lib`（`lib/hd2d/town.txt` を含む。設計 §2）・
# 固有のタイル対応表・ボクセル板は箱の直下ではなく `cores\angband\` の下に置く。
if (-not $WithoutCores) {
    $angbandLib = 'angband/lib'
    if (-not (Test-Path $angbandLib)) { throw "$angbandLib がありません" }
    # 原作のデータはリポジトリに無い。写していないまま配ると gamedata の無い Angband になる。
    if (-not (Test-Path "$angbandLib/gamedata")) { throw 'angband/lib/gamedata がありません。先に python tools/core_sources/prepare_original_data.py を実行してください' }
    $angbandLibDst = Join-Path $coreStage 'angband/lib'
    New-Item -ItemType Directory -Force (Split-Path -Parent $angbandLibDst) | Out-Null
    Copy-Item -Recurse $angbandLib $angbandLibDst

    # 日本語化。訳のカタログと名前の表（angband/lang）と、
    # 説明の行だけ日本語にした gamedata の写し（angband/lib-ja。作成物で追跡しない）。
    # an_bootstrap.c が日本語のときだけ <lib>/../lib-ja/gamedata を読む。無ければ英語の説明のまま。
    & python tools/angband/an_build_ja_gamedata.py build
    if ($LASTEXITCODE -ne 0) { throw 'an_build_ja_gamedata.py build が失敗しました' }
    # ゲーム内ヘルプの訳（angband/lib-ja/help。設計 §18。ui-help.c が日本語のときだけ先に探す）
    & python tools/angband/an_build_ja_help.py build
    if ($LASTEXITCODE -ne 0) { throw 'an_build_ja_help.py build が失敗しました' }
    foreach ($angbandJa in @('angband/lang', 'angband/lib-ja')) {
        if (-not (Test-Path $angbandJa)) { throw "$angbandJa がありません" }
        $angbandJaDst = Join-Path $coreStage $angbandJa
        New-Item -ItemType Directory -Force (Split-Path -Parent $angbandJaDst) | Out-Null
        Copy-Item -Recurse $angbandJa $angbandJaDst
    }

    $angbandTileMap = 'angband/tilework/mapping.csv'
    if (-not (Test-Path -LiteralPath $angbandTileMap)) { throw "見つかりません: $angbandTileMap" }
    $angbandTileMapDst = Join-Path $coreStage 'angband/tilework/mapping.csv'
    New-Item -ItemType Directory -Force (Split-Path -Parent $angbandTileMapDst) | Out-Null
    Copy-Item -LiteralPath $angbandTileMap -Destination $angbandTileMapDst

    $angbandSlab = 'angband/assets/slab'
    if (-not (Test-Path $angbandSlab)) { throw "$angbandSlab がありません" }
    $angbandSlabDst = Join-Path $coreStage 'angband/assets/slab'
    New-Item -ItemType Directory -Force (Split-Path -Parent $angbandSlabDst) | Out-Null
    Copy-Item -Recurse $angbandSlab $angbandSlabDst

    # AngbandCore.exe が同じ exe_dir() 基準で見にいく共有ファイル 2 本
    # （tilework/tile_mapping.csv・lib/edit/BaseitemDefinitions.jsonc）。
    # 無いと「タイル対応表が開けない」で黙って ASCII の @ 表示へ落ちる
    # （Android 版で先に踏んだのと同じ穴。2026-09-25 に確認）。
    $angbandSharedTileMap = 'tilework/tile_mapping.csv'
    if (-not (Test-Path -LiteralPath $angbandSharedTileMap)) { throw "見つかりません: $angbandSharedTileMap" }
    $angbandSharedTileMapDst = Join-Path $coreStage 'tilework/tile_mapping.csv'
    New-Item -ItemType Directory -Force (Split-Path -Parent $angbandSharedTileMapDst) | Out-Null
    Copy-Item -LiteralPath $angbandSharedTileMap -Destination $angbandSharedTileMapDst

    $angbandBaseitem = 'lib/edit/BaseitemDefinitions.jsonc'
    if (-not (Test-Path -LiteralPath $angbandBaseitem)) { throw "見つかりません: $angbandBaseitem" }
    $angbandBaseitemDst = Join-Path $coreStage 'lib/edit/BaseitemDefinitions.jsonc'
    New-Item -ItemType Directory -Force (Split-Path -Parent $angbandBaseitemDst) | Out-Null
    Copy-Item -LiteralPath $angbandBaseitem -Destination $angbandBaseitemDst

    # **Umoria も実行ファイルの隣（`cores\umoria\`）を読む**（`um_main.cpp` の `configure_paths()`・
    # `um_town.cpp` の `load_map()`）。原作の文章（`umoria/data`。build_windows.py が原作の git から写す。
    # 追跡しない）と町の地図（`umoria/hd2d/town.txt`）。セーブとスコアは `data_dir()` 側（箱の直下の `umoria\`）。
    foreach ($umoriaDir in @('umoria/data', 'umoria/hd2d', 'umoria/lang')) {
        if (-not (Test-Path $umoriaDir)) { throw "$umoriaDir がありません（python tools/umoria/build_windows.py を先に）" }
        $umoriaDst = Join-Path $coreStage $umoriaDir
        New-Item -ItemType Directory -Force (Split-Path -Parent $umoriaDst) | Out-Null
        Copy-Item -Recurse $umoriaDir $umoriaDst
    }
}

# 新UIのアイテムは実行時にボクセル板を描くため、タイル画像をコピーしない。
# コアデータ内の旧2Dタイルも配布ステージから除く。
$legacyTileDirs = @('lib/xtra/graf','tangband/lib/xtra/graf','gensoband/lib/xtra/graf','toband/lib/xtra/graf','silq/lib/xtra/graf','frox/lib/xtra/graf')
foreach ($relative in $legacyTileDirs) {
    $tileDir = Join-Path $stage $relative
    if (Test-Path -LiteralPath $tileDir) { Assert-PackagePath $tileDir; Remove-Item -LiteralPath $tileDir -Recurse -Force }
}

foreach ($file in @('gensoband/tilework/mapping.csv', 'toband/tilework/mapping.csv', 'silq/tilework/terrain_map.csv',
                     'frox/tilework/terrain_map.csv', 'frox/tilework/mapping.csv')) {
    $destination = Join-Path $stage $file
    New-Item -ItemType Directory -Force (Split-Path -Parent $destination) | Out-Null
    Copy-Item -LiteralPath $file -Destination $destination
}
$forbiddenTileImages = @(Get-ChildItem -LiteralPath $stage -Recurse -File | Where-Object {
    $_.Extension -in @('.png','.bmp','.jpg','.jpeg','.webp') -and
    ($_.FullName -match '[\\/]tilework[\\/]' -or $_.FullName -match '[\\/]assets[\\/]tiles[\\/]' -or
        $_.FullName -match '[\\/]lib[\\/]xtra[\\/]graf[\\/]')
})
if ($forbiddenTileImages.Count) { throw "Tile images entered the package stage: $($forbiddenTileImages[0].FullName)" }

# 検査が退避したセーブも配らない。削除対象は今回の配布ステージ内だけに限定する。
$stageRoot = [IO.Path]::GetFullPath($stage).TrimEnd('\') + '\'
foreach ($backup in @(Get-ChildItem -LiteralPath $stage -Directory -Recurse -Force | Where-Object { $_.Name -like '.hd2d-save-*' })) {
    $target = [IO.Path]::GetFullPath($backup.FullName)
    if (-not $target.StartsWith($stageRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected package cleanup path' }
    Remove-Item -LiteralPath $target -Recurse -Force
}

# 遊んだ跡は配らない。**印の 1 枚は残す**——`delete.me`（変愚・短愚・幻想）と
# `.keepalive`（Sil-Q・Frox）・`.gitkeep`（幻想の `save`）で、これを消すと空になり、
# `Compress-Archive` が空のものを詰めないので**箱から消える**。`frox` の `user` は
# コアが**在ることを求める**（`fc_validate_dir(ANGBAND_DIR_USER, 1)`）ので、
# 消えると配布物が起動しない。
$playedDirs = @('lib', 'tangband/lib', 'gensoband/lib', 'toband/lib', 'silq/lib', 'frox/lib')
foreach ($sub in ($playedDirs | ForEach-Object { "$_/save"; "$_/user"; "$_/apex"; "$_/bone"; "$_/data" })) {
    $p = Join-Path $stage $sub
    Assert-PackagePath $p
    New-Item -ItemType Directory -Force -Path $p | Out-Null
    Get-ChildItem -LiteralPath $p -Force | Where-Object { $_.Name -notin @('delete.me', '.keepalive', '.gitkeep') } | ForEach-Object {
        Assert-PackagePath $_.FullName
        Remove-Item -LiteralPath $_.FullName -Recurse -Force
    }
    # Compress-Archive skips dot files. Keep the required runtime directory in the ZIP.
    [IO.File]::WriteAllText((Join-Path $p 'delete.me'), '')
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

foreach ($f in @('LICENSE', 'README.md', 'readme-hengband.md', 'THIRD-PARTY-NOTICES.txt', 'readme_angband')) {
    if (Test-Path $f) { Copy-Item $f $stage }
}

# --- 遊び方の紙 ---------------------------------------------------------------
$notes = @"
変愚蛮怒HD（$Version・Windows x64）

はじめかた
  HengbandHd2d.exe を起動してください。最初に、どのゲームで遊ぶかを選ぶ画面が出ます。
  ゲーム本体（コア）の exe は cores\ の中にあります。画面側から起動されるので、
  直接起動しても動きません。

必要なもの
  * 64 ビットの Windows と、OpenGL 4.6 が使えるグラフィックス
  * Microsoft Visual C++ 再頒布可能パッケージ（x64）
    入っていないと、ウィンドウが出る前に終了します。

入っているもの
  画面 1 つと、11 のゲーム（変愚蛮怒・短愚蛮怒・幻想蛮怒・TOband・Sil-Q・FroxComposband・RogueClone2・Rogue 3.6・Rogue 5.4・Angband・Umoria）。
  セーブデータは各ゲームの lib\save（Angband は箱の直下の angband\save、Umoria は umoria）に書き込まれます。

設定
  hd2d.cfg が実行ファイルと同じ場所に作られます。ゲーム内の機能メニューからも変更できます。
  初回は新UIのAになります。保存済みの表示設定は維持されます。
  機能 → 画面 → ウィンドウのデザインでA/B/CまたはオリジナルUIを選べます。
  新UIには現在の射線・範囲表示の改修を含みます。未対応の効果は範囲を推測表示せず、
  原作の条件で操作を続けられます。全効果への対応は継続中です。

English
  See README.txt.
"@
if ($WithoutCores) {
    $notes = $notes.Replace('画面 1 つと、11 のゲーム（変愚蛮怒・短愚蛮怒・幻想蛮怒・TOband・Sil-Q・FroxComposband・RogueClone2・Rogue 3.6・Rogue 5.4・Angband・Umoria）。', '画面とインポート用コンパイラーを同梱しています。ゲームコアの実行ファイルは含みません。')
    $notes = $notes.Replace('ゲーム本体（コア）の exe は cores\ の中にあります。画面側から起動されるので、', '取り込んだコアはアプリ専用の保存先に登録されます。画面側から起動されるので、')
    $notes += "`r`n初回は「コアをインポート」から対応版の原作ソース ZIP を選択して、ビルド・登録してください。`r`n"
}
if (-not $IncludeCompiler) { $notes += "`r`nこの配布版は10コア同梱です。コアを取り込んでビルドする機能は含みません。`r`n" }
$notes += "`r`nタイル画像は同梱していません。店・所持品・品物選択のアイコンは、原作の記号と色を使ったボクセル板で表示します。`r`n"
$notesPath = Join-Path $stage 'はじめに.txt'
[System.IO.File]::WriteAllText($notesPath, $notes, (New-Object System.Text.UTF8Encoding $true))

# 英語の案内。**上の日本語と同じ 5 つの見出し**にしてある。
# 片方だけ直すと食い違うので、書き換えるときは必ず両方を直すこと。
$notesEn = @"
Hengband HD ($Version, Windows x64)

Getting started
  Run HengbandHd2d.exe. You will be asked which game to play.
  The game core executables are in cores\. They are started by the screen side,
  so running them directly does nothing.

Requirements
  * 64-bit Windows and a graphics device that supports OpenGL 4.6
  * Microsoft Visual C++ Redistributable (x64)
    Without it, the program exits before a window appears.

What is included
  One screen and eleven games (Hengband, Tangband, Gensoband, TOband, Sil-Q, FroxComposband, RogueClone2, Rogue 3.6, Rogue 5.4, Angband, Umoria).
  Save files are written to each game's lib\save directory (Angband: angband\save next to this folder; Umoria: umoria).

Settings
  hd2d.cfg is created next to the executable. You can also change the settings
  from the in-game feature menu.
  New installations start with modern UI theme A. Existing UI preferences are preserved.
  Choose A/B/C or Original under Features > Screen > Window design.
  The new UI includes the current trajectory and area-preview changes. Effects without
  a supported preview keep their original controls and do not show an invented area.
  Coverage for all effects is still in progress.

Japanese
  See はじめに.txt.
"@
if ($WithoutCores) {
    $notesEn = $notesEn.Replace('One screen and eleven games (Hengband, Tangband, Gensoband, TOband, Sil-Q, FroxComposband, RogueClone2, Rogue 3.6, Rogue 5.4, Angband, Umoria).', 'The UI and import compiler are included. Game core executables are not included.')
    $notesEn = $notesEn.Replace('The game core executables are in cores\. They are started by the screen side,', 'Imported cores are stored in application data. They are started by the UI,')
    $notesEn += "`r`nChoose Import core, select an original source ZIP for a supported version, then build and register it.`r`n"
}
if (-not $IncludeCompiler) { $notesEn += "`r`nThis package includes eleven prebuilt cores. Core import and compilation are not included.`r`n" }
$notesEn += "`r`nTile images are not included. Shop, inventory and item-selection icons render the existing voxel board assets.`r`n"
$notesEnPath = Join-Path $stage 'README.txt'
[System.IO.File]::WriteAllText($notesEnPath, $notesEn, (New-Object System.Text.UTF8Encoding $true))

# --- 起動の確認 ---------------------------------------------------------------
# 詰めた画面を 1 度起動する（`--core-select-check`。コアは起こさず、コア選択の画面を確かめて終わる。箱には何も書かない）。
# 2026-10-04 の配布物は起動直後に落ちていたのに、詰める段では誰も起動していなかったので気づけなかった。
$checkExe = Join-Path $stage 'HengbandHd2d.exe'
$hadMute = Test-Path Env:HENGBAND_MUTE
$env:HENGBAND_MUTE = '1'
$check = Start-Process -FilePath $checkExe -ArgumentList '--core-select-check' -WorkingDirectory $stage -PassThru -WindowStyle Hidden
if (-not $hadMute) { Remove-Item Env:HENGBAND_MUTE }
if (-not $check.WaitForExit(120000)) { Stop-Process -Id $check.Id -Force; throw '配布用の画面が 120 秒で終わりませんでした（--core-select-check）' }
if ($check.ExitCode -ne 0) { throw ("配布用の画面の起動の確認に失敗しました（--core-select-check・終了コード " + $check.ExitCode + "）") }
Write-Output '配布用の画面の起動を確かめました（--core-select-check）'

# --- zip ----------------------------------------------------------------------
# **名前は固定**。版は $name（zip の中の箱）に残る。
# **2026-10-06 に `HengbandHd2d-win.zip` から改めた**（決めたこと「ユーザーの目に見えるところから HD2D を除去」。「HD-2D」「HD2D」はスクウェア・エニックスの登録商標。実行ファイルと設定ファイルの名前は変えていない）。
$zip = Join-Path $OutDir 'HengbandHD-win.zip'
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path $stage -DestinationPath $zip

$size = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Write-Output "できました: $zip（$size MB）"
