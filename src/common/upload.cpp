#include <mega/common/error_or.h>
#include <mega/common/upload.h>

namespace mega
{
namespace common
{

void Upload::begin(BoundCallback callback)
{
    // Sanity.
    assert(callback);

    // Called when the file's content has been uploaded.
    auto uploaded = [](BoundCallback& bound,
                       ErrorOr<UploadResult> result) {
        // Couldn't upload the file's content.
        if (!result)
            return bound(unexpected(result.error()));

        // Extract bind callback.
        auto bind = std::move(*result);

        // Sanity.
        assert(bind);

        // Try and bind a name to our uploaded content.
        bind(std::move(bound), NodeHandle());
    }; // uploaded

    UploadCallback wrapper =
      std::bind(std::move(uploaded),
                std::move(callback),
                std::placeholders::_1);

    // Try and upload the file's content.
    return begin(std::move(wrapper));
}

} // common
} // mega

