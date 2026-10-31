"""A regression capture cannot pass through a missing renderer or bad frame."""
import csv
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("render_regression", Path(__file__).parents[1] / "render_regression.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
from png_readback import write_png


class CaptureValidationTests(unittest.TestCase):
    def fixture(self, root, backend="Direct3D 11", missing=0, draws=1, uniform=False):
        profile, image = root / "frames.csv", root / "frame.png"
        Path(str(profile) + ".meta.json").write_text(json.dumps({"backend":backend,"width":64,"height":64}))
        with profile.open("w",newline="") as output:
            writer=csv.writer(output)
            writer.writerow(["missing_resources","dropped","draws","submitted_meshes"])
            writer.writerow([missing,0,draws,draws])
        write_png(image, [[(0,0,0) if uniform else (x*4,y*4,0) for x in range(64)] for y in range(64)])
        return profile,image

    def test_actual_draw_and_image_pass(self):
        with tempfile.TemporaryDirectory() as name:
            profile,image=self.fixture(Path(name))
            metadata,colors=module.validate_capture(profile,image,"d3d11",64,64,1)
            self.assertEqual(metadata["backend"],"Direct3D 11")
            self.assertGreater(colors,16)

    def test_missing_or_wrong_renderer_and_blank_capture_fail(self):
        for options in ({"backend":"Noop"},{"missing":1},{"draws":0},{"uniform":True}):
            with self.subTest(options=options), tempfile.TemporaryDirectory() as name:
                profile,image=self.fixture(Path(name),**options)
                with self.assertRaises(ValueError):
                    module.validate_capture(profile,image,"d3d11",64,64,1)

    def test_incomplete_trace_fails(self):
        with tempfile.TemporaryDirectory() as name:
            profile,image=self.fixture(Path(name))
            with self.assertRaises(ValueError):
                module.validate_capture(profile,image,"d3d11",64,64,2)

    def test_opengl_version_label_is_accepted_but_es_or_noop_are_not(self):
        self.assertTrue(module.backend_matches("opengl","OpenGL 4.3"))
        self.assertFalse(module.backend_matches("opengl","OpenGL ES 3.0"))
        self.assertFalse(module.backend_matches("opengl","Noop"))


if __name__=="__main__": unittest.main()
