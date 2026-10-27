"""Compare two PNG captures of the same scene, for the editor-versus-package image checks.

    python compare_images.py editor.png game.png [--pixel-tolerance 12] [--max-mean 3.0]
        [--max-differing 0.02] [--ignore x0,y0,x1,y1 ...]

The images must have the same size. A pixel differs when any of its channels differs by more than --pixel-tolerance (0 to 255).
The check fails when the mean absolute channel difference is over --max-mean or when more than --max-differing of the pixels
(outside the --ignore rectangles) differ. The numbers are printed either way, so a limit can be set from a real run.
"""
import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from png_readback import read_png  # noqa: E402


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
    args = parser.parse_args(argv)
    ignore = []
    for text in args.ignore:
        parts = [int(value) for value in text.split(",")]
        if len(parts) != 4:
            parser.error("--ignore takes x0,y0,x1,y1")
        ignore.append(tuple(parts))
    try:
        stats = compare(read_png(args.first), read_png(args.second), args.pixel_tolerance, ignore)
    except (ValueError, OSError, KeyError) as error:
        print("compare_images: %s" % error)
        return 2
    print("compare_images: %dx%d, %d pixels, mean difference %.3f, %.2f%% of pixels differ by more than %d, worst %d" % (
        stats["width"], stats["height"], stats["pixels"], stats["mean"], stats["differing"] * 100.0, args.pixel_tolerance, stats["worst"]))
    if stats["mean"] > args.max_mean or stats["differing"] > args.max_differing:
        print("compare_images: FAILED (limits: mean %.3f, differing %.2f%%)" % (args.max_mean, args.max_differing * 100.0))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
