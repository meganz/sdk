#include "mega/pwm_file_parser.h"

#include <vincentlaucsb-csv-parser/csv.hpp>

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
    PassFileParseResult result;
    if (!fileIsAccessible(filePath))
    {
        result.mErrCode = EPassFileParseError::cantOpenFile;
        result.mErrMsg = "File (" + filePath + ") cannot be opened.";
        return result;
    }

    csv::CSVFormat format;
    format.delimiter(',').header_row(0).variable_columns(true);

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
        result.mErrCode = EPassFileParseError::missingColumn;
        if (nMissing == expectedColumnNames.size())
            result.mErrMsg += "The first line of the .csv file is expected to be a header with the "
                              "column names separated by commas.";
        return result;
    }
    size_t expectedNumCols = colNames.size();
    uint32_t lineNumber = 0;
    bool thereIsAValidEntry = false;
    for (auto& row: reader)
    {
        PassEntryParseResult entryResult;
        entryResult.mLineNumber = ++lineNumber;
        if (row.size() != expectedNumCols)
        {
            entryResult.mErrCode = EPassEntryParseError::invalidNumOfColumns;
            result.mResults.emplace_back(std::move(entryResult));
            continue;
        }

        if (std::string name = row["name"].get<>(); !name.empty())
            entryResult.mName = std::move(name);
        if (std::string url = row["url"].get<>(); !url.empty())
            entryResult.mData.setUrl(url.c_str());
        if (std::string username = row["username"].get<>(); !username.empty())
            entryResult.mData.setUserName(username.c_str());
        if (std::string password = row["password"].get<>(); !password.empty())
            entryResult.mData.setPassword(password.c_str());
        if (std::string note = row["note"].get<>(); !note.empty())
            entryResult.mData.setNotes(note.c_str());

        result.mResults.emplace_back(std::move(entryResult));
        thereIsAValidEntry = true;
    }
    if (!thereIsAValidEntry)
    {
        result.mErrCode = EPassFileParseError::noValidEntries;
        result.mErrMsg = result.mResults.empty() ?
                             "The input file has no entries to read" :
                             "All the entries in the file were wrongly formatted";
    }
    return result;
}

}
