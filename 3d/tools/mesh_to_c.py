from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

import numpy as np

try:
    import trimesh
except ImportError:  # pragma: no cover - covered by the GUI error path
    trimesh = None

try:
    import fast_simplification
except ImportError:  # pragma: no cover - covered by the simplification error path
    fast_simplification = None


SUPPORTED_EXTENSIONS = {".obj", ".stl"}

AXIS_PRESETS = {
    "keep": (
        "Keep XYZ",
        np.eye(3, dtype=np.float64),
    ),
    "z_up_to_y_up": (
        "Z-up to Y-up",
        np.array(
            [
                [1.0, 0.0, 0.0],
                [0.0, 0.0, 1.0],
                [0.0, -1.0, 0.0],
            ],
            dtype=np.float64,
        ),
    ),
    "y_up_to_z_up": (
        "Y-up to Z-up",
        np.array(
            [
                [1.0, 0.0, 0.0],
                [0.0, 0.0, -1.0],
                [0.0, 1.0, 0.0],
            ],
            dtype=np.float64,
        ),
    ),
}


@dataclass(frozen=True)
class ConversionOptions:
    symbol: str
    axis_preset: str = "keep"
    merge_vertices: bool = True
    merge_digits: int = 6
    center: bool = True
    origin: tuple[float, float, float] | None = None
    normalize: bool = True
    target_extent: float = 2.0
    scale: float = 1.0
    simplify_ratio: float = 0.0
    flip_winding: bool = False
    include_vertex_normals: bool = False
    export_obj: bool = False
    float_precision: int = 6


@dataclass(frozen=True)
class SourceMesh:
    path: Path
    vertices: np.ndarray
    faces: np.ndarray


@dataclass(frozen=True)
class ConvertedMesh:
    source_path: Path
    vertices: np.ndarray
    faces: np.ndarray
    face_normals: np.ndarray
    vertex_normals: np.ndarray | None
    source_origin: np.ndarray
    simplified_faces_removed: int
    dropped_degenerate_faces: int
    applied_scale: float

    @property
    def bounds(self) -> np.ndarray:
        return np.vstack((self.vertices.min(axis=0), self.vertices.max(axis=0)))

    @property
    def extents(self) -> np.ndarray:
        return np.ptp(self.vertices, axis=0)


@dataclass(frozen=True)
class ExportResult:
    symbol: str
    c_path: Path
    h_path: Path
    obj_path: Path | None
    mesh: ConvertedMesh


def _require_trimesh() -> None:
    if trimesh is None:
        raise RuntimeError(
            "缺少 trimesh。请安装依赖："
            "python -m pip install -r requirements.txt"
        )


def sanitize_symbol(value: str) -> str:
    value = re.sub(r"[^A-Za-z0-9_]", "_", value.strip())
    value = re.sub(r"_+", "_", value).strip("_").lower()
    if not value:
        value = "model"
    if value[0].isdigit():
        value = f"model_{value}"
    return value


def load_source_mesh(
    path: str | Path,
    *,
    merge_vertices: bool = True,
    merge_digits: int = 6,
) -> SourceMesh:
    _require_trimesh()
    source_path = Path(path).expanduser().resolve()
    if not source_path.is_file():
        raise FileNotFoundError(source_path)
    if source_path.suffix.lower() not in SUPPORTED_EXTENSIONS:
        raise ValueError("仅支持 STL 和 OBJ 文件")
    if not 0 <= merge_digits <= 12:
        raise ValueError("合并精度位数必须在 0 到 12 之间")

    loaded = trimesh.load(
        source_path,
        force="mesh",
        process=False,
    )
    if not isinstance(loaded, trimesh.Trimesh):
        raise ValueError("所选文件不包含有效的三角网格")

    mesh = loaded.copy()
    if merge_vertices:
        mesh.merge_vertices(digits_vertex=merge_digits)
    mesh.remove_unreferenced_vertices()

    vertices = np.asarray(mesh.vertices, dtype=np.float64)
    faces = np.asarray(mesh.faces, dtype=np.int64)
    if vertices.ndim != 2 or vertices.shape[1] != 3 or len(vertices) == 0:
        raise ValueError("模型中没有有效的三维顶点")
    if faces.ndim != 2 or faces.shape[1] != 3 or len(faces) == 0:
        raise ValueError("模型中没有三角面")
    if not np.isfinite(vertices).all():
        raise ValueError("模型顶点包含 NaN 或无穷大坐标")

    return SourceMesh(
        path=source_path,
        vertices=np.ascontiguousarray(vertices.copy()),
        faces=np.ascontiguousarray(faces.copy()),
    )


