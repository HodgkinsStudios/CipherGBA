// HodgkinsStudios CipherGBA. GBA cheat format specifications: GBATEK.
// Cipher implementations are original C++17 implementations of the published algorithms.
#include "CheatCodes.hpp"
#include "CheatSeedTables.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <sstream>
#include <utility>

namespace pixelgba::cheats {
namespace {
using u32=std::uint32_t;
using u16=std::uint16_t;
constexpr u32 delta=0x9E3779B9u;
constexpr std::array<u32,4> gsKey{{0x09F4FBBDu,0x9681884Au,0x352027E9u,0xF3DEE5A7u}};
constexpr std::array<u32,4> arKey{{0x7AA9648Fu,0x7FAE6994u,0xC0EFAAD5u,0x42712C57u}};
void tea(u32& a,u32& b,const std::array<u32,4>& k,bool decode) {
    u32 sum=decode?0xC6EF3720u:0u;
    for(unsigned i=0;i<32;i++) {
        if(decode){
            b-=(((a<<4)+k[2])^(a+sum)^((a>>5)+k[3]));
            a-=(((b<<4)+k[0])^(b+sum)^((b>>5)+k[1]));
            sum-=delta;
        }else{
            sum+=delta;
            a+=(((b<<4)+k[0])^(b+sum)^((b>>5)+k[1]));
            b+=(((a<<4)+k[2])^(a+sum)^((a>>5)+k[3]));
        }
    }
}
std::string strip(const std::string& x){
    const auto begin=x.find_first_not_of(" \t\r\n");if(begin==std::string::npos)return {};
    return x.substr(begin,x.find_last_not_of(" \t\r\n")-begin+1);
}
std::string upper(std::string s){for(char& c:s)c=char(std::toupper(static_cast<unsigned char>(c)));return s;}
bool hexWord(const std::string& s,u32& out,unsigned maxDigits=8){
    if(s.empty()||s.size()>maxDigits)return false;
    for(char c:s)if(!std::isxdigit(static_cast<unsigned char>(c)))return false;
    try{out=static_cast<u32>(std::stoul(s,nullptr,16));return true;}catch(...){return false;}
}
bool writable(u32 address,unsigned width){
    const unsigned region=address>>24;
    if(width!=1&&width!=2&&width!=4)return false;
    if((address&(width-1))!=0)return false;
    // Do not let a bad decoder create writes to ROM, BIOS or unmapped bus space.
    if(region==2||region==3||region==4||region==5||region==6||region==7||region==0xE){
        return (address&0xFFFFFFu)<=0xFFFFFFu-(width-1u);
    }
    return false;
}
bool readable(u32 address,unsigned width){return writable(address,width) || ((address>>24)==8&&width<=4);}
u32 mask(unsigned width){return width==4?0xFFFFFFFFu:((1u<<(8*width))-1u);}
void reseed(std::array<u32,4>& keys,u16 params,const std::uint8_t* a,const std::uint8_t* b){
    const unsigned high=params>>8,low=params&255;
    for(unsigned row=0;row<4;row++)for(unsigned col=0;col<4;col++)
        keys[row]=(keys[row]<<8)|std::uint8_t(a[(high+col)&255]+b[(low+row)&255]);
}
struct CBState {
    u32 master=0,rng=0;
    std::array<u32,4> seeds{};
    std::array<unsigned char,48> table{};
    u32 random(){
        u32 a=rng*0x41C64E6Du+0x3039u;
        u32 b=a*0x41C64E6Du+0x3039u;
        u32 c=b*0x41C64E6Du+0x3039u;
        rng=c;
        return (a<<14&0xC0000000u)|(b>>1&0x3FFF8000u)|(c>>16&0x7FFFu);
    }
    void setup(u32 op1,u16 op2){
        master=op1;
        rng=u32(op2&255)^0x1111u;
        for(unsigned i=0;i<48;i++)table[i]=std::uint8_t(i);
        for(unsigned i=0;i<80;i++){
            const auto x=random()%48,y=random()%48;
            std::swap(table[x],table[y]);
        }
        rng=0x4EFAD1C3u;
        for(unsigned i=0;i<((op1>>24)&15u);i++)random();
        seeds[2]=random();seeds[3]=random();
        rng=u32(op2>>8)^0xF254u;
        for(unsigned i=0;i<(op2>>8);i++)random();
        seeds[0]=random();seeds[1]=random();
    }
    void decode(u32& op1,u16& op2)const{
        if(!master)return;
        unsigned char bytes[6]={std::uint8_t(op1>>24),std::uint8_t(op1>>16),std::uint8_t(op1>>8),
            std::uint8_t(op1),std::uint8_t(op2>>8),std::uint8_t(op2)};
        for(int i=47;i>=0;i--){
            const unsigned j=table[unsigned(i)];
            const unsigned x=unsigned(i)/8u,y=j/8u,bx=unsigned(i)%8u,by=j%8u;
            const bool a=(bytes[x]>>bx)&1,b=(bytes[y]>>by)&1;
            bytes[x]=std::uint8_t((bytes[x]&~(1u<<bx))|(unsigned(b)<<bx));
            bytes[y]=std::uint8_t((bytes[y]&~(1u<<by))|(unsigned(a)<<by));
        }
        op1=(u32(bytes[0])<<24)|(u32(bytes[1])<<16)|(u32(bytes[2])<<8)|bytes[3];
        op2=u16(unsigned(bytes[4])<<8|bytes[5]);
        op1^=seeds[0];op2^=u16(seeds[1]);
        bytes[0]=std::uint8_t(op1>>24);bytes[1]=std::uint8_t(op1>>16);bytes[2]=std::uint8_t(op1>>8);
        bytes[3]=std::uint8_t(op1);bytes[4]=std::uint8_t(op2>>8);bytes[5]=std::uint8_t(op2);
        for(unsigned i=0;i<5;i++)bytes[i]^=std::uint8_t((master>>8)^bytes[i+1]);
        bytes[5]^=std::uint8_t(master>>8);
        for(unsigned i=5;i>0;i--)bytes[i]^=std::uint8_t(master^bytes[i-1]);
        bytes[0]^=std::uint8_t(master);
        op1=((u32(bytes[0])<<24)|(u32(bytes[1])<<16)|(u32(bytes[2])<<8)|bytes[3])^seeds[2];
        op2=u16((unsigned(bytes[4])<<8|bytes[5])^seeds[3]);
    }
};
struct Pair {u32 a=0,b=0;};
bool linePair(const std::string& s,Format fmt,Pair& result){
    std::string h;for(char c:s){if(std::isspace(static_cast<unsigned char>(c))||c=='-'||c==':')continue;
        if(!std::isxdigit(static_cast<unsigned char>(c)))return false;
        h+=c;}
    const unsigned second=fmt==Format::CodeBreaker?4:8;
    if(h.size()!=8+second)return false;
    return hexWord(h.substr(0,8),result.a)&&hexWord(h.substr(8),result.b,second);
}
void emit(std::vector<Operation>& out,Action action,u32 address,u32 value,unsigned width){
    const bool conditional=action>=Action::Equal && action<=Action::UnsignedGreater;
    out.push_back({action,address,value,width,1,0,0,conditional?1u:0u});
}
bool check(std::vector<Operation>& ops,std::string& error){
    if(ops.size()>2048){error="Cheat program is too large";return false;}
    for(const auto& o:ops){
        if(o.action==Action::EndIf||o.action==Action::Else||o.action==Action::Button)continue;
        if(o.action==Action::RomPatch){
            if(o.address>=0x02000000u || (o.address&1u)){
                error="ROM patch address is invalid";return false;
            }
            continue;
        }
        if(o.count>4096){error="Repeat count is too large";return false;}
        if((o.action==Action::Write||o.action==Action::WriteIndirect||o.action==Action::Or||o.action==Action::And||o.action==Action::Add)
            && !writable(o.address,o.width)){
            error="Code targets an unsupported or unaligned memory address";return false;
        }
        if(o.action>=Action::Equal&&o.action<=Action::UnsignedGreater&&!readable(o.address,o.width)){
            error="Conditional reads an unsupported memory address";return false;
        }
        if(o.count>1){
            const std::uint64_t end=std::uint64_t(o.address)+std::uint64_t(o.count-1)*o.addressStep;
            if(end>0xFFFFFFFFull || !writable(u32(end),o.width)){
                error="Repeated code crosses the writable memory boundary";return false;
            }
        }
    }
    return true;
}
} // namespace

void encryptGameShark(u32& a,u32& b,bool v3){tea(a,b,v3?arKey:gsKey,false);}
void decryptGameShark(u32& a,u32& b,bool v3){tea(a,b,v3?arKey:gsKey,true);}
std::string formatName(Format f){
    switch(f){case Format::Raw:return "RAW";case Format::CodeBreaker:return "CB";
        case Format::GameShark12:return "GS12";case Format::ActionReplay34:return "AR34";
        case Format::GameSharkRaw:return "GSRAW";case Format::ActionReplayRaw:return "ARRAW";}
    return "?";
}

bool compile(const std::string& input,Program& out,std::string& error){
    error.clear();Program result;result.source=strip(input);
    if(result.source.empty()){error="Empty cheat code";return false;}
    std::string body=result.source;
    const auto colon=body.find(':');
    if(colon!=std::string::npos){
        const auto prefix=upper(strip(body.substr(0,colon)));
        if(prefix=="RAW")result.format=Format::Raw;
        else if(prefix=="CB"||prefix=="CODEBREAKER")result.format=Format::CodeBreaker;
        else if(prefix=="GS"||prefix=="GS12"||prefix=="GAMESHARK")result.format=Format::GameShark12;
        else if(prefix=="AR"||prefix=="AR34"||prefix=="ACTIONREPLAY")result.format=Format::ActionReplay34;
        else if(prefix=="GSRAW")result.format=Format::GameSharkRaw;
        else if(prefix=="ARRAW")result.format=Format::ActionReplayRaw;
        else if(prefix.find_first_not_of("0123456789ABCDEF")==std::string::npos)
            result.format=Format::Raw;
        else {error="Unknown code format prefix";return false;}
        if(result.format!=Format::Raw||prefix=="RAW")body=body.substr(colon+1);
    }else{
        std::string first=body.substr(0,body.find(';'));std::string digits;
        for(char c:first)if(std::isxdigit(static_cast<unsigned char>(c)))digits+=c;
        result.format=digits.size()==12?Format::CodeBreaker:Format::GameShark12;
    }
    if(result.format==Format::Raw){
        std::string first=strip(body);if(first.rfind("RAW:",0)==0)first=first.substr(4);
        unsigned addr=0,val=0,width=32;
        if(std::sscanf(first.c_str(),"%x:%x:%u",&addr,&val,&width)<2||
            (width!=8&&width!=16&&width!=32)){error="RAW expects address:value:8|16|32";return false;}
        emit(result.instructions,Action::Write,addr,val,width/8);
        if(!check(result.instructions,error))return false;
        out=std::move(result);return true;
    }
    std::vector<Pair> pairs;
    std::stringstream lines(body);std::string fragment;
    while(std::getline(lines,fragment,';')){
        fragment=strip(fragment);if(fragment.empty())continue;
        Pair p;if(!linePair(fragment,result.format,p)){
            error="Expected "+std::string(result.format==Format::CodeBreaker?"8+4":"8+8")+" hex digits per line";return false;
        }
        pairs.push_back(p);
        if(pairs.size()>256){error="Too many code lines";return false;}
    }
    if(pairs.empty()){error="No code lines";return false;}
    CBState cb;
    std::array<u32,4> keys=(result.format==Format::ActionReplay34||result.format==Format::ActionReplayRaw)?arKey:gsKey;
    int pendingList=0;u32 listValue=0;
    unsigned pendingGSAddresses=0;u32 gsListValue=0;
    bool block=false;
    for(std::size_t i=0;i<pairs.size();i++){
        u32 a=pairs[i].a,b=pairs[i].b;
        if(result.format==Format::CodeBreaker){
            u16 v=u16(b);cb.decode(a,v);b=v;
            if(pendingList>0){
                // List entries pack 16-bit values in big-endian order in each 6-byte continuation.
                std::array<u16,3> values{{u16(a>>16),u16(a),u16(b)}};
                for(u16 item:values){
                    if(!pendingList)break;
                    const u16 swapped=u16((item>>8)|(item<<8));
                    emit(result.instructions,Action::Write,listValue,swapped,2);
                    listValue+=2;--pendingList;
                }
                continue;
            }
            const unsigned type=a>>28,addr=a&0x0FFFFFFFu;
            if(type==0||type==1){continue;} // CodeBreaker game-ID / hook metadata.
            if(type==9){cb.setup(a,u16(b));continue;}
            if(type==4){
                if(i+1>=pairs.size()){error="CodeBreaker fill requires a parameter line";return false;}
                // Fill parameter: upper half is additive value step, lower is count, second word is address stride.
                Pair param=pairs[++i];u16 p=u16(param.b);cb.decode(param.a,p);
                Operation op{Action::Write,addr,u16(b),2,param.a&0xFFFFu,p,u16(param.a>>16),0};
                if(!op.count){error="Empty CodeBreaker fill";return false;}
                result.instructions.push_back(op);continue;
            }
            if(type==5){pendingList=int(b);listValue=addr;
                if(pendingList==0||pendingList>192){error="Invalid CodeBreaker list length";return false;}
                continue;
            }
            Action action=Action::Write;unsigned width=2;
            switch(type){
                case 2:action=Action::Or;break;case 3:width=1;break;
                case 6:action=Action::And;break;case 7:action=Action::Equal;break;
                case 8:break;case 10:action=Action::NotEqual;break;
                case 11:action=Action::Greater;break;case 12:action=Action::Less;break;
                case 13:if(addr==0x20u){action=Action::BitClear;emit(result.instructions,action,0x04000130u,b,2);continue;}
                    error="Unsupported CodeBreaker special operation";return false;
                case 14:action=Action::Add;break;case 15:action=Action::BitSet;break;
                default:error="Unsupported CodeBreaker opcode";return false;
            }
            emit(result.instructions,action,addr,b,width);
            continue;
        }
        const bool ar=(result.format==Format::ActionReplay34||result.format==Format::ActionReplayRaw);
        const bool encrypted=(result.format==Format::GameShark12||result.format==Format::ActionReplay34);
        if(encrypted)tea(a,b,keys,true);
        if(pendingGSAddresses){
            for(u32 address:{a,b}){
                if(!pendingGSAddresses)break;
                emit(result.instructions,Action::Write,address,gsListValue,4);
                --pendingGSAddresses;
            }
            continue;
        }
        if(b==0x001DC0DEu)continue; // Cartridge/game ID marker.
        if(a==0xDEADFACEu){reseed(keys,u16(b),ar?tables::ar1:tables::gs1,ar?tables::ar2:tables::gs2);continue;}
        if(!ar){
            const u32 addr=a&0x0FFFFFFFu;
            switch(a>>28){
                case 0:emit(result.instructions,Action::Write,addr,b,1);break;
                case 1:emit(result.instructions,Action::Write,addr,b,2);break;
                case 2:emit(result.instructions,Action::Write,addr,b,4);break;
                case 3:{
                    // The header supplies the 32-bit value; continuation pairs
                    // contain up to two destination addresses each.
                    const unsigned count=(a&0xFFFFu);
                    if(count<2||count>129){error="GameShark list length out of range";return false;}
                    pendingGSAddresses=count-1;
                    gsListValue=b;
                    break;
                }
                case 6:
                    emit(result.instructions,Action::RomPatch,(a&0xFFFFFFu)<<1,u16(b),2);
                    break;
                case 8:{
                    const unsigned x=a&0x00F00000u;
                    if(x!=0x00100000u&&x!=0x00200000u){error="Unsupported GameShark device-button code";return false;}
                    emit(result.instructions,Action::Button,0,0,2);
                    emit(result.instructions,Action::Write,a&0x0F0FFFFFu,b,x==0x00100000u?1:2);
                    break;
                }
                case 13:{
                    Action cond=Action::Equal;
                    switch(b>>20){case 0:cond=Action::Equal;break;case 1:cond=Action::NotEqual;break;
                        case 2:cond=Action::LessEqual;break;case 3:cond=Action::GreaterEqual;break;
                        default:error="Unsupported GameShark conditional";return false;}
                    emit(result.instructions,cond,addr,b&0xFFFFu,2);break;
                }
                case 14:{
                    const unsigned n=(a>>16)&255u;
                    if(n==0||n>64){error="Invalid GameShark condition span";return false;}
                    Operation op{Action::Equal,b&0x0FFFFFFFu,a&0xFFFFu,2,1,0,0,n};
                    result.instructions.push_back(op);break;
                }
                case 15:continue; // Hardware hook not required for per-frame application.
                default:error="Unsupported GameShark opcode";return false;
            }
        }else{
            const u32 addr=(a&0xFFFFFu)|((a<<4)&0x0F000000u);
            if(a==0){
                if(b==0){continue;}
                if(b==0x40000000u){emit(result.instructions,Action::EndIf,0,0,1);block=false;continue;}
                if(b==0x60000000u){emit(result.instructions,Action::Else,0,0,1);continue;}
                if((b&0xFE000000u)==0x10000000u||
                   (b&0xFE000000u)==0x12000000u||
                   (b&0xFE000000u)==0x14000000u){
                    if(i+1>=pairs.size()){error="Action Replay button code needs a continuation";return false;}
                    Pair payload=pairs[++i];
                    if(encrypted)tea(payload.a,payload.b,keys,true);
                    const u32 addrButton=(b&0xFFFFFu)|((b<<4)&0x0F000000u);
                    const unsigned widthButton=1u<<((b>>25)&3u);
                    emit(result.instructions,Action::Button,0,0,1);
                    emit(result.instructions,Action::Write,addrButton,payload.a,widthButton);
                    continue;
                }
                if((b&0xFE000000u)==0x18000000u || (b&0xFE000000u)==0x1A000000u ||
                   (b&0xFE000000u)==0x1C000000u || (b&0xFE000000u)==0x1E000000u){
                    if(i+1>=pairs.size()){error="Action Replay ROM patch is missing its value";return false;}
                    Pair payload=pairs[++i];
                    if(encrypted)tea(payload.a,payload.b,keys,true);
                    emit(result.instructions,Action::RomPatch,(b&0xFFFFFFu)<<1,u16(payload.a),2);
                    continue;
                }
                if((b&0xFE000000u)==0x80000000u || (b&0xFE000000u)==0x82000000u ||
                   (b&0xFE000000u)==0x84000000u){
                    if(i+1>=pairs.size()){error="Action Replay slide requires continuation";return false;}
                    Pair payload=pairs[++i];if(encrypted)tea(payload.a,payload.b,keys,true);
                    const unsigned widthSlide=1u<<((b>>25)&3u);
                    Operation op{Action::Write,(b&0xFFFFFu)|((b<<4)&0x0F000000u),
                        payload.a&mask(widthSlide),widthSlide,(payload.b>>16)&255u,
                        (payload.b&0xFFFFu)*widthSlide,(payload.b>>24),0};
                    if(op.count==0){error="Empty Action Replay slide";return false;}
                    result.instructions.push_back(op);continue;
                }
                error="Unsupported Action Replay special code";return false;
            }
            if((a>>24)==0xC4u)continue; // ROM hook metadata.
            const unsigned width=1u<<((a>>25)&3u);
            if(width>4){error="Invalid Action Replay width";return false;}
            const u32 value=b&mask(width);
            if(a&0x38000000u){
                Action cond=Action::Equal;
                switch(a&0x38000000u){
                    case 0x08000000u:cond=Action::Equal;break;
                    case 0x10000000u:cond=Action::NotEqual;break;
                    case 0x18000000u:cond=Action::Less;break;
                    case 0x20000000u:cond=Action::Greater;break;
                    case 0x28000000u:cond=Action::UnsignedLess;break;
                    case 0x30000000u:cond=Action::UnsignedGreater;break;
                    case 0x38000000u:cond=Action::BitSet;break;
                }
                const unsigned action=(a>>30)&3u;
                if(action==3){error="Action Replay disable-condition unsupported";return false;}
                Operation op{cond,addr,value,width,1,0,0,action==1?2u:action==2?0u:1u};
                result.instructions.push_back(op);
                if(action==2)block=true;
                continue;
            }
            if(a&0x01000000u && (a&0xFE000000u)!=0xC6000000u){
                error="Unsupported Action Replay address flags";return false;
            }
            switch(a&0xC0000000u){
                case 0:{
                    Operation op{Action::Write,addr,value,width,1,width,0,0};
                    if(width<4){op.count=(b>>(8*width))+1u;
                        if(op.count>4096){error="Action Replay repeat count too large";return false;}}
                    result.instructions.push_back(op);break;
                }
                case 0x40000000u:{
                    // The 32-bit pointer at 'addr' identifies the destination.
                    Operation op{Action::WriteIndirect,addr,value,width,1,
                        width<4u?((b>>(8*width))*width):0u,0u,0u};
                    result.instructions.push_back(op);break;
                }
                case 0x80000000u:emit(result.instructions,Action::Add,addr,value,width);break;
                case 0xC0000000u:{
                    if((a&0xFE000000u)!=0xC6000000u){error="Unsupported Action Replay I/O opcode";return false;}
                    emit(result.instructions,Action::Write,0x04000000u|(a&0xFFFFFu),value,2);
                    break;
                }
            }
        }
    }
    if(pendingList>0){error="CodeBreaker list is missing continuation lines";return false;}
    if(pendingGSAddresses>0){error="GameShark list is missing continuation lines";return false;}
    if(block){error="Unterminated Action Replay conditional block";return false;}
    if(!check(result.instructions,error))return false;
    out=std::move(result);
    return true;
}

void execute(const Program& p,const Bus& bus){
    if(!p.enabled||!bus.read||!bus.write)return;
    unsigned skip=0;bool blockSkip=false;std::vector<bool> stack;
    for(const auto& o:p.instructions){
        if(o.action==Action::EndIf){if(!stack.empty()){blockSkip=stack.back();stack.pop_back();}continue;}
        if(o.action==Action::Else){blockSkip=!blockSkip;continue;}
        if(blockSkip)continue;
        if(skip){--skip;continue;}
        if(o.action==Action::Button){if(!bus.deviceButton||!bus.deviceButton())skip=1;continue;}
        if(o.action==Action::RomPatch){if(bus.patchRom16)bus.patchRom16(o.address,u16(o.value));continue;}
        if(o.action==Action::WriteIndirect){
            const auto address=bus.read(o.address,4)+o.addressStep;
            if(writable(address,o.width))bus.write(address,o.value&mask(o.width),o.width);
            continue;
        }
        const bool needsRead=o.action!=Action::Write;
        const u32 current=needsRead?bus.read(o.address,o.width)&mask(o.width):0u;
        const auto signedValue=[&](u32 v)->std::int32_t{
            if(o.width==1)return std::int8_t(v);
            if(o.width==2)return std::int16_t(v);
            return std::int32_t(v);
        };
        bool yes=true;
        switch(o.action){
            case Action::Equal:yes=current==(o.value&mask(o.width));break;
            case Action::NotEqual:yes=current!=(o.value&mask(o.width));break;
            case Action::Greater:yes=signedValue(current) > signedValue(o.value&mask(o.width));break;
            case Action::Less:yes=signedValue(current) < signedValue(o.value&mask(o.width));break;
            case Action::GreaterEqual:yes=current>=o.value;break;
            case Action::LessEqual:yes=current<=o.value;break;
            case Action::BitSet:yes=(current&o.value)!=0;break;
            case Action::BitClear:yes=(current&o.value)==0;break;
            case Action::UnsignedLess:yes=current<(o.value&mask(o.width));break;
            case Action::UnsignedGreater:yes=current>(o.value&mask(o.width));break;
            default:break;
        }
        if(o.action>=Action::Equal&&o.action<=Action::UnsignedGreater){
            if(!yes){if(o.skip==0){stack.push_back(blockSkip);blockSkip=true;}
                else skip=o.skip;}
            else if(o.skip==0)stack.push_back(blockSkip);
            continue;
        }
        for(unsigned i=0;i<o.count;i++){
            const u32 address=o.address+i*o.addressStep;
            const u32 value=o.value+i*o.valueStep;
            u32 next=value;
            switch(o.action){
                case Action::And:next=bus.read(address,o.width)&value;break;
                case Action::Or:next=bus.read(address,o.width)|value;break;
                case Action::Add:next=bus.read(address,o.width)+value;break;
                default:break;
            }
            bus.write(address,next&mask(o.width),o.width);
        }
    }
}
} // namespace pixelgba::cheats
