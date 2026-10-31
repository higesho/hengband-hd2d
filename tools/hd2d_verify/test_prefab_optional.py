"""規則の行の `"optional": true`（`hd2d/world/prefab_library.cpp` の `PrefabLibrary::load`）を、実行体の `--material-check` で確かめる。

`--material-check` は GL を使わず、プレハブライブラリを読んで個数を出す（読めなければ終了コード 1）。
ここでは本物の素材（`assets/voxel`）から数個だけ写した小さなプレハブの置き場と、それを指す 1 行ずつの対応表を一時フォルダへ作り、
`--voxel-dir=` で指して次を確かめる（作った一時フォルダだけを触る。開発ツリーの `assets/` は読むだけ）。

  * 印のある行が指すプレハブが無ければ、その行を飛ばして読み込みは成功する。標準エラーに省いた行の数と最初の名前が 1 回出る
  * 印のある行でも、指すプレハブが全部あれば省かない（何も出さない）
  * 印のある行が複数のプレハブを指し、その 1 つだけが無ければ、行ごと飛ばす（残りのプレハブは読む）
  * 印の無い行（`"optional": false` を含む）は、プレハブが無ければ今までどおり読み込み全体が失敗する
  * 同じプレハブを印の無い行も指していれば、印のある行があっても必須のままで、失敗する

使い方（リポジトリ直下。`HengbandHd2d.exe` を先に組むこと）:

    python tools/hd2d_verify/test_prefab_optional.py
"""
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
EXE = ROOT / 'HengbandHd2d.exe'
SOURCE = ROOT / 'assets' / 'voxel'
#: 小さな置き場へ写す本物のプレハブ（3 種。`arch_stone` は `arch_stone_ew` と対）。
PREFABS = ('arch_stone', 'arch_stone_ew', 'stairs_down')
MISSING = 'zz_optional_test_missing'


class PrefabOptionalTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='hd2d-prefab-optional-')
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.voxel = self.root / 'voxel'
        self.voxel.mkdir()
        for name in PREFABS:
            for ext in ('.vox', '.jsonc'):
                shutil.copy2(SOURCE / (name + ext), self.voxel / (name + ext))

    def run_check(self, rows: str):
        """対応表の `terrains_extra` に `rows` を書き、`--material-check` を走らせる。(終了コード, 標準エラーと標準出力の字)。"""
        (self.voxel / 'terrain_prefabs.jsonc').write_text('{ "terrains_extra": [\n' + rows + '\n] }\n', encoding='utf-8', newline='\n')
        env = dict(os.environ, HENGBAND_MUTE='1', HENGBAND_DATA_ROOT=str(self.root / 'data'))
        proc = subprocess.run([str(EXE), '--material-check', '--voxel-dir=' + str(self.voxel)], cwd=str(ROOT), env=env,
                              stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
        return proc.returncode, proc.stdout.decode('utf-8', errors='replace')

    @staticmethod
    def loaded_count(text: str) -> int:
        found = re.search(r'material-check: プレハブ (\d+) 個', text)
        return int(found.group(1)) if found else -1

    def test_optional_row_with_missing_prefab_is_skipped(self):
        code, text = self.run_check(
            '{ "key": "T_OK", "id": 30001, "ground": ["stairs_down"] },\n'
            f'{{ "key": "T_OPT", "id": 30002, "ground": ["{MISSING}"], "optional": true }}')
        self.assertEqual(code, 0, text)
        self.assertEqual(self.loaded_count(text), 1, text)
        self.assertEqual(len(re.findall('optional の規則 1 行を省きました', text)), 1, text)
        self.assertIn('最初: T_OPT', text)
        self.assertIn(MISSING, text)

    def test_optional_row_with_all_prefabs_is_kept(self):
        code, text = self.run_check('{ "key": "T_OPT", "id": 30002, "ground": ["stairs_down"], "optional": true }')
        self.assertEqual(code, 0, text)
        self.assertEqual(self.loaded_count(text), 1, text)
        self.assertNotIn('省きました', text)

    def test_optional_row_loses_the_whole_row_when_one_prefab_is_missing(self):
        # `object` は在り、`object_ew` だけ無い。行ごと飛ばす（在るほうのプレハブは読むが、行は規則に載らない）。
        code, text = self.run_check(
            '{ "key": "T_DOOR", "id": 30003, "ground": ["stairs_down"], "object": ["arch_stone"], '
            f'"object_ew": ["{MISSING}"], "optional": true }},\n'
            '{ "key": "T_OK", "id": 30001, "ground": ["stairs_down"] }')
        self.assertEqual(code, 0, text)
        self.assertEqual(self.loaded_count(text), 2, text)       # stairs_down と arch_stone
        self.assertIn('optional の規則 1 行を省きました（最初: T_DOOR', text)

    def test_row_without_the_mark_still_fails(self):
        code, text = self.run_check(f'{{ "key": "T_BAD", "id": 30004, "ground": ["{MISSING}"] }}')
        self.assertEqual(code, 1, text)
        self.assertIn(f'対応表のプレハブ "{MISSING}" を読めませんでした', text)

    def test_optional_false_still_fails(self):
        code, text = self.run_check(f'{{ "key": "T_BAD", "id": 30004, "ground": ["{MISSING}"], "optional": false }}')
        self.assertEqual(code, 1, text)
        self.assertIn(f'対応表のプレハブ "{MISSING}" を読めませんでした', text)

    def test_prefab_also_named_by_a_required_row_stays_required(self):
        code, text = self.run_check(
            f'{{ "key": "T_OPT", "id": 30002, "ground": ["{MISSING}"], "optional": true }},\n'
            f'{{ "key": "T_BAD", "id": 30004, "ground": ["{MISSING}"] }}')
        self.assertEqual(code, 1, text)
        self.assertIn(f'対応表のプレハブ "{MISSING}" を読めませんでした', text)


if __name__ == '__main__':
    if not EXE.is_file():
        sys.exit('実行体が無い: %s（先に組むこと）' % EXE)
    unittest.main(verbosity=2)
