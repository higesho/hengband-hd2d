"""外部の編集済みソースからパッチと検証ハッシュを作る。--record なしでは保存しない。"""
import argparse
import copy
import io
import json
import os
from pathlib import Path
import subprocess
import tarfile
import tempfile

from prepare import HERE, UI_ROOT, DEFAULT_UPSTREAM, digest, transform


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', required=True, type=Path)
    parser.add_argument('--upstream', type=Path, default=Path(os.environ.get('HENGBAND_UPSTREAM_ROOT', DEFAULT_UPSTREAM)))
    parser.add_argument('--core', choices=('hengband', 'gensoband', 'silq', 'frox'))
    parser.add_argument('--record', action='store_true')
    args = parser.parse_args()
    current = args.source_root.resolve()
    if current.is_relative_to(UI_ROOT.resolve()) or current.is_relative_to(args.upstream.resolve()):
        raise ValueError('Use an external working copy, not the UI or pristine upstream source')
    manifest = json.loads((HERE / 'manifest.json').read_text(encoding='utf-8'))
    updated = copy.deepcopy(manifest)
    patches = {}
    for source in updated['sources']:
        if args.core and source['name'] != args.core:
            continue
        folder = current / source['output']
        if not folder.is_dir():
            raise FileNotFoundError(folder)
        originals = {}
        repo = args.upstream / source['upstream_repo']
        raw = subprocess.check_output(['git', '-C', str(repo), '-c', 'safe.directory=' + repo.as_posix(),
                                       'archive', '--format=tar', source['commit'], 'src'])
        with tarfile.open(fileobj=io.BytesIO(raw), mode='r:') as archive:
            for member in archive:
                if member.isfile():
                    originals[member.name[4:]] = archive.extractfile(member).read()
        files = {}
        with tempfile.TemporaryDirectory(prefix='core-patch-') as temp:
            temp = Path(temp)
            for p in sorted(folder.rglob('*')):
                if not p.is_file():
                    continue
                if p.is_symlink() or p.suffix.lower() in ('.obj', '.o', '.exe', '.dll', '.so', '.pdb'):
                    raise ValueError('Unexpected build output or link in source: ' + str(p))
                name = p.relative_to(folder).as_posix()
                raw = p.read_bytes()
                newline = 'keep' if b'\0' in raw else ('lf' if b'\r\n' not in raw else
                    ('crlf' if raw.count(b'\r\n') == raw.count(b'\n') else 'keep'))
                spec = {'sha256': digest(raw), 'upstream': name in originals,
                        'newline': newline, 'bom': raw.startswith(b'\xef\xbb\xbf')}
                target = temp / 'target/src' / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(raw)
                if name in originals:
                    spec['upstream_sha256'] = digest(originals[name])
                    target = temp / 'base/src' / name
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(transform(originals[name], spec))
                files[name] = spec
            (temp / 'base/src').mkdir(parents=True, exist_ok=True)
            (temp / 'target/src').mkdir(parents=True, exist_ok=True)
            diff = subprocess.run(['git', '-c', 'core.autocrlf=false', 'diff', '--no-index', '--binary',
                                   '--no-renames', '--', 'base', 'target'], cwd=temp, capture_output=True)
            if diff.returncode not in (0, 1):
                raise RuntimeError(diff.stderr)
            patch = diff.stdout.replace(b'a/base/', b'a/').replace(b'b/target/', b'b/')
        patch_hash = digest(patch)
        changed = source['files'] != files or source['patch_sha256'] != patch_hash
        print(source['name'], 'files=', len(files), 'patch_bytes=', len(patch), 'changed=', changed)
        source['files'], source['patch_sha256'] = files, patch_hash
        patches[source['patch']] = patch
    if args.record and updated != manifest:
        updated['baseline'] = subprocess.check_output(['git', '-C', str(UI_ROOT), '-c',
            'safe.directory=' + UI_ROOT.as_posix(), 'rev-parse', 'HEAD']).decode().strip()
        for name, data in patches.items():
            target = HERE / name
            if not target.resolve().is_relative_to(HERE):
                raise ValueError('Invalid patch path')
            target.write_bytes(data)
        (HERE / 'manifest.json').write_text(json.dumps(updated, ensure_ascii=False, indent=2) + '\n', encoding='utf-8', newline='\n')
        print('Recorded. Prepare a new output directory and run the build and regression checks.')


if __name__ == '__main__':
    main()
