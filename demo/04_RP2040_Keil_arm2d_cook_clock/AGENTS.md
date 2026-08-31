# Repository Guidelines

## Arm-2D Scene Style

- Use the [Arm-2D official repository](https://github.com/ARM-software/Arm-2D) as the primary reference for UI effects, controls, and scene implementations.
- Name each scene source/header pair after the scene name, for example `my_scene` uses `arm_2d_scene_my_scene.c` and `arm_2d_scene_my_scene.h`.
- Match the official Arm-2D scene style used by `project/mdk/RTE/Acceleration/arm_2d_scene_gas_gauge.c` and `project/mdk/RTE/Acceleration/arm_2d_scene_gas_gauge.h`.
- Keep scene symbols consistent with that style, including `user_scene_<name>_t`, `__arm_2d_scene_<name>_init`, `__on_scene_<name>_*`, `__pfb_draw_scene_<name>_handler`, include guards, OOC implementation macros, indentation, macro naming, and private/public member layout.

## Reference Projects

- `demo1` refers to the primary reference project at `../01_RP2040_Keil-arm2d_flip`.

## Local Build Environment

- Prefer the local Keil MDK at `D:\Keil_v5`; use `D:\Keil_v5\UV4\UV4.exe` for command-line builds.
- Keep compatibility with a second Keil MDK installation at `E:\Keil_v5`; when it exists, use `E:\Keil_v5\UV4\UV4.exe` without changing project files.
