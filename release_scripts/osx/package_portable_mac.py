#!/usr/bin/env python3
"""
package_portable_mac.py
Automatiza el empaquetado completo, portabilidad de librerias y firma de OpenBoard en macOS.
Uso: python3 release_scripts/osx/package_portable_mac.py [output_zip_name]
"""

import os
import sys
import shutil
import subprocess
import glob
import re

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
BUILD_DIR = os.path.join(REPO_ROOT, "build")
APP_DIR = os.path.join(BUILD_DIR, "OpenBoard.app")
CONTENTS_DIR = os.path.join(APP_DIR, "Contents")
MACOS_DIR = os.path.join(CONTENTS_DIR, "MacOS")
RESOURCES_DIR = os.path.join(CONTENTS_DIR, "Resources")
FRAMEWORKS_DIR = os.path.join(CONTENTS_DIR, "Frameworks")

def run(cmd, check=True):
    print(f"==> {' '.join(cmd) if isinstance(cmd, list) else cmd}")
    res = subprocess.run(cmd, shell=isinstance(cmd, str), capture_output=True, text=True)
    if check and res.returncode != 0:
        print(f"ERROR: {res.stderr}")
        sys.exit(res.returncode)
    return res

def is_macho(p):
    try:
        with open(p, "rb") as fp:
            return fp.read(4) in (b"\xcf\xfa\xed\xfe", b"\xce\xfa\xed\xfe", b"\xca\xfe\xba\xbe")
    except Exception:
        return False

def step_1_copy_skeleton():
    print("\n[1/5] Armando estructura del .app y recursos...")
    os.makedirs(MACOS_DIR, exist_ok=True)
    os.makedirs(RESOURCES_DIR, exist_ok=True)
    os.makedirs(FRAMEWORKS_DIR, exist_ok=True)

    src_bin = os.path.join(BUILD_DIR, "openboard")
    dst_bin = os.path.join(MACOS_DIR, "OpenBoard")
    if not os.path.exists(src_bin):
        print(f"Error: no existe {src_bin}. Compilaste antes?")
        sys.exit(1)
    shutil.copy2(src_bin, dst_bin)
    os.chmod(dst_bin, 0o755)

    # Info.plist y PkgInfo
    plist_src = os.path.join(REPO_ROOT, "resources/macx/Info.plist")
    plist_dst = os.path.join(CONTENTS_DIR, "Info.plist")
    with open(plist_src, "r") as f:
        content = f.read().replace("@SHORT_VERSION@", "1.8.0")
    with open(plist_dst, "w") as f:
        f.write(content)

    with open(os.path.join(CONTENTS_DIR, "PkgInfo"), "w") as f:
        f.write("APPLOpenB")

    # Carpetas de recursos
    for folder in ["etc", "library", "fonts"]:
        src = os.path.join(REPO_ROOT, "resources", folder)
        dst = os.path.join(RESOURCES_DIR, folder)
        if os.path.exists(dst):
            shutil.rmtree(dst)
        shutil.copytree(src, dst)

    for icns in glob.glob(os.path.join(REPO_ROOT, "resources/macx/*.icns")):
        shutil.copy2(icns, RESOURCES_DIR)

    hints = os.path.join(REPO_ROOT, "resources/startupHints")
    if os.path.exists(hints):
        dst = os.path.join(RESOURCES_DIR, "startupHints")
        if os.path.exists(dst):
            shutil.rmtree(dst)
        shutil.copytree(hints, dst)

    # Traducciones
    for qm in glob.glob(os.path.join(BUILD_DIR, "i18n/OpenBoard_*.qm")):
        lang = os.path.basename(qm).replace("OpenBoard_", "").replace(".qm", "")
        lang_dir = os.path.join(RESOURCES_DIR, f"{lang}.lproj")
        os.makedirs(lang_dir, exist_ok=True)
        shutil.copy2(qm, lang_dir)

def step_2_macdeployqt():
    print("\n[2/5] Desplegando dependencias Qt con macdeployqt...")
    # Detectar macdeployqt desde Homebrew o PATH
    macdeployqt_bin = "/opt/homebrew/opt/qt6/bin/macdeployqt"
    if not os.path.exists(macdeployqt_bin):
        # Fallback a which
        which_res = subprocess.run(["which", "macdeployqt"], capture_output=True, text=True)
        macdeployqt_bin = which_res.stdout.strip()

    if not macdeployqt_bin or not os.path.exists(macdeployqt_bin):
        print("ERROR: macdeployqt no encontrado.")
        sys.exit(1)

    run([macdeployqt_bin, APP_DIR], check=False)

    # Copiar QtQuickWidgets que macdeployqt suele omitir
    qt_declarative_dirs = [
        "/opt/homebrew/opt/qtdeclarative/lib/QtQuickWidgets.framework",
        "/opt/homebrew/opt/qt6/lib/QtQuickWidgets.framework"
    ]
    target_qw = os.path.join(FRAMEWORKS_DIR, "QtQuickWidgets.framework")
    if not os.path.exists(target_qw):
        for candidate in qt_declarative_dirs:
            if os.path.exists(candidate):
                print(f"Copiando {candidate} -> {target_qw}")
                shutil.copytree(candidate, target_qw, symlinks=True)
                break

    # Limpiar headers y temporales
    for h in [os.path.join(target_qw, "Headers"), os.path.join(target_qw, "Versions/A/Headers")]:
        if os.path.exists(h):
            shutil.rmtree(h, ignore_errors=True)

    # Eliminar posibles restos temporales de ldid
    for ldid in glob.glob(os.path.join(FRAMEWORKS_DIR, "QtWebEngineCore.framework/Versions/A/.ldid.*")):
        os.remove(ldid)

