"""Follows the local server's console_mp.log, as `tail -F` does."""
import os

HISTORY_BYTES = 64 * 1024


class LogTail:
    def __init__(self, path, history=HISTORY_BYTES):
        self.path = path
        self._history = history
        self._file = None
        self._pos = 0
        self._partial = ""
        self._seen = False          # opened once: a file that shows up later is a new one, read from its start
        self._cut = False           # the next read starts in the middle of a line

    def close(self):
        if self._file:
            self._file.close()
            self._file = None

    def _open(self):
        try:
            self._file = open(self.path, "rb")
        except OSError:
            return False
        size = os.fstat(self._file.fileno()).st_size
        self._pos = 0 if self._seen else max(0, size - self._history)
        self._cut = self._pos > 0
        self._partial = ""
        self._seen = True
        return True

    def poll(self):
        """The complete lines written since the last call (the last `history` bytes on the first one)."""
        if self._file is None and not self._open():
            return []
        try:
            size = os.stat(self.path).st_size
        except OSError:
            self.close()
            return []
        if size < self._pos:
            # truncated or replaced: a new server run
            self.close()
            if not self._open():
                return []
        self._file.seek(self._pos)
        data = self._file.read()
        self._pos += len(data)
        if not data:
            return []
        lines = (self._partial + data.decode("latin-1")).split("\n")
        self._partial = lines.pop()
        if self._cut and lines:
            lines.pop(0)
            self._cut = False
        return [line.rstrip("\r") for line in lines]
