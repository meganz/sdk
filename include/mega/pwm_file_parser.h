#ifndef INCLUDE_MEGA_PWM_FILE_PARSER_H_
#define INCLUDE_MEGA_PWM_FILE_PARSER_H_

#include "megaapi_impl.h"

namespace mega::pwm::import
{

// Error codes when parsing a password entry in a file
enum class EPassEntryParseError : uint8_t
{
    ok = 0,
    invalidNumOfColumns,
};

/**
 * @class PassEntryParseResult
 * @brief A helper struct to store the result from parsing an entry in a file with passwords to
 * import
 *
 */
struct PassEntryParseResult
{
    /**
     * @brief An error code associated to a problem that invalidates the parsing of the entry.
     */
    EPassEntryParseError mErrCode = EPassEntryParseError::ok;

    /**
     * @brief The line number in the file associated toe the entry. This can be useful to report the
     * source of the problems.
     */
    uint32_t mLineNumber{0};

    /**
     * @brief The name that labels the password entry.
     *
     * @note This struct does not force any condition on this member, i.e., empty is a valid value
     */
    std::string mName;

    /**
     * @brief The data stored in the entry. Already in a format to be used in the password node
     * creation step
     */
    mega::MegaNodePrivate::PNDataPrivate mData{nullptr, nullptr, nullptr, nullptr};
};

// Error codes when parsing a file with passwords
enum class EPassFileParseError : uint8_t
{
    ok = 0,
    noValidEntries,
    fileDoesNotExist,
    cantOpenFile,
    missingColumn,
};

/**
 * @class PassFileParseResult
 * @brief A helper struct to hold a full report of the parsing password file process.
 *
 */
struct PassFileParseResult
{
    /**
     * @brief An error code associated to a problem that invalidates the file parsing as a whole.
     */
    EPassFileParseError mErrCode = EPassFileParseError::ok;

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
 * @return A PassFileParseResult object containing all the valid and invalid found entries together
 * with handy error codes and messages.
 */
PassFileParseResult parseGooglePasswordCSVFile(const std::string& filePath);

}

#endif // INCLUDE_MEGA_PWM_FILE_PARSER_H_
