"""固定ハッシュのコンパイラーを取得して、配布用インポートキットを生成する。開発者専用。"""
import argparse
import hashlib
import json
import re
import uuid
from pathlib import Path
import shutil
import subprocess
import tarfile
import urllib.request
import zipfile
from make_kit import ROOT, build as build_windows
from make_android_kit import build as build_android


def file_hash(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
    return h.hexdigest()


def obtain(info, cache, name):
    cache.mkdir(parents=True,exist_ok=True)
    archive=cache/name
    if not archive.exists():
        temp=archive.with_suffix(archive.suffix+'.download')
        urllib.request.urlretrieve(info['url'],temp)
        if file_hash(temp)!=info['sha256']:raise ValueError('Compiler archive checksum mismatch')
        temp.rename(archive)
    if file_hash(archive)!=info['sha256']:raise ValueError('Compiler archive checksum mismatch: '+str(archive))
    extracted=cache/(name+'-unpacked')
    marker=extracted/'.verified'
    if not marker.exists():
        extracted.mkdir(exist_ok=True)
        if name.endswith('.zip'):
            with zipfile.ZipFile(archive) as z:
                for entry in z.infolist():
                    if not (extracted/entry.filename).resolve().is_relative_to(extracted.resolve()):raise ValueError('Unsafe archive path')
                z.extractall(extracted)
        else:
            with tarfile.open(archive) as t:
                for entry in t:
                    if not entry.isfile():continue
                    path=(extracted/entry.name).resolve()
                    if not path.is_relative_to(extracted.resolve()):raise ValueError('Unsafe archive path')
                    path.parent.mkdir(parents=True,exist_ok=True)
                    with t.extractfile(entry) as source,path.open('wb') as target:shutil.copyfileobj(source,target)
        marker.write_text(info['sha256'],encoding='ascii')
    return extracted/info['directory']


def stage_windows_compiler(source, destination):
    """コンパイルに必要なバイナリーと DLL、ヘッダー・ライブラリ・許諾だけを配る。"""
    stage=destination.parent/('.new-toolchain-'+uuid.uuid4().hex);stage.mkdir()
    queue=['clang++.exe','clang.exe','clang-23.exe','ld.lld.exe'];seen=set()
    while queue:
        name=queue.pop()
        if name in seen:continue
        seen.add(name);file=source/'bin'/name
        raw=subprocess.check_output([str(source/'bin/llvm-readobj.exe'),'--coff-imports',str(file)]).decode()
        for dependency in re.findall(r'^  Name: (.+)$',raw,re.M):
            if (source/'bin'/dependency.strip()).is_file():queue.append(dependency.strip())
        (stage/'bin').mkdir(exist_ok=True);shutil.copy2(file,stage/'bin'/name)
    for f in (source/'bin').glob('*.cfg'):shutil.copy2(f,stage/'bin'/f.name)
    for folder in ['include','i686-w64-mingw32','lib/clang']:shutil.copytree(source/folder,stage/folder)
    shutil.copy2(source/'LICENSE.TXT',stage/'LICENSE.TXT')
    backup=destination.parent/('.old-toolchain-'+uuid.uuid4().hex)
    if destination.exists():destination.rename(backup)
    try:stage.rename(destination)
    except BaseException:
        if backup.exists():backup.rename(destination)
        raise
    if backup.exists():
        if backup.resolve().parent!=destination.resolve().parent:raise ValueError('Unexpected compiler cleanup path')
        shutil.rmtree(backup)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cache',type=Path,default=ROOT/'scratch_old/core-import/download-cache')
    p.add_argument('--platform',choices=['windows','android','all'],default='all')
    p.add_argument('--ndk',type=Path,default=Path('C:/Android/sdk/ndk/27.2.12479018'))
    p.add_argument('--database',type=Path)
    a=p.parse_args();info=json.loads((Path(__file__).with_name('toolchains.json')).read_text(encoding='utf-8-sig'))
    win=obtain(info['windows'],a.cache,'llvm-mingw.zip')
    build_windows(ROOT/'core-import')
    stage_windows_compiler(win,ROOT/'core-import/toolchain')
    shutil.copy2(Path(__file__).with_name('toolchains.json'),ROOT/'core-import/toolchains.json')
    if a.platform!='windows':
        android=obtain(info['android'],a.cache,'android-ndk.tar.xz')
        output=ROOT/'android/core-import-kit'
        database=a.database
        if database is None:
            choices=[p for p in (ROOT/'android/hd2d/.cxx').glob('**/arm64-v8a/compile_commands.json') if 'hengcore.dir' in p.read_text(encoding='utf-8')]
            if not choices:raise FileNotFoundError('Build all five Android cores before preparing the import kit')
            database=max(choices,key=lambda p:p.stat().st_mtime_ns)
        build_android(ROOT/'core-import',output,database,android)
        jni=ROOT/'android/core-import-jni/arm64-v8a';jni.mkdir(parents=True,exist_ok=True)
        native=android/'toolchains/llvm/prebuilt/linux-x86_64/bin'
        shutil.copy2(native/'clang-21',jni/'libhbclang.so');shutil.copy2(native/'lld',jni/'libhblld.so')
        compiler=a.ndk/'toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe'
        subprocess.run([str(compiler),'--target=aarch64-linux-android33','--sysroot='+str(compiler.parent.parent/'sysroot'),'-fPIE','-pie','-Wl,-z,max-page-size=16384',str(Path(__file__).with_name('android_linker.c')),'-o',str(jni/'libhblink.so')],check=True)
        licenses=output/'licenses';licenses.mkdir(exist_ok=True)
        for f in android.glob('NOTICE*'):shutil.copy2(f,licenses/f.name)
    print('Import kits prepared. Package Windows/APK using the normal packaging scripts.')


if __name__=='__main__':main()
