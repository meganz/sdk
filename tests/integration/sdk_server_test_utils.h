#pragma once

#include "SdkTest_test.h"

class SdkServerTest: public SdkTest
{
protected:
    unique_ptr<MegaNode> createFolder(unsigned int apiIndex,
                                      const std::string& name,
                                      MegaNode* parent);

    unique_ptr<MegaNode> createFolder(unsigned int apiIndex, const std::string& name);

    unique_ptr<MegaNode> uploadFile(unsigned int apiIndex,
                                    const std::string& name,
                                    const std::string& contents,
                                    MegaNode* parent = nullptr);
};