def step_3_make_fully_portable():
    print("\n[3/5] Desacoplando librerias locales (reescribiendo rutas a @executable_path)...")
    main_bin = os.path.join(MACOS_DIR, "OpenBoard")

    # 1. En el binario principal: reescribir librerias no-Qt que vienen de Homebrew
    res = subprocess.run(["otool", "-L", main_bin], capture_output=True, text=True)
    for line in res.stdout.splitlines()[1:]:
        parts = line.strip().split()
        if not parts:
            continue
        dep = parts[0]
        if "/opt/homebrew" in dep or "/usr/local" in dep:
            base = os.path.basename(dep)
            target = os.path.join(FRAMEWORKS_DIR, base)
            if os.path.exists(target):
                new_path = f"@executable_path/../Frameworks/{base}"
                subprocess.run(["install_name_tool", "-change", dep, new_path, main_bin], capture_output=True)
                print(f"  Main bin link: {base} -> @executable_path/../Frameworks/{base}")

    # 2. En todo el bundle: reescribir dependencias de Qt que apunten a /opt/homebrew
    pat = re.compile(r"^\s+(/opt/homebrew/\S*?/lib/(Qt\w+)\.framework/Versions/A/\2)\s")
    for root, _, files in os.walk(APP_DIR):
        for n in files:
            p = os.path.join(root, n)
            if os.path.islink(p) or not is_macho(p):
                continue
            out = subprocess.run(["otool", "-L", p], capture_output=True, text=True).stdout
            deps = [m.group(1) for l in out.splitlines() if (m := pat.match(l))]
            helper = "/Helpers/QtWebEngineProcess.app/" in p
            prefix = "@loader_path/../../../../../../../" if helper else "@executable_path/../Frameworks/"
            for d in deps:
                name = d.split("/")[-1]
                target = f"{FRAMEWORKS_DIR}/{name}.framework/Versions/A/{name}"
                if os.path.exists(target):
                    new_p = f"{prefix}{name}.framework/Versions/A/{name}"
                    subprocess.run(["install_name_tool", "-change", d, new_p, p], capture_output=True)

    # 3. Corregir IDs de frameworks y eliminar RPATHs de Homebrew
    for root, _, files in os.walk(APP_DIR):
        for n in files:
            p = os.path.join(root, n)
            if os.path.islink(p) or not is_macho(p):
                continue

            # ID
            out_id = subprocess.run(["otool", "-D", p], capture_output=True, text=True).stdout.splitlines()
            if len(out_id) > 1 and ("/opt/homebrew" in out_id[1] or "/usr/local" in out_id[1]):
                if ".framework" in p:
                    fw_name = [x for x in p.split("/") if x.endswith(".framework")][0]
                    bin_name = os.path.basename(p)
                    new_id = f"@rpath/{fw_name}/Versions/A/{bin_name}"
                else:
                    new_id = f"@rpath/{os.path.basename(p)}"
                subprocess.run(["install_name_tool", "-id", new_id, p], capture_output=True)

            # Delete host RPATHs
            out_rpath = subprocess.run(["otool", "-l", p], capture_output=True, text=True).stdout.splitlines()
            for i, l in enumerate(out_rpath):
                if "cmd LC_RPATH" in l:
                    for j in range(i+1, min(i+5, len(out_rpath))):
                        if "path " in out_rpath[j]:
                            rpath = out_rpath[j].strip().split()[1]
                            if "/opt/homebrew" in rpath or "/usr/local" in rpath or "/Users/" in rpath:
                                subprocess.run(["install_name_tool", "-delete_rpath", rpath, p], capture_output=True)

def step_4_codesign():
    print("\n[4/5] Firmando el bundle ad-hoc con codesign...")
    # Limpiar restos temporales
    for f in glob.glob(os.path.join(MACOS_DIR, "OpenBoard*gFmyt*")):
        os.remove(f)

    res = run(["codesign", "--force", "--deep", "--sign", "-", APP_DIR])
    verify = run(["codesign", "--verify", "--deep", "--strict", APP_DIR])
    print("Firma valida y verificada con exito.")

def step_5_create_zip(zip_name):
    print(f"\n[5/5] Creando archivo comprimido {zip_name}...")
    zip_path = os.path.join(BUILD_DIR, zip_name)
    if os.path.exists(zip_path):
        os.remove(zip_path)
    subprocess.run(["ditto", "-c", "-k", "--keepParent", APP_DIR, zip_path], check=True)
    print(f"\n Listo! Paquete final generado en: {zip_path}")

def main():
    zip_name = sys.argv[1] if len(sys.argv) > 1 else "OpenBoard-macOS-AppleSilicon.zip"
    step_1_copy_skeleton()
    step_2_macdeployqt()
    step_3_make_fully_portable()
    step_4_codesign()
    step_5_create_zip(zip_name)

if __name__ == "__main__":
    main()
