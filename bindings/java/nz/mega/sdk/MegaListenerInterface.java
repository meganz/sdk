package nz.mega.sdk;

/**
 * Interface to get all information related to a MEGA account.
 * <p>
 * Implementations of this interface can receive all events (request, transfer, global) and two additional
 * events related to the synchronization engine. The SDK will provide a new interface to get synchronization
 * events separately in future updates.
 * Multiple inheritance is not used for compatibility with other programming languages.
 */
public interface MegaListenerInterface extends MegaRequestListenerInterface, MegaGlobalListenerInterface,
        MegaTransferListenerInterface {
}
