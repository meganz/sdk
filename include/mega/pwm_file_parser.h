#ifndef INCLUDE_MEGA_PWM_FILE_PARSER_H_
#define INCLUDE_MEGA_PWM_FILE_PARSER_H_

#include "mega/types.h"

namespace mega::pwm::import
{

/**
 * @class PassEntryParseResult
 * @brief A helper struct to store the result from parsing an entry in a file with passwords to
 * import
 *
 */
struct PassEntryParseResult
{
    enum class ErrCode : uint8_t
    {
        OK = 0,
        INVALID_NUM_OF_COLUMN,
    };

    /**
     * @brief An error code associated to a problem that invalidates the parsing of the entry.
     */
    ErrCode mErrCode = ErrCode::OK;

    /**
     * @brief The contents from the file that was used to create the entry.
     */
    std::string mOriginalContent;

    /**
     * @brief The name that labels the password entry.
     *
     * @note This struct does not force any condition on this member, i.e. empty is a valid value
     */
    std::string mName;

    /**
     * @brief Members for the different fields that can be found in an entry
     */
    std::string mUrl;
    std::string mUserName;
    std::string mPassword;
    std::string mNote;
};

/**
 * @class PassFileParseResult
 * @brief A helper struct to hold a full report of the parsing password file process.
 *
 */
struct PassFileParseResult
{
    enum class ErrCode : uint8_t
    {
        OK = 0,
        NO_VALID_ENTRIES,
        FILE_DOES_NOT_EXIST,
        CANT_OPEN_FILE,
        MISSING_COLUMN,
        INVALID_HEADER,
    };

    /**
     * @brief An error code associated to a problem that invalidates the file parsing as a whole.
     */
    ErrCode mErrCode = ErrCode::OK;

    /**
     * @brief An error messages with additional information useful for logging.
     */
    std::string mErrMsg;

    /**
     * @brief A vector with a PassEntryParseResult object with the parse information for each of the
     * rows found in the file.
     */
    std::vector<PassEntryParseResult> mResults;
};

/**
 * @brief Reads the password entries listed in the input csv file exported from Google's Password
 * application.
 *
 * @param filePath The path to the csv file.
 * @return A container holding all the valid and invalid entries found together, with handy error
 * codes and messages.
 */
PassFileParseResult parseGooglePasswordCSVFile(const std::string& filePath);

enum class FileSource : uint8_t
{
    GOOGLE_PASSWORD = 0,
};

/**
 * @brief Given the source of the file with passwords to import, decides the parser to apply and
 * returns the result from parsing the file
 *
 * @param filePath The path to the csv file.
 * @param source The app that was used to export the file
 * @return A container holding all the valid and invalid entries found together, with handy error
 * codes and messages.
 */
PassFileParseResult readPasswordImportFile(const std::string& filePath, const FileSource source);
}

#endif // INCLUDE_MEGA_PWM_FILE_PARSER_H_
