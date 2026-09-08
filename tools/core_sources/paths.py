"""コアの解析・翻訳用ツールが参照する、外部の作業用ソースの場所。"""
import os
from pathlib import Path


def prepared_root():
    ui = Path(__file__).resolve().parents[2]
    return Path(os.environ.get('HENGBAND_CORE_SOURCE_ROOT', ui.parent / 'roguelike-cores' / 'build-sources'))


def core_source(name='hengband'):
    root = prepared_root()
    if not (root / '.prepared.json').is_file():
        raise FileNotFoundError(f'Run tools/core_sources/prepare.py first: {root}')
    return root / ('src' if name in ('hengband', 'tangband') else name + '/src')
