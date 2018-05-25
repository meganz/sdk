/**
* @file win32/autocomplete.cpp
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

#include <mega/win32/autocomplete.h>
#include <mega/megaclient.h>
#include <cassert>
#include <filesystem>
#include <algorithm>

namespace fs = std::experimental::filesystem;

namespace mega {
namespace autocomplete {

template<class T>
static T clamp(T v, T lo, T hi)
{
    // todo: switch to c++17 std version when we can
    if (v < lo)
    {
        return lo;
    }
    else if (v > hi)
    {
        return hi;
    }
    else
    {
        return v;
    }
}

inline static bool icmp(char a, char b)
{
    return(toupper(a) == toupper(b));
}

ACState::quoting::quoting()
{
}

ACState::quoting::quoting(std::string& s)
{
    quoted = !s.empty() && s[0] == '\"' || s[0] == '\'';
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

void ACState::addCompletion(const std::string& s, bool caseInsensitive) 
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
                completions.emplace_back(ACState::Completion{ s, caseInsensitive });
            }
        }
    }
}

void ACState::addPathCompletion(std::string& f, const std::string& relativeRootPath, bool isFolder, char dir_sep, bool caseInsensitive)
{
    if (f.size() > relativeRootPath.size() && f.compare(0, relativeRootPath.size(), relativeRootPath) == 0)
    {
        f.erase(0, relativeRootPath.size());
    }
    if (unixStyle && isFolder)
    {
        f.push_back(dir_sep);
    }
    addCompletion(f, caseInsensitive);
}



std::ostream& operator<<(std::ostream& s, const ACNode& n)
{
    return n.describe(s);
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
        s2 << *subnode;
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
        bool matches = param ? (!s.word().s.empty() && s.word().s[0] != '-') : (s.word().s == exactText);
        s.i += matches ? 1 : 0;
        return !matches;
    }
}

bool Text::match(ACState& s) const
{
    if (s.i < s.words.size() && (param ? !s.word().s.empty() && s.word().s[0] != '-' : s.word().s == exactText))
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

WholeNumber::WholeNumber(size_t def_val)
    : defaultvalue(def_val)
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
            if (!isdigit(c))
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
            if (!isdigit(c))
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
    return s << "N";
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
        fs::path searchPath(s.word().s + (s.word().s.empty() || s.word().s.back() == '\\' ? "*" : ""));
        bool relative = !searchPath.is_absolute();
        searchPath = relative ? fs::current_path() / searchPath : searchPath;
        std::string cp = relative ? fs::current_path().u8string() + "\\" : "";
        if ((searchPath.filename() == ".." || searchPath.filename() == ".") && fs::exists(searchPath))
        {
            s.addPathCompletion(searchPath.u8string(), cp, true, '\\', true);
        }
        else
        {
            searchPath.remove_filename(); // iterate the whole directory, and filter
            for (fs::directory_iterator iter(searchPath); iter != fs::directory_iterator(); ++iter)
            {
                if (reportFolders && fs::is_directory(iter->status()) ||
                    reportFiles && fs::is_regular_file(iter->status()))
                {
                    s.addPathCompletion(iter->path().u8string(), cp, fs::is_directory(iter->status()), '\\', true);
                }
            }
        }
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

MegaFS::MegaFS(bool files, bool folders, MegaClient* c, ::mega::handle* curDirHandle, const std::string descriptionPrefix)
    : reportFiles(files)
    , reportFolders(folders)
    , client(c)
    , cwd(curDirHandle)
    , descPref(descriptionPrefix)
{
}

bool MegaFS::addCompletions(ACState& s)
{ 
    if (s.atCursor())
    {
        if (client && cwd)
        {
            Node* n = NULL;
            std::string pathprefix;
            if (!s.word().s.empty() && s.word().s[0] == '/')
            {
                pathprefix += "/";
                n = client->nodebyhandle(client->rootnodes[0]);
            }
            else if (*cwd != UNDEF)
            {
                n = client->nodebyhandle(*cwd);
            }

            // drill down folders
            size_t sepPos = 0;
            while (n && std::string::npos != (sepPos = s.word().s.find('/', pathprefix.size())))
            {
                std::string folderName = s.word().s.substr(pathprefix.size(), sepPos - pathprefix.size());
                pathprefix += folderName + "/";
                if (folderName == ".")
                {
                    n = n;
                }
                else if (folderName == "..")
                {
                    n = n->parent;
                }
                else
                {
                    Node* nodematch = NULL;
                    for (Node* subnode : n->children)
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
                std::string f = s.word().s;
                s.addPathCompletion(f, "", true, '/', false);
            }
            else
            {
                // iterate specified folder
                if (n)
                {
                    for (Node* subnode : n->children)
                    {
                        if (reportFolders && subnode->type == FOLDERNODE ||
                            reportFiles && subnode->type == FILENODE)
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
        if (!s.word().s.empty() && s.word().s[0] != '-')
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

ACState prepACState(const std::string line, size_t insertPos, ACN syntax, bool unixStyle)
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
    ACState acs = prepACState(line, insertPos, syntax, unixStyle);

    acs.i = 0;
    syntax->addCompletions(acs);

    return CompletionState{ line, acs.wordPos.back(), acs.words.back(), acs.completions, unixStyle };
}

void autoExec(const std::string line, size_t insertPos, ACN syntax, bool unixStyle)
{
    ACState acs = prepACState(line, insertPos, syntax, unixStyle);

    if (!acs.words.empty() && (acs.words[0].s.size() || acs.words.size() > 1))
    {
        if (auto e = dynamic_cast<autocomplete::Either*>(syntax.get()))
        {
            std::vector<ACN> v;
            Either::ExecFn f;
            std::vector<ACN> firstWordMatches;
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
                        firstWordMatches.push_back(e->eithers[i]);
                }
            }
            if (v.empty())
            {
                std::cout << "Invalid syntax";
                if (firstWordMatches.empty())
                {
                    std::cout << ", type 'help' for command syntax" << std::endl;
                }
                else
                {
                    for (auto fwm : firstWordMatches)
                        std::cout << std::endl << e->describePrefix << *fwm << endl;
                }
            }
            else if (v.size() == 1)
            {
                acs.i = 0;
                if (f)
                {
                    f(acs);
                }
                else
                {
                    std::cout << "Operation not implemented yet" << std::endl;
                }
            }
            else
            {
                std::cout << "Ambiguous syntax" << std::endl;
                for (auto a : v)
                {
                    std::cout << e->describePrefix << *a << std::endl;
                }
            }
        }
    }
}


static size_t utf8strlen(const std::string s)
{
    size_t len = 0;
    for (char c : s)
    {
        if ((c & 0xC0) != 0x80)
        {
            len += 1;
        }
    }
    return len;
}

void applyCompletion(CompletionState& s, bool forwards, unsigned consoleWidth)
{
    if (!s.completions.empty())
    {
        if (!s.unixStyle)
        {
            int index = ((!forwards && s.lastAppliedIndex == -1) ? -1 : (s.lastAppliedIndex + (forwards ? 1 : -1))) + (int)s.completions.size();
            index %= s.completions.size();

            // restore quotes if it had them already
            auto& c = s.completions[index];
            std::string w = c.s;
            s.originalWord.q.applyQuotes(w);
            s.line.replace(s.wordPos.first, s.wordPos.second - s.wordPos.first, w);
            s.wordPos.second = int(w.size() + s.wordPos.first);
            s.lastAppliedIndex = index;
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
                // show remaining possibilities.  Assume up to 3 columns 5 rows, then they have to press again for more
                std::cout << std::endl;
                size_t sumSize = 0, sumCount = 0, sumMax = 0;
                for (size_t i = s.unixListCount; i < s.unixListCount + 30 && i < s.completions.size(); ++i)
                {
                    sumSize += s.completions[i].s.size();
                    sumCount += 1;
                    sumMax = std::max<size_t>(sumMax, s.completions[i].s.size());
                }
                size_t estimate = sumCount == 0 ? 50 : (sumSize / sumCount) * 5 / 3;
                size_t columnWidth = clamp<size_t>(std::min<size_t>(estimate, sumMax + 3), 10, 200);
                consoleWidth -= (consoleWidth > 3 ? 3 : 0);  // indent
                size_t columns = std::max<size_t>(1, consoleWidth / columnWidth);
                size_t row = 0, linepos = 0;
                while (s.unixListCount < s.completions.size() && row < 5)
                {
                    if (linepos > 0 && linepos % columnWidth == 0)
                    {
                        std::cout << ' ';
                        linepos += 1;
                    }
                    if (linepos <= columnWidth * (columns-1))
                    {
                        while (linepos % columnWidth)
                        {
                            std::cout << ' ';
                            linepos += 1;
                        }
                    }
                    if (linepos > 0 && linepos + s.completions[s.unixListCount].s.size() > columnWidth * columns)
                    {
                        std::cout << std::endl;
                        linepos = 0;
                        row += 1;
                    }
                    if (linepos == 0)
                    {   // indent
                        std::cout << "   ";
                    }
                    std::cout << s.completions[s.unixListCount].s;
                    linepos += utf8strlen(s.completions[s.unixListCount].s);
                    s.unixListCount += 1;
                }
                if (linepos != 0)
                {
                    std::cout << std::endl;
                }
                if (s.unixListCount < s.completions.size())
                {
                    std::cout << "<press again for more>" << std::endl;
                }
                else
                {
                    s.unixListCount = 0;
                }
            }
        }
    }
}


ACN either(ACN n1, ACN n2, ACN n3, ACN n4)
{
    auto n = std::make_unique<Either>();
    n->Add(n1);
    n->Add(n2);
    n->Add(n3);
    n->Add(n4);
    return n;
}

static ACN sequenceBuilder(ACN n1, ACN n2)
{
    return n2 ? std::make_shared<Sequence>(n1, n2) : n1;
}

ACN sequence(ACN n1, ACN n2, ACN n3, ACN n4, ACN n5, ACN n6, ACN n7, ACN n8)
{
    return sequenceBuilder(n1, sequenceBuilder(n2, sequenceBuilder(n3, sequenceBuilder(n4, sequenceBuilder(n5, sequenceBuilder(n6, sequenceBuilder(n7, n8)))))));
}

ACN text(const std::string s)
{
    return std::make_shared<Text>(s, false);
}

ACN param(const std::string s)
{
    return std::make_shared<Text>(s, true);
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


ACN wholenumber(size_t defaultvalue)
{
    return std::make_shared<WholeNumber>(defaultvalue);
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

ACN remoteFSPath(MegaClient* client, ::mega::handle* cwd, const std::string descriptionPrefix)
{
    return ACN(new MegaFS(true, true, client, cwd, descriptionPrefix));
}

ACN remoteFSFile(MegaClient* client, ::mega::handle* cwd, const std::string descriptionPrefix)
{
    return ACN(new MegaFS(true, false, client, cwd, descriptionPrefix));
}

ACN remoteFSFolder(MegaClient* client, ::mega::handle* cwd, const std::string descriptionPrefix)
{
    return ACN(new MegaFS(false, true, client, cwd, descriptionPrefix));
}


}}; //namespaces
