#include <iostream>
#include <filesystem>
#include <vector>
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
#endif



bool build = false;
bool setup = false;
fs::path portsFile;
fs::path sdkRootPath;
string triplet;

vector<string> ports;

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

            for (auto port : ports)
            {
                auto slashpos = port.find("/");
                if (slashpos == string::npos)
                {
                    cout << "bad port: " << port << endl;
                    return 1;
                }
                string portname = port.substr(0, slashpos);
                string portversion = port.substr(slashpos + 1);

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


            for (auto port : ports)
            {
                auto slashpos = port.find("/");
                if (slashpos == string::npos)
                {
                    cout << "bad port: " << port << endl;
                    return 1;
                }

                execute("vcpkg install --triplet " + triplet + " " + port.substr(0, slashpos));
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
        cout << "Command failed with resuld code " << result << " ( command was " << command << ")" <<endl;
        exit(1);
    }
}


bool showSyntax()
{
    cout << "build3rdParty --setup --ports <ports override file> --triplet <triplet> --sdkroot <path>" << endl;
    cout << "build3rdParty --build --ports <ports override file> --triplet <triplet>" << endl;
    return false;
}

bool readCommandLine(int argc, char* argv[])
{
    if (argc <= 1) return showSyntax();

    std::vector<char*> myargv1(argv, argv + argc);
    std::vector<char*> myargv2;

    for (auto it = myargv1.begin(); it != myargv1.end(); ++it)
    {
        if (std::string(*it) == "--ports")
        {
            if (++it == myargv1.end()) return showSyntax();
            portsFile = *it;
            argc -= 2;
        }
        else if (std::string(*it) == "--triplet")
        {
            if (++it == myargv1.end()) return showSyntax();
            triplet = *it;
            argc -= 2;
        }
        else if (std::string(*it) == "--sdkroot")
        {
            if (++it == myargv1.end()) return showSyntax();
            sdkRootPath = *it;
            argc -= 2;
        }
        else if (std::string(*it) == "--setup")
        {
            setup = true;
            argc -= 1;
        }
        else if (std::string(*it) == "--build")
        {
            build = true;
            argc -= 1;
        }
        else
        {
            myargv2.push_back(*it);
        }
    }

    if (myargv2.empty())
    {
        cout << "unknown parameter: " << myargv2[1] << endl;
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
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(0, 1);
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
        if (s.empty() || s[0] == '#') continue;
        ports.push_back(s);
    }

    return true;
}