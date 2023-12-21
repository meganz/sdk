#include <iostream>
#include <filesystem>
#include <vector>
#include <map>
#include <string>
#include <sstream>
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

// platform_ prefix to avoid #define subsitutions on linux
enum class Platform {
    platform_all, // special value to indicate build on all platforms
    platform_windows,
    platform_osx,
    platform_ios,
    platform_linux,
};

Platform buildPlatform =
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
Platform::platform_windows
#elif __APPLE__
#include <TargetConditionals.h>
    #if TARGET_OS_MAC
Platform::platform_osx
    #elif TARGET_OS_IPHONE || TARGET_IPHONE_SIMULATOR
Platform::platform_ios
    #endif
#elif __linux__
Platform::platform_linux
#endif
; // error here: platform not detected from supported list


bool build = false;
bool setup = false;
bool removeUnusedPorts = false;
bool noPkgConfig = false;
fs::path portsFile;
fs::path sdkRootPath;
fs::path patchPath;
string triplet;

map<string, string> ports;
map<string, string> featurePackages;
map<string, fs::path> patches;

fs::path initialDir = fs::current_path();
fs::path vcpkgDir = fs::current_path() / "vcpkg";
fs::path cloneDir = fs::current_path() / "vcpkg_clone";

vector<string> split(const string& input, char separator);
string platformToString(Platform p);
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

            const fs::path vcpkgTripletDir = vcpkgDir / "triplets";
            const fs::path tripletFile = triplet + ".cmake";

            if (fs::exists(sdkRootPath / "contrib" / "cmake" / "vcpkg_overlay_triplets" / tripletFile))
            {
                if (fs::exists(vcpkgTripletDir / tripletFile))
                {
                    fs::remove(vcpkgTripletDir / tripletFile);
                }
                cout << "Copying triplet from SDK: " << triplet << endl;
                fs::copy(sdkRootPath / "contrib" / "cmake" / "vcpkg_overlay_triplets" / tripletFile, vcpkgTripletDir / tripletFile);
            }
            else if (!fs::exists(vcpkgTripletDir / tripletFile) && !fs::exists(vcpkgTripletDir / "community" / tripletFile))
            {
                cout << "triplet not found in the SDK or in vcpkg: " << triplet << endl;
                exit(1);
            }

            for (auto portPair : ports)
            {
                const string& portname = portPair.first;
                const string& portversion = portPair.second;

                if (fs::is_directory(vcpkgDir / "ports" / portname))
                {
                    cout << "Removing " << (vcpkgDir / "ports" / portname).u8string() << endl;
                    #ifdef WIN32
                    // remove_all doesn't like read-only files in the git repo.  however it seems that will be fixed in future
                    execute("rmdir /S /Q " + ("\"" + (vcpkgDir / "ports" / portname).u8string() + "\""));
                    #else
                    fs::remove_all(vcpkgDir / "ports" / portname);
                    #endif
                }
                if (portversion.size() == 40) // length of git hash
                {
                    fs::current_path(cloneDir);
                    execute("git checkout --force --quiet " + portversion);
                    cout << "Copying port for " << portname << " from vcpkg commit " << portversion << endl;
                    fs::copy(cloneDir / "ports" / portname, vcpkgDir / "ports" / portname, fs::copy_options::recursive);
                    fs::current_path(vcpkgDir / "ports" / portname);

                    // before we apply any patch, init this as a new git repo to make it easy to
                    // amend or add to patches - or to create a new patch file for this lib
                    // (this turned out to cause problems for normal situations but perhaps we can overcome those in future or use it selectively)
                    //execute("git init .");
                    //execute("git add *.*");
                    //execute("git add ./*");
                    //execute("git commit -m patchInit");

                    auto patch = patches.find(portname);
                    if (patch != patches.end()) {
                        cout << "Applying patch " << patch->second.u8string() << " for port " << portname << "\n";
                        execute("git apply --verbose --ignore-whitespace --directory=ports/" + portname + " " + patch->second.u8string());
                    }
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
                    execute("vcpkg install --triplet " + triplet + " --host-triplet "  + triplet + " " + portPair.first + featurePackages[portPair.first]);
                #else
                    execute("./vcpkg install --triplet " + triplet + " --host-triplet "  + triplet + " " + portPair.first + featurePackages[portPair.first]);
                #endif
            }
        }
        return 0;
    }

    return 1;
}
catch (exception& e)
{
    cout << "exception: " << e.what() << endl;
    return 1;
}

vector<string> split(const string& input, char sep)
{
    vector<string> result;
    string token;
    istringstream tokenStream(input);

    while (std::getline(tokenStream, token, sep))
    {
        result.push_back(token);
    }

    return result;
}

