package nz.mega.sdk;

/**
 * Interface to receive SDK logs.
 * You can implement this class and pass an object of your subclass to MegaApiJava.setLoggerClass()
 * to receive SDK logs. You will also have to use MegaApiJava.setLogLevel() to select the level of
 * the logs that you want to receive.
 */
class DelegateMegaLogger extends MegaLogger {
    MegaLoggerInterface listener;

    DelegateMegaLogger(MegaLoggerInterface listener) {
        this.listener = listener;
    }

    /**
     * This function will be called with all logs with level <= your selected level of logging.
     * By default logging level is MegaApi.LOG_LEVEL_INFO.
     *
     * @param time
     *            Readable string representing the current time.
     *            The SDK retains the ownership of this string, it will not be valid after this function returns.
     * @param loglevel
     *            Log level of this message.
     *            Valid values are: <br>
     *          • LOG_LEVEL_FATAL = 0 <br>
     *          • LOG_LEVEL_ERROR = 1 <br>
     *          • LOG_LEVEL_WARNING = 2 <br>
     *          • LOG_LEVEL_INFO = 3 <br>
     *          • LOG_LEVEL_DEBUG = 4 <br>
     *          • LOG_LEVEL_MAX = 5
     * @param source
     *            Location where this log was generated.
     *            For logs generated inside the SDK, this will contain the source file and the line of code.
     *            The SDK retains the ownership of this string, it will not be valid after this function returns.
     * @param message
     *            Log message.
     *            The SDK retains the ownership of this string, it will be valid after this function returns.
     */
    public void log(String time, int loglevel, String source, String message) {
        if (listener != null) {
            listener.log(time, loglevel, source, message);
        }
    }
}
