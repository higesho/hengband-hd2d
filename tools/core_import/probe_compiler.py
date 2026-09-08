"""開発時に同梱コンパイラー向けのビルド定義を作る。利用者 ZIP 内のスクリプトは使わない。"""
import argparse
import concurrent.futures
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools'))
from transcode_cp932_src import transcode


def windows_recipe(core, source):
    projects={'hengband':'HengbandCore','tangband':'TangbandCore','gensoband':'GensobandCore','silq':'SilCore','frox':'FroxCore'}
    project=ROOT/'VisualStudio'/projects[core]/(projects[core]+'.vcxproj')
    tree=ET.parse(project); ns={'m':'http://schemas.microsoft.com/developer/msbuild/2003'}
    def path(value):
        value=value.replace('$(HengbandCoreSourceRoot)',str(source)).replace('\\','/')
        return str((project.parent/value).resolve())
    defs=next(n for n in tree.findall('m:ItemDefinitionGroup',ns) if 'Release' in n.get('Condition',''))
    definitions=defs.find('m:ClCompile/m:PreprocessorDefinitions',ns).text.split(';')
    include=defs.find('m:ClCompile/m:AdditionalIncludeDirectories',ns).text.split(';')
    include=[path(x) for x in include if '$' not in x or x.startswith('$(HengbandCoreSourceRoot)')]
    flags=['--target=i686-w64-windows-gnu','-w','-O1','-fms-extensions','-fcommon','-Wno-implicit-int','-Wno-implicit-function-declaration','-Wno-incompatible-pointer-types','-Wno-int-conversion']
    flags += ['-D'+x for x in definitions if '%' not in x]
    flags += ['-I'+x for x in include]
    units=[]
    for item in tree.findall('m:ItemGroup/m:ClCompile',ns):
        if any(x.text=='true' and ('Release' in x.get('Condition','Release')) for x in item.findall('m:ExcludedFromBuild',ns)): continue
        file=Path(path(item.get('Include')))
        lang='c++' if core in ('hengband','tangband') or file.suffix=='.cpp' else 'c'
        extra=['-x',lang]+(['-std=c++20'] if lang=='c++' else [])
        if core in ('hengband','tangband'): extra+=['-include',str(source/'src/stdafx.h')]
        for d in item.findall('m:PreprocessorDefinitions',ns):
            extra += ['-D'+x for x in (d.text or '').split(';') if '%' not in x]
        for d in item.findall('m:ForcedIncludeFiles',ns):
            for x in (d.text or '').split(';'):
                if x and '%' not in x: extra += ['-include',x]
        units.append((file,flags+extra))
    return units


def main():
    p=argparse.ArgumentParser();p.add_argument('core');a=p.parse_args()
    work=ROOT/'scratch_old/core-import/probe';work.mkdir(exist_ok=True)
    source=Path(os.environ.get('HENGBAND_CORE_SOURCE_ROOT',ROOT.parent/'roguelike-cores/build-sources')).resolve()
    # This probe uses the already verified external sources. The runtime will reconstruct from ZIP.
    units=windows_recipe(a.core,source)
    tool=next((ROOT/'scratch_old/core-import/windows-toolchain').glob('*/bin/clang++.exe'))
    if a.core in ('gensoband','silq'):
        for folder,encoding in [(source/'gensoband/src','cp932'),(ROOT/(a.core+'/adapter'),'utf8')]:
            if a.core=='silq' and folder.name=='src': continue
            dest=work/a.core/('source' if folder.name=='src' else 'adapter')
            for f in folder.rglob('*'):
                if not f.is_file():continue
                target=dest/f.relative_to(folder);target.parent.mkdir(parents=True,exist_ok=True)
                data=f.read_bytes()
                if f.suffix in ('.c','.h','.cpp'): data,_=transcode(data,source_encoding=encoding)
                target.write_bytes(data)
            units=[(Path(str(f).replace(str(folder),str(dest))),[x.replace(str(folder),str(dest)) for x in flags]) for f,flags in units]
    out=work/a.core/'obj';out.mkdir(parents=True,exist_ok=True)
    def compile(pair):
        i,(file,flags)=pair;obj=out/(str(i)+'.o')
        r=subprocess.run([str(tool),*flags,'-c',str(file),'-o',str(obj)],capture_output=True)
        if r.returncode:
            (out/(str(i)+'.log')).write_bytes(r.stdout+r.stderr)
            print('FAIL',file, (r.stdout+r.stderr).decode('utf-8','replace')[:1800],flush=True)
        return r.returncode,obj
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool: result=list(pool.map(compile,enumerate(units)))
    if any(r[0] for r in result): raise SystemExit(1)
    args=[str(tool),'--target=i686-w64-windows-gnu','-static',*[str(x[1]) for x in result],'-lwinmm','-lgdi32','-lws2_32','-ldbghelp','-o',str(work/(a.core+'.exe'))]
    rsp=work/(a.core+'-link.rsp');rsp.write_text('\n'.join(chr(34)+x.replace('\\','/')+chr(34) for x in args[1:]),encoding='utf-8');r=subprocess.run([args[0],'@'+str(rsp)],capture_output=True);print(r.stderr.decode('utf-8','replace'));raise SystemExit(r.returncode)

if __name__=='__main__':main()
