// Cross-platform CI verification of battery saves and save-state durability.
#include "pixelgba/Core.hpp"
#include "pixelgba/StateFiles.hpp"
#include "PortableTAS.hpp"
#include "PortableAVI.hpp"
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <iterator>
#include <vector>
#include <cstdint>
namespace fs=std::filesystem;
static void ok(bool yes,const std::string& what){if(!yes)throw std::runtime_error(what);}
static void createRom(const fs::path& path,unsigned byte){
    std::vector<std::uint8_t> b(16384);
    b[0]=0xFE;b[1]=0xFF;b[2]=0xFF;b[3]=0xEA;
    constexpr char title[]="PERSISTTEST ";
    for(unsigned i=0;i<12;i++)b[0xA0+i]=std::uint8_t(title[i]);
    constexpr char code[]="PS02";
    for(unsigned i=0;i<4;i++)b[0xAC+i]=std::uint8_t(code[i]);
    b[0xB2]=0x96;
    constexpr char marker[]="SRAM_V";
    for(unsigned i=0;i<6;i++)b[0x1E0+i]=std::uint8_t(marker[i]);
    b[0x234]=std::uint8_t(byte);
    unsigned sum=0;
    for(unsigned i=0xA0;i<=0xBC;i++)sum+=b[i];
    b[0xBD]=std::uint8_t(0x100u-((sum+0x19u)&255u));
    std::ofstream out(path,std::ios::binary);
    ok(bool(out),"create synthetic ROM");
    out.write(reinterpret_cast<const char*>(b.data()),std::streamsize(b.size()));
    out.close();ok(bool(out),"write synthetic ROM");
}
static std::vector<std::uint8_t> fileBytes(const fs::path& p){
    std::ifstream in(p,std::ios::binary);
    return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};
}
static void overwrite(const fs::path& p,const std::vector<std::uint8_t>& b){
    std::ofstream out(p,std::ios::binary|std::ios::trunc);
    ok(bool(out),"cannot open file");
    out.write(reinterpret_cast<const char*>(b.data()),std::streamsize(b.size()));
    out.close();ok(bool(out),"cannot write file");
}
// Hardware-visible cartridge backup-device protocol regression (synthetic only).
static void testBackupDevices(const fs::path& root) {
    using Save=pixelgba::SaveMemory;
    auto unlock=[](Save& s){s.write(0x5555,0xaa);s.write(0x2aaa,0x55);};
    auto program=[&](Save& s,unsigned a,unsigned v){
        unlock(s);s.write(0x5555,0xa0);s.write(a,std::uint8_t(v));
    };
    auto bank=[&](Save& s,unsigned b){unlock(s);s.write(0x5555,0xb0);s.write(0,std::uint8_t(b));};
    auto erase=[&](Save& s,unsigned a,bool chip){
        unlock(s);s.write(0x5555,0x80);unlock(s);
        s.write(chip?0x5555:a,chip?0x10:0x30);
    };
    auto roundtrip=[&](Save& s,const char* name,Save::Type type,std::size_t size){
        const auto path=root/name;std::string error;
        ok(s.save(path,error),"backup save: "+error);
        ok(fs::file_size(path)==size,"backup file size");
        Save result;result.configure(type);
        ok(result.load(path,error),"backup reload: "+error);
        ok(result.save(path,error),"backup replace: "+error);
        ok(fs::file_size(path)==size,"replacement file size");
        return result;
    };
    Save s;
    ok(s.size()==32768,"SRAM size");
    s.write(0x15,0x45);ok(s.read(0x8015)==0x45,"SRAM mirror");
    ok(roundtrip(s,"SRAM.sav",Save::Type::SRAM,32768).read(0x15)==0x45,
       "SRAM persistence");
    std::vector<std::uint8_t> old(65536,0xff);old[0x15]=0x45;old[0x8015]=0x88;
    overwrite(root/"legacy.sav",old);
    std::string error;Save legacy;
    ok(legacy.load(root/"legacy.sav",error),"legacy SRAM load");
    ok(legacy.read(0x15)==0x45&&legacy.read(0x8015)==0x45,"legacy SRAM mirror");
    roundtrip(legacy,"legacy.sav",Save::Type::SRAM,32768);
    Save f;f.configure(Save::Type::Flash64);
    unlock(f);f.write(0x5555,0x90);
    ok(f.read(0)==0x32&&f.read(1)==0x1b,"Flash64 ID");
    f.write(0,0xf0);ok(f.read(0)==0xff,"Flash64 exit ID");
    program(f,0x1234,0xa5);program(f,0x1234,0xfa);
    ok(f.read(0x1234)==0xa0,"Flash64 program bit rule");
    program(f,0x4321,0x53);erase(f,0x1234,false);
    ok(f.read(0x1234)==0xff&&f.read(0x4321)==0x53,"Flash64 sector erase");
    erase(f,0,true);ok(f.read(0x4321)==0xff,"Flash64 chip erase");
    program(f,0x2222,0x17);
    ok(roundtrip(f,"FLASH64.sav",Save::Type::Flash64,65536).read(0x2222)==0x17,
       "Flash64 persistence");
    Save g;g.configure(Save::Type::Flash128);
    unlock(g);g.write(0x5555,0x90);
    ok(g.read(0)==0x62&&g.read(1)==0x13,"Flash128 ID");
    g.write(0,0xf0);program(g,0x1234,0xc3);bank(g,1);
    program(g,0x1234,0x37);
    ok(g.read(0x1234)==0x37,"Flash128 bank1 program");
    bank(g,0);ok(g.read(0x1234)==0xc3,"Flash128 bank0 isolation");
    bank(g,1);erase(g,0x1234,false);
    ok(g.read(0x1234)==0xff,"Flash128 bank1 erase");
    bank(g,0);ok(g.read(0x1234)==0xc3,"Flash128 bank0 after erase");
    bank(g,1);program(g,0x2468,0x6e);bank(g,0);
    Save h=roundtrip(g,"FLASH128.sav",Save::Type::Flash128,131072);
    ok(h.read(0x1234)==0xc3,"Flash128 bank0 reload");
    bank(h,1);ok(h.read(0x2468)==0x6e,"Flash128 bank1 reload");
    const std::array<std::uint8_t,8> payload{{0x13,0x7d,0x09,0xff,0,0x80,0x21,0x3c}};
    auto eeprom=[&](Save& m,bool read,unsigned address,unsigned bits,
                    const std::array<std::uint8_t,8>& bytes){
        m.writeEeprom(1);m.writeEeprom(read?1:0);
        for(int i=int(bits)-1;i>=0;--i)m.writeEeprom((address>>i)&1u);
        if(!read)for(auto byte:bytes)for(int i=7;i>=0;--i)m.writeEeprom((byte>>i)&1u);
        m.writeEeprom(0);
    };
    auto eread=[&](Save& m,unsigned address,unsigned bits){
        std::array<std::uint8_t,8> none{},out{};
        eeprom(m,true,address,bits,none);
        for(unsigned i=0;i<4;i++)ok(m.readEeprom()==0,"EEPROM dummy bits");
        for(auto& b:out)for(int i=7;i>=0;--i)b|=std::uint8_t(m.readEeprom()<<i);
        ok(m.readEeprom()==1,"EEPROM idle high");
        return out;
    };
    Save e;e.configure(Save::Type::EEPROM512);
    eeprom(e,false,42,6,payload);ok(eread(e,42,6)==payload,"EEPROM512 serial");
    Save ecopy=roundtrip(e,"EEPROM512.sav",Save::Type::EEPROM512,512);
    ok(eread(ecopy,42,6)==payload,"EEPROM512 reload");
    Save k;k.configure(Save::Type::EEPROM8K);
    eeprom(k,false,903,14,payload);ok(eread(k,903,14)==payload,"EEPROM8K serial");
    Save kcopy=roundtrip(k,"EEPROM8K.sav",Save::Type::EEPROM8K,8192);
    ok(eread(kcopy,903,14)==payload,"EEPROM8K reload");
    Save upgrade;upgrade.configure(Save::Type::EEPROM512);upgrade.setEepromAddressBits(14);
    ok(upgrade.type()==Save::Type::EEPROM8K&&upgrade.size()==8192,
       "EEPROM 512 to 8K size promotion");
    std::cout<<"PASS: SRAM/Flash64/Flash128/EEPROM512/EEPROM8K backup device protocols\n";
}
// Hardware-only PPU clock-domain regression. Directly advance master clocks
// without executing guest instructions, so this cannot tune a test ROM's PC.
static void verifyDisplayClockEdges(){
    // Never put this >550KiB device on the already occupied Windows CI stack.
    auto ppuCore=std::make_unique<pixelgba::GbaCore>();
    auto& core=*ppuCore;
    const auto disp=[&](){return core.read16(0x04000004u);};
    const auto y=[&](){return core.read16(0x04000006u);};
    core.write16(0x04000004u,0x0018u); // HBlank + VBlank IRQ enables
    core.write16(0x04000200u,0x0003u); // IE, both interrupt sources
    ok(core.cycles()==0u&&y()==0u&&!(disp()&3u),"initial video clock");
    for(unsigned row=0;row<160u;row++){
        ok(y()==row&&!(disp()&2u),"visible scanline beginning");
        core.advanceHardware(1005u);
        ok(y()==row&&!(disp()&2u),"premature HBlank flag");
        core.advanceHardware(1u);
        ok(y()==row&&(disp()&2u)==2u,"HBlank not asserted at clock 1006");
        ok((core.read16(0x04000202u)&2u)!=0u,"HBlank IRQ missing at edge");
        core.write16(0x04000202u,2u); // IF is W1C
        ok((core.read16(0x04000202u)&2u)==0u,"HBlank IF clear");
        core.advanceHardware(1u);
        ok((disp()&2u)!=0u&&(core.read16(0x04000202u)&2u)==0u,
           "HBlank IRQ repeated inside scanline");
        core.advanceHardware(225u);
        ok(y()==row+1u&&!(disp()&2u),"HBlank not cleared at next line");
        ok(core.cycles()==(row+1ull)*1232ull,"scanline duration drift");
    }
    ok((disp()&1u)!=0u,"VBlank absent at scanline 160");
    ok((core.read16(0x04000202u)&1u)!=0u,"VBlank IRQ edge missing");
    core.write16(0x04000202u,1u);
    for(unsigned row=160u;row<228u;row++){
        core.advanceHardware(1232u);
        ok(y()==(row+1u)%228u,"vertical counter progression");
    }
    ok(core.frames()==1u && y()==0u && !(disp()&3u),
       "228-scanline frame wrap");
    ok(core.cycles()==228ull*1232ull,"frame clocks drifted");
    std::cout<<"PASS: PPU HBlank @1006, 1232-clock lines, IRQ edge, 228-line frame\n";
}
int main(int argc,char** argv){
    try{
        ok(argc==2,"usage: CipherGBA_PersistenceSmoke TEMP_DIRECTORY");
        const auto root=fs::u8path(argv[1]);fs::create_directories(root);
        const auto rom=root/"rom.gba",foreign=root/"foreign.gba",state=root/"state.cgs";
        createRom(rom,0x15);createRom(foreign,0x26);
        auto coreOwner=std::make_unique<pixelgba::GbaCore>();
        auto& core=*coreOwner;std::string err;
        ok(core.configureSaveDirectory(root/"saves",err),"configure saves "+err);
        ok(core.loadCartridge(rom,err),"load ROM "+err);
        ok(core.saveType()==pixelgba::SaveMemory::Type::SRAM,"SRAM detection");
        for(unsigned i=0;i<3;i++)ok(core.runFrame(),"frame run");
        core.write8(0x02001234,0x12);core.write8(0x0E000000,0xA5);
        ok(core.read8(0x02001234)==0x12&&core.read8(0x0E000000)==0xA5,
           "RAM/SRAM write");
        ok(pixelgba::StateCodec::save(core,state,err),"state save "+err);
        const auto previous=fileBytes(state);ok(previous.size()>100,"short state");
        core.write8(0x02001234,0x34);core.write8(0x0E000000,0x5A);
        ok(pixelgba::StateCodec::save(core,state,err),"state replacement "+err);
        core.write8(0x02001234,0x99);
        ok(pixelgba::StateCodec::load(core,state,err),"state load "+err);
        ok(core.read8(0x02001234)==0x34&&core.read8(0x0E000000)==0x5A,
           "state restore");
        const auto valid=fileBytes(state);ok(valid.size()>100&&valid!=previous,
                                             "state not replaced");
        core.write8(0x02001234,0x99);
        auto broken=valid;broken.back()^=0xA5;overwrite(state,broken);
        ok(!pixelgba::StateCodec::load(core,state,err),"accepted corrupt state");
        ok(core.read8(0x02001234)==0x99,"corrupt state modified RAM");
        broken=valid;broken.resize(broken.size()-16);overwrite(state,broken);
        ok(!pixelgba::StateCodec::load(core,state,err),"accepted truncated state");
        ok(core.read8(0x02001234)==0x99,"truncated state modified RAM");
        overwrite(state,valid);
        ok(pixelgba::StateCodec::load(core,state,err),"restore saved state");
        ok(core.flushSave(err),"SRAM flush "+err);
        auto save=fileBytes(core.savePath());
        ok(save.size()==32768&&save[0]==0x5A,"first SRAM persistence");
        core.write8(0x0E000000,0xB6);
        ok(core.flushSave(err),"replace battery "+err);
        save=fileBytes(core.savePath());
        ok(save.size()==32768&&save[0]==0xB6,"second SRAM replacement");
        auto reloadedOwner=std::make_unique<pixelgba::GbaCore>();
        auto& reloaded=*reloadedOwner;
        ok(reloaded.configureSaveDirectory(root/"saves",err),"reload configure");
        ok(reloaded.loadCartridge(rom,err),"reload ROM");
        ok(reloaded.read8(0x0E000000)==0xB6,"battery reload in new core");
        auto wrongOwner=std::make_unique<pixelgba::GbaCore>();
        auto& wrong=*wrongOwner;
        ok(wrong.loadCartridge(foreign,err),"foreign cartridge load");
        ok(!pixelgba::StateCodec::load(wrong,state,err),"foreign ROM state accepted");
        // Portable TAS movie: deterministic button masks, ROM guard, invalid data.
        const auto movie=root/"tas"/"smoke.cgtas";
        ciphergba_host::Tas writer;
        writer.begin();
        const std::array<std::uint16_t,4> sequence{{1u,0x155u,0x2AAu,0x3FFu}};
        for(auto mask:sequence){
            std::uint16_t got=0;
            ok(writer.consume(mask,got)&&got==mask,"TAS input capture");
        }
        ok(writer.save(movie,core.cartridge().fingerprint(),err),"TAS movie save");
        const auto recorded=fileBytes(movie);
        ok(recorded.size()==16u+2u*sequence.size(),"TAS movie length");
        ciphergba_host::Tas playback;
        ok(playback.load(movie,core.cartridge().fingerprint(),err),"TAS movie load");
        for(auto wanted:sequence){
            std::uint16_t actual=0;
            ok(playback.consume(0,actual)&&actual==wanted,"TAS playback mismatch");
        }
        ok(!playback.playback(),"TAS did not stop after last input");
        ok(!playback.load(movie,0x12345678,err),"TAS accepted foreign ROM");
        auto malformed=recorded;malformed.back()=0xffu;overwrite(movie,malformed);
        ok(!playback.load(movie,core.cartridge().fingerprint(),err),
           "TAS accepted input mask beyond ten buttons");
        // AVI: frame/audio write, RIFF/AVI headers, and proper close.
        ciphergba_host::AviWriter avi;
        const auto capture=root/"captures"/"smoke.avi";
        ok(avi.open(capture,err),"AVI open "+err);
        std::array<std::uint32_t,240u*160u> pixels{};
        for(std::size_t i=0;i<pixels.size();i++)
            pixels[i]=std::uint32_t((i*13u)&0xffffffu);
        std::vector<std::int16_t> sound(1098,314);
        for(unsigned i=0;i<3;i++){
            ok(avi.appendFrame(pixels),"AVI video write");
            ok(avi.appendAudio(sound),"AVI PCM write");
        }
        ok(avi.frames()==3&&avi.close(),"AVI close");
        const auto video=fileBytes(capture);
        ok(video.size()>240u*160u*3u*3u,"AVI output truncated");
        ok(std::memcmp(video.data(),"RIFF",4)==0 &&
           std::memcmp(video.data()+8,"AVI ",4)==0,"AVI signature mismatch");
        testBackupDevices(root);
        verifyDisplayClockEdges();
        std::cout<<"PASS: battery, state, TAS and AVI writer regressions\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;
    }
}
