"""Kirv Videoteca PC: prepares videos for the PS Vita app and sends them over FTP."""
import json
import os
import shutil
import sys
import threading
import webbrowser

import webview
from webview.dom import DOMEventHandler

from . import media, vita

APP_DIR = os.path.dirname(os.path.abspath(__file__))
CONFIG = os.path.join(os.environ.get("APPDATA") or os.path.expanduser("~/.config"), "KirvVideoteca", "pc.json")
DEFAULTS = {"host": "", "port": 1337, "lang": "es",
            "output": os.path.join(os.path.expanduser("~"), "Videos", "Kirv Videoteca"), "keep": True}


def resource(*parts):
    """Files bundled by PyInstaller live under _MEIPASS; in development next to this module."""
    return os.path.join(getattr(sys, "_MEIPASS", os.path.dirname(APP_DIR)), "kirv_pc", *parts) \
        if getattr(sys, "frozen", False) else os.path.join(APP_DIR, *parts)


def sync_assets():
    """In development, copy the Vita app's UI art so both sides share one look."""
    target = resource("ui", "img")
    source = os.path.join(APP_DIR, "..", "..", "assets", "ui")
    if getattr(sys, "frozen", False) or not os.path.isdir(source):
        return
    os.makedirs(target, exist_ok=True)
    for name in os.listdir(source):
        if name.endswith(".png"):
            shutil.copy2(os.path.join(source, name), os.path.join(target, name))


