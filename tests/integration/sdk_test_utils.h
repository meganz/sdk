#pragma once

#include "stdfs.h"

namespace sdk_test
{

/**
 * @brief Returns the path to the folder containing resources for the tests.
 *
 * IMPORTANT: `setTestDataDir` must be called before.
 */
fs::path getTestDataDir();

/**
 * @brief Sets the path to the folder where the test resources are located. Usually called from the main function.
 * It should be set before calling getTestDataDir or copyFileFromTestData
 *
 * Example:
 *    setTestDataDir(fs::absolute(fs::path(argv[0]).parent_path()));
 *
 */
void setTestDataDir(const fs::path& dataDir);

/**
 * @brief Copies file from the resources data directory to the given destination (current working directory by default).
 * IMPORTANT: `setTestDataDir` must be called before.
 */
void copyFileFromTestData(fs::path filename, fs::path destination = ".");

}
