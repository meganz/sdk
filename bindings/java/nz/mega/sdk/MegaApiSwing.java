package nz.mega.sdk;

import javax.swing.SwingUtilities;

public class MegaApiSwing extends MegaApiJava {

    public MegaApiSwing(String appKey, String userAgent, String path) {
        super(appKey, userAgent, path, new MegaGfxProcessor());
    }

    @Override
    void runCallback(Runnable runnable) {
        SwingUtilities.invokeLater(runnable);
    }
}
