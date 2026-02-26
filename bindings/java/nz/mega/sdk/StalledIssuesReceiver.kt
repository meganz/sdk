package nz.mega.sdk

/**
 * A class to asynchronously receive Stalled Issues list
 * @property onStallListLoaded - lambda that returns Stalled Issues
 */
class StalledIssuesReceiver(
    private val onStallListLoaded: (List<MegaSyncStall>) -> Unit,
) : MegaRequestListenerInterface {

    override fun onRequestStart(api: MegaApiJava, request: MegaRequest) {

    }

    override fun onRequestUpdate(api: MegaApiJava, request: MegaRequest) {

    }

    override fun onRequestFinish(api: MegaApiJava, request: MegaRequest, e: MegaError) {
        if (request.type == MegaRequest.TYPE_GET_SYNC_STALL_LIST) {
            val stallList = request.megaSyncStallList
            if (stallList == null) {
                onStallListLoaded(emptyList())
                return
            }
            // Copy objects with memory ownership to prevent SIGSEGV null pointer dereference
            stallList.copy().let { list ->
                val copiedList = (0 until list.size()).mapNotNull { index ->
                    list.get(index)?.copy()
                }
                onStallListLoaded(copiedList)
            }
        }
    }

    override fun onRequestTemporaryError(api: MegaApiJava, request: MegaRequest, e: MegaError) {

    }
}