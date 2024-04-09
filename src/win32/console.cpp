/**
 * @file win32/console.cpp
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

#include "mega.h"
#include "megaapi.h"
#include <windows.h>
#include <conio.h>
#include <fstream>
#include <iomanip>

#include <io.h>
#include <fcntl.h>
#include <algorithm>
#include <string>
#include <cwctype>

namespace mega {

using namespace std;

#ifdef NO_READLINE

std::string WinConsole::toUtf8String(const std::wstring& ws, UINT codepage)
{
    std::string s;
    s.resize((ws.size() + 1) * 4);
    int nchars = WideCharToMultiByte(codepage, 0, ws.data(), int(ws.size()), (LPSTR)s.data(), int(s.size()), NULL, NULL);
    s.resize(nchars);
    return s;
}

std::wstring WinConsole::toUtf16String(const std::string& s, UINT codepage)
{
    std::wstring ws;
    ws.resize(s.size() + 1);
    int nwchars = MultiByteToWideChar(codepage, 0, s.data(), int(s.size()), (LPWSTR)ws.data(), int(ws.size()));
    ws.resize(nwchars);
    return ws;
}

inline static bool wicmp(wchar_t a, wchar_t b)
{
    return(towupper(a) == towupper(b));
}

struct Utf8Rdbuf : public streambuf
{
    HANDLE h;
    UINT codepage = CP_UTF8;
    UINT failover_codepage = CP_UTF8;
    std::ofstream logfile;
    WinConsole::logstyle logstyle = WinConsole::no_log;
    WinConsole* wc;

    bool log(const string& localfile, WinConsole::logstyle ls)
    {
        logfile.close();
        logstyle = WinConsole::no_log;

        if (localfile.empty() && ls == WinConsole::no_log)
        {
            return true;
        }

        logfile.open(localfile.c_str(), ios::out | ios::binary | ios::trunc);
        if (logfile.fail() || !logfile.is_open())
        {
            return false;
        }
        
        logstyle = ls;
        return true;
    }

    Utf8Rdbuf(HANDLE ch, WinConsole* w) : h(ch), wc(w) {}

    streamsize xsputn(const char* s, streamsize n)
    {
        DWORD bn = DWORD(_Pnavail()), written = 0;
        string s8(pbase(), bn);
        pbump(-int(bn));
        s8.append(s, size_t(n));

        if (logstyle == WinConsole::utf8_log)
        {
            logfile << s8;
        }

        wstring ws = WinConsole::toUtf16String(s8);

        if (logstyle == WinConsole::utf16_log)
        {
            logfile.write((const char*)ws.data(), ws.size() * sizeof(wchar_t));
        }
        else if (logstyle == WinConsole::codepage_log)
        {
            logfile << WinConsole::toUtf8String(ws, codepage);
        }

        if (wc)
        {
            wc->retractPrompt();
        }

        BOOL b = WriteConsoleW(h, ws.data(), DWORD(ws.size()), &written, NULL);
        if (!b)
        {
            // The font can't display some characters (fails on windows 7 - windows 10 not so much but just in case).  
            // Output those that we can and indicate the others.  
            // If the user selects a suitable font then there should not be any failures.
            for (unsigned i = 0; i < ws.size(); ++i)
            {
                b = WriteConsoleW(h, ws.data() + i, 1, &written, NULL);
                if (!b && failover_codepage != codepage)
                {
                    // for raster fonts, we can have a second go with another code page, translating directly from utf16
                    if (SetConsoleOutputCP(failover_codepage))
                    {
                        b = WriteConsoleW(h, ws.data() + i, 1, &written, NULL);
                        SetConsoleOutputCP(codepage);
                    }
                }
                if (!b)
                {
                    wostringstream wos;
                    wos << L"<CHAR/" << hex << unsigned short(ws.data()[i]) << L">";
                    wstring str = wos.str();
                    WriteConsoleW(h, str.data(), DWORD(str.size()), &written, NULL);
                }
            }
        }

        return n;
    }

    int overflow(int c)
    {
        char cc = char(c);
        xsputn(&cc, 1);
        return c;
    }

};

void ConsoleModel::addInputChar(wchar_t c)
{
    insertPos = std::clamp(insertPos, 0, buffer.size());
    if (c == 13)
    {
        buffer.push_back(c);
        insertPos = buffer.size();
        newlinesBuffered = true;
        consoleNewlineNeeded = true;
        searchingHistory = false;
        historySearchString.clear();
    }
    else
    {
        if (searchingHistory)
        {
            historySearchString.push_back(c);
            updateHistoryMatch(searchingHistoryForward, false);
        }
        else
        {
            buffer.insert(insertPos, 1, c);
            insertPos += 1;
        }
        redrawInputLineNeeded = true;
    }
#ifdef HAVE_AUTOCOMPLETE
    autocompleteState.active = false;
#endif
}

void ConsoleModel::getHistory(int index, int offset)
{
    if (inputHistory.empty() && offset == 1)
    {
        buffer.clear();
        newlinesBuffered = false;
    }
    else
    {
        index = std::clamp(index, 0, (int)inputHistory.size() - 1) + (enteredHistory ? offset : (offset == -1 ? -1 : 0));
        if (index < 0 || index >= (int)inputHistory.size())
        {
            return;
        }
        inputHistoryIndex = index;
        buffer = inputHistory[inputHistoryIndex];
        enteredHistory = true;
        newlinesBuffered = false;
    }
    insertPos = buffer.size();
    redrawInputLineNeeded = true;
}

void ConsoleModel::searchHistory(bool forwards)
{
    if (!searchingHistory)
    {
        searchingHistory = true;
        searchingHistoryForward = forwards;
        historySearchString.clear();
    }
    else
    {
        updateHistoryMatch(forwards, true);
    }
    redrawInputLineNeeded = true;
}

void ConsoleModel::updateHistoryMatch(bool forwards, bool increment)
{
    bool checking = false;
    for (unsigned i = 0; i < inputHistory.size()*2; ++i)
    {
        size_t index = forwards ? inputHistory.size()*2 - i - 1 : i;
        index %= inputHistory.size();
        checking = checking || !enteredHistory || index == inputHistoryIndex;
        if (checking && !(enteredHistory && increment && index == inputHistoryIndex))
        {
            auto iter = std::search(inputHistory[index].begin(), inputHistory[index].end(), historySearchString.begin(), historySearchString.end(), wicmp);
            if (iter != inputHistory[index].end())
            {
                inputHistoryIndex = index;
                enteredHistory = true;
                buffer = inputHistory[index];
                insertPos = buffer.size();
                newlinesBuffered = false;
                redrawInputLineNeeded = true;
                break;
            }
        }
    }
}

void ConsoleModel::deleteHistorySearchChars(size_t n)
{
    if (n == 0)
    {
        searchingHistory = false;
    }
    else
    {
        n = std::min<size_t>(n, historySearchString.size());
        historySearchString.erase(historySearchString.size() - n, n);
        updateHistoryMatch(searchingHistoryForward, false);
    }
    redrawInputLineNeeded = true;
}

void ConsoleModel::redrawInputLine(int p)
{
    insertPos = std::clamp(p, 0, buffer.size());
    redrawInputLineNeeded = true;
}

void ConsoleModel::autoComplete(bool forwards, unsigned consoleWidth)
{   
#ifdef HAVE_AUTOCOMPLETE
    if (autocompleteSyntax)
    {
        if (!autocompleteState.active)
        {
            std::string u8line = WinConsole::toUtf8String(buffer);
            size_t u8InsertPos = WinConsole::toUtf8String(buffer.substr(0, insertPos)).size();
            autocompleteState = autocomplete::autoComplete(u8line, u8InsertPos, autocompleteSyntax, unixCompletions);

            if (autocompleteFunction)
            {
                // also get additional app specific options, and merge
                std::vector<autocomplete::ACState::Completion> appcompletions = autocompleteFunction(WinConsole::toUtf8String(getInputLineToCursor()));
                autocomplete::ACState acs;
                acs.words.push_back(autocompleteState.originalWord);
                acs.completions.swap(autocompleteState.completions);
                for (auto& c : appcompletions)
                {
                    acs.addCompletion(c.s, c.caseInsensitive, c.couldExtend);
                }
                autocompleteState.completions.swap(acs.completions);
                autocompleteState.tidyCompletions();
            }
            autocompleteState.active = true;
        }

        autocomplete::applyCompletion(autocompleteState, forwards, consoleWidth, redrawInputLineConsoleFeedback);
        buffer = WinConsole::toUtf16String(autocompleteState.line);
        newlinesBuffered = false;
        size_t u16InsertPos = WinConsole::toUtf16String(autocompleteState.line.substr(0, autocompleteState.wordPos.second)).size();
        insertPos = std::clamp(u16InsertPos, 0, buffer.size());
        redrawInputLineNeeded = true;
    }
#endif
}

static bool isWordBoundary(size_t i, const std::wstring s)
{
    return i == 0 || i >= s.size() || is_space(s[i - 1]) && !is_space(s[i + 1]);
}

int ConsoleModel::detectWordBoundary(int start, bool forward)
{
    start = std::clamp(start, 0, buffer.size());
    do
    {
        start += (forward ? 1 : -1);
    } while (!isWordBoundary(start, buffer));
    return start;
}

void ConsoleModel::deleteCharRange(int start, int end)
{
    start = std::clamp(start, 0, buffer.size());
    end = std::clamp(end, 0, buffer.size());
    if (start < end)
    {
        buffer.erase(start, end - start);
        newlinesBuffered = buffer.find(13) != string::npos;
        redrawInputLine(start);
    }
}

void ConsoleModel::performLineEditingAction(lineEditAction action, unsigned consoleWidth)
{
#ifdef HAVE_AUTOCOMPLETE
    if (action != AutoCompleteForwards && action != AutoCompleteBackwards)
    {
        autocompleteState.active = false;
    }
#endif
    if (action != HistorySearchForward && action != HistorySearchBackward && action != DeleteCharLeft && action != ClearLine)
    {
        searchingHistory = false;
    }

    int pos = (int)insertPos;
    int bufSize = buffer.size();

    switch (action)
    {
    case CursorLeft: redrawInputLine(pos - 1); break;
    case CursorRight: redrawInputLine(pos + 1); break;
    case CursorStart: redrawInputLine(0);  break;
    case CursorEnd: redrawInputLine(bufSize); break;
    case WordLeft: redrawInputLine((int)detectWordBoundary(pos, false));  break;
    case WordRight: redrawInputLine((int)detectWordBoundary(pos, true));  break;
    case HistoryUp: getHistory((int)inputHistoryIndex, 1);  break;
    case HistoryDown: getHistory((int)inputHistoryIndex, -1); break;
    case HistoryStart: getHistory((int)inputHistory.size() - 1, 0);  break;
    case HistoryEnd: getHistory(0, 0);  break;
    case HistorySearchForward: searchHistory(true);  break;
    case HistorySearchBackward: searchHistory(false);  break;
    case ClearLine: searchingHistory ? deleteHistorySearchChars(0) : deleteCharRange(0, bufSize);  break;
    case DeleteCharLeft: searchingHistory ? deleteHistorySearchChars(1) : deleteCharRange(pos - 1, pos);  break;
    case DeleteCharRight: deleteCharRange(pos, pos + 1); break;
    case DeleteWordLeft: deleteCharRange(detectWordBoundary(pos, false), pos);  break;
    case DeleteWordRight: deleteCharRange(pos, detectWordBoundary(pos, true));  break;
    case AutoCompleteForwards: autoComplete(true, consoleWidth); break;
    case AutoCompleteBackwards: autoComplete(false, consoleWidth); break;
    }
}

bool ConsoleModel::checkForCompletedInputLine(std::wstring& ws)
{
    auto newlinePos = std::find(buffer.begin(), buffer.end(), 13);
    if (newlinePos != buffer.end())
    {
        ws.assign(buffer.begin(), newlinePos);
        buffer.erase(buffer.begin(), newlinePos + 1);
        insertPos = 0;
        newlinesBuffered = buffer.find(13) != string::npos;
        bool sameAsLastCommand = !inputHistory.empty() && inputHistory[0] == ws;
        bool sameAsChosenHistory = !inputHistory.empty() &&
            inputHistoryIndex >= 0 && inputHistoryIndex < inputHistory.size() &&
            inputHistory[inputHistoryIndex] == ws;
        if (echoOn && !sameAsLastCommand && !ws.empty())
        {
            if (inputHistory.size() + 1 > MaxHistoryEntries)
            {
                inputHistory.pop_back();
            }
            inputHistory.push_front(ws);
            inputHistoryIndex = sameAsChosenHistory ? inputHistoryIndex + 1 : -1;
        }
        enteredHistory = false;
        return true;
    }
    newlinesBuffered = false;
    return false;
}

std::wstring ConsoleModel::getInputLineToCursor()
{
    insertPos = std::clamp(insertPos, 0, buffer.size());
    return buffer.substr(0, insertPos);
}
#endif

WinConsole::WinConsole()
{
#ifdef NO_READLINE
    hInput = GetStdHandle(STD_INPUT_HANDLE);
    hOutput = GetStdHandle(STD_OUTPUT_HANDLE);

    DWORD dwMode;
    GetConsoleMode(hInput, &dwMode);
    SetConsoleMode(hInput, dwMode & ~(ENABLE_MOUSE_INPUT));
    FlushConsoleInputBuffer(hInput);
    blockingConsolePeek = false;
#endif
}

WinConsole::~WinConsole()
{
#ifdef NO_READLINE
    if (rdbuf)
    {
        std::cout.rdbuf(oldrb1);
        std::cerr.rdbuf(oldrb2);
        delete rdbuf;
    }
#endif
}

#ifdef NO_READLINE
string WinConsole::getConsoleFont(COORD& size)
{
    CONSOLE_FONT_INFOEX cfi;
    memset(&cfi, 0, sizeof(cfi));
    cfi.cbSize = sizeof(cfi);
    GetCurrentConsoleFontEx(hOutput, FALSE, &cfi);

    wstring wname = cfi.FaceName;
    string name = WinConsole::toUtf8String(wname);

    if (!(cfi.FontFamily & TMPF_TRUETYPE) && (wname.size() < 6 || name.find("?") != string::npos))
    {
        // the name is garbled on win 7, try to compensate
        name = "Terminal";
    }

    size = cfi.dwFontSize;
    return name;
}

void WinConsole::getShellCodepages(UINT& codepage, UINT& failover_codepage)
{
    if (rdbuf)
    {
        codepage = rdbuf->codepage;
        failover_codepage = rdbuf->failover_codepage;
    }
    else
    {
        codepage = GetConsoleOutputCP(); 
        failover_codepage = codepage;
    }
}

bool WinConsole::setShellConsole(UINT codepage, UINT failover_codepage)
{
    // Call this if your console app is taking live input, with the user editing commands on screen, similar to cmd or powershell

    // Ideally we would work in unicode all the time (with codepage = CP_UTF8).  However in windows 7 for example, with raster 
    // font selected, the o symbol with diacritic (U+00F3) is not output correctly.  So we offer the option to attempt output 
    // a second time in a 'failover' codepage, or to output in a single codepage only.  The user has control with the 'codepage' command.

    // use cases covered
    // utf8 output with std::cout (since we already use cout so much and it's compatible with other platforms)
    // unicode input with windows ReadConsoleInput api
    // drag and drop filenames from explorer to the console window
    // copy and paste unicode filenames from 'ls' output into your next command
    // upload and download unicode/utf-8 filenames to/from Mega
    // input a unicode/utf8 password without displaying anything
    // normal cmd window type editing, including autocomplete (with runtime selectable unix style or dos style, default to local platform rules)
    // the console must have a suitable font selected for the characters to diplay properly

    BOOL ok =  SetConsoleCP(codepage);
    ok = ok && SetConsoleOutputCP(codepage);
    if (!ok)
    {
        codepage = CP_UTF8;
        failover_codepage = GetOEMCP();
        SetConsoleCP(codepage);
        SetConsoleOutputCP(codepage);
    }

    // skip the historic complexities of output modes etc, our own rdbuf can write direct to console
    if (!rdbuf)
    {
        rdbuf = new Utf8Rdbuf(hOutput, this);
        oldrb1 = std::cout.rdbuf(rdbuf);
        oldrb2 = std::cerr.rdbuf(rdbuf);
    }
    rdbuf->codepage = codepage;
    rdbuf->failover_codepage = failover_codepage;
    return ok;
}

#ifdef HAVE_AUTOCOMPLETE
void WinConsole::setAutocompleteSyntax(autocomplete::ACN a)
{
    model.autocompleteSyntax = a;
}

void WinConsole::setAutocompleteFunction(std::function<vector<autocomplete::ACState::Completion>(string)> f)
{
    model.autocompleteFunction = f;
}
#endif

void WinConsole::setAutocompleteStyle(bool unix)
{
    model.unixCompletions = unix;
}

bool WinConsole::getAutocompleteStyle() const
{
    return model.unixCompletions;
}

HANDLE WinConsole::inputAvailableHandle()
{
    // returns a handle that will be signalled when there is console input to process (ie records available for PeekConsoleInput)
    // client can wait on this handle with other handles as higher priority
    return hInput;
}

bool WinConsole::consolePeek()
{
    return blockingConsolePeek?consolePeekBlocking():consolePeekNonBlocking();
}

bool WinConsole::consolePeekNonBlocking()
{
    std::cout << std::flush;

    // Read keypreses up to the first newline (or multiple newlines if
    bool checkPromptOnce = true;
    for (;;)
    {
        INPUT_RECORD ir;
        DWORD nRead;
        BOOL ok = PeekConsoleInput(hInput, &ir, 1, &nRead);  // peek first so we never wait
        assert(ok);
        if (!nRead)
        {
            break;
        }

        bool isCharacterGeneratingKeypress =
            ir.EventType == 1 && ir.Event.KeyEvent.uChar.UnicodeChar != 0 &&
            (ir.Event.KeyEvent.bKeyDown ||  // key press
            (!ir.Event.KeyEvent.bKeyDown && ((ir.Event.KeyEvent.dwControlKeyState & LEFT_ALT_PRESSED) || ir.Event.KeyEvent.wVirtualKeyCode == VK_MENU)));  // key release that emits a unicode char

        if (isCharacterGeneratingKeypress && (currentPrompt.empty() || model.newlinesBuffered))
        {
            break;
        }

        ok = ReadConsoleInputW(hInput, &ir, 1, &nRead);  // discard the event record
        assert(ok);
        assert(nRead == 1);

        ConsoleModel::lineEditAction action = interpretLineEditingKeystroke(ir);

        if ((action != ConsoleModel::nullAction || isCharacterGeneratingKeypress) && checkPromptOnce)
        {
            redrawPromptIfLoggingOccurred();
            checkPromptOnce = false;
        }
        if (action != ConsoleModel::nullAction)
        {
            CONSOLE_SCREEN_BUFFER_INFO sbi;
            BOOL ok = GetConsoleScreenBufferInfo(hOutput, &sbi);
            assert(ok);
            unsigned consoleWidth = ok ? sbi.dwSize.X : 50;

            model.performLineEditingAction(action, consoleWidth);
        }
        else if (isCharacterGeneratingKeypress)
        {
            for (int i = ir.Event.KeyEvent.wRepeatCount; i--; )
            {
                model.addInputChar(ir.Event.KeyEvent.uChar.UnicodeChar);
            }

            if (model.newlinesBuffered)
            {
                break;
            }
        }
    }
    if (model.redrawInputLineNeeded && model.echoOn)
    {
#ifdef HAVE_AUTOCOMPLETE
        redrawInputLine(&model.redrawInputLineConsoleFeedback);
#else
        redrawInputLine();
#endif
    }
    if (model.consoleNewlineNeeded)
    {
        DWORD written = 0;
        #ifndef NDEBUG
        BOOL b =
        #endif
        WriteConsoleW(hOutput, L"\n", 1, &written, NULL);
        assert(b && written == 1);
    }
    model.redrawInputLineNeeded = false;
    model.consoleNewlineNeeded = false;
    return model.newlinesBuffered;
}

bool WinConsole::consolePeekBlocking()
{
    std::cout << std::flush;

    // Read keypreses up to the first newline (or multiple newlines if
    bool checkPromptOnce = true;
    bool isCharacterGeneratingKeypress = false;

    INPUT_RECORD ir;
    DWORD nRead;
    if (!ReadConsoleInputW(hInput, &ir, 1, &nRead))  // discard the event record
    {
        return false;
    }

    irs.push_back(ir);

    isCharacterGeneratingKeypress =
            ir.EventType == 1 && ir.Event.KeyEvent.uChar.UnicodeChar != 0 &&
            (ir.Event.KeyEvent.bKeyDown ||  // key press
             (!ir.Event.KeyEvent.bKeyDown && ((ir.Event.KeyEvent.dwControlKeyState & LEFT_ALT_PRESSED) || ir.Event.KeyEvent.wVirtualKeyCode == VK_MENU)));  // key release that emits a unicode char

    if (!(isCharacterGeneratingKeypress && (currentPrompt.empty() || model.newlinesBuffered)))
    {
        while(!irs.empty())
        {
            INPUT_RECORD &ir = irs.front();
            ConsoleModel::lineEditAction action = interpretLineEditingKeystroke(ir);

            if ((action != ConsoleModel::nullAction || isCharacterGeneratingKeypress) && checkPromptOnce)
            {
                redrawPromptIfLoggingOccurred();
                checkPromptOnce = false;
            }
            if (action != ConsoleModel::nullAction)
            {
                CONSOLE_SCREEN_BUFFER_INFO sbi;
                BOOL ok = GetConsoleScreenBufferInfo(hOutput, &sbi);
                assert(ok);
                unsigned consoleWidth = ok ? sbi.dwSize.X : 50;

                model.performLineEditingAction(action, consoleWidth);
            }
            else if (isCharacterGeneratingKeypress)
            {
                for (int i = ir.Event.KeyEvent.wRepeatCount; i--; )
                {
                    model.addInputChar(ir.Event.KeyEvent.uChar.UnicodeChar);
                }
                if (model.newlinesBuffered)  // todo: address case where multiple newlines were added from this one record (as we may get stuck in wait())
                {
                    irs.pop_front();
                    break;
                }
            }
            irs.pop_front();
        }
    }
    if (model.redrawInputLineNeeded && model.echoOn)
    {
#ifdef HAVE_AUTOCOMPLETE
        redrawInputLine(&model.redrawInputLineConsoleFeedback);
#else
        redrawInputLine();
#endif
    }
    if (model.consoleNewlineNeeded)
    {
        DWORD written = 0;
        #ifndef NDEBUG
        BOOL b =
        #endif
        WriteConsoleW(hOutput, L"\n", 1, &written, NULL);
        assert(b && written == 1);
    }
    model.redrawInputLineNeeded = false;
    model.consoleNewlineNeeded = false;
    return model.newlinesBuffered;
}

ConsoleModel::lineEditAction WinConsole::interpretLineEditingKeystroke(INPUT_RECORD &ir)
{
    if (ir.EventType == 1 && ir.Event.KeyEvent.bKeyDown)
    {
        bool ctrl = ir.Event.KeyEvent.dwControlKeyState & (RIGHT_CTRL_PRESSED | LEFT_CTRL_PRESSED);
        bool shift = ir.Event.KeyEvent.dwControlKeyState & SHIFT_PRESSED;
        switch (ir.Event.KeyEvent.wVirtualKeyCode)
        {
        case VK_LEFT: return ctrl ? ConsoleModel::WordLeft : ConsoleModel::CursorLeft;
        case VK_RIGHT: return ctrl ? ConsoleModel::WordRight : ConsoleModel::CursorRight;
        case VK_UP: return ConsoleModel::HistoryUp;
        case VK_DOWN: return ConsoleModel::HistoryDown;
        case VK_PRIOR: return ConsoleModel::HistoryStart; // pageup
        case VK_NEXT: return ConsoleModel::HistoryEnd; // pagedown
        case VK_HOME: return ConsoleModel::CursorStart;
        case VK_END: return ConsoleModel::CursorEnd;
        case VK_DELETE: return ConsoleModel::DeleteCharRight;
        case VK_INSERT: return ConsoleModel::Paste;  // the OS takes care of this; we don't see it
        case VK_CONTROL: break;
        case VK_SHIFT: break;
        case 's':
        case 'S': return ctrl ? (shift ? ConsoleModel::HistorySearchBackward : ConsoleModel::HistorySearchForward) : ConsoleModel::nullAction;
        case 'r':
        case 'R': return ctrl ? (shift ? ConsoleModel::HistorySearchForward : ConsoleModel::HistorySearchBackward) : ConsoleModel::nullAction;
        default:
            switch (ir.Event.KeyEvent.uChar.UnicodeChar)
            {
            case '\b': return ConsoleModel::DeleteCharLeft;
            case '\t': return shift ? ConsoleModel::AutoCompleteBackwards : ConsoleModel::AutoCompleteForwards;
            case VK_ESCAPE: return ConsoleModel::ClearLine;
            default:
                break;
            }
            break;
        }
    }
    return ConsoleModel::nullAction;
}

#ifdef HAVE_AUTOCOMPLETE
void WinConsole::redrawInputLine(::mega::autocomplete::CompletionTextOut* autocompleteFeedback = nullptr)
#else
void WinConsole::redrawInputLine()
#endif

{
    CONSOLE_SCREEN_BUFFER_INFO sbi;

#ifdef HAVE_AUTOCOMPLETE
    if (autocompleteFeedback && !autocompleteFeedback->stringgrid.empty())
    {
        promptRetracted = true;
        cout << "\n" << std::flush;
        for (auto& r : autocompleteFeedback->stringgrid)
        {
            int x = 0;
            for (unsigned c = 0; c < r.size(); ++c)
            {
                cout << r[c] << std::flush;
                if (c + 1 == r.size())
                {
                    cout << "\n" << std::flush;
                }
                else
                {
                    x += autocompleteFeedback->columnwidths[c];

                    // to make the grid nice in the presence of unicode characters that are sometimes double-width glyphs, we set the X coordinate explicitly
                    BOOL ok = GetConsoleScreenBufferInfo(hOutput, &sbi);
                    if (ok && sbi.dwCursorPosition.X < x)
                    {
                        sbi.dwCursorPosition.X = short(x);
                        SetConsoleCursorPosition(hOutput, sbi.dwCursorPosition);
                    }
                }
            }
        }
        autocompleteFeedback->stringgrid.clear();
        autocompleteFeedback->columnwidths.clear();
    }
#endif

    BOOL ok = GetConsoleScreenBufferInfo(hOutput, &sbi);
    assert(ok);
    if (ok)
    {
        std::string sprompt = model.searchingHistory ? ("history-" + std::string(model.searchingHistoryForward ? "F:'" : "R:'") + toUtf8String(model.historySearchString) + "'> ")
                                                    : currentPrompt;
        std::wstring wprompt = toUtf16String(sprompt);

        if (long(wprompt.size() + model.buffer.size() + 1) < sbi.dwSize.X || !model.echoOn)
        {
            inputLineOffset = 0;
        }
        else
        {
            // scroll the line if the cursor reaches the end, or moves back within 15 of the start
            size_t showleft = 15;
            if (inputLineOffset + showleft >= model.insertPos)
            {
                inputLineOffset = model.insertPos - std::min<size_t>(showleft, model.insertPos);
            } else if (wprompt.size() + model.insertPos + 1 >= inputLineOffset + sbi.dwSize.X)
            {
                inputLineOffset = wprompt.size() + model.insertPos + 1 - sbi.dwSize.X;
            }
        }

        size_t width = std::max<size_t>(wprompt.size() + model.buffer.size() + 1 + inputLineOffset, sbi.dwSize.X); // +1 to show character under cursor 
        std::unique_ptr<CHAR_INFO[]> line(new CHAR_INFO[width]);

        for (size_t i = width; i--; )
        {
            line[i].Attributes = sbi.wAttributes;
            if (i < inputLineOffset)
            {
                line[i].Char.UnicodeChar = ' ';
            }
            else if (inputLineOffset && i + 1 == inputLineOffset + wprompt.size())
            {
                line[i].Char.UnicodeChar = '|';
                line[i].Attributes |= FOREGROUND_INTENSITY | FOREGROUND_GREEN;
                line[i].Attributes &= ~(FOREGROUND_RED | FOREGROUND_BLUE);
            }
            else if (i < inputLineOffset + wprompt.size())
            {
                line[i].Char.UnicodeChar = wprompt[i - inputLineOffset];
                line[i].Attributes |= FOREGROUND_INTENSITY;
            }
            else if (i < wprompt.size() + model.buffer.size() && model.echoOn)
            {
                line[i].Char.UnicodeChar = model.buffer[i - wprompt.size()];
            }
            else
            {
                line[i].Char.UnicodeChar = ' ';
            }
        }

        SMALL_RECT screenarea2{ 0, sbi.dwCursorPosition.Y, sbi.dwSize.X, sbi.dwCursorPosition.Y };
        ok = WriteConsoleOutputW(hOutput, line.get(), COORD{ SHORT(width), 1 }, COORD{ SHORT(inputLineOffset), 0 }, &screenarea2);
        assert(ok);

        COORD cpos{ SHORT(wprompt.size() + model.insertPos - inputLineOffset), sbi.dwCursorPosition.Y };
        ok = SetConsoleCursorPosition(hOutput, cpos);
        assert(ok);

        promptRetracted = false;
    }
}

void WinConsole::retractPrompt()
{
    if (currentPrompt.size() && !promptRetracted)
    {
        CONSOLE_SCREEN_BUFFER_INFO sbi;
        BOOL ok = GetConsoleScreenBufferInfo(hOutput, &sbi);
        assert(ok);
        //if (0 == memcmp(&knownCursorPos, &sbi.dwCursorPosition, sizeof(COORD)))
        {
            size_t width = std::max<size_t>(currentPrompt.size() + model.buffer.size() + 1 + inputLineOffset, sbi.dwSize.X); // +1 to show character under cursor 
            std::unique_ptr<CHAR_INFO[]> line(new CHAR_INFO[width]);

            for (size_t i = width; i--; )
            {
                line[i].Attributes = sbi.wAttributes;
                line[i].Char.UnicodeChar = ' ';
            }

            SMALL_RECT screenarea2{ 0, sbi.dwCursorPosition.Y, sbi.dwSize.X, sbi.dwCursorPosition.Y };
            ok = WriteConsoleOutputW(hOutput, line.get(), COORD{ SHORT(width), 1 }, COORD{ SHORT(inputLineOffset), 0 }, &screenarea2);
            assert(ok);

            COORD cpos{ 0, sbi.dwCursorPosition.Y };
            ok = SetConsoleCursorPosition(hOutput, cpos);
            assert(ok);

            promptRetracted = true;
        }
    }
}

wstring WinConsole::getInputLineToCursor()
{
    return model.getInputLineToCursor();
}

bool WinConsole::consoleGetch(wchar_t& c)
{
    // todo: remove this function once we don't need to support readline for any version of megacli on windows
    if (consolePeek())
    {
        c = model.buffer.front();
        model.buffer.erase(0, 1);
        model.newlinesBuffered = model.buffer.find(13) != string::npos;
        return true;
    }
    return false;
}
#endif
void WinConsole::readpwchar(char* pw_buf, int pw_buf_size, int* pw_buf_pos, char** line)
{
#ifdef NO_READLINE
    // todo: remove/stub this function once we don't need to support readline for any version of megacli on windows
    wchar_t c;
    if (consoleGetch(c))  // only processes once newline is buffered, so no backspace processing needed
    {
        if (c == 13)
        {
            *line = _strdup(toUtf8String(wstring(&c, 1)).c_str());
            memset(pw_buf, 0, pw_buf_size);
        }
        else if (*pw_buf_pos + 2 <= pw_buf_size)
        {
            *(wchar_t*)(pw_buf + *pw_buf_pos) = c;
            *pw_buf_pos += 2;
        }
    }
#else

    char c;
    DWORD cread;

    if (ReadConsole(GetStdHandle(STD_INPUT_HANDLE), &c, 1, &cread, NULL) == 1)
    {
        if ((c == 8) && *pw_buf_pos)
        {
            (*pw_buf_pos)--;
        }
        else if (c == 13)
        {
            *line = (char*)malloc(*pw_buf_pos + 1);
            memcpy(*line, pw_buf, *pw_buf_pos);
            (*line)[*pw_buf_pos] = 0;
        }
        else if (*pw_buf_pos < pw_buf_size)
        {
            pw_buf[(*pw_buf_pos)++] = c;
        }
    }
#endif
}

void WinConsole::setecho(bool echo)
{
#ifdef NO_READLINE
    model.echoOn = echo;
#else

    HANDLE hCon = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;

    GetConsoleMode(hCon, &mode);

    if (echo)
    {
        mode |= ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT;
    }
    else
    {
        mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT);
    }

    SetConsoleMode(hCon, mode);
#endif
}

#ifdef NO_READLINE
void WinConsole::redrawPromptIfLoggingOccurred()
{
    if (promptRetracted)
    {
        redrawInputLine();
    }
}

void WinConsole::updateInputPrompt(const std::string& newprompt)
{
    cout << std::flush;
    currentPrompt = newprompt;
    redrawInputLine();
}

char* WinConsole::checkForCompletedInputLine()
{
    if (rdbuf && rdbuf->logstyle != WinConsole::no_log)
    {
        rdbuf->logfile << flush;
    }
    redrawPromptIfLoggingOccurred();
    if (consolePeek())
    {
        std::wstring ws;
        if (model.checkForCompletedInputLine(ws))
        {

            if (rdbuf && rdbuf->logstyle == WinConsole::utf16_log)
            {
                std::wstring wprompt = toUtf16String(currentPrompt);
                rdbuf->logfile.write((const char*)wprompt.data(), wprompt.size() * sizeof(wchar_t));
                rdbuf->logfile.write((const char*)ws.data(), ws.size() * sizeof(wchar_t));
                rdbuf->logfile.write((const char*)(const wchar_t*)L"\n", sizeof(wchar_t));
            }

            string u8s = toUtf8String(ws);

            if (rdbuf && rdbuf->logstyle == WinConsole::utf8_log)
            {
                rdbuf->logfile << currentPrompt << u8s << "\n";
            }
            else if (rdbuf && rdbuf->logstyle == WinConsole::codepage_log)
            {
                rdbuf->logfile << currentPrompt << toUtf8String(ws, rdbuf->codepage) << "\n";
            }

            currentPrompt.clear();

            return _strdup(u8s.c_str());
        }
    }
    return NULL;
}

void WinConsole::clearScreen()
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    BOOL ok = GetConsoleScreenBufferInfo(hOutput, &csbi);
    assert(ok);
    if (ok)
    {
        // Fill the entire buffer with spaces 
        DWORD count;
        ok = FillConsoleOutputCharacter(hOutput, (TCHAR) ' ', csbi.dwSize.X *csbi.dwSize.Y, { 0, 0 }, &count);
        assert(ok);

        // Fill the entire buffer with the current colors and attributes 
        ok = FillConsoleOutputAttribute(hOutput, csbi.wAttributes, csbi.dwSize.X *csbi.dwSize.Y, { 0, 0 }, &count);
        assert(ok);
    }
    ok = SetConsoleCursorPosition(hOutput, { 0, 0 });
    assert(ok);
    currentPrompt.clear();
}

void WinConsole::outputHistory()
{
    for (size_t i = model.inputHistory.size(); i--; )
    {
        std::cout << toUtf8String(model.inputHistory[i]) << std::endl;
    }
}

bool WinConsole::log(const std::string& filename, logstyle logstyle)
{
    return rdbuf->log(filename, logstyle);
}
#endif
} // namespace
