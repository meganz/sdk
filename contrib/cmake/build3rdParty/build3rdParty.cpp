#include <iostream>
#include <filesystem>
#include <vector>
#include <map>
#include <string>
#include <fstream>

using namespace std;


#if (__cplusplus >= 201703L && (!defined(__GNUC__) || (__GNUC__*100+__GNUC_MINOR__) >= 800))
#include <filesystem>
namespace fs = std::filesystem;
#define USE_FILESYSTEM
#elif !defined(__MINGW32__) && !defined(__ANDROID__) && (!defined(__GNUC__) || (__GNUC__*100+__GNUC_MINOR__) >= 503)
#define USE_FILESYSTEM
#ifdef WIN32
#include <filesystem>
namespace fs = std::experimental::filesystem;
#else
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#endif
#else
//for mac
#include <filesystem>
namespace fs = std::__fs::filesystem;
#endif



bool build = false;
bool setup = false;
bool removeUnusedPorts = false;
bool noPkgConfig = false;
fs::path portsFile;
fs::path sdkRootPath;
string triplet;

map<string, string> ports;

fs::path initialDir = fs::current_path();
fs::path vcpkgDir = fs::current_path() / "vcpkg";
fs::path cloneDir = fs::current_path() / "vcpkg_clone";


bool readCommandLine(int argc, char* argv[]);
void execute(string command);

int main(int argc, char* argv[])
try
{
    if (readCommandLine(argc, argv))
    {
        if (setup)
        {
            if (!fs::is_directory("vcpkg"))
            {
                execute("git clone https://github.com/microsoft/vcpkg.git");
                execute("git clone --progress -v vcpkg vcpkg_clone");
                fs::current_path("vcpkg");
                #ifdef WIN32
                    execute(".\\bootstrap-vcpkg.bat -disableMetrics");
                #else
                    execute("./bootstrap-vcpkg.sh -disableMetrics");
                #endif
            }
            else
            {
                fs::current_path("vcpkg");
            }

            if (fs::exists(sdkRootPath / "contrib" / "cmake" / "vcpkg_extra_triplets" / (triplet + ".cmake")))
            {
                if (fs::exists(vcpkgDir / "triplets" / (triplet + ".cmake")))
                {
                    fs::remove(vcpkgDir / "triplets" / (triplet + ".cmake"));
                }
                cout << "Copying triplet from SDK: " << triplet << endl;
                fs::copy(sdkRootPath / "contrib" / "cmake" / "vcpkg_extra_triplets" / (triplet+".cmake"), vcpkgDir / "triplets" / (triplet + ".cmake"));
            }
            else if (!fs::exists(vcpkgDir / "triplets" / (triplet + ".cmake")))
            {
                cout << "triplet not found in the SDK or in vcpkg: " << triplet << endl;
                exit(1);
            }

            for (auto portPair : ports)
            {
                const string& portname = portPair.first;
                const string& portversion = portPair.second;

                if (fs::is_directory(fs::path("ports") / portname))
                {
                    cout << "Removing " << (vcpkgDir / "ports" / portname).u8string() << endl;
                    fs::remove_all(vcpkgDir / "ports" / portname);
                }
                if (portversion.size() == 40)
                {
                    fs::current_path(cloneDir);
                    execute("git checkout --quiet " + portversion);
                    cout << "Copying port for " << portname << " from vcpkg commit " << portversion << endl;
                    fs::copy(cloneDir / "ports" / portname, vcpkgDir / "ports" / portname, fs::copy_options::recursive);
                    fs::current_path(vcpkgDir);
                }
                else
                {
                    cout << "Copying port for " << portname << " from SDK customized port " << portversion << endl;
                    fs::copy(sdkRootPath / "contrib" / "cmake" / "vcpkg_extra_ports" / portname / portversion, vcpkgDir / "ports" / portname, fs::copy_options::recursive);
                }
            }

            if (removeUnusedPorts)
            {
                for (auto dir = fs::directory_iterator(vcpkgDir / "ports"); dir != fs::directory_iterator(); ++dir)
                {
                    if (ports.find(dir->path().filename().u8string()) == ports.end())
                    {
                        fs::remove_all(dir->path());
                    }
                }
            }

            if (noPkgConfig)
            {
                cout << "Performing no-op substitution of vcpkg_fixup_pkgconfig and PKGCONFIG to skip pkgconfig integration/checks\n";
                ofstream vcpkg_fixup_pkgconfig(vcpkgDir / "scripts" / "cmake" / "vcpkg_fixup_pkgconfig.cmake", std::ios::trunc);
                if (!vcpkg_fixup_pkgconfig)
                {
                    cout << "Could not open vcpkg script file to suppress pkgconfig\n";
                    return 1;
                }

                vcpkg_fixup_pkgconfig << 
                "function(vcpkg_fixup_pkgconfig)\n"
                "endfunction()\n"
                "set(PKGCONFIG \":\")\n"; // i.e., use no-op : operator
            }

        }
        else if (build)
        {

            if (!fs::is_directory("vcpkg"))
            {
                cout << "This command should be run from just outside 'vcpkg' folder - maybe it is not set up?" << endl;
                return 1;
            }
            else
            {
                fs::current_path("vcpkg");
            }


            for (auto portPair : ports)
            {
                #ifdef WIN32
                    execute("vcpkg install --triplet " + triplet + " " + portPair.first);
                #else
                    execute("./vcpkg install --triplet " + triplet + " " + portPair.first);
                #endif
            }
        }
    }

    return 0;
}
catch (exception& e)
{
    cout << "exception: " << e.what() << endl;
    return 1;
}


