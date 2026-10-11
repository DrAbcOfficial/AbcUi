import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from PIL import Image

spec = importlib.util.spec_from_file_location('converter', Path(__file__).resolve().parents[1] / 'convert_tga_to_png.py')
converter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(converter)


class ConversionTests(unittest.TestCase):
    def test_existing_png_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'image.tga'
            Image.new('RGBA', (2, 2), (1, 2, 3, 4)).save(source)
            target = source.with_suffix('.png')
            target.write_bytes(b'owned by user')
            with self.assertRaises(FileExistsError):
                converter.convert(source)
            self.assertEqual(target.read_bytes(), b'owned by user')
            self.assertTrue(source.exists())

    def test_pixels_verified_and_source_kept(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'image.tga'
            Image.new('RGBA', (2, 2), (1, 2, 3, 4)).save(source)
            converter.convert(source)
            with Image.open(source.with_suffix('.png')) as image:
                self.assertEqual(image.getpixel((0, 0)), (1, 2, 3, 4))
            self.assertTrue(source.exists())
            self.assertFalse(list(Path(directory).glob('*.tmp')))

    def test_batch_failure_keeps_all_sources(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            Image.new('RGB', (2, 2)).save(root / 'good.tga')
            (root / 'bad.tga').write_bytes(b'not an image')
            with patch.object(converter, 'ASSET_ROOT', root), patch('sys.argv', ['convert']):
                self.assertEqual(converter.main(), 1)
            self.assertTrue((root / 'good.tga').exists())
            self.assertTrue((root / 'bad.tga').exists())

    def test_all_failed_has_no_division_by_zero(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'bad.tga').write_bytes(b'not an image')
            with patch.object(converter, 'ASSET_ROOT', root), patch('sys.argv', ['convert']):
                self.assertEqual(converter.main(), 1)


if __name__ == '__main__':
    unittest.main()
