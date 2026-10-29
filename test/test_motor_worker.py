"""Replay cable captures through the unchanged production decoder/load worker."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "firmware/VoltraKnob/src"


class MotorWorkerTests(unittest.TestCase):
    def test_actual_cable_replies_and_motor_writes(self):
        source = (SOURCE / "link/voltra_link.cpp").read_text()
        # Compile these functions verbatim. Only platform/BLE/time dependencies
        # are replaced by the host harness; no replica of the load algorithm.
        fragments = [source[source.index("enum class CommandType:"):source.index("struct Command ")]]
        start = source.index("struct Command ")
        fragments.append(source[start:source.index("\n", start)])
        for start_text, end_text in [
            ("void applyControllerFrame(", "void notify("),
            ("void completeMotor(", "void performMotor("),
            ("void performMotor(", "\nvoid pump("),
        ]:
            start = source.index(start_text)
            fragments.append(source[start:source.index(end_text, start)])
        compiler = shutil.which("clang++") or shutil.which("g++")
        self.assertIsNotNone(compiler, "A C++17 compiler is required")
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            (build / "production_motor_worker.cpp").write_text("\n".join(fragments))
            binary = build / "test_motor_worker"
            subprocess.run([
                compiler, "-std=c++17", "-Wall", "-Wextra", "-Wno-unused-parameter",
                "-I", str(SOURCE), "-I", str(ROOT / "test"), "-I", str(build),
                str(ROOT / "test/test_motor_worker.cpp"),
                str(SOURCE / "protocol/voltra_protocol.cpp"), "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
