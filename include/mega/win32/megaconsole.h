/**
 * @file mega/win32/megaconsole.h
 * @brief Win32 console I/O
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

#ifndef CONSOLE_CLASS
#define CONSOLE_CLASS WinConsole

#include <string>
#include <deque>
#include <mega/autocomplete.h>
#include <mega/console.h>

namespace mega {

#ifdef NO_READLINE

struct MEGA_API Utf8Rdbuf;

struct MEGA_API ConsoleModel
{
    enum {
        MaxHistoryEntries = 20
    };
    enum lineEditAction {
        nullAction,
        CursorLeft, CursorRight, CursorStart, CursorEnd,
        WordLeft, WordRight,
        HistoryUp, HistoryDown, HistoryStart, HistoryEnd, HistorySearchForward, HistorySearchBackward,
        ClearLine, DeleteCharLeft, DeleteCharRight, DeleteWordLeft, DeleteWordRight,
        Paste, AutoCompleteForwards, AutoCompleteBackwards
    };

#ifdef HAVE_AUTOCOMPLETE
    // If using autocomplete, client to specify the syntax of commands here so we know what.  Assign to this directly
    ::mega::autocomplete::ACN autocompleteSyntax;
    
    // If supplied, autocomplete will try to get additional completions from this function (eg for consulting MEGAcmd's server)
    std::function<vector<autocomplete::ACState::Completion>(string)> autocompleteFunction;
#endif
    // a buffer to store characters received from keypresses.  After a newline is received, we 
    // don't check for keypresses anymore until that line is consumed.
    std::wstring buffer;

    // the point in the buffer that new characters get inserted (corresponds to cursor on screen)
    size_t insertPos = 0;

    // we can receive multiple newlines in a single key event. All these must be consumed before we check for more keypresses
    bool newlinesBuffered = false;

    // remember the last N commands executed 
    std::deque<std::wstring> inputHistory;
 
    // when using up and down arrow keys, or using history search, this is the history line selected
    size_t inputHistoryIndex = 0;

    // slightly different handling on the first history keypress
    bool enteredHistory = false;
    bool searchingHistory = false;
    bool searchingHistoryForward = false;
    std::wstring historySearchString;

    // if echo is on then edits appear on screen; if it is off then nothing appears and history is not added (suitable for passwords).
    bool echoOn = true;

    // we can do autocomplete like windows' cmd.exe or like unix.  Start with the one matching the current platform
#ifdef WIN32
    bool unixCompletions = false;
#else
    bool unixCompletions = true;
#endif

#ifdef HAVE_AUTOCOMPLETE
    // flags to indicate to the real console if redraws etc need to occur
    ::mega::autocomplete::CompletionTextOut redrawInputLineConsoleFeedback;
#endif
    bool redrawInputLineNeeded = false;
    bool consoleNewlineNeeded = false;

    // real console tells us a key is pressed resulting in a character to add
    void addInputChar(wchar_t c);

    // real console has interpreted a key press as a special action needed
    void performLineEditingAction(lineEditAction action, unsigned consoleWidth);

    // client can check this after adding characters or performing actions to see if the user submitted the line for processing 
    bool checkForCompletedInputLine(std::wstring& ws);
    std::wstring getInputLineToCursor(); 

private:
#ifdef HAVE_AUTOCOMPLETE
    autocomplete::CompletionState autocompleteState;
#endif
    void getHistory(int index, int offset);
    void searchHistory(bool forwards);
    void updateHistoryMatch(bool forwards, bool increment);
    void deleteHistorySearchChars(size_t n);
    void deleteCharRange(int start, int end);
    void redrawInputLine(int p);
    int detectWordBoundary(int start, bool forward);
    void autoComplete(bool forwards, unsigned consoleWidth);
};
    
#endif

struct MEGA_API WinConsole : public Console
{
    void readpwchar(char*, int, int* pw_buf_pos, char**);
    void setecho(bool);

    WinConsole();
    ~WinConsole();

#ifdef NO_READLINE
    HANDLE inputAvailableHandle();

    // functions for native command editing (ie not using readline library)
    string getConsoleFont(COORD& xy);
    bool setShellConsole(UINT codepage = CP_UTF8, UINT failover_codepage = CP_UTF8);
    void getShellCodepages(UINT& codepage, UINT& failover_codepage);

#ifdef HAVE_AUTOCOMPLETE
    void setAutocompleteSyntax(autocomplete::ACN);
    void setAutocompleteFunction(std::function<vector<autocomplete::ACState::Completion>(string)>);
#endif
    void setAutocompleteStyle(bool unix);
    bool getAutocompleteStyle() const;
    bool consolePeek();
    bool consolePeekNonBlocking();
    bool consolePeekBlocking();
    bool consoleGetch(wchar_t& c);
    void updateInputPrompt(const std::string& newprompt);
    char* checkForCompletedInputLine();
    void clearScreen();
    void outputHistory();
    void retractPrompt();

    std::wstring getInputLineToCursor();

    enum logstyle { no_log, utf8_log, utf16_log, codepage_log };
    bool log(const std::string& filename, logstyle logstyle);

    static std::string toUtf8String(const std::wstring& ws, UINT codepage = CP_UTF8);
    static std::wstring toUtf16String(const std::string& s, UINT codepage = CP_UTF8);
    bool blockingConsolePeek;

private:
    std::deque<INPUT_RECORD> irs;
    HANDLE hInput;
    HANDLE hOutput;
    bool promptRetracted = false;
    ConsoleModel model;
    Utf8Rdbuf *rdbuf = nullptr;
    streambuf *oldrb1 = nullptr, *oldrb2 = nullptr;
    bool logging = false;

    std::string currentPrompt;
    size_t inputLineOffset = 0;

    void redrawPromptIfLoggingOccurred();
#ifdef HAVE_AUTOCOMPLETE
    void redrawInputLine(::mega::autocomplete::CompletionTextOut* autocompleteFeedback);
#else
    void redrawInputLine();
#endif
    ConsoleModel::lineEditAction interpretLineEditingKeystroke(INPUT_RECORD &ir);
#endif
};
} // namespace

#endif
