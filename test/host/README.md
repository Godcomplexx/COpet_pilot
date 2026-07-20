# Host unit tests

These tests run the pure logic modules from `main/modes/` on the development
machine — no board, no flashing. They exist so state-machine transitions can be
verified quickly and repeatably as part of the M5 architecture work.

Covered modules:

| Module | Test | What is checked |
|---|---|---|
| `core/copet_behavior.c` | `test_copet_behavior.c` | P0–P3 priority, interruption, Focus/Wi‑Fi sources, shake escalation, cooldown, repeat history, cancel and timer wrap-around |
| `modes/focus_mode.c` | `test_focus_mode.c` | ready/run/pause transitions, work→break→work, session count, remaining time preservation |
| `modes/menu_mode.c` | `test_menu_mode.c` | item layout, selection wrap in both directions, multi-step scroll |
| `modes/animation_mode.c` | `test_animation_mode.c` | frame interval gating, frame advance, wrap to first frame |
| `modes/desk_mode.c` | `test_desk_mode.c` | MPU6050 motion classification and labels |
| `modes/settings_mode.c` | `test_settings_mode.c` | sound setting initialization and toggle |

The logic modules include only `<stdbool.h>`/`<stdint.h>`/`<stddef.h>`, so they
compile with any host C compiler.

## Run

```powershell
powershell -File test/host/run_tests.ps1
```

The runner prefers `gcc`/`clang` on `PATH` and otherwise falls back to the
Visual Studio Build Tools (`vcvars64.bat` + `cl.exe`). Each suite prints its
check and failure counts and exits non-zero on any failure.

Build artifacts land in `test/host/build/` (git-ignored).
