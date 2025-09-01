#pragma once

#include <mega/fuse/common/testing/printers.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <dirent.h>
#include <ostream>

void PrintTo(const struct dirent& entry, std::ostream* ostream);

void PrintTo(const struct stat& stat, std::ostream* ostream);
