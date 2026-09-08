"""APK の展開一覧・インポート SDK・レシピの内容を配布前に照合する。"""
import argparse
import hashlib
import json
import zipfile

def audit(path):
    with zipfile.ZipFile(path) as apk:
        manifest=apk.read('assets/assets.manifest').decode('utf-8').splitlines()
        for line in manifest[1:]:
            name,size=line.rsplit('\t',1)
            item=apk.getinfo('assets/'+name)
            if item.file_size!=int(size):raise ValueError('Asset size mismatch: '+name)
        catalog=json.loads(apk.read('assets/core-import/catalog.json'))
        for name,expected in catalog['sdk_sha256'].items():
            if hashlib.sha256(apk.read('assets/core-import/'+name)).hexdigest()!=expected:raise ValueError('SDK checksum mismatch: '+name)
        for item in catalog['targets'].values():
            if hashlib.sha256(apk.read('assets/core-import/'+item['recipe'])).hexdigest()!=item['sha256']:raise ValueError('Recipe checksum mismatch')
        print('PASS:',len(manifest)-1,'asset entries;',len(catalog['sdk_sha256']),'SDK files; all recipes match')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('apk');audit(parser.parse_args().apk)
