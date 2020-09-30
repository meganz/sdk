package nz.mega.sdk;

public class MegaPushNotificationSettingsAndroid {

    /**
     * Creates a copy of MegaPushNotificationSetting.
     *
     * @param receivedPush The MegaPushNotificationsSetting received.
     * @return A copy of MegaPushNotificationSettings.
     */
    public static MegaPushNotificationSettings copy(MegaPushNotificationSettings receivedPush) {
        return receivedPush.copy();
    }
}
