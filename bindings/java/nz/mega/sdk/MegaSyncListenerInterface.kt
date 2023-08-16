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
}