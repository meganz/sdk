/**
 * @file posix/console.cpp
 * @brief POSIX console/terminal control
 *
 * (c) 2013-2014 by Mega Limited, Wellsford, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#include "mega.h"

namespace mega {
using namespace std;

PosixConsole::PosixConsole()
{
    // set up the console
    if (tcgetattr(STDIN_FILENO, &term) < 0)
    {
        perror("tcgetattr");
        throw runtime_error("tcgetattr");
    }

    oldlflag = term.c_lflag;
    oldvtime = term.c_cc[VTIME];
    term.c_lflag &= static_cast<tcflag_t>(~ICANON);
    term.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &term) < 0)
    {
        perror("tcsetattr");
        throw runtime_error("tcsetattr at ctor");
    }
}

PosixConsole::~PosixConsole()
{
    term.c_lflag = oldlflag;
    term.c_cc[VTIME] = oldvtime;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &term) < 0)
    {
        perror("tcsetattr");
    }
}

// FIXME: UTF-8 compatibility
void PosixConsole::readpwchar(char* pw_buf, int pw_buf_size, int* pw_buf_pos, char** line)
{
    char c;

    if (read(STDIN_FILENO, &c, 1) == 1)
    {
        if (c == 8 && *pw_buf_pos)
        {
            (*pw_buf_pos)--;
        }
        else if (c == 13)
        {
            *line = (char*)malloc(static_cast<size_t>(*pw_buf_pos + 1));
            memcpy(*line, pw_buf, static_cast<size_t>(*pw_buf_pos));
            (*line)[*pw_buf_pos] = 0;
        }
        else if (*pw_buf_pos < pw_buf_size)
        {
            pw_buf[(*pw_buf_pos)++] = c;
        }
    }
}

void PosixConsole::setecho(bool /*echo*/)
{
}
} // namespace
