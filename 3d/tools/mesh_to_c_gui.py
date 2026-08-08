from __future__ import annotations

import math
import threading
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

import numpy as np

from mesh_to_c import (
    AXIS_PRESETS,
    ConversionOptions,
    ConvertedMesh,
    SourceMesh,
    convert_mesh,
    load_source_mesh,
    sanitize_symbol,
    write_outputs,
)


APP_TITLE = "模型转 C 数组"

AXIS_LABELS = {
    "keep": "保持 XYZ",
    "z_up_to_y_up": "Z 轴向上转 Y 轴向上",
    "y_up_to_z_up": "Y 轴向上转 Z 轴向上",
}

ORIGIN_LABELS = {
    "keep": "保持模型原点",
    "center": "包围盒中心",
    "custom": "自定义坐标",
}


class WireframePreview(tk.Canvas):
    def __init__(self, master: tk.Misc) -> None:
        super().__init__(
            master,
            background="#202428",
            highlightthickness=0,
            width=720,
            height=420,
        )
        self._vertices: np.ndarray | None = None
        self._edges: np.ndarray | None = None
        self._yaw = -0.55
        self._pitch = 0.35
        self._zoom = 1.0
        self._drag_origin: tuple[int, int] | None = None
        self._render_pending = False

        self.bind("<Configure>", lambda _event: self.schedule_render())
        self.bind("<ButtonPress-1>", self._on_drag_start)
        self.bind("<B1-Motion>", self._on_drag)
        self.bind("<MouseWheel>", self._on_wheel)
        self.bind("<Button-4>", lambda _event: self._zoom_by(1.1))
        self.bind("<Button-5>", lambda _event: self._zoom_by(1.0 / 1.1))

    def clear_mesh(self) -> None:
        self._vertices = None
        self._edges = None
        self.schedule_render()

    def set_mesh(self, mesh: ConvertedMesh) -> None:
        vertices = np.asarray(mesh.vertices, dtype=np.float64)
        faces = np.asarray(mesh.faces, dtype=np.int64)
        if len(faces) > 3000:
            selection = np.linspace(0, len(faces) - 1, 3000, dtype=np.int64)
            faces = faces[selection]
        edges = np.vstack(
            (
                faces[:, [0, 1]],
                faces[:, [1, 2]],
                faces[:, [2, 0]],
            )
        )
        edges = np.unique(np.sort(edges, axis=1), axis=0)
        self._vertices = vertices
        self._edges = edges
        self._zoom = 1.0
        self.schedule_render()

    def schedule_render(self) -> None:
        if self._render_pending:
            return
        self._render_pending = True
        self.after_idle(self._render)

    def _on_drag_start(self, event: tk.Event) -> None:
        self._drag_origin = (event.x, event.y)

    def _on_drag(self, event: tk.Event) -> None:
        if self._drag_origin is None:
            return
        old_x, old_y = self._drag_origin
        self._yaw += (event.x - old_x) * 0.01
        self._pitch = max(
            -math.pi * 0.49,
            min(math.pi * 0.49, self._pitch + (event.y - old_y) * 0.01),
        )
        self._drag_origin = (event.x, event.y)
        self.schedule_render()

    def _on_wheel(self, event: tk.Event) -> None:
        self._zoom_by(1.1 if event.delta > 0 else 1.0 / 1.1)

    def _zoom_by(self, factor: float) -> None:
        self._zoom = max(0.2, min(8.0, self._zoom * factor))
        self.schedule_render()

    def _rotation_matrix(self) -> np.ndarray:
        cy, sy = math.cos(self._yaw), math.sin(self._yaw)
        cp, sp = math.cos(self._pitch), math.sin(self._pitch)
        yaw = np.array(
            [[cy, 0.0, sy], [0.0, 1.0, 0.0], [-sy, 0.0, cy]],
            dtype=np.float64,
        )
        pitch = np.array(
            [[1.0, 0.0, 0.0], [0.0, cp, -sp], [0.0, sp, cp]],
            dtype=np.float64,
        )
        return pitch @ yaw

    def _render(self) -> None:
        self._render_pending = False
        self.delete("all")
        width = max(1, self.winfo_width())
        height = max(1, self.winfo_height())

        if self._vertices is None or self._edges is None:
            self.create_text(
                width * 0.5,
                height * 0.5,
                text="尚未加载模型",
                fill="#aeb7bf",
                font=("Microsoft YaHei UI", 12),
            )
            return

        bounds_center = (
            self._vertices.min(axis=0) + self._vertices.max(axis=0)
        ) * 0.5
        rotation = self._rotation_matrix()
        rotated = (self._vertices - bounds_center) @ rotation.T
        rotated_origin = (-bounds_center) @ rotation.T
        fit_points = np.vstack((rotated[:, :2], rotated_origin[:2]))
        fit_min = fit_points.min(axis=0)
        fit_max = fit_points.max(axis=0)
        fit_center = (fit_min + fit_max) * 0.5
        span = max(float((fit_max - fit_min).max()), 1e-9)
        scale = min(width, height) * 0.78 * self._zoom / span
        screen = np.empty((len(rotated), 2), dtype=np.float64)
        screen[:, 0] = (rotated[:, 0] - fit_center[0]) * scale + width * 0.5
        screen[:, 1] = -(rotated[:, 1] - fit_center[1]) * scale + height * 0.5
        origin_screen = np.array(
            [
                (rotated_origin[0] - fit_center[0]) * scale + width * 0.5,
                -(rotated_origin[1] - fit_center[1]) * scale + height * 0.5,
            ]
        )

        edge_depth = rotated[self._edges].mean(axis=1)[:, 2]
        for edge_index in np.argsort(edge_depth):
            i0, i1 = self._edges[edge_index]
            self.create_line(
                float(screen[i0, 0]),
                float(screen[i0, 1]),
                float(screen[i1, 0]),
                float(screen[i1, 1]),
                fill="#79b7a2",
                width=1,
            )

        marker_size = 6
        self.create_line(
            origin_screen[0] - marker_size,
            origin_screen[1],
            origin_screen[0] + marker_size,
            origin_screen[1],
            fill="#f0b35a",
            width=2,
        )
        self.create_line(
            origin_screen[0],
            origin_screen[1] - marker_size,
            origin_screen[0],
            origin_screen[1] + marker_size,
            fill="#f0b35a",
            width=2,
        )
        self.create_text(
            origin_screen[0] + 9,
            origin_screen[1] - 9,
            text="零点",
            anchor="w",
            fill="#f0b35a",
            font=("Microsoft YaHei UI", 9, "bold"),
        )

        self._draw_axes(width, height)

    def _draw_axes(self, width: int, height: int) -> None:
        rotation = self._rotation_matrix()
        length = min(width, height) * 0.08
        origin = np.array([42.0, height - 42.0])
        axes = (
            (np.array([1.0, 0.0, 0.0]), "#e06c75", "X"),
            (np.array([0.0, 1.0, 0.0]), "#98c379", "Y"),
            (np.array([0.0, 0.0, 1.0]), "#61afef", "Z"),
        )
        for vector, colour, label in axes:
            projected = rotation @ vector
            end = origin + np.array([projected[0], -projected[1]]) * length
            self.create_line(*origin, *end, fill=colour, width=2)
            self.create_text(
                float(end[0]),
                float(end[1]),
                text=label,
                fill=colour,
                font=("Segoe UI", 9, "bold"),
            )


