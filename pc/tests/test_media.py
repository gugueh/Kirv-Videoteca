import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from kirv_pc import media, vita  # noqa: E402


def info(video=None, audio=None, subs=(), fmt="mov,mp4,m4a,3gp,3g2,mj2", duration="100"):
    streams = []
    if video is not None:
        streams.append(dict({"index": 0, "codec_type": "video", "codec_name": "h264", "profile": "High",
                             "pix_fmt": "yuv420p", "width": 1280, "height": 720, "level": 40}, **video))
    if audio is not None:
        streams.append(dict({"index": 1, "codec_type": "audio", "codec_name": "aac", "profile": "LC",
                             "channels": 2, "sample_rate": "48000"}, **audio))
    for i, s in enumerate(subs):
        streams.append(dict({"index": 2 + i, "codec_type": "subtitle"}, **s))
    return {"streams": streams, "format": {"format_name": fmt, "duration": duration}}


class Plan(unittest.TestCase):
    def test_ready(self):
        p = media.plan(info({}, {}))
        self.assertEqual((p["summary"], p["video"], p["audio"]), ("ready", "copy", "copy"))

    def test_he_aac_needs_audio_only(self):
        p = media.plan(info({}, {"profile": "HE-AAC"}))
        self.assertEqual((p["summary"], p["video"], p["audio"]), ("encode_audio", "copy", "encode"))

    def test_surround_and_rates(self):
        self.assertEqual(media.plan(info({}, {"channels": 6}))["audio"], "encode")
        self.assertEqual(media.plan(info({}, {"sample_rate": "22050"}))["audio"], "encode")
        self.assertEqual(media.plan(info({}, {"codec_name": "ac3", "profile": None}))["audio"], "encode")

    def test_video_reasons(self):
        for change in ({"codec_name": "hevc"}, {"width": 1920, "height": 1080}, {"pix_fmt": "yuv420p10le"},
                       {"profile": "High 10"}, {"level": 51}):
            p = media.plan(info(change, {}))
            self.assertEqual((p["summary"], p["video"]), ("encode_video", "encode"), change)

    def test_mkv_is_remuxed_and_subtitles_kept(self):
        p = media.plan(info({}, {}, subs=[{"codec_name": "ass"}], fmt="matroska,webm"), "a.mkv")
        self.assertEqual((p["summary"], p["remux"], p["subtitle"]), ("remux", True, 2))
        self.assertIsNone(media.plan(info({}, {}, subs=[{"codec_name": "hdmv_pgs_subtitle"}]))["subtitle"])

    def test_no_video_and_no_audio(self):
        self.assertEqual(media.plan(info(None, {}))["summary"], "unsupported")
        p = media.plan(info({}, None))
        self.assertEqual((p["summary"], p["audio"]), ("ready", "none"))

    def test_cover_art_is_not_the_video(self):
        streams = info({}, {})
        streams["streams"].insert(0, {"index": 9, "codec_type": "video", "codec_name": "mjpeg", "disposition": {"attached_pic": 1}})
        self.assertEqual(media.plan(streams)["video"], "copy")

    def test_command(self):
        p = media.plan(info({"codec_name": "hevc"}, {"profile": "HE-AAC"}))
        args = media.command("ffmpeg", "in.mkv", "out.mp4", p)
        self.assertIn("libx264", args)
        self.assertIn("aac_low", args)
        self.assertEqual(args[-1], "out.mp4")
        self.assertIn("+faststart", args)
        copy = media.command("ffmpeg", "in.mp4", "out.mp4", media.plan(info({}, {"profile": "HE-AAC"})))
        self.assertEqual(copy[copy.index("-c:v") + 1], "copy")
        self.assertEqual(media.output_name("x/Ep 01.mkv"), "Ep 01.mp4")


class Files(unittest.TestCase):
    def atom(self, kind, body=b""):
        return (8 + len(body)).to_bytes(4, "big") + kind + body

    def test_faststart(self):
        with tempfile.TemporaryDirectory() as d:
            good, bad = os.path.join(d, "g.mp4"), os.path.join(d, "b.mp4")
            open(good, "wb").write(self.atom(b"ftyp", b"isom") + self.atom(b"moov", b"x") + self.atom(b"mdat", b"y"))
            open(bad, "wb").write(self.atom(b"ftyp", b"isom") + self.atom(b"mdat", b"y") + self.atom(b"moov", b"x"))
            self.assertTrue(media.faststart(good))
            self.assertFalse(media.faststart(bad))
            self.assertFalse(media.faststart(os.path.join(d, "missing.mp4")))

    def test_safe_name(self):
        self.assertEqual(vita.safe_name('Serie: "Final"?'), "Serie_ _Final__")
        self.assertEqual(vita.safe_name("  Temporada 1.  "), "Temporada 1")
        self.assertEqual(vita.safe_name("..."), "Videos")
        self.assertEqual(vita.safe_name("Mirai Nikki"), "Mirai Nikki")

    def test_is_video(self):
        self.assertTrue(media.is_video("a.MKV"))
        self.assertFalse(media.is_video("a.srt"))


if __name__ == "__main__":
    unittest.main()
