"""Exercise real parallel Ninja staging with shared and separate output paths."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


MODULE = Path(__file__).resolve().parents[2] / "CMake/SerializeParticleRuntimeStaging.cmake"


class RuntimeStagingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cmake = shutil.which(os.environ.get("CMAKE_COMMAND", "cmake"))
        cls.ninja = shutil.which("ninja")
        if not cls.cmake or not cls.ninja:
            raise RuntimeError("Runtime staging tests require CMake 3.27+ and Ninja on PATH")

    def exercise(self, *, serialize, separate_outputs=False):
        with tempfile.TemporaryDirectory(prefix="Runtime staging ") as temporary:
            root = Path(temporary)
            build = root / "build"
            work = build / "work"
            work.mkdir(parents=True)
            payload = b"runtime staging fixture\x00\xff"
            (work / "payload.bin").write_bytes(payload)
            (root / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.27)
project(RuntimeStaging NONE)
include("''' + MODULE.as_posix() + '''")
foreach(index RANGE 0 2)
    set(destination "${CMAKE_BINARY_DIR}/output.bin")
    if(SEPARATE_OUTPUTS)
        set(destination "${CMAKE_BINARY_DIR}/consumer${index}/output.bin")
    endif()
    add_custom_target(stage_${index}
        COMMAND "${CMAKE_COMMAND}" "-DNAME=${index}" "-DDEST=${destination}"
            "-DWORK=${CMAKE_BINARY_DIR}/work" -P "${CMAKE_SOURCE_DIR}/worker.cmake"
        VERBATIM)
    if(SERIALIZE)
        kb_serialize_particle_runtime_staging(stage_${index})
    endif()
endforeach()
if(SERIALIZE)
    # Registering an existing consumer again must not introduce a cycle.
    kb_serialize_particle_runtime_staging(stage_0)
endif()
''', encoding="utf-8")
            (root / "worker.cmake").write_text('''file(LOCK "${WORK}/copy.lock" GUARD PROCESS TIMEOUT 0 RESULT_VARIABLE result)
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "runtime staging overlap")
endif()
file(APPEND "${WORK}/events.txt" "${NAME}:start\\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 0.2 COMMAND_ERROR_IS_FATAL ANY)
get_filename_component(directory "${DEST}" DIRECTORY)
file(MAKE_DIRECTORY "${directory}")
file(COPY_FILE "${WORK}/payload.bin" "${DEST}" ONLY_IF_DIFFERENT)
file(APPEND "${WORK}/events.txt" "${NAME}:end\\n")
''', encoding="utf-8")

            def run(*args):
                return subprocess.run([self.cmake, *map(str, args)], capture_output=True,
                    text=True, encoding="utf-8", errors="replace", timeout=45,
                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))

            configured = run("-S", root, "-B", build, "-G", "Ninja",
                "-DCMAKE_MAKE_PROGRAM=" + self.ninja,
                "-DSERIALIZE=" + ("ON" if serialize else "OFF"),
                "-DSEPARATE_OUTPUTS=" + ("ON" if separate_outputs else "OFF"))
            self.assertEqual(configured.returncode, 0, configured.stdout + configured.stderr)
            built = run("--build", build, "--target", "stage_0", "stage_1", "stage_2", "--parallel", "3")
            if not serialize:
                self.assertNotEqual(built.returncode, 0, "Concurrent staging negative control unexpectedly passed")
                self.assertIn("runtime staging overlap", built.stdout + built.stderr)
                return
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            events = (work / "events.txt").read_text(encoding="utf-8").splitlines()
            self.assertEqual(len(events), 6, events)
            self.assertEqual(sorted(events[::2]), ["0:start", "1:start", "2:start"])
            for start, end in zip(events[::2], events[1::2]):
                self.assertEqual(start.replace(":start", ":end"), end)
            destinations = [build / f"consumer{index}/output.bin" for index in range(3)] if separate_outputs else [build / "output.bin"]
            for destination in destinations:
                self.assertEqual(destination.read_bytes(), payload)

    def test_shared_output_is_serialized(self):
        self.exercise(serialize=True)

    def test_custom_outputs_are_preserved(self):
        self.exercise(serialize=True, separate_outputs=True)

    def test_negative_control_detects_parallel_copies(self):
        self.exercise(serialize=False)


if __name__ == "__main__":
    unittest.main()
