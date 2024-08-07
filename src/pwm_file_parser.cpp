#include "mega/pwm_file_parser.h"

#include "mega/mega_csv.h"

namespace mega::pwm::import
{

namespace
{
/**
 * @brief Checks that all the names in expected are contained in existing. Each time one of the
 * expected is not found in existing, an error messages is appended to the outErrMsg output
 * parameter.
 *
 * @return The number of missing entries.
 */
unsigned int missingNames(const std::vector<std::string>& existing,
                          const std::vector<std::string>& expected,
                          std::string& outErrMsg)
{
    unsigned int missing = 0;
    for (const auto& e: expected)
    {
        if (std::find(std::begin(existing), std::end(existing), e) == std::end(existing))
        {
            outErrMsg += "Missing mandatory column with name: " + e + "\n";
            ++missing;
        }
    }
    return missing;
}

inline bool fileIsAccessible(const std::string& fname)
{
    std::ifstream f(fname.c_str());
    return f.good();
}

}

PassFileParseResult parseGooglePasswordCSVFile(const std::string& filePath)
{
    csv::CSVFormat format;
    format.delimiter(',').header_row(0).variable_columns(true);

    // We will go through the file in parallel to get original content for error reporting
    PassFileParseResult result;
    std::ifstream openFile(filePath.c_str());
    if (!openFile.good())
    {
        result.mErrCode = PassFileParseResult::ErrCode::CANT_OPEN_FILE;
        result.mErrMsg = "File can not be opened";
        return result;
    }

    if (std::string headerLine; !std::getline(openFile, headerLine))
    {
        // File is empty
        result.mErrCode = PassFileParseResult::ErrCode::INVALID_HEADER;
        result.mErrMsg = "File should have at least a header row";
        return result;
    }

    csv::CSVReader reader{filePath, format};
    const auto colNames = reader.get_col_names();
    static const std::vector<std::string> expectedColumnNames{"name",
                                                              "url",
                                                              "username",
                                                              "password",
                                                              "note"};

    if (unsigned int nMissing = missingNames(colNames, expectedColumnNames, result.mErrMsg);
        nMissing != 0)
    {
        if (nMissing == expectedColumnNames.size())
        {
            result.mErrCode = PassFileParseResult::ErrCode::INVALID_HEADER;
            result.mErrMsg += "The first line of the .csv file is expected to be a header with the "
                              "column names separated by commas.";
        }
        else
        {
            result.mErrCode = PassFileParseResult::ErrCode::MISSING_COLUMN;
        }
        return result;
    }
    size_t expectedNumCols = colNames.size();
    bool thereIsAValidEntry = false;

    for (auto& row: reader)
    {
        PassEntryParseResult entryResult;
        // Save the original line
        std::getline(openFile, entryResult.mOriginalContent);
        if (row.size() != expectedNumCols)
        {
            entryResult.mErrCode = PassEntryParseResult::ErrCode::INVALID_NUM_OF_COLUMN;
            result.mResults.emplace_back(std::move(entryResult));
            continue;
        }

        entryResult.mName = row["name"].get();
        entryResult.mUrl = row["url"].get();
        entryResult.mUserName = row["username"].get();
        entryResult.mPassword = row["password"].get();
        entryResult.mNote = row["note"].get();

        result.mResults.emplace_back(std::move(entryResult));
        thereIsAValidEntry = true;
    }
    if (!thereIsAValidEntry)
    {
        result.mErrCode = PassFileParseResult::ErrCode::NO_VALID_ENTRIES;
        result.mErrMsg = result.mResults.empty() ?
                             "The input file has no entries to read" :
                             "All the entries in the file were wrongly formatted";
    }
    return result;
}

PassFileParseResult readPasswordImportFile(const std::string& filePath, const FileSource source)
{
    // Common validation
    // TODO: Once C++17 filesystem is allowed, check for existence for a more detailed error report
    if (!fileIsAccessible(filePath))
    {
        PassFileParseResult result;
        result.mErrCode = PassFileParseResult::ErrCode::CANT_OPEN_FILE;
        result.mErrMsg = "File (" + filePath + ") could not be opened.";
        return result;
    }

    switch (source)
    {
        case FileSource::GOOGLE_PASSWORD:
            return parseGooglePasswordCSVFile(filePath);
    }
    assert(false); // All cases should be covered by the switch statement
    return {};
}
}