string platformToString(Platform p)
{
    switch (p) {
        case Platform::platform_windows:
        return "windows";
        case Platform::platform_osx:
        return "osx";
        case Platform::platform_ios:
        return "ios";
        case Platform::platform_linux:
        return "linux";
        default:
        throw std::logic_error("Unhandled platform enumerator");
    }
}

Platform stringToPlatform(string s)
{
cout << "checking platform " << s;
        if (s == "windows") return Platform::platform_windows;
        if (s == "osx") return Platform::platform_osx;
        if (s == "ios") return Platform::platform_ios;
        if (s == "linux") return Platform::platform_linux;
        throw std::logic_error("Unhandled platform enumerator");
        return Platform::platform_linux;
}


void execute(string command)
{
    cout << "Executing: " << command << endl;
    int result = system(command.c_str());
    if (result != 0)
    {
        cout << "Command failed with result code " << result << ". Command was: " << command <<endl;
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
        else if (std::string(*it) == "--platform")
        {
            if (++it == myargv1.end()) return showSyntax();
            buildPlatform = stringToPlatform(*it);
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

    patchPath = sdkRootPath / "contrib" / "cmake" / "vcpkg_patches";

    ifstream portsIn(portsFile.string().c_str());
    while (portsIn)
    {
        string s;
        getline(portsIn, s);

        // remove comments
        auto hashpos = s.find("#");
        if (hashpos != string::npos) s.erase(hashpos);

        // remove whitespace
        s.erase(0, s.find_first_not_of(" \n\r\t"));
        s.erase(s.find_last_not_of(" \n\r\t")+1);
        if (s.empty()) continue;

        vector<string> platformExpressions = split(s, ' ');
cout << "split: ";
for (auto s : platformExpressions) cout << " '" << s << "'";
cout << endl;
        if (platformExpressions.empty()) continue;

        s = platformExpressions.front();
        platformExpressions.erase(platformExpressions.begin());

        // check if we have include/exclude or patch expressions for this platform
        fs::path patchFile = "";
        {
           bool shouldBuild = true;

           for (const string& rawExpr : platformExpressions)
           {
cout << "considering " << rawExpr << endl;
               vector<string> expr = split(rawExpr, ':');

               if (expr.size() != 2)
               {
                   cout << "Malformed platform or patch expression " << rawExpr << "\n";
                   exit(1);
               }

               const string& exprPlatform = expr[0];
               const string& exprArg = expr[1];

               if (exprPlatform != "all" && exprPlatform != platformToString(buildPlatform)) continue;

               if (exprArg == "on") {
                  shouldBuild = true;
cout << "build" << endl;
}
               else if (exprArg == "off") {
cout << "don't build" << endl;
shouldBuild = false;
}
               else if (fs::path(expr[1]).extension() != ".patch")
               {
                   cout << "Not a patch file: " << expr[1] << " for " << s << endl;
                   exit(1);
               }
               else {
                   patchFile = exprArg;
                   shouldBuild = true;
cout << "build with patch " << patchFile << endl;
               }
           }

           if (!shouldBuild) continue;
       }

        // extract port/version map
        auto slashpos = s.find("/");
        if (slashpos == string::npos)
        {
            cout << "bad port: " << s << endl;
            exit(1);
        }
        string portname = s.substr(0, slashpos);
        string portversion = s.substr(slashpos + 1);

	// extract featurePackage
	string featurePackage;
	auto featurePackagePos = portname.find("[");
	if (featurePackagePos != string::npos)
	{
 		featurePackage = portname.substr(featurePackagePos);
		portname = portname.substr(0, featurePackagePos);
	}

        auto existing = ports.find(portname);
        if (existing != ports.end() && existing->second != portversion)
        {
            cout << "conflicting port versions: " << portname << " " << existing->second << " " << portversion << endl;
            return 1;
        }
        ports[portname] = portversion;
        featurePackages[portname] = featurePackage;

        if (build) continue;

        if (!patchFile.empty())
        {
            auto existingPatch = patches.find(portname);
            if (existingPatch != patches.end() && existingPatch->second != patchFile)
            {
                cout << "Conflicting patch files: " << patchFile << " and " << existingPatch->second << " for " << portname << "\n";
                return 1;
            }
            if (!fs::exists(patchPath / patchFile))
            {
                cout << "Nonexistent patch " << patchFile << " for " << portname << ", patches must be in " << patchPath.u8string() << "\n";
            }
            cout << "Got patch " << patchFile << " for " << portname << "\n";
            patches[portname] = patchPath / patchFile;
        }
    }

    return true;
}
