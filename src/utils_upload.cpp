#include "mega/utils_upload.h"
#include "mega/utils.h"
#include "mega/filesystem.h"
#include "mega/logging.h"

namespace mega {

// Judge if upload is needed (fingerprint + MAC double check)
UploadJudgement shouldProceedWithUpload(
    const std::shared_ptr<Node>& previousNode, 
    const FileFingerprint& fp, 
    const std::unique_ptr<FileAccess>& fa, 
    bool allowDuplicateVersions, 
    const std::string& fileName,
    CompareMetaMacFunc compareMacFunc)
{
    if (!previousNode) return {true, nullptr};

    if (previousNode->type != FILENODE)
    {
        LOG_warn << "Can't upload file over folder with same name: " << fileName;
        return {false, nullptr};
    }

    if (!fp.isvalid || !previousNode->isvalid || !(fp == *static_cast<FileFingerprint*>(previousNode.get())))
    {
        return {true, nullptr};
    }

    // Check MAC match for further decision
    bool isMacMatched = compareMacFunc(fa.get(), previousNode.get());
    if (isMacMatched && !allowDuplicateVersions)
    {
        LOG_info << "File '" << fileName << "' matches fingerprint+MAC. Ready for remote copy.";
        return {false, previousNode};
    }
    else if (isMacMatched)
    {
        LOG_debug << "File matches fingerprint+MAC, but duplicates allowed. Proceeding upload.";
        return {true, nullptr};
    } 
    else
    {
        LOG_debug << "File matches fingerprint but not MAC. Proceeding upload.";
        return {true, nullptr};
    }
}

} // namespace mega
