// Created by Jacob Hodgkins.
// GBA scanline rasterizer with text/affine/bitmap BGs, priority,
// normal and affine OBJ, OBJ windows, BG windows, mosaic, and color effects.
// Rasterized incrementally in visible pixel spans. CPU writes to PPU-visible
// memory/registers finalize elapsed pixels at an approximate 4-cycle/pixel
// boundary; this does not emulate a fully cycle-accurate pixel pipeline.
#include "pixelgba/Subsystems.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
namespace pixelgba {
namespace {
using IO=std::array<std::uint8_t,1024>;
using VRAM=std::array<std::uint8_t,96*1024>;
using OAM=std::array<std::uint8_t,1024>;
unsigned rd16(const IO& v,unsigned offset){return v[offset]|(unsigned(v[offset+1])<<8);}
std::int32_t rd28(const IO& v,unsigned off){
    const std::uint32_t v28=std::uint32_t(rd16(v,off))|(std::uint32_t(rd16(v,off+2))<<16);
    return std::int32_t(v28<<4)>>4;
}
unsigned vr8(const VRAM& v,unsigned off){return v[off<v.size()?off:off%v.size()];}
unsigned vr16(const VRAM& v,unsigned off){
    if(off+1u<v.size())return unsigned(v[off])|(unsigned(v[off+1u])<<8u);
    return vr8(v,off)|(vr8(v,off+1u)<<8u);
}
std::uint16_t color(const std::array<std::uint8_t,1024>& p,unsigned n){return std::uint16_t(rd16(p,(n&511)*2)&0x7fffu);}
// A tiny 32-entry lookup stays cache-friendly and avoids
// three integer divide-by-31 colour expansions for every output pixel.
constexpr std::array<unsigned,32> expand5=[] {
    std::array<unsigned,32> a{};
    for(unsigned i=0;i<32;i++)a[i]=i*255u/31u;
    return a;
}();
// A 15-bit GBA palette entry maps to one immutable 24-bit RGB value.
// Precompute the exact existing 5-bit channel expansion once; renderSpan
// calls rgb() tens of thousands of times per game frame.
const std::array<unsigned,32768> rgbColors=[] {
    std::array<unsigned,32768> values{};
    for(unsigned bgr=0;bgr<values.size();++bgr)
        values[bgr]=(expand5[bgr&31u]<<16u)|
                    (expand5[(bgr>>5u)&31u]<<8u)|expand5[(bgr>>10u)&31u];
    return values;
}();
unsigned rgb(unsigned bgr){return rgbColors[bgr&0x7fffu];}
unsigned alpha(unsigned a,unsigned b,unsigned eva,unsigned evb){
    unsigned c=0;for(unsigned i=0;i<3;i++){
        unsigned v=std::min(31u,(((a>>(i*5))&31)*eva+((b>>(i*5))&31)*evb)>>4);
        c|=v<<(i*5);
    }return c;
}
unsigned brightness(unsigned a,unsigned y,bool brighten){
    unsigned c=0;for(unsigned i=0;i<3;i++){
        unsigned v=(a>>(i*5))&31;
        v=brighten?v+((31-v)*y>>4):v-(v*y>>4);
        c|=v<<(i*5);
    }return c;
}
struct Pixel { unsigned col=0,order=999,layer=5;bool present=false,semi=false;};
static constexpr unsigned spriteSize[3][4][2]={
    {{8,8},{16,16},{32,32},{64,64}},
    {{16,8},{32,8},{32,16},{64,32}},
    {{8,16},{8,32},{16,32},{32,64}}
};
bool inside(unsigned p,unsigned begin,unsigned end){return begin<=end?(p>=begin&&p<end):(p>=begin||p<end);}
// Decode registers once per raster span. Games often render multiple BGs
// and call sampleBg thousands of times with the same IO configuration.
// Spans are ended on PPU register/VRAM changes by GbaCore, so recomputing
// at each renderSpan boundary preserves mid-scanline effects.
struct BgSampler {
    unsigned bg=0,ctrl=0,mode=0,priority=0,charBase=0,screenBase=0;
    unsigned mosaic=0,scrollX=0,scrollY=0,width=0,height=0;
    std::int16_t pa=0,pb=0,pc=0,pd=0;
    bool text=false,affine=false;
    // A text BG tile-map entry applies to an entire 8x8 tile. The
    // source tile/key remain valid until the next raster-span boundary.
    unsigned lastTileKey=~0u,lastTileInfo=0;
};
BgSampler configureBg(unsigned bg,unsigned mode,const IO& io){
    BgSampler out;
    out.bg=bg;out.mode=mode;out.ctrl=rd16(io,8+bg*2);
    out.priority=out.ctrl&3u;
    out.charBase=((out.ctrl>>2)&3u)*0x4000u;
    out.screenBase=((out.ctrl>>8)&31u)*0x800u;
    out.mosaic=rd16(io,0x4c);
    out.text=mode==0||(mode==1&&bg<2);
    out.affine=(mode==1&&bg==2)||(mode==2&&bg>=2);
    if(out.text){
        const unsigned sizes=(out.ctrl>>14)&3u;
        out.width=(sizes&1u)?512u:256u;
        out.height=(sizes&2u)?512u:256u;
        out.scrollX=rd16(io,0x10+bg*4)&511u;
        out.scrollY=rd16(io,0x12+bg*4)&511u;
    }else if(out.affine||(bg==2&&mode>=3&&mode<=5)){
        const unsigned offset=0x20+(bg-2)*0x10;
        out.pa=std::int16_t(rd16(io,offset));
        out.pb=std::int16_t(rd16(io,offset+2));
        out.pc=std::int16_t(rd16(io,offset+4));
        out.pd=std::int16_t(rd16(io,offset+6));
    }
    return out;
}
Pixel sampleBg(BgSampler& b,unsigned x,unsigned y,const IO& io,
              const VRAM& vr,const std::array<std::uint8_t,1024>& pal,
              const std::array<std::array<std::int32_t,2>,2>& origin){
    const unsigned ctrl=b.ctrl,mode=b.mode;
    Pixel out{};out.order=b.priority*8+b.bg+1;out.layer=b.bg;
    if(ctrl&0x40){x-=x%((b.mosaic&15)+1);if(b.text)y-=y%(((b.mosaic>>4)&15)+1);}
    if(b.text){
        // Text BG dimensions are always powers of two (256 or 512 pixels).
        // A mask preserves exact wrapped coordinates without a runtime divide.
        const unsigned tx=(x+b.scrollX)&(b.width-1u);
        const unsigned ty=(y+b.scrollY)&(b.height-1u);
        const unsigned key=(tx>>3)|((ty>>3)<<6);
        if(b.lastTileKey!=key){
            const unsigned block=(tx/256)+(ty/256)*(b.width/256);
            b.lastTileInfo=vr16(vr,b.screenBase+block*2048+((ty%256)/8)*64+((tx%256)/8)*2);
            b.lastTileKey=key;
        }
        const unsigned tileInfo=b.lastTileInfo;
        const unsigned tile=tileInfo&1023;
        const unsigned px=(tileInfo&0x400)?7-(tx&7):(tx&7);
        const unsigned py=(tileInfo&0x800)?7-(ty&7):(ty&7);
        const bool eight=ctrl&0x80;
        const unsigned address=b.charBase+tile*(eight?64:32)+py*(eight?8:4)+(eight?px:px/2);
        const unsigned byte=vr8(vr,address),idx=eight?byte:((byte>>((px&1)*4))&15);
        if(!idx)return out;
        out.col=color(pal,idx+(eight?0:((tileInfo>>12)&15)*16));
    }else if(b.affine||(b.bg==2 && mode>=3&&mode<=5)){
        const unsigned index=b.bg-2;
        std::int32_t ox=origin[index][0],oy=origin[index][1];
        if(ctrl&0x40){
            const unsigned back=y%(((b.mosaic>>4)&15)+1);
            ox-=std::int32_t(b.pb)*std::int32_t(back);
            oy-=std::int32_t(b.pd)*std::int32_t(back);
        }
        std::int32_t tx=(ox+std::int32_t(b.pa)*std::int32_t(x))>>8;
        std::int32_t ty=(oy+std::int32_t(b.pc)*std::int32_t(x))>>8;
        if(b.affine){
            const unsigned size=128u<<((ctrl>>14)&3);
            if(ctrl&0x2000){tx&=size-1;ty&=size-1;}
            else if(tx<0||ty<0||unsigned(tx)>=size||unsigned(ty)>=size)return out;
            const unsigned tile=vr8(vr,b.screenBase+(unsigned(ty)/8)*(size/8)+unsigned(tx)/8);
            const unsigned idx=vr8(vr,b.charBase+tile*64+(unsigned(ty)&7)*8+(unsigned(tx)&7));
            if(!idx)return out;
            out.col=color(pal,idx);
        }else{
            const unsigned page=(rd16(io,0)&0x10)?0xa000u:0;
            const unsigned width=mode==5?160:240,height=mode==5?128:160;
            if(tx<0||ty<0||unsigned(tx)>=width||unsigned(ty)>=height)return out;
            const unsigned pos=unsigned(ty)*width+unsigned(tx);
            if(mode==4)out.col=color(pal,vr8(vr,page+pos));
            else out.col=vr16(vr,page+pos*2)&0x7fff;
        }
    }else return out;
    out.present=true;return out;
}
struct SpriteLine {std::array<Pixel,240> color{};std::array<bool,240> objWindow{};};
SpriteLine renderSprites(unsigned y,unsigned display,const IO& io,const VRAM& vr,
                         const std::array<std::uint8_t,1024>& pal,const OAM& oam,
                         unsigned beginX,unsigned endX){
    SpriteLine out{};
    const bool oneDim=display&0x40;
    const unsigned mos=rd16(io,0x4c);
    for(int idx=127;idx>=0;--idx){
        const unsigned base=unsigned(idx)*8;
        const unsigned a0=rd16(oam,base),a1=rd16(oam,base+2),a2=rd16(oam,base+4);
        const bool affine=a0&0x100,doubleSize=affine&&(a0&0x200);
        if(!affine&&(a0&0x200))continue; // OBJ disabled
        const unsigned shape=(a0>>14)&3,s=(a1>>14)&3;if(shape==3)continue;
        const unsigned w=spriteSize[shape][s][0],h=spriteSize[shape][s][1];
        const unsigned boxW=w*(doubleSize?2:1),boxH=h*(doubleSize?2:1);
        const unsigned originX=a1&511,originY=a0&255;
        const unsigned screenY=(y+256-originY)&255;
        if(screenY>=boxH)continue;
        const bool eight=a0&0x2000;
        const unsigned type=(a0>>10)&3;
        const unsigned priority=(a2>>10)&3;
        const unsigned bpp=eight?8:4;
        const unsigned tileBase=(a2&1023)&(eight?~1u:~0u);
        const bool mosaic=a0&0x1000;
        // Clip in OAM's 512-pixel wraparound domain before entering the
        // expensive affine/tile sampler. Sprites entirely past the right
        // edge have no visible work; those crossing 512 wrap to the left.
        // Every renderSpan consumes only [beginX,endX). Limit OBJ sampling
        // to that span too. This is important for games that split scanlines
        // with raster register writes: the previous implementation decoded
        // the same invisible pixels again on each subspan. Perform the clip
        // in the sprite's local coordinate domain to preserve 512px wrap.
        unsigned bxBegin=originX>=240u?(512u-originX):0u;
        unsigned bxEnd=std::min(boxW,originX<240u?(240u-originX):boxW);
        if(originX<240u){
            bxBegin=std::max(bxBegin,beginX>originX?beginX-originX:0u);
            bxEnd=std::min(bxEnd,endX>originX?endX-originX:0u);
        }else{
            bxBegin=std::max(bxBegin,512u-originX+beginX);
            bxEnd=std::min(bxEnd,512u-originX+endX);
        }
        if(bxBegin>=bxEnd)continue;
        // The affine transform matrix is fixed for this OAM entry and
        // raster span. Loading it once avoids four OAM reads per sprite pixel.
        int affinePa=0,affinePb=0,affinePc=0,affinePd=0;
        if(affine){
            const unsigned mat=((a1>>9)&31u)*32u;
            affinePa=std::int16_t(rd16(oam,mat+6u));
            affinePb=std::int16_t(rd16(oam,mat+14u));
            affinePc=std::int16_t(rd16(oam,mat+22u));
            affinePd=std::int16_t(rd16(oam,mat+30u));
        }
        for(unsigned bx=bxBegin;bx<bxEnd;bx++){
            const unsigned x=(originX+bx)&511u;
            unsigned xx=bx,yy=screenY;
            if(mosaic){const unsigned mw=((mos>>8)&15)+1,mh=((mos>>12)&15)+1;xx-=xx%mw;yy-=yy%mh;}
            if(affine){
                const int dx=int(xx)-int(boxW/2),dy=int(yy)-int(boxH/2);
                const int sx=((affinePa*dx+affinePb*dy)>>8)+int(w/2);
                const int sy=((affinePc*dx+affinePd*dy)>>8)+int(h/2);
                if(sx<0||sy<0||sx>=int(w)||sy>=int(h))continue;
                xx=unsigned(sx);yy=unsigned(sy);
            }else{
                if(a1&0x1000)xx=w-1-xx;
                if(a1&0x2000)yy=h-1-yy;
            }
            const unsigned tileRow=yy/8,tileColumn=xx/8;
            const unsigned tile=tileBase+tileRow*(oneDim?(w/8)*(bpp/4):32)+tileColumn*(bpp/4);
            // Bitmap modes use first 16KiB of OBJ memory for BG data.
            if((display&7)>=3 && tile<512)continue;
            const unsigned addr=0x10000u+((tile&1023)*32)+(yy&7)*(eight?8:4)+(eight?(xx&7):((xx&7)/2));
            if(addr>=vr.size())continue;
            const unsigned packed=vr8(vr,addr);
            const unsigned pix=eight?packed:((packed>>((xx&1)*4))&15);
            if(!pix)continue;
            if(type==2){out.objWindow[x]=true;continue;}
            if(type==3)continue;
            const unsigned rank=priority*8;
            // Draw sprites back-to-front for equal ranks; OAM index 0 wins ties.
            if(!out.color[x].present || rank<=out.color[x].order){
                out.color[x]={color(pal,256+pix+(eight?0:((a2>>12)&15)*16)),rank,4,true,type==1};
            }
        }
    }
    return out;
}
} // namespace
void Ppu::affineRegisterWritten(unsigned off,const IO& io){
    for(unsigned i=0;i<2;i++){
        const unsigned base=0x20+0x10*i;
        if(off>=base+8&&off<base+16){
            const unsigned axis=(off-base-8)/4,reg=base+8+4*axis;
            affineReference_[i][axis]=rd28(io,reg);
        }
    }
}
void Ppu::latchAffine(const IO& io){
    for(unsigned i=0;i<2;i++)for(unsigned axis=0;axis<2;axis++)
        affineReference_[i][axis]=rd28(io,0x28+0x10*i+4*axis);
}
void Ppu::advanceAffine(const IO& io){
    for(unsigned i=0;i<2;i++){
        const unsigned base=0x20+0x10*i;
        affineReference_[i][0]+=std::int16_t(rd16(io,base+2));
        affineReference_[i][1]+=std::int16_t(rd16(io,base+6));
    }
}
void Ppu::renderLine(unsigned y,const IO& io,const std::array<std::uint8_t,1024>& pal,
                     const VRAM& vr,const OAM& oam){
    renderSpan(y,0,Width,io,pal,vr,oam);
}
void Ppu::renderSpan(unsigned y,unsigned beginX,unsigned endX,const IO& io,
                     const std::array<std::uint8_t,1024>& pal,const VRAM& vr,const OAM& oam){
    if(y>=Height || beginX>=Width || beginX>=endX)return;
    endX=std::min(endX,Width);
    const unsigned display=rd16(io,0),mode=display&7;
    if(display&0x80){
        std::fill(framebuffer_.begin()+y*Width+beginX,
                  framebuffer_.begin()+y*Width+endX,0xffffffu);
        return;
    }
    // Register snapshots are valid for this entire span, because PPU-visible
    // writes flush the prior pixels before changing the underlying registers.
    std::array<BgSampler,4> bg={{configureBg(0,mode,io),configureBg(1,mode,io),
                                      configureBg(2,mode,io),configureBg(3,mode,io)}};
    // Single-layer/empty scenes occur frequently in splash screens and menus.
    // When no OBJ, window or color effect is enabled, avoid candidate sorting,
    // sprite setup and window testing entirely. Other display configurations
    // retain the complete hardware compositor below.
    if(!(display&0xf000u) && ((rd16(io,0x50)>>6u)&3u)==0u){
        const unsigned mask=(display>>8u)&15u;
        if(mask==0u){
            std::fill(framebuffer_.begin()+y*Width+beginX,
                      framebuffer_.begin()+y*Width+endX,rgb(color(pal,0)));
            return;
        }
        if((mask&(mask-1u))==0u){
            unsigned bgIndex=0;while(!(mask&(1u<<bgIndex)))++bgIndex;
            for(unsigned x=beginX;x<endX;++x){
                const Pixel pixel=sampleBg(bg[bgIndex],x,y,io,vr,pal,affineReference_);
                framebuffer_[y*Width+x]=rgb(pixel.present?pixel.col:color(pal,0));
            }
            return;
        }
    }
    const auto sprites=(display&(0x1000u|0x8000u))?
        renderSprites(y,display,io,vr,pal,oam,beginX,endX):SpriteLine{};
    // Pre-order BGs by their fixed hardware draw rank for this raster span.
    // In the effects-disabled path, as soon as a frontmost visible layer is
    // found there is no need to sample lower-priority BGs. Equal priority
    // still follows BG index; OBJ's rank is lower and therefore wins ties.
    // Span boundaries are flushed before register/VRAM writes, so this order
    // always uses the same register snapshot as the original compositor.
    std::array<unsigned,4> bgOrder{{0u,1u,2u,3u}};
    for(unsigned i=1;i<4u;++i){
        const unsigned item=bgOrder[i];
        unsigned j=i;
        while(j && bg[item].priority<bg[bgOrder[j-1u]].priority){
            bgOrder[j]=bgOrder[j-1u];
            --j;
        }
        bgOrder[j]=item;
    }
    const unsigned winIn=rd16(io,0x48),winOut=rd16(io,0x4a);
    const unsigned blend=rd16(io,0x50),coeff=rd16(io,0x52),fade=std::min(16u,rd16(io,0x54)&31u);
    const unsigned eva=std::min(16u,coeff&31u),evb=std::min(16u,(coeff>>8)&31u);
    for(unsigned x=beginX;x<endX;x++){
        unsigned region=0x3f; // no windows: all BG, OBJ and effects enabled
        if(display&0xe000){
            region=winOut&0x3f;
            if((display&0x8000)&&sprites.objWindow[x])region=(winOut>>8)&0x3f;
            // WIN0 wins overlaps against WIN1 and OBJ window.
            for(int w=1;w>=0;--w){
                if(!(display&(0x2000u<<w)))continue;
                const unsigned h=rd16(io,0x40+2*w),v=rd16(io,0x44+2*w);
                if(inside(x,(h>>8)&255,h&255) && inside(y,(v>>8)&255,v&255))
                    region=(winIn>>(w*8))&0x3f;
            }
        }
        // No alpha/brightness is possible when effects mode is off (or
        // masked by the current window), unless a semi-transparent OBJ
        // is present. The normal compositor sorts up to six candidates;
        // in this common case only the frontmost priority is observable.
        // Keep its stable OBJ/BG tie ordering and sample BGs in index order.
        // Semi-transparent OBJ can still trigger alpha in effects mode 0,
        // so it always falls through to the original sorting path.
        if((!(region&0x20u) || ((blend>>6u)&3u)==0u) &&
           !(sprites.color[x].present && sprites.color[x].semi &&
             (display&0x1000u) && (region&0x10u) && (region&0x20u))){
            unsigned nearestOrder=999u;
            unsigned nearestColor=color(pal,0);
            if((display&0x1000u) && (region&0x10u) && sprites.color[x].present){
                nearestOrder=sprites.color[x].order;
                nearestColor=sprites.color[x].col;
            }
            for(unsigned slot=0;slot<4u;++slot){
                const unsigned bgIndex=bgOrder[slot];
                if(!(display&(0x100u<<bgIndex)) || !(region&(1u<<bgIndex)))continue;
                // Sorted by hardware rank, so later BGs cannot be in front
                // of an already-selected opaque BG or eligible OBJ pixel.
                if(bg[bgIndex].priority*8u+bgIndex+1u >= nearestOrder)break;
                const Pixel p=sampleBg(bg[bgIndex],x,y,io,vr,pal,affineReference_);
                if(p.present){
                    nearestColor=p.col;
                    break;
                }
            }
            framebuffer_[y*Width+x]=rgb(nearestColor);
            continue;
        }
        std::array<Pixel,6> values{};
        unsigned count=0;
        values[count++]={color(pal,0),999,5,true,false};
        if((display&0x1000)&&(region&0x10)&&sprites.color[x].present)values[count++]=sprites.color[x];
        for(unsigned bgIndex=0;bgIndex<4;bgIndex++){
            if(!(display&(0x100u<<bgIndex))||!(region&(1u<<bgIndex)))continue;
            const auto p=sampleBg(bg[bgIndex],x,y,io,vr,pal,affineReference_);
            if(p.present)values[count++]=p;
        }
        // Stable insertion sort: six candidates maximum, avoids std::sort's
        // larger-range assumptions and preserves OBJ/BG ties deterministically.
        for(unsigned i=1;i<count;i++){
            const Pixel value=values[i];unsigned j=i;
            while(j>0&&value.order<values[j-1].order){values[j]=values[j-1];--j;}
            values[j]=value;
        }
        const Pixel first=values[0];
        unsigned output=first.col;
        if(region&0x20){
            const unsigned modeEffect=(blend>>6)&3;
            const bool target1=(blend&(1u<<first.layer))!=0;
            if((modeEffect==1&&target1)||first.semi){
                for(unsigned i=1;i<count;i++)if(blend&(1u<<(8+values[i].layer))){
                    output=alpha(first.col,values[i].col,eva,evb);break;
                }
            }else if(target1&&modeEffect==2)output=brightness(first.col,fade,true);
            else if(target1&&modeEffect==3)output=brightness(first.col,fade,false);
        }
        framebuffer_[y*Width+x]=rgb(output);
    }
}
} // namespace pixelgba