class MeshToCApp(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title(APP_TITLE)
        self.geometry("1180x860")
        self.minsize(1020, 680)

        self.source_var = tk.StringVar()
        self.output_var = tk.StringVar()
        self.symbol_var = tk.StringVar(value="model")
        self.axis_var = tk.StringVar(value=AXIS_LABELS["keep"])
        self.merge_var = tk.BooleanVar(value=True)
        self.merge_digits_var = tk.IntVar(value=6)
        self.origin_mode_var = tk.StringVar(value=ORIGIN_LABELS["center"])
        self.origin_x_var = tk.StringVar(value="0.0")
        self.origin_y_var = tk.StringVar(value="0.0")
        self.origin_z_var = tk.StringVar(value="0.0")
        self.normalize_var = tk.BooleanVar(value=True)
        self.target_extent_var = tk.StringVar(value="2.0")
        self.scale_var = tk.StringVar(value="1.0")
        self.simplify_percent_var = tk.DoubleVar(value=0.0)
        self.simplify_percent_label_var = tk.StringVar(value="0%")
        self.flip_winding_var = tk.BooleanVar(value=False)
        self.vertex_normals_var = tk.BooleanVar(value=False)
        self.export_obj_var = tk.BooleanVar(value=False)
        self.precision_var = tk.IntVar(value=6)
        self.status_var = tk.StringVar(value="就绪")
        self.source_stats_var = tk.StringVar(value="-")
        self.output_stats_var = tk.StringVar(value="-")
        self.extents_var = tk.StringVar(value="-")
        self.origin_stats_var = tk.StringVar(value="-")

        self._source_mesh: SourceMesh | None = None
        self._busy = False
        self._axis_label_to_key = {label: key for key, label in AXIS_LABELS.items()}
        self._origin_label_to_key = {
            label: key for key, label in ORIGIN_LABELS.items()
        }

        self._configure_style()
        self._build_menu()
        self._build_ui()
        self._update_origin_controls()
        self.bind_all("<Control-o>", lambda _event: self.browse_source())
        self.bind_all("<Control-e>", lambda _event: self.export_files())

    def _configure_style(self) -> None:
        style = ttk.Style(self)
        if "vista" in style.theme_names():
            style.theme_use("vista")
        style.configure("Toolbar.TButton", padding=(10, 5))
        style.configure("Primary.TButton", padding=(14, 7))

    def _build_menu(self) -> None:
        menu = tk.Menu(self)
        file_menu = tk.Menu(menu, tearoff=False)
        file_menu.add_command(label="打开模型", command=self.browse_source)
        file_menu.add_command(label="导出 C/H", command=self.export_files)
        file_menu.add_separator()
        file_menu.add_command(label="退出", command=self.destroy)
        menu.add_cascade(label="文件", menu=file_menu)
        self.configure(menu=menu)

    def _build_ui(self) -> None:
        root = ttk.Frame(self, padding=10)
        root.pack(fill=tk.BOTH, expand=True)

        file_frame = ttk.Frame(root)
        file_frame.pack(fill=tk.X, pady=(0, 8))
        file_frame.columnconfigure(1, weight=1)

        ttk.Label(file_frame, text="输入模型").grid(row=0, column=0, sticky="w")
        ttk.Entry(file_frame, textvariable=self.source_var).grid(
            row=0, column=1, sticky="ew", padx=8
        )
        ttk.Button(
            file_frame,
            text="浏览",
            command=self.browse_source,
            style="Toolbar.TButton",
        ).grid(row=0, column=2)
        self.load_button = ttk.Button(
            file_frame,
            text="加载预览",
            command=self.load_preview,
            style="Toolbar.TButton",
        )
        self.load_button.grid(row=0, column=3, padx=(8, 0))

        ttk.Label(file_frame, text="输出目录").grid(
            row=1, column=0, sticky="w", pady=(8, 0)
        )
        ttk.Entry(file_frame, textvariable=self.output_var).grid(
            row=1, column=1, sticky="ew", padx=8, pady=(8, 0)
        )
        ttk.Button(
            file_frame,
            text="浏览",
            command=self.browse_output,
            style="Toolbar.TButton",
        ).grid(row=1, column=2, pady=(8, 0))

        content = ttk.Panedwindow(root, orient=tk.HORIZONTAL)
        content.pack(fill=tk.BOTH, expand=True)

        preview_frame = ttk.Frame(content)
        settings_frame = ttk.Frame(content, width=340)
        content.add(preview_frame, weight=4)
        content.add(settings_frame, weight=0)

        self.preview = WireframePreview(preview_frame)
        self.preview.pack(fill=tk.BOTH, expand=True)

        settings_canvas = tk.Canvas(
            settings_frame,
            width=350,
            highlightthickness=0,
            borderwidth=0,
            background=self.cget("background"),
        )
        settings_scrollbar = ttk.Scrollbar(
            settings_frame,
            orient=tk.VERTICAL,
            command=settings_canvas.yview,
        )
        settings_canvas.configure(yscrollcommand=settings_scrollbar.set)
        settings_canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        settings_scrollbar.pack(side=tk.RIGHT, fill=tk.Y)

        settings_content = ttk.Frame(settings_canvas)
        settings_window = settings_canvas.create_window(
            (0, 0),
            window=settings_content,
            anchor="nw",
        )
        settings_content.bind(
            "<Configure>",
            lambda _event: settings_canvas.configure(
                scrollregion=settings_canvas.bbox("all")
            ),
        )
        settings_canvas.bind(
            "<Configure>",
            lambda event: settings_canvas.itemconfigure(
                settings_window,
                width=event.width,
            ),
        )

        self._build_settings(settings_content)

        log_frame = ttk.Frame(root)
        log_frame.pack(fill=tk.X, pady=(8, 0))
        self.log = tk.Text(
            log_frame,
            height=4,
            wrap="word",
            state="disabled",
            background="#f3f4f5",
            foreground="#24292f",
            relief="solid",
            borderwidth=1,
            font=("Consolas", 9),
        )
        self.log.pack(fill=tk.X)

        status_frame = ttk.Frame(root)
        status_frame.pack(fill=tk.X, pady=(6, 0))
        ttk.Label(status_frame, textvariable=self.status_var).pack(side=tk.LEFT)
        self.progress = ttk.Progressbar(status_frame, mode="indeterminate", length=160)
        self.progress.pack(side=tk.RIGHT)

    def _build_settings(self, parent: ttk.Frame) -> None:
        identity = ttk.LabelFrame(parent, text="输出标识", padding=10)
        identity.pack(fill=tk.X, padx=(10, 0), pady=(0, 8))
        identity.columnconfigure(1, weight=1)
        ttk.Label(identity, text="符号前缀").grid(row=0, column=0, sticky="w")
        ttk.Entry(identity, textvariable=self.symbol_var).grid(
            row=0, column=1, sticky="ew", padx=(8, 0)
        )

        transform = ttk.LabelFrame(parent, text="模型变换", padding=10)
        transform.pack(fill=tk.X, padx=(10, 0), pady=(0, 8))
        transform.columnconfigure(1, weight=1)

        ttk.Label(transform, text="坐标系转换").grid(row=0, column=0, sticky="w")
        axis_box = ttk.Combobox(
            transform,
            textvariable=self.axis_var,
            values=list(AXIS_LABELS.values()),
            state="readonly",
        )
        axis_box.grid(row=0, column=1, sticky="ew", padx=(8, 0))

        ttk.Checkbutton(
            transform, text="合并重复顶点", variable=self.merge_var
        ).grid(row=1, column=0, columnspan=2, sticky="w", pady=(8, 0))
        ttk.Label(transform, text="合并精度位数").grid(
            row=2, column=0, sticky="w", pady=(6, 0)
        )
        ttk.Spinbox(
            transform,
            from_=0,
            to=12,
            textvariable=self.merge_digits_var,
            width=8,
        ).grid(row=2, column=1, sticky="w", padx=(8, 0), pady=(6, 0))

        ttk.Label(transform, text="零点方式").grid(
            row=3, column=0, sticky="w", pady=(8, 0)
        )
        origin_mode_box = ttk.Combobox(
            transform,
            textvariable=self.origin_mode_var,
            values=list(ORIGIN_LABELS.values()),
            state="readonly",
        )
        origin_mode_box.grid(
            row=3, column=1, sticky="ew", padx=(8, 0), pady=(8, 0)
        )
        origin_mode_box.bind(
            "<<ComboboxSelected>>",
            lambda _event: self._update_origin_controls(),
        )

        ttk.Label(transform, text="自定义零点").grid(
            row=4, column=0, sticky="w", pady=(6, 0)
        )
        origin_values = ttk.Frame(transform)
        origin_values.grid(
            row=4, column=1, sticky="w", padx=(8, 0), pady=(6, 0)
        )
        self.origin_entries = []
        for column, (label, variable) in enumerate(
            (
                ("X", self.origin_x_var),
                ("Y", self.origin_y_var),
                ("Z", self.origin_z_var),
            )
        ):
            ttk.Label(origin_values, text=label).grid(row=0, column=column * 2)
            entry = ttk.Entry(origin_values, textvariable=variable, width=7)
            entry.grid(row=0, column=column * 2 + 1, padx=(2, 6))
            self.origin_entries.append(entry)

        origin_actions = ttk.Frame(transform)
        origin_actions.grid(
            row=5, column=0, columnspan=2, sticky="ew", pady=(6, 0)
        )
        ttk.Button(
            origin_actions,
            text="取模型中心",
            command=self._set_origin_from_model_center,
        ).pack(side=tk.LEFT)
        ttk.Button(
            origin_actions,
            text="重置为 0",
            command=self._reset_custom_origin,
        ).pack(side=tk.LEFT, padx=(6, 0))
        ttk.Button(
            origin_actions,
            text="应用预览",
            command=self.load_preview,
        ).pack(side=tk.RIGHT)

        ttk.Checkbutton(
            transform, text="按最大尺寸归一化", variable=self.normalize_var
        ).grid(row=6, column=0, columnspan=2, sticky="w", pady=(8, 0))

        ttk.Label(transform, text="目标尺寸").grid(
            row=7, column=0, sticky="w", pady=(6, 0)
        )
        ttk.Entry(transform, textvariable=self.target_extent_var, width=10).grid(
            row=7, column=1, sticky="w", padx=(8, 0), pady=(6, 0)
        )
        ttk.Label(transform, text="附加缩放").grid(
            row=8, column=0, sticky="w", pady=(6, 0)
        )
        ttk.Entry(transform, textvariable=self.scale_var, width=10).grid(
            row=8, column=1, sticky="w", padx=(8, 0), pady=(6, 0)
        )

        ttk.Label(transform, text="简化率（降低面数）").grid(
            row=9, column=0, sticky="w", pady=(8, 0)
        )
        simplify_controls = ttk.Frame(transform)
        simplify_controls.grid(
            row=9, column=1, sticky="ew", padx=(8, 0), pady=(8, 0)
        )
        simplify_controls.columnconfigure(0, weight=1)
        self.simplify_scale = ttk.Scale(
            simplify_controls,
            from_=0.0,
            to=95.0,
            variable=self.simplify_percent_var,
            command=self._on_simplify_changed,
        )
        self.simplify_scale.grid(row=0, column=0, sticky="ew")
        self.simplify_scale.bind(
            "<ButtonRelease-1>",
            lambda _event: self._apply_simplification_preview(),
        )
        ttk.Label(
            simplify_controls,
            textvariable=self.simplify_percent_label_var,
            width=5,
            anchor="e",
        ).grid(row=0, column=1, padx=(6, 0))

        ttk.Checkbutton(
            transform, text="翻转三角面绕序", variable=self.flip_winding_var
        ).grid(row=10, column=0, columnspan=2, sticky="w", pady=(8, 0))

        output = ttk.LabelFrame(parent, text="生成内容", padding=10)
        output.pack(fill=tk.X, padx=(10, 0), pady=(0, 8))
        output.columnconfigure(1, weight=1)
        ttk.Checkbutton(
            output,
            text="生成 Q14 顶点法线",
            variable=self.vertex_normals_var,
        ).grid(row=0, column=0, columnspan=2, sticky="w")
        ttk.Checkbutton(
            output,
            text="导出处理后的 OBJ",
            variable=self.export_obj_var,
        ).grid(row=1, column=0, columnspan=2, sticky="w", pady=(4, 0))
        ttk.Label(output, text="小数精度").grid(
            row=2, column=0, sticky="w", pady=(6, 0)
        )
        ttk.Spinbox(
            output,
            from_=1,
            to=9,
            textvariable=self.precision_var,
            width=8,
        ).grid(row=2, column=1, sticky="w", padx=(8, 0), pady=(6, 0))

        stats = ttk.LabelFrame(parent, text="模型统计", padding=10)
        stats.pack(fill=tk.X, padx=(10, 0), pady=(0, 8))
        stats.columnconfigure(1, weight=1)
        ttk.Label(stats, text="原始模型").grid(row=0, column=0, sticky="nw")
        ttk.Label(stats, textvariable=self.source_stats_var).grid(
            row=0, column=1, sticky="w", padx=(8, 0)
        )
        ttk.Label(stats, text="转换结果").grid(row=1, column=0, sticky="nw", pady=(4, 0))
        ttk.Label(stats, textvariable=self.output_stats_var).grid(
            row=1, column=1, sticky="w", padx=(8, 0), pady=(4, 0)
        )
        ttk.Label(stats, text="模型尺寸").grid(row=2, column=0, sticky="nw", pady=(4, 0))
        ttk.Label(stats, textvariable=self.extents_var).grid(
            row=2, column=1, sticky="w", padx=(8, 0), pady=(4, 0)
        )
        ttk.Label(stats, text="导出零点").grid(
            row=3, column=0, sticky="nw", pady=(4, 0)
        )
        ttk.Label(stats, textvariable=self.origin_stats_var).grid(
            row=3, column=1, sticky="w", padx=(8, 0), pady=(4, 0)
        )

        self.export_button = ttk.Button(
            parent,
            text="导出 C/H",
            command=self.export_files,
            style="Primary.TButton",
        )
        self.export_button.pack(fill=tk.X, padx=(10, 0))

    def browse_source(self) -> None:
        selected = filedialog.askopenfilename(
            title="打开模型",
            filetypes=(
                ("模型文件", "*.stl *.obj"),
                ("STL 文件", "*.stl"),
                ("OBJ 文件", "*.obj"),
                ("所有文件", "*.*"),
            ),
        )
        if not selected:
            return
        path = Path(selected)
        self._source_mesh = None
        self.source_var.set(str(path))
        self.output_var.set(str(path.parent))
        self.symbol_var.set(sanitize_symbol(path.stem))
        self.load_preview()

    def browse_output(self) -> None:
        selected = filedialog.askdirectory(title="选择输出目录")
        if selected:
            self.output_var.set(selected)

    def _update_origin_controls(self) -> None:
        origin_mode = self._origin_label_to_key.get(self.origin_mode_var.get())
        state = tk.NORMAL if origin_mode == "custom" else tk.DISABLED
        for entry in self.origin_entries:
            entry.configure(state=state)

    def _set_origin_from_model_center(self) -> None:
        if self._source_mesh is None:
            messagebox.showerror(APP_TITLE, "请先加载模型预览。")
            return
        center = (
            self._source_mesh.vertices.min(axis=0)
            + self._source_mesh.vertices.max(axis=0)
        ) * 0.5
        self.origin_mode_var.set(ORIGIN_LABELS["custom"])
        self.origin_x_var.set(f"{float(center[0]):.9g}")
        self.origin_y_var.set(f"{float(center[1]):.9g}")
        self.origin_z_var.set(f"{float(center[2]):.9g}")
        self._update_origin_controls()

    def _reset_custom_origin(self) -> None:
        self.origin_mode_var.set(ORIGIN_LABELS["custom"])
        self.origin_x_var.set("0.0")
        self.origin_y_var.set("0.0")
        self.origin_z_var.set("0.0")
        self._update_origin_controls()

    def _on_simplify_changed(self, value: str) -> None:
        percent = round(float(value))
        self.simplify_percent_label_var.set(f"{percent}%")

    def _apply_simplification_preview(self) -> None:
        if self._busy or not self.source_var.get().strip():
            return
        self.load_preview()

    def _options_from_ui(self) -> ConversionOptions:
        axis_key = self._axis_label_to_key.get(self.axis_var.get())
        if axis_key is None:
            raise ValueError("请选择有效的坐标系转换方式")
        origin_mode = self._origin_label_to_key.get(self.origin_mode_var.get())
        if origin_mode is None:
            raise ValueError("请选择有效的零点方式")
        try:
            merge_digits = int(self.merge_digits_var.get())
            target_extent = float(self.target_extent_var.get())
            scale = float(self.scale_var.get())
            simplify_ratio = round(float(self.simplify_percent_var.get())) / 100.0
            float_precision = int(self.precision_var.get())
            origin = None
            if origin_mode == "custom":
                origin = (
                    float(self.origin_x_var.get()),
                    float(self.origin_y_var.get()),
                    float(self.origin_z_var.get()),
                )
        except (ValueError, tk.TclError) as error:
            raise ValueError("请检查数值参数，必须填写有效数字") from error
        return ConversionOptions(
            symbol=sanitize_symbol(self.symbol_var.get()),
            axis_preset=axis_key,
            merge_vertices=self.merge_var.get(),
            merge_digits=merge_digits,
            center=origin_mode == "center",
            origin=origin,
            normalize=self.normalize_var.get(),
            target_extent=target_extent,
            scale=scale,
            simplify_ratio=simplify_ratio,
            flip_winding=self.flip_winding_var.get(),
            include_vertex_normals=self.vertex_normals_var.get(),
            export_obj=self.export_obj_var.get(),
            float_precision=float_precision,
        )

    def load_preview(self) -> None:
        if self._busy:
            return
        source_path = self.source_var.get().strip()
        if not source_path:
            messagebox.showerror(APP_TITLE, "请先选择 STL 或 OBJ 文件。")
            return
        try:
            options = self._options_from_ui()
        except (TypeError, ValueError) as error:
            messagebox.showerror(APP_TITLE, str(error))
            return

        def task():
            source = load_source_mesh(
                source_path,
                merge_vertices=options.merge_vertices,
                merge_digits=options.merge_digits,
            )
            return source, convert_mesh(source, options), options

        self._start_task("正在加载模型...", task, self._preview_loaded)

    def _preview_loaded(
        self,
        result: tuple[SourceMesh, ConvertedMesh, ConversionOptions],
    ) -> None:
        source, converted, options = result
        self._source_mesh = source
        self.symbol_var.set(sanitize_symbol(options.symbol))
        self.preview.set_mesh(converted)
        self._update_stats(source, converted)
        self._append_log(
            f"已加载 {source.path.name}：{len(converted.vertices)} 个顶点，"
            f"{len(converted.faces)} 个三角面"
            + (
                f"（简化移除 {converted.simplified_faces_removed} 面）"
                if converted.simplified_faces_removed
                else ""
            )
        )

    def export_files(self) -> None:
        if self._busy:
            return
        if not self.source_var.get().strip():
            messagebox.showerror(APP_TITLE, "请先选择 STL 或 OBJ 文件。")
            return
        if not self.output_var.get().strip():
            messagebox.showerror(APP_TITLE, "请先选择输出目录。")
            return

        try:
            options = self._options_from_ui()
        except (TypeError, ValueError) as error:
            messagebox.showerror(APP_TITLE, str(error))
            return

        output_dir = Path(self.output_var.get()).expanduser()
        source_path = self.source_var.get().strip()
        symbol = sanitize_symbol(options.symbol)
        output_paths = [output_dir / f"{symbol}.c", output_dir / f"{symbol}.h"]
        if options.export_obj:
            output_paths.append(output_dir / f"{symbol}_processed.obj")
        existing = [path for path in output_paths if path.exists()]
        if existing and not messagebox.askyesno(
            APP_TITLE,
            "输出文件已经存在，是否覆盖？",
        ):
            return

        def task():
            source = load_source_mesh(
                source_path,
                merge_vertices=options.merge_vertices,
                merge_digits=options.merge_digits,
            )
            result = write_outputs(source, output_dir, options)
            return source, result

        self._start_task("正在导出 C 数组...", task, self._export_complete)

    def _export_complete(self, result) -> None:
        source, exported = result
        self._source_mesh = source
        self.preview.set_mesh(exported.mesh)
        self._update_stats(source, exported.mesh)
        paths = [str(exported.c_path), str(exported.h_path)]
        if exported.obj_path is not None:
            paths.append(str(exported.obj_path))
        self._append_log("已导出：\n  " + "\n  ".join(paths))
        self.status_var.set(
            f"已导出 {len(exported.mesh.vertices)} 个顶点 / "
            f"{len(exported.mesh.faces)} 个三角面"
        )

    def _update_stats(self, source: SourceMesh, converted: ConvertedMesh) -> None:
        self.source_stats_var.set(
            f"{len(source.vertices)} 顶点 / {len(source.faces)} 三角面"
        )
        dropped = ""
        if converted.simplified_faces_removed:
            dropped += f" / 简化 -{converted.simplified_faces_removed} 面"
        if converted.dropped_degenerate_faces:
            dropped += f" / 移除 {converted.dropped_degenerate_faces} 退化面"
        self.output_stats_var.set(
            f"{len(converted.vertices)} 顶点 / "
            f"{len(converted.faces)} 三角面{dropped}"
        )
        extents = converted.extents
        self.extents_var.set(
            f"X {extents[0]:.5g} / Y {extents[1]:.5g} / Z {extents[2]:.5g}"
        )
        origin = converted.source_origin
        self.origin_stats_var.set(
            f"X {origin[0]:.6g} / Y {origin[1]:.6g} / Z {origin[2]:.6g}"
        )

    def _start_task(self, label: str, function, callback) -> None:
        self._set_busy(True, label)

        def worker() -> None:
            try:
                result = function()
            except Exception as error:
                self.after(0, lambda error=error: self._task_failed(error))
                return
            self.after(0, lambda: self._task_succeeded(callback, result))

        threading.Thread(target=worker, daemon=True).start()

    def _task_succeeded(self, callback, result) -> None:
        self._set_busy(False, "就绪")
        callback(result)

    def _task_failed(self, error: Exception) -> None:
        self._set_busy(False, "失败")
        self._append_log(f"错误：{error}")
        messagebox.showerror(APP_TITLE, str(error))

    def _set_busy(self, busy: bool, status: str) -> None:
        self._busy = busy
        self.status_var.set(status)
        state = tk.DISABLED if busy else tk.NORMAL
        self.load_button.configure(state=state)
        self.export_button.configure(state=state)
        self.simplify_scale.configure(state=state)
        if busy:
            self.progress.start(10)
        else:
            self.progress.stop()

    def _append_log(self, text: str) -> None:
        self.log.configure(state="normal")
        self.log.insert(tk.END, text.rstrip() + "\n")
        self.log.see(tk.END)
        self.log.configure(state="disabled")


def main() -> None:
    app = MeshToCApp()
    app.mainloop()


if __name__ == "__main__":
    main()
