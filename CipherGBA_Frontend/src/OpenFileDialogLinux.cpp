// Linux SDL2 native file dialog: prefer xdg-desktop-portal (Flatpak and
// modern desktops), falling back to Zenity on hosts without the portal.
#include "OpenFileDialog.hpp"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <optional>
#include <string>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#ifdef CIPHERGBA_HAVE_DBUS
#include <dbus/dbus.h>
#endif
namespace ciphergba_host {
namespace {
#ifdef CIPHERGBA_HAVE_DBUS
// Percent-decode only file:// URIs returned by a trusted file chooser portal.
std::string filenameFromUri(const std::string& uri){
    if(uri.rfind("file:///",0)!=0)return {};
    std::string path=uri.substr(7);
    std::string result;
    for(std::size_t i=0;i<path.size();++i){
        if(path[i]=='%'){
            if(i+2>=path.size())return {};
            auto hex=[](char c)->int{
                if(c>='0'&&c<='9')return c-'0';
                if(c>='a'&&c<='f')return c-'a'+10;
                if(c>='A'&&c<='F')return c-'A'+10;
                return -1;
            };
            const int a=hex(path[i+1]),b=hex(path[i+2]);
            if(a<0||b<0||a*16+b==0)return {};
            result.push_back(static_cast<char>(a*16+b));i+=2;
        }else result.push_back(path[i]);
    }
    return result;
}
std::optional<std::string> portalOpen(bool bios){
    DBusError err;dbus_error_init(&err);
    DBusConnection* conn=dbus_bus_get_private(DBUS_BUS_SESSION,&err);
    if(!conn){dbus_error_free(&err);return std::nullopt;}
    dbus_connection_set_exit_on_disconnect(conn,FALSE);
    auto done=[&]{
        if(dbus_error_is_set(&err))dbus_error_free(&err);
        dbus_connection_close(conn);dbus_connection_unref(conn);
    };
    // A service check prevents a no-portal desktop from hanging in a D-Bus
    // method call. A private connection avoids consuming SDL's own messages.
    if(!dbus_bus_name_has_owner(conn,"org.freedesktop.portal.Desktop",&err)){
        done();return std::nullopt;
    }
    const char* rule="type='signal',interface='org.freedesktop.portal.Request',member='Response'";
    dbus_bus_add_match(conn,rule,&err);
    if(dbus_error_is_set(&err)){done();return std::nullopt;}
    dbus_connection_flush(conn);
    DBusMessage* call=dbus_message_new_method_call(
        "org.freedesktop.portal.Desktop","/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.FileChooser","OpenFile");
    if(!call){done();return std::nullopt;}
    DBusMessageIter args{}, options{};
    dbus_message_iter_init_append(call,&args);
    const char* parent="";
    const char* title=bios?"Open original GBA BIOS":"Open Game Boy Advance ROM";
    if(!dbus_message_iter_append_basic(&args,DBUS_TYPE_STRING,&parent) ||
       !dbus_message_iter_append_basic(&args,DBUS_TYPE_STRING,&title) ||
       !dbus_message_iter_open_container(&args,DBUS_TYPE_ARRAY,"{sv}",&options)){
        dbus_message_unref(call);done();return std::nullopt;
    }
    // An empty options map is allowed; modality and single-file selection
    // are the portal's defaults. All permissions remain under user control.
    if(!dbus_message_iter_close_container(&args,&options)){
        dbus_message_unref(call);done();return std::nullopt;
    }
    DBusMessage* reply=dbus_connection_send_with_reply_and_block(conn,call,15000,&err);
    dbus_message_unref(call);
    if(!reply){done();return std::nullopt;}
    const char* requestPath=nullptr;
    DBusMessageIter replyIter{};
    if(!dbus_message_iter_init(reply,&replyIter) ||
       dbus_message_iter_get_arg_type(&replyIter)!=DBUS_TYPE_OBJECT_PATH){
        dbus_message_unref(reply);done();return std::nullopt;
    }
    dbus_message_iter_get_basic(&replyIter,&requestPath);
    const std::string request=requestPath?requestPath:"";
    dbus_message_unref(reply);
    if(request.empty()){done();return std::nullopt;}
    // A portal request response is an async signal (response:uint32,
    // results:a{sv}). A user may take time to choose a file.
    for(unsigned seconds=0;seconds<600;++seconds){
        if(!dbus_connection_read_write(conn,1000))break;
        DBusMessage* msg=nullptr;
        while((msg=dbus_connection_pop_message(conn))!=nullptr){
            const bool matching=dbus_message_is_signal(msg,
                "org.freedesktop.portal.Request","Response") &&
                request==dbus_message_get_path(msg);
            if(!matching){dbus_message_unref(msg);continue;}
            DBusMessageIter iter{};
            dbus_uint32_t response=1;
            if(!dbus_message_iter_init(msg,&iter) ||
               dbus_message_iter_get_arg_type(&iter)!=DBUS_TYPE_UINT32){
                dbus_message_unref(msg);done();return std::nullopt;
            }
            dbus_message_iter_get_basic(&iter,&response);
            if(response!=0){
                dbus_message_unref(msg);done();return std::string{};
            }
            std::string selected;
            if(dbus_message_iter_next(&iter) &&
               dbus_message_iter_get_arg_type(&iter)==DBUS_TYPE_ARRAY){
                DBusMessageIter dict{};
                dbus_message_iter_recurse(&iter,&dict);
                while(dbus_message_iter_get_arg_type(&dict)==DBUS_TYPE_DICT_ENTRY){
                    DBusMessageIter entry{};
                    dbus_message_iter_recurse(&dict,&entry);
                    const char* key=nullptr;
                    if(dbus_message_iter_get_arg_type(&entry)==DBUS_TYPE_STRING)
                        dbus_message_iter_get_basic(&entry,&key);
                    if(key && std::strcmp(key,"uris")==0 &&
                       dbus_message_iter_next(&entry) &&
                       dbus_message_iter_get_arg_type(&entry)==DBUS_TYPE_VARIANT){
                        DBusMessageIter variant{};
                        dbus_message_iter_recurse(&entry,&variant);
                        if(dbus_message_iter_get_arg_type(&variant)==DBUS_TYPE_ARRAY){
                            DBusMessageIter uris{};
                            dbus_message_iter_recurse(&variant,&uris);
                            if(dbus_message_iter_get_arg_type(&uris)==DBUS_TYPE_STRING){
                                const char* first=nullptr;
                                dbus_message_iter_get_basic(&uris,&first);
                                if(first)selected=filenameFromUri(first);
                            }
                        }
                    }
                    dbus_message_iter_next(&dict);
                }
            }
            dbus_message_unref(msg);
            done();
            return selected;
        }
    }
    done();
    return std::nullopt;
}
#endif
std::string zenityOpen(bool bios){
    // No shell or path interpolation: the native picker returns a file path.
    int fds[2];
    if(pipe(fds)!=0)return {};
    const pid_t pid=fork();
    if(pid<0){close(fds[0]);close(fds[1]);return {};}
    if(pid==0){
        close(fds[0]);
        if(dup2(fds[1],STDOUT_FILENO)<0)_exit(127);
        close(fds[1]);
        if(bios)execlp("zenity","zenity","--file-selection","--title=Open GBA BIOS",
                       "--file-filter=BIOS (*.bin) | *.bin",nullptr);
        else execlp("zenity","zenity","--file-selection","--title=Open GBA ROM",
                     "--file-filter=Game Boy Advance (*.gba *.zip) | *.gba *.zip",nullptr);
        _exit(127);
    }
    close(fds[1]);
    std::string value;char buf[512];
    while(value.size()<32768){
        const ssize_t n=read(fds[0],buf,sizeof(buf));
        if(n>0)value.append(buf,static_cast<std::size_t>(n));
        else if(n<0 && errno==EINTR)continue;
        else break;
    }
    close(fds[0]);
    int status=0;pid_t waited;
    do{waited=waitpid(pid,&status,0);}while(waited<0&&errno==EINTR);
    if(waited<0||!WIFEXITED(status)||WEXITSTATUS(status)!=0)return {};
    while(!value.empty()&&(value.back()=='\n'||value.back()=='\r'))value.pop_back();
    return value;
}
} // namespace
std::string openFileDialog(bool bios){
#ifdef CIPHERGBA_HAVE_DBUS
    if(const auto chosen=portalOpen(bios))return *chosen;
#endif
    return zenityOpen(bios);
}
} // namespace ciphergba_host
