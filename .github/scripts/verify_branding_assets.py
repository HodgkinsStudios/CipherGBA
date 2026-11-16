#!/usr/bin/env python3
"""ROM-free integration test for the one canonical cross-platform icon source."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[2]
ICON=ROOT/'assets/branding/ciphergba_icon.png'


def check(condition,msg):
    if not condition:raise AssertionError(msg)


def main():
    sys.path.insert(0,str(ROOT/'packaging/linux'))
    from resize_png import load_rgb_or_rgba
    width,height,channels,_=load_rgb_or_rgba(ICON)
    check((width,height)==(1254,1254),'Canonical GBA icon size changed')
    check(channels==3,'Canonical input should be high-resolution RGB PNG')
    readme=(ROOT/'README.md').read_text(encoding='utf-8')
    check('assets/branding/ciphergba_icon.png' in readme,'README does not use canonical artwork')
    sdl=(ROOT/'CipherGBA_Frontend/src/AppSDL2.cpp').read_text(encoding='utf-8')
    check('void uiDrawChrome(int w,int h)' in sdl,'Portable frontend menu bar is absent')
    check('uiDrawNoRom(w,top,bottom);' in sdl,'No-ROM frontend screen is absent')
    check('if(chrome)uiDrawChrome(w,h);' in sdl,'Frontend menu bar is not rendered')
    render=sdl.split('    void render(){',1)[1].split('    int loop(',1)[0]
    check('kAboutArtwork' not in render,'About artwork must never replace the main UI')
    check('app.mainMenu();' not in sdl.split('int main(int argc,char** argv){',1)[1],
          'Main window startup must not hide behind a modal popup')
    x11=(ROOT/'CipherGBA_Frontend/src/AppX11.cpp').read_text(encoding='utf-8')
    for section in ('LIBRARY','SETTINGS','SYSTEM','TAS','CHEATS','INFO'):
        check(section in sdl, 'Missing CP93 SDL2 section: '+section)
    for section in ('"Library"','"Settings"','"System"','"TAS"','"Cheats"','"Info"'):
        check(section in x11, 'Missing CP93 X11 section: '+section)
    for name in ('uiDrawInput','uiDrawCheats','uiPanelClick','uiDrawNoRom'):
        check(name in sdl,'Missing portable full frontend component: '+name)
    check('void drawNoRomDashboard()' in x11,'Native frontend lacks CP93 no-ROM dashboard')
    check('SDL_SetWindowIcon(window,icon)' in sdl,'Application window icon missing')
    check('void about()' in sdl and 'kAboutArtwork' in sdl,'Logo absent from About')
    for old in ('ciphergba_logo.png','ciphergba_readme_banner.png'):
        check(not (ROOT/'assets/branding'/old).exists(),'Old branding asset retained: '+old)
        check(old not in readme,'README references retired branding: '+old)
    with tempfile.TemporaryDirectory(prefix='ciphergba-icon-') as tmp:
        out=Path(tmp)
        native=ROOT/'packaging/icons/generate_native_icon.py'
        generated=ROOT/'packaging/branding/generate_embedded_artwork.py'
        for kind,ext,magic in [('ico','ico',b'\x00\x00\x01\x00'),('icns','icns',b'icns')]:
            dest=out/('CipherGBA.'+ext)
            subprocess.run([sys.executable,str(native),str(ICON),str(dest),kind],check=True)
            check(dest.read_bytes().startswith(magic),'Native icon '+kind+' invalid')
        hdr=out/'BrandArtwork.hpp'
        subprocess.run([sys.executable,str(generated),str(ICON),str(hdr)],check=True)
        code=hdr.read_text(encoding='utf-8')
        for label in ('kAboutArtwork','kIcon64','kIcon32'):
            check(label in code,'Generated header missing '+label)
        check('kAboutBanner' not in code,'Old banner embedded')
        bmp=out/'desktop.png'
        subprocess.run([sys.executable,str(ROOT/'packaging/linux/resize_png.py'),str(ICON),str(bmp)],check=True)
        x,y,_,_=load_rgb_or_rgba(bmp)
        check((x,y)==(256,256),'Linux/Flatpak icon must be exactly 256x256')
    print('PASS: one canonical CipherGBA art source, README, native ICO/ICNS, Linux 256px, embedded About/WM icons')


if __name__=='__main__':
    main()
