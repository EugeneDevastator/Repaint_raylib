import sys
import os
import subprocess
import zipfile
import shutil
from pathlib import Path


def get_bundle_dir():
    if getattr(sys, "frozen", False):
        return Path(sys._MEIPASS)
    return Path(__file__).parent


def get_install_dir():
    if getattr(sys, "frozen", False):
        return Path(sys.executable).parent
    return Path(__file__).parent


def ensure_python(install_dir, bundle_dir):
    if sys.platform == "win32":
        python_exe = install_dir / "python" / "python.exe"
    else:
        python_exe = install_dir / "python" / "bin" / "python3"

    if python_exe.exists():
        return python_exe

    print("[bootstrap] Extracting portable Python...")
    python_zip = bundle_dir / "python.zip"
    if python_zip.exists():
        tmp = install_dir / ".python_tmp"
        tmp.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(python_zip, "r") as zf:
            zf.extractall(tmp)
        contents = list(tmp.iterdir())
        if len(contents) == 1 and contents[0].is_dir():
            shutil.move(str(contents[0]), str(install_dir / "python"))
        else:
            shutil.move(str(tmp), str(install_dir / "python"))
        shutil.rmtree(tmp, ignore_errors=True)

    if not python_exe.exists():
        print(f"[bootstrap] ERROR: Python not found at {python_exe}")
        sys.exit(1)
    return python_exe


def ensure_deps(install_dir, python_exe):
    deps_dir = install_dir / "deps"
    if deps_dir.exists():
        return

    req = install_dir / "requirements.txt"
    print("[bootstrap] Installing dependencies (this may take a minute)...")
    subprocess.check_call(
        [str(python_exe), "-m", "pip", "install",
         "--target", str(deps_dir), "-r", str(req)],
        stdout=subprocess.DEVNULL,
    )


def ensure_file(install_dir, bundle_dir, name):
    dst = install_dir / name
    if not dst.exists():
        src = bundle_dir / name
        if src.exists():
            shutil.copy2(src, dst)
    return dst


def main():
    install_dir = get_install_dir()
    bundle_dir = get_bundle_dir()

    python_exe = ensure_python(install_dir, bundle_dir)
    ensure_deps(install_dir, python_exe)

    core_py = ensure_file(install_dir, bundle_dir, "nnserver_core.py")
    ensure_file(install_dir, bundle_dir, "requirements.txt")

    env = os.environ.copy()
    env["PYTHONPATH"] = str(install_dir / "deps")

    print("[bootstrap] Starting nnserver...")
    subprocess.check_call([str(python_exe), str(core_py)], env=env)


if __name__ == "__main__":
    main()
