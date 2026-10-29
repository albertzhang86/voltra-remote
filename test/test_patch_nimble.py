"""Patch integrity and deterministic execution of the real patched write path."""
import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "test/fixtures/nimble-2.5.1"
spec = importlib.util.spec_from_file_location("patch_nimble", ROOT / "tools/patch_nimble.py")
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)


def fixture_library(path):
    (path / "src").mkdir(parents=True)
    (path / "library.properties").write_text("name=NimBLE-Arduino\nversion=2.5.1\n")
    for name in ["NimBLERemoteValueAttribute.cpp", "NimBLERemoteValueAttribute.h"]:
        shutil.copyfile(FIXTURES / name, path / "src" / name)
    # Repeat-pairing guards are independent: their exact source anchors suffice
    # to check all-or-nothing application alongside the complete write sources.
    for name in ["src/NimBLEServer.cpp", "src/NimBLEServer.h"]:
        (path / name).write_text("\n".join(before for before, _ in patch.PATCHES[name]))


class NimblePatchTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.library = Path(self.temp.name) / "library"
        fixture_library(self.library)

    def snapshot(self):
        return {str(p.relative_to(self.library)): p.read_bytes() for p in self.library.rglob("*") if p.is_file()}

    def test_version_guard_changes_nothing(self):
        (self.library / "library.properties").write_text("version=2.5.2\n")
        before = self.snapshot()
        with self.assertRaisesRegex(ValueError, "Expected NimBLE-Arduino 2.5.1"):
            patch.apply_patch(self.library)
        self.assertEqual(before, self.snapshot())

    def test_unknown_source_is_atomic(self):
        file = self.library / "src/NimBLEServer.cpp"
        file.write_text("unrecognized upstream revision")
        before = self.snapshot()
        with self.assertRaisesRegex(ValueError, "refusing a partial patch"):
            patch.apply_patch(self.library)
        self.assertEqual(before, self.snapshot())

    def test_patch_is_idempotent(self):
        patch.apply_patch(self.library)
        once = self.snapshot()
        patch.apply_patch(self.library)
        self.assertEqual(once, self.snapshot())
        source = (self.library / "src/NimBLERemoteValueAttribute.cpp").read_text()
        self.assertEqual(source.count("struct HostQueuedWrite"), 1)
        self.assertEqual(source.count("bool NimBLERemoteValueAttribute::writeValueHostQueued("), 1)

    def test_existing_pairing_patch_upgrades_without_duplicate_callback(self):
        for name in ["src/NimBLEServer.cpp", "src/NimBLEServer.h"]:
            file = self.library / name
            source = file.read_text()
            for before, after in patch.PATCHES[name]:
                source = source.replace(before, after, 1)
            file.write_text(source)
        patch.apply_patch(self.library)
        self.assertEqual((self.library / "src/NimBLEServer.cpp").read_text().count(
            "bool NimBLEServerCallbacks::onRepeatPairing("), 1)

    def test_exact_patched_code_with_preempting_host_scheduler(self):
        patch.apply_patch(self.library)
        build = Path(self.temp.name) / "build"
        build.mkdir()
        source = (self.library / "src/NimBLERemoteValueAttribute.cpp").read_text()
        # Compile the implementation unchanged, only excluding platform includes
        # and unrelated readValue. GATT write/TX ordering and response dispatch
        # are likewise the exact upstream 2.5.1 source, not a conceptual model.
        start = source.index("namespace {\nstruct HostQueuedWrite")
        end = source.index("/**\n * @brief Read the value")
        (build / "patched_remote_write.cpp").write_text(source[start:end])
        shutil.copyfile(FIXTURES / "ble_gattc_write.c", build / "extracted_gatt_write.cpp")
        shutil.copyfile(FIXTURES / "NimBLETaskData.cpp", build / "extracted_task_data.cpp")
        compiler = shutil.which("clang++") or shutil.which("g++")
        self.assertIsNotNone(compiler, "A C++17 compiler is required for the production-code regression")
        binary = build / "test"
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Wno-unused-parameter",
                        "-I", str(build), str(ROOT / "test/test_nimble_host_queue.cpp"),
                        "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
