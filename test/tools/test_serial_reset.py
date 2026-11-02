"""USB-UART reset ordering only; no physical serial port is opened by tests."""
from contextlib import ExitStack
from pathlib import Path
import stat
import struct
import sys
import termios
from types import SimpleNamespace
import unittest
from unittest.mock import call, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
import reset_serial


class SerialResetTests(unittest.TestCase):
    def harness(self, stack, path='/dev/ttyUSB0', mode=stat.S_IFCHR, tty=True):
        stack.enter_context(patch.object(Path, 'resolve', return_value=Path(path)))
        stack.enter_context(patch.object(Path, 'stat', return_value=SimpleNamespace(st_mode=mode)))
        opened=stack.enter_context(patch.object(reset_serial.os, 'open', return_value=11))
        stack.enter_context(patch.object(reset_serial.os, 'isatty', return_value=tty))
        closed=stack.enter_context(patch.object(reset_serial.os, 'close'))
        ioctl=stack.enter_context(patch.object(reset_serial.fcntl, 'ioctl'))
        wait=stack.enter_context(patch.object(reset_serial.time, 'sleep'))
        return opened,closed,ioctl,wait

    def test_application_boot_pulse_uses_no_serial_data_and_releases_port(self):
        with ExitStack() as stack:
            opened,closed,ioctl,wait=self.harness(stack)
            reset_serial.reset('/dev/serial/by-id/identified-m5')
            opened.assert_called_once_with(Path('/dev/ttyUSB0'),reset_serial.os.O_RDWR |
                                          reset_serial.os.O_NOCTTY | reset_serial.os.O_NONBLOCK)
            self.assertEqual(ioctl.call_args_list,[
                call(11,termios.TIOCMBIC,struct.pack('I',termios.TIOCM_DTR)),
                call(11,termios.TIOCMBIS,struct.pack('I',termios.TIOCM_RTS)),
                call(11,termios.TIOCMBIC,struct.pack('I',termios.TIOCM_RTS))])
            wait.assert_called_once_with(.1)
            closed.assert_called_once_with(11)

    def test_cancel_or_wait_failure_releases_en_and_descriptor(self):
        for error in (KeyboardInterrupt(),OSError('interrupted')):
            with self.subTest(error=error),ExitStack() as stack:
                _,closed,ioctl,wait=self.harness(stack)
                wait.side_effect=error
                with self.assertRaises(type(error)):reset_serial.reset('/dev/ttyUSB0')
                self.assertEqual(ioctl.call_args_list[-1],call(11,termios.TIOCMBIC,struct.pack('I',termios.TIOCM_RTS)))
                closed.assert_called_once_with(11)

    def test_regular_files_and_unrelated_devices_are_rejected_before_open(self):
        for path,mode in (('/tmp/ttyUSB0',stat.S_IFCHR),('/dev/null',stat.S_IFCHR),
                          ('/dev/ttyUSB0',stat.S_IFREG)):
            with self.subTest(path=path,mode=mode),ExitStack() as stack:
                opened,_,ioctl,_=self.harness(stack,path,mode)
                with self.assertRaises(ValueError):reset_serial.reset(path)
                opened.assert_not_called();ioctl.assert_not_called()

    def test_non_terminal_driver_closes_without_toggling_lines(self):
        with ExitStack() as stack:
            _,closed,ioctl,_=self.harness(stack,tty=False)
            with self.assertRaises(ValueError):reset_serial.reset('/dev/ttyUSB0')
            ioctl.assert_not_called();closed.assert_called_once_with(11)


if __name__=='__main__':unittest.main()
