#include <mega/fuse/platform/shell.h>
#include <mega/fuse/platform/windows.h>
#include <mega/scoped_helpers.h>
#include <mega/utils.h>

// Disable for the including order
// clang-format off
#include <ExDisp.h>
#include <ShlObj.h>
#include <ShlObj_core.h>
#include <ShObjIdl.h>

// clang-format on

namespace mega
{
namespace fuse
{
namespace platform
{
namespace shell
{

template<typename T>
class ComPtr
{
public:
    ComPtr() = default;

    ComPtr(const ComPtr&) = delete;

    ComPtr& operator=(ComPtr&) = delete;

    bool operator!() const
    {
        return (p == nullptr);
    }

    T* operator->() const
    {
        return p;
    }

    // Avoid misuse. Take its address only when p is nullptr
    T** operator&()
    {
        assert(p == nullptr);
        return &p;
    }

    T* get() const
    {
        return p;
    }

    ~ComPtr()
    {
        if (p)
            p->Release();
        p = nullptr;
    }

private:
    T* p{nullptr};
};

bool isMatchedShell(IShellView* shellView, const Prefixes& prefixes)
{
    ComPtr<IFolderView> folderView;
    if (FAILED(shellView->QueryInterface(IID_PPV_ARGS(&folderView))))
        return false;

    ComPtr<IPersistFolder2> persistFolder;
    if (FAILED(folderView->GetFolder(IID_PPV_ARGS(&persistFolder))))
        return false;

    LPITEMIDLIST idl = nullptr;
    if (FAILED(persistFolder->GetCurFolder(&idl)))
        return false;

    const auto idlReleaser = makeScopedDestructor(
        [&idl]()
        {
            CoTaskMemFree(idl);
            idl = nullptr;
        });

    WCHAR szPath[MAX_PATH];
    if (!SHGetPathFromIDListW(idl, szPath))
        return false;

    const auto p = std::wstring(szPath);
    auto isStartedWith = [&p](const std::wstring& prefix)
    {
        return Utils::startswith(p, prefix);
    };
    return std::any_of(prefixes.begin(), prefixes.end(), isStartedWith);
}

void setToListView(IShellView* shellView)
{
    // Query IFolderView2
    ComPtr<IFolderView2> folderView2;
    auto hr = shellView->QueryInterface(IID_PPV_ARGS(&folderView2));
    if (FAILED(hr))
        return;

    // Set view mode to List View
    folderView2->SetViewModeAndIconSize(FVM_LIST, -1);
}

bool init()
{
    return !FAILED(CoInitialize(NULL));
}

void setView(const Prefixes& prefixes)
{
    // Get the desktop Shell windows interface
    ComPtr<IShellWindows> windows;
    auto hr = CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&windows));
    if (FAILED(hr))
        return;

    // Iterate through the shell windows to find one that's a folder
    long count = 0;
    windows->get_Count(&count);
    for (long i = 0; i < count; i++)
    {
        // Get the window's IDispath interface
        ComPtr<IDispatch> disp;
        VARIANT v = {{{VT_I4}}};
        v.lVal = i;
        windows->Item(v, &disp);
        if (!disp)
            continue;

        // Get the IServiceProvider interface from the window
        ComPtr<IServiceProvider> serviceProvider;
        if (FAILED(disp->QueryInterface(IID_PPV_ARGS(&serviceProvider))))
            continue;

        // Get IShellBrowser
        ComPtr<IShellBrowser> shellBrowser;
        if (FAILED(
                serviceProvider->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&shellBrowser))))
            continue;

        // Get the active IShellView
        ComPtr<IShellView> shellView;
        if (FAILED(shellBrowser->QueryActiveShellView(&shellView)))
            continue;

        if (!isMatchedShell(shellView.get(), prefixes))
            continue;

        setToListView(shellView.get());
    }
}

void uninit()
{
    CoUninitialize();
}
} // shell
} // platform
} // fuse
} // mega
