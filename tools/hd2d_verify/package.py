"""展開した Windows 配布物で 5 コアの操作・保存・読み込みを検査する。"""
import argparse
from contextlib import ExitStack
import importlib
from pathlib import Path
import shutil
import sys

from runtime import CfgGuard, SaveGuard
import playthrough
import test_keyboard_live


def main():
    sys.stdout.reconfigure(line_buffering=True)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package', type=Path)
    args = parser.parse_args()
    package = args.package.resolve()
    source = Path(playthrough.ROOT)
    if package == source or not (package / 'cores/HengbandCore.exe').is_file():
        parser.error('展開した配布フォルダーを指定してください')
    work = package / 'scratch_old/package-check'
    work.mkdir(parents=True, exist_ok=True)
    with ExitStack() as guards:
        guards.enter_context(CfgGuard(package))
        for core in ('gensoband', 'silq', 'frox', 'tangband'):
            for folder in ('save', 'user', 'apex'):
                guards.enter_context(SaveGuard(package / core / 'lib' / folder, None, None))
        # 検査の設定だけ供給する。配布元のデータや資産はコピーしない。
        for cfg in source.glob('*.cfg'):
            shutil.copy2(cfg, package / cfg.name)
        playthrough.ROOT = test_keyboard_live.ROOT = str(package)
        playthrough.EXE = test_keyboard_live.EXE = str(package / 'HengbandHd2d.exe')
        playthrough.CORE = str(package / 'cores/HengbandCore.exe')
        playthrough.SAVE_DIR = str(package / 'lib/save')
        playthrough.USER_DIR = str(package / 'lib/user')
        test_keyboard_live.main()
        for core, module, exe in (
            ('gensoband', 'gb_protocol_driver', 'GensobandCore.exe'),
            ('silq', 'sq_protocol_driver', 'SilCore.exe'),
            ('frox', 'fc_protocol_driver', 'FroxCore.exe'),
        ):
            sys.path.insert(0, str(source / 'tools' / core))
            driver = importlib.import_module(module)
            driver.REPO = package
            driver.CORE = package / 'cores' / exe
            driver.SAVE_DIR = package / core / 'lib/save'
            output = work / core
            output.mkdir(exist_ok=True)
            assert driver.cmd_m0(output) == 0, core
        sys.path.insert(0, str(source / 'tools/tangband'))
        import tb_smoke
        tb_smoke.REPO = tb_smoke.gb.REPO = package
        tb_smoke.gb.CORE = package / 'cores/TangbandCore.exe'
        tb_smoke.TB_SAVE = package / 'tangband/lib/save'
        tb_smoke.TB_DATA = package / 'tangband/lib/data'
        tb_smoke.HENG_DATA = package / 'lib/data'
        sys.argv = ['tb_smoke', str(work / 'tangband')]
        assert tb_smoke.main() == 0, 'tangband'
    print('PASS: packaged core checks completed (Hengband UI; legacy save/load; Tangband birth/save)', flush=True)


if __name__ == '__main__':
    main()
