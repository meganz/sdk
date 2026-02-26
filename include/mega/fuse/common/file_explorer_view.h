#pragma once

#include <string>

namespace mega
{
namespace fuse
{
#define DEFINE_FILE_EXPLORER_VIEWS(expander) \
    expander(NONE) \
    expander(LIST)

enum FileExplorerView : int
{
#define DEFINE_FILE_EXPLORER_VIEW_ENUMERANT(name) FILE_EXPLORER_VIEW_##name,
    DEFINE_FILE_EXPLORER_VIEWS(DEFINE_FILE_EXPLORER_VIEW_ENUMERANT)
#undef DEFINE_FILE_EXPLORER_VIEW_ENUMERANT
};

FileExplorerView toFileExplorerView(const std::string& view);

const char* toString(FileExplorerView view);

} // fuse
} // mega
