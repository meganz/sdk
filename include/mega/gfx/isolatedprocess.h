#pragma once

#include "mega.h"
#include "mega/gfx.h"
#include "mega/gfx/worker/tasks.h"

namespace mega {

class GfxProviderIsolatedProcess : public IGfxProvider
{
public:
    std::vector<std::string> generateImages(FileSystemAccess* fa,
                                            const LocalPath& localfilepath,
                                            const std::vector<Dimension>& dimensions) override;

    const char* supportedformats() override;

    const char* supportedvideoformats() override;

private:

    std::vector<gfx::GfxSize> toGfxSize(const std::vector<Dimension>& dimensions);

    std::string mformats;
};

}
