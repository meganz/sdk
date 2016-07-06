#ifndef CONFIGURATIONMANAGER_H
#define CONFIGURATIONMANAGER_H

#include "megacmd.h"
#include <fstream>
#include <map>

class ConfigurationManager{
private:
    static std::string configFolder;

    static void loadConfigDir();


public:
    static std::map<std::string,sync_struct *> configuredSyncs;
    static std::string session;

    static bool isConfigurationLoaded()
    {
        return configFolder.size();
    }

    static void loadConfiguration();

    static void saveSyncs(std::map<std::string,sync_struct *> syncsmap);

    static void saveSession(const char*session);
};


#endif // CONFIGURATIONMANAGER_H
