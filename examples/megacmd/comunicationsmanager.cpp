/**
 * @file examples/megacmd/comunicationsmanager.cpp
 * @brief MegaCMD: Communications manager non supporting non-interactive mode
 *
 * (c) 2013-2016 by Mega Limited, Auckland, New Zealand
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

#include "comunicationsmanager.h"

using namespace std;
using namespace mega;

OUTSTREAMTYPE &operator<<(OUTSTREAMTYPE &os, const CmdPetition& p)
{
    return os << p.line;
}

ComunicationsManager::ComunicationsManager()
{
}




bool ComunicationsManager::receivedReadlineInput(int readline_fd)
{
    return FD_ISSET(readline_fd, &fds);
}

bool ComunicationsManager::receivedPetition()
{
    return false;
}

int ComunicationsManager::waitForPetitionOrReadlineInput(int readline_fd)
{
    FD_ZERO(&fds);
    FD_SET(readline_fd, &fds);

    int rc = select(FD_SETSIZE, &fds, NULL, NULL, NULL);
    if (rc < 0)
    {
        if (errno != EINTR)  //syscall
        {
#ifdef _WIN32
            if (errno != ENOENT) // unexpectedly enters here, although works fine TODO: review this
#endif
            LOG_fatal << "Error at select: " << errno;
            return errno;
        }
    }
    return 0;
}


int ComunicationsManager::waitForPetition()
{
    return 0;
}

void ComunicationsManager::returnAndClosePetition(CmdPetition *inf, OUTSTRINGSTREAM *s, int outCode)
{
    delete inf;
    return;
}

/**
 * @brief getPetition
 * @return pointer to new CmdPetition. Petition returned must be properly deleted (this can be calling returnAndClosePetition)
 */
CmdPetition * ComunicationsManager::getPetition()
{
    CmdPetition *inf = new CmdPetition();
    return inf;
}

string ComunicationsManager::get_petition_details(CmdPetition *inf)
{
    return "";
}


ComunicationsManager::~ComunicationsManager()
{
}

MegaThread *CmdPetition::getPetitionThread() const
{
    return petitionThread;
}

void CmdPetition::setPetitionThread(MegaThread *value)
{
    petitionThread = value;
}
