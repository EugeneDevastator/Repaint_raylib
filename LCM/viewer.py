import os
import tkinter as tk
from tkinter import ttk
from PIL import Image, ImageTk, ImageDraw
import hashlib

IMAGE_DIR = "images"
CACHE_DIR = ".thumb_cache"
THUMB_SIZE = 150

RENAMES = {
    "n": "steps",
    "cfg": "g",
    "s": "str"
}

KNOWN_PARAMS = ["str", "g", "res", "steps", "loops"]

DEFAULTS = {
    "str": 1.0,
    "g": 7,
    "res": 512,
    "steps": 4,
    "loops": 1
}

BG = "#ffffff"
BG2 = "#f0f0f0"
BG3 = "#e0e0e0"
BG_DISABLED = "#e8e8e8"
FG = "#111111"
FG2 = "#333333"
FG_DISABLED = "#aaaaaa"
ACCENT = "#2266cc"
FONT = ("Arial", 12)
FONT_BOLD = ("Arial", 12, "bold")
FONT_HEADER = ("Arial", 11, "bold")


def parse_filename(filename):
    name = os.path.splitext(filename)[0]
    parts = name.split("_")
    params = {}
    for part in parts:
        part = part.replace("-", ".")
        all_keys = list(RENAMES.keys()) + KNOWN_PARAMS
        all_keys.sort(key=len, reverse=True)
        for key in all_keys:
            if part.startswith(key):
                val_str = part[len(key):]
                try:
                    val = float(val_str) # if "." in val_str else int(val_str)
                    final_key = RENAMES.get(key, key)
                    params[final_key] = val
                    break
                except ValueError:
                    pass
    for k, v in DEFAULTS.items():
        if k not in params:
            params[k] = v
    return params


def load_database():
    db = []
    if not os.path.exists(IMAGE_DIR):
        return db
    for fname in os.listdir(IMAGE_DIR):
        if fname.lower().endswith(".png"):
            params = parse_filename(fname)
            if params:
                db.append({"file": os.path.join(IMAGE_DIR, fname), "params": params})
    return db


def get_thumb_path(image_path):
    os.makedirs(CACHE_DIR, exist_ok=True)
    h = hashlib.md5(image_path.encode()).hexdigest()
    return os.path.join(CACHE_DIR, f"{h}_{THUMB_SIZE}.png")


def build_thumbnail(image_path):
    thumb_path = get_thumb_path(image_path)
    if os.path.exists(thumb_path):
        if os.path.getmtime(thumb_path) >= os.path.getmtime(image_path):
            return thumb_path
    img = Image.open(image_path).resize((THUMB_SIZE, THUMB_SIZE), Image.LANCZOS)
    img.save(thumb_path, "PNG")
    return thumb_path


class SplashProgress(tk.Toplevel):
    def __init__(self, parent, total):
        super().__init__(parent)
        self.title("Loading thumbnails...")
        self.resizable(False, False)
        self.configure(bg=BG)
        self.total = total
        w, h = 400, 110
        self.geometry(f"{w}x{h}+{(self.winfo_screenwidth()-w)//2}+{(self.winfo_screenheight()-h)//2}")
        self.grab_set()
        tk.Label(self, text="Building thumbnail cache...", bg=BG, fg=FG,
                 font=FONT_BOLD).pack(pady=(18, 6))
        self.bar = ttk.Progressbar(self, length=340, maximum=total, mode="determinate")
        self.bar.pack(pady=4)
        self.lbl = tk.Label(self, text=f"0 / {total}", bg=BG, fg=FG2, font=FONT)
        self.lbl.pack()

    def update_progress(self, done):
        self.bar["value"] = done
        self.lbl.config(text=f"{done} / {self.total}")
        self.update()


