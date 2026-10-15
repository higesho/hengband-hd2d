"""開発時に同梱コンパイラー向けのビルド定義を作る。遊ぶ人 ZIP 内のスクリプトは使わない。"""
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


ROGUE36_C = ['vers.c', 'armor.c', 'chase.c', 'command.c', 'daemon.c', 'daemons.c', 'fight.c', 'init.c', 'io.c', 'list.c', 'main.c',
             'mdport.c', 'misc.c', 'monsters.c', 'move.c', 'newlevel.c', 'options.c', 'pack.c', 'passages.c', 'potions.c', 'rings.c',
             'rip.c', 'rooms.c', 'save.c', 'scrolls.c', 'state.c', 'sticks.c', 'things.c', 'weapons.c', 'wizard.c', 'xcrypt.c']
ROGUE54_C = ['vers.c', 'extern.c', 'armor.c', 'chase.c', 'command.c', 'daemon.c', 'daemons.c', 'fight.c', 'init.c', 'io.c', 'list.c',
             'mach_dep.c', 'main.c', 'mdport.c', 'misc.c', 'monsters.c', 'move.c', 'new_level.c', 'options.c', 'pack.c', 'passages.c',
             'potions.c', 'rings.c', 'rip.c', 'rooms.c', 'save.c', 'scrolls.c', 'state.c', 'sticks.c', 'things.c', 'weapons.c',
             'wizard.c', 'xcrypt.c']
ANGBAND_ADAPTER_C = ['an_bootstrap.c', 'an_null_term.c', 'an_wiz_stats_stub.c', 'an_map_shim.c', 'an_native_inventory.c',
                     'an_text_hooks.c', 'an_pad_shim.c', 'an_ui_shim.c', 'an_targeting.c', 'an_town_rng.c', 'an_town_map.c',
                     'an_profile.c', 'an_fx.c', 'an_wide.c', 'an_name_ja.c', 'an_lore_ja.c']
# 原作のファイル名の一覧は tools/angband/build_windows.py と同じ（149 本。main.c と main-*.c は組まない）。
ANGBAND_CORE_C = [
    "buildid.c",
    "cave-map.c",
    "cave-square.c",
    "cave-view.c",
    "cave.c",
    "cmd-cave.c",
    "cmd-core.c",
    "cmd-misc.c",
    "cmd-obj.c",
    "cmd-pickup.c",
    "cmd-spoil.c",
    "cmd-wizard.c",
    "datafile.c",
    "debug.c",
    "effect-handler-attack.c",
    "effect-handler-general.c",
    "effects-info.c",
    "effects.c",
    "game-event.c",
    "game-input.c",
    "game-world.c",
    "gen-cave.c",
    "gen-chunk.c",
    "gen-monster.c",
    "gen-room.c",
    "gen-util.c",
    "generate.c",
    "grafmode.c",
    "guid.c",
    "init.c",
    "load.c",
    "message.c",
    "mon-attack.c",
    "mon-blows.c",
    "mon-desc.c",
    "mon-group.c",
    "mon-init.c",
    "mon-list.c",
    "mon-lore.c",
    "mon-make.c",
    "mon-move.c",
    "mon-msg.c",
    "mon-predicate.c",
    "mon-spell.c",
    "mon-summon.c",
    "mon-timed.c",
    "mon-util.c",
    "obj-chest.c",
    "obj-curse.c",
    "obj-desc.c",
    "obj-gear.c",
    "obj-ignore.c",
    "obj-info.c",
    "obj-init.c",
    "obj-knowledge.c",
    "obj-list.c",
    "obj-make.c",
    "obj-pile.c",
    "obj-power.c",
    "obj-properties.c",
    "obj-randart.c",
    "obj-slays.c",
    "obj-tval.c",
    "obj-util.c",
    "option.c",
    "parser.c",
    "player-attack.c",
    "player-birth.c",
    "player-calcs.c",
    "player-class.c",
    "player-history.c",
    "player-path.c",
    "player-properties.c",
    "player-quest.c",
    "player-race.c",
    "player-spell.c",
    "player-timed.c",
    "player-util.c",
    "player.c",
    "project-feat.c",
    "project-mon.c",
    "project-obj.c",
    "project-player.c",
    "project.c",
    "randname.c",
    "save-charoutput.c",
    "save.c",
    "savefile.c",
    "score-util.c",
    "score.c",
    "sound-core.c",
    "source.c",
    "store.c",
    "target.c",
    "trap.c",
    "ui-birth.c",
    "ui-command.c",
    "ui-context.c",
    "ui-curse.c",
    "ui-death.c",
    "ui-display.c",
    "ui-effect.c",
    "ui-entry-combiner.c",
    "ui-entry-renderers.c",
    "ui-entry.c",
    "ui-equip-cmp.c",
    "ui-event.c",
    "ui-game.c",
    "ui-help.c",
    "ui-history.c",
    "ui-init.c",
    "ui-input.c",
    "ui-keymap.c",
    "ui-knowledge.c",
    "ui-map.c",
    "ui-menu.c",
    "ui-mon-list.c",
    "ui-mon-lore.c",
    "ui-obj-list.c",
    "ui-object.c",
    "ui-options.c",
    "ui-output.c",
    "ui-player-properties.c",
    "ui-player.c",
    "ui-prefs.c",
    "ui-score.c",
    "ui-signals.c",
    "ui-spell.c",
    "ui-spoil.c",
    "ui-store.c",
    "ui-target.c",
    "ui-term.c",
    "ui-visuals.c",
    "ui-wizard.c",
    "wiz-debug.c",
    "wiz-spoil.c",
    "z-bitflag.c",
    "z-color.c",
    "z-dice.c",
    "z-expression.c",
    "z-file.c",
    "z-form.c",
    "z-quark.c",
    "z-queue.c",
    "z-rand.c",
    "z-textblock.c",
    "z-type.c",
    "z-util.c",
    "z-virt.c",
]

