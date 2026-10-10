"""Migration archive integrity and path-boundary regressions."""
import argparse
import pathlib
import struct
import sys
import tempfile
import unittest
import zlib
from unittest.mock import patch

sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
from migrate_client import archive_entries, extract


def archive(path,name='figures/body.nif',content=b'original mesh'):
    packed=zlib.compress(content)
    entry=bytearray(284)
    entry[:len(name)]=name.encode()
    entry[268:284]=struct.pack('<4I',len(content),0,len(packed),zlib.crc32(packed))
    directory=zlib.compress(entry)
    fields=struct.pack('<4I',zlib.crc32(directory),len(directory),0,1)
    path.write_bytes(b'MPAK\x00'+bytes(v^i for i,v in enumerate(fields))+directory+packed)


class ArchiveTests(unittest.TestCase):
    def test_offsets_and_npk_format(self):
        with tempfile.TemporaryDirectory() as temporary:
            path=pathlib.Path(temporary)/'mesh.npk'
            archive(path)
            record=archive_entries(path)[0]
            self.assertEqual(record['name'],'figures/body.nif')
            self.assertEqual(zlib.decompress(path.read_bytes()[record['offset']:]),b'original mesh')

    def test_unsafe_paths_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            path=pathlib.Path(temporary)/'mesh.mpk'
            for name in ('../outside.nif','/outside.nif','C:/outside.nif','figures/../../outside.nif'):
                archive(path,name)
                with self.assertRaisesRegex(ValueError,'Unsafe'):archive_entries(path)

    def test_directory_crc(self):
        with tempfile.TemporaryDirectory() as temporary:
            path=pathlib.Path(temporary)/'mesh.mpk'
            archive(path)
            data=bytearray(path.read_bytes());data[21]^=1;path.write_bytes(data)
            with self.assertRaisesRegex(ValueError,'CRC'):archive_entries(path)

    def test_real_loose_asset_wins_and_resume(self):
        with tempfile.TemporaryDirectory() as temporary,patch('migrate_client.reserve'):
            base=pathlib.Path(temporary);client=base/'source';client.mkdir();output=base/'cache';output.mkdir()
            archive(client/'assets.mpk')
            loose=client/'figures/body.nif';loose.parent.mkdir();loose.write_bytes(b'loose real mesh')
            args=argparse.Namespace(client=client,output=output)
            extract(args);extract(args)
            self.assertEqual((output/'client/figures/body.nif').read_bytes(),b'loose real mesh')
            self.assertEqual(loose.read_bytes(),b'loose real mesh')


if __name__=='__main__':unittest.main()