class ImageBrowser(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Image Parameter Browser")
        self.state("zoomed")
        self.configure(bg=BG)
        self.withdraw()

        self.db = load_database()
        self.all_params = self._collect_all_params()

        # --- Fast lookup index: frozenset of (k,v) pairs -> entry ---
        self._index = {}
        for entry in self.db:
            key = frozenset(entry["params"].items())
            self._index[key] = entry

        self.x_var = tk.StringVar()
        self.y_var = tk.StringVar()
        self.slider_vars = {}
        self.slider_values = {}
        self.slider_widgets = {}   # param -> {"frame", "slider", "val_label"}
        self.cell_size = THUMB_SIZE

        self.tk_cache = {}
        self._empty_img = None

        self._apply_style()
        self._build_ui()
        self._init_dropdowns()
        self._init_sliders()
        self._preload_thumbnails()

        self.deiconify()
        self.update_grid()

    def _preload_thumbnails(self):
        if not self.db:
            return
        splash = SplashProgress(self, len(self.db))
        for i, entry in enumerate(self.db):
            thumb_path = build_thumbnail(entry["file"])
            entry["thumb_path"] = thumb_path
            splash.update_progress(i + 1)
        splash.destroy()

    def _get_tk_image(self, thumb_path):
        if thumb_path not in self.tk_cache:
            img = Image.open(thumb_path)
            self.tk_cache[thumb_path] = ImageTk.PhotoImage(img)
        return self.tk_cache[thumb_path]

    def _apply_style(self):
        style = ttk.Style(self)
        style.theme_use("clam")
        style.configure("TCombobox", fieldbackground=BG, background=BG,
                        foreground=FG, font=FONT)
        style.configure("TScrollbar", background=BG3, troughcolor=BG2)

    def _collect_all_params(self):
        params = {}
        for entry in self.db:
            for k, v in entry["params"].items():
                params.setdefault(k, set()).add(v)
        return {k: sorted(v) for k, v in params.items()}

    def _build_ui(self):
        top = tk.Frame(self, bg=BG2, pady=8)
        top.pack(side=tk.TOP, fill=tk.X)

        tk.Label(top, text="X Axis:", bg=BG2, fg=FG, font=FONT_BOLD).pack(side=tk.LEFT, padx=(14, 4))
        self.x_combo = ttk.Combobox(top, textvariable=self.x_var, state="readonly", width=12, font=FONT)
        self.x_combo.pack(side=tk.LEFT, padx=(0, 24))
        self.x_combo.bind("<<ComboboxSelected>>", lambda e: self._on_axis_change())

        tk.Label(top, text="Y Axis:", bg=BG2, fg=FG, font=FONT_BOLD).pack(side=tk.LEFT, padx=(0, 4))
        self.y_combo = ttk.Combobox(top, textvariable=self.y_var, state="readonly", width=12, font=FONT)
        self.y_combo.pack(side=tk.LEFT)
        self.y_combo.bind("<<ComboboxSelected>>", lambda e: self._on_axis_change())

        tk.Frame(self, bg=BG3, height=2).pack(fill=tk.X)

        main = tk.Frame(self, bg=BG)
        main.pack(side=tk.TOP, fill=tk.BOTH, expand=True)

        grid_frame = tk.Frame(main, bg=BG)
        grid_frame.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        self.canvas = tk.Canvas(grid_frame, bg=BG, highlightthickness=0)
        vscroll = ttk.Scrollbar(grid_frame, orient=tk.VERTICAL, command=self.canvas.yview)
        hscroll = ttk.Scrollbar(grid_frame, orient=tk.HORIZONTAL, command=self.canvas.xview)
        self.canvas.configure(yscrollcommand=vscroll.set, xscrollcommand=hscroll.set)
        vscroll.pack(side=tk.RIGHT, fill=tk.Y)
        hscroll.pack(side=tk.BOTTOM, fill=tk.X)
        self.canvas.pack(fill=tk.BOTH, expand=True)

        self.grid_inner = tk.Frame(self.canvas, bg=BG)
        self.canvas_window = self.canvas.create_window((0, 0), window=self.grid_inner, anchor="nw")
        self.grid_inner.bind("<Configure>", self._on_grid_configure)

        tk.Frame(main, bg=BG3, width=2).pack(side=tk.RIGHT, fill=tk.Y)

        right = tk.Frame(main, bg=BG2, width=230)
        right.pack(side=tk.RIGHT, fill=tk.Y)
        right.pack_propagate(False)

        tk.Label(right, text="Filters", bg=BG2, fg=FG,
                 font=("Arial", 14, "bold")).pack(pady=(14, 6))
        tk.Frame(right, bg=BG3, height=2).pack(fill=tk.X, padx=8)

        self.sliders_frame = tk.Frame(right, bg=BG2)
        self.sliders_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=8)

    def _on_grid_configure(self, event):
        self.canvas.configure(scrollregion=self.canvas.bbox("all"))

    def _init_dropdowns(self):
        params = list(self.all_params.keys())
        self.x_combo["values"] = params
        self.y_combo["values"] = params
        if len(params) >= 1:
            self.x_var.set(params[0])
        if len(params) >= 2:
            self.y_var.set(params[1])

    def _init_sliders(self):
        for widget in self.sliders_frame.winfo_children():
            widget.destroy()
        self.slider_vars = {}
        self.slider_values = {}
        self.slider_widgets = {}

        for param, values in self.all_params.items():
            self.slider_values[param] = values
            var = tk.IntVar(value=0)
            self.slider_vars[param] = var

            frame = tk.Frame(self.sliders_frame, bg=BG2)
            frame.pack(fill=tk.X, pady=6)

            top_row = tk.Frame(frame, bg=BG2)
            top_row.pack(fill=tk.X)

            name_label = tk.Label(top_row, text=param, bg=BG2, fg=FG,
                                  font=FONT_BOLD, anchor="w")
            name_label.pack(side=tk.LEFT)

            val_label = tk.Label(top_row, text=str(values[0]), bg=BG2,
                                 fg=ACCENT, font=FONT_BOLD, anchor="e")
            val_label.pack(side=tk.RIGHT)

            slider = tk.Scale(
                frame, from_=0, to=len(values) - 1,
                orient=tk.HORIZONTAL, variable=var,
                showvalue=False, bg=BG2, fg=FG,
                troughcolor=BG3, activebackground=ACCENT,
                highlightthickness=0, bd=0,
                command=lambda v, p=param, vl=val_label: self._on_slider(p, vl)
            )
            slider.pack(fill=tk.X)

            tick_frame = tk.Frame(frame, bg=BG2)
            tick_frame.pack(fill=tk.X)
            tk.Label(tick_frame, text=str(values[0]), bg=BG2, fg=FG2,
                     font=("Arial", 9)).pack(side=tk.LEFT)
            tk.Label(tick_frame, text=str(values[-1]), bg=BG2, fg=FG2,
                     font=("Arial", 9)).pack(side=tk.RIGHT)

            self.slider_widgets[param] = {
                "frame": frame,
                "slider": slider,
                "val_label": val_label,
                "name_label": name_label,
            }

        self._update_slider_states()

    def _update_slider_states(self):
        """Gray out sliders whose param is currently used as X or Y axis."""
        x_param = self.x_var.get()
        y_param = self.y_var.get()
        axis_params = {x_param, y_param}

        for param, widgets in self.slider_widgets.items():
            is_axis = param in axis_params
            fg_color = FG_DISABLED if is_axis else FG
            bg_color = BG_DISABLED if is_axis else BG2
            accent = FG_DISABLED if is_axis else ACCENT
            state = tk.DISABLED if is_axis else tk.NORMAL

            widgets["frame"].config(bg=bg_color)
            widgets["name_label"].config(bg=bg_color, fg=fg_color)
            widgets["val_label"].config(bg=bg_color, fg=accent)
            widgets["slider"].config(
                state=state,
                bg=bg_color,
                troughcolor=BG3 if not is_axis else BG_DISABLED
            )

    def _on_axis_change(self):
        self._update_slider_states()
        self.update_grid()

    def _on_slider(self, param, val_label):
        idx = self.slider_vars[param].get()
        values = self.slider_values[param]
        val_label.config(text=str(values[idx]))
        self.update_grid()

    def _get_filter_values(self):
        result = {}
        for param, var in self.slider_vars.items():
            idx = var.get()
            result[param] = self.slider_values[param][idx]
        return result

    def _find_entry(self, filters):
        """O(1) lookup via index."""
        key = frozenset(filters.items())
        return self._index.get(key)

    def _get_empty_image(self):
        if self._empty_img is None:
            img = Image.new("RGB", (self.cell_size, self.cell_size), color=(220, 220, 220))
            draw = ImageDraw.Draw(img)
            m = 20
            s = self.cell_size
            draw.line([(m, m), (s - m, s - m)], fill=(180, 180, 180), width=2)
            draw.line([(s - m, m), (m, s - m)], fill=(180, 180, 180), width=2)
            self._empty_img = ImageTk.PhotoImage(img)
        return self._empty_img

    def update_grid(self):
        for widget in self.grid_inner.winfo_children():
            widget.destroy()

        x_param = self.x_var.get()
        y_param = self.y_var.get()
        if not x_param or not y_param or x_param == y_param:
            tk.Label(self.grid_inner, text="Pick two different axis parameters.",
                     bg=BG, fg=FG2, font=FONT).grid(row=0, column=0, padx=20, pady=20)
            return

        filters = self._get_filter_values()

        # Remove axis params from filters — they vary across the grid
        filters.pop(x_param, None)
        filters.pop(y_param, None)

        x_vals_all = self.all_params.get(x_param, [])
        y_vals_all = self.all_params.get(y_param, [])

        # O(1) per cell now
        def has_image(x_val, y_val):
            f = dict(filters)
            f[x_param] = x_val
            f[y_param] = y_val
            return self._find_entry(f) is not None

        x_vals = [x for x in x_vals_all if any(has_image(x, y) for y in y_vals_all)]
        y_vals = [y for y in y_vals_all if any(has_image(x, y) for x in x_vals_all)]

        if not x_vals or not y_vals:
            tk.Label(self.grid_inner, text="No images match current filters.",
                     bg=BG, fg=FG2, font=FONT).grid(row=0, column=0, padx=20, pady=20)
            return

        cell_pad = 4

        tk.Label(self.grid_inner, text="", bg=BG3,
                 width=10, height=2).grid(row=0, column=0, padx=cell_pad, pady=cell_pad, sticky="nsew")

        for col, xv in enumerate(x_vals):
            tk.Label(self.grid_inner, text=f"{x_param}={xv}",
                     bg=BG3, fg=FG, font=FONT_HEADER,
                     width=14, height=2).grid(
                row=0, column=col + 1, padx=cell_pad, pady=cell_pad, sticky="nsew")

        for row, yv in enumerate(y_vals):
            tk.Label(self.grid_inner, text=f"{y_param}={yv}",
                     bg=BG3, fg=FG, font=FONT_HEADER,
                     width=12, height=2).grid(
                row=row + 1, column=0, padx=cell_pad, pady=cell_pad, sticky="nsew")

            for col, xv in enumerate(x_vals):
                f = dict(filters)
                f[x_param] = xv
                f[y_param] = yv
                entry = self._find_entry(f)

                if entry and "thumb_path" in entry:
                    img = self._get_tk_image(entry["thumb_path"])
                    lbl = tk.Label(self.grid_inner, image=img, bg=BG,
                                   cursor="hand2", relief="flat", bd=1)
                    lbl.image = img
                    lbl.bind("<Button-1>", lambda e, p=entry["file"]: self._open_full(p))
                else:
                    img = self._get_empty_image()
                    lbl = tk.Label(self.grid_inner, image=img, bg=BG, relief="flat", bd=1)
                    lbl.image = img

                lbl.grid(row=row + 1, column=col + 1, padx=cell_pad, pady=cell_pad)

    def _open_full(self, path):
        win = tk.Toplevel(self, bg=BG)
        win.title(os.path.basename(path))
        img = Image.open(path)
        tk_img = ImageTk.PhotoImage(img)
        lbl = tk.Label(win, image=tk_img, bg=BG)
        lbl.image = tk_img
        lbl.pack(padx=10, pady=10)


if __name__ == "__main__":
    app = ImageBrowser()
    app.mainloop()
