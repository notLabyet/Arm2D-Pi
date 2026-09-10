# Repository Guidelines

## Arm-2D Scene Style

- Name each scene source/header pair after the scene name, for example `my_scene` uses `arm_2d_scene_my_scene.c` and `arm_2d_scene_my_scene.h`.
- Match the official Arm-2D scene style used by `project/mdk/RTE/Acceleration/arm_2d_scene_gas_gauge.c` and `project/mdk/RTE/Acceleration/arm_2d_scene_gas_gauge.h`.
- Keep scene symbols consistent with that style, including `user_scene_<name>_t`, `__arm_2d_scene_<name>_init`, `__on_scene_<name>_*`, `__pfb_draw_scene_<name>_handler`, include guards, OOC implementation macros, indentation, macro naming, and private/public member layout.

## Local Build Environment

- Keil MDK is installed at `D:\keil538`; use `D:\keil538\UV4\UV4.exe` for command-line builds.
