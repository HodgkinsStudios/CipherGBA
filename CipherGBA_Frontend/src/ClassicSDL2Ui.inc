    // Faithful cross-platform renderer of CipherGBA v0.1.0 Linux X11
    // frontend. Only host presentation code lives here. The GBA core is not
    // modified, and fast-forward/rewind controls are intentionally omitted.
    enum class UiPanel { Game, Controller, Cheats, About, RecordingDirectory };
    enum UiCmd {
        UNone=0, ULoadRom=1, ULoadBios=2, USaveState=3, ULoadState=4,
        UScreenshot=5, UNextSlot=6, UResetRom=7, UQuit=8,
        UFullScreen=9, UWindowed=10, UPause=12, UMute=13,
        UInput=16, UCheats=17, UAbout=19,
        URecordToast=22, URecordTimer=23, UStretch=24, UEnableCheats=25,
        UTasRecord=30, UTasStop=31, UTasPlayback=32, UTasStopPlayback=33,
        UTasReset=34, URecordingFolder=35, URecordAVI=36, UStopAVI=37,
        USpeed100=101, USpeed50=102, USpeed25=103,
        UAudio1=110, UAudio2=111, UAudio3=112,
        UAudio4=113, UAudio5=114, UAudio6=115,
        USlot1=120, USlot2=121, USlot3=122, USlot4=123
    };
    struct UiItem {
        std::string text;
        int command=0, submenu=0;
        bool separator=false;
    };
    struct UiMenu {
        std::string title;
        std::vector<UiItem> items;
    };
    static UiItem uiActionItem(const char* text,int cmd) {
        return {text,cmd,0,false};
    }
    static UiItem uiBranchItem(const char* text,int submenu) {
        return {text,0,submenu,false};
    }
    static UiItem uiSeparator() { return {"",0,0,true}; }

    UiPanel uiPanel_=UiPanel::Game;
    int uiSubmenu_=-1, uiHover_=-1,uiSubHover_=-1;
    int uiSelectedCheat_=-1,uiCheatScroll_=0;
    bool uiCaptureSingle_=false,uiCheatEditing_=false;
    bool uiBeforePanelPaused_=false;
    bool uiRecordToast_=true,uiRecordTimer_=false,uiTasReset_=true;
    std::string uiCheatText_,uiDirectoryText_,uiNoticeText_;
    fs::path uiRecordingDirectory_;
    SDL_Texture* uiAboutTexture_=nullptr;
    std::chrono::steady_clock::time_point uiNoticeUntil_{};
    double uiActualFps_=0.0;

    const std::vector<UiMenu>& uiMenus()const{
        static const std::vector<UiMenu> menus={
            {"File",{uiActionItem("Load ROM...",ULoadRom),
                     uiActionItem("Load BIOS...",ULoadBios),
                     uiActionItem("Reset ROM",UResetRom),
                     uiSeparator(),uiActionItem("Quit",UQuit)}},
            {"Input",{uiBranchItem("GBA Controller",2)}},
            {"Configure",{uiActionItem("Controller Input",UInput),
                  uiActionItem("Record With Toast",URecordToast),
                  uiActionItem("Record With Timer",URecordTimer),
                  uiActionItem("Custom AVI Output Location...",URecordingFolder),
                  uiBranchItem("Emulator Speed",4)}},
            {"System",{uiActionItem("Full Screen",UFullScreen),
                 uiActionItem("Full Screen (Stretched)",UStretch),
                 uiActionItem("Windowed",UWindowed),
                 uiActionItem("Pause / Resume",UPause),uiSeparator(),
                 uiActionItem("Mute",UMute),uiBranchItem("APU",5),
                 uiActionItem("Save State",USaveState),
                 uiActionItem("Load State",ULoadState),
                 uiBranchItem("Save State Slot",6),
                 uiActionItem("Screenshot",UScreenshot),uiSeparator(),
                 uiActionItem("Record AVI",URecordAVI),
                 uiActionItem("Stop Recording",UStopAVI)}},
            {"TAS",{uiActionItem("Record Inputs",UTasRecord),
               uiActionItem("Stop Recording Inputs",UTasStop),
               uiActionItem("View Recorded Inputs",UTasPlayback),
               uiActionItem("Stop Viewing Inputs",UTasStopPlayback),
               uiActionItem("Reset Before Input Recording",UTasReset)}},
            {"Cheats",{uiActionItem("Configure GBA Cheat Codes",UCheats),
                  uiActionItem("Enable Cheats",UEnableCheats)}},
            {"Help",{uiActionItem("Info",UAbout)}}
        };
        return menus;
    }
    std::vector<UiItem> uiSubItems(int submenu)const{
        switch(submenu){
        case 2:return {uiActionItem("Controller Input",UInput)};
        case 4:return {uiActionItem("100%",USpeed100),
                       uiActionItem("50%",USpeed50),
                       uiActionItem("25%",USpeed25)};
        case 5:return {uiActionItem("PSG Square 1",UAudio1),
                       uiActionItem("PSG Square 2",UAudio2),
                       uiActionItem("PSG Wave",UAudio3),
                       uiActionItem("PSG Noise",UAudio4),
                       uiActionItem("Direct Sound A",UAudio5),
                       uiActionItem("Direct Sound B",UAudio6)};
        case 6:return {uiActionItem("1",USlot1),uiActionItem("2",USlot2),
                       uiActionItem("3",USlot3),uiActionItem("4",USlot4)};
        }
        return {};
    }

    int uiScale()const{
        int ww=0,wh=0,rw=0,rh=0;
        SDL_GetWindowSize(window,&ww,&wh);
        SDL_GetRendererOutputSize(renderer,&rw,&rh);
        return std::clamp((rw+std::max(1,ww)/2)/std::max(1,ww),1,3);
    }
    int uiBarHeight()const{return 38*uiScale();}
    int uiStatusHeight()const{return 30*uiScale();}
    bool uiChromeVisible()const{
        return !fullscreen||uiPanel_!=UiPanel::Game||uiMenu_>=0||
               (uiMouseY_>=0&&uiMouseY_<uiBarHeight());
    }
    void uiMapMouse(int& x,int& y)const{
        int ww=0,wh=0,rw=0,rh=0;
        SDL_GetWindowSize(window,&ww,&wh);
        SDL_GetRendererOutputSize(renderer,&rw,&rh);
        x=x*rw/std::max(1,ww);y=y*rh/std::max(1,wh);
    }
    int uiFontWidth(const std::string& s)const{return static_cast<int>(s.size())*9;}
    int uiRootWidth(int index)const{
        return uiFontWidth(uiMenus()[static_cast<std::size_t>(index)].title)+36;
    }
    int uiRootX(int index)const{
        int x=0;for(int i=0;i<index;++i)x+=uiRootWidth(i);
        return x;
    }
    int uiPopupWidth(int menu)const{
        int w=260;for(const auto& item:uiMenus()[static_cast<std::size_t>(menu)].items)
            w=std::max(w,uiFontWidth(item.text)+73);
        return w;
    }
    int uiTabAt(int x,int y)const{
        const int sc=uiScale();x/=sc;y/=sc;
        if(y<0||y>=38)return -1;
        for(std::size_t i=0;i<uiMenus().size();++i){
            const int left=uiRootX(static_cast<int>(i));
            if(x>=left&&x<left+uiRootWidth(static_cast<int>(i)))return static_cast<int>(i);
        }
        return -1;
    }
    static SDL_Color uiColor(std::uint32_t c){
        return {static_cast<Uint8>((c>>16)&0xff),
                static_cast<Uint8>((c>>8)&0xff),
                static_cast<Uint8>(c&0xff),255};
    }
    void uiFill(int x,int y,int w,int h,std::uint32_t color){
        if(w<1||h<1)return;
        const auto c=uiColor(color);
        SDL_SetRenderDrawColor(renderer,c.r,c.g,c.b,c.a);
        const SDL_Rect r{x,y,w,h};
        SDL_RenderFillRect(renderer,&r);
    }
    void uiRect(int x,int y,int w,int h,std::uint32_t color){
        const int sc=uiScale();
        uiFill(x*sc,y*sc,w*sc,h*sc,color);
    }
    void uiOutline(int x,int y,int w,int h,std::uint32_t color){
        if(w<1||h<1)return;
        const auto c=uiColor(color);
        SDL_SetRenderDrawColor(renderer,c.r,c.g,c.b,c.a);
        const SDL_Rect r{x*uiScale(),y*uiScale(),w*uiScale(),h*uiScale()};
        SDL_RenderDrawRect(renderer,&r);
    }
    void uiLabel(int x,int baseline,const std::string& text,
                 std::uint32_t color=0xeef3f8){
        const int sc=uiScale();
        const int fontHeight=2*sc;
        const auto c=uiColor(color);
        SDL_SetRenderDrawColor(renderer,c.r,c.g,c.b,c.a);
        int column=0;
        for(unsigned char ch:text){
            if(ch==' '){++column;continue;}
            const ciphergba_about::Glyph* glyph=nullptr;
            for(const auto& g:ciphergba_about::kGlyphs)
                if(static_cast<unsigned char>(g.ch)==ch){glyph=&g;break;}
            // The 0.1 desktop font supports mixed-case text and punctuation.
            if(!glyph){
                for(const auto& g:ciphergba_about::kGlyphs)
                    if(g.ch=='?'){glyph=&g;break;}
            }
            if(glyph){
                for(int row=0;row<7;++row)for(int px=0;px<5;++px){
                    if(glyph->rows[row][px]=='1'){
                        // Resample the 5-column bitmap into an eight-pixel
                        // glyph and retain a one-pixel inter-glyph gap. This
                        // reproduces the v0.1 X11 9x15 fallback font metrics.
                        const int begin=(px*8)/5;
                        const int end=((px+1)*8)/5;
                        const SDL_Rect pixel{(x+column*9+begin)*sc,
                            (baseline-15+row*2)*sc,(end-begin)*sc,fontHeight};
                        SDL_RenderFillRect(renderer,&pixel);
                    }
                }
            }
            ++column;
        }
    }
    void uiButton(int x,int y,int w,int h,const std::string& label,bool selected=false){
        uiRect(x,y,w,h,selected?0x285580:0x23384b);
        uiOutline(x,y,w,h,0x557da1);
        uiLabel(x+12,y+h/2+6,label,0xe9f2ff);
    }
    bool uiInside(int x,int y,int l,int t,int w,int h)const{
        return x>=l&&x<l+w&&y>=t&&y<t+h;
    }
    void uiNotice(std::string text){
        uiNoticeText_=std::move(text);
        uiNoticeUntil_=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    }
    bool uiChecked(int cmd)const{
        if(cmd==UMute)return muted;
        if(cmd==UPause)return paused;
        if(cmd==UFullScreen)return fullscreen&&!stretched;
        if(cmd==UStretch)return fullscreen&&stretched;
        if(cmd==UWindowed)return !fullscreen;
        if(cmd>=USpeed100&&cmd<=USpeed25)
            return speedPercent_==(cmd==USpeed100?100:cmd==USpeed50?50:25);
        if(cmd>=USlot1&&cmd<=USlot4)return stateSlot==cmd-USlot1+1;
        if(cmd>=UAudio1&&cmd<=UAudio6)return (core.audioChannelMask()&(1u<<(cmd-UAudio1)))!=0;
        if(cmd==UEnableCheats)return cheatsEnabled_;
        if(cmd==UTasReset)return uiTasReset_;
        if(cmd==URecordToast)return uiRecordToast_;
        if(cmd==URecordTimer)return uiRecordTimer_;
        return false;
    }

    void uiOpenInput(){
        uiBeforePanelPaused_=paused;
        paused=true;pausedBeforeInput_=uiBeforePanelPaused_;
        oldKeyMap_=keyMap_;oldPadMap_=padMap_;oldAxisMap_=axisMap_;
        remap_=RemapMode::None;uiCaptureSingle_=false;
        uiPanel_=UiPanel::Controller;
        uiNotice("CONTROLLER INPUT");
    }
    void uiCloseInput(bool save){
        if(save){
            if(!saveBindings())return;
            uiNotice("CONTROLLER INPUT SAVED");
        }else{
            keyMap_=oldKeyMap_;padMap_=oldPadMap_;axisMap_=oldAxisMap_;
        }
        uiPanel_=UiPanel::Game;
        remap_=RemapMode::None;uiCaptureSingle_=false;
        paused=uiBeforePanelPaused_;
    }
    void uiOpenCheats(){
        uiBeforePanelPaused_=paused;
        paused=true;uiPanel_=UiPanel::Cheats;
        uiSelectedCheat_=-1;uiCheatScroll_=0;uiCheatEditing_=false;
        cheats_.clear();loadCheats();
    }
    void uiReturnPanel(){
        if(uiPanel_==UiPanel::Controller){uiCloseInput(false);return;}
        uiPanel_=UiPanel::Game;
        uiCheatEditing_=false;SDL_StopTextInput();
        remap_=RemapMode::None;uiCaptureSingle_=false;
        paused=uiBeforePanelPaused_;
    }
    void uiSubmitCheat(){
        std::string source=uiCheatText_;
        for(char& c:source)if(c=='\r'||c=='\n')c=';';
        if(cheats_.size()>=64){uiNotice("CHEAT LIST FULL");return;}
        pixelgba::cheats::Program program;
        std::string error;
        if(!pixelgba::cheats::compile(source,program,error)){
            uiNotice(error.empty()?"INVALID GBA CODE":"INVALID GBA CODE: "+error);
            return;
        }
        cheats_.push_back(std::move(program));
        uiCheatScroll_=std::max(0,static_cast<int>(cheats_.size())-10);
        uiSelectedCheat_=static_cast<int>(cheats_.size())-1;
        cheatsEnabled_=true;
        if(saveCheats())uiNotice("GBA CHEAT ADDED");
        uiCheatEditing_=false;uiCheatText_.clear();SDL_StopTextInput();
    }
    void uiOpenRecordingFolder(){
        uiBeforePanelPaused_=paused;
        paused=true;uiPanel_=UiPanel::RecordingDirectory;
        const auto current=uiRecordingDirectory_.empty()?data/"recordings":uiRecordingDirectory_;
        uiDirectoryText_=current.u8string();
        SDL_StartTextInput();
    }
    void uiSaveRecordingFolder(){
        const fs::path dest=fs::u8path(uiDirectoryText_);
        if(dest.empty()){uiNotice("FOLDER PATH IS EMPTY");return;}
        std::error_code ec;
        fs::create_directories(dest,ec);
        if(ec||!fs::is_directory(dest,ec)){
            uiNotice("UNAVAILABLE RECORDING FOLDER");
            return;
        }
        uiRecordingDirectory_=dest;
        uiNotice("RECORDING FOLDER UPDATED");
        uiReturnPanel();
    }
    void uiTasAction(int command){
        if(!core.cartridge().loaded()){uiNotice("LOAD A ROM FIRST");return;}
        std::string error;
        if(command==UTasReset){uiTasReset_=!uiTasReset_;return;}
        if(command==UTasRecord){
            if(tas_.recording()&&!tas_.save(tasPath(),core.cartridge().fingerprint(),error)){
                uiNotice("TAS SAVE FAILED");return;
            }
            if(uiTasReset_){
                if(!core.flushSave(error)){uiNotice("CARTRIDGE SAVE FAILED");return;}
                core.reset();
            }
            if(audio)SDL_ClearQueuedAudio(audio);
            tas_.begin();uiNotice("TAS INPUT RECORDING STARTED");
        }else if(command==UTasStop){
            if(!tas_.save(tasPath(),core.cartridge().fingerprint(),error))uiNotice("TAS SAVE FAILED");
            else uiNotice("TAS INPUT RECORDING SAVED");
        }else if(command==UTasPlayback){
            if(tas_.recording()&&!tas_.save(tasPath(),core.cartridge().fingerprint(),error)){
                uiNotice("TAS SAVE FAILED");return;
            }
            if(!tas_.load(tasPath(),core.cartridge().fingerprint(),error)){
                uiNotice("NO RECORDED INPUT MOVIE");return;
            }
            if(!core.flushSave(error)){
                tas_.stopPlayback();uiNotice("SAVE FLUSH FAILED");return;
            }
            core.reset();if(audio)SDL_ClearQueuedAudio(audio);
            uiNotice("TAS INPUT PLAYBACK");
        }else if(command==UTasStopPlayback){
            tas_.stopPlayback();uiNotice("TAS PLAYBACK STOPPED");
        }
    }
    void uiAction(int cmd){
        uiMenu_=-1;uiSubmenu_=-1;uiHover_=uiSubHover_=-1;
        if(cmd>=USlot1&&cmd<=USlot4){
            stateSlot=cmd-USlot1+1;uiNotice("STATE SLOT "+std::to_string(stateSlot));return;
        }
        if(cmd>=UAudio1&&cmd<=UAudio6){
            core.setAudioChannelMask(core.audioChannelMask()^(1u<<(cmd-UAudio1)));
            uiNotice("GBA AUDIO CHANNEL TOGGLED");return;
        }
        if(cmd>=UTasRecord&&cmd<=UTasStopPlayback){uiTasAction(cmd);return;}
        switch(cmd){
        case ULoadRom:chooseFile(false);uiPanel_=UiPanel::Game;break;
        case ULoadBios:chooseFile(true);break;
        case UResetRom:
            if(core.cartridge().loaded()){
                std::string error;core.flushSave(error);
                core.reset();if(audio)SDL_ClearQueuedAudio(audio);
                tas_.clear();uiNotice("GBA RESET");
            }
            break;
        case UQuit:running=false;break;
        case UFullScreen:stretched=false;if(!fullscreen)toggleFullscreen();break;
        case UStretch:stretched=true;if(!fullscreen)toggleFullscreen();break;
        case UWindowed:stretched=false;if(fullscreen)toggleFullscreen();break;
        case UPause:paused=!paused;if(audio)SDL_ClearQueuedAudio(audio);
            uiNotice(paused?"PAUSED":"RESUMED");break;
        case UMute:muted=!muted;if(audio)SDL_ClearQueuedAudio(audio);break;
        case USpeed100:speedPercent_=100;break;
        case USpeed50:speedPercent_=50;break;
        case USpeed25:speedPercent_=25;break;
        case UInput:uiOpenInput();break;
        case UScreenshot:screenshot();uiNotice("SCREENSHOT SAVED");break;
        case USaveState:saveState();uiNotice("SAVE STATE");break;
        case ULoadState:loadState();uiNotice("LOAD STATE");break;
        case URecordAVI:
            if(!avi_.active()&&toggleAvi()&&uiRecordToast_)uiNotice("AVI RECORDING STARTED");
            break;
        case UStopAVI:
            if(avi_.active()&&toggleAvi()&&uiRecordToast_)uiNotice("AVI SAVED");
            break;
        case UTasReset:uiTasReset_=!uiTasReset_;break;
        case UCheats:uiOpenCheats();break;
        case UEnableCheats:cheatsEnabled_=!cheatsEnabled_;
            if(!cheatsEnabled_)core.clearRomCheatPatches();break;
        case URecordToast:uiRecordToast_=!uiRecordToast_;break;
        case URecordTimer:uiRecordTimer_=!uiRecordTimer_;break;
        case URecordingFolder:uiOpenRecordingFolder();break;
        case UAbout:uiBeforePanelPaused_=paused;paused=true;
            uiPanel_=UiPanel::About;break;
        default:break;
        }
    }
    void uiMove(int x,int y){
        uiMouseX_=x;uiMouseY_=y;
        if(uiMenu_<0)return;
        const int sc=uiScale();
        const int lx=x/sc,ly=y/sc;
        if(ly<38){
            const int tab=uiTabAt(x,y);
            if(tab>=0&&tab!=uiMenu_){
                uiMenu_=tab;uiSubmenu_=-1;uiHover_=uiSubHover_=-1;
            }
            return;
        }
        const auto& list=uiMenus()[static_cast<std::size_t>(uiMenu_)].items;
        const int left=uiRootX(uiMenu_),width=uiPopupWidth(uiMenu_);
        int hover=-1,sub=-1;
        if(uiInside(lx,ly,left,38,width,int(list.size())*32)){
            hover=(ly-38)/32;
            if(list[static_cast<std::size_t>(hover)].submenu)sub=hover;
        }
        uiHover_=hover;
        if(sub>=0)uiSubmenu_=sub;
        else if(hover>=0)uiSubmenu_=-1;
        uiSubHover_=-1;
        if(uiSubmenu_>=0&&uiSubmenu_<int(list.size())){
            const auto child=uiSubItems(list[static_cast<std::size_t>(uiSubmenu_)].submenu);
            const int sx=std::min(left+width,uiDrawableWidth()/sc-222);
            const int sy=38+uiSubmenu_*32;
            if(uiInside(lx,ly,sx,sy,222,int(child.size())*32))
                uiSubHover_=(ly-sy)/32;
        }
    }
    int uiDrawableWidth()const{
        int w=0,h=0;SDL_GetRendererOutputSize(renderer,&w,&h);return w;
    }
    void uiClick(int x,int y){
        uiMouseX_=x;uiMouseY_=y;
        const int sc=uiScale(),lx=x/sc,ly=y/sc;
        const int w=uiDrawableWidth()/sc;
        if(uiPanel_!=UiPanel::Game&&ly>=38){
            uiPanelClick(lx,ly);return;
        }
        if(ly<38){
            const int tab=uiTabAt(x,y);
            if(tab>=0){
                uiMenu_=uiMenu_==tab?-1:tab;
                uiSubmenu_=-1;uiHover_=uiSubHover_=-1;
            }else{uiMenu_=-1;uiSubmenu_=-1;}
            return;
        }
        if(uiMenu_>=0){
            const auto& list=uiMenus()[static_cast<std::size_t>(uiMenu_)].items;
            const int left=uiRootX(uiMenu_),width=uiPopupWidth(uiMenu_);
            if(uiSubmenu_>=0&&uiSubmenu_<int(list.size())){
                const auto child=uiSubItems(list[static_cast<std::size_t>(uiSubmenu_)].submenu);
                const int sx=std::min(left+width,w-222),sy=38+uiSubmenu_*32;
                if(uiInside(lx,ly,sx,sy,222,int(child.size())*32)){
                    uiAction(child[static_cast<std::size_t>((ly-sy)/32)].command);
                    return;
                }
            }
            if(uiInside(lx,ly,left,38,width,int(list.size())*32)){
                const int index=(ly-38)/32;
                const auto& selected=list[static_cast<std::size_t>(index)];
                if(selected.separator)return;
                if(selected.submenu){
                    uiSubmenu_=index;uiHover_=index;
                }else uiAction(selected.command);
                return;
            }
        }
        uiMenu_=-1;uiSubmenu_=-1;
    }
    void uiPanelClick(int x,int y){
        int w=0,h=0;SDL_GetRendererOutputSize(renderer,&w,&h);
        const int sc=uiScale();w/=sc;h/=sc;
        if(uiPanel_==UiPanel::Controller){
            for(int i=0;i<10;++i){
                const int rowY=38+121+i*39;
                if(y>=rowY&&y<rowY+33){
                    if(x>=176&&x<436){
                        remapIndex_=unsigned(i);remap_=RemapMode::Keyboard;
                        uiCaptureSingle_=true;mappingTitle();
                    }else if(x>=460&&x<745){
                        if(!controller||!SDL_GameControllerGetAttached(controller)){
                            uiNotice("CONNECT A GAMEPAD FIRST");return;
                        }
                        remapIndex_=unsigned(i);remap_=RemapMode::Gamepad;
                        uiCaptureSingle_=true;mappingTitle();
                    }
                    return;
                }
            }
            if(y>=h-30-72&&y<h-30-31){
                if(x>=24&&x<168)uiCloseInput(true);
                else if(x>=179&&x<339){
                    keyMap_=defaultKeys_;padMap_=defaultPad_;axisMap_={};
                    remap_=RemapMode::None;
                }else if(x>=350&&x<504)uiCloseInput(false);
            }
            return;
        }
        if(uiPanel_==UiPanel::About){
            if(y>=h-30-74&&x<230)uiReturnPanel();
            return;
        }
        if(uiPanel_==UiPanel::RecordingDirectory){
            if(y>=h-30-74){
                if(x>=25&&x<225)uiSaveRecordingFolder();
                else if(x>=250&&x<455)uiReturnPanel();
            }
            return;
        }
        if(uiPanel_!=UiPanel::Cheats)return;
        const int rows=std::max(0,std::min(10,int(cheats_.size())-uiCheatScroll_));
        if(y>=38+122&&y<38+122+rows*38){
            const int which=(y-38-122)/38+uiCheatScroll_;
            if(which>=0&&which<int(cheats_.size())){
                uiSelectedCheat_=which;
                cheats_[static_cast<std::size_t>(which)].enabled=
                    !cheats_[static_cast<std::size_t>(which)].enabled;
                saveCheats();
            }
            return;
        }
        if(y>=h-30-73&&y<h-30-31){
            if(x>=25&&x<160){
                uiCheatEditing_=true;uiCheatText_.clear();SDL_StartTextInput();
            }else if(x>=172&&x<315){
                if(uiSelectedCheat_>=0&&uiSelectedCheat_<int(cheats_.size())){
                    cheats_.erase(cheats_.begin()+uiSelectedCheat_);
                    uiSelectedCheat_=-1;uiCheatScroll_=std::min(uiCheatScroll_,
                        std::max(0,int(cheats_.size())-10));
                    saveCheats();core.clearRomCheatPatches();
                }
            }else if(x>=327&&x<481){saveCheats();uiNotice("CHEATS SAVED");}
            else if(x>=493&&x<647){saveCheats();uiReturnPanel();}
        }
    }
    void uiWheel(int direction){
        if(uiPanel_==UiPanel::Cheats)
            uiCheatScroll_=std::clamp(uiCheatScroll_+direction,0,
                                    std::max(0,int(cheats_.size())-10));
    }
    void uiTextEvent(const SDL_Event& event){
        if(event.type!=SDL_TEXTINPUT)return;
        std::string* target=uiPanel_==UiPanel::RecordingDirectory?
            &uiDirectoryText_:(uiPanel_==UiPanel::Cheats&&uiCheatEditing_?
            &uiCheatText_:nullptr);
        if(!target)return;
        const std::size_t limit=target==&uiDirectoryText_?1024:512;
        for(const unsigned char* p=reinterpret_cast<const unsigned char*>(event.text.text);
            *p&&target->size()<limit;++p){
            if(*p>=32&&*p!=127)target->push_back(char(*p));
        }
    }
    void uiPaste(){
        if(!SDL_HasClipboardText())return;
        char* copied=SDL_GetClipboardText();if(!copied)return;
        std::string pasted(copied);SDL_free(copied);
        std::string* dst=uiPanel_==UiPanel::RecordingDirectory?
            &uiDirectoryText_:(uiCheatEditing_?&uiCheatText_:nullptr);
        if(!dst)return;
        const std::size_t limit=dst==&uiDirectoryText_?1024:512;
        for(char c:pasted){
            if(dst->size()>=limit)break;
            if(c=='\r'||c=='\n'){
                if(dst==&uiCheatText_&& !dst->empty() && dst->back()!=';')dst->push_back(';');
            }else if(static_cast<unsigned char>(c)>=32)dst->push_back(c);
        }
    }
    void uiDrawMenus(int w,int h){
        const int sc=uiScale();w/=sc;h/=sc;
        uiRect(0,0,w,38,0x182330);
        uiRect(0,37,w,1,0x486988);
        for(std::size_t i=0;i<uiMenus().size();++i){
            const int left=uiRootX(int(i)),width=uiRootWidth(int(i));
            if(uiMenu_==int(i)||uiTabAt(uiMouseX_,uiMouseY_)==int(i))
                uiRect(left,0,width,38,uiMenu_==int(i)?0x2d4c6d:0x22405e);
            uiLabel(left+17,25,uiMenus()[i].title,0xeef3f8);
        }
        if(uiMenu_<0||uiMenu_>=int(uiMenus().size()))return;
        const auto& list=uiMenus()[static_cast<std::size_t>(uiMenu_)].items;
        const int left=uiRootX(uiMenu_),width=uiPopupWidth(uiMenu_);
        uiRect(left,38,width,int(list.size())*32,0x131c27);
        uiOutline(left,38,width,int(list.size())*32,0x486887);
        for(std::size_t i=0;i<list.size();++i){
            const int yy=38+int(i)*32;
            const auto& item=list[i];
            if(item.separator){uiRect(left+13,yy+16,width-26,1,0x485b6e);continue;}
            if(int(i)==uiHover_)uiRect(left+1,yy,width-2,32,0x265580);
            if(uiChecked(item.command))uiLabel(left+13,yy+22,"X",0xffda5b);
            uiLabel(left+35,yy+22,item.text,0xf0f5fa);
            if(item.submenu)uiLabel(left+width-21,yy+22,">",0xb4cfe5);
        }
        if(uiSubmenu_>=0&&uiSubmenu_<int(list.size())&&
            list[static_cast<std::size_t>(uiSubmenu_)].submenu){
            const auto child=uiSubItems(list[static_cast<std::size_t>(uiSubmenu_)].submenu);
            const int sx=std::min(left+width,w-222);
            const int sy=38+uiSubmenu_*32;
            uiRect(sx,sy,222,int(child.size())*32,0x131c27);
            uiOutline(sx,sy,222,int(child.size())*32,0x486887);
            for(std::size_t i=0;i<child.size();++i){
                const int yy=sy+int(i)*32;
                if(int(i)==uiSubHover_)uiRect(sx+1,yy,220,32,0x265580);
                if(uiChecked(child[i].command))uiLabel(sx+13,yy+22,"X",0xffda5b);
                uiLabel(sx+35,yy+22,child[i].text,0xf1f6fb);
            }
        }
    }
    void uiDrawStatus(int w,int h){
        if(fullscreen)return;
        const int sc=uiScale();w/=sc;h/=sc;
        uiRect(0,h-30,w,30,0x070d15);
        uiRect(0,h-30,w,1,0x486988);
        const std::string rom=core.cartridge().loaded()?
           core.cartridge().title():"NO ROM";
        std::string status=rom+"  SLOT "+std::to_string(stateSlot)+
            "  "+std::to_string(speedPercent_)+"%";
        if(core.cartridge().loaded())status+="  "+std::to_string(int(uiActualFps_+0.5))+" FPS";
        if(paused)status+="  PAUSED";
        if(muted)status+="  MUTED";
        if(cheatsEnabled_)status+="  CHEATS";
        if(avi_.active())status+="  REC"+
            (uiRecordTimer_?(" "+std::to_string(avi_.frames()/60u)+"s"):"");
        if(tas_.recording())status+="  TAS REC "+std::to_string(tas_.count());
        if(tas_.playback())status+="  TAS PLAY "+std::to_string(tas_.cursor())+
            "/"+std::to_string(tas_.count());
        if(!audio)status+="  AUDIO UNAVAILABLE";
        uiLabel(13,h-9,status,0xe1edf7);
    }
    void uiDrawNotice(int w,int h){
        if(std::chrono::steady_clock::now()>=uiNoticeUntil_)return;
        const int sc=uiScale();w/=sc;h/=sc;
        const int width=std::min(w-22,uiFontWidth(uiNoticeText_)+32);
        const int x=(w-width)/2,y=h-(fullscreen?60:95);
        uiRect(x,y,width,40,0x04080e);
        uiOutline(x,y,width,40,0xffcd46);
        uiLabel(x+16,y+27,uiNoticeText_,0xffec99);
    }
    void uiAboutArtwork(int x,int y,int width){
        if(!uiAboutTexture_){
            std::vector<unsigned char> pixels;
            const auto& art=pixelgba::artwork::kAboutArtwork;
            if(pixelgba::artwork::decode(art,pixels)){
                uiAboutTexture_=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,
                    SDL_TEXTUREACCESS_STATIC,art.width,art.height);
                if(uiAboutTexture_){
                    if(SDL_UpdateTexture(uiAboutTexture_,nullptr,pixels.data(),art.width*4)!=0){
                        SDL_DestroyTexture(uiAboutTexture_);uiAboutTexture_=nullptr;
                    }else SDL_SetTextureBlendMode(uiAboutTexture_,SDL_BLENDMODE_BLEND);
                }
            }
        }
        if(uiAboutTexture_){
            const int sc=uiScale();
            const auto& art=pixelgba::artwork::kAboutArtwork;
            const int height=width*int(art.height)/int(art.width);
            const SDL_Rect bounds{x*sc,y*sc,width*sc,height*sc};
            SDL_RenderCopy(renderer,uiAboutTexture_,nullptr,&bounds);
        }else uiLabel(x,y+40,"CipherGBA",0x69c5ff);
    }
    void uiDrawPanel(int w,int h){
        const int sc=uiScale();w/=sc;h/=sc;
        uiRect(0,38,w,h-38-(fullscreen?0:30),0x090f18);
        if(uiPanel_==UiPanel::About){
            const int logoW=std::max(1,std::min({w-46,420,h-38-30-280}));
            const int logoH=logoW*int(pixelgba::artwork::kAboutArtwork.height)/
                                   int(pixelgba::artwork::kAboutArtwork.width);
            uiAboutArtwork((w-logoW)/2,53,logoW);
            const int base=38+logoH+54;
            uiLabel(28,base,"CipherGBA v0.3.0  |  C++17  |  HodgkinsStudios",0xe2edff);
            uiLabel(28,base+34,"Game Boy Advance Emulator  -  Windows / macOS / Linux",0xcbd9e9);
            uiLabel(28,base+68,"ARM7TDMI  |  Integrated HLE or user-supplied original BIOS",0xcbd9e9);
            uiLabel(28,base+102,"suite.gba: 6,994 / 6,998 documented; four HBlank cases unresolved",0x9ebbd6);
            uiButton(26,h-30-74,190,42,"RETURN TO GAME");
            return;
        }
        if(uiPanel_==UiPanel::Controller){
            uiLabel(25,80,"CONTROLLER INPUT",0x67c3ff);
            uiLabel(25,110,"Click a binding, then press a keyboard key or gamepad button / axis.",0xc3d9e9);
            uiLabel(25,143,"GBA",0xa7c6df);
            uiLabel(197,143,"KEYBOARD",0xa7c6df);
            uiLabel(476,143,"GAMEPAD",0xa7c6df);
            for(int i=0;i<10;++i){
                const int y=159+i*39;
                uiLabel(25,y+23,buttonNames_[static_cast<std::size_t>(i)],0xf0f4fa);
                const char* key=SDL_GetScancodeName(keyMap_[static_cast<std::size_t>(i)]);
                std::string padLabel;
                if(axisMap_[static_cast<std::size_t>(i)].axis>=0){
                    const auto mapping=axisMap_[static_cast<std::size_t>(i)];
                    const char* axis=SDL_GameControllerGetStringForAxis(
                        static_cast<SDL_GameControllerAxis>(mapping.axis));
                    padLabel=std::string(axis&&*axis?axis:"AXIS")+
                        (mapping.direction>0?" +":" -");
                }else{
                    const char* pad=SDL_GameControllerGetStringForButton(
                        padMap_[static_cast<std::size_t>(i)]);
                    padLabel=(pad&&*pad)?pad:"Unassigned";
                }
                uiButton(176,y,260,33,(key&&*key)?key:"Unassigned",
                    remap_==RemapMode::Keyboard&&remapIndex_==unsigned(i));
                uiButton(460,y,285,33,padLabel,
                    remap_==RemapMode::Gamepad&&remapIndex_==unsigned(i));
            }
            if(remap_!=RemapMode::None)
                uiLabel(25,h-30-102,remap_==RemapMode::Gamepad?
                        "WAITING FOR GAMEPAD INPUT...":"PRESS A KEY...",0xffd777);
            uiButton(24,h-30-72,144,41,"SAVE");
            uiButton(179,h-30-72,160,41,"DEFAULTS");
            uiButton(350,h-30-72,154,41,"CANCEL");
            return;
        }
        if(uiPanel_==UiPanel::RecordingDirectory){
            uiLabel(25,92,"CUSTOM AVI OUTPUT LOCATION",0x67c3ff);
            uiLabel(25,135,"Enter an absolute folder path; Enter or SAVE confirms.",0xc3d9e9);
            uiRect(25,167,std::min(w-55,920),45,0x223f59);
            std::string line=uiDirectoryText_;
            if(line.size()>75)line="..."+line.substr(line.size()-72);
            uiLabel(36,195,line+"_",0xfafafa);
            uiButton(25,h-30-74,200,42,"SAVE");
            uiButton(250,h-30-74,205,42,"RETURN");
            return;
        }
        if(uiPanel_!=UiPanel::Cheats)return;
        uiLabel(25,81,"GBA CHEAT CODES",0x67c3ff);
        uiLabel(25,113,"CB:XXXXXXXX XXXX   GS:XXXXXXXX XXXXXXXX   AR:XXXXXXXX XXXXXXXX",0xc8d9e9);
        uiLabel(25,141,"RAW:address:value:bits | Semicolon separates linked code lines.",0x9eb9d1);
        const int end=std::min(int(cheats_.size()),uiCheatScroll_+10);
        for(int i=uiCheatScroll_;i<end;++i){
            const int yy=160+(i-uiCheatScroll_)*38;
            uiRect(20,yy,w-45,32,i==uiSelectedCheat_?0x284b6b:0x1b2937);
            const auto& code=cheats_[static_cast<std::size_t>(i)];
            std::string display=std::string(code.enabled?"[X] ":"[ ] ")+code.source;
            if(code.source.find(':')==std::string::npos)
                display=std::string(code.enabled?"[X] ":"[ ] ")+
                        pixelgba::cheats::formatName(code.format)+"  "+code.source;
            if(display.size()>91)display=display.substr(0,88)+"...";
            uiLabel(31,yy+23,display,0xe2f1ff);
        }
        if(uiCheatEditing_){
            const int y=h-30-155;
            uiLabel(30,y,"ENTER RAW / CB / GS / AR CODE; ; JOINS LINES; ENTER TO ADD:",0xffd777);
            uiRect(25,y+12,std::min(w-65,620),42,0x223f59);
            std::string visible=uiCheatText_.size()>51?
                "..."+uiCheatText_.substr(uiCheatText_.size()-48):uiCheatText_;
            uiLabel(35,y+40,visible+"_",0xfafafa);
        }
        uiButton(25,h-30-73,135,42,"ADD");
        uiButton(172,h-30-73,143,42,"REMOVE");
        uiButton(327,h-30-73,154,42,"SAVE");
        uiButton(493,h-30-73,154,42,"RETURN");
    }
    void uiDrawNoRom(int,int,int){} // Matches the original empty GBA viewport.
    void uiDrawChrome(int w,int h){
        uiDrawStatus(w,h);
        uiDrawNotice(w,h);
        if(uiChromeVisible())uiDrawMenus(w,h);
    }
