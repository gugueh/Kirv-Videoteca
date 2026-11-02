"""Decides what a video needs to play in Kirv Videoteca on the PS Vita, and builds
the ffmpeg command for it. Analysis and planning are pure functions so they can be
tested without ffmpeg."""
import json
import os
import shutil
import subprocess
import sys

# What SceAvPlayer in the Vita app accepts (see the app's README, "Limits").
MAX_WIDTH, MAX_HEIGHT = 1280, 720
VIDEO_PROFILES = {"baseline", "constrained baseline", "main", "high"}
MAX_LEVEL = 41
AUDIO_RATES = {44100, 48000}
TEXT_SUBTITLES = {"subrip", "srt", "ass", "ssa", "mov_text", "webvtt", "text"}
VIDEO_EXTENSIONS = {".mp4", ".m4v", ".mkv", ".avi", ".mov", ".webm", ".ts", ".m2ts", ".wmv", ".flv", ".mpg", ".mpeg"}


def tool(name):
    """ffmpeg/ffprobe next to the bundled program first, then on PATH."""
    exe = name + (".exe" if os.name == "nt" else "")
    roots = [getattr(sys, "_MEIPASS", None), os.path.dirname(sys.executable) if getattr(sys, "frozen", False) else None,
             os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ffmpeg")]
    for root in roots:
        if root:
            for candidate in (os.path.join(root, exe), os.path.join(root, "ffmpeg", exe)):
                if os.path.isfile(candidate):
                    return candidate
    return shutil.which(name)


def is_video(path):
    return os.path.splitext(path)[1].lower() in VIDEO_EXTENSIONS


def probe(path):
    ffprobe = tool("ffprobe")
    if not ffprobe:
        raise RuntimeError("ffprobe not found")
    out = subprocess.run([ffprobe, "-v", "error", "-print_format", "json", "-show_format", "-show_streams", path],
                         capture_output=True, check=True, **_hidden()).stdout
    return json.loads(out.decode("utf-8", "replace"))


def faststart(path):
    """True when the MP4 index (moov) comes before the media data (mdat)."""
    try:
        with open(path, "rb") as f:
            for _ in range(64):
                head = f.read(8)
                if len(head) < 8:
                    return False
                size, kind = int.from_bytes(head[:4], "big"), head[4:]
                if kind == b"moov":
                    return True
                if kind == b"mdat":
                    return False
                if size == 1:
                    size = int.from_bytes(f.read(8), "big") - 8
                elif size == 0:
                    return False
                f.seek(size - 8, 1)
    except OSError:
        return False
    return False


def plan(info, path=""):
    """Returns what to do: {'video','audio','subtitle','remux','summary','reasons','duration'}.
    video/audio: 'copy', 'encode' or 'none'. subtitle: stream index to extract or None."""
    streams = info.get("streams", [])
    video = next((s for s in streams if s.get("codec_type") == "video" and not s.get("disposition", {}).get("attached_pic")), None)
    audio = next((s for s in streams if s.get("codec_type") == "audio"), None)
    subtitle = next((s for s in streams if s.get("codec_type") == "subtitle" and s.get("codec_name") in TEXT_SUBTITLES), None)
    reasons = []
    result = {"video": "none", "audio": "none", "subtitle": None, "remux": False, "reasons": reasons,
              "duration": float(info.get("format", {}).get("duration") or 0), "width": 0, "height": 0,
              "video_codec": "", "audio_codec": ""}
    if not video:
        result["summary"] = "unsupported"
        reasons.append("no_video")
        return result
    w, h = int(video.get("width") or 0), int(video.get("height") or 0)
    result.update(width=w, height=h, video_codec=video.get("codec_name", ""))
    profile = (video.get("profile") or "").lower()
    level = int(video.get("level") or 0)
    if video.get("codec_name") != "h264":
        reasons.append("video_codec")
    elif profile not in VIDEO_PROFILES:
        reasons.append("video_profile")
    elif video.get("pix_fmt") != "yuv420p":
        reasons.append("video_pixels")
    elif w > MAX_WIDTH or h > MAX_HEIGHT:
        reasons.append("video_size")
    elif level > MAX_LEVEL:
        reasons.append("video_level")
    result["video"] = "encode" if reasons else "copy"
    if audio:
        result["audio_codec"] = audio.get("codec_name", "") + ("" if not audio.get("profile") else " " + audio["profile"])
        ok = (audio.get("codec_name") == "aac" and (audio.get("profile") or "LC") == "LC"
              and int(audio.get("channels") or 0) <= 2 and int(audio.get("sample_rate") or 0) in AUDIO_RATES)
        result["audio"] = "copy" if ok else "encode"
        if not ok:
            reasons.append("audio")
    if subtitle is not None:
        result["subtitle"] = subtitle.get("index")
    fmt = info.get("format", {}).get("format_name", "")
    is_mp4 = "mp4" in fmt and (not path or path.lower().endswith((".mp4", ".m4v")))
    extra_streams = sum(1 for s in streams if s.get("codec_type") in ("video", "audio")) > (1 + (1 if audio else 0))
    if not is_mp4 or extra_streams or (path and os.path.isfile(path) and not faststart(path)):
        result["remux"] = True
        reasons.append("container")
    if result["video"] == "encode":
        result["summary"] = "encode_video"
    elif result["audio"] == "encode":
        result["summary"] = "encode_audio"
    elif result["remux"]:
        result["summary"] = "remux"
    else:
        result["summary"] = "ready"
    return result


def output_name(src):
    return os.path.splitext(os.path.basename(src))[0] + ".mp4"


def command(ffmpeg, src, dst, p):
    """ffmpeg arguments for plan p. Progress goes to stdout as key=value lines."""
    args = [ffmpeg, "-nostdin", "-hide_banner", "-v", "error", "-y", "-progress", "pipe:1", "-i", src, "-map", "0:v:0"]
    if p["audio"] != "none":
        args += ["-map", "0:a:0"]
    if p["video"] == "copy":
        args += ["-c:v", "copy"]
    else:
        args += ["-c:v", "libx264", "-preset", "medium", "-crf", "21", "-profile:v", "high", "-level:v", "4.1",
                 "-pix_fmt", "yuv420p",
                 "-vf", "scale='min(%d,iw)':'min(%d,ih)':force_original_aspect_ratio=decrease:force_divisible_by=2" % (MAX_WIDTH, MAX_HEIGHT),
                 "-maxrate", "6M", "-bufsize", "12M"]
    if p["audio"] == "copy":
        args += ["-c:a", "copy"]
    elif p["audio"] == "encode":
        args += ["-c:a", "aac", "-profile:a", "aac_low", "-b:a", "128k", "-ac", "2", "-ar", "48000"]
    args += ["-sn", "-dn", "-map_metadata", "-1", "-movflags", "+faststart", dst]
    return args


def subtitle_command(ffmpeg, src, dst, index):
    return [ffmpeg, "-nostdin", "-hide_banner", "-v", "error", "-y", "-i", src, "-map", "0:%d" % index, "-c:s", "srt", dst]


def _hidden():
    """No console window per ffmpeg call on Windows."""
    if os.name == "nt":
        return {"creationflags": 0x08000000}
    return {}


def run(args, duration, on_progress=None, cancel=None):
    """Runs ffmpeg, reporting progress 0..1. Returns (ok, error text)."""
    proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, **_hidden())
    for raw in proc.stdout:
        if cancel and cancel():
            proc.kill()
            proc.wait()
            return False, "cancelled"
        line = raw.decode("utf-8", "replace").strip()
        if line.startswith("out_time_us=") and duration > 0 and on_progress:
            try:
                on_progress(min(1.0, int(line.split("=", 1)[1]) / 1e6 / duration))
            except ValueError:
                pass
    err = proc.stderr.read().decode("utf-8", "replace")
    return proc.wait() == 0, err.strip()[-400:]