void execute(string command)
{
    cout << "Executing: " << command << endl;
    int result = system(command.c_str());
    if (result != 0)
    {
        cout << "Command failed with result code " << result << " ( command was " << command << ")" <<endl;
        exit(1);
    }
}


bool showSyntax()
{
    cout << "build3rdParty --setup [--removeunusedports] [--nopkgconfig] --ports <ports override file> --triplet <triplet> --sdkroot <path>" << endl;
    cout << "build3rdParty --build --ports <ports override file> --triplet <triplet>" << endl;
    return false;
}

bool readCommandLine(int argc, char* argv[])
{
    if (argc <= 1) return showSyntax();

    std::vector<char*> myargv1(argv + 1, argv + argc);
    std::vector<char*> myargv2;

    for (auto it = myargv1.begin(); it != myargv1.end(); ++it)
    {
        if (std::string(*it) == "--ports")
        {
            if (++it == myargv1.end()) return showSyntax();
            portsFile = *it;
        }
        else if (std::string(*it) == "--triplet")
        {
            if (++it == myargv1.end()) return showSyntax();
            triplet = *it;
        }
        else if (std::string(*it) == "--sdkroot")
        {
            if (++it == myargv1.end()) return showSyntax();
            sdkRootPath = *it;
        }
        else if (std::string(*it) == "--setup")
        {
            setup = true;
        }
        else if (std::string(*it) == "--removeunusedports" && setup)
        {
            removeUnusedPorts = true;
        }
        else if (std::string(*it) == "--nopkgconfig" && setup)
        {
            noPkgConfig = true;
        }
        else if (std::string(*it) == "--build")
        {
            build = true;
        }
        else
        {
            myargv2.push_back(*it);
        }
    }

    if (!myargv2.empty())
    {
        cout << "unknown parameter: " << myargv2[0] << endl;
        return false;
    }

    if (!(setup || build) || portsFile.empty() || triplet.empty())
    {
        return showSyntax();
    }

    if (setup && sdkRootPath.empty())
    {
        return showSyntax();
    }

    ifstream portsIn(portsFile.string().c_str());
    while (portsIn)
    {
        string s;
        getline(portsIn, s);

        // remove comments
        auto hashpos = s.find("#");
        if (hashpos != string::npos) s.erase(hashpos);

        // remove whitespace
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(0, 1);
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
        if (s.empty()) continue;

        // extract port/version map
        auto slashpos = s.find("/");
        if (slashpos == string::npos)
        {
            cout << "bad port: " << s << endl;
            return 1;
        }
        string portname = s.substr(0, slashpos);
        string portversion = s.substr(slashpos + 1);

        auto existing = ports.find(portname);
        if (existing != ports.end() && existing->second != portversion)
        {
            cout << "conflicting port versions: " << portname << " " << existing->second << " " << portversion << endl;
            return 1;
        }
        ports[portname] = portversion;

    }

    return true;
}
