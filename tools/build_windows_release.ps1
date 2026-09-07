<#
.SYNOPSIS
  SDL2 UI 版（HD2D 入り）の Windows 配布セットを作る。

.DESCRIPTION
  上流の `Build-Windows-Release-Package.ps1`（GDI 版・VS2019・タイル無し）と同じ
  「何を入れて何を消すか」の規則を踏襲しつつ、SDL2 UI に要るものを足したもの。

  上流との差分:
    - ビルド構成が `Release|Win32` ＋ `/p:HENGBAND_SDL2_UI=1 /p:HENGBAND_SDL2_HD2D=1`
    - vcpkg の SDL2 群 DLL と MSVC ランタイム（/MD なので要る）を同梱する
    - `tilework/sfc`（原本 640px・1.2GB）を **縮小して**同梱する（既定 128px）
    - `assets/`（placeholder タイル等）を同梱する
    - 英語版（`English-Release`）は作らない。SDL2 UI 側の表示文言が日本語前提のため

  MSBuild の場所は VS18 Community 固定。

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\build_windows_release.ps1

.EXAMPLE
  # 原本のタイルをそのまま入れる（1.3GB 超）／ビルドは済んでいる
  powershell -ExecutionPolicy Bypass -File tools\build_windows_release.ps1 -TileSize 0 -SkipBuild

.NOTES
  - このファイルは **UTF-8 BOM 付き**で保存すること（PowerShell 5.1 は BOM 無しを ANSI として読む）。
  - `lib/save` `lib/user` の中身は入れない（自分のセーブと設定を配らないため）。
