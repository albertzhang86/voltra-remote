"""Diagnostic patch must remain version pinned, atomic and repeatable."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('trace_patch',ROOT/'tools/patch_nimble_trace.py')
patch=importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)

class AttTracePatchTest(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.library=Path(self.temp.name)
        (self.library/'library.properties').write_text('version=2.5.1\n')
        for name,replacements in patch.PATCHES.items():
            path=self.library/patch.HOST/name
            path.parent.mkdir(parents=True,exist_ok=True)
            path.write_text('#include "ble_hs_priv.h"\n'+'\n'.join(before for before,_ in replacements))
    def snapshot(self):
        return {str(p.relative_to(self.library)):p.read_bytes() for p in self.library.rglob('*') if p.is_file()}
    def test_idempotence(self):
        patch.apply_patch(self.library)
        once=self.snapshot()
        patch.apply_patch(self.library)
        self.assertEqual(once,self.snapshot())
    def test_unknown_source_does_not_partially_patch(self):
        (self.library/patch.HOST/'ble_hs_hci.c').write_text('unknown upstream source')
        before=self.snapshot()
        with self.assertRaises(ValueError):patch.apply_patch(self.library)
        self.assertEqual(before,self.snapshot())
    def test_version_guard(self):
        (self.library/'library.properties').write_text('version=2.5.2\n')
        before=self.snapshot()
        with self.assertRaises(ValueError):patch.apply_patch(self.library)
        self.assertEqual(before,self.snapshot())

if __name__=='__main__':unittest.main()
