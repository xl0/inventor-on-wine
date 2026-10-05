# 192 Two side observations from 177's review: opengl32:opengl with GLX (UseEGL=N) dies of a fatal X error; lockstress / presentchurn on NVIDIA end with exit code 137 after DONE
Status: draft, not worked on · Found in: review of 177 (inst/177-review/, 2026-10-05) · on both builds (integ cffd27540ee
= build-next, and with 177's fix), so not 177 · observations only, no cause

## 1. opengl32:opengl with the GLX backend ends in a fatal X error
With `UseEGL=N` (winex11 uses GLX instead of the default EGL) the opengl32:opengl conformance unit does not finish:
- NVIDIA Xorg (:101, libGLX_nvidia 595.91): `X Error of failed request: BadMatch`, major opcode 152 (GLX),
  X_GLXMakeContextCurrent, then the process hangs in exit inside the NVIDIA library
  (`inst/177-review/out/gpu-ogl-glx-base.out`, `gpu-ogl-glx-fix.out`).
- Mesa on Xvfb: `X Error of failed request: BadMatch`, major opcode 130 (MIT-SHM), X_ShmPutImage
  (`inst/177-review/out/mesa-glx-base-1.out`, `mesa-glx-fix-1.out`, ...).
BadMatch is not among the errors winex11 ignores on gdi_display, and nothing expects it there. The default (EGL)
passes (same counts as the regress baseline). Open: which test, which drawable / visual pair, whether upstream
master has it.

## 2. lockstress and presentchurn on NVIDIA print DONE and then often exit with 137
`inst/173/lockstress.exe` (EGL) and `inst/182-review/presentchurn.exe` (Vulkan) on :101 print their DONE line and the
process then ends with exit code 137 (128 + SIGKILL) instead of 0, on both builds
(`inst/177-review/out/RESULT-gpu.txt`: gpu-egl-ls-*, gpu-vk-churn-*). The runner (`inst/177-review/gpu.sh`) only
kills runs that outlive its timeout and reports those as HANG, so the kill comes from somewhere else. Open: who
sends it (process exit path with other threads still in the GL / Vulkan driver?).
