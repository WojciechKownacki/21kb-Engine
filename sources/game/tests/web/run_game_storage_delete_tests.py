"""Verify native slot deletion through a cooked game and real browser restarts."""
from pathlib import Path

from run_game_storage_tests import BrowserSession, author_fixture as author_storage_fixture, main
from run_game_touch_tests import require


def author_fixture(args, root):
    return author_storage_fixture(args, root, script_name="StorageDelete",
        script_source=Path(__file__).with_name("fixtures") / "StorageDelete.lua")


def verify(value, phase, status="Clean"):
    expected = [
        (False, False, False, "", "", ""),
        (True, True, True, "first", "second", "settings"),
        (False, True, True, "", "second", "settings"),
        (False, True, False, "", "second", ""),
        (True, True, False, "recreated", "second", ""),
    ][phase]
    observed = tuple(value[name] for name in
        ["first", "second", "settings", "firstValue", "secondValue", "settingsValue"])
    require(value["phase"] == phase and observed == expected and value["status"] == status,
        "Native slot contents differ at phase " + str(phase) + ": " + str(value))


def run_cases(args, root, report, origin):
    profile = root / "profile"
    for phase in range(5):
        if phase in [1, 2]:
            with BrowserSession(args, root, profile, report, origin, "abort-" + str(phase)) as case:
                verify(case.ready(), phase)
                case.client.evaluate("window.kbDeletionAborts=0;"
                    "window.kbDeletionOriginalTransaction=IDBDatabase.prototype.transaction;"
                    "IDBDatabase.prototype.transaction=function(...args){"
                    "const tx=kbDeletionOriginalTransaction.apply(this,args);"
                    "if(args[1]==='readwrite'){"
                    "tx.addEventListener('abort',()=>++kbDeletionAborts);"
                    "queueMicrotask(()=>tx.abort());}return tx;}")
                cursor = len(case.states())
                case.click_save()
                failed = case.wait("interrupted native deletion reports failure", lambda state:
                    state["phase"] == phase + 1 and state["status"] == "Failed", cursor)
                verify(failed, phase + 1, status="Failed")
                require(failed["deleted"] and case.client.evaluate("kbDeletionAborts") == 1,
                    "The deletion did not abort one real browser transaction: " + str(failed))
                require(not any(state["phase"] == phase + 1 and state["status"] == "Clean"
                    for state in case.states()[cursor:]), "An aborted deletion reported Clean")
                report.setdefault("graphics", []).append(dict(phase=phase, aborted=True, context=case.graphics()))
                report["observations"].append(dict(check="real deletion transaction abort preserves committed files",
                    phase=phase, workingState=failed, abortedTransactions=1))
        with BrowserSession(args, root, profile, report, origin, "phase-" + str(phase)) as case:
            value = case.ready()
            verify(value, phase)
            report["observations"].append(dict(check="restart reads exact native slots", phase=phase, state=value))
            report.setdefault("graphics", []).append(dict(phase=phase, context=case.graphics()))
            if phase in [0, 4]:
                case.blue_button("slot-button-" + str(phase))
            if phase == 4:
                continue
            cursor = len(case.states())
            case.click_save()
            value = case.wait("native slot mutation commits", lambda state:
                state["phase"] == phase + 1 and state["started"] and state["status"] == "Clean", cursor)
            verify(value, phase + 1)
            if phase in [1, 2]:
                require(value["deleted"] and value["missing"] and value["refused"],
                    "Deletion, missing slot or invalid name did not use the native API: " + str(value))
            report["observations"].append(dict(check="native button mutation commits", phase=phase, state=value))


if __name__ == "__main__":
    raise SystemExit(main(fixture=author_fixture, scenario=run_cases,
        schema="21kb.web-game-storage-delete/v1", description=__doc__, prefix="StorageDelete"))
