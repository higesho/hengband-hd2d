"""UI の原作ソース除去と、再構成したソースのハッシュを検査する。"""
import subprocess
import xml.etree.ElementTree as ET
from paths import prepared_root
from prepare import UI_ROOT, DEFAULT_UPSTREAM, prepare

ORIGINAL_TREES = ('src', 'gensoband/src', 'silq/src', 'frox/src', 'hengband-3.0.2.4-Beta')


def main():
    for path in ORIGINAL_TREES:
        if (UI_ROOT / path).exists() or (UI_ROOT / path).is_symlink():
            raise RuntimeError('Original source still exists inside UI: ' + path)
    tracked = subprocess.check_output(['git', '-C', str(UI_ROOT), '-c', 'safe.directory=' + UI_ROOT.as_posix(),
                                       'ls-files', '-z', '--', *ORIGINAL_TREES])
    if tracked:
        raise RuntimeError('Original source is still tracked by the UI repository')
    project = ET.parse(UI_ROOT / 'VisualStudio/HengbandHd2d/HengbandHd2d.vcxproj')
    dependencies = []
    for node in project.iter():
        kind = node.tag.rsplit('}', 1)[-1]
        if kind == 'AdditionalIncludeDirectories':
            dependencies.append(node.text or '')
        elif kind in ('ClCompile', 'ClInclude', 'ResourceCompile'):
            dependencies.append(node.attrib.get('Include', ''))
    if any('..\\..\\src' in value or 'HengbandCoreSourceRoot' in value for value in dependencies):
        raise RuntimeError('UI project still refers to core sources')
    prepare(DEFAULT_UPSTREAM, prepared_root(), verify_only=True)
    print('PASS: no original source trees in UI; external prepared source hashes match the baseline')


if __name__ == '__main__':
    main()
