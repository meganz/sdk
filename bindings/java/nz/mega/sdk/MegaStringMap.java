package nz.mega.sdk;

public class MegaStringMap {

    /**
     * Creates a copy of MegaStringMap required for its usage in the app.
     *
     * @param map The MegaStringMap received.
     * @return A copy of MegaStringMap.
     */
    public static MegaStringMap copy(MegaStringMap megaStringMap) {
        return megaStringMap.copy();
    }
}
