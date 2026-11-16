#!/usr/bin/env python3
"""Assemble verified CipherGBA v0.3 release assets from one CI run.

Requires only Python standard library. No ROMs, BIOS images or user saves.
The macOS .app bundles are retained INSIDE their original Apple ditto ZIPs
so code signatures and executable permissions are not damaged in transit.
"""
import argparse
import hashlib
import re
import shutil
import sys
import zipfile
from pathlib import Path

ARTIFACTS={
    "windows":"CipherGBA-v0.3-Windows-x64-CI",
    "x11":"CipherGBA-v0.3-Linux-X11-x86_64-CI",
    "appimage":"CipherGBA-v0.3-AppImage-x86_64-CI",
    "flatpak":"CipherGBA-v0.3-Flatpak-CI",
    "mac_arm":"CipherGBA-v0.3-macOS-arm64-CI",
    "mac_intel":"CipherGBA-v0.3-macOS-x86_64-CI"
}
def require(condition,message):
    if not condition:
        raise RuntimeError(message)

def hash_file(path):
    d=hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda:f.read(1024*1024),b""):
            d.update(chunk)
    return d.hexdigest()

def zip_valid(path):
    with zipfile.ZipFile(path) as z:
        require(z.testzip() is None,"Corrupt archive: "+str(path))
        return z.namelist()

def copy_single(artifact_dir,pattern,out):
    matches=list(artifact_dir.glob(pattern))
    require(len(matches)==1,"Expected one "+pattern+" in "+str(artifact_dir))
    shutil.copyfile(matches[0],out)
    require(out.stat().st_size>100000,"Release binary too small: "+str(out))

