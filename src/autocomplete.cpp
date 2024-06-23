/**
* @file autocomplete.cpp
* @brief Win32 console I/O autocomplete support
*
* (c) 2013-2018 by Mega Limited, Auckland, New Zealand
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

// autocomplete to support platforms without readline (and complement readline where it is available)

#include <mega/autocomplete.h>
#include <mega/megaclient.h>
#include <cassert>
#include <algorithm>

#if !defined(__MINGW32__) && !defined(__ANDROID__) && (!defined(__GNUC__) || (__GNUC__*100+__GNUC_MINOR__) >= 503)
    #define HAVE_FILESYSTEM
    #include <filesystem>
    namespace fs = std::filesystem;
#endif

namespace mega {
namespace autocomplete {
using namespace std;


inline static bool icmp(char a, char b)
{
    return(toupper(a) == toupper(b));
}

ACState::quoting::quoting()
{
}

ACState::quoting::quoting(std::string& s)
{
    quoted = (!s.empty() && s[0] == '\"') || (s[0] == '\'');
    if (quoted)
    {
        quote_char = s[0];
        s.erase(0, 1);
        if (!s.empty() && s.back() == quote_char)
        {
            s.pop_back();
        }
    }
}

void ACState::quoting::applyQuotes(std::string& w)
{
    if (quoted && quote_char != 0)
    {
        // reapply quotes as the user had them
        w.reserve(w.size() + 2);
        w.insert(0, 1, quote_char);
        w.push_back(quote_char);
    }
    else
    {
        // add quotes if it has a space in it now and doesn't already start with a quote
        if (w.find(' ') != std::string::npos)
        {
            w = "\"" + w + "\"";
        }
    }
}

ACState::quoted_word::quoted_word()
{
}

ACState::quoted_word::quoted_word(const std::string &str)
    : s(str), q(s)
{
}

ACState::quoted_word::quoted_word(const std::string &str, const quoting& quot)
    : s (str), q(quot)
{
}

string ACState::quoted_word::getQuoted()
{
    string qs = s;
    q.applyQuotes(qs);
    return qs;
}

bool ACState::extractflag(const string& flag)
{
    for (auto i = words.begin(); i != words.end(); ++i)
    {
        if (i->s == flag && !i->q.quoted)
        {
            words.erase(i);
            return true;
        }
    }
    return false;
}

bool ACState::extractflagparam(const string& flag, string& param)
{
    for (auto i = words.begin(); i != words.end(); ++i)
    {
        if (i->s == flag)
        {
            auto j = i;
            ++j;
            if (j != words.end())
            {
                param = j->s;
                words.erase(i, ++j);
                return true;
            }
        }
    }
    return false;
}

void ACState::addCompletion(const std::string& s, bool caseInsensitive, bool couldextend)
{
    // add if it matches the prefix. Doing the check here keeps subclasses simple
    assert(atCursor());
    const std::string& prefix = word().s;
    if (!s.empty() && s.size() >= prefix.size())
    {
        bool equal;
        if (caseInsensitive)
        {
            equal = std::equal(prefix.begin(), prefix.end(), s.begin(), icmp);
        }
        else
        {
            equal = s.compare(0, prefix.size(), prefix) == 0;
        }

        if (equal)
        {
            // also only offer options when the user starts with "-", and not otherwise.
            if ((s[0] == '-' && !prefix.empty() && prefix[0] == '-') ||
                (s[0] != '-' && (prefix.empty() || prefix[0] != '-')))
            {
                completions.emplace_back(s, caseInsensitive, couldextend);
            }
        }
    }
}

void ACState::addPathCompletion(std::string&& f, const std::string& relativeRootPath, bool isFolder, char dir_sep, bool caseInsensitive)
{
    if (f.size() > relativeRootPath.size() && f.compare(0, relativeRootPath.size(), relativeRootPath) == 0)
    {
        f.erase(0, relativeRootPath.size());
    }
    if (dir_sep != '\\')
    {
        string from = "\\";
        string to(1,dir_sep);
        size_t start_pos = 0;
        while (( start_pos = f.find(from, start_pos)) != std::string::npos)
        {
            f.replace(start_pos, from.length(), to);
            start_pos += to.length();
        }
    }

    if (unixStyle && isFolder)
    {
        f.push_back(dir_sep);
    }
    addCompletion(f, caseInsensitive, isFolder);
}

std::ostream& operator<<(std::ostream& s, const ACNode& n)
{
    return n.describe(s);
}

ACNode::~ACNode()
{
}

Optional::Optional(ACN n)
    : subnode(n)
{
}

bool Optional::addCompletions(ACState& s)
{
    subnode->addCompletions(s);
    return s.i == s.words.size();
}

bool Optional::match(ACState& s) const
{
    auto i = s.i;
    if (!subnode->match(s))
        s.i = i;
    return true;
}

std::ostream& Optional::describe(std::ostream& s) const
{
    if (auto e = dynamic_cast<Either*>(subnode.get()))
    {
        std::ostringstream s2;
        s2 << *e;
        std::string str = s2.str();
        if (str.size() >= 2 && str.front() == '(' && str.back() == ')')
        {
            str.pop_back();
            str.erase(0, 1);
        }
        return s << "[" << str << "]";
    }
    else
    {
        return s << "[" << *subnode << "]";
    }
}

Repeat::Repeat(ACN n)
    : subnode(n)
{
}

bool Repeat::addCompletions(ACState& s)
{
    unsigned n = s.i;
    while (s.i < s.words.size() && !subnode->addCompletions(s))
    {
        if (s.i <= n) // not advancing
            break;
        n = s.i;
    }
    return s.i >= s.words.size();
}

bool Repeat::match(ACState& s) const
{
    for (;;)
    {
        if (s.i >= s.words.size())
            break;

        auto i = s.i;
        if (!subnode->match(s))
        {
            s.i = i;
            break;
        }
    }
    return true;
}

std::ostream& Repeat::describe(std::ostream& s) const
{
    return s << *subnode << "*";
}

Sequence::Sequence(ACN n1, ACN n2)
    : current(n1), next(n2)
{
}

bool Sequence::addCompletions(ACState& s)
{
    if (current->addCompletions(s))
    {
        return true;
    }
    bool stop = s.i < s.words.size() ? next->addCompletions(s) : true;
    return stop;
}


bool Sequence::match(ACState& s) const
{
    return current->match(s) && next->match(s);
}

std::ostream& Sequence::describe(std::ostream& s) const
{
    return s << *current << " " << *next;
}

Text::Text(const std::string& s, bool isParam)
    : exactText(s)
    , param(isParam)
{
    assert(!exactText.empty() && exactText[0] != '-');
}

bool Text::addCompletions(ACState& s)
{
    if (s.atCursor())
    {
        s.addCompletion(param ? "<" + exactText + ">" : exactText);
        return true;
    }
    else
    {
        bool matches = param ? (!s.word().s.empty() && (s.word().s[0] != '-' || s.word().q.quoted)) : (s.word().s == exactText);
        s.i += matches ? 1 : 0;
        return !matches;
    }
}

bool Text::match(ACState& s) const
{
    if (s.i < s.words.size() && (param ? !s.word().s.empty() && (s.word().s[0] != '-' || s.word().q.quoted) : s.word().s == exactText))
    {
        s.i += 1;
        return true;
    }
    return false;
}

std::ostream& Text::describe(std::ostream& s) const
{
    return s << (param ? "<" + exactText + ">" : exactText);
}

bool ExportedLink::isLink(const string& s, bool file, bool folder)
{
    bool filestr = (s.find("https://mega.nz/#!") != string::npos || s.find("https://mega.nz/file/") != string::npos) ||
                   (s.find("https://mega.co.nz/#!") != string::npos || s.find("https://mega.co.nz/file/") != string::npos);
    bool folderstr = (s.find("https://mega.nz/#F!") != string::npos || s.find("https://mega.nz/folder/") != string::npos) ||
                     (s.find("https://mega.co.nz/#F!") != string::npos || s.find("https://mega.co.nz/folder/") != string::npos);

    if (file && !folder)
    {
        return filestr;
    }
    else if (!file && folder)
    {
        return folderstr;
    }
    return filestr || folderstr;
}

ExportedLink::ExportedLink(bool file, bool folder)
    : filelink(file), folderlink(folder)
{
}

bool ExportedLink::addCompletions(ACState& s)
{
    if (s.atCursor())
    {
        if (filelink && !folderlink)
        {
            s.addCompletion("<exportedfilelink#key>");
        }
        else if (!filelink && folderlink)
        {
            s.addCompletion("<exportedfolderlink#key>");
        }
        else
        {
            s.addCompletion("<exportedlink#key>");
        }
        return true;
    }
    else
    {
        bool matches = !s.word().s.empty() && s.word().s[0] != '-' && isLink(s.word().s, filelink, folderlink);
        s.i += matches ? 1 : 0;
        return !matches;
    }
}

bool ExportedLink::match(ACState& s) const
{
    if (s.i < s.words.size() && (!s.word().s.empty() && s.word().s[0] != '-' && isLink(s.word().s, filelink, folderlink)))
    {
        s.i += 1;
        return true;
    }
    return false;
}

std::ostream& ExportedLink::describe(std::ostream& s) const
{
    if (filelink && !folderlink)
    {
        return s << "<exportedfilelink#key>";
    }
    else if (!filelink && folderlink)
    {
        return s << "<exportedfolderlink#key>";
    }
    else
    {
        return s << "<exportedlink#key>";
    }
}

Flag::Flag(const std::string& s)
    : flagText(s)
{
    assert(!flagText.empty() && flagText[0] == '-');
}

bool Flag::addCompletions(ACState& s)
{
    if (s.atCursor())
    {
        // only offer flag completions if the user requests it with "-"
        if (!s.word().s.empty() && s.word().s[0] == '-')
        {
            s.addCompletion(flagText);
        }
        return true;
    }
    else
    {
        bool matches = s.word().s == flagText;
        s.i += matches ? 1 : 0;
        return !matches;
    }
}

bool Flag::match(ACState& s) const
{
    if (s.i < s.words.size() && s.word().s == flagText)
    {
        s.i += 1;
        return true;
    }
    return false;
}

std::ostream& Flag::describe(std::ostream& s) const
{
    return s << flagText;
}

Either::Either(const std::string& prefix)
    : describePrefix(prefix)
{
}

void Either::Add(ACN n)
{
    if (n)
    {
        eithers.push_back(n);
        execFuncs.push_back(nullptr);
    }
}

void Either::Add(ExecFn f, ACN n)
{
    if (n)
    {
        eithers.push_back(n);
        execFuncs.push_back(f);
    }
}

bool Either::addCompletions(ACState& s)
{
    bool stop = true;
    int n = s.i;
    int best_s_i = s.i;
    for (auto& p : eithers)
    {
        s.i = n;
        if (!p->addCompletions(s))
        {
            stop = false;
            best_s_i = std::max<int>(s.i, best_s_i);
        }
    }
    s.i = best_s_i;
    return stop;
}

bool Either::match(ACState& s) const
{
    auto i = s.i;
    for (auto e : eithers)
    {
        s.i = i;
        if (e->match(s))
            return true;  // todo: address possible ambiguities
    }
    return false;
}


std::ostream& Either::describe(std::ostream& s) const
{
    if (!describePrefix.empty())
    {
        for (unsigned i = 0; i < eithers.size(); ++i)
        {
            s << describePrefix << *eithers[i] << std::endl;
        }
    }
    else
    {
        std::ostringstream s2;
        for (int i = 0; i < int(eithers.size() * 2) - 1; ++i)
        {
            (i & 1 ? s2 << "|" : s2 << *eithers[i / 2]);
        }
        std::string str = s2.str();
        if (str.find(' ') == std::string::npos)
        {
            s << str;
        }
        else
        {
            s << "(" << str << ")";
        }
    }
    return s;
}

WholeNumber::WholeNumber(const std::string& description, size_t defaultValue)
  : defaultvalue(defaultValue)
  , description(description)
{
}

bool WholeNumber::addCompletions(ACState& s)
{
    if (s.atCursor())
    {
        s.addCompletion(to_string(defaultvalue));
        return true;
    }
    else
    {
        for (char c : s.word().s)
        {
            if (!is_digit(c))
            {
                return true;
            }
        }
        s.i += 1;
    }
    return false;
}


bool WholeNumber::match(ACState& s) const
{
    if (s.i < s.words.size())
    {
        for (char c : s.word().s)
        {
            if (!is_digit(c))
            {
                return false;
            }
        }
        s.i += 1;
        return true;
    }
    return false;
}


std::ostream& WholeNumber::describe(std::ostream& s) const
{
    return s << description;
}


LocalFS::LocalFS(bool files, bool folders, const std::string descriptionPrefix)
    : reportFiles(files)
    , reportFolders(folders)
    , descPref(descriptionPrefix)
{
}

bool LocalFS::addCompletions(ACState& s)
{
    if (s.atCursor())
    {
#ifdef HAVE_FILESYSTEM
        fs::path searchPath = fs::u8path(s.word().s + (s.word().s.empty() || (s.word().s.back() == '\\'  || s.word().s.back() == '/' ) ? "*" : ""));
#ifdef WIN32
        char sep = (!s.word().s.empty() && s.word().s.find('/') != string::npos ) ?'/':'\\';
#else
        char sep = '/';
#endif
        bool relative = !searchPath.is_absolute();
        searchPath = relative ? (fs::current_path() /= searchPath) : searchPath;
        std::string cp = relative ? fs::current_path().u8string() + sep : "";
        if ((searchPath.filename() == ".." || searchPath.filename() == ".") && fs::exists(searchPath))
        {
            s.addPathCompletion(searchPath.u8string(), cp, true, sep, true);
        }
        else
        {
            searchPath.remove_filename(); // iterate the whole directory, and filter
#ifdef WIN32
            std::string spath = searchPath.u8string();
            if (spath.back() == ':')
            {
                searchPath = spath.append("\\");
            }
#endif

            if (fs::exists(searchPath) && fs::is_directory(searchPath))
            {
                for (fs::directory_iterator iter(searchPath); iter != fs::directory_iterator(); ++iter)
                {
                    if ((reportFolders && fs::is_directory(iter->status())) ||
                        (reportFiles && fs::is_regular_file(iter->status())))
                    {
                        s.addPathCompletion(iter->path().u8string(), cp, fs::is_directory(iter->status()), sep, true);
                    }
                }
            }
        }
#else
// todo: implement local directory listing for any platforms without std::filsystem, if it turns out to be needed
#endif
        return true;
    }
    else
    {
        // don't let an option be misinterpreted as a filename.  Files beginning with a '-' will need to be quoted
        bool stop = s.i >= s.words.size() || s.word().s.empty() || s.word().s.at(0) == '-';
        s.i += stop ? 0 : 1;
        return stop;
    }
}

bool LocalFS::match(ACState& s) const
{
    if (s.i < s.words.size())
    {
        if (!s.word().s.empty() && s.word().s[0] != '-')
        {
            s.i += 1;
            return true;
        }
    }
    return false;
}

std::ostream& LocalFS::describe(std::ostream& s) const
{
    return s << descPref << (descPref.size() < 10 ? (reportFiles ? (reportFolders ? "localpath" : "localfile") : "localfolder") : "");
}

MegaFS::MegaFS(bool files, bool folders, MegaClient* c, ::mega::NodeHandle* curDirHandle, const std::string descriptionPrefix, ::mega::NodeHandle* prevDirHandle)
    : client(c)
    , cwd(curDirHandle)
    , previous_cwd(prevDirHandle)
    , reportFiles(files)
    , reportFolders(folders)
    , descPref(descriptionPrefix)
{
}

shared_ptr<Node> addShareRootCompletions(ACState& s, MegaClient* client, string& pathprefix)
{
    const string& path = s.word().s;
    string::size_type t = path.find_first_of(":/");

    if (t == string::npos || path[t] == ':')
    {
        for (const user_map::value_type& u : client->users)
        {
            if (t == string::npos && !u.second.sharing.empty())
            {
                string str;
                s.addCompletion(u.second.email + ":", true, true);
            }
            else if (u.second.email == path.substr(0, t))
            {
                string::size_type pos = path.find_first_of("/", t + 1);
                for (handle h : u.second.sharing)
                {
                    if (shared_ptr<Node> n = client->nodebyhandle(h))
                    {
                        if (pos == string::npos)
                        {
                            s.addPathCompletion(path.substr(0, t + 1) + n->displayname(), "", n->type != FILENODE, '/', false);
                        }
                        else if (n->displayname() == path.substr(t + 1, pos - t - 1))
                        {
                            pathprefix = path.substr(0, pos + 1);
                            return n;
                        }
                    }
                }
            }
        }
    }
    return NULL;
}

bool MegaFS::addCompletions(ACState& s)
{
    if (s.atCursor())
    {
        if (client && cwd)
        {
            shared_ptr<Node> n;
            std::string pathprefix;
            if (!s.word().s.empty() && s.word().s[0] == '/')
            {
                if (s.word().s.size() >= 2 && s.word().s[1] == '/')
                {
                    if (s.word().s.size() >= 5 && !strncmp(s.word().s.c_str(), "//in/", 5))
                    {
                        pathprefix = "//in/";
                        n = client->nodeByHandle(client->mNodeManager.getRootNodeVault());
                    }
                    else if (s.word().s.size() >= 6 && !strncmp(s.word().s.c_str(), "//bin/", 6))
                    {
                        pathprefix = "//bin/";
                        n = client->nodeByHandle(client->mNodeManager.getRootNodeRubbish());
                    }
                    else
                    {
                        s.addPathCompletion(string("//bin"), "", true, '/', false);
                        s.addPathCompletion(string("//in"), "", true, '/', false);
                        return true;
                    }
                }
                else
                {
                    pathprefix = "/";
                    n = client->nodeByHandle(client->mNodeManager.getRootNodeFiles());
                }
            }
            else
            {
                n = addShareRootCompletions(s, client, pathprefix);

                if (!n && *cwd != UNDEF)
                {
                    n = client->nodeByHandle(*cwd);
                    pathprefix.clear();
                }
            }

            // drill down folders
            size_t sepPos = 0;
            while (n && std::string::npos != (sepPos = s.word().s.find('/', pathprefix.size())))
            {
                std::string folderName = s.word().s.substr(pathprefix.size(), sepPos - pathprefix.size());
                pathprefix += folderName + "/";
                if (folderName == ".")
                {
                }
                else if (folderName == "..")
                {
                    n = n->parent;
                }
                else if (folderName == "-" && previous_cwd != nullptr)
                {
                    n = client->nodeByHandle(*previous_cwd);
                }
                else
                {
                    shared_ptr<Node> nodematch = NULL;
                    for (auto& subnode : client->getChildren(n.get()))
                    {
                        if (subnode->type == FOLDERNODE && subnode->displayname() == folderName)
                        {
                            nodematch = subnode;
                            break;
                        }
                    }
                    n = nodematch;
                }
            }

            std::string leaf = s.word().s.substr(pathprefix.size(), std::string::npos);
            if (n && (leaf == "." || (leaf == ".." && n->type != ROOTNODE)))
            {
                s.addPathCompletion(string(s.word().s), "", true, '/', false);
            }
            else
            {
                // iterate specified folder
                if (n)
                {
                    for (auto& subnode : client->getChildren(n.get()))
                    {
                        if ((reportFolders && subnode->type == FOLDERNODE) ||
                            (reportFiles && subnode->type == FILENODE))
                        {
                            s.addPathCompletion(pathprefix + subnode->displayname(), "", subnode->type == FOLDERNODE, '/', false);
                        }
                    }
                }
            }
        }
        return true;
    }
    else
    {
        // don't let an option be misinterpreted as a filename.  Files beginning with a '-' will need to be quoted
        bool stop = s.word().s.empty() || s.word().s.at(0) == '-';
        s.i += stop ? 0 : 1;
        return stop;
    }
}

bool MegaFS::match(ACState& s) const
{
    if (s.i < s.words.size())
    {
        if (!s.word().s.empty() && (s.word().s[0] != '-' || s.word().s.size() == 1) && !ExportedLink::isLink(s.word().s, true, true))
        {
            s.i += 1;
            return true;
        }
    }
    return false;
}

std::ostream& MegaFS::describe(std::ostream& s) const
{
    return s << descPref << (reportFiles ? (reportFolders ? "remotepath" : "remotefile") : "remotefolder");
}

MegaContactEmail::MegaContactEmail(MegaClient* c)
    : client(c)
{
}

bool MegaContactEmail::addCompletions(ACState& s)
{
    if (s.atCursor())
    {
        if (client)
        {
            for (const user_map::value_type& u : client->users)
            {
                if (u.second.show == VISIBLE)
                {
                    s.addCompletion(u.second.email, true);
                }
            }
        }
        return true;
    }
    else
    {
        // don't let an option be misinterpreted as an email.  Emails beginning with a '-' (prob not legal anyway) will need to be quoted
        bool stop = s.word().s.empty() || s.word().s.at(0) == '-';
        s.i += stop ? 0 : 1;
        return stop;
    }
}

bool MegaContactEmail::match(ACState& s) const
{
    if (s.i < s.words.size())
    {
        if (!s.word().s.empty() && s.word().s[0] != '-')
        {
            s.i += 1;
            return true;
        }
    }
    return false;
}

#ifdef ENABLE_SYNC

BackupID::BackupID(MegaClient& client, bool onlyActive)
  : mClient(client)
  , mOnlyActive(onlyActive)
{
}

bool BackupID::addCompletions(ACState& state)
{
    auto ids = backupIDs();

    if (state.atCursor())
    {
        for (auto& id : filter(ids, state))
            state.addCompletion(std::move(id));

        return true;
    }

    return match(ids, state);
}

std::ostream& BackupID::describe(std::ostream& ostream) const
{
    return ostream << "BackupID";
}

string_vector& BackupID::filter(string_vector& ids, const ACState& state) const
{
    if (state.i >= state.words.size())
        return ids;

    auto& word = state.words.back();
    auto& prefix = word.s;

    if (prefix.empty())
        return ids;

    auto predicate = [&prefix](const string& id) {
        return prefix.size() > id.size()
               || id.compare(0, prefix.size(), prefix);
    };

    auto i = remove_if(ids.begin(), ids.end(), predicate);
    ids.erase(i, ids.end());

    return ids;
}

bool BackupID::match(ACState& state) const
{
    return state.i < state.words.size()
           && match(backupIDs(), state);
}

string_vector BackupID::backupIDs() const
{
    string_vector result;
    handle_set seen;

    for (auto& config : mClient.syncs.getConfigs(mOnlyActive))
    {
        if (seen.emplace(config.mBackupId).second)
            result.emplace_back(toHandle(config.mBackupId));
    }

    return result;
}

bool BackupID::match(const string_vector& ids, ACState& state) const
{
    auto& word = state.words[state.i];

    if (word.s.empty() || (!word.q.quoted && word.s[0] == '-'))
        return false;

    auto i = find(ids.begin(), ids.end(), word.s);

    if (i != ids.end())
        return ++state.i, true;

    return false;
}

ACN backupID(MegaClient& client, bool onlyActive)
{
    return make_shared<BackupID>(client, onlyActive);
}

#endif // ENABLE_SYNC

std::ostream& MegaContactEmail::describe(std::ostream& s) const
{
    return s << "<email>";
}


std::pair<int, int> identifyNextWord(const std::string& line, int startPos)
{
    const char* strStart = line.c_str();
    const char* ptr = strStart + startPos;

    while (*ptr > 0 && *ptr <= ' ')
    {
        ptr++;
    }

    std::pair<int, int> ret(int(ptr - strStart), int(ptr - strStart));

    if (!*ptr)
    {
        return ret;
    }

    // todo: yes the console recognises escapes on linux, but not on windows.  Should we match the platform we are executing on (up to a reasonable point - as the rules on windows are pretty messy!)?  https://www.microsoft.com/resources/documentation/windows/xp/all/proddocs/en-us/cmd.mspx?mfr=true

    if (*ptr == '"')
    {
        for (++ptr; *ptr; ++ptr)
        {
            if (*ptr == '"')
            {
                ++ptr;
                break;
            }
        }
    }
    else if (*ptr == '\'')
    {
        for (++ptr; *ptr; ++ptr)
        {
            if (*ptr == '\'')
            {
                ++ptr;
                break;
            }
        }
    }
    else
    {
        for (; *ptr; ++ptr)
        {
            if (*ptr == ' ' || (*ptr == '\"') || (*ptr == '\''))
            {
                break;
            }
        }
    }

    ret.second = int(ptr - strStart);
    return ret;
}

ACState prepACState(const std::string line, size_t insertPos, bool unixStyle)
{
    if (insertPos == std::string::npos)
    {
        insertPos = line.size();
    }

    // find where we're up to in the line and what syntax options are available at that point
    ACState acs;
    acs.unixStyle = unixStyle;
    std::pair<int, int> linepos{ 0,0 };
    bool last;
    do
    {
        linepos = identifyNextWord(line, linepos.second);
        std::string word = line.substr(linepos.first, linepos.second - linepos.first);
        last = linepos.first == linepos.second;
        bool cursorInWord = linepos.first <= int(insertPos) && int(insertPos) <= linepos.second;
        if (cursorInWord)
        {
            last = true;
            word.erase(insertPos - linepos.first, std::string::npos);
            linepos.second = int(insertPos);  // keep everything to the right of the cursor
        }
        if (!acs.words.empty() && linepos.first == acs.wordPos.back().second)
        {
            // continuation, so combine into one word. eg "c:\prog files"\nextthing
            ACState::quoting q(word);
            acs.words.back().s += word;
            acs.wordPos.back().second = linepos.second;
            if (!acs.words.back().q.quoted)
            {
                acs.words.back().q = q;
            }
        }
        else
        {
            acs.wordPos.push_back(linepos);
            acs.words.emplace_back(word);
        }
    } while (!last);

    return acs;
}

CompletionState autoComplete(const std::string line, size_t insertPos, ACN syntax, bool unixStyle)
{
    ACState acs = prepACState(line, insertPos, unixStyle);

    acs.i = 0;
    syntax->addCompletions(acs);

    CompletionState cs;
    cs.line = line;
    cs.wordPos = acs.wordPos.back();
    cs.originalWord = acs.words.back();
    cs.completions = acs.completions;
    cs.unixStyle = acs.unixStyle;
    cs.tidyCompletions();

    return cs;
}

bool autoExec(const std::string line, size_t insertPos, ACN syntax, bool unixStyle, string& consoleOutput, bool reportNoMatch)
{
    ACState acs = prepACState(line, insertPos, unixStyle);

    while (!acs.words.empty() && acs.words.back().s.empty() && !acs.words.back().q.quoted)
    {
        acs.words.pop_back();
    }

    if (!acs.words.empty())
    {
        if (auto e = dynamic_cast<autocomplete::Either*>(syntax.get()))
        {
            std::vector<ACN> v;
            Either::ExecFn f;
            std::vector<ACN> firstWordMatches;
            std::ostringstream conout;
            for (unsigned i = 0; i < e->eithers.size(); ++i)
            {
                acs.i = 0;
                if (e->eithers[i]->match(acs) && acs.i == acs.words.size())
                {
                    v.push_back(e->eithers[i]);
                    f = e->execFuncs[i];
                }
                acs.i = 0;
                if (auto seq = dynamic_cast<Sequence*>(e->eithers[i].get()))
                {
                    if (seq->current->match(acs))
                    {
                        firstWordMatches.push_back(e->eithers[i]);
                    }
                }
            }
            if (v.empty())
            {
                if (!reportNoMatch)
                {
                    return false;
                }
                conout << "Invalid syntax";
                if (firstWordMatches.empty())
                {
                    conout << ", type 'help' for command syntax" << std::endl;
                }
                else
                {
                    for (auto fwm : firstWordMatches)
                    {
                        conout << std::endl << e->describePrefix << *fwm << endl;
                    }
                }
            }
            else if (v.size() == 1)
            {
                acs.i = 0;
                if (f)
                {
                    f(acs);
                }
                else if (!reportNoMatch)
                {
                    return false;
                }
                else
                {
                    conout << "Operation not implemented yet" << std::endl;
                }
            }
            else
            {
                conout << "Ambiguous syntax" << std::endl;
                for (auto a : v)
                {
                    conout << e->describePrefix << *a << std::endl;
                }
            }
            consoleOutput = conout.str();
        }
    }
    return true;
}

unsigned utf8GlyphCount(const string &str)
{
    int c, i, ix, q;
    for (q = 0, i = 0, ix = int(str.length()); i < ix; i++, q++)
    {
        c = (unsigned char)str[i];

        if (c >= 0 && c <= 127) i += 0;
        else if ((c & 0xE0) == 0xC0) i += 1;
        else if ((c & 0xF0) == 0xE0) { i += 2; q++; } //these glyphs may occupy 2 characters! Problem: not always. Let's assume the worst
        else if ((c & 0xF8) == 0xF0) i += 3;
        else q++; // invalid utf8 - leave lots of space
    }
    return q;
}

const string& CompletionState::unixColumnEntry(int row, int col, int rows)
{
    static string emptyString;
    size_t index = unixListCount + col * rows + row;
    return index < completions.size() ? completions[index].s : emptyString;
}

unsigned CompletionState::calcUnixColumnWidthInGlyphs(int col, int rows)
{
    unsigned width = 0;
    for (int r = 0; r < rows; ++r)
    {
        width = std::max<unsigned>(width, utf8GlyphCount(unixColumnEntry(r, col, rows)));
    }
    return width;
}

void CompletionState::tidyCompletions()
{
    // sort and eliminate duplicates
    std::sort(completions.begin(), completions.end(), [](const ACState::Completion& c1, const ACState::Completion& c2) { return c1.s < c2.s; });
    completions.erase(std::unique(completions.begin(), completions.end(), [](const ACState::Completion& c1, const ACState::Completion& c2) { return c1.s == c2.s; }), completions.end());
}

void applyCompletion(CompletionState& s, bool forwards, unsigned consoleWidth, CompletionTextOut& textOut)
{
    if (!s.completions.empty())
    {
        if (!s.unixStyle)
        {
            int index = ((!forwards && s.lastAppliedIndex == -1) ? -1 : (s.lastAppliedIndex + (forwards ? 1 : -1))) + (int)s.completions.size();
            index = static_cast<int>(index % s.completions.size());

            // restore quotes if it had them already
            auto& c = s.completions[index];
            std::string w = c.s;
            s.originalWord.q.applyQuotes(w);
            w += (s.completions.size() == 1 && !c.couldExtend) ? " " : "";
            s.line.replace(s.wordPos.first, s.wordPos.second - s.wordPos.first, w);
            s.wordPos.second = int(w.size() + s.wordPos.first);
            s.lastAppliedIndex = index;

            if (s.completions.size()==1)
            {
                s.active = false;
            }
        }
        else
        {
            if (!s.firstPressDone)
            {
                // add characters that match all possibilities
                std::string exactChars = s.completions[0].s;
                // keep the uppercase/lowercase as specified by the user (for case sensitive they will match anyway)
                size_t commonLen = std::min<size_t>(exactChars.size(), s.originalWord.s.size());
                exactChars.replace(0, commonLen, s.originalWord.s.substr(0, commonLen));
                for (auto& c : s.completions)
                {
                    for (unsigned i = 0; i < exactChars.size(); ++i)
                    {
                        if (i == c.s.size() || (c.caseInsensitive ? !icmp(exactChars[i], c.s[i]) : exactChars[i] != c.s[i]))
                        {
                            exactChars.erase(i, std::string::npos);
                            break;
                        }
                    }
                }
                s.originalWord.q.applyQuotes(exactChars);
                exactChars += (s.completions.size() == 1 && !s.completions[0].couldExtend) ? " " : "";
                s.line.replace(s.wordPos.first, s.wordPos.second - s.wordPos.first, exactChars);
                s.wordPos.second = int(exactChars.size() + s.wordPos.first);
                s.firstPressDone = true;
                s.unixListCount = 0;
                if (s.completions.size() == 1)
                {
                    s.active = false;
                }
            }
            else
            {
                // show remaining possibilities.  Proper columns, unix order, alphabetical vertically then left-to-right
                unsigned rows = 1, cols = 0, sumwidth = 0;
                for (unsigned c = 0; ;)
                {
                    unsigned width = s.calcUnixColumnWidthInGlyphs(c, rows);
                    if (width == 0)
                    {
                        cols = c;
                        break;
                    }
                    else
                    {
                        sumwidth += width + 3;
                        if (3 + sumwidth > consoleWidth)
                        {
                            if (rows == 5)
                            {
                                cols = c;
                                break;
                            }
                            else
                            {
                                ++rows;
                                c = 0;
                                sumwidth = 0;
                            }
                        }
                        else if (s.unixListCount + rows * (c + 1) >= s.completions.size())
                        {
                            cols = c + 1;
                            break;
                        }
                        else
                        {
                            ++c;
                        }
                    }
                }

                rows = std::max<int>(rows, 1);
                cols = std::max<int>(cols, 1);
                for (unsigned c = 0; c < cols; ++c)
                {
                    textOut.columnwidths.push_back(s.calcUnixColumnWidthInGlyphs(c, rows) + (c == 0 ? 6 : 3));
                }
                for (unsigned r = 0; r < rows; ++r)
                {
                    textOut.stringgrid.push_back(vector<string>());
                    for (unsigned c = 0; c < cols; ++c)
                    {
                        const string& entry = s.unixColumnEntry(r, c, rows);
                        if (!entry.empty())
                        {
                            textOut.stringgrid[r].push_back((c == 0 ? "   " : "") + entry);
                        }
                    }
                }

                s.unixListCount += rows * cols;
                if (s.unixListCount < s.completions.size())
                {
                    textOut.stringgrid.push_back(vector<string>(1, "<press again for more>"));
                }
                else
                {
                    s.unixListCount = 0;
                    s.firstPressDone = false;
                }
            }
        }
    }
}

ACN either(ACN n1, ACN n2, ACN n3, ACN n4, ACN n5, ACN n6, ACN n7, ACN n8, ACN n9, ACN n10, ACN n11, ACN n12, ACN n13)
{
    auto n = std::make_shared<Either>();
    n->Add(n1);
    n->Add(n2);
    n->Add(n3);
    n->Add(n4);
    n->Add(n5);
    n->Add(n6);
    n->Add(n7);
    n->Add(n8);
    n->Add(n9);
    n->Add(n10);
    n->Add(n11);
    n->Add(n12);
    n->Add(n13);
    return n;
}

static ACN sequenceBuilder(ACN n1, ACN n2)
{
    return n2 ? std::make_shared<Sequence>(n1, n2) : n1;
}

ACN sequence(ACN n1, ACN n2, ACN n3, ACN n4, ACN n5, ACN n6, ACN n7, ACN n8, ACN n9, ACN n10)
{
    return sequenceBuilder(n1, sequenceBuilder(n2, sequenceBuilder(n3, sequenceBuilder(n4, sequenceBuilder(n5, sequenceBuilder(n6, sequenceBuilder(n7, sequenceBuilder(n8, sequenceBuilder(n9, n10)))))))));
}

ACN text(const std::string s)
{
    return std::make_shared<Text>(s, false);
}

ACN param(const std::string s)
{
    return std::make_shared<Text>(s, true);
}

ACN exportedLink(bool file, bool folder)
{
    return std::make_shared<ExportedLink>(file, folder);
}

ACN flag(const std::string s)
{
    return std::make_shared<Flag>(s);
}

ACN opt(ACN n)
{
    return std::make_shared<Optional>(n);
}

ACN repeat(ACN n)
{
    return std::make_shared<Repeat>(n);
}


ACN wholenumber(const std::string& description, size_t defaultValue)
{
    return std::make_shared<WholeNumber>(description, defaultValue);
}

ACN wholenumber(size_t defaultValue)
{
    return wholenumber("N", defaultValue);
}

ACN localFSPath(const std::string descriptionPrefix)
{
    return ACN(new LocalFS(true, true, descriptionPrefix));
}

ACN localFSFile(const std::string descriptionPrefix)
{
    return ACN(new LocalFS(true, false, descriptionPrefix));
}

ACN localFSFolder(const std::string descriptionPrefix)
{
    return ACN(new LocalFS(false, true, descriptionPrefix));
}

ACN remoteFSPath(MegaClient* client, ::mega::NodeHandle* cwd, const std::string descriptionPrefix)
{
    return ACN(new MegaFS(true, true, client, cwd, descriptionPrefix));
}

ACN remoteFSFile(MegaClient* client, ::mega::NodeHandle* cwd, const std::string descriptionPrefix)
{
    return ACN(new MegaFS(true, false, client, cwd, descriptionPrefix));
}

ACN remoteFSFolder(MegaClient* client, ::mega::NodeHandle* cwd, const std::string descriptionPrefix, ::mega::NodeHandle* previous_cwd)
{
    return ACN(new MegaFS(false, true, client, cwd, descriptionPrefix, previous_cwd));
}

ACN contactEmail(MegaClient* client)
{
    return ACN(new MegaContactEmail(client));
}

}}; //namespaces

