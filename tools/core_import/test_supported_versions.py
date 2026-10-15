"""対応バージョン定義が複数バージョンを保持できることを確認する。"""
from make_kit import validate_supported_versions


def target(id, version):
    return {
        'id': id,
        'core': 'sample',
        'install_id': 'sample',
        'name': 'Sample',
        'version': version,
        'output': 'SampleCore.exe',
        'repo': 'sample',
        'commit': '0' * 40,
    }


def expect_error(data, text):
    try:
        validate_supported_versions(data)
    except ValueError as exc:
        assert text in str(exc), str(exc)
    else:
        raise AssertionError('Expected validation error: ' + text)


def main():
    data={'schema':1,'targets':[target('sample','1.0'),target('sample-1.1','1.1')]}
    result=validate_supported_versions(data)
    assert [x['version'] for x in result]==['1.0','1.1']
    assert result[0]['install_id']==result[1]['install_id']=='sample'
    print('PASS: multiple versions of one core can coexist in the import definitions')

    expect_error({'schema':1,'targets':[target('sample','1.0'),target('sample','1.1')]},'Duplicate import target ID')
    expect_error({'schema':1,'targets':[target('sample','1.0'),target('sample-copy','1.0')]},'Duplicate supported core version')
    print('PASS: duplicate target IDs and duplicate core versions are rejected')


if __name__=='__main__':main()
