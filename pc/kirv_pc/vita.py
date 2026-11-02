# Copyright (c) 2025-2026 gugueh (Kirv Studio). All rights reserved.
"""Sends files to the Kirv Videoteca library on a PS Vita over VitaShell's FTP server."""
import ftplib
import os

LIBRARY = "/ux0:/data/VMPH00001/video"
BLOCK = 256 * 1024


def safe_name(name):
    """Folder/file names the Vita app accepts (no FAT-reserved characters, no edge spaces or final dot)."""
    cleaned = "".join("_" if c in '<>:"/\\|?*' or ord(c) < 32 else c for c in name).strip()
    cleaned = cleaned.rstrip(".").strip()
    return cleaned[:200] or "Videos"


class Vita:
    def __init__(self, host, port=1337, timeout=15):
        self.ftp = ftplib.FTP()
        self.ftp.encoding = "utf-8"
        self.ftp.connect(host, int(port), timeout=timeout)
        self.ftp.login()
        self.ftp.set_pasv(True)

    def close(self):
        try:
            self.ftp.quit()
        except (ftplib.Error, OSError):
            self.ftp.close()

    def _list(self, path):
        """VitaShell ignores a path given to LIST and lists the current folder, so change into it first."""
        lines = []
        try:
            self.ftp.cwd(path)
            self.ftp.retrlines("LIST", lines.append)
        except ftplib.error_perm:
            return []
        finally:
            try:
                self.ftp.cwd("/")
            except ftplib.Error:
                pass
        return lines

    def folders(self):
        """Top-level folders of the library."""
        return sorted(line.split(None, 8)[-1] for line in self._list(LIBRARY) if line.startswith("d"))

    def listing(self, folder):
        """{name: size} of the files in a library folder."""
        result = {}
        for line in self._list(LIBRARY + "/" + folder):
            parts = line.split(None, 8)
            if len(parts) == 9 and not line.startswith("d"):
                try:
                    result[parts[8]] = int(parts[4])
                except ValueError:
                    pass
        return result

    def ensure(self, folder):
        for path in ("/ux0:/data/VMPH00001", LIBRARY, LIBRARY + "/" + folder):
            try:
                self.ftp.mkd(path)
            except ftplib.error_perm:
                pass  # already there

    def send(self, local, folder, name=None, on_progress=None, cancel=None):
        """Uploads local into the library folder and checks the size on the Vita."""
        name = name or os.path.basename(local)
        size = os.path.getsize(local)
        sent = [0]
        self.ensure(folder)
        if self.listing(folder).get(name) == size:
            return 0  # already on the Vita, identical size: nothing to send

        def block(data):
            if cancel and cancel():
                raise InterruptedError("cancelled")
            sent[0] += len(data)
            if on_progress and size:
                on_progress(sent[0] / size)

        with open(local, "rb") as f:
            self.ftp.storbinary("STOR " + LIBRARY + "/" + folder + "/" + name, f, BLOCK, block)
        remote = self.listing(folder).get(name)
        if remote != size:
            raise IOError("size mismatch on the Vita (%s of %s bytes)" % (remote, size))
        return size