# コアごとの Windows リンク時ライブラリ（開発ビルドの build_windows.py と同じ）。
LINK_LIBS = {
    'angband': ['-ladvapi32', '-lshell32', '-luser32', '-lshlwapi', '-lws2_32'],
    'rogue36': ['-ladvapi32', '-lshell32', '-luser32', '-lshlwapi', '-lws2_32'],
    'rogue54': ['-ladvapi32', '-lshell32', '-luser32', '-lshlwapi', '-lws2_32'],
    'rogue2': ['-lws2_32'],
    'umoria': ['-ladvapi32', '-lshell32', '-luser32', '-lshlwapi', '-lws2_32'],
}
DEFAULT_LINK_LIBS = ['-lwinmm', '-lgdi32', '-lgdiplus', '-lcomdlg32', '-lws2_32', '-ldbghelp']


def windows_recipe(core, source, generated=None):
    """generated: 原作から作る生成ヘッダの `presentation` フォルダー（変愚・短愚だけ使う）。
    既定は再構成先の `generated/presentation`（キットでは ZIP から再構成されて `{source}/generated/...` になる）。"""
    if core == 'rogue2':
        patched=source/'rogue2/patched-src'
        adapter=ROOT/'rogue2/adapter';compat=adapter/'compat'
        include=[compat,adapter,patched]
        cflags=['--target=i686-w64-windows-gnu','-std=gnu11','-O1','-w','-DJAPAN','-DTOPSCO','-D_CRT_SECURE_NO_WARNINGS',
                *[('-I'+str(x)) for x in include],'-include','r2_build_config.h']
        upstream=['display.c','hit.c','init.c','invent.c','level.c','machdep.c','main.c','message.c','monster.c','move.c',
                  'object.c','pack.c','play.c','random.c','ring.c','room.c','save.c','score.c','spechit.c','throw.c','trap.c','use.c','zap.c']
        units=[]
        for name in upstream:
            extra=['-Dmain=r2_game_main'] if name=='main.c' else []
            units.append((patched/name,cflags+extra+['-x','c']))
        for name in ['r2_curses.c','r2_compat_iconv.c','r2_persist.c','r2_shim.c']:
            units.append((adapter/name,cflags+['-x','c']))
        cppflags=['--target=i686-w64-windows-gnu','-std=c++20','-O1','-w','-D_CRT_SECURE_NO_WARNINGS',
                  *[('-I'+str(x)) for x in [adapter,compat,patched,ROOT/'presentation',ROOT/'platform',ROOT/'third_party/cpp/include']],
                  '-x','c++']
        for file in [adapter/'r2_main.cpp',adapter/'r2_frame.cpp',ROOT/'presentation/frame/frame_codec.cpp',
                     ROOT/'presentation/frame/protocol_messages.cpp',ROOT/'presentation/frame/protocol_transport.cpp',
                     ROOT/'presentation/bridge/input_event_adapter.cpp',ROOT/'platform/portable/legacy_os.cpp']:
            units.append((file,cppflags))
        return units
    if core in ('rogue36','rogue54'):
        # tools/rogue_legacy/build_windows.py と同じ一覧・旗（Windows 開発ビルドと揃える）
        if core=='rogue36':generation,entry,name,version,config,files=36,'r36_game_main','Rogue 3.6','3.6.4','r36_build_config.h',ROGUE36_C
        else:generation,entry,name,version,config,files=54,'r54_game_main','Rogue 5.4','5.4.4','r54_build_config.h',ROGUE54_C
        src=source/core/'patched-src';adapter=ROOT/'rogue_legacy/adapter';compat=adapter/'compat'
        cflags=['--target=i686-w64-windows-gnu','-std=gnu11','-O2','-w','-DRL_GENERATION='+str(generation),
                '-include',str(compat/config),'-I'+str(compat),'-I'+str(adapter),'-I'+str(src),'-x','c']
        units=[]
        for f in files:units.append((src/f,cflags+(['-Dmain='+entry] if f=='main.c' else [])))
        for f in ['rl_curses.c','rl_i18n.c','rl_exit.c','rl_inventory.c','rl_sound.c']:units.append((adapter/f,cflags))
        cppflags=['--target=i686-w64-windows-gnu','-std=c++20','-O2','-DRL_GENERATION='+str(generation),
                  '-DRL_CORE_NAME="'+name+'"','-DRL_CORE_VERSION="'+version+'"','-DRL_GAME_MAIN='+entry,
                  '-I'+str(adapter),'-I'+str(compat),'-I'+str(src),'-I'+str(ROOT/'third_party/cpp/include'),
                  '-I'+str(ROOT/'presentation'),'-I'+str(ROOT/'platform'),'-x','c++']
        for f in [adapter/'rl_main.cpp',adapter/'rl_frame.cpp',ROOT/'presentation/frame/frame_codec.cpp',
                  ROOT/'presentation/frame/protocol_messages.cpp',ROOT/'presentation/frame/protocol_transport.cpp',
                  ROOT/'presentation/bridge/input_event_adapter.cpp',ROOT/'platform/portable/legacy_os.cpp']:
            units.append((f,cppflags))
        return units
    if core == 'angband':
        # tools/angband/build_windows.py と同じ一覧・旗
        src=source/'angband/patched-src';adapter=ROOT/'angband/adapter'
        core_flags=['--target=i686-w64-windows-gnu','-std=gnu11','-O2','-w','-I'+str(src),'-x','c']
        adapter_flags=['--target=i686-w64-windows-gnu','-std=gnu11','-O2','-I'+str(src),'-I'+str(adapter),
                       '-I'+str(ROOT/'platform'),'-I'+str(ROOT/'presentation'),'-x','c']
        cppflags=['--target=i686-w64-windows-gnu','-std=c++20','-O2','-I'+str(adapter),'-I'+str(ROOT/'third_party/cpp/include'),
                  '-I'+str(ROOT/'presentation'),'-I'+str(ROOT/'platform'),'-x','c++']
        units=[(src/f,core_flags) for f in ANGBAND_CORE_C]
        units+=[(adapter/f,adapter_flags) for f in ANGBAND_ADAPTER_C]
        for f in [adapter/'an_main.cpp',adapter/'an_frame.cpp',adapter/'an_manifest.cpp',adapter/'an_lang.cpp',
                  ROOT/'presentation/frame/frame_codec.cpp',ROOT/'presentation/frame/protocol_messages.cpp',
                  ROOT/'presentation/frame/protocol_transport.cpp',ROOT/'presentation/bridge/input_event_adapter.cpp',
                  ROOT/'presentation/bridge/native_targeting_runtime.cpp',ROOT/'platform/portable/legacy_os.cpp']:
            units.append((f,cppflags))
        return units
    if core == 'umoria':
        # tools/umoria/build_windows.py と同じ一覧・旗（原作の .cpp のうち curses の端末 ui_io.cpp を除く全部）
        src=source/'umoria/patched-src';adapter=ROOT/'umoria/adapter'
        core_flags=['--target=i686-w64-windows-gnu','-std=c++14','-O2','-w','-I'+str(src),'-x','c++']
        cppflags=['--target=i686-w64-windows-gnu','-std=c++20','-O2','-I'+str(src),'-I'+str(adapter),
                  '-I'+str(ROOT/'third_party/cpp/include'),'-I'+str(ROOT/'presentation'),'-I'+str(ROOT/'platform'),'-x','c++']
        units=[(f,core_flags+(['-Dmain=um_game_main'] if f.name=='main.cpp' else []))
               for f in sorted(src.glob('*.cpp')) if f.name!='ui_io.cpp']
        for f in [adapter/'um_io.cpp',adapter/'um_main.cpp',adapter/'um_frame.cpp',adapter/'um_town.cpp',adapter/'um_lang.cpp',adapter/'um_recall_ja.cpp',
                  ROOT/'presentation/frame/frame_codec.cpp',ROOT/'presentation/frame/protocol_messages.cpp',
                  ROOT/'presentation/frame/protocol_transport.cpp',ROOT/'presentation/bridge/input_event_adapter.cpp',
                  ROOT/'platform/portable/legacy_os.cpp']:
            units.append((f,cppflags))
        return units
    projects={'hengband':'HengbandCore','tangband':'TangbandCore','gensoband':'GensobandCore','silq':'SilCore','frox':'FroxCore'}
    project=ROOT/'VisualStudio'/projects[core]/(projects[core]+'.vcxproj')
    tree=ET.parse(project); ns={'m':'http://schemas.microsoft.com/developer/msbuild/2003'}
    # 短愚蛮怒は変愚 3.0.2.4 の写し（tangband/src）を自分のソースとして組む。
    heng_src=source/('tangband/src' if core=='tangband' else 'src')
    roots={'HengbandCoreSourceRoot':source,'HengTargetingSourceRoot':heng_src,'HengTargetingTargetRoot':heng_src/'target',
           'FroxTargetingSourceRoot':source/'frox/src','FroxTargetingInputSource':source/'frox/src/xtra2.c',
           'GensoTargetingSourceRoot':source/'gensoband/src','SilTargetingSourceRoot':source/'silq/src'}
    def path(value):
        for name,root in roots.items():value=value.replace('$('+name+')',str(root))
        value=value.replace('\\','/')
        return str((project.parent/value).resolve())
    defs=next(n for n in tree.findall('m:ItemDefinitionGroup',ns) if 'Release' in n.get('Condition',''))
    definitions=defs.find('m:ClCompile/m:PreprocessorDefinitions',ns).text.split(';')
    include=defs.find('m:ClCompile/m:AdditionalIncludeDirectories',ns).text.split(';')
    include=[path(x) for x in include if '$' not in x or any(x.startswith('$('+name+')') for name in roots)]
    if core in ('hengband','tangband'):include.append(str(generated if generated else source/'generated'/'presentation'))
    flags=['--target=i686-w64-windows-gnu','-w','-O1','-fms-extensions','-fcommon','-Wno-implicit-int','-Wno-implicit-function-declaration','-Wno-incompatible-pointer-types','-Wno-int-conversion']
    flags += ['-D'+x for x in definitions if '%' not in x]
    flags += ['-I'+x for x in include]
    units=[]
    for item in tree.findall('m:ItemGroup/m:ClCompile',ns):
        if any(x.text=='true' and ('Release' in x.get('Condition','Release')) for x in item.findall('m:ExcludedFromBuild',ns)): continue
        file=Path(path(item.get('Include')))
        lang='c++' if core in ('hengband','tangband') or file.suffix=='.cpp' else 'c'
        extra=['-x',lang]+(['-std=c++20'] if lang=='c++' else [])
        if core in ('hengband','tangband'): extra+=['-include',str(heng_src/'stdafx.h')]
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
    generated=None
    if a.core in ('hengband','tangband'):
        # 生成ヘッダはリポジトリの外へ作る（tools/core_sources/generate_modern_*.py）。
        gen_root=work/'generated'
        for script in ('generate_modern_item_details.py','generate_modern_artifact_lore.py'):
            subprocess.run([sys.executable,str(ROOT/'tools/core_sources'/script),'--source-root',str(source),'--output-root',str(gen_root),'--variant',a.core],check=True)
        generated=gen_root/('tangband/presentation' if a.core=='tangband' else 'presentation')
    units=windows_recipe(a.core,source,generated)
    tool=next((ROOT/'scratch_old/core-import/windows-toolchain').glob('*/bin/clang++.exe'))
    if a.core in ('gensoband','silq','frox'):
        for folder,encoding in [(source/'gensoband/src','cp932'),(ROOT/(a.core+'/adapter'),'utf8')]:
            if a.core!='gensoband' and folder.name=='src': continue
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
    args=[str(tool),'--target=i686-w64-windows-gnu','-static',*[str(x[1]) for x in result],*LINK_LIBS.get(a.core,DEFAULT_LINK_LIBS),'-o',str(work/(a.core+'.exe'))]
    rsp=work/(a.core+'-link.rsp');rsp.write_text('\n'.join(chr(34)+x.replace('\\','/')+chr(34) for x in args[1:]),encoding='utf-8');r=subprocess.run([args[0],'@'+str(rsp)],capture_output=True);print(r.stderr.decode('utf-8','replace'));raise SystemExit(r.returncode)

if __name__=='__main__':main()
