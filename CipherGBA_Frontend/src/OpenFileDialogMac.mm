#import <Cocoa/Cocoa.h>
#include "OpenFileDialog.hpp"
namespace ciphergba_host {
std::string openFileDialog(bool bios){
    @autoreleasepool {
        NSOpenPanel* panel=[NSOpenPanel openPanel];
        [panel setCanChooseDirectories:NO];
        [panel setCanChooseFiles:YES];
        [panel setAllowsMultipleSelection:NO];
        [panel setTitle:(bios?@"Open GBA BIOS":@"Open Game Boy Advance ROM")];
        [panel setAllowedFileTypes:(bios?@[@"bin"]:@[@"gba",@"zip"])];
        if([panel runModal]!=NSModalResponseOK)return {};
        NSString* result=[[[panel URLs] firstObject] path];
        if(!result)return {};
        const char* utf8=[result UTF8String];
        return utf8?std::string(utf8):std::string();
    }
}
}
