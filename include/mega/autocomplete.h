/**
* @file autocomplete.h
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

// autocomplete - so far just megacli and megaclc on windows and linux.

#ifndef MEGA_AUTOCOMPLETE_H
#define MEGA_AUTOCOMPLETE_H 1

#define HAVE_AUTOCOMPLETE 1

#include "mega/types.h"
#include <string>
#include <vector>
#include <memory>

namespace mega {
    class MEGA_API MegaClient;

namespace autocomplete {

    struct MEGA_API ACNode;
    typedef std::shared_ptr<ACNode> ACN;

    struct MEGA_API ACState
    {
        struct Completion {
            std::string s;
            bool caseInsensitive = false;
            bool couldExtend = false;
            inline Completion(const std::string& str, bool b, bool b2 = true) : s(str), caseInsensitive(b), couldExtend(b2) {}
        };

        std::vector<Completion> completions;
        void addCompletion(const std::string& s, bool caseInsenstive = false, bool couldextend = false);
        void addPathCompletion(std::string&& f, const std::string& relativeRootPath, bool isFolder, char dir_sep, bool caseInsensitive);

        std::vector<std::pair<int, int>> wordPos;

        struct quoting
        {
            quoting();
            quoting(std::string&);
            bool quoted = false;
            char quote_char = 0;
            void applyQuotes(std::string& s);
        };
        struct quoted_word
        {
            quoted_word();
            quoted_word(const std::string &);
            quoted_word(const std::string &, const quoting&);
            std::string s;
            quoting q;
            string getQuoted();
        };

        std::vector<quoted_word> words;
        unsigned i = 0;

        bool unixStyle = false;

        bool atCursor() {
            return i+1 >= words.size();
        }
        const quoted_word& word() {
            return words[i];
        }

        bool extractflag(const string& flag);
        bool extractflagparam(const string& flag, string& param);

        // More convenient than the above.
        std::optional<std::string> extractflagparam(const std::string& flag);

        ACN selectedSyntax;
    };


    struct MEGA_API ACNode
    {
        // returns true if we should stop searching deeper than this node.  Used by autoComplete
        virtual bool addCompletions(ACState& s) = 0;

        // indicate whether this node and its subnodes is a match for the state (ie, plausible intrpretation of remaining words). Used by autoExec
        virtual bool match(ACState& s) const = 0;

        // output suitable for user 'help'
        virtual std::ostream& describe(std::ostream& s) const = 0;

        virtual ~ACNode();
    };

    std::ostream& operator<<(std::ostream&, const ACNode&);

    struct MEGA_API Optional : public ACNode
    {
        ACN subnode;
        Optional(ACN n);
        bool isOptional();
        bool addCompletions(ACState& s) override;
        std::ostream& describe(std::ostream& s) const override;
        bool match(ACState& s) const override;
    };

    struct MEGA_API Repeat : public ACNode
    {
        ACN subnode;
        Repeat(ACN n);
        bool addCompletions(ACState& s) override;
        std::ostream& describe(std::ostream& s) const override;
        bool match(ACState& s) const override;
    };

    struct MEGA_API Sequence : public ACNode
    {
        ACN current, next;
        Sequence(ACN n1, ACN n2);
        bool addCompletions(ACState& s) override;
        std::ostream& describe(std::ostream& s) const override;
        bool match(ACState& s) const override;
    };

    struct MEGA_API Text : public ACNode
    {
        std::string exactText;
        bool param;
        Text(const std::string& s, bool isParam);
        bool addCompletions(ACState& s) override;
        std::ostream& describe(std::ostream& s) const override;
        bool match(ACState& s) const override;
    };

    struct MEGA_API ExportedLink : public ACNode
    {
        bool filelink, folderlink;
        static bool isLink(const string& s, bool file, bool folder);
        ExportedLink(bool file = true, bool folder = true);
        bool addCompletions(ACState& s) override;
        std::ostream& describe(std::ostream& s) const override;
        bool match(ACState& s) const override;
    };

   struct MEGA_API Flag : public ACNode
    {
        std::string flagText;
        Flag(const std::string& s);
        bool addCompletions(ACState& s) override;
        std::ostream& describe(std::ostream& s) const override;
        bool match(ACState& s) const override;
    };

    struct MEGA_API Either : public ACNode
    {
        typedef std::function<void(ACState&)> ExecFn;
        std::vector<ACN> eithers;
        std::vector<ExecFn> execFuncs;
        std::string describePrefix;
        Either(const std::string& describePrefix="");
        void Add(ACN n);
        void Add(ExecFn, ACN n);
        bool addCompletions(ACState& s) override;
        std::ostream& describe(std::ostream& s) const override;
        bool match(ACState& s) const override;
    };

    struct MEGA_API WholeNumber : public ACNode
    {
        size_t defaultvalue;
        std::string description;
        WholeNumber(const std::string& description, size_t defaultValue);
        bool addCompletions(ACState& s) override;
        std::ostream& describe(std::ostream& s) const override;
        bool match(ACState& s) const override;
    };

    struct MEGA_API LocalFS : public ACNode
    {
        bool reportFiles = true;
        bool reportFolders = true;
        std::string descPref;
        LocalFS(bool files, bool folders, const std::string descriptionPrefix);
        bool addCompletions(ACState& s) override;
        std::ostream& describe(std::ostream& s) const override;
        bool match(ACState& s) const override;
    };

    struct MEGA_API MegaFS : public ACNode
    {
        MegaClient* client;
        ::mega::NodeHandle* cwd;
        bool reportFiles = true;
        bool reportFolders = true;
        std::string descPref;
        MegaFS(bool files, bool folders, MegaClient* a, ::mega::NodeHandle* curDirHandle, const std::string descriptionPrefix);
        bool addCompletions(ACState& s) override;
        std::ostream& describe(std::ostream& s) const override;
        bool match(ACState& s) const override;
    };

    struct MEGA_API MegaContactEmail : public ACNode
    {
        MegaClient* client;
        MegaContactEmail(MegaClient*);
        bool addCompletions(ACState& s) override;
        std::ostream& describe(std::ostream& s) const override;
        bool match(ACState& s) const override;
    };

#ifdef ENABLE_SYNC

    class MEGA_API BackupID
      : public ACNode
    {
    public:
        BackupID(MegaClient& client, bool onlyActive);

        bool addCompletions(ACState& state) override;

        std::ostream& describe(std::ostream& ostream) const override;

        bool match(ACState& state) const override;

    private:
        struct DontValidate {};

        string_vector backupIDs() const;

        string_vector& filter(string_vector& ids, const ACState& state) const;

        bool match(const string_vector& ids, ACState& state) const;

        MegaClient& mClient;
        bool mOnlyActive;
    }; // BackupID

    ACN backupID(MegaClient& client, bool onlyActive = false);

#endif // ENABLE_SYNC

    struct MEGA_API CompletionState
    {
        std::string line;
        std::pair<int, int> wordPos;
        ACState::quoted_word originalWord;
        std::vector<ACState::Completion> completions;
        bool unixStyle = false;

        int lastAppliedIndex = -1;
        bool active = false;
        bool firstPressDone = false;
        size_t unixListCount = 0;
        unsigned calcUnixColumnWidthInGlyphs(int col, int rows);
        const string& unixColumnEntry(int row, int col, int rows);
        void tidyCompletions();
    };

    // helper function - useful in megacli for now
    ACState prepACState(const std::string line, size_t insertPos, bool unixStyle);

    // get a list of possible strings at the current cursor position
    CompletionState autoComplete(const std::string line, size_t insertPos, ACN syntax, bool unixStyle);

    // put the next possible string or unambiguous portion thereof at the cursor position, or indicate options to the user
    struct CompletionTextOut { vector<vector<string>> stringgrid; vector<int> columnwidths; };
    void applyCompletion(CompletionState& s, bool forwards, unsigned consoleWidth, CompletionTextOut& consoleOutput);

    // execute the function attached to the matching syntax
    bool autoExec(const std::string line, size_t insertPos, ACN syntax, bool unixStyle, string& consoleOutput, bool reportNoMatch);

    // functions to bulid command descriptions

    template<typename... Args>
    ACN either(Args&&... args)
    {
        static_assert((std::is_same_v<std::decay_t<Args>, ACN> && ...),
                      "All arguments must be of type ACN");
        auto n = std::make_shared<Either>();
        (n->Add(std::forward<Args>(args)), ...);
        return n;
    }

    template<typename... Args>
    ACN sequence(ACN n1, Args&&... args)
    {
        static_assert((std::is_same_v<std::decay_t<Args>, ACN> && ...),
                      "All arguments must be of type ACN");
        if constexpr (sizeof...(args) == 0)
        {
            return n1;
        }
        else
        {
            static const auto sequenceBuilder = [](ACN n1, ACN n2) -> ACN
            {
                return n2 ? std::make_shared<Sequence>(n1, n2) : n1;
            };
            return sequenceBuilder(n1, sequence(std::forward<Args>(args)...));
        }
    }

    ACN text(const std::string s);
    ACN param(const std::string s);
    ACN flag(const std::string s);
    ACN opt(ACN n);
    ACN repeat(ACN n);
    ACN exportedLink(bool file = true, bool folder = true);
    ACN wholenumber(const std::string& description, size_t defaultValue);
    ACN wholenumber(size_t defaultvalue);
    ACN localFSPath(const std::string descriptionPrefix = "");
    ACN localFSFile(const std::string descriptionPrefix = "");
    ACN localFSFolder(const std::string descriptionPrefix = "");
    ACN remoteFSPath(MegaClient*, ::mega::NodeHandle*, const std::string descriptionPrefix = "");
    ACN remoteFSFile(MegaClient*, ::mega::NodeHandle*, const std::string descriptionPrefix = "");
    ACN remoteFSFolder(MegaClient*, ::mega::NodeHandle*, const std::string descriptionPrefix = "");
    ACN contactEmail(MegaClient*);

}} //namespaces
#endif
