"""Verify production WASM startup failures without claiming native or GPU acceptance."""
import shutil

from run_game_storage_tests import main
from run_game_wasm_loading_tests import WasmLoadingHandler, run_cases as loading_cases


def fixture(args, root):
    web = root / "Web"
    web.mkdir()
    for name in ["Game.html", "Game.js", "Game.wasm"]:
        shutil.copy2(args.directory / name, web / name)
    # Every case must fail before the runtime can request a package.
    (web / "Game.kbpack").write_bytes(b"")
    return web


def run_cases(args, root, report, origin):
    report["coverage"] = dict(bootstrapFailures=True, nativeRuntime=False, gpuRendering=False,
        package="Unused empty sentinel; requesting it fails the bootstrap check")
    loading_cases(args, root, report, origin, failures_only=True)


if __name__ == "__main__":
    raise SystemExit(main(fixture=fixture, scenario=run_cases, native_tools=False,
        http_handler_type=WasmLoadingHandler, schema="21kb.web-wasm-bootstrap-failures/v1",
        description=__doc__, prefix="WasmBootstrapFailures"))
