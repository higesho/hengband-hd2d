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
    return root / ('src' if name == 'hengband' else name + '/src')


def generated_root(source_root=None):
    """原作から作る生成ヘッダ（リポジトリの外）。`<再構成先>-generated/presentation/bridge/` に置く。

    インクルードパスには `<generated_root>/presentation` を足す（`#include "bridge/..."` が解決する）。
    """
    env = os.environ.get('HENGBAND_CORE_GENERATED_ROOT')
    if env:
        return Path(env)
    root = Path(source_root) if source_root else prepared_root()
    return root.with_name(root.name + '-generated')


def write_if_changed(path, text, encoding='utf-8-sig'):
    """内容が同じなら書かない（並列ビルドで同じファイルを奪い合わないため）。"""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    data = text.encode(encoding)
    if path.is_file() and path.read_bytes() == data:
        return False
    tmp = path.with_name(path.name + f'.{os.getpid()}.tmp')
    tmp.write_bytes(data)
    os.replace(tmp, path)
    return True
