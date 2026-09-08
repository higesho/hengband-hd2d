# 外部原作からコアをビルドする

原作ソースは UI のリポジトリに含めない。UI 側に残すのは接続コード、ビルド設定、変更パッチ、ゲームデータ、翻訳と資産である。

## 配置

```text
workspace/
  hengband-hd2d/          UI・アダプター・パッチ
    tools/core_sources/manifest.json
    tools/core_sources/patches/
  roguelike-cores/
    upstream/                   原作の個別 Git リポジトリ（改変しない）
      hengband/ tangband/ gensoband/ silq/ frox/
    build-sources/              再構成した作業用ソース（Git 管理しない）
      src/ gensoband/src/ silq/src/ frox/src/
      android-sjis/             Android 用の変換結果
```

原作の版と取得元は管理先の `manifest.json`、再構成に必要な版・ファイル・パッチのハッシュはこのディレクトリの `manifest.json` に固定している。
`prepare.py` は原作の作業ツリーではなく指定コミットの Git オブジェクトを読む。ネットからの自動取得はしない。
原作の取得元と固定版は [原作 ZIP の対応表](../core_import/README.md#対応するソース) を参照する。各原作の Git リポジトリを上記の名前で clone し、manifest.json の固定コミットが含まれる状態にする。

短愚蛮怒は従来どおり、変愚の作業用ソースを `TANGBAND` 定義でビルドする。短愚 26.0.4 の統合済み差分は `hengband.patch` に含まれる。
原作の短愚ソースを直接ビルドする方式へ変更するとゲーム内容が変わるため、今回その変更はしない。

## ソースの準備と検証

```powershell
python tools/core_sources/prepare.py
python tools/core_sources/prepare.py --verify
```

デフォルト以外の場所を使う場合:

```powershell
python tools/core_sources/prepare.py --upstream D:/sources/upstream --output D:/build/core-sources
```

`HENGBAND_UPSTREAM_ROOT` と `HENGBAND_CORE_SOURCE_ROOT` 環境変数でも指定できる。
改行と BOM の変換はファイルごとに記録し、パッチ適用後の全ファイルを起点の SHA-256 と比較する。
既存の生成結果は検証して再利用する。内容が変わっていた場合や、別の版を構成する場合は既存フォルダーを上書きせず、新しい出力先を指定する。

## Windows

```powershell
./tools/core_sources/Build-Windows.ps1 -Python python
```

このコマンドは準備・検証の後に 5 コアをビルドする。`-Rebuild` でクリーンビルド。
必要なら `-MSBuild`、`-Upstream`、`-CoreSourceRoot` を指定する。
既存の Visual Studio ソリューションも、準備後は同じようにビルドできる。

```powershell
msbuild VisualStudio/Hengband.sln /p:Configuration=Release /p:Platform=x64 /m
```

直接プロジェクトをビルドする際の参照先は `/p:HengbandCoreSourceRoot=D:/build/core-sources` で変更できる。
コアの出力は従来どおり UI の実行体と同じ場所。ゲームデータの場所やセーブの形式は変えない。

UI だけなら原作の準備は不要:

```powershell
msbuild VisualStudio/HengbandHd2d/HengbandHd2d.vcxproj /p:Configuration=Release /p:Platform=x64 /m
```

## Android / Quest

原作を準備した後に従来の Gradle ビルドを実行する。`HB_CORE_SOURCE_ROOT` CMake 変数で作業用ソースの場所を変更できる。
CP932 のソース変換結果も外部の `build-sources/android-sjis/` に置く。
`tools/package/Build-Hd2dApk.ps1` は準備・検証と変換を先に実行する。

UI ライブラリ単体のビルドでは `-DHENGBAND_BUILD_CORES=OFF` を指定でき、原作やその変換結果を必要としない。
通常の APK は既存と同じ 5 コアを同梱する。UI 単体ビルドをそのまま「5 コアで遊べる APK」と扱わない。

## 変更を維持する

生成した原作ソースを直接コミットしない。接続コードの変更は従来どおり UI リポジトリへ記録する。
原作部分への修正が必要な場合はパッチとファイルハッシュを更新し、新しい出力先へ再構成して全コアのビルド・回帰検査を通す。
編集した外部ソースから差分を確認し、必要な場合だけ記録する:

```powershell
python tools/core_sources/capture.py --source-root D:/build/edited-sources
python tools/core_sources/capture.py --source-root D:/build/edited-sources --record
python tools/core_sources/prepare.py --output D:/build/verified-sources
```

`--record` なしではパッチを変更しない。`--core` で対象を限定できる。
記録後は新しい出力先で再構成・検証する。原作の保管ディレクトリを編集対象にしない。

過去の公開履歴は書き換えない。再構成の基準はこのフォルダーのmanifest.jsonとパッチで管理する。

検証の入口: [回帰検査](../hd2d_verify/README.md)。
