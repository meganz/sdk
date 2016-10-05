/**
 * @file examples/megacmd/configurationmanager.cpp
 * @brief MegaCMD: configuration manager
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

#include "configurationmanager.h"
#include "megaapi_impl.h"

#include <pwd.h>  //getpwuid_r
#include <fstream>


bool is_file_exist(const char *fileName)
{
    std::ifstream infile(fileName);
    return infile.good();
}

string ConfigurationManager::configFolder;
map<string, sync_struct *> ConfigurationManager::configuredSyncs;
string ConfigurationManager::session;
map<string, sync_struct *> ConfigurationManager::loadedSyncs;


std::string ConfigurationManager::getConfigFolder()
{
    return configFolder;
}

void ConfigurationManager::loadConfigDir(){
    const char *homedir = NULL;
    homedir = getenv("HOME");
    if (!homedir)
    {
        struct passwd pd;
        struct passwd* pwdptr = &pd;
        struct passwd* tempPwdPtr;
        char pwdbuffer[200];
        int pwdlinelen = sizeof( pwdbuffer );

        if (( getpwuid_r(22, pwdptr, pwdbuffer, pwdlinelen, &tempPwdPtr)) != 0)
        {
            LOG_fatal << "Couldnt get HOME folder";
            return;
        }
        else
        {
            homedir = pwdptr->pw_dir;
        }
    }

    stringstream sconfigDir;
    sconfigDir << homedir << "/" << ".megaCmd";

    MegaFileSystemAccess *fsAccess = new MegaFileSystemAccess();
    configFolder = sconfigDir.str();

    int oldPermissions = fsAccess->getdefaultfolderpermissions();
    fsAccess->setdefaultfolderpermissions(0700);
    if (!is_file_exist(configFolder.c_str()) && !fsAccess->mkdirlocal(&configFolder, false))
    {
        LOG_err << "Config folder not created";
        return;
    }
    fsAccess->setdefaultfolderpermissions(oldPermissions);
    delete fsAccess;
}


void ConfigurationManager::saveSession(const char*session){
    stringstream sessionfile;
    if (!configFolder.size())
    {
        loadConfigDir();
    }
    if (configFolder.size())
    {
        sessionfile << configFolder << "/" << "session";
        LOG_debug << "Session file: " << sessionfile.str();

        ofstream fo(sessionfile.str().c_str(), ios::out);

        if (fo.is_open())
        {
            fo << session;
            fo.close();
        }
    }
    else
    {
        LOG_err << "Couldnt access configuration folder ";
    }
}

void ConfigurationManager::saveSyncs(map<string, sync_struct *> syncsmap)
{
    stringstream syncsfile;
    if (!configFolder.size())
    {
        loadConfigDir();
    }
    if (configFolder.size())
    {
        syncsfile << configFolder << "/" << "syncs";
        LOG_debug << "Syncs file: " << syncsfile.str();

        ofstream fo(syncsfile.str().c_str(), ios::out | ios::binary);

        if (fo.is_open())
        {
            map<string, sync_struct *>::iterator itr;
            int i = 0;
            for (itr = syncsmap.begin(); itr != syncsmap.end(); ++itr, i++)
            {
                sync_struct *thesync = ((sync_struct*)( *itr ).second );

                fo.write((char*)&thesync->fingerprint, sizeof( long long ));
                fo.write((char*)&thesync->handle, sizeof( MegaHandle ));
                const char * localPath = thesync->localpath.c_str();
                size_t lengthLocalPath = thesync->localpath.size();
                fo.write((char*)&lengthLocalPath, sizeof( size_t ));
                fo.write((char*)localPath, sizeof( char ) * lengthLocalPath);
            }

            fo.close();
        }
    }
    else
    {
        LOG_err << "Couldnt access configuration folder ";
    }
}

void ConfigurationManager::loadConfiguration(){
    stringstream sessionfile;
    if (!configFolder.size())
    {
        loadConfigDir();
    }
    if (configFolder.size())
    {
        sessionfile << configFolder << "/" << "session";
        LOG_debug << "Session file: " << sessionfile.str();

        ifstream fi(sessionfile.str().c_str(), ios::in);
        if (fi.is_open())
        {
            string line;
            getline(fi, line);
            {
                session = line;
                LOG_debug << "Session read from configuration: " << line.substr(0, 5) << "...";
                //login?
            }
            fi.close();
        }

        stringstream syncsfile;
        if (!configFolder.size())
        {
            loadConfigDir();
        }
        if (configFolder.size())
        {
            syncsfile << configFolder << "/" << "syncs";
            LOG_debug << "Syncs file: " << syncsfile.str();

            ifstream fi(syncsfile.str().c_str(), ios::in | ios::binary);

            if (fi.is_open())
            {
                if (fi.fail())
                {
                    LOG_err << "fail with sync file";
                }

                while (!( fi.peek() == EOF ))
                {
                    sync_struct *thesync = new sync_struct; //TODO: this is never deleted
                    //Load syncs
                    fi.read((char*)&thesync->fingerprint, sizeof( long long ));
                    fi.read((char*)&thesync->handle, sizeof( MegaHandle ));
                    size_t lengthLocalPath;
                    fi.read((char*)&lengthLocalPath, sizeof( size_t ));
                    char localPath[lengthLocalPath + 1];
                    fi.read((char*)localPath, sizeof( char ) * lengthLocalPath);
                    localPath[lengthLocalPath] = '\0';
                    thesync->localpath = string(localPath);

                    configuredSyncs[localPath] = thesync;
                }

                if (fi.bad())
                {
                    LOG_err << "fail with sync file  at the end";
                }

                fi.close();
            }
        }
    }
    else
    {
        LOG_err << "Couldnt access configuration folder ";
    }
}
