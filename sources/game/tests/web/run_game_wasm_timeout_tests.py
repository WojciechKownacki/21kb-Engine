"""Check the real WASM bootstrap idle deadline before native runtime startup."""
from run_game_download_timeout_tests import TimeoutHandler, run_cases as download_cases
from run_game_storage_delete_tests import author_fixture
from run_game_storage_tests import main


class WasmTimeoutHandler(TimeoutHandler):
    resource_name = "Game.wasm"


def run_cases(args, root, report, origin):
    download_cases(args, root, report, origin, resource_name="Game.wasm", error_code="wasm-timeout")


if __name__ == "__main__":
    raise SystemExit(main(fixture=author_fixture, scenario=run_cases, http_handler_type=WasmTimeoutHandler,
        schema="21kb.web-game-wasm-timeout/v1", description=__doc__, prefix="WasmTimeout"))
