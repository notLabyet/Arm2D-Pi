import tempfile
import unittest
from pathlib import Path

import numpy as np
import trimesh

from mesh_to_c import (
    ConversionOptions,
    SourceMesh,
    convert_mesh,
    load_source_mesh,
    normals_to_q14,
    sanitize_symbol,
    write_outputs,
)


class MeshToCTest(unittest.TestCase):
    def test_obj_and_stl_load_and_normalize(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            box = trimesh.creation.box(extents=(2.0, 3.0, 4.0))

            for suffix in (".obj", ".stl"):
                source_path = root / f"box{suffix}"
                box.export(source_path)
                source = load_source_mesh(source_path, merge_vertices=True)
                converted = convert_mesh(
                    source,
                    ConversionOptions(symbol="box", target_extent=2.0),
                )

                self.assertEqual(len(converted.vertices), 8)
                self.assertEqual(len(converted.faces), 12)
                self.assertAlmostEqual(float(converted.extents.max()), 2.0, places=7)
                np.testing.assert_allclose(
                    np.linalg.norm(converted.face_normals, axis=1),
                    np.ones(len(converted.faces)),
                    atol=1e-12,
                )

    def test_axis_conversion_and_winding(self) -> None:
        source = SourceMesh(
            path=Path("triangle.obj"),
            vertices=np.array(
                [[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0]],
                dtype=np.float64,
            ),
            faces=np.array([[0, 1, 2]], dtype=np.int64),
        )
        options = ConversionOptions(
            symbol="triangle",
            axis_preset="z_up_to_y_up",
            center=False,
            normalize=False,
        )
        converted = convert_mesh(source, options)
        np.testing.assert_allclose(converted.vertices[2], [0.0, 0.0, -1.0])

        flipped = convert_mesh(
            source,
            ConversionOptions(
                symbol="triangle",
                center=False,
                normalize=False,
                flip_winding=True,
            ),
        )
        np.testing.assert_allclose(
            flipped.face_normals[0],
            -np.array([0.0, 0.0, 1.0]),
        )

    def test_custom_origin_is_shifted_before_axis_and_scale(self) -> None:
        source = SourceMesh(
            path=Path("pointer.obj"),
            vertices=np.array(
                [
                    [10.0, 20.0, 30.0],
                    [11.0, 20.0, 30.0],
                    [10.0, 21.0, 30.0],
                ],
                dtype=np.float64,
            ),
            faces=np.array([[0, 1, 2]], dtype=np.int64),
        )
        converted = convert_mesh(
            source,
            ConversionOptions(
                symbol="pointer",
                axis_preset="z_up_to_y_up",
                origin=(10.0, 20.0, 30.0),
                normalize=False,
                scale=2.0,
            ),
        )

        np.testing.assert_allclose(converted.vertices[0], [0.0, 0.0, 0.0])
        np.testing.assert_allclose(converted.vertices[1], [2.0, 0.0, 0.0])
        np.testing.assert_allclose(converted.vertices[2], [0.0, 0.0, -2.0])
        np.testing.assert_allclose(converted.source_origin, [10.0, 20.0, 30.0])

    def test_simplification_reduces_faces_and_rebuilds_normals(self) -> None:
        sphere = trimesh.creation.icosphere(subdivisions=3)
        source = SourceMesh(
            path=Path("sphere.obj"),
            vertices=np.asarray(sphere.vertices, dtype=np.float64),
            faces=np.asarray(sphere.faces, dtype=np.int64),
        )
        converted = convert_mesh(
            source,
            ConversionOptions(
                symbol="sphere",
                simplify_ratio=0.75,
                include_vertex_normals=True,
            ),
        )

        self.assertEqual(len(converted.faces), 320)
        self.assertEqual(converted.simplified_faces_removed, 960)
        self.assertLess(len(converted.vertices), len(source.vertices))
        self.assertLess(int(converted.faces.max()), len(converted.vertices))
        np.testing.assert_allclose(
            np.linalg.norm(converted.face_normals, axis=1),
            np.ones(len(converted.faces)),
            atol=1e-12,
        )
        np.testing.assert_allclose(
            np.linalg.norm(converted.vertex_normals, axis=1),
            np.ones(len(converted.vertex_normals)),
            atol=1e-12,
        )
        self.assertAlmostEqual(float(converted.extents.max()), 2.0, places=7)

    def test_simplification_rate_validation(self) -> None:
        source = SourceMesh(
            path=Path("triangle.obj"),
            vertices=np.array(
                [[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0]],
                dtype=np.float64,
            ),
            faces=np.array([[0, 1, 2]], dtype=np.int64),
        )
        with self.assertRaisesRegex(ValueError, "简化率"):
            convert_mesh(
                source,
                ConversionOptions(symbol="triangle", simplify_ratio=1.0),
            )

    def test_write_c_header_normals_and_obj(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            source_path = root / "mesh.obj"
            trimesh.creation.icosphere(subdivisions=1).export(source_path)
            source = load_source_mesh(source_path)
            result = write_outputs(
                source,
                root / "generated",
                ConversionOptions(
                    symbol="Demo Mesh",
                    include_vertex_normals=True,
                    export_obj=True,
                ),
            )

            header = result.h_path.read_text(encoding="ascii")
            source_text = result.c_path.read_text(encoding="ascii")
            self.assertIn("DEMO_MESH_VERTEX_COUNT", header)
            self.assertIn("demo_mesh_face_normals_q14", header)
            self.assertIn("demo_mesh_vertex_normals_q14", header)
            self.assertIn("const Thd_point_t demo_mesh_vertices[]", source_text)
            self.assertIn("const tri_t demo_mesh_tris[]", source_text)
            self.assertIn("Source origin shifted to zero", source_text)
            self.assertTrue(result.obj_path.is_file())
            self.assertEqual(result.obj_path.name, "demo_mesh_processed.obj")

            q14 = normals_to_q14(result.mesh.face_normals).astype(np.int32)
            lengths = np.linalg.norm(q14, axis=1)
            self.assertLess(float(np.abs(lengths - 16384.0).max()), 1.5)

    def test_symbol_sanitization(self) -> None:
        self.assertEqual(sanitize_symbol("12 Demo-Mesh"), "model_12_demo_mesh")


if __name__ == "__main__":
    unittest.main()
