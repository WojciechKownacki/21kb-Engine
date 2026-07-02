# Test scripts

Run from the repository root with PowerShell.

```powershell
.\tests\run-engine-tests.ps1
.\tests\run-editor-tests.ps1
.\tests\run-all-tests.ps1
.\tests\run-material-graph-release-gate.ps1 -BuildDir build/ci-fix -Config Debug -SkipConfigure
.\tests\run-regression.ps1
.\tests\run-render-smoke.ps1 -Renderer d3d11 -ExerciseWindowEvents -AutoExposureLuminance 0.18 -AutoExposureBias 0.0 -ScreenshotPath temp\render_smoke_d3d11.bmp
.\tests\run-render-smoke.ps1 -Renderer d3d11 -ExerciseWindowEvents -ForceGpuDrivenCpuFallback -ScreenshotPath temp\render_smoke_d3d11_gpu_fallback.bmp
```

`run-regression.ps1` uses a clean `build/regression` directory by default, then builds engine tests, editor tests, the editor executable, and runs CTest.

`run-render-smoke.ps1` is a Windows GPU smoke harness. It opens a real bgfx window, can force D3D11, exercises resize/minimize/restore, validates that the captured BMP is non-empty/non-flat, and checks GPU-driven runtime stats. `-ForceGpuDrivenCpuFallback` verifies the CPU fallback/parity path. `run-all-tests.ps1 -IncludeRenderSmoke` appends both GPU compute and forced CPU fallback smoke runs after the normal CTest suite.

`run-material-graph-release-gate.ps1` is the MAT-71 gate for Material Graph. It builds and runs renderer/editor tests, builds the editor executable, builds and runs `kb_standalone_player --self-test`, scans the Material Graph code surface for blocked placeholder tokens, and relies on the renderer test binary's D3D11 graph readback coverage for the required visual GPU proof. Pass `-IncludeWindowSmoke` to append the broader bgfx window smoke.
