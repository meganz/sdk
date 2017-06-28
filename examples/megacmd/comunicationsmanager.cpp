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

#ifdef _WIN32
std::ostream &operator<<(std::ostream &os, const CmdPetition& p)
{
    return os << p.line;
}
#endif

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

void ComunicationsManager::registerStateListener(CmdPetition *inf)
{
    stateListenersPetitions.push_back(inf);
    if (stateListenersPetitions.size()>300 && stateListenersPetitions.size()%10 == 0) //TODO: define limit as constant ~300
    {
        LOG_debug << " Number of register listeners has grown too much: " << stateListenersPetitions.size() << ". Sending an ACK to discard disconnected ones.";
        string sack="ack";
        informStateListeners(sack);
    }
    return;
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


void ComunicationsManager::stopWaiting()
{
}

int ComunicationsManager::get_next_comm_id()
{
    return 0;
}

void ComunicationsManager::informStateListeners(string &s)
{
    for (std::vector< CmdPetition * >::iterator it = stateListenersPetitions.begin(); it != stateListenersPetitions.end();)
    {
        if (informStateListener((CmdPetition *)*it, s) <0)
        {
            delete *it;
            it = stateListenersPetitions.erase(it);
        }
        else
        {
             ++it;
        }
    }
}

int ComunicationsManager::informStateListener(CmdPetition *inf, string &s)
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

bool ComunicationsManager::getConfirmation(CmdPetition *inf, string message)
{
    return false;
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