class Api:
    def __init__(self):
        self._window = None
        self.items = []
        self._lock = threading.Lock()
        self.busy = False
        self.stop = False
        self.settings = dict(DEFAULTS)
        try:
            with open(CONFIG, encoding="utf-8") as f:
                self.settings.update(json.load(f))
        except (OSError, ValueError):
            pass

    # --- helpers -------------------------------------------------------
    def _emit(self, event, data):
        if self._window:
            self._window.evaluate_js("window.kirv && kirv.on(%s, %s)" % (json.dumps(event), json.dumps(data)))

    def _public(self, item):
        return {k: v for k, v in item.items() if not k.startswith("_")}

    # --- called from the page -----------------------------------------
    def state(self):
        return {"settings": self.settings, "ffmpeg": bool(media.tool("ffmpeg") and media.tool("ffprobe")),
                "items": [self._public(i) for i in self.items], "busy": self.busy}

    def save_settings(self, values):
        self.settings.update({k: values[k] for k in DEFAULTS if k in values})
        os.makedirs(os.path.dirname(CONFIG), exist_ok=True)
        with open(CONFIG, "w", encoding="utf-8") as f:
            json.dump(self.settings, f, indent=2)
        return self.settings

    def pick(self, folder):
        kind = webview.FileDialog.FOLDER if folder else webview.FileDialog.OPEN
        chosen = self._window.create_file_dialog(kind, allow_multiple=not folder,
                                                file_types=() if folder else ("Videos (*.mp4;*.mkv;*.avi;*.mov;*.m4v;*.webm;*.ts;*.wmv)",))
        if chosen:
            self.add(list(chosen))

    def add(self, paths):
        """Adds files or whole folders; each video is analysed in the background."""
        found = []
        for path in paths:
            if os.path.isdir(path):
                for root, _, files in os.walk(path):
                    found += [os.path.join(root, f) for f in sorted(files) if media.is_video(f)]
            elif media.is_video(path):
                found.append(path)
        with self._lock:
            known = {i["path"] for i in self.items}
            new = []
            for path in found:
                if path in known:
                    continue
                item = {"id": len(self.items) + len(new) + 1, "path": path, "name": os.path.basename(path),
                        "folder": vita.safe_name(os.path.basename(os.path.dirname(path))),
                        "size": os.path.getsize(path), "status": "analysing", "progress": 0, "detail": ""}
                new.append(item)
            self.items += new
        for item in new:
            self._emit("item", self._public(item))
        threading.Thread(target=self._analyse, args=(new,), daemon=True).start()
        return len(new)

    def _analyse(self, items):
        for item in items:
            try:
                p = media.plan(media.probe(item["path"]), item["path"])
                item["_plan"] = p
                item.update(status=p["summary"], duration=p["duration"], width=p["width"], height=p["height"],
                            video=p["video_codec"], audio=p["audio_codec"], subtitles=p["subtitle"] is not None)
            except Exception as error:  # unreadable or not a video
                item.update(status="unsupported", detail=str(error)[:200])
            self._emit("item", self._public(item))

    def rename_folder(self, old, new):
        new = vita.safe_name(new)
        for item in self.items:
            if item["folder"] == old and item["status"] not in ("sending", "converting"):
                item["folder"] = new
        return new

    def remove(self, item_id):
        if not self.busy:
            self.items = [i for i in self.items if i["id"] != item_id]

    def clear(self):
        if not self.busy:
            self.items = [i for i in self.items if i["status"] not in ("sent", "done")] if any(
                i["status"] in ("sent", "done") for i in self.items) else []

    def vita_folders(self):
        try:
            box = vita.Vita(self.settings["host"], self.settings["port"], timeout=6)
            try:
                return {"ok": True, "folders": box.folders()}
            finally:
                box.close()
        except Exception as error:
            return {"ok": False, "error": str(error)}

    def open_studio(self):
        webbrowser.open("https://www.behance.net/guguehh")

    def cancel(self):
        self.stop = True

    def start(self, send):
        if self.busy:
            return False
        self.busy, self.stop = True, False
        threading.Thread(target=self._work, args=(bool(send),), daemon=True).start()
        return True

    # --- the job --------------------------------------------------------
    def _work(self, send):
        box = None
        ffmpeg = media.tool("ffmpeg")
        try:
            if send:
                box = vita.Vita(self.settings["host"], self.settings["port"])
            for item in list(self.items):
                if self.stop:
                    break
                p = item.get("_plan")
                if not p or item["status"] in ("unsupported", "sent", "analysing") or (not send and item["status"] == "done"):
                    continue
                files = self._convert(item, p, ffmpeg)
                if files is None:
                    continue
                if send:
                    self._send(box, item, files)
                if not self.settings.get("keep") and send and item["status"] == "sent":
                    for local in files:
                        if local != item["path"] and os.path.isfile(local):
                            os.remove(local)
        except Exception as error:
            self._emit("error", str(error))
        finally:
            if box:
                box.close()
            self.busy = False
            self._emit("finished", {"cancelled": self.stop})

    def _convert(self, item, p, ffmpeg):
        """Returns the files to send: the original when it is ready, else fresh copies."""
        if item.get("_files"):
            return item["_files"]
        if p["summary"] == "ready":
            item["_files"] = [item["path"]]
            return item["_files"]
        out_dir = os.path.join(self.settings["output"], item["folder"])
        os.makedirs(out_dir, exist_ok=True)
        target = os.path.join(out_dir, media.output_name(item["path"]))
        if os.path.abspath(target) == os.path.abspath(item["path"]):
            target = os.path.splitext(target)[0] + " (Vita).mp4"
        item.update(status="converting", progress=0, detail="")
        self._emit("item", self._public(item))

        def progress(value):
            item["progress"] = value
            self._emit("progress", {"id": item["id"], "progress": value, "status": "converting"})

        ok, error = media.run(media.command(ffmpeg, item["path"], target, p), p["duration"], progress, lambda: self.stop)
        if not ok:
            item.update(status="failed", detail=error or "ffmpeg")
            if os.path.isfile(target):
                os.remove(target)
            self._emit("item", self._public(item))
            return None
        files = [target]
        if p["subtitle"] is not None:
            srt = os.path.splitext(target)[0] + ".srt"
            if media.run(media.subtitle_command(ffmpeg, item["path"], srt, p["subtitle"]), 0)[0]:
                files.append(srt)
        item.update(_files=files, status="done", progress=1)
        self._emit("item", self._public(item))
        return files

    def _send(self, box, item, files):
        item.update(status="sending", progress=0)
        self._emit("item", self._public(item))
        try:
            for local in files:
                name = media.output_name(item["path"]) if local.endswith(".mp4") else os.path.basename(local)
                box.send(local, item["folder"], name,
                         lambda v: self._emit("progress", {"id": item["id"], "progress": v, "status": "sending"}),
                         lambda: self.stop)
            item.update(status="sent", progress=1)
        except Exception as error:
            item.update(status="failed", detail=str(error)[:200])
        self._emit("item", self._public(item))


def main():
    sync_assets()
    api = Api()
    window = webview.create_window("Kirv Videoteca · PC", resource("ui", "index.html"), js_api=api,
                                   width=1100, height=720, min_size=(900, 600), background_color="#0F1C2E")
    api._window = window

    def on_drop(event):
        paths = [f.get("pywebviewFullPath") for f in event.get("dataTransfer", {}).get("files", [])]
        api.add([p for p in paths if p])

    def loaded():
        window.dom.document.events.dragover += DOMEventHandler(lambda e: None, True, True, debounce=500)
        window.dom.document.events.drop += DOMEventHandler(on_drop, True, True)

    window.events.loaded += loaded
    webview.start()


if __name__ == "__main__":
    main()
