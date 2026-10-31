from __future__ import annotations

import struct
import json
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "compare_images.py"
sys.path.insert(0, str(ROOT / "scripts"))

import compare_images  # noqa: E402


def write_png(path: Path, pixels: list[list[tuple[int, int, int]]]) -> None:
    """A minimal 8 bit RGB PNG, filter 0, for the tests."""
    height = len(pixels)
    width = len(pixels[0])
    raw = b"".join(b"\x00" + bytes(channel for pixel in row for channel in pixel) for row in pixels)

    def chunk(kind: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def flat(width: int, height: int, value: tuple[int, int, int]) -> list[list[tuple[int, int, int]]]:
    return [[value for _ in range(width)] for _ in range(height)]


class CompareImagesTests(unittest.TestCase):
    def run_script(self, *arguments: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run([sys.executable, "-I", str(SCRIPT), *arguments], capture_output=True, text=True, check=False)

    def test_identical_images_pass_with_zero_difference(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            first, second = Path(directory) / "a.png", Path(directory) / "b.png"
            write_png(first, flat(8, 6, (10, 20, 30)))
            write_png(second, flat(8, 6, (10, 20, 30)))
            result = self.run_script(str(first), str(second))
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("mean difference 0.000", result.stdout)

    def test_small_differences_are_inside_the_pixel_tolerance(self) -> None:
        stats = compare_images.compare(flat(4, 4, (100, 100, 100)), flat(4, 4, (105, 100, 98)), pixel_tolerance=12)
        self.assertEqual(stats["differing"], 0.0)
        self.assertAlmostEqual(stats["mean"], 7.0 / 3.0)
        self.assertEqual(stats["worst"], 5)

    def test_a_darker_image_fails_the_limits(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            first, second = Path(directory) / "a.png", Path(directory) / "b.png"
            write_png(first, flat(8, 6, (180, 180, 180)))
            write_png(second, flat(8, 6, (110, 110, 110)))
            result = self.run_script(str(first), str(second), "--max-mean", "5", "--max-differing", "0.1")
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertIn("FAILED", result.stdout)
            self.assertIn("100.00% of pixels differ", result.stdout)

    def test_a_few_pixels_may_differ_within_the_fraction(self) -> None:
        second = flat(10, 10, (50, 50, 50))
        second[3][4] = (255, 0, 0)
        stats = compare_images.compare(flat(10, 10, (50, 50, 50)), second, pixel_tolerance=12)
        self.assertAlmostEqual(stats["differing"], 0.01)
        with tempfile.TemporaryDirectory() as directory:
            first_path, second_path = Path(directory) / "a.png", Path(directory) / "b.png"
            write_png(first_path, flat(10, 10, (50, 50, 50)))
            write_png(second_path, second)
            self.assertEqual(self.run_script(str(first_path), str(second_path), "--max-mean", "10", "--max-differing", "0.02").returncode, 0)
            self.assertEqual(self.run_script(str(first_path), str(second_path), "--max-mean", "10", "--max-differing", "0.005").returncode, 1)

    def test_ignored_rectangles_are_left_out(self) -> None:
        second = flat(10, 10, (50, 50, 50))
        for y in range(2, 4):
            for x in range(2, 4):
                second[y][x] = (255, 255, 255)
        stats = compare_images.compare(flat(10, 10, (50, 50, 50)), second, ignore=[(2, 2, 4, 4)])
        self.assertEqual(stats["differing"], 0.0)
        self.assertEqual(stats["pixels"], 96)
        with self.assertRaises(ValueError):
            compare_images.compare(flat(2, 2, (0, 0, 0)), flat(2, 2, (0, 0, 0)), ignore=[(0, 0, 2, 2)])

    def test_images_of_different_sizes_are_refused(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            first, second = Path(directory) / "a.png", Path(directory) / "b.png"
            write_png(first, flat(8, 6, (1, 2, 3)))
            write_png(second, flat(6, 8, (1, 2, 3)))
            result = self.run_script(str(first), str(second))
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertIn("different sizes", result.stdout)

    def test_a_missing_file_is_an_error_not_a_pass(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            first = Path(directory) / "a.png"
            write_png(first, flat(2, 2, (1, 2, 3)))
            self.assertEqual(self.run_script(str(first), str(Path(directory) / "missing.png")).returncode, 2)

    def test_failed_comparison_retains_diff_and_report(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_png(root / "a.png", flat(8, 6, (180, 180, 180)))
            write_png(root / "b.png", flat(8, 6, (110, 110, 110)))
            result = self.run_script(str(root / "a.png"), str(root / "b.png"),
                                     "--diff", str(root / "diff.png"), "--report", str(root / "report.json"))
            self.assertEqual(result.returncode, 1)
            report = json.loads((root / "report.json").read_text())
            self.assertEqual(report["result"], "FAIL")
            self.assertEqual(report["statistics"]["mean"], 70)
            self.assertEqual(compare_images.read_png(root / "diff.png")[0][0], (255, 255, 255))

    def test_nonfinite_or_out_of_range_thresholds_cannot_pass(self) -> None:
        for option, value in (("--max-mean", "nan"), ("--max-differing", "inf"),
                              ("--pixel-tolerance", "256"), ("--max-differing", "-1")):
            self.assertEqual(self.run_script("a.png", "b.png", option, value).returncode, 2)


if __name__ == "__main__":
    unittest.main()
