"""固定した外部原作と接続用パッチから、UI の外にコアの作業用ソースを構成する。"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
from contextlib import contextmanager
import uuid

HERE = Path(__file__).resolve().parent
UI_ROOT = HERE.parent.parent
DEFAULT_UPSTREAM = UI_ROOT.parent / 'roguelike-cores' / 'upstream'
DEFAULT_OUTPUT = UI_ROOT.parent / 'roguelike-cores' / 'build-sources'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def git(repo, *args, **kwargs):
    return subprocess.run(['git', '-C', str(repo), '-c', 'safe.directory=' + repo.as_posix(),
                           '-c', 'core.autocrlf=false', *args], check=True, **kwargs)


def transform(data: bytes, spec: dict) -> bytes:
    if spec['newline'] != 'keep':
        data = data.replace(b'\r\n', b'\n')
        if spec['newline'] == 'crlf':
            data = data.replace(b'\n', b'\r\n')
    if spec['bom']:
        if not data.startswith(b'\xef\xbb\xbf'):
            data = b'\xef\xbb\xbf' + data
    elif data.startswith(b'\xef\xbb\xbf'):
        data = data[3:]
    return data


def export_base(repo: Path, source: dict, dest: Path):
    """作業ツリーではなく固定コミットの blob を使う。原作のローカル編集を混ぜない。"""
    expected = source['files']
    proc = subprocess.Popen(['git', '-C', str(repo), '-c', 'safe.directory=' + repo.as_posix(),
                             'archive', '--format=tar', source['commit'], 'src'], stdout=subprocess.PIPE)
    found = set()
    try:
        with tarfile.open(fileobj=proc.stdout, mode='r|') as archive:
            for member in archive:
                if not member.isfile() or not member.name.startswith('src/'):
                    continue
                name = member.name[4:]
                spec = expected.get(name)
                if spec is None or not spec['upstream']:
                    continue
                target = dest / 'src' / name
                if not target.resolve().is_relative_to(dest.resolve()):
                    raise ValueError('Invalid upstream path: ' + name)
                raw = archive.extractfile(member).read()
                if digest(raw) != spec['upstream_sha256']:
                    raise ValueError('Unexpected upstream content: ' + name)
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(transform(raw, spec))
                found.add(name)
        if proc.wait() != 0:
            raise RuntimeError('Cannot export ' + source['name'])
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait()
    missing = {name for name, spec in expected.items() if spec['upstream']} - found
    if missing:
        raise ValueError('Missing upstream files: ' + ', '.join(sorted(missing)[:5]))


def verify_core(folder: Path, source: dict):
    expected = source['files']
    actual = {p.relative_to(folder).as_posix(): p for p in folder.rglob('*') if p.is_file()}
    if actual.keys() != expected.keys():
        raise ValueError('File list mismatch: ' + source['name'])
    bad = [name for name, path in actual.items() if digest(path.read_bytes()) != expected[name]['sha256']]
    if bad:
        raise ValueError('Source hash mismatch: ' + ', '.join(bad[:5]))


def manifest_fingerprint(manifest: dict):
    data = json.dumps(manifest, sort_keys=True).encode()
    for source in manifest['sources']:
        patch = HERE / source['patch']
        raw = patch.read_bytes()
        if digest(raw) != source['patch_sha256']:
            raise ValueError('Patch hash mismatch: ' + str(patch))
        data += raw
    return digest(data)


@contextmanager
def staging_directory(parent: Path):
    # tempfile.mkdtemp の Windows の非継承 ACL を完成品へ持ち込まない。
    # 最終配置先と同じ親の通常の継承権限で作成する。
    stage = parent / ('.core-prepare-' + uuid.uuid4().hex)
    stage.mkdir()
    try:
        yield stage
    finally:
        if stage.exists():
            if stage.parent.resolve() != parent.resolve() or not stage.name.startswith('.core-prepare-'):
                raise ValueError('Unexpected staging cleanup path')
            shutil.rmtree(stage)


def prepare(upstream: Path, output: Path, verify_only=False):
    upstream, output = upstream.resolve(), output.resolve()
    if output == upstream or output.is_relative_to(upstream) or output.is_relative_to(UI_ROOT):
        raise ValueError('Prepared sources must be outside the UI and upstream source directories')
    manifest = json.loads((HERE / 'manifest.json').read_text(encoding='utf-8'))
    fingerprint = manifest_fingerprint(manifest)
    marker = output / '.prepared.json'
    if marker.is_file():
        previous = json.loads(marker.read_text(encoding='utf-8'))
        if previous.get('fingerprint') == fingerprint:
            for source in manifest['sources']:
                verify_core(output / source['output'], source)
            print('Prepared sources verified:', output)
            return
    if verify_only:
        raise ValueError('No matching prepared sources: ' + str(output))
    if output.exists():
        # 未知のディレクトリや利用者の編集を消さない。更新は新しい出力先へ作る。
        raise ValueError('Output already exists; choose a new empty output path: ' + str(output))
    output.parent.mkdir(parents=True, exist_ok=True)
    with staging_directory(output.parent) as temp:
        temp = Path(temp)
        result = temp / 'result'
        result.mkdir()
        for source in manifest['sources']:
            repo = upstream / source['upstream_repo']
            work = temp / source['name']
            work.mkdir()
            export_base(repo, source, work)
            git(work, 'init', '--quiet', '--initial-branch=prepared')
            patch = HERE / source['patch']
            if patch.stat().st_size:
                git(work, 'apply', '--check', '--binary', '--whitespace=nowarn', str(patch))
                git(work, 'apply', '--binary', '--whitespace=nowarn', str(patch))
            verify_core(work / 'src', source)
            target = result / source['output']
            target.parent.mkdir(parents=True, exist_ok=True)
            (work / 'src').rename(target)
            print(source['name'] + ': ' + str(len(source['files'])) + ' files match the baseline')
        (result / '.prepared.json').write_text(json.dumps({'fingerprint': fingerprint,
            'baseline': manifest['baseline']}, indent=2) + '\n', encoding='utf-8')
        result.rename(output)
    print('Prepared sources:', output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--upstream', type=Path, default=Path(os.environ.get('HENGBAND_UPSTREAM_ROOT', DEFAULT_UPSTREAM)))
    parser.add_argument('--output', type=Path, default=Path(os.environ.get('HENGBAND_CORE_SOURCE_ROOT', DEFAULT_OUTPUT)))
    parser.add_argument('--verify', action='store_true')
    args = parser.parse_args()
    prepare(args.upstream, args.output, args.verify)


if __name__ == '__main__':
    main()
