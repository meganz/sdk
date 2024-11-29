package nz.mega.sdk

/**
 * A class to asynchronously receive Stalled Issues list
 * @property onStallListLoaded - lambda that returns Stalled Issues
 */
class StalledIssuesReceiver(
    private val onStallListLoaded: (MegaSyncStallList) -> Unit,
) : MegaRequestListenerInterface {

    override fun onRequestStart(api: MegaApiJava, request: MegaRequest) {

    }

    override fun onRequestUpdate(api: MegaApiJava, request: MegaRequest) {

    }

    override fun onRequestFinish(api: MegaApiJava, request: MegaRequest, e: MegaError) {
        if (request.type == MegaRequest.TYPE_GET_SYNC_STALL_LIST) {
            onStallListLoaded(request.megaSyncStallList)
        }
    }

    override fun onRequestTemporaryError(api: MegaApiJava, request: MegaRequest, e: MegaError) {

    }
}