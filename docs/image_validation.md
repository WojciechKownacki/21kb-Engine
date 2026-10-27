# Image input validation

The texture loader validates bitmap headers before calling the platform or bundled
image decoder. A BMP must contain its declared header and pixel offset. Its width
and absolute height must be nonzero and at most 16384 pixels. Unknown compression
codes and invalid RLE or bitfield bit-depth combinations fail the load.

For RGB and bitfield bitmaps, the source must contain every declared row, including
four-byte row padding. The check uses 64-bit arithmetic and handles negative heights
without signed overflow. The row layout follows Microsoft's
[bitmap header specification](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-bitmapinfoheader).
Compressed payloads remain subject to the format decoder's validation.

The resource-registry CLI tests decode complete RGB24 core-header bitmaps and
1/4/8/16/24/32-bit info/V4/V5 bitmaps, including top-down rows, and reject missing
headers, pixel rows and out-of-range offsets. Image fuzz corpus regressions retain
the short BMP inputs that caused decoder timeouts. Run `kb_renderer_tests
resource-registry` and the `kb_fuzz_image` corpus test with sanitizers enabled.
