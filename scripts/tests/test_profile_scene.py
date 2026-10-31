"""Protect warmup exclusion, delayed GPU query accounting and incomplete captures."""
import csv
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("profile_scene", Path(__file__).parents[1] / "profile_scene.py")
profile = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profile)


class SceneProfileTests(unittest.TestCase):
    def fixture(self, root):
        path = root / "run.csv"
        fields = ["dropped", "missing_resources", "processing_wall_ms", "simulation_ms", "begin_submit_ms", "end_frame_ms", "scene_sync_ms"]
        fields += ["width", "height", "lighting_path", "draws", "submitted_meshes", "visible_meshes", "scene_lights"]
        with path.open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=fields)
            writer.writeheader()
            for time in (999, 1, 2, 3, 4):
                row = {field: 0 if field in fields[:2] else time for field in fields}
                row.update(width=64, height=64, lighting_path=3, draws=10, submitted_meshes=10, visible_meshes=10, scene_lights=3)
                writer.writerow(row)
        trace = []
        # bgfx frame ids need not start at zero. Query 100 belongs to warmup;
        # query 101 repeats and must contribute only one sample.
        for index, query in enumerate((None, 100, 101, 101, 102)):
            trace.append({"rendering": {"bgfx_frame": 100 + index, "gpu_frame": query,
                         "gpu_ms": None if query is None else {100: 999, 101: 2, 102: 4}[query],
                         "memory": {"mesh_payload_bytes": str(100 + index), "driver_gpu_used_bytes": None}}})
        Path(str(path) + ".trace.jsonl").write_text("\n".join(map(json.dumps, trace)), encoding="utf-8")
        return path

    def test_warmup_and_unique_delayed_gpu_queries(self):
        with tempfile.TemporaryDirectory() as directory:
            result = profile.summarize(self.fixture(Path(directory)), 1, 4)
        self.assertEqual(result["cpu_ms"]["begin_submit_ms"]["p50"], 2.5)
        self.assertAlmostEqual(result["cpu_ms"]["processing_wall_ms"]["p95"], 3.85)
        self.assertEqual(result["gpu_ms"]["samples"], 2)
        self.assertEqual(result["gpu_ms"]["p50"], 3)
        self.assertEqual(result["memory_peak_bytes"]["mesh_payload_bytes"], 104)
        self.assertEqual(result["observed_configuration"]["lighting_paths"], [3])
        self.assertIsNone(result["memory_peak_bytes"]["driver_gpu_used_bytes"])

    def test_incomplete_and_dropped_scene_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = self.fixture(Path(directory))
            with self.assertRaisesRegex(ValueError, "complete frames"):
                profile.summarize(path, 1, 5)
            text = path.read_text(encoding="utf-8").replace("0,0,1,", "1,0,1,")
            path.write_text(text, encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "dropped geometry"):
                profile.summarize(path, 1, 4)

    def test_unavailable_measurements_are_not_zero(self):
        result = profile.percentiles([None, float("nan"), float("inf")])
        self.assertEqual(result["samples"], 0)
        self.assertIsNone(result["p50"])


if __name__ == "__main__":
    unittest.main()
