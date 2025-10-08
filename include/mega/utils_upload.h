#ifndef MEGA_UTILS_UPLOAD_H
#define MEGA_UTILS_UPLOAD_H 1

#include <memory>
#include <string>
#include "node.h"
#include "filefingerprint.h"
#include "filesystem.h"

namespace mega {

using CompareMetaMacFunc = bool (*)(FileAccess* fa, Node* node);

// Upload decision result
struct UploadJudgement
{
    bool needUpload; // True if upload is needed
    std::shared_ptr<Node> sourceNode; // reusable remote node
};

/**
 * Determines if a file upload should proceed based on fingerprint and MAC checks.
 * 
 * @param previousNode Existing remote node (may be null)
 * @param fp Local file's fingerprint (size, mtime, hash subset)
 * @param fa File accessor for the local file
 * @param allowDuplicateVersions Whether to allow duplicate versions
 * @param fileName File name (for logging)
 * @param compareFunc Metadata/MAC comparison function (defaults to CompareLocalFileMetaMacWithNode)
 * @return UploadJudgement with upload necessity and reusable node (if applicable)
 */
UploadJudgement shouldProceedWithUpload(
    const std::shared_ptr<Node>& previousNode, 
    const FileFingerprint& fp, 
    const std::unique_ptr<FileAccess>& fa, 
    bool allowDuplicateVersions, 
    const std::string& fileName,
    CompareMetaMacFunc compareFunc = CompareLocalFileMetaMacWithNode);

} // namespace

#endif