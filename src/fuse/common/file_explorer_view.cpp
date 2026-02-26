#include <mega/fuse/common/file_explorer_view.h>

#include <map>

namespace mega
{
namespace fuse
{
FileExplorerView toFileExplorerView(const std::string& view)
{
    static const std::map<std::string, FileExplorerView> views = {
#define DEFINE_FILE_EXPLORER_VIEW_ENTRY(name) {#name, FILE_EXPLORER_VIEW_##name},
        DEFINE_FILE_EXPLORER_VIEWS(DEFINE_FILE_EXPLORER_VIEW_ENTRY)
#undef DEFINE_FILE_EXPLORER_VIEW_ENTRY
    };

    auto i = views.find(view);

    if (i != views.end())
        return i->second;

    // Assume some sane default.
    return FILE_EXPLORER_VIEW_LIST;
}

const char* toString(FileExplorerView view)
{
    static const std::map<FileExplorerView, std::string> strings = {
#define DEFINE_FILE_EXPLORER_VIEW_ENTRY(name) {FILE_EXPLORER_VIEW_##name, #name},
        DEFINE_FILE_EXPLORER_VIEWS(DEFINE_FILE_EXPLORER_VIEW_ENTRY)
#undef DEFINE_FILE_EXPLORER_VIEW_ENTRY
    }; // strings

    if (auto i = strings.find(view); i != strings.end())
        return i->second.c_str();

    return "N/A";
}

} // fuse
} // mega
