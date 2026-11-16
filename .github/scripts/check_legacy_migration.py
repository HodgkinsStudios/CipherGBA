#!/usr/bin/env python3
"""Verify the actual Linux SDL2 existing user data import without real user files."""
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile

if len(sys.argv)!=3:
    sys.exit("Usage: check_legacy_migration.py CIPHERGBA SYNTHETIC_ROM")
if not sys.platform.startswith("linux"):
    sys.exit("This XDG_DATA_HOME-isolated test is Linux-specific")

binary=Path(sys.argv[1]).resolve()
rom=Path(sys.argv[2]).resolve()

with tempfile.TemporaryDirectory(prefix="ciphergba-migration-") as temp:
    root=Path(temp)
    source=root/"original"/"CipherGBA_Data"
    destBase=root/"private-data"
    dest=destBase/"HodgkinsStudios"/"CipherGBA"
    samples={
        "saves/new.sav":b"legacy-battery-data",
        "saves/UPPER.SAV":b"uppercase extension",
        "saves/existing.sav":b"must not replace",
        "states/state.cgs":b"synthetic save-state copy",
        "tas/movie.cgtas":b"synthetic TAS copy",
        "cheats/cheat.txt":b"synthetic cheat copy",
        "bios/other.bin":b"synthetic non-BIOS file"
    }
    for name,data in samples.items():
        p=source/name
        p.parent.mkdir(parents=True,exist_ok=True)
        p.write_bytes(data)
    sources={name:hashlib.sha256((source/name).read_bytes()).hexdigest()
             for name in samples}
    (source/"saves"/"ignored.gba").write_bytes(b"invalid type")
    (source/"saves"/"nested").mkdir()
    (source/"saves"/"nested"/"secret.sav").write_bytes(b"nested")
    (source/"bindings.ini").write_bytes(b"old keycodes incompatible")
    try:
        (source/"saves"/"alias.sav").symlink_to(source/"saves"/"new.sav")
    except OSError:
        pass
    keep=dest/"saves"/"existing.sav"
    keep.parent.mkdir(parents=True,exist_ok=True)
    keep.write_bytes(b"KEEP CURRENT DATA")
    env=dict(os.environ,XDG_DATA_HOME=str(destBase),
             SDL_VIDEODRIVER="dummy",SDL_AUDIODRIVER="dummy",
             APPIMAGE_EXTRACT_AND_RUN="1")
    def invoke(path):
        process=subprocess.run([str(binary),"--import-data",str(path),
                                "--rom",str(rom),"--smoke-frames","1"],
                               env=env,timeout=100,text=True,
                               stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        assert process.returncode==0,(process.returncode,process.stdout,process.stderr)
        assert "Data import: copied=" in process.stdout,process.stdout
        return process.stdout
    first=invoke(source.parent)
    for name,data in samples.items():
        current=dest/name
        if name=="saves/existing.sav":
            assert current.read_bytes()==b"KEEP CURRENT DATA"
        else:
            assert current.read_bytes()==data,(name,current)
        assert hashlib.sha256((source/name).read_bytes()).hexdigest()==sources[name]
    for name in ("saves/ignored.gba","saves/nested/secret.sav",
                 "saves/alias.sav","bindings.ini"):
        assert not (dest/name).exists(),name
    assert "copied=6" in first,first
    second=invoke(source)
    assert "copied=0" in second,second
    (source/"saves"/"new.sav").write_bytes(b"updated legacy source")
    third=invoke(source.parent)
    assert "copied=0" in third,third
    assert (dest/"saves"/"new.sav").read_bytes()==samples["saves/new.sav"]
    assert keep.read_bytes()==b"KEEP CURRENT DATA"
    print("PASS: legacy data import preserves existing saves, source integrity,")
    print("      uppercase extensions, symlink/nested exclusions and repeated imports")
