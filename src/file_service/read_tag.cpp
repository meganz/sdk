#include <mega/file_service/read_tag.h>
#include <mega/overloaded.h>

namespace mega
{
namespace file_service
{

bool isJumpCandidate(ReadTag readTag)
{
    return std::visit(overloaded{[](NormalReadTag)
                                 {
                                     return false;
                                 },
                                 [](StreamReadTag tag)
                                 {
                                     return tag.mIsJumpCandidate;
                                 }},
                      readTag);
}

} // file_service
} // mega
