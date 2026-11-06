"""Compare two PNG captures of the same scene, for the editor-versus-package image checks.

    python compare_images.py editor.png game.png [--pixel-tolerance 12] [--max-mean 3.0]
        [--max-differing 0.02] [--ignore x0,y0,x1,y1 ...]

The images must have the same size. A pixel differs when any of its channels differs by more than --pixel-tolerance (0 to 255).
The check fails when the mean absolute channel difference is over --max-mean or when more than --max-differing of the pixels
(outside the --ignore rectangles) differ. The numbers are printed either way, so a limit can be set from a real run.
"""
import argparse
import json
import math
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from png_readback import read_png, write_png  # noqa: E402


def compare(first, second, pixel_tolerance=12, ignore=()):
    """Returns a dict of statistics, or raises ValueError when the images cannot be compared."""
    if len(first) != len(second) or (first and len(first[0]) != len(second[0])):
        raise ValueError("the images have different sizes: %dx%d and %dx%d" % (
            len(first[0]) if first else 0, len(first), len(second[0]) if second else 0, len(second)))
    height = len(first)
    width = len(first[0]) if height else 0
    total = 0
    differing = 0
    channel_sum = 0
    worst = 0
    counted = 0
    for y in range(height):
        row_a, row_b = first[y], second[y]
        for x in range(width):
            if any(x0 <= x < x1 and y0 <= y < y1 for x0, y0, x1, y1 in ignore):
                continue
            a, b = row_a[x], row_b[x]
            largest = 0
            for channel in range(3):
                delta = abs(a[channel] - b[channel])
                channel_sum += delta
                if delta > largest:
                    largest = delta
            counted += 1
            total += 1
            if largest > pixel_tolerance:
                differing += 1
            if largest > worst:
                worst = largest
    if counted == 0:
        raise ValueError("every pixel is ignored")
    return {
        "width": width, "height": height, "pixels": counted,
        "mean": channel_sum / (counted * 3.0), "differing": differing / counted, "worst": worst,
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("first")
    parser.add_argument("second")
    parser.add_argument("--pixel-tolerance", type=int, default=12)
    parser.add_argument("--max-mean", type=float, default=3.0)
    parser.add_argument("--max-differing", type=float, default=0.02)
    parser.add_argument("--ignore", action="append", default=[], help="x0,y0,x1,y1 (right and bottom edges excluded)")
    parser.add_argument("--diff", type=pathlib.Path, help="write absolute RGB channel differences amplified four times")
    parser.add_argument("--report", type=pathlib.Path, help="write machine-readable result on success or failure")
    args = parser.parse_args(argv)
    if not (0 <= args.pixel_tolerance <= 255 and math.isfinite(args.max_mean) and 0 <= args.max_mean <= 255
            and math.isfinite(args.max_differing) and 0 <= args.max_differing <= 1):
        parser.error("tolerances must be finite: pixel/mean 0..255, differing fraction 0..1")
    ignore = []
    for text in args.ignore:
        parts = [int(value) for value in text.split(",")]
        if len(parts) != 4:
            parser.error("--ignore takes x0,y0,x1,y1")
        if not (0 <= parts[0] < parts[2] and 0 <= parts[1] < parts[3]):
            parser.error("--ignore requires nonnegative coordinates and positive area")
        ignore.append(tuple(parts))
    report = {"schema": "21kb.image-comparison/v1", "first": args.first, "second": args.second,
              "limits": {"pixel_tolerance": args.pixel_tolerance, "max_mean": args.max_mean, "max_differing": args.max_differing},
              "ignore": ignore, "diff_amplification": 4}
    try:
        first, second = read_png(args.first), read_png(args.second)
        stats = compare(first, second, args.pixel_tolerance, ignore)
        if args.diff:
            write_png(args.diff, [[tuple(min(255, abs(a[c] - b[c]) * 4) for c in range(3))
                                  for a, b in zip(row_a, row_b)] for row_a, row_b in zip(first, second)])
    except (ValueError, OSError, KeyError) as error:
        report.update(result="ERROR", error=str(error))
        if args.report: args.report.write_text(json.dumps(report, indent=2), encoding="utf-8")
        print("compare_images: %s" % error)
        return 2
    passed = stats["mean"] <= args.max_mean and stats["differing"] <= args.max_differing
    report.update(result="PASS" if passed else "FAIL", statistics=stats)
    if args.report: args.report.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print("compare_images: %dx%d, %d pixels, mean difference %.3f, %.2f%% of pixels differ by more than %d, worst %d" % (
        stats["width"], stats["height"], stats["pixels"], stats["mean"], stats["differing"] * 100.0, args.pixel_tolerance, stats["worst"]))
    if not passed:
        print("compare_images: FAILED (limits: mean %.3f, differing %.2f%%)" % (args.max_mean, args.max_differing * 100.0))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
