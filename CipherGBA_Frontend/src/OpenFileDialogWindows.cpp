#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <filesystem>
#include <string>
#include <vector>
#include "OpenFileDialog.hpp"
namespace ciphergba_host {
std::string openFileDialog(bool bios){
    std::vector<wchar_t> buffer(32768,L'\0');
    OPENFILENAMEW ofn{};
    ofn.lStructSize=sizeof(ofn);
    ofn.lpstrFile=buffer.data();
    ofn.nMaxFile=static_cast<DWORD>(buffer.size());
    const wchar_t biosFilter[]=L"GBA BIOS (*.bin)\0*.bin\0All files (*.*)\0*.*\0";
    const wchar_t romFilter[]=L"GBA ROMs (*.gba;*.zip)\0*.gba;*.zip\0All files (*.*)\0*.*\0";
    ofn.lpstrFilter=bios?biosFilter:romFilter;
    ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_EXPLORER|OFN_NOCHANGEDIR;
    if(!GetOpenFileNameW(&ofn))return {};
    return std::filesystem::path(buffer.data()).u8string();
}
}
