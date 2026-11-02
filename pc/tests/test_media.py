# Copyright (c) 2025-2026 gugueh (Kirv Studio). All rights reserved.
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

    def test_compact_for_the_vita_screen(self):
        big = info({"width": 1920, "height": 1080, "bit_rate": "1400000"}, {})
        p = media.plan(big, compact=True)
        self.assertEqual((p["summary"], p["video"], p["compact"]), ("shrink", "encode", True))
        args = media.command("ffmpeg", "in.mp4", "out.mp4", p)
        self.assertIn("23", args)
        self.assertTrue(any("960" in a and "544" in a for a in args))
        small = info({"width": 848, "height": 480, "bit_rate": "460000"}, {"profile": "HE-AAC"})
        self.assertEqual(media.plan(small, compact=True)["summary"], "encode_audio")
        heavy = info({"width": 960, "height": 540, "bit_rate": "5000000"}, {})
        self.assertEqual(media.plan(heavy, compact=True)["summary"], "shrink")
        hevc = media.plan(info({"codec_name": "hevc", "width": 1920, "height": 1080}, {}), compact=True)
        self.assertEqual((hevc["summary"], hevc["compact"]), ("encode_video", True))
        self.assertEqual(media.plan(big)["summary"], "encode_video")
        self.assertFalse(media.plan(big)["compact"])

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


class Subtitles(unittest.TestCase):
    def cues(self, text):
        out = []
        if not text.strip():
            return out
        for block in text.strip().split("\n\n"):
            lines = block.split("\n")
            a, b = lines[1].split(" --> ")
            out.append((a, b, "\n".join(lines[2:])))
        return out

    def test_tags_and_overlaps(self):
        src = ("﻿1\r\n00:00:01,000 --> 00:00:03,000\r\n<font size=\"20\">{\\an8}Arriba <i>x</i></font>\r\n\r\n"
               "2\r\n00:00:02,000 --> 00:00:04,000\r\nB\\NC\r\n\r\n3\r\n00:00:05,000 --> 00:00:05,000\r\nvacío\r\n")
        self.assertEqual(self.cues(media.vita_srt(src)), [
            ("00:00:01,000", "00:00:02,000", "Arriba x"),
            ("00:00:02,000", "00:00:03,000", "Arriba x\nB\nC"),
            ("00:00:03,000", "00:00:04,000", "B\nC")])

    def test_random_cues_never_overlap(self):
        import random
        rng = random.Random(7)
        for _ in range(500):
            blocks = []
            for i in range(rng.randint(0, 12)):
                a = rng.randint(0, 20000)
                b = a + rng.randint(0, 5000)
                blocks.append("%d\n%s --> %s\n%s" % (i, media._stamp(a), media._stamp(b), rng.choice(["a", "b", "", "<b>c</b>"])))
            out = self.cues(media.vita_srt("\n\n".join(blocks))) if blocks else []
            ends = [(media._ms(*a.replace(",", ":").split(":")), media._ms(*b.replace(",", ":").split(":"))) for a, b, _ in out]
            for (a, b), nxt in zip(ends, ends[1:] + [None]):
                self.assertLess(a, b)
                if nxt:
                    self.assertLessEqual(b, nxt[0])
            for _, _, body in out:
                self.assertTrue(body and "<" not in body)

    def test_empty(self):
        self.assertEqual(media.vita_srt(""), "")
        self.assertEqual(media.vita_srt("hola"), "")


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
