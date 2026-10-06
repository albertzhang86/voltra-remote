"""The model binary is mandatory when the partition table declares it."""
import importlib.util,tempfile,unittest
from pathlib import Path
spec=importlib.util.spec_from_file_location('firmware_parts',Path(__file__).resolve().parents[1]/'tools/firmware_parts.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class FirmwarePartsTests(unittest.TestCase):
 def test_model_validation(self):
  with tempfile.TemporaryDirectory() as folder:
   build=Path(folder)
   for name in ['VoltraKnob.ino.bootloader.bin','VoltraKnob.ino.partitions.bin','boot_app0.bin','VoltraKnob.ino.bin']:(build/name).write_bytes(b'firmware')
   (build/'partitions.csv').write_text('# Name,Type,SubType,Offset,Size\nmodel,data,spiffs,0xB00000,0x400000\n')
   with self.assertRaises(ValueError):m.firmware_parts(build)
   (build/'srmodels.bin').write_bytes(b'model')
   self.assertEqual(m.firmware_parts(build)[-1],('0xb00000','srmodels.bin'))
   (build/'partitions.csv').write_text('model,data,spiffs,0xB00000,0x4\n')
   with self.assertRaises(ValueError):m.firmware_parts(build)
   (build/'partitions.csv').write_text('# no speech partition\n')
   self.assertEqual(len(m.firmware_parts(build)),4)
if __name__=='__main__':unittest.main()
