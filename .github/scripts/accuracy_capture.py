#!/usr/bin/env python3
"""Capture mGBA GBA-suite category evidence with the CipherGBA portable core.

No ROM, BIOS, binary, or save is bundled. Requires a locally supplied suite.gba.
PPM files plus CRC32 hashes are NOT a substitute for scored per-case assertions.
"""
import argparse
import ctypes as ct
from pathlib import Path
import zlib

NAMES=("Memory","IO_Read","Timing","Timer_CountUp","Timer_IRQ","Shifter",
       "Carry","Multiply_Long","BIOS_Math","DMA","SIO_RW","SIO_Timing",
       "Misc_Edge","Video")
API={
    "ciphergba_create":([],ct.c_void_p),
    "ciphergba_destroy":([ct.c_void_p],None),
    "ciphergba_set_save_directory":([ct.c_void_p,ct.c_char_p],ct.c_int),
    "ciphergba_load_rom":([ct.c_void_p,ct.c_char_p],ct.c_int),
    "ciphergba_load_bios":([ct.c_void_p,ct.c_char_p],ct.c_int),
    "ciphergba_run_frame":([ct.c_void_p],ct.c_int),
    "ciphergba_get_framebuffer":([ct.c_void_p],ct.POINTER(ct.c_uint32)),
    "ciphergba_set_buttons":([ct.c_void_p,ct.c_uint16],None),
    "ciphergba_last_error":([ct.c_void_p],ct.c_char_p)
}

def setup(path):
    lib=ct.CDLL(str(path.resolve()))
    for name,(args,ret) in API.items():
        fn=getattr(lib,name)
        fn.argtypes=args
        fn.restype=ret
    return lib

def capture(lib,suite,bios,output,index,detail):
    mode="BIOS" if bios else "HLE"
    core=lib.ciphergba_create()
    if not core:raise RuntimeError("Cannot create core")
    try:
        saves=output/("saves_"+mode+"_"+str(index))
        saves.mkdir(parents=True,exist_ok=True)
        if not lib.ciphergba_set_save_directory(core,str(saves).encode()):
            raise RuntimeError("Cannot configure save directory")
        if bios and not lib.ciphergba_load_bios(core,str(bios).encode()):
            raise RuntimeError("Cannot load original BIOS")
        if not lib.ciphergba_load_rom(core,str(suite).encode()):
            raise RuntimeError("Cannot load test suite")
        def run(frames):
            for _ in range(frames):
                if not lib.ciphergba_run_frame(core):
                    raise RuntimeError(lib.ciphergba_last_error(core).decode(errors="replace"))
        def press(mask,held=6,released=14):
            lib.ciphergba_set_buttons(core,mask)
            run(held)
            lib.ciphergba_set_buttons(core,0)
            run(released)
        run(450 if bios else 240)
        for _ in range(index):press(128) # GBA Down
        press(1,20,100 if detail else 300) # GBA A
        if detail:
            if index!=12:raise ValueError("HBlank detail requires Misc Edge")
            press(128,8,20) # Next subtest: HBlank
            press(1,20,500) # Open its results
        else:run(200)
        fb=ct.cast(lib.ciphergba_get_framebuffer(core),
                   ct.POINTER(ct.c_uint32*38400)).contents
        rgb=bytearray()
        for pixel in fb:
            rgb.extend(((pixel>>16)&255,(pixel>>8)&255,pixel&255))
        crc=zlib.crc32(rgb)
        title="HBlank_Detail" if detail else NAMES[index]
        path=output/(mode+"_"+title+".ppm")
        path.write_bytes(b"P6\n240 160\n255\n"+rgb)
        return path,crc
    finally:
        lib.ciphergba_destroy(core)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core",type=Path,required=True,help="Path to the CipherGBA shared library")
    parser.add_argument("--suite",type=Path,required=True,help="Private suite.gba path")
    parser.add_argument("--bios",type=Path,help="Optional private 16KiB BIOS for second sweep")
    parser.add_argument("--out",type=Path,default=Path("suite-captures"))
    parser.add_argument("--hblank-only",action="store_true")
    args=parser.parse_args()
    for path in [args.core,args.suite]+([args.bios] if args.bios else []):
        if not path.is_file():parser.error("Input missing: "+str(path))
    args.out.mkdir(parents=True,exist_ok=True)
    lib=setup(args.core)
    cases=[(12,True)] if args.hblank_only else (
        [(i,False) for i in range(len(NAMES))]+[(12,True)])
    lines=["mode\tcategory\tCRC32\tlocal_PPM_file"]
    for bios in (None,args.bios) if args.bios else (None,):
        for index,detail in cases:
            file,crc=capture(lib,args.suite.resolve(),bios,args.out,index,detail)
            mode="BIOS" if bios else "HLE"
            name="HBlank_Detail" if detail else NAMES[index]
            lines.append(mode+"\t"+name+"\t"+format(crc,"08X")+"\t"+file.name)
            print(mode,name,format(crc,"08X"),flush=True)
    (args.out/"framebuffer_digests.tsv").write_text("\n".join(lines)+"\n")
    print("Captured framebuffer evidence only; not an automatic 6,998-check PASS count.")

if __name__=="__main__":main()
