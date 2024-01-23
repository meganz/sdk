#pragma once

#include <sys/types.h>
#include <sys/stat.h>

#include <dirent.h>

#include <ostream>

#include <mega/fuse/common/testing/printers.h>

void PrintTo(const struct dirent& entry, std::ostream* ostream);

void PrintTo(const struct stat& stat, std::ostream* ostream);

