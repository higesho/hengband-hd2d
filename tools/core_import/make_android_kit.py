"""Android 端末内コンパイル用の定義を既存 CMake のコンパイル一覧から作る。"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
from probe_compiler import ROOT

def build(windows, output, database, toolchain):
    shutil.copytree(windows,output,dirs_exist_ok=True,ignore=shutil.ignore_patterns('toolchain'))
    catalog=json.loads((output/'catalog.json').read_text(encoding='utf-8'))
    commands=json.loads(database.read_text(encoding='utf-8'))
    source=Path(os.environ.get('HENGBAND_CORE_SOURCE_ROOT',ROOT.parent/'roguelike-cores/build-sources')).resolve().as_posix()
    tree=toolchain/'toolchains/llvm/prebuilt/linux-x86_64'
    for folder in ['sysroot','lib/clang']:
        shutil.copytree(tree/folder,output/'toolchain'/folder,dirs_exist_ok=True)
    # SDL のリンク先とヘッダー。原作のソースは含めない。
    artifacts=list((ROOT/'android/hd2d/build/intermediates/cxx').glob('**/arm64-v8a/libSDL2.so'))
    if not artifacts:raise FileNotFoundError('Build the Android app before preparing the import kit')
    obj=max(artifacts,key=lambda p:p.stat().st_mtime_ns).parent
    (output/'libs').mkdir(exist_ok=True)
    for name in ['libSDL2.so','libSDL2_mixer.so']:shutil.copy2(obj/name,output/'libs'/name)
    headers=output/'sdk/android-include';headers.mkdir(exist_ok=True)
    shutil.copytree(ROOT/'third_party/android/SDL2/include',headers/'SDL2',dirs_exist_ok=True)
    shutil.copy2(ROOT/'third_party/android/SDL2_mixer/include/SDL_mixer.h',headers/'SDL_mixer.h')
    def replace(s):
        s=s.replace('\\','/')
        for core in ['gensoband','silq']:
            s=s.replace(source+'/android-sjis/'+core+'/adapter','{kit}/sdk/'+core+'/adapter')
            s=s.replace(source+'/android-sjis/'+core+'/src','{source}/'+core+'/src')
        s=s.replace(source+'/src/external-lib/include','{kit}/sdk/third_party/cpp/include') if active not in ['hengband','tangband'] else s
        s=s.replace(source,'{source}').replace(ROOT.as_posix(),'{kit}/sdk')
        if s.startswith('-I') and ('/.cxx/' in s or '/third_party/android/' in s):return '-I{kit}/sdk/android-include'
        if s.startswith('--sysroot='):return '--sysroot={kit}/toolchain/sysroot'
        return s
    for active,target in [('hengband','hengcore'),('tangband','tangcore'),('gensoband','gensocore'),('silq','silcore'),('frox','froxcore')]:
        recipe=json.loads((output/(active+'.json')).read_text(encoding='utf-8'))
        result=[];objects=[]
        for row in commands:
            if 'CMakeFiles/'+target+'.dir/' not in row['command'].replace('\\','/'):continue
            args=shlex.split(row['command'].replace('\\','/'))[1:]
            cleaned=[];i=0
            while i<len(args):
                if args[i] in ['-o','-c']:i+=2;continue
                if args[i]=='-g':i+=1;continue
                cleaned.append(replace(args[i]));i+=1
            file=replace(row['file']);obj='{work}/'+str(len(result))+'.o';objects.append(obj)
            lang='c++' if any(x.startswith('-std=c++') or x.startswith('-std=gnu++') for x in cleaned) else 'c'
            result.append(['{compiler}','-I{kit}/sdk/android-include/SDL2','--driver-mode=g++','-resource-dir={kit}/toolchain/lib/clang/21',*cleaned,'-O1','-Wno-incompatible-pointer-types','-x',lang,'-c',file,'-o',obj])
        if len(result)<10:raise ValueError('The compile database must contain all five cores: '+target)
        link=['--driver-mode=g++','--target=aarch64-linux-android33','--sysroot={kit}/toolchain/sysroot','-resource-dir={kit}/toolchain/lib/clang/21','--ld-path={linker}','-shared','-static-libstdc++','-Wl,-Bsymbolic','-Wl,--no-undefined','-Wl,-z,max-page-size=16384','-llog']
        if active in ['hengband','tangband']:link+=['-L{kit}/libs','-lSDL2','-lSDL2_mixer']
        link+=['-o','{output}']
        result.append(['{compiler}','@{work}/link.rsp'])
        recipe.update(platform='android',output='lib'+target+'.so',commands=result,responses={'link.rsp':'\n'.join('"'+s+'"' for s in objects+link)})
        raw=json.dumps(recipe,ensure_ascii=False,separators=(',',':')).encode()
        (output/(active+'.json')).write_bytes(raw);catalog['targets'][active]['sha256']=hashlib.sha256(raw).hexdigest()
        print(active,len(result),flush=True)
    catalog['sdk_sha256']={p.relative_to(output).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((output/'sdk').rglob('*')) if p.is_file() and not any(part.startswith('.') for part in p.relative_to(output).parts)}
    (output/'catalog.json').write_text(json.dumps(catalog,indent=2)+'\n',encoding='utf-8',newline='\n')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--windows',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--database',type=Path,required=True);p.add_argument('--toolchain',type=Path,required=True);a=p.parse_args();build(a.windows,a.output,a.database,a.toolchain)
