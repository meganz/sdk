package nz.mega.sdk;

import javax.swing.SwingUtilities;

/**
 * Control a MEGA account or a shared folder using a Java Swing GUI.
 */
public class MegaApiSwing extends MegaApiJava {

    /**
     * Instantiates a new MEGA API using Swing.
     *
     * @param appKey
     *              AppKey of your application. You can generate your AppKey for free here: <br>
     *              https://mega.co.nz/#sdk
     * @param userAgent
     *              User agent to use in network requests. If you pass null to this parameter,
     *              a default user agent will be used
     * @param path
     *              Base path to store the local cache. If you pass null to this parameter,
     *              the SDK will not use any local cache.
     */
    public MegaApiSwing(String appKey, String userAgent, String path) {
        super(appKey, userAgent, path, new MegaGfxProcessor());
    }

    @Override
    void runCallback(Runnable runnable) {
        SwingUtilities.invokeLater(runnable);
    }
}
