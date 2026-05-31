# Test scripts

Run from the repository root with PowerShell.

```powershell
.\tests\run-engine-tests.ps1
.\tests\run-editor-tests.ps1
.\tests\run-all-tests.ps1
.\tests\run-regression.ps1
```

`run-regression.ps1` uses a clean `build/regression` directory by default, then builds engine tests, editor tests, the editor executable, and runs CTest.
