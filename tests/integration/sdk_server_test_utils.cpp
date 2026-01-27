#include "sdk_server_test_utils.h"

#include "integration_test_utils.h"

unique_ptr<MegaNode> SdkServerTest::createFolder(unsigned int apiIndex,
                                                 const std::string& name,
                                                 MegaNode* parent)
{
    if (parent == nullptr)
    {
        return nullptr;
    }
    MegaHandle handle = SdkTest::createFolder(apiIndex, name.c_str(), parent);
    if (handle == UNDEF)
    {
        return nullptr;
    }
    return std::unique_ptr<MegaNode>(megaApi[apiIndex]->getNodeByHandle(handle));
}

unique_ptr<MegaNode> SdkServerTest::createFolder(unsigned int apiIndex, const std::string& name)
{
    auto rootNode = unique_ptr<MegaNode>(megaApi[apiIndex]->getRootNode());
    return createFolder(apiIndex, name, rootNode.get());
}

unique_ptr<MegaNode> SdkServerTest::uploadFile(unsigned int apiIndex,
                                               const std::string& name,
                                               const std::string& contents,
                                               MegaNode* parent)
{
    deleteFile(name);
    sdk_test::LocalTempFile f(name, contents);
    return sdk_test::uploadFile(megaApi[apiIndex].get(), name, parent);
}