def _normalized_vectors(vectors: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    lengths = np.linalg.norm(vectors, axis=1)
    valid = lengths > np.finfo(np.float64).eps
    normalized = np.zeros_like(vectors, dtype=np.float64)
    normalized[valid] = vectors[valid] / lengths[valid, None]
    return normalized, valid


def _build_vertex_normals(
    vertices: np.ndarray,
    faces: np.ndarray,
    face_cross: np.ndarray,
) -> np.ndarray:
    accumulated = np.zeros_like(vertices, dtype=np.float64)
    for column in range(3):
        np.add.at(accumulated, faces[:, column], face_cross)
    normals, _ = _normalized_vectors(accumulated)
    return normals


def _simplify_mesh(
    vertices: np.ndarray,
    faces: np.ndarray,
    simplify_ratio: float,
) -> tuple[np.ndarray, np.ndarray, int]:
    if simplify_ratio <= 0.0 or len(faces) <= 1:
        return vertices, faces, 0
    if fast_simplification is None:
        raise RuntimeError(
            "缺少模型简化组件 fast-simplification。请安装依赖："
            "python -m pip install -r requirements.txt"
        )

    target_face_count = max(1, round(len(faces) * (1.0 - simplify_ratio)))
    if target_face_count >= len(faces):
        return vertices, faces, 0

    simplified_vertices, simplified_faces = fast_simplification.simplify(
        points=np.asarray(vertices, dtype=np.float64),
        triangles=np.asarray(faces, dtype=np.int64),
        target_count=target_face_count,
    )
    simplified_vertices = np.asarray(simplified_vertices, dtype=np.float64)
    simplified_faces = np.asarray(simplified_faces, dtype=np.int64)
    if (
        simplified_vertices.ndim != 2
        or simplified_vertices.shape[1] != 3
        or len(simplified_vertices) == 0
        or simplified_faces.ndim != 2
        or simplified_faces.shape[1] != 3
        or len(simplified_faces) == 0
    ):
        raise ValueError("模型简化后没有得到有效的三角网格")

    removed_faces = len(faces) - len(simplified_faces)
    return (
        np.ascontiguousarray(simplified_vertices),
        np.ascontiguousarray(simplified_faces),
        removed_faces,
    )


def convert_mesh(source: SourceMesh, options: ConversionOptions) -> ConvertedMesh:
    if options.axis_preset not in AXIS_PRESETS:
        raise ValueError(f"未知的坐标系转换方式：{options.axis_preset}")
    if options.target_extent <= 0.0:
        raise ValueError("目标尺寸必须大于零")
    if options.scale <= 0.0:
        raise ValueError("附加缩放必须大于零")
    if not 0.0 <= options.simplify_ratio < 1.0:
        raise ValueError("模型简化率必须大于等于 0 且小于 100%")
    if not 1 <= options.float_precision <= 9:
        raise ValueError("小数精度必须在 1 到 9 之间")

    source_origin = None
    if options.origin is not None:
        source_origin = np.asarray(options.origin, dtype=np.float64)
        if source_origin.shape != (3,) or not np.isfinite(source_origin).all():
            raise ValueError("自定义零点必须包含三个有效坐标")

    matrix = AXIS_PRESETS[options.axis_preset][1]
    vertices = np.asarray(source.vertices @ matrix.T, dtype=np.float64)
    faces = source.faces.copy()

    if source_origin is not None:
        transformed_origin = source_origin @ matrix.T
    elif options.center:
        transformed_origin = (vertices.min(axis=0) + vertices.max(axis=0)) * 0.5
        source_origin = transformed_origin @ matrix
    else:
        transformed_origin = np.zeros(3, dtype=np.float64)
        source_origin = np.zeros(3, dtype=np.float64)
    vertices -= transformed_origin

    vertices, faces, simplified_faces = _simplify_mesh(
        vertices,
        faces,
        options.simplify_ratio,
    )

    applied_scale = options.scale
    if options.normalize:
        maximum_extent = float(np.ptp(vertices, axis=0).max())
        if maximum_extent <= np.finfo(np.float64).eps:
            raise ValueError("模型尺寸为零，无法归一化")
        applied_scale *= options.target_extent / maximum_extent
    vertices *= applied_scale

    if options.flip_winding:
        faces = faces[:, [0, 2, 1]]

    triangles = vertices[faces]
    face_cross = np.cross(
        triangles[:, 1] - triangles[:, 0],
        triangles[:, 2] - triangles[:, 0],
    )
    face_normals, valid_faces = _normalized_vectors(face_cross)
    dropped_faces = int((~valid_faces).sum())
    if dropped_faces:
        faces = faces[valid_faces]
        face_cross = face_cross[valid_faces]
        face_normals = face_normals[valid_faces]

    if len(faces) == 0:
        raise ValueError("转换后的所有三角面均已退化")

    referenced = np.zeros(len(vertices), dtype=bool)
    referenced[faces.reshape(-1)] = True
    if not referenced.all():
        remap = np.full(len(vertices), -1, dtype=np.int64)
        remap[referenced] = np.arange(int(referenced.sum()), dtype=np.int64)
        vertices = vertices[referenced]
        faces = remap[faces]

    if len(vertices) > 0xFFFF:
        raise ValueError(
            f"模型有 {len(vertices)} 个顶点；tri_t 最多支持 65535 个顶点"
        )
    if len(faces) > 0xFFFF:
        raise ValueError(
            f"模型有 {len(faces)} 个三角面；渲染器计数类型为 uint16_t"
        )
    if float(np.abs(vertices).max()) >= 32768.0:
        raise ValueError("顶点坐标超出有符号 Q16 的表示范围")

    vertex_normals = None
    if options.include_vertex_normals:
        vertex_normals = _build_vertex_normals(vertices, faces, face_cross)

    return ConvertedMesh(
        source_path=source.path,
        vertices=np.ascontiguousarray(vertices),
        faces=np.ascontiguousarray(faces, dtype=np.uint16),
        face_normals=np.ascontiguousarray(face_normals),
        vertex_normals=(
            None if vertex_normals is None else np.ascontiguousarray(vertex_normals)
        ),
        source_origin=np.ascontiguousarray(source_origin),
        simplified_faces_removed=simplified_faces,
        dropped_degenerate_faces=dropped_faces,
        applied_scale=applied_scale,
    )


def normals_to_q14(normals: np.ndarray) -> np.ndarray:
    values = np.rint(np.clip(normals, -1.0, 1.0) * 16384.0)
    return np.clip(values, -16384, 16384).astype(np.int16)


def _format_float(value: float, precision: int) -> str:
    threshold = 0.5 * (10.0 ** -precision)
    if abs(value) < threshold:
        value = 0.0
    return f"{value:.{precision}f}f"


def _format_vector_rows(rows: Iterable[Iterable[int]]) -> str:
    return "\n".join(
        f"    {{{int(x):6d}, {int(y):6d}, {int(z):6d}}}," for x, y, z in rows
    )


def generate_header_text(
    symbol: str,
    vertex_count: int,
    triangle_count: int,
    include_vertex_normals: bool,
) -> str:
    symbol = sanitize_symbol(symbol)
    macro = symbol.upper()
    guard = f"{macro}_MODEL_H"
    vertex_normals = ""
    if include_vertex_normals:
        vertex_normals = (
            f"extern const int16_t {symbol}_vertex_normals_q14"
            f"[{macro}_VERTEX_COUNT][3];\n"
        )

    return (
        f"#ifndef {guard}\n"
        f"#define {guard}\n\n"
        f"#include \"ThD_test.h\"\n\n"
        f"#define {macro}_VERTEX_COUNT    ({vertex_count}u)\n"
        f"#define {macro}_TRI_COUNT       ({triangle_count}u)\n\n"
        f"extern const Thd_point_t {symbol}_vertices[{macro}_VERTEX_COUNT];\n"
        f"extern const tri_t {symbol}_tris[{macro}_TRI_COUNT];\n"
        f"extern const int16_t {symbol}_face_normals_q14"
        f"[{macro}_TRI_COUNT][3];\n"
        f"{vertex_normals}\n"
        f"#endif\n"
    )


def generate_c_text(
    mesh: ConvertedMesh,
    symbol: str,
    header_name: str,
    *,
    float_precision: int,
) -> str:
    symbol = sanitize_symbol(symbol)
    origin_text = ", ".join(f"{float(value):.9g}" for value in mesh.source_origin)
    vertex_lines = []
    for x, y, z in mesh.vertices:
        vertex_lines.append(
            "    {Q16(%s), Q16(%s), Q16(%s)},"
            % (
                _format_float(float(x), float_precision),
                _format_float(float(y), float_precision),
                _format_float(float(z), float_precision),
            )
        )

    triangle_lines = [
        f"    {{{int(i0)}, {int(i1)}, {int(i2)}}}," for i0, i1, i2 in mesh.faces
    ]
    face_normal_lines = _format_vector_rows(normals_to_q14(mesh.face_normals))

    vertex_normal_block = ""
    if mesh.vertex_normals is not None:
        vertex_normal_lines = _format_vector_rows(normals_to_q14(mesh.vertex_normals))
        vertex_normal_block = (
            f"\nconst int16_t {symbol}_vertex_normals_q14[][3] = {{\n"
            f"{vertex_normal_lines}\n"
            f"}};\n"
        )

    return (
        f"/* Generated from {mesh.source_path.name} by mesh_to_c. */\n"
        f"/* Source origin shifted to zero: ({origin_text}). */\n"
        f"#include \"{header_name}\"\n\n"
        f"const Thd_point_t {symbol}_vertices[] = {{\n"
        f"{chr(10).join(vertex_lines)}\n"
        f"}};\n\n"
        f"const tri_t {symbol}_tris[] = {{\n"
        f"{chr(10).join(triangle_lines)}\n"
        f"}};\n\n"
        f"const int16_t {symbol}_face_normals_q14[][3] = {{\n"
        f"{face_normal_lines}\n"
        f"}};\n"
        f"{vertex_normal_block}"
    )


def write_outputs(
    source: SourceMesh,
    output_directory: str | Path,
    options: ConversionOptions,
) -> ExportResult:
    symbol = sanitize_symbol(options.symbol)
    mesh = convert_mesh(source, options)
    output_dir = Path(output_directory).expanduser().resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    c_path = output_dir / f"{symbol}.c"
    h_path = output_dir / f"{symbol}.h"
    header_text = generate_header_text(
        symbol,
        len(mesh.vertices),
        len(mesh.faces),
        options.include_vertex_normals,
    )
    c_text = generate_c_text(
        mesh,
        symbol,
        h_path.name,
        float_precision=options.float_precision,
    )

    h_path.write_text(header_text, encoding="ascii", newline="\n")
    c_path.write_text(c_text, encoding="ascii", newline="\n")

    obj_path = None
    if options.export_obj:
        _require_trimesh()
        obj_path = output_dir / f"{symbol}_processed.obj"
        export_mesh = trimesh.Trimesh(
            vertices=mesh.vertices,
            faces=mesh.faces,
            process=False,
        )
        export_mesh.export(obj_path)

    return ExportResult(
        symbol=symbol,
        c_path=c_path,
        h_path=h_path,
        obj_path=obj_path,
        mesh=mesh,
    )


def _build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Convert STL/OBJ meshes to C arrays")
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--symbol")
    parser.add_argument("--axis", choices=AXIS_PRESETS, default="keep")
    parser.add_argument("--no-merge", action="store_true")
    parser.add_argument("--merge-digits", type=int, default=6)
    parser.add_argument("--no-center", action="store_true")
    parser.add_argument("--origin", nargs=3, type=float, metavar=("X", "Y", "Z"))
    parser.add_argument("--no-normalize", action="store_true")
    parser.add_argument("--target-extent", type=float, default=2.0)
    parser.add_argument("--scale", type=float, default=1.0)
    parser.add_argument(
        "--simplify",
        type=float,
        default=0.0,
        metavar="PERCENT",
        help="percentage of triangle faces to remove",
    )
    parser.add_argument("--flip-winding", action="store_true")
    parser.add_argument("--vertex-normals", action="store_true")
    parser.add_argument("--export-obj", action="store_true")
    return parser


def main() -> None:
    parser = _build_argument_parser()
    args = parser.parse_args()
    symbol = sanitize_symbol(args.symbol or args.input.stem)
    options = ConversionOptions(
        symbol=symbol,
        axis_preset=args.axis,
        merge_vertices=not args.no_merge,
        merge_digits=args.merge_digits,
        center=not args.no_center,
        origin=None if args.origin is None else tuple(args.origin),
        normalize=not args.no_normalize,
        target_extent=args.target_extent,
        scale=args.scale,
        simplify_ratio=args.simplify / 100.0,
        flip_winding=args.flip_winding,
        include_vertex_normals=args.vertex_normals,
        export_obj=args.export_obj,
    )
    source = load_source_mesh(
        args.input,
        merge_vertices=options.merge_vertices,
        merge_digits=options.merge_digits,
    )
    result = write_outputs(source, args.output, options)
    print(f"vertices: {len(result.mesh.vertices)}")
    print(f"triangles: {len(result.mesh.faces)}")
    print(f"C: {result.c_path}")
    print(f"H: {result.h_path}")
    if result.obj_path is not None:
        print(f"OBJ: {result.obj_path}")


if __name__ == "__main__":
    main()
