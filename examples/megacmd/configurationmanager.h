#ifndef CONFIGURATIONMANAGER_H
#define CONFIGURATIONMANAGER_H

using namespace std;

#include "megacmd.h"
#include <fstream>
#include <map>

class ConfigurationManager{
private:
    static string configFolder;

    static void loadConfigDir();


public:
    static map<string,sync_struct *> configuredSyncs;
    static string session;

    static bool isConfigurationLoaded()
    {
        return configFolder.size();
    }

    static void loadConfiguration();

    static void saveSyncs(map<string,sync_struct *> syncsmap);

    static void saveSession(const char*session);
};


#endif // CONFIGURATIONMANAGER_H
