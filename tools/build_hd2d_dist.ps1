<#
.SYNOPSIS
  ボクセル HD2D の「動く一式」（`dist/HengbandHd2d/`）を組み立てる。

.DESCRIPTION
  `HengbandHd2d.exe`（描画と UI）と `HengbandCore.exe`（ゲーム本体）の 2 つで動く。
  HD2D 側が起動時にコアを起こして標準入出力で繋がる（プロトコル v1）ので、**両方が要る**。

  これまで手で組んでいたものを台本にしたもの（2026-08-10・P10 第 4 陣）。
  `tools/build_windows_release.ps1`（SDL2 UI 版のリリース）とは**別物**である:

    - 入れる exe が違う（あちらは `Hengband.exe` 1 つ、こちらは Hd2d ＋ Core の 2 つ）
    - **`assets/voxel/` が主役**（あちらの exe はこれを読まない）
    - セーブを**入れる**（見てもらうための一式なので。あちらは配らない）。
      配る相手が別の人なら `-NoSaves`（2026-08-11 に決めた）

.PARAMETER TileSize
  `tilework/sfc` の PNG をこの画素数へ縮めて同梱する。0 なら原本のまま（846MB）。
  既定 **128**（リリース版と同じ「圧縮版」。約 51MB）。以前の手組みは 256 だった。

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\build_hd2d_dist.ps1

.EXAMPLE
  # ビルドは済んでいる／タイルは原本のまま／ZIP は作らない
  powershell -ExecutionPolicy Bypass -File tools\build_hd2d_dist.ps1 -TileSize 0 -SkipBuild -NoZip

.EXAMPLE
  # セーブを入れずに組む（人に渡す一式）
  powershell -ExecutionPolicy Bypass -File tools\build_hd2d_dist.ps1 -NoSaves

.NOTES
  - このファイルは **UTF-8 BOM 付き**で保存すること（PowerShell 5.1 は BOM 無しを ANSI で読む）。
  - 出来上がりは cwd 相対で `assets/` と `lib/` を読む。**フォルダ構成を変えない。**
