#pragma once

#include "stdfs.h"

namespace sdk_test
{

/**
 * @brief Returns the path to the folder containing the test executable.
 *
 * IMPORTANT: `setTestDataDir` must be called before.
 */
fs::path getTestDataDir();

/**
 * @brief Sets the path to the folder where the test binary is located. You must call this inside main.
 *
 * Example:
 *    setTestDataDir(fs::absolute(fs::path(argv[0]).parent_path()));
 *
 */
void setTestDataDir(const fs::path& dataDir);

/**
 * @brief Copy file from the directory where the tests binary is located to the given destination (current working
 * directory by default).
 */
void copyFileFromTestData(fs::path filename, fs::path destination = ".");

}
