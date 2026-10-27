"""Stage generated directories and keep the last successful output on failure."""
from contextlib import contextmanager
from pathlib import Path
import os
import shutil
import tempfile


@contextmanager
def staged_directory(destination, preserve=False):
    destination = Path(destination).absolute()
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".build-", dir=destination.parent) as temp:
        staged = Path(temp) / "output"
        if preserve and destination.exists():
            shutil.copytree(destination, staged)
        else:
            staged.mkdir()
        yield staged
        previous = Path(temp) / "previous"
        had_previous = destination.exists()
        if had_previous:
            os.replace(destination, previous)
        try:
            os.replace(staged, destination)
        except BaseException:
            if had_previous:
                os.replace(previous, destination)
            raise
