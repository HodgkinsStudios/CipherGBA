#!/usr/bin/env python3
"""Read each scored mGBA GBA suite category from its on-screen BG1 text map.

Supply your own suite.gba and optional BIOS; this script never embeds or
exports a ROM, BIOS, framebuffer, screenshot, or save-memory image.
"""
import argparse
import ctypes as ct
import hashlib
import json
import re
import time
from pathlib import Path

NAMES=("Memory","IO_Read","Timing","Timer_CountUp","Timer_IRQ","Shifter",
       "Carry","Multiply_Long","BIOS_Math","DMA","SIO_RW","SIO_Timing","Misc_Edge")
SIGNATURES={
 "ciphergba_create":([],ct.c_void_p),
 "ciphergba_destroy":([ct.c_void_p],None),
 "ciphergba_load_rom":([ct.c_void_p,ct.c_char_p],ct.c_int),
 "ciphergba_load_bios":([ct.c_void_p,ct.c_char_p],ct.c_int),
 "ciphergba_run_frame":([ct.c_void_p],ct.c_int),
 "ciphergba_set_buttons":([ct.c_void_p,ct.c_uint16],None),
 "ciphergba_read8":([ct.c_void_p,ct.c_uint32],ct.c_uint8),
 "ciphergba_last_error":([ct.c_void_p],ct.c_char_p),
}

def load_library(path):
    lib=ct.CDLL(str(path.resolve()))
    for name,(args,result) in SIGNATURES.items():
        method=getattr(lib,name)
        method.argtypes=args
        method.restype=result
    return lib

def capture(lib,rom,bios,index):
    core=lib.ciphergba_create()
    if not core:
        raise RuntimeError("Failed to create GBA core")
    try:
        if bios and not lib.ciphergba_load_bios(core,str(bios).encode()):
            raise RuntimeError("Cannot load user-provided BIOS")
        if not lib.ciphergba_load_rom(core,str(rom).encode()):
            raise RuntimeError("Cannot load user-provided GBA test ROM")
        def frames(n):
            for i in range(n):
                if not lib.ciphergba_run_frame(core):
                    raise RuntimeError("runFrame failed: "+str(lib.ciphergba_last_error(core)))
        def press(button,held=6,released=14):
            lib.ciphergba_set_buttons(core,button)
            frames(held)
            lib.ciphergba_set_buttons(core,0)
            frames(released)
        frames(450 if bios else 240)
        for _ in range(index):
            press(128) # GBA Down
        # Hold A for six frames: a longer hold can repeat into detail pages.
        press(1,6,340)
        read=lambda address:lib.ciphergba_read8(core,address)
        lines=[]
        # suite's textBase = SCREEN_BASE_BLOCK(1): tile=letter - ' '.
        for row in range(20):
            chars=[]
            for col in range(32):
                address=0x06000800+2*(row*32+col)
                word=read(address)|(read(address+1)<<8)
                value=(word&0x3ff)+32
                chars.append(chr(value) if 32<=value<=126 else "?")
            lines.append("".join(chars).rstrip())
        score=re.search(r"\b(\d{1,5})\s*/\s*(\d{1,5})\b",lines[1])
        if not score:
            raise RuntimeError("Suite score not rendered: "+repr(lines[:6]))
        log=bytearray()
        for off in range(32768):
            value=read(0x0e000000+off)
            if value==0:
                break
            log.append(value)
        # Raw SRAM lines are diagnostic only. Upstream misc-edge.c passes
        # expected/observed arguments in reverse order to doResult().
        failure_lines=[line for line in log.decode("latin1").splitlines()
                       if ": FAIL" in line]
        return {"category":NAMES[index],"index":index,
                "passed":int(score.group(1)),"total":int(score.group(2)),
                "screen_header":lines[:6],"raw_failure_lines":failure_lines}
    finally:
        lib.ciphergba_destroy(core)

def sha256(path):
    digest=hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda:stream.read(1024*1024),b""):
            digest.update(block)
    return digest.hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core",type=Path,required=True,help="Built shared CipherGBA core")
    parser.add_argument("--suite",type=Path,required=True,help="Private suite.gba")
    parser.add_argument("--bios",type=Path,help="Private 16KiB original BIOS")
    parser.add_argument("--mode",choices=("HLE","BIOS","both"),default="HLE")
    parser.add_argument("--start",type=int,default=0)
    parser.add_argument("--end",type=int,default=13,
                        help="Exclusive index; Video is visual-only, not scored")
    parser.add_argument("--out",type=Path,default=Path("local-suite-scores"))
    args=parser.parse_args()
    if not args.core.is_file() or not args.suite.is_file():
        parser.error("--core and --suite must exist")
    if args.mode in ("BIOS","both") and (not args.bios or not args.bios.is_file()):
        parser.error("--bios required in original BIOS mode")
    if not (0<=args.start<args.end<=13):
        parser.error("Category range must be 0 <= start < end <= 13")
    args.out.mkdir(parents=True,exist_ok=True)
    lib=load_library(args.core)
    proof={"suite_sha256":sha256(args.suite),
           "bios_sha256":sha256(args.bios) if args.bios else None,
           "categories":{},"note":"Video category is not numerically scored"}
    started=time.monotonic()
    modes=("HLE","BIOS") if args.mode=="both" else (args.mode,)
    for mode in modes:
        results=[]
        for index in range(args.start,args.end):
            item=capture(lib,args.suite,args.bios if mode=="BIOS" else None,index)
            results.append(item)
            (args.out/(mode+"_"+NAMES[index]+".json")).write_text(
                json.dumps(item,indent=2)+"\n")
            print(mode,NAMES[index],str(item["passed"])+"/"+str(item["total"]),
                  "elapsed",round(time.monotonic()-started,1),flush=True)
        passed=sum(item["passed"] for item in results)
        total=sum(item["total"] for item in results)
        proof["categories"][mode]={"passed":passed,"total":total,"checks":results}
        print(mode,passed,"/",total,"FAIL",total-passed,flush=True)
    (args.out/"summary.json").write_text(json.dumps(proof,indent=2)+"\n")
    print("Full numeric run means 13 scored categories; Video is visual-only.")
    print("WARNING: miscellaneous SRAM error logs reverse actual/expected fields.")
if __name__=="__main__":
    main()
