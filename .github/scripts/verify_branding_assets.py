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
    check('#include "ClassicSDL2Ui.inc"' in sdl,'Portable v0.1 UI not installed')
    check('uiDrawNoRom(w,top,bottom);' in sdl,'No-ROM renderer missing')
    check('uiDrawChrome(w,h);' in sdl,'Menu/status chrome not drawn')
    check('app.mainMenu();' not in sdl.split('int main(int argc,char** argv){',1)[1],
          'Startup must not hide behind a modal popup')
    x11=(ROOT/'CipherGBA_Frontend/src/AppX11.cpp').read_text(encoding='utf-8')
    classic=(ROOT/'CipherGBA_Frontend/src/ClassicSDL2Ui.inc').read_text(encoding='utf-8')
    for section in ('File','Input','Configure','System','TAS','Cheats','Help'):
        check('{"'+section+'"' in x11, 'Original X11 menu missing: '+section)
        check('{"'+section+'"' in classic, 'Portable original menu missing: '+section)
    for item in ('GBA Controller','Emulator Speed','Save State Slot',
                 'Controller Input','Record AVI','Configure GBA Cheat Codes'):
        check(item in x11 and item in classic,'Required v0.1 menu action missing: '+item)
    check('constexpr int Bar=38' in x11,'Native menubar must match v0.1')
    check('int uiBarHeight()const{return 38*uiScale();}' in classic,'Portable bar height wrong')
    check('int uiStatusHeight()const{return 30*uiScale();}' in classic,'Portable status bar height wrong')
    check('void uiDrawNoRom(int,int,int){}' in classic,'Main window must be blank without a ROM')
    check('drawNoRomDashboard' not in x11 and 'drawNoRomDashboard' not in classic,
          'Incorrect CP93 dashboard still present')
    for frontend in (sdl,x11,classic):
        check('fastForward_' not in frontend and 'fastKey_' not in frontend,
              'Fast-forward control must remain absent')
        check('200% DOUBLE SPEED' not in frontend,'Accelerated-playback action returned')
    check('CipherGBA v0.3.0' in sdl and 'CipherGBA v0.3.0' in x11,
          'Version 0.3 missing from frontends')
    check('SDL_SetWindowIcon(window,icon)' in sdl,'Application window icon missing')
    check('uiAboutArtwork(' in classic and 'kAboutArtwork' in classic,
          'About panel missing branding')
    render=sdl.split('    void render(){',1)[1].split('    int loop(',1)[0]
    check('kAboutArtwork' not in render,
          'Main viewport must never be replaced by the About art')
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