#>
param(
    [int]$TileSize = 128,
    [switch]$SkipBuild,
    [switch]$NoZip,
    [switch]$NoSaves,
    [string]$OutRoot = "dist"
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $repo

function Say([string]$m) { Write-Host "[hd2d-dist] $m" }
function Die([string]$m) { Write-Host "[hd2d-dist] !! $m"; exit 1 }

<#
  .SYNOPSIS
    PE ヘッダの機械語（x64 / Win32）を読む。
  .DESCRIPTION
    画面が x64・コアが Win32 になったので、**同梱する DLL の版を取り違えると
    配った先でだけ落ちる**（手元は再頒布可能パッケージが入っているので気づけない）。
    組み立てのときに 1 度見ておく。
#>
function Get-PeMachine([string]$path) {
    $fs = [IO.File]::OpenRead((Resolve-Path $path))
    try {
        $br = New-Object IO.BinaryReader($fs)
        $fs.Position = 0x3C
        $pe = $br.ReadInt32()
        $fs.Position = $pe + 4
        $machine = $br.ReadUInt16()
    } finally {
        $fs.Close()
    }
    switch ($machine) {
        0x8664 { "x64" }
        0x14C  { "Win32" }
        default { "不明($machine)" }
    }
}

$name = "HengbandHd2d"
$dest = Join-Path $repo (Join-Path $OutRoot $name)

# -------------------------------------------------------------------- ビルド
if (-not $SkipBuild) {
    $msbuild = "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"
    if (-not (Test-Path $msbuild)) { Die "MSBuild がありません: $msbuild" }
    # **起動中の exe があると LNK1104 で落ちる**（実際に踏んだ）。先に落とす。
    foreach ($p in @("HengbandHd2d", "HengbandCore", "TangbandCore")) {
        Get-Process $p -ErrorAction SilentlyContinue | Stop-Process -Force
    }
    <#
      **画面は x64・コアは Win32。** VR（OpenXR）のために画面側だけ x64 にしてある
      。`Release|Win32` は**画面を組まない**ので、
      両方を Win32 で組んでいた頃の書き方のままだと**古い画面 exe がそのまま配られる**。
      別プロセスでパイプはバイト列なので、混在していてよい。
    #>
    $builds = @(
        @{ Proj = "HengbandCore\HengbandCore.vcxproj"; Platform = "Win32" },
        @{ Proj = "TangbandCore\TangbandCore.vcxproj"; Platform = "Win32" },
        @{ Proj = "HengbandHd2d\HengbandHd2d.vcxproj"; Platform = "x64" }
    )
    foreach ($b in $builds) {
        Say "ビルド: $($b.Proj) (Release|$($b.Platform))"
        & $msbuild (Join-Path ".\VisualStudio" $b.Proj) /m /t:Build `
            /p:Configuration=Release /p:Platform=$($b.Platform) /v:minimal /nologo
        if ($LASTEXITCODE -ne 0) { Die "ビルドに失敗しました: $($b.Proj) (exit=$LASTEXITCODE)" }
    }
}
foreach ($exe in @("HengbandHd2d.exe", "HengbandCore.exe", "TangbandCore.exe")) {
    if (-not (Test-Path ".\$exe")) { Die "$exe がありません（-SkipBuild を外して実行する）" }
}

# ---------------------------------------------------------------- 組み立て
if (Test-Path $dest) { Remove-Item $dest -Recurse -Force }
New-Item $dest -ItemType Directory | Out-Null
Copy-Item .\HengbandHd2d.exe, .\HengbandCore.exe, .\TangbandCore.exe -Destination $dest

<#
  DLL 群。ビルド後イベントがリポジトリ直下へ置いている（画面が x64 になったので**全部 x64**）。
  `openxr_loader.dll` は VR の loader、`jsoncpp.dll` はそれが読む依存である。
  **コア（Win32）は DLL を 1 つも要求しない**（`winmm` と `dbghelp` だけ）ので、
  ここに x64 の DLL しか無くても衝突しない。
#>
$dlls = @("SDL2.dll", "SDL2_image.dll", "SDL2_ttf.dll",
          "freetype.dll", "libpng16.dll", "z.dll", "bz2.dll",
          "brotlicommon.dll", "brotlidec.dll",
          "openxr_loader.dll", "jsoncpp.dll")
foreach ($dll in $dlls) {
    if (-not (Test-Path ".\$dll")) { Die "$dll がありません（ビルド後イベントが走っていない）" }
    Copy-Item ".\$dll" -Destination $dest
}
foreach ($dll in @("SDL2.dll", "openxr_loader.dll")) {
    if ((Get-PeMachine (Join-Path $dest $dll)) -ne "x64") {
        Die "$dll が x64 ではありません（Win32 の版が直下に残っている。x64 で組み直すこと）"
    }
}

<#
  MSVC ランタイム。**同梱できない。**画面は x64・コアは Win32 なので、要る CRT が
  `MSVCP140.dll` という**同じ名前で 2 つ**あり、1 つのフォルダには片方しか置けない。
  片方を置くと、もう片方の exe が「そこに見つけた DLL の機械語が違う」で
  0xc000007b（アプリケーションを正しく起動できませんでした）になる。

  だから**再頒布可能パッケージを入れてもらう**ことにして、手に入るなら
  インストーラを同梱する（`vcredist/`。ふつうのゲーム機なら既に入っている）。
#>
$vcDir = Join-Path $dest "vcredist"
$vcFound = @()
foreach ($arch in @("x64", "x86")) {
    $inst = Get-ChildItem "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Redist\MSVC\*\vc_redist.$arch.exe" -ErrorAction SilentlyContinue |
        Sort-Object FullName | Select-Object -Last 1
    if ($null -ne $inst) {
        if (-not (Test-Path $vcDir)) { New-Item $vcDir -ItemType Directory | Out-Null }
        Copy-Item $inst.FullName -Destination $vcDir
        $vcFound += $arch
    }
}
if ($vcFound.Count -gt 0) {
    Say "MSVC 再頒布可能パッケージを同梱しました（$($vcFound -join ' / ')）"
} else {
    Say "!! MSVC 再頒布可能パッケージのインストーラが見つかりませんでした（README で案内する）"
}

# ---- assets/。**voxel が主役**（対応表とプレハブ。ここが無いと仮の箱しか出ない）。
Copy-Item .\assets -Destination $dest -Recurse
$voxelCount = (Get-ChildItem (Join-Path $dest "assets\voxel") -Filter *.jsonc).Count
# 実体の板（2026-08-15）。**これが無いとキャラもモンスターも 1 体も出ない**ので、
# 地形のプレハブとは別に数えて、下の検算でも見る。
$slabCount = (Get-ChildItem (Join-Path $dest "assets\voxel\slab") -Filter *.vox -ErrorAction SilentlyContinue).Count

# ---- lib/。Makefile.am と .gitattributes は落とす。**セーブは既定で入れる**（見てもらう一式なので）。
$null = & robocopy .\lib (Join-Path $dest "lib") /E /XF Makefile.am .gitattributes desktop.ini /NFL /NDL /NJH /NJS /NP
if ($LASTEXITCODE -ge 8) { Die "lib のコピーに失敗しました（robocopy exit=$LASTEXITCODE）" }

# ---- tangband/lib。
#      コア選択で TangbandCore.exe を選んだときだけ読まれる。edit/help ＋ 記録系の空フォルダ。
$null = & robocopy .\tangband\lib (Join-Path $dest "tangband\lib") /E /XF Makefile.am .gitattributes desktop.ini /NFL /NDL /NJH /NJS /NP
if ($LASTEXITCODE -ge 8) { Die "tangband/lib のコピーに失敗しました（robocopy exit=$LASTEXITCODE）" }

<#
  `-NoSaves`: セーブを入れない一式（2026-08-11 に決めた「配布用をセーブデータ除いて作成して」）。

  **フォルダそのものは残す。** `lib/save/` が無いとコアはセーブを書けない。
  `delete.me` は本家がその「空フォルダを配る」ために置いている札なので、それだけ残す。
  `*.sdl2panels`（UI が覚えた小窓の配置）もセーブに付随するものなので一緒に落とす。
#>
if ($NoSaves) {
    $saveDir = Join-Path $dest "lib\save"
    if (Test-Path $saveDir) {
        Get-ChildItem $saveDir -File | Where-Object { $_.Name -ne "delete.me" } | Remove-Item -Force
    }
    # 遊んだ記録（`playrecord-<名前>.txt`）もセーブに付随するもの。人に渡す一式には要らない。
    Get-ChildItem (Join-Path $dest "lib\user") -Filter "playrecord-*.txt" -ErrorAction SilentlyContinue |
        Remove-Item -Force
    # 短愚蛮怒側のセーブも同じ扱い（分離ディレクトリ tangband/lib/save）。
    $tangSave = Join-Path $dest "tangband\lib\save"
    if (Test-Path $tangSave) {
        Get-ChildItem $tangSave -File | Where-Object { $_.Name -ne "delete.me" } | Remove-Item -Force
    }
    Say "セーブを外しました（lib/save・tangband/lib/save は delete.me だけ・遊んだ記録も外した）"
}

<#
  自動拾いは**既定で「何も拾わない」**（2026-08-11 に決めた:
  「自動拾いが一部有効になっている。＄以外の自動拾いはデフォルトで OFF にして」）。

  **お金（＄）は自動拾いの対象ではない。**踏めば必ず拾う（`carry()` が
  `py_pickup_all_golds_on_floor()` を無条件で呼ぶ）ので、設定を空にすると
  ちょうど「＄以外は拾わない」になる。

  **消すのではなく注釈だけのファイルを置く。**消すと、ゲーム中に自動拾いエディタ（`_`）を
  開いた瞬間に `lib/pref/picktype.prf`（516 行の見本）がコピーされて全部有効に戻る
  （`autopick-reader-writer.cpp` の `prepare_default_pickpref`）。
#>
# `picktype*.prf` と、こちらで取った控え（`*.orig`）はどちらも配る一式には要らない。
$pickTypes = Get-ChildItem (Join-Path $dest "lib\user") -ErrorAction SilentlyContinue |
    Where-Object { ($_.Name -like "picktype*") -or ($_.Extension -eq ".orig") }
foreach ($pt in $pickTypes) { Remove-Item $pt.FullName -Force }
$pickNote = @"
#***
#***  自動拾いは既定で「何も拾わない」。
#***  お金（＄）は自動拾いの対象ではなく、踏めば必ず拾う。
#***  拾いたいものが出てきたら、ゲーム中に ``_``（自動拾いエディタ）で足すこと。
#***  本家の見本を入れたいときは lib/pref/picktype.prf をこの場所へ写す。
#***
"@
$pickPath = Join-Path $dest "lib\user\picktype.prf"
[System.IO.File]::WriteAllText($pickPath, ($pickNote -replace "`r?`n", "`r`n"),
    [System.Text.Encoding]::GetEncoding(932))
Say "自動拾いを空にしました（lib/user/picktype.prf は注釈だけ）"

# ---- tilework/sfc。**タイルの参照はリポジトリ相対のパスで焼き込んである**
#      （`presentation/bridge/official_tile_table.h`）ので、ここが無いと全部 placeholder になる。
$tileDst = Join-Path $dest "tilework\sfc"
if ($TileSize -le 0) {
    Say "タイルを原本のまま同梱します（時間がかかります）"
    New-Item $tileDst -ItemType Directory -Force | Out-Null
    $null = & robocopy .\tilework\sfc $tileDst *.png /NFL /NDL /NJH /NJS /NP
    if ($LASTEXITCODE -ge 8) { Die "タイルのコピーに失敗しました（robocopy exit=$LASTEXITCODE）" }
} else {
    Say "タイルを最大 ${TileSize}px へ縮小して同梱します"
    & py -3 tools\shrink_tiles.py --src tilework\sfc --dst $tileDst --max-side $TileSize
    if ($LASTEXITCODE -ne 0) { Die "タイルの縮小に失敗しました（exit=$LASTEXITCODE）" }
}
Copy-Item .\tilework\tile_mapping.csv -Destination (Join-Path $dest "tilework") -ErrorAction SilentlyContinue

<#
  ---- hd2d.cfg（VR の値を**先に書いておく**）------------------------------------

  VR の見え方は cfg でしか変えられない（機能メニューに VR の節はまだ無い。M4）。
  空のフォルダには cfg が無く、キーの綴りも分からないので、**注釈つきで置いておく**。
  値はどれも既定と同じなので、書いてあること自体は挙動を変えない。

  手元の `hd2d.cfg`（自分で詰めたカメラなど）があればそれを土台にする。
  **`vr_` で始まる行は落としてから足す**——二重に書くと後の行が勝ち、
  「直したのに効かない」の元になる。
#>
$cfgLines = @()
if (Test-Path .\hd2d.cfg) {
    # `cores=` / `last_core=` も落とす——手元の cfg には配らないコア（幻想蛮怒など）が
    # 並んでいることがあり、写すと選択画面に「起動できないコア」が出る。落としておけば
    # 初回起動の自動発見（**在る exe だけ**並べる）が正しい一覧を作る。
    $cfgLines = Get-Content .\hd2d.cfg | Where-Object { $_ -notmatch '^\s*(vr_|cores=|last_core=)' }
}
$vrBlock = @'

# ============================================================ VR（メモ帳で直す）
# 直したら再起動すること（cfg は起動時に読む）。どれも仮の値。
# 卓は畳 1 畳（1.82×0.91m）で固定。動かせるのは高さ・距離・縮尺・板の置き場所。
vr_table_height_m=0
vr_table_forward_m=0.7
vr_tile_m=0.025
# 文字の大きさ（1 桁の見込み角）。上げると読めるが板が視界より大きくなる。
vr_panel_deg_per_cell=0.4
vr_panel_dist_m=1.2
vr_panel_pitch_deg=12
vr_panel_pitch_fps_deg=10
# 一人称の 1 マス(m)。0 なら身長から決める（ふつう 3m）。
vr_fps_cell_m=0
# 板 1 枚ずつの手置き: vr_panel_<名前>=<左右角>,<上下角>,<距離m>,<倍率>（倍率 0 で出さない）
#   名前: status sub1 sub2 sub3 sub4 sub5 minimap message prompt bottom term
#vr_panel_status=-40,10,1.2,1.0
#vr_panel_minimap=45,20,1.3,1.2
#vr_panel_sub5=0,0,1.2,0
'@
$cfgText = (($cfgLines -join "`r`n") + "`r`n" + ($vrBlock -replace "`r?`n", "`r`n"))
[System.IO.File]::WriteAllText((Join-Path $dest "hd2d.cfg"), $cfgText,
    (New-Object System.Text.UTF8Encoding($false)))
Say "hd2d.cfg を置きました（VR の値を注釈つきで先に書いてある）"

# ------------------------------------------------------------------ README
$tileNote = if ($TileSize -le 0) { "原本のまま（最大 640px）" } else { "最大 ${TileSize}px へ縮小したもの" }
$saves = (Get-ChildItem (Join-Path $dest "lib\save") -File |
          Where-Object { $_.Extension -eq "" -and $_.Name -ne "delete.me" } |
          Select-Object -ExpandProperty Name) -join " / "
$saveTitle = if ($NoSaves) { "動く一式（BGM・効果音つき／セーブ無し）" } else { "動く一式（BGM・効果音・セーブつき）" }
$saveIntro = if ($NoSaves) {
    "リリース用ではなく、**そのまま動かして見るための一式**。音楽・効果音は入っているが、`n" +
    "**セーブは 1 つも入れていない**（新しくキャラクターを作るところから始まる）。"
} else {
    "リリース用ではなく、**そのまま動かして見るための一式**。作業中のセーブと`n音楽・効果音もそのまま入れてある。"
}
$saveSection = if ($NoSaves) {
    "## セーブ`n`n**入っていない。**タイトルで ``C) Create a new character`` から始めること。`n" +
    "遊んだ結果は ``lib/save/`` に出来る（リポジトリのセーブとは別物）。"
} else {
    "## 入っているセーブ`n`n    $saves`n`nタイトルで ``L) Load`` を選ぶと名前順に並ぶ。**ANGWIL / ZUL** はアングウィルとズルの`n" +
    "町を見るためのもの（レベル 50）、**TELMORA** はテルモラ、**PLAYER** は辺境の地、`n**DBG** は鉄獄 5 階（レベル 50）。"
}
$readme = @"
変愚蛮怒 ボクセル HD2D — $saveTitle
=============================================================

$saveIntro

組み立てた台本: tools/build_hd2d_dist.ps1（タイル ${TileSize}px）

## 起動

    HengbandHd2d.exe

**必ずこのフォルダを作業ディレクトリにして起動すること。** ``assets/`` と ``lib/`` を
cwd 相対で読む（ショートカットを作るなら「作業フォルダー」をここに向ける）。

``HengbandHd2d.exe`` は絵を描くだけで、ゲーム本体は同じフォルダの ``HengbandCore.exe`` を
自分で起こして繋がる（プロトコル v1・標準入出力）。**両方が要る。**

起動すると**ゲームコアの選択**が出る（変愚蛮怒／短愚蛮怒）。↑↓ で選んで Enter。
次からは前回選んだコアにカーソルが載る。

## 短愚蛮怒（コア選択で選ぶ）

変愚蛮怒のバリアント（tanguband/tangband v26.0.4 相当）。コンセプトは「サクサク・スピーディー」:

- **ゲーム内 14 日以内に混沌のサーペントを倒す**（誕生時オプション doomsday・既定 ON）。
  15 日目からは「終末のサーペント」が湧き続けて世界が終わる
- 成長 10 倍速（必要経験値 1/10）・高級品の出現率が高い・ユニークは高級品を複数ドロップ
- セーブ・スコア・データは ``tangband/lib/`` に分かれる（変愚蛮怒のセーブとは別物）

## 入っているもの

    HengbandHd2d.exe      3D の描画と UI（**x64**。VR はこちらが喋る）
    HengbandCore.exe      変愚蛮怒コア（コア選択の既定。**Win32**）
    TangbandCore.exe      短愚蛮怒コア（コア選択で選ぶと起動。**Win32**）
    tangband/lib/         短愚蛮怒のデータ一式（edit / help。セーブもここに分かれる）
    *.dll                 SDL2 一式と openxr_loader（どれも x64）
    vcredist/             MSVC 再頒布可能パッケージ（入っていなければ入れる。下記）
    assets/voxel/         ボクセル素材 ${voxelCount} 冊と対応表（町とダンジョンの意匠もここ）
    assets/voxel/slab/    実体の板 ${slabCount} 枚（キャラ・モンスター・アイテム）
    assets/ui/            タイトル画とパネルの背面
    assets/tiles/         タイルの置き場所（既定は placeholder。実体は下の tilework/）
    tilework/sfc/         公式タイル（${tileNote}）
    lib/                  ゲームのデータ一式（edit / xtra の音 / save / user）

$saveSection

## ダンジョンの意匠（P10 第 4 陣・2026-08-10）

20 本すべてのダンジョンに、名前の意味に沿った**小物**が入っている。
4 本（**イークの洞穴・迷宮・城・金鉱**）は床と壁の**材ごと**替えてある。

深い所は歩いて行くのが大変なので、**合成フロアに N 番のダンジョンの顔をさせる**
口を用意してある（絵を見るためだけの口。ゲームは起こさない）:

    HengbandHd2d.exe --terrain-check --dungeon=15 --windowed=1600x900

## VR（Quest 2 ＋ Virtual Desktop）

**別の機械で初めて動かすときは、この順で 1 つずつ確かめること。** 途中で止まったら、
そこが原因である（先へ進んでも何も見えない）。

### 0. 下ごしらえ（この一式の外）

- **MSVC 再頒布可能パッケージ**が要る。``vcredist\vc_redist.x64.exe`` と
  ``vc_redist.x86.exe`` を**両方**入れる（画面が x64・ゲーム本体が Win32 なので両方）。
  「0xc000007b」「VCRUNTIME140.dll が見つかりません」で止まるならこれ
- PC 側に **Virtual Desktop Streamer**、Quest 側に Virtual Desktop アプリ
- グラフィックドライバは新しめのもの（**OpenGL 4.6** が要る）

### 1. まず平らな画面で動くか

    HengbandHd2d.exe --windowed=1600x900

ここが動かないなら VR 以前の問題（DLL かドライバ）。

### 2. HMD を繋いでセッションだけ確かめる

    HengbandHd2d.exe --vr-check

Quest を被った状態で走らせる。**左目が青・右目が緑**に見えれば通っている。
画面（コンソール）には ``XR_RUNTIME VirtualDesktopXR …`` と
``states … -> FOCUSED``、``drawn N frames`` が出る。

- ``XR_ERROR_FORM_FACTOR_UNAVAILABLE`` … HMD を被っていない／繋がっていない
- ランタイム名が ``SteamVR/OpenXR`` になっているときは、Virtual Desktop の
  Streaming 設定の「OpenXR runtime」がそちらを向いている。**どちらでも動く**のが
  狙いなので、できれば両方で試す

### 3. 本編を VR で

    HengbandHd2d.exe --vr

- **ジオラマ**（既定）… 目の前に**畳 1 畳のテーブル**があり、その上に地図が載る。
  まわりは暗い部屋。文字は**空中の板**になって頭のまわりに浮かぶ
- **一人称**（F7 かコントローラーの L3）… ゲーム空間の中に立つ。空も天井もある。
  板は同じように宙に浮く。右スティックの左右で**45 度ずつ振り向く**（座ったままでも後ろを向ける）
- 頭の向きがそのまま照準になる（一人称）。歩くのは左スティック
- **窓には左目の絵が出る**（鏡）。PC の画面で見ている人にも分かる

### 4. 見え方を直す（``hd2d.cfg`` をメモ帳で開く）

**この一式の数値はすべて仮**である。被ってみて合わないところを直してほしい。
書き換えたら**再起動**（cfg は起動時に読む）。

    vr_table_height_m=0        天板の高さ(m)。0 なら頭の高さから決める（座り約0.70/立ち約1.15）
    vr_table_forward_m=0.70    頭から卓の中心まで(m)。卓が近すぎ／遠すぎるとき
    vr_tile_m=0.025            地図の 1 マス(m)。大きくすると地図が育つ（卓からはみ出してもよい）
    vr_panel_deg_per_cell=0.40 文字の大きさ。上げると読みやすいが板が視界に入らなくなる
    vr_panel_dist_m=1.20       板までの距離(m)
    vr_panel_pitch_deg=12      板を持ち上げる角(度)。卓と重なるなら上げる
    vr_panel_pitch_fps_deg=10  一人称のときの持ち上げ角
    vr_fps_cell_m=0            一人称の 1 マス(m)。0 なら身長から決める（ふつう 3m）

板は**1 枚ずつ**置き場所を決められる（``<左右角>,<上下角>,<距離m>,<倍率>``。
**倍率 0 でその板を出さない**）:

    vr_panel_status=-40,10,1.2,1.0     状態列を左 40 度へ
    vr_panel_minimap=45,20,1.3,1.2     ミニマップを右上へ・少し大きく
    vr_panel_sub5=0,0,1.2,0            重量の板を出さない

名前は ``status`` ``sub1``〜``sub5`` ``minimap`` ``message`` ``prompt`` ``bottom`` ``term``。

**字が小さいと感じたら、``vr_panel_deg_per_cell`` を上げる前に窓を小さくするほうが効く**
（``--windowed=1280x720``）。桁数が減るぶん 1 桁が大きくなる。

### 5. うまくいかないとき

- **何も見えない／真っ黒** … ``--vr`` を付けずに動くかを先に見る。VR が立たないときは
  理由をコンソールへ出して**平らな画面のまま続ける**（落ちない）
- **上下が逆** … コンソールの ``XR_RUNTIME`` の名前と一緒に教えてほしい（ランタイム差）
- **酔う** … 一人称の ``vr_fps_cell_m`` を 3 より大きく（世界が大きくなり、歩幅が伸びる）。
  機能メニュー ＞ カメラ ＞「移動のなめらかさ」を「実体だけ」にするのも効く
- **コンソールの記録がほしい** …
  ``HengbandHd2d.exe --vr 2> vr.log``（``vr.log`` に起動行が残る）
- **重い／かくつく** … いちばん効くのは **Virtual Desktop の画質設定**（描く画素数を
  ランタイムが決めているので、こちらでは選んでいない）。次に ``--post=bloom``
  （色調整とビネットを落とす）、``--post=off``（ポスト処理を全部切る）
- **日本語が ``?`` になる** … 日本語版 Windows でない機械では ``msgothic.ttc`` が無い。
  ``assets/fonts/placeholder_mono.ttf`` に日本語の入った TTF を置けば最優先で使う

## 覚えておくとよい起動引数

    --vr                       VR で起動（既定は切。立たなければ理由を出して平らなまま続く）
    --vr-check                 VR の検査（ゲームは起こさない。左目が青・右目が緑）
    --vr-fake                  疑似 HMD（HMD 無しで両眼の絵を窓に並べる。見え方の確認用）
    --windowed=1600x900        窓で起動（既定は全画面）
    --layout=full|hybrid|split 画面の作り（既定は split。実行中は F9 でも回せる）
    --camera=46,40,110         見下ろし角・画角・1 マスの画素
    --dof=0〜2.4               被写界深度の強さ
    --post=off                 ポスト処理を全部切る（比べる基準）
    --dungeon=N                合成フロアに N 番のダンジョンの顔をさせる（上記）
    --town-view --town-data=lib\edit\towns\03_Morivant.txt
                               **町のデータをその場で描く**（ゲームは起こさない）。
                               --town-at=x,y で見る場所、--shot=out.bmp で撮って終わる
    --edit=名前                ボクセルエディタ（素材を直す）

実行中のキー: F10 = 機能メニュー / F9 = 画面の作り / F8 = サブパネル / F7 = 一人称視点 /
Alt+Enter = 画面モード

## 操作の割り当て（2026-08-11）

機能メニュー（F10）→ **「操作の割り当て」**で、**操作ごとに**キーボードとコントローラーを決める。

- 行が操作・列がキーボード／コントローラー。カーソル（□ 枠）を升へ合わせて **Enter で変更モード**
- 変更モードで**押したキー／ボタンがその升の割り当てになる**
- **ESC は「割り当てを消す」**（コアの既定のキーへ戻る）。15 秒何も押さないと変更モードを降りる
- **十字キー・Enter・ESC**（コントローラーは十字・A・B）は選ぶ／決定／取消として
  システムが押さえているので、ほかの操作へは割り当てられない
- 画面の最下段に、**コントローラーの全ボタンの割り当て**が常に出ている

## コントローラーでマクロを使う（2026-08-14）

**割り当ての無いボタンは、そのままマクロのトリガーになる。**

1. ``@`` →(4) マクロの作成 →「トリガーキー:」で**そのボタンを押す**（``\[Pad_Start]`` と出る）
2. マクロ行動を入れて決定
3. ゲーム中にそのボタンを押すとマクロが走る

``@`` →(2) で ``.prf`` に書き出せるので、マクロはセーブと一緒に持ち歩ける。
既定の割り当てのままだと空いているのは **Start** だけなので、増やすときは
「操作の割り当て」で **ESC を押して割り当てを外し**、空きを作ること。

### 同時押し

**LB / RB を押しながら**別のボタンを押すと、別の枠になる（LB＋RB で 3 層目）。
枠は 7 ボタン × 3 層 ＝ **21 個増える**。それぞれにマクロも普通のコマンドも割り当てられる。

- **LB / RB は単独でも今までどおり使える**（押した瞬間ではなく、離したときに走る）
- 修飾を押している間、最下段の帯とバーチャルパッドの札が**その層の中身に変わる**
- マクロが乗っているボタンには「マクロ」と出る

## 階が変わったときの演出（2026-08-11）

黒い幕が地図を覆い、**地名**が左から・**階層**が右から入ってきて中央で止まります。
**Enter（Space・コントローラーの A）で先へ進みます**——左右へ抜けて地図が浮かび上がります。

- 出るのは**地上マップとダンジョン**だけ。**全体マップ（``<`` の広域マップ）には出ません**
- 地上は**地名だけ**（階層はありません）
- 待っている間、キーはゲームへ届きません（押し間違いでコマンドが走らないように）

## 一人称視点（F7）

    W / S           前 / 後ろへ 1 歩          A / D    左 / 右へ 1 歩（横歩き）
    Q / E           左 / 右を向く（押している間ずっと回り、離すと 45° の刻みへ吸い付く）
    マウスを動かす  視点を回す（左右＋上下。**ボタンは要りません**）
    左クリック      決定                      右クリック   取消
    ホイール押込み  視界を水平に戻す（コントローラーの R3 でも同じ）
    ホイール回す    画角 90〜120 度
    右 Shift        機能メニュー

上下（仰角・俯角）は最大 60 度まで。**機能メニュー ＞ カメラ ＞「一人称の上下視点」**で
入／切を切り替えられます。切ると水平（やや下向き）に固定されます。

一人称の間はマウスが窓に取り込まれます（カーソルが消えます）。メニューを開くと戻ります。

ミニマップの自分は、一人称のときだけ**向いている方角を向いた三角**になります。

WASDQE 以外のキーは一人称でも今までどおりゲームのコマンドとして働く。

## コントローラーの既定の割り当て

    X   拾う              Y   投射物を撃つ
    R1  階段を降りる      L1  階段を上る
    R2  掘る              L2  探す
    R3  視界を水平に戻す  L3  一人称の入切（F7 と同じ）
    A   決定              B   取消          Select  機能メニュー（固定）

画面の最下段に、いまの割り当てが常に出ています。変えるのは「操作の割り当て」から。

## リアルタイム進行（新機能・既定は切）

    機能メニュー ＞ ゲーム進行

    リアルタイム進行  実時間でゲームが進む。押さずにいると行動機会を見送る
                      （加速していれば 1 秒に何度も動ける）
    進行の速さ        1 行動あたりの秒数（0.2〜3.0 秒）。通常速度のときの目安
    小窓の裏          持ち物・足元の選択を開いている間も世界を進めるか。
                      「進む」だと選びながら殴られ、死ぬこともある
    被弾の閃き        受けた属性の色で画面が光る（物理＝白・炎＝赤 …）
    被弾の揺れ        画面が揺れる。酔う人は切る

**切っていれば従来どおりのターン制です。** 詳しくは
``（リポジトリ側）。

## 環境変数（見るとき・測るとき）

    HD2D_FORCE_TIME=20:30      描画の時刻を上書き（コアには触らない）。夜の町を見る
    HD2D_GPU_SYNC=1            vsync を切って glFinish（費用を測るとき）
    HD2D_TERRAIN_VARIANCE=0    マスごとの色の散らしを切る

## 注記

- ``lib/user/user.prf`` に ``Y:auto_more`` と ``Y:allow_debug_opts`` が入っている。
  **無人検証のための設定**なので、普通に遊ぶなら消してよい
- ``hd2d.cfg`` は初回起動時に作られる。消しても既定値で立ち上がる。
  **VR の値もここに書く**（上の「VR」の節）。機能メニューには**まだ VR の節が無い**
- **セーブは元のリポジトリと別物になる。**ここで遊んだ結果はリポジトリへは返らない
- **タイルの実体は ``tilework/sfc/`` にある。**参照は
  ``presentation/bridge/official_tile_table.h`` にリポジトリ相対のパスで焼き込んであるので、
  ここが無いとモンスターも道具も地形も placeholder になる
"@
$readme | Set-Content -Path (Join-Path $dest "README.txt") -Encoding UTF8

# ------------------------------------------------------------------ 検算
$files = Get-ChildItem $dest -Recurse -File
$bytes = ($files | Measure-Object -Property Length -Sum).Sum
foreach ($must in @("HengbandHd2d.exe", "HengbandCore.exe", "assets\voxel\terrain_prefabs.jsonc",
                    "lib\edit\DungeonDefinitions.jsonc", "tilework\sfc",
                    "assets\voxel\slab\P0_0_0.vox",
                    "TangbandCore.exe", "tangband\lib\edit\MonraceDefinitions.jsonc",
                    "tangband\lib\help\jhelp.hlp", "tangband\lib\save\delete.me",
                    "assets\voxel\slab\R1417.vox", "tilework\sfc\R1417.png",
                    "openxr_loader.dll", "hd2d.cfg")) {
    if (-not (Test-Path (Join-Path $dest $must))) { Die "同梱漏れ: $must" }
}
<#
  **exe の機械語を出しておく。**画面が x64・コアが Win32 という混在は間違えやすく、
  取り違えると配った先でだけ落ちる（`Release|Win32` で組むと画面が古いまま残る）。
#>
$machHd2d = Get-PeMachine (Join-Path $dest "HengbandHd2d.exe")
$machCore = Get-PeMachine (Join-Path $dest "HengbandCore.exe")
$machTang = Get-PeMachine (Join-Path $dest "TangbandCore.exe")
if ($machHd2d -ne "x64") { Die "HengbandHd2d.exe が x64 ではありません（$machHd2d）。VR が動きません" }
Say "exe: HengbandHd2d=$machHd2d / HengbandCore=$machCore / TangbandCore=$machTang"
$tileCount = (Get-ChildItem $tileDst -Filter *.png).Count
if ($tileCount -lt 1000) { Die "タイルが少なすぎます（$tileCount 枚）" }
# **板の枚数も見る。**1,700 枚前後あるはずで、少ないと出ない実体が出る。
if ($slabCount -lt 1500) { Die "実体の板が少なすぎます（$slabCount 枚）" }
Say "ファイル $($files.Count) 個 / $([math]::Round($bytes / 1MB, 1))MB（プレハブ $voxelCount 冊・板 $slabCount 枚・タイル $tileCount 枚）"

# -------------------------------------------------------------------- ZIP
if (-not $NoZip) {
    $zip = Join-Path (Join-Path $repo $OutRoot) "$name.zip"
    if (Test-Path $zip) { Remove-Item $zip -Force }
    Say "ZIP を作ります（枚数が多いので数分かかります）: $zip"
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::CreateFromDirectory($dest, $zip,
        [System.IO.Compression.CompressionLevel]::Optimal, $true)
    Say "ZIP 完成: $zip（$([math]::Round((Get-Item $zip).Length / 1MB, 1))MB）"
}

Say "完了: $dest"
