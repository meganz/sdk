package nz.mega.sdk;

/**
 * Interface to receive SDK logs.
 * <p>
 * You can implement this class and pass an object of your subclass to MegaApi.setLoggerClass() to receive SDK logs.
 * You will also have to use MegaApi.setLogLevel() to select the level of the logs that you want to receive.
 */
public interface MegaLoggerInterface {
    /**
     * This function will be called for all logs with level <= your selected level of logging.
     * <p>
     * By default log level is MegaApi.LOG_LEVEL_INFO.
     *  
     * @param time
     *            Readable string representing the current time.
     *            The SDK retains the ownership of this string, it will not be valid after this function returns
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
     *            The SDK retains the ownership of this string, it won't be valid after this function returns
     * @param message
     *            Log message.
     *            The SDK retains the ownership of this string, it won't be valid after this function returns
     */
    public void log(String time, int loglevel, String source, String message);
}
