package nz.mega.sdk;

/**
 * Interface to process node trees.
 * <p>
 * An implementation of this class can be used to process a node tree passing a pointer to MegaApi.processMegaTree().
 */
class DelegateMegaTreeProcessor extends MegaTreeProcessor {
    MegaApiJava megaApi;
    MegaTreeProcessorInterface listener;

    DelegateMegaTreeProcessor(MegaApiJava megaApi, MegaTreeProcessorInterface listener) {
        this.megaApi = megaApi;
        this.listener = listener;
    }

    /**
     * Function that will be called for all nodes in a node tree.
     *
     * @param node
     *          Node to be processed
     * @return
     *          true to continue processing nodes, false to stop
     */
    public boolean processMegaNode(MegaNode node) {
        if (listener != null)
            return listener.processMegaNode(megaApi, node);
        return false;
    }
}