#>
param(
    [string]$Version = "",
    [int]$TileSize = 128,
    [switch]$SkipBuild,
    [switch]$NoZip,
    [switch]$WithPdb,
    [string]$OutRoot = "dist"
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $repo

function Say([string]$m) { Write-Host "[release] $m" }
function Die([string]$m) { Write-Host "[release] !! $m"; exit 1 }

# ---------------------------------------------------------------- バージョン
if ($Version -eq "") {
    $header = Get-Content "src\system\angband-version.h" -Encoding UTF8 -Raw
    $major = [regex]::Match($header, 'H_VER_MAJOR\s+(\d+)').Groups[1].Value
    $minor = [regex]::Match($header, 'H_VER_MINOR\s+(\d+)').Groups[1].Value
    $patch = [regex]::Match($header, 'H_VER_PATCH\s+(\d+)').Groups[1].Value
    $extra = [regex]::Match($header, 'H_VER_EXTRA\s+(\d+)').Groups[1].Value
    $status = [regex]::Match($header, 'VERSION_STATUS\s*=\s*VersionStatusType::(\w+)').Groups[1].Value
    $suffix = switch ($status) {
        "ALPHA" { "Alpha$extra" }
        "BETA" { "Beta$extra" }
        "RELEASE_CANDIDATE" { "RC$extra" }
        default { "" }
    }
    $Version = "$major.$minor.$patch$suffix"
}
$name = "Hengband-$Version-sdl2-win32-jp"
$dest = Join-Path $repo (Join-Path $OutRoot $name)
Say "版 = $Version / 出力先 = $dest"

# -------------------------------------------------------------------- ビルド
if (-not $SkipBuild) {
    $msbuild = "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"
    if (-not (Test-Path $msbuild)) { Die "MSBuild がありません: $msbuild" }
    Say "Release|Win32 をビルドします（HENGBAND_SDL2_UI=1 / HD2D=1）"
    & $msbuild .\VisualStudio\Hengband.sln /m /t:Build /p:Configuration=Release /p:Platform=Win32 `
        /p:HENGBAND_SDL2_UI=1 /p:HENGBAND_SDL2_HD2D=1 /v:minimal /nologo
    if ($LASTEXITCODE -ne 0) { Die "ビルドに失敗しました（exit=$LASTEXITCODE）" }
}
if (-not (Test-Path ".\Hengband.exe")) { Die "Hengband.exe がありません（-SkipBuild を外して実行する）" }

# ---------------------------------------------------------------- 組み立て
if (Test-Path $dest) { Remove-Item $dest -Recurse -Force }
New-Item $dest -ItemType Directory | Out-Null

# 本体と読み物。`readme_angband` は上流の同梱物（ライセンス告知を含む）。
Copy-Item -Path .\Hengband.exe, .\readme.md, .\autopick.txt, .\THIRD-PARTY-NOTICES.txt -Destination $dest
Copy-Item -Path .\readme_angband -Destination $dest -Recurse

<#
  デバッグ情報（pdb）は **既定ではセットに入れず、ZIP の隣に別置き**にする。
  123MB あり、タイルとデータを足した配布物の約半分を占めてしまう一方、
  遊ぶ側には要らない（落ちたときの解析に要るのは配る側）。
  上流の `Build-Windows-Release-Package.ps1` は同梱しているので、
  同じにしたいときは -WithPdb を付ける。
#>
if ($WithPdb) {
    Copy-Item -Path .\Hengband.pdb -Destination $dest
} elseif (Test-Path .\Hengband.pdb) {
    Copy-Item -Path .\Hengband.pdb -Destination (Join-Path $repo (Join-Path $OutRoot "$name.pdb"))
}

# ---- SDL2 群 DLL（vcpkg x86-windows の Release 版）。ビルド後イベントがルートへ置いている。
$sdlDlls = @(
    "SDL2.dll", "SDL2_image.dll", "SDL2_ttf.dll",
    "freetype.dll", "libpng16.dll", "z.dll", "bz2.dll",
    "brotlicommon.dll", "brotlidec.dll", "brotlienc.dll", "libcurl.dll"
)
foreach ($dll in $sdlDlls) {
    if (-not (Test-Path ".\$dll")) { Die "$dll がありません（Release ビルドのビルド後イベントが走っていない）" }
    Copy-Item ".\$dll" -Destination $dest
}

# ---- MSVC ランタイム。SDL2 構成は /MD なので、再配布可能パッケージ未導入の環境では
#      これが無いと起動しない。exe の隣に置く形（local deployment）で同梱する。
$crtDir = Get-ChildItem "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Redist\MSVC\*\x86\Microsoft.VC*.CRT" -Directory -ErrorAction SilentlyContinue |
    Sort-Object FullName | Select-Object -Last 1
if ($null -eq $crtDir) {
    Say "!! MSVC ランタイム（x86）が見つかりませんでした。同梱を飛ばします"
    Say "!! 配布先には『Visual C++ 2015-2022 再配布可能パッケージ (x86)』が要ります"
} else {
    Copy-Item (Join-Path $crtDir.FullName "*.dll") -Destination $dest
    Say "MSVC ランタイムを同梱: $($crtDir.Name)"
}

# ---- lib/。Makefile.am と *.raw と .gitattributes は入れない（上流と同じ）。
#      save/ user/ は **中身を配らない**（自分のセーブと設定が混ざる）。
$roboLog = & robocopy .\lib (Join-Path $dest "lib") /E /XF Makefile.am *.raw .gitattributes desktop.ini /XD save user /NFL /NDL /NJH /NJS /NP
if ($LASTEXITCODE -ge 8) { Die "lib のコピーに失敗しました（robocopy exit=$LASTEXITCODE）" }
foreach ($sub in @("save", "user")) {
    $d = Join-Path $dest "lib\$sub"
    New-Item $d -ItemType Directory | Out-Null
    Copy-Item ".\lib\$sub\delete.me" -Destination $d -ErrorAction SilentlyContinue
}
# ハイスコアの雛形だけは戻す（*.raw を落としているため。上流と同じ扱い）。
Copy-Item .\lib\apex\h_scores.raw -Destination (Join-Path $dest "lib\apex")
# 音楽は mp3 と cfg と readme だけ（上流と同じ）。
Get-ChildItem (Join-Path $dest "lib\xtra\music") -File |
    Where-Object { $_.Name -notmatch '\.(mp3|cfg)$' -and $_.Name -ne "readme.txt" } |
    Remove-Item -Force

# ---- assets/（placeholder タイルとフォントの README）。
Copy-Item .\assets -Destination $dest -Recurse

<#
  bot/ … AI ボットに遊ばせるための一式（K-49）。
  仕様書だけ入れてもキーを送る道具が無いと動かないので、送り手も一緒に入れる。
  ゲーム側の JSON 出力は**既定 OFF**なので、入れておいても普通に遊ぶ人には影響しない。
#>
$botDir = Join-Path $dest "bot"
New-Item $botDir -ItemType Directory | Out-Null
Copy-Item .\docs\-Destination $botDir
Copy-Item .\tools\sdl2_verify\send_keys.ps1 -Destination $botDir

# ---- tilework/sfc。原本は 640px・1.2GB なので既定では縮小して入れる。
$tileDst = Join-Path $dest "tilework\sfc"
if ($TileSize -le 0) {
    Say "タイルを原本のまま同梱します（1.2GB。時間がかかります）"
    New-Item $tileDst -ItemType Directory -Force | Out-Null
    $null = & robocopy .\tilework\sfc $tileDst *.png /NFL /NDL /NJH /NJS /NP
    if ($LASTEXITCODE -ge 8) { Die "タイルのコピーに失敗しました（robocopy exit=$LASTEXITCODE）" }
} else {
    Say "タイルを最大 ${TileSize}px へ縮小して同梱します"
    & py -3 tools\shrink_tiles.py --src tilework\sfc --dst $tileDst --max-side $TileSize
    if ($LASTEXITCODE -ne 0) { Die "タイルの縮小に失敗しました（exit=$LASTEXITCODE）" }
}

# ---- 同梱物の説明。exe を配る側が最初に読む 1 枚。
$tileNote = if ($TileSize -le 0) { "原本（最大 640px）" } else { "最大 ${TileSize}px へ縮小したもの" }
$readme = @"
変愚蛮怒 $Version — SDL2 UI 版（Windows / 32bit）

■ 遊び方
  Hengband.exe をそのまま実行してください。インストールは要りません。
  フォルダごと移動・コピーして構いませんが、**中のフォルダ構成は変えないでください**
  （lib と tilework を exe と同じ場所に置いたまま使います）。

■ 動作環境
  Windows 10 / 11。OpenGL 2.0 以上が使える環境なら HD2D 表示も動きます。
  MSVC ランタイムは同梱しています（同梱できなかった場合は
  「Visual C++ 2015-2022 再配布可能パッケージ (x86)」を入れてください）。

■ 操作
  キーボードは従来どおりです。ゲームパッドとタッチにも対応しています。
  F10（またはパッドの Select）で機能メニューが開き、表示・音量・キーバインドなどを
  その場で変えられます。設定は Hengband.exe と同じ場所の
  sdl2_ui_options.cfg / sdl2_pad_binds.cfg に保存されます（初回起動時に作られます）。

■ タイル
  tilework/sfc の PNG は$tileNote です。
  表示の大きさは機能メニューの「タイル表示サイズ」で変えられます。

■ セーブ
  lib/save に保存されます。上書き更新するときは lib/save を残してください。

■ AI ボットに遊ばせる（bot フォルダ）
  ゲームの状態を JSON で書き出し、外からキーを送って操作できます。
  仕様と手順は bot/、キーを送る道具は bot/send_keys.ps1 です。
  **既定は OFF** なので、普通に遊ぶぶんには何も起きません（F10 の機能メニューの
  「ボット用 JSON 出力」で ON にします）。

■ ライセンスと権利表記
  readme.md / readme_angband / THIRD-PARTY-NOTICES.txt を参照してください。
"@
$readmePath = Join-Path $dest "README-SDL2.txt"
$readme | Set-Content -Path $readmePath -Encoding UTF8

# ------------------------------------------------------------------ 検算
$files = Get-ChildItem $dest -Recurse -File
$bytes = ($files | Measure-Object -Property Length -Sum).Sum
$leaks = $files | Where-Object { $_.Name -eq "Makefile.am" -or $_.Extension -eq ".raw" -and $_.Name -ne "h_scores.raw" }
if ($leaks.Count -gt 0) { Say "!! 入ってはいけないファイルが $($leaks.Count) 件あります: $($leaks[0].FullName)" }
$saveLeak = Get-ChildItem (Join-Path $dest "lib\save") -File | Where-Object { $_.Name -ne "delete.me" }
if ($saveLeak.Count -gt 0) { Die "セーブが混ざっています: $($saveLeak[0].Name)" }
Say "ファイル $($files.Count) 個 / $([math]::Round($bytes / 1MB, 1))MB"

# -------------------------------------------------------------------- ZIP
if (-not $NoZip) {
    $zip = Join-Path (Join-Path $repo $OutRoot) "$name.zip"
    if (Test-Path $zip) { Remove-Item $zip -Force }
    Say "ZIP を作ります（枚数が多いので数分かかります）: $zip"
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::CreateFromDirectory($dest, $zip,
        [System.IO.Compression.CompressionLevel]::Optimal, $true)
    $zipMb = [math]::Round((Get-Item $zip).Length / 1MB, 1)
    Say "ZIP 完成: $zip（${zipMb}MB）"
}

Say "完了: $dest"
