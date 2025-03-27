#pragma once

#include <mega/common/upload_callbacks.h>
#include <mega/common/upload_forward.h>

#include <mega/types.h>

namespace mega
{
namespace common
{

class Upload
{
protected:
    Upload() = default;

public:
    virtual ~Upload() = default;

    // Begin the upload.
    void begin(BoundCallback callback);

    virtual void begin(UploadCallback callback) = 0;

    // Cancel the upload.
    //
    // Returns true if the upload could be cancelled.
    virtual bool cancel() = 0;

    // Query whether an upload was cancelled.
    virtual bool cancelled() const = 0;

    // Query whether an upload has completed.
    virtual bool completed() const = 0;

    // Query the result of the upload.
    virtual Error result() const = 0;
}; // Upload

} // common
} // mega

