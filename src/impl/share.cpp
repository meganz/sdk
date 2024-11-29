#include "impl/share.h"

namespace mega
{
namespace impl
{

ShareData::ShareData(MegaHandle nodeHandle, const Share* share, bool verified):
    mNodeHandle(nodeHandle),
    mShare(share),
    mVerified(verified)
{}

MegaHandle ShareData::getNodeHandle() const
{
    return mNodeHandle;
}

const Share* ShareData::getShare() const
{
    return mShare;
}

bool ShareData::isVerified() const
{
    return mVerified;
}

vector<ShareData> ShareExtractor::extractOutShares(const Node* n,
                                                   const KeyManager& keyManager,
                                                   Filter filter)
{
    vector<ShareData> shares;
    if (n->outshares)
    {
        for (const auto& outShare: *n->outshares)
        {
            const Share* share = outShare.second.get();
            assert(!share->pcr);
            if (share->user) // public links have no user
            {
                const bool verified =
                    !keyManager.isUnverifiedOutShare(n->nodehandle,
                                                     toHandle(share->user->userhandle));
                ShareData data{n->nodehandle, share, verified};
                if (!filter || filter(data)) // no filter or filter() returns true
                {
                    shares.push_back(std::move(data));
                }
            }
        }
    }
    return shares;
}

vector<ShareData> ShareExtractor::extractPendingShares(const Node* n,
                                                       const KeyManager& keyManager,
                                                       Filter filter)
{
    vector<ShareData> shares;
    if (n->pendingshares)
    {
        for (const auto& pendingShare: *n->pendingshares)
        {
            const Share* share = pendingShare.second.get();
            if (share->pcr)
            {
                const bool verified =
                    !keyManager.isUnverifiedOutShare(n->nodehandle, share->pcr->targetemail);

                ShareData data{n->nodehandle, share, verified};
                if (!filter || filter(data)) // no filter or filter() returns true
                {
                    shares.push_back(std::move(data));
                }
            }
        }
    }
    return shares;
}

vector<ShareData> ShareExtractor::extractShares(const sharedNode_vector& sharedNodes,
                                                const KeyManager& keyManager,
                                                Filter filter)
{
    vector<ShareData> shares;
    auto outputIt = std::back_inserter(shares);
    for (const auto& n: sharedNodes)
    {
        auto outShares = extractOutShares(n.get(), keyManager, filter);
        std::move(outShares.begin(), outShares.end(), outputIt);

        auto pendingShares = extractPendingShares(n.get(), keyManager, filter);
        std::move(pendingShares.begin(), pendingShares.end(), outputIt);
    }
    return shares;
}

} // namespace impl
} // namespace mega
