import importlib.util
import io
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import Mock, patch

spec = importlib.util.spec_from_file_location("serial_tail", Path(__file__).parents[1] / "tools/serial_tail.py")
serial_tail = importlib.util.module_from_spec(spec)
spec.loader.exec_module(serial_tail)


class FakePort:
    def __init__(self):
        self.events = []
        self.closed = False
        self.read = Mock(return_value=b"")

    def __setattr__(self, name, value):
        if name in ("dtr", "rts"):
            self.events.append((name, value))
        super().__setattr__(name, value)

    def open(self):
        self.events.append(("open", self.dtr, self.rts))

    def close(self):
        self.closed = True


class SerialTailTests(unittest.TestCase):
    def test_open_never_deasserts_dtr_while_rts_is_asserted(self):
        port = FakePort()
        factory = Mock(return_value=port)
        with patch.object(serial_tail, "disable_hangup_on_close", return_value=True) as disable:
            serial_tail.open_monitor("test-device", 115200, factory)
        self.assertEqual(port.events, [
            ("dtr", True), ("rts", True), ("open", True, True),
            ("rts", False), ("dtr", False),
        ])
        factory.assert_called_once_with(port=None, baudrate=115200, timeout=0.5)
        disable.assert_called_once_with(port)

    def test_capture_without_newlines_is_bounded_and_copies_output(self):
        port = FakePort()
        port.read.side_effect = [b"no newline", b" still no newline"]
        display, saved = io.StringIO(), io.StringIO()
        ticks = iter([0, 0, 0.8, 1.01])
        serial_tail.capture(port, 1, [display, saved], clock=lambda: next(ticks))
        self.assertEqual(port.read.call_count, 2)
        self.assertAlmostEqual(port.timeout, 0.2)
        self.assertEqual(display.getvalue(), "no newline still no newline")
        self.assertEqual(display.getvalue(), saved.getvalue())

    def test_unsupported_hangup_control_is_nonfatal(self):
        port = Mock()
        termios = types.SimpleNamespace(error=RuntimeError,
            tcgetattr=Mock(side_effect=RuntimeError("unsupported")))
        with patch.dict(sys.modules, {"termios": termios}):
            self.assertFalse(serial_tail.disable_hangup_on_close(port))

    def test_disconnect_closes_port_and_keeps_already_captured_output(self):
        port = FakePort()
        port.read.side_effect = [b"[status] alive\n", OSError("device disconnected")]
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "serial.log"
            module = types.SimpleNamespace(Serial=Mock(return_value=port))
            with patch.dict(sys.modules, {"serial": module}), \
                 patch.object(serial_tail, "disable_hangup_on_close", return_value=True), \
                 patch("sys.stdout", new_callable=io.StringIO), \
                 patch("sys.stderr", new_callable=io.StringIO) as errors:
                result = serial_tail.main(["--port", "test-device", "--seconds", "1", "--output", str(path)])
            self.assertEqual(result, 1)
            self.assertTrue(port.closed)
            self.assertEqual(path.read_text(), "[status] alive\n")
            self.assertIn("Capture stopped: device disconnected", errors.getvalue())


if __name__ == "__main__":
    unittest.main()