def zip_folder(root,out,target):
    require(root.is_dir(),"Missing packaged platform directory: "+str(root))
    with zipfile.ZipFile(out,"w",compression=zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for p in sorted(root.rglob("*")):
            require(not p.is_symlink(),"Unexpected symlink in artifact: "+str(p))
            if p.is_file():
                z.write(p,target+"/"+p.relative_to(root).as_posix())
    zip_valid(out)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--artifacts",type=Path,required=True)
    p.add_argument("--source",type=Path,required=True)
    p.add_argument("--output",type=Path,required=True)
    p.add_argument("--commit",required=True)
    args=p.parse_args()
    require(bool(re.fullmatch(r"[0-9a-f]{40}",args.commit)),"Expected full 40-character commit SHA")
    a=args.artifacts.resolve()
    out=args.output.resolve()
    out.mkdir(parents=True,exist_ok=True)
    require(not list(out.iterdir()),"Output directory must be empty")
    folders={k:a/v for k,v in ARTIFACTS.items()}
    for name,folder in folders.items():
        require(folder.is_dir(),"Missing CI artifact "+name+": "+str(folder))
    expected_win=("CipherGBA.exe","SDL2.dll","libz-1.dll",
                  "libstdc++-6.dll","libgcc_s_seh-1.dll","libwinpthread-1.dll",
                  "THIRD_PARTY_NOTICES.md")
    for filename in expected_win:
        require((folders["windows"]/filename).is_file(),"Windows dependency missing: "+filename)
    require((folders["windows"]/"CipherGBA.exe").open("rb").read(2)==b"MZ","Invalid Windows EXE")
    require((folders["x11"]/"CipherGBA").open("rb").read(4)==b"\x7fELF","Invalid Linux X11 ELF")
    require((folders["x11"]/"libCipherGBA_Core.so.1").is_file(),"X11 shared core missing")
    assets=[]
    def add(name):
        path=out/name
        assets.append(path)
        return path
    zip_folder(folders["windows"],add("CipherGBA-v0.3-Windows-x64.zip"),
               "CipherGBA-v0.3-Windows-x64")
    zip_folder(folders["x11"],add("CipherGBA-v0.3-Linux-X11-x86_64.zip"),
               "CipherGBA-v0.3-Linux-X11-x86_64")
    app=add("CipherGBA-v0.3-Linux-x86_64.AppImage")
    copy_single(folders["appimage"],"*.AppImage",app)
    require(app.open("rb").read(4)==b"\x7fELF","Invalid Linux AppImage")
    flat=add("CipherGBA-v0.3-Linux-x86_64.flatpak")
    copy_single(folders["flatpak"],"*.flatpak",flat)
    for key,arch in (("mac_arm","arm64"),("mac_intel","x86_64")):
        target=add("CipherGBA-v0.3-macOS-"+arch+".zip")
        copy_single(folders[key],"*.zip",target)
        names=zip_valid(target)
        require(any(x.endswith("CipherGBA.app/Contents/MacOS/CipherGBA")
                    for x in names),"Mac .app executable missing in "+arch)
        require(any("CipherGBA.app/Contents/Frameworks/" in x for x in names),
                "Bundled macOS frameworks absent for "+arch)
    source=add("CipherGBA-v0.3-Source.zip")
    shutil.copyfile(args.source,source)
    names=zip_valid(source)
    for expected in ("README.md","RELEASE_NOTES.md","THIRD_PARTY_NOTICES.md"):
        require(expected in names,"Missing source documentation "+expected)
    forbid={".gba",".gbc",".sav",".cgs",".cgtas",".exe",".dll",".so",".dylib",".bin"}
    for name in names:
        path=Path(name)
        require(not path.suffix.lower() in forbid,
                "Compiled or private ROM/BIOS/save file in source archive: "+name)
    readme=(
        "CipherGBA v0.3 | HodgkinsStudios\n"
        "Commit: "+args.commit+"\n\n"
        "Windows x64: extract CipherGBA-v0.3-Windows-x64.zip and run CipherGBA.exe.\n"
        "macOS arm64/Intel: extract the appropriate macOS ZIP; open CipherGBA.app.\n"
        "  Apps are ad-hoc signed, not notarized. Review macOS Gatekeeper notices.\n"
        "Linux AppImage: chmod +x *.AppImage && ./CipherGBA-v0.3-Linux-x86_64.AppImage\n"
        "Linux Flatpak: flatpak install --user ./CipherGBA-v0.3-Linux-x86_64.flatpak\n"
        "  Requires the Freedesktop 25.08 runtime.\n"
        "Linux X11: extract the X11 ZIP and run ./CipherGBA (system libX11/zlib required).\n"
        "Source: CipherGBA-v0.3-Source.zip (C++17 / CMake; no game or firmware files).\n\n"
        "Accuracy: 6994/6998 in both BIOS modes; 4 disputed HBlank suite tests.\n"
        "GUI/audio/controller workflows on clean Windows/Mac systems remain unverified.\n"
        "No Nintendo ROMs or BIOS are included. No project-wide reuse license is granted.\n"
        "See RELEASE_NOTES.md inside the source archive for full details.\n"
    )
    first_manifest="".join(hash_file(x)+"  "+x.name+"\n" for x in assets)
    combined=out/"CipherGBA-v0.3-ALL-PLATFORMS.zip"
    with zipfile.ZipFile(combined,"w",compression=zipfile.ZIP_STORED,allowZip64=True) as z:
        z.writestr("CipherGBA-v0.3/README_FIRST.txt",readme)
        z.writestr("CipherGBA-v0.3/SHA256SUMS.txt",first_manifest)
        for item in assets:
            z.write(item,"CipherGBA-v0.3/"+item.name)
    zip_valid(combined)
    assets.append(combined)
    manifest=out/"SHA256SUMS.txt"
    manifest.write_text("".join(hash_file(x)+"  "+x.name+"\n" for x in assets))
    # Re-check all named files from the same manifest users will download.
    for line in manifest.read_text().splitlines():
        expected,name=line.split("  ",1)
        require(hash_file(out/name)==expected,"SHA-256 verification failed: "+name)
    print("PASS: source + six platform assets + combined release, SHA-256 verified")
    for item in assets:
        print(item.name,item.stat().st_size,hash_file(item))
if __name__=="__main__":
    try:main()
    except Exception as e:
        print("FAIL: "+str(e),file=sys.stderr)
        raise SystemExit(1)
