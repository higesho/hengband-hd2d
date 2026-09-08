"""Keep protocol differences visible while ignoring masked installation paths."""
import json
import unittest
from playthrough import comparable_observation

def row(size,core='hengband',path='<パス>',kind='hello_ack'):
    payload=json.dumps({'core_name':core,'asset_roots':{'graf':path}},ensure_ascii=False)
    return '  '+json.dumps({'dir':'in','len':size,'payload':payload,'t':kind},ensure_ascii=False)+'\n'

class ObservationTests(unittest.TestCase):
    def test_installation_path_length(self):
        self.assertEqual(comparable_observation(row(183)),comparable_observation(row(178)))
    def test_core_identity_is_still_compared(self):
        self.assertNotEqual(comparable_observation(row(183)),comparable_observation(row(178,core='silq')))
    def test_unmasked_length_is_preserved(self):
        self.assertNotEqual(comparable_observation(row(183,path='graf')),comparable_observation(row(178,path='graf')))
    def test_other_messages_keep_length(self):
        self.assertNotEqual(comparable_observation(row(183,kind='notice')),comparable_observation(row(178,kind='notice')))
    def test_missing_asset_declaration_is_visible(self):
        missing='  '+json.dumps({'dir':'in','payload':'{}','t':'hello_ack'})+'\n'
        self.assertNotEqual(comparable_observation(row(183)),comparable_observation(missing))

if __name__=='__main__':unittest.main()
