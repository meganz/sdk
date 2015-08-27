package nz.mega.sdk;

public interface MegaLoggerInterface {
    
    /**
     * This function will be called with all logs with level <= your selected level of logging 
     * (by default it is MegaApi::LOG_LEVEL_INFO) 
     *  
     * @param time
     *            Readable string representing the current time.
     *            The SDK retains the ownership of this string, it won't be valid after this funtion returns.
     * @param loglevel
     *            Log level of this message
     *            Valid values are:
     *          • LOG_LEVEL_FATAL = 0
     *          • LOG_LEVEL_ERROR = 1
     *          • LOG_LEVEL_WARNING = 2
     *          • LOG_LEVEL_INFO = 3
     *          • LOG_LEVEL_DEBUG = 4
     *          • LOG_LEVEL_MAX = 5
     * @param source
     *            Location where this log was generated
     *            For logs generated inside the SDK, this will contain the source file and the line of code. 
     *            The SDK retains the ownership of this string, it won't be valid after this funtion returns.
     * @param message
     *            Log message
     *            The SDK retains the ownership of this string, it won't be valid after this funtion returns. 
     */
    public void log(String time, int loglevel, String source, String message);
}
