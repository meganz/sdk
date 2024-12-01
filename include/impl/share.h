#pragma once

#include "mega.h"
#include "megaapi.h"

namespace mega
{

struct Share;

namespace impl
{

class ShareData
{
public:
    ShareData(MegaHandle nodeHandle, const Share* share, bool verified);

    MegaHandle getNodeHandle() const;

    const Share* getShare() const;

    bool isVerified() const;

    m_time_t creationTime() const;

private:
    MegaHandle mNodeHandle;

    const Share* mShare;

    bool mVerified;
};

class ShareExtractor
{
public:
    using Filter = std::function<bool(const ShareData&)>;

    // Extract both out and pending shares, if filter(data) returns false, the share data is dropped
    static vector<ShareData> extractShares(const sharedNode_vector& sharedNodes,
                                           const KeyManager& keyManager,
                                           Filter filter = nullptr);

private:
    static std::vector<ShareData> extractPendingShares(const Node* n,
                                                       const KeyManager& keyManager,
                                                       Filter filter);

    static std::vector<ShareData> extractOutShares(const Node* n,
                                                   const KeyManager& keyManager,
                                                   Filter filter);
};

class ShareSorter
{
public:
    ShareSorter(int order):
        mOrder(order)
    {}

    // Sort in place
    void sort(std::vector<ShareData>& shares) const;

private:
    void sortByCreationTimeAsc(std::vector<ShareData>& shares) const;

    void sortByCreationTimeDesc(std::vector<ShareData>& shares) const;

    int mOrder{MegaApi::ORDER_NONE};
};

} // namespace impl
} // namespace mega
