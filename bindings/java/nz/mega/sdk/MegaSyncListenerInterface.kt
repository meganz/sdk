package nz.mega.sdk

/**
 * Interface to receive information about the syncs
 */
interface MegaSyncListenerInterface {

    /**
     * @brief This callback will be called when a sync is removed.
     *
     * This entail that the sync is completely removed from cache
     *
     * The SDK retains the ownership of the sync parameter.
     * Don't use it after this functions returns.
     *
     * @param api MegaApi object that is synchronizing files
     * @param sync MegaSync object representing a sync
     */
    fun onSyncDeleted(api: MegaApiJava, sync: MegaSync)

    /**
     * @brief This function is called when there is an update on
     * the number of nodes or transfers in the sync
     *
     * The SDK retains the ownership of the MegaSyncStats.
     * Don't use it after this functions returns. But you can copy it
     *
     * @param api MegaApi object that is synchronizing files
     * @param syncStats Identifies the sync and provides the counts
     */
    fun onSyncStatsUpdated(api: MegaApiJava, syncStats: MegaSyncStats)

    /**
     * @brief This function is called when the state of the synchronization changes
     *
     * The SDK calls this function when the state of the synchronization changes. you can use
     * MegaSync::getRunState to get the new state of the synchronization
     * and MegaSync::getError to get the error if any.
     *
     * The SDK retains the ownership of the sync parameter.
     * Don't use it after this functions returns.
     *
     * @param api MegaApi object that is synchronizing files
     * @param sync MegaSync object that has changed its state
     */
    fun onSyncStateChanged(api: MegaApiJava, sync: MegaSync)
}