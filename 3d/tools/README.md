# Mesh to C Arrays

Desktop converter for STL/OBJ meshes used by the RP2040 Arm-2D 3D renderer.

## Install

```powershell
python -m pip install -r requirements.txt
```

## Run

Double-click `run_mesh_to_c_gui.bat`, or run:

```powershell
python mesh_to_c_gui.py
```

The tool can also run without the UI:

```powershell
python mesh_to_c.py input.stl output_folder --symbol demo --export-obj
```

## Build Windows EXE

Double-click `build_mesh_to_c_exe.bat`, or run it from a terminal. The script
keeps its isolated build dependencies under `%TEMP%\mesh_to_c_build_deps`, so
optional packages from the system Python installation are not bundled. The
single-file GUI application is written to `dist\MeshToC.exe`.

## Generated data

For a symbol named `demo`, the generated `demo.c` and `demo.h` contain:

```c
demo_vertices
demo_tris
demo_face_normals_q14
```

Optional vertex normals are emitted as `demo_vertex_normals_q14`.
When enabled, the processed mesh is exported as `demo_processed.obj`.

The model instance can reference the arrays directly:

```c
{
    .ptVertices = demo_vertices,
    .ptTris = demo_tris,
    .pi16FaceNormalsQ14 = demo_face_normals_q14,
    .hwVertexCount = DEMO_VERTEX_COUNT,
    .hwTriCount = DEMO_TRI_COUNT,
}
```

The default transform centers the model and scales its longest dimension to
`2.0`, which matches the current renderer's approximate `[-1, 1]` model space.

## Simplifying the model

Use the Chinese GUI's simplification slider to reduce the triangle count from
0% to 95%. Releasing the slider refreshes the preview. The exporter keeps the
chosen origin stable, simplifies the transformed mesh, applies the requested
target size, and rebuilds face and optional vertex normals before writing the C
arrays.

The command-line tool exposes the same setting. For example, remove roughly
75% of the input triangles with:

```powershell
python mesh_to_c.py input.stl output_folder --simplify 75
```

## Adjusting the model origin

The Chinese GUI provides three origin modes:

- Keep the STL/OBJ origin.
- Move the bounding-box center to the origin (default).
- Enter a custom X/Y/Z origin in the original STL/OBJ coordinate system.

For a dial pointer, enter the shaft position as the custom origin. The exporter
subtracts that point before axis conversion and scaling, so the shaft becomes
`(0, 0, 0)` in the generated C array. The preview marks the exported origin with
an orange cross, and the processed OBJ uses the same adjusted coordinates.
