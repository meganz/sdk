/**
 * @file win32/drivenotifywin.cpp
 * @brief Mega SDK various utilities and helper classes
 *
 * (c) 2013-2020 by Mega Limited, Auckland, New Zealand
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

#ifdef USE_DRIVE_NOTIFICATIONS


#include <combaseapi.h>
#include <comutil.h>
#include <wbemcli.h>
#include <wrl/client.h> // ComPtr
#include <unknwn.h>

#include "mega/drivenotify.h"

#include <sstream>
#include <iomanip>

using namespace std;
using namespace Microsoft::WRL; // ComPtr



namespace mega {

    // Class containing COM initialization code, and common property reading code for WMI.
    // Not really useful on its own.
    class WinWmi
    {
    public:
        // Remember to call CoUninitialize() if this succeeded.
        static bool InitializeCom();

        // Remember to call (*ppLocator)->Release() and (*ppService)->Release() if this succeeded.
        static bool GetWbemService(IWbemLocator** ppLocator, IWbemServices** ppService, const wstring& wmiNamespace = L"ROOT\\CIMV2");

        static DriveInfo GetVolumeProperties(IWbemClassObject* pQueryObject);

        static uint32_t GetUi32Property(IWbemClassObject* pQueryObject, const wstring& name);
        static wstring GetStringProperty(IWbemClassObject* pQueryObject, const wstring& name);

        static wstring EscapeWql(const wstring& wql);
    };



    //
    // DriveNotifyWin
    /////////////////////////////////////////////

    bool DriveNotifyWin::startNotifier()
    {
        if (mEventSinkThread.joinable() || mStop.load())  return false;

        mEventSinkThread = thread(&DriveNotifyWin::doInThread, this);

        return true;
    }



    void DriveNotifyWin::stopNotifier()
    {
        if (!mEventSinkThread.joinable())  return;

        mStop.store(true);
        mEventSinkThread.join();
        mStop.store(false); // allow reusing this instance
    }



    bool DriveNotifyWin::doInThread()
    {
        // init com
        if (!WinWmi::InitializeCom())  return false;

        IWbemLocator* pLocator = nullptr;
        IWbemServices* pService = nullptr;
        if (!WinWmi::GetWbemService(&pLocator, &pService)) { CoUninitialize(); return false; }

        // BSTR is wchar_t*. Use the latter to avoid including even more obscure headers.
        wchar_t foolBstrWql[] = L"WQL";
        wchar_t* bstrWql = foolBstrWql; // avoid compiler warning

        // build query
        wchar_t foolBstrQuery[] = L"Select * From __InstanceOperationEvent "
                                  L" Within 3 Where"
                                  L"     TargetInstance isa 'Win32_LogicalDisk'"
                                  L"     and (__CLASS='__InstanceCreationEvent'"      // added a drive
                                  L"       or __CLASS='__InstanceDeletionEvent')";    // removed a drive
        wchar_t* bstrQuery = foolBstrQuery; // avoid compiler warning

        // run query
        IEnumWbemClassObject* pEnumerator = nullptr;
        HRESULT result = pService->ExecNotificationQuery(
            bstrWql,                                                  // strQueryLanguage
            bstrQuery,                                                // strQuery
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,    // lFlags
            nullptr,                                                  // pCtx
            &pEnumerator);                                            // ppEnum

        if (FAILED(result))
        {
            pService->Release();
            pLocator->Release();
            CoUninitialize();

            return false;
        }

        // fetch results
        IWbemClassObject* pQueryObject = nullptr; // keep it outside the loop, to *not* be initialized every time
        ULONG returnedObjectCount = 0;
        while (pEnumerator && !mStop.load())
        {
            // poll for one event at a time
            result = pEnumerator->Next(500 /*ms*/, 1, &pQueryObject, &returnedObjectCount);

            if (!returnedObjectCount)  continue; // no event so far

            // get event properties
            VARIANT evPropVariant;
            VariantInit(&evPropVariant);
            CIMTYPE propType = CIM_ILLEGAL;
            long propFlavor = 0;

            // determine the event type
            const wstring& eventClass = WinWmi::GetStringProperty(pQueryObject, L"__CLASS");
            if (eventClass.empty())  continue; // ignore any errors

            EventType eventType = eventClass == L"__InstanceCreationEvent" ? DRIVE_CONNECTED_EVENT :
                                  eventClass == L"__InstanceDeletionEvent" ? DRIVE_DISCONNECTED_EVENT :
                                                                             UNKNOWN_EVENT;

            if (eventType == UNKNOWN_EVENT)  continue; // ignore

            // get the object containing the reference to the drive properties object
            result = pQueryObject->Get(L"TargetInstance", 0, &evPropVariant, &propType, &propFlavor);
            if (FAILED(result))  continue; // ignore any errors
            if (propType != CIM_OBJECT || evPropVariant.vt != VT_UNKNOWN)
            {
                VariantClear(&evPropVariant);
                continue;
            }

            // get the object containing drive properties
            IUnknown* targetInst = evPropVariant.punkVal;
            ComPtr<IWbemClassObject> pDriveInfo;
            result = targetInst->QueryInterface(IID_IWbemClassObject, (void**)pDriveInfo.GetAddressOf());
            if (FAILED(result))
            {
                VariantClear(&evPropVariant);
                continue;
            }

            // get drive properties
            DriveInfo p = WinWmi::GetVolumeProperties(pDriveInfo.Get());
            p.connected = eventType == DRIVE_CONNECTED_EVENT;

            add(move(p));

            VariantClear(&evPropVariant);
        }

        if (pQueryObject)  pQueryObject->Release();
        if (pEnumerator)  pEnumerator->Release();
        pService->Release();
        pLocator->Release();
        CoUninitialize();

        return SUCCEEDED(result);
    }



    //
    // UniqueDriveIdWin
    /////////////////////////////////////////////

    map<int, string> UniqueDriveIdWin::getIds(const string& mountPoint)
    {
        map<int, string> ids;

        if (mountPoint.empty())  return ids;

        // make sure this string does not contain embeded null characters
        // (i.e. if received from LocalPath::platformEncoded() on Windows)
        assert(mountPoint.find('\0') == string::npos);

        // convert mountPoint to wide string (dumb conversion)
        wstring wMountPoint;
        for (auto c : mountPoint)  wMountPoint += c;

        // init COM
        if (!WinWmi::InitializeCom())  return ids;
        IWbemLocator* pLocator = nullptr;
        IWbemServices* pService = nullptr;
        if (!WinWmi::GetWbemService(&pLocator, &pService)) { CoUninitialize(); return ids; }

        // get partition id: VolumeSerialNumber
        wstring query = L"SELECT __PATH, VolumeSerialNumber from Win32_LogicalDisk where DeviceID = \"" + wMountPoint + L'"';
        auto values = getWqlValues(pService, query, { L"__PATH", L"VolumeSerialNumber" });
        // save it as narrow string
        string& vsn = ids[UniqueDriveId::VOLUME_SN];
        for (wchar_t c : values[1])  vsn += char(c);

        // get Win32_LogicalDiskToPartition.Antecedent
        query = L"SELECT Antecedent from Win32_LogicalDiskToPartition where Dependent = \"" + WinWmi::EscapeWql(values[0]) + L'"';
        values = getWqlValues(pService, query, { L"Antecedent" });

        // get Win32_DiskDriveToDiskPartition.Antecedent
        query = L"SELECT Antecedent from Win32_DiskDriveToDiskPartition where Dependent = \"" + WinWmi::EscapeWql(values[0]) + L'"';
        values = getWqlValues(pService, query, { L"Antecedent" });

        // get multiple fields from Win32_DiskDrive;
        // for a numeric field, pass a function that will convert it to string
        // (SerialNumber is also available on Windows)
        query = L"SELECT Signature, PNPDeviceID from Win32_DiskDrive where __PATH = \"" + WinWmi::EscapeWql(values[0]) + L'"';
        auto f = [this](IWbemClassObject* o, const wstring& n) { return convertUi32ToB16str(o, n); };
        vector<function<wstring(IWbemClassObject*, const wstring&)>> convFuncs = { f };
        values = getWqlValues(pService, query, { L"Signature", L"PNPDeviceID" }, &convFuncs);

        // extract disk id from Win32_DiskDrive.PNPDeviceID, and save it as narrow string
        string& di = ids[UniqueDriveId::DISK_ID];
        const wstring& wdi = getIdFromPNPDevId(values[1]);
        for (wchar_t c : wdi)  di += char(c);

        // get disk signature, or its GUID
        if (values[0].empty())
        {
            // disk has GPT partition table, go for its GUID
            // reset the WMI connection to a different namespace
            pService->Release();  pService = nullptr;
            pLocator->Release();  pLocator = nullptr;
            const wstring& wmiNamespace = L"ROOT\\Microsoft\\Windows\\Storage";
            if (!WinWmi::GetWbemService(&pLocator, &pService, wmiNamespace)) { CoUninitialize(); return ids; }

            // get the drive letter, as upper case, without the semicolon
            const wchar_t l = wchar_t(toupper(wMountPoint[0]));

            // get the __PATH for the given mount point
            query = wstring(L"SELECT __RELPATH from MSFT_Volume where DriveLetter = '") + l + L'\'';
            values = getWqlValues(pService, query, { L"__RELPATH" });

            // get MSFT_PartitionToVolume.Partition
            const wstring& prefix = L"\\\\.\\" + wmiNamespace + L':';
            query = L"SELECT Partition from MSFT_PartitionToVolume where Volume = \"" + WinWmi::EscapeWql(prefix) + WinWmi::EscapeWql(values[0]) + L'"';
            values = getWqlValues(pService, query, { L"Partition" });

            // get MSFT_DiskToPartition.Disk
            query = L"SELECT Disk from MSFT_DiskToPartition where Partition = \"" + WinWmi::EscapeWql(values[0]) + L'"';
            values = getWqlValues(pService, query, { L"Disk" });

            // remove prefix
            if (values[0].compare(0, prefix.size(), prefix) == 0)  values[0].erase(0, prefix.size());

            // get MSFT_Disk.GUID
            // this could be forther confirmed by making sure the disk is using GPT (PartitionStyle==2), but does not seem necessary
            query = L"SELECT Guid from MSFT_Disk where __RELPATH = \"" + WinWmi::EscapeWql(values[0]) + L'"';
            values = getWqlValues(pService, query, { L"Guid" });

            // remove leading and trailing curly brackets
            if (!values[0].empty() && values[0].front() == L'{' && values[0].back() == L'}')
            {
                values[0].pop_back();
                values[0].erase(0, 1);
            }
        }
        // save it as narrow string
        string& ds = ids[UniqueDriveId::DISK_SIGNATURE];
        for (wchar_t c : values[0])  ds += char(c);

        // release COM resources
        pService->Release();
        pLocator->Release();
        CoUninitialize();

        return ids;
    }

    vector<wstring> UniqueDriveIdWin::getWqlValues(IWbemServices* pService, const wstring& query, const vector<wstring>& fields,
                                                   const vector<function<wstring(IWbemClassObject*, const wstring&)>>* convFuncs)
    {
        ComPtr<IEnumWbemClassObject> pEnumerator;

        // run query
        HRESULT result = pService->ExecQuery(
            (wchar_t*)L"WQL",                           // strQueryLanguage
            (wchar_t*)query.c_str(),                    // strQuery
            WBEM_FLAG_FORWARD_ONLY,                     // lFlags
            nullptr,                                    // pCtx
            &pEnumerator);                              // ppEnum

        vector<wstring> values(fields.size());
        if (FAILED(result) || !pEnumerator)  return values;

        // get the first row (there should be only one)
        ComPtr<IWbemClassObject> pQueryObject;
        ULONG returnedObjectCount = 0;
        result = pEnumerator->Next(100 /*ms*/, 1, pQueryObject.GetAddressOf(), &returnedObjectCount);

        if (!returnedObjectCount)  return values; // no results or error

        for (auto i = 0; i < fields.size(); ++i)
        {
            // retrieving a string value is simple;
            // but when retrieving an non-string, it needs to be converted
            values[i] = (convFuncs && convFuncs->size() > i && convFuncs->at(i)) ?
                convFuncs->at(i)(pQueryObject.Get(), fields[i]) :
                WinWmi::GetStringProperty(pQueryObject.Get(), fields[i]);
        }

        return values;
    }


    wstring UniqueDriveIdWin::convertUi32ToB16str(IWbemClassObject* queryObj, const wstring& field)
    {
        uint32_t iVal = WinWmi::GetUi32Property(queryObj, field);
        wstring sVal;

        if (iVal)
        {
            wstringstream stream;
            // pad the value with 0 to the left, and always have 8 characters
            stream << setfill(L'0') << setw(8) << hex << iVal;
            sVal = stream.str();
        }

        return sVal;
    }


    wstring UniqueDriveIdWin::getIdFromPNPDevId(const wstring& pnpIdString)
    {
        // Expected PNPDeviceId will be of the following form:
        // USBSTOR\DISK&VEN_ADATA&PROD_USB_FLASH_DRIVE&REV_1100\27C1609381310127&0
        // from which we need to extract the string after the last '\' (which
        // represents DriveId&PartitionNumber), and before the last '&', thus
        // 27C1609381310127.
        //
        // However, when an id could not be read from a device, Windows will construct
        // that last part from other different values. In this case, we should not
        // rely on that construct, which after the last '\' will have an '&' in the
        // second position, i.e.
        // SCSI\DISK&VEN_NVME&PROD_PM981A_NVME_SAMS\5&3995B453&0&000000
        wstring devId;

        // get string after last '\'
        auto pos = pnpIdString.rfind(L'\\');
        if (pos == wstring::npos || pos + 2 >= pnpIdString.size())  return devId;
        devId = pnpIdString.substr(pos + 1);

        // get string before last '&'
        pos = devId.rfind(L'&');
        if (pos == wstring::npos || count(devId.begin(), devId.end(), L'&') != 1)  devId.clear();
        else  devId.erase(pos);

        return devId;
    }



    //
    // VolumeQuery
    /////////////////////////////////////////////

    map<wstring, DriveInfo> VolumeQuery::query()
    {
        map<wstring, DriveInfo> queryResults;

        // init com
        HRESULT result = WinWmi::InitializeCom();
        if (FAILED(result))  return queryResults;

        IWbemLocator* pLocator = nullptr;
        IWbemServices* pService = nullptr;
        result = WinWmi::GetWbemService(&pLocator, &pService);

        if (FAILED(result)) { CoUninitialize(); return queryResults; }

        // BSTR is wchar_t*. Use the latter to avoid including even more obscure headers.
        wchar_t foolBstrWql[] = L"WQL";
        wchar_t* bstrWql = foolBstrWql; // avoid compiler warning

        // build query
        wchar_t foolBstrQuery[] = L"SELECT DeviceID, Description, DriveType, MediaType,"
                                        L" ProviderName, Size, SystemName, VolumeSerialNumber"
                                  L" FROM Win32_LogicalDisk";
        wchar_t* bstrQuery = foolBstrQuery;

        IEnumWbemClassObject* pEnumerator = nullptr;

        // run query
        result = pService->ExecQuery(
            bstrWql,                                    // strQueryLanguage
            bstrQuery,                                  // strQuery
            WBEM_FLAG_FORWARD_ONLY,                     // lFlags
            nullptr,                                    // pCtx
            &pEnumerator);                              // ppEnum

        if (FAILED(result))
        {
            pService->Release();
            pLocator->Release();
            CoUninitialize();

            return queryResults;
        }

        // fetch results
        while (pEnumerator)
        {
            // get one row at a time
            ComPtr<IWbemClassObject> pQueryObject;
            ULONG returnedObjectCount = 0;
            result = pEnumerator->Next(WBEM_INFINITE, 1, pQueryObject.GetAddressOf(), &returnedObjectCount);

            if (!returnedObjectCount)  break; // no more results or error

            // get properties
            const DriveInfo& p = WinWmi::GetVolumeProperties(pQueryObject.Get());
            queryResults.emplace(p.mountPoint, p);
        }

        if (pEnumerator)  pEnumerator->Release();
        pService->Release();
        pLocator->Release();
        CoUninitialize();

        return queryResults;
    }



    //
    // WinWmi
    /////////////////////////////////////////////

    DriveInfo WinWmi::GetVolumeProperties(IWbemClassObject* pQueryObject)
    {
        DriveInfo v;

        // DeviceID             // string
        v.mountPoint = GetStringProperty(pQueryObject, L"DeviceID");

        // ProviderName         // string
        v.location = GetStringProperty(pQueryObject, L"ProviderName");

        // VolumeSerialNumber   // string
        v.volumeSerialNumber = GetStringProperty(pQueryObject, L"VolumeSerialNumber");

        // Size                 // string (yup, string representation of a large number)
        v.size = GetStringProperty(pQueryObject, L"Size");

        // Description          // string
        v.description = GetStringProperty(pQueryObject, L"Description");

        // DriveType            // uint32
        v.driveType = GetUi32Property(pQueryObject, L"DriveType");

        // MediaType            // uint32
        v.mediaType = GetUi32Property(pQueryObject, L"MediaType");

        return v;
    }



    wstring WinWmi::GetStringProperty(IWbemClassObject* pQueryObject, const wstring& name)
    {
        VARIANT propVariant;
        VariantInit(&propVariant);
        wstring propStr;

        HRESULT result = pQueryObject->Get(name.c_str(), 0, &propVariant, nullptr, nullptr);
        if (FAILED(result))  return propStr;

        if (propVariant.vt != VT_NULL)
        {
            propStr = propVariant.bstrVal;
            VariantClear(&propVariant);
        }

        return propStr;
    }



    uint32_t WinWmi::GetUi32Property(IWbemClassObject* pQueryObject, const wstring& name)
    {
        VARIANT propVariant;
        VariantInit(&propVariant);

        HRESULT result = pQueryObject->Get(name.c_str(), 0, &propVariant, nullptr, nullptr);
        if (FAILED(result))  return 0;

        uint32_t prop = propVariant.vt == VT_NULL ? 0 : propVariant.uintVal;
        VariantClear(&propVariant);

        return prop;
    }



    bool WinWmi::InitializeCom()
    {
        HRESULT result = CoInitializeEx(0, COINIT_APARTMENTTHREADED);
        if (FAILED(result))  return false;

        result = CoInitializeSecurity(
            nullptr,                        // pSecDesc
            -1,                             // cAuthSvc (COM authentication)
            nullptr,                        // asAuthSvc
            nullptr,                        // pReserved1
            RPC_C_AUTHN_LEVEL_DEFAULT,      // dwAuthnLevel
            RPC_C_IMP_LEVEL_IMPERSONATE,    // dwImpLevel
            nullptr,                        // pAuthList
            EOAC_NONE,                      // dwCapabilities
            nullptr                         // Reserved
        );

        if (FAILED(result) && result != RPC_E_TOO_LATE)
        {
            CoUninitialize();

            return false;
        }

        return true;
    }



    bool WinWmi::GetWbemService(IWbemLocator** ppLocator, IWbemServices** ppService, const wstring& wmiNamespace)
    {
        HRESULT result = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER,
            IID_IWbemLocator, reinterpret_cast<LPVOID*>(ppLocator));

        if (FAILED(result))  return false;

        // BSTR is wchar_t*. Use the latter to avoid including even more obscure headers.
        wchar_t* bstr = (wchar_t*)wmiNamespace.c_str();
        result = (*ppLocator)->ConnectServer(
            bstr,                       // strNetworkResource
            nullptr,                    // strUser
            nullptr,                    // strPassword
            nullptr,                    // strLocale
            0,                          // lSecurityFlags
            nullptr,                    // strAuthority
            nullptr,                    // pCtx
            ppService                   // ppNamespace
        );

        if (FAILED(result))
        {
            (*ppLocator)->Release();

            return false;
        }

        result = CoSetProxyBlanket(
            *ppService,                     // pProxy
            RPC_C_AUTHN_WINNT,              // dwAuthnSvc
            RPC_C_AUTHZ_NONE,               // dwAuthzSvc
            nullptr,                        // pServerPrincName
            RPC_C_AUTHN_LEVEL_CALL,         // dwAuthnLevel
            RPC_C_IMP_LEVEL_IMPERSONATE,    // dwImpLevel
            nullptr,                        // pAuthInfo
            EOAC_NONE                       // dwCapabilities
        );

        if (FAILED(result))
        {
            (*ppService)->Release();
            (*ppLocator)->Release();

            return false;
        }

        return true;
    }



    wstring WinWmi::EscapeWql(const wstring& wql)
    {
        // The following special characters in a query condition, must first be
        // escaped by prefixing them with a backslash:
        // - backslash (\)
        // - double quotes (")
        // - single quotes (')
        // https://docs.microsoft.com/en-us/windows/win32/wmisdk/where-clause

        wstring escWql;

        for (auto w : wql)
        {
            switch (w)
            {
            case L'\\':
            case L'\'':
            case L'"':
                escWql += L'\\';
                // [[fallthrough]]; // C++17
            default:
                escWql += w;
            }
        }

        return escWql;
    }



    //
    // Debug helpers
    /////////////////////////////////////////////

    // For each VARIANT instance in the returned map, VariantClear() should be called,
    // to avoid memory leaks.
    map<wstring, VARIANT> GetAllProperties(IWbemClassObject* pQueryObject)
    {
        map<wstring, VARIANT> allProps;

        if (!pQueryObject)  return allProps;

        wchar_t* propName = nullptr;
        VARIANT propVariant;
        CIMTYPE propType = CIM_ILLEGAL;
        long propFlavor = 0;

        HRESULT r = pQueryObject->BeginEnumeration(WBEM_FLAG_ALWAYS);
        if (FAILED(r))  return allProps;

        while (pQueryObject->Next(0, &propName, &propVariant, &propType, &propFlavor) != WBEM_S_NO_MORE_DATA)
        {
            allProps[propName] = propVariant;

            SysFreeString(propName);
            propName = nullptr;
        }

        return allProps;
    }



    wstring GetUuidQualifier(IWbemClassObject* pQueryObject)
    {
        // UUID (this one comes as a "qualifier"); not very useful though,
        // as it is the same for all events.
        // Keep this as sample code needed to get a qualifier.
        IWbemQualifierSet *pQualSet = nullptr;
        wstring uuid;
        HRESULT result = pQueryObject->GetQualifierSet(&pQualSet);

        if (FAILED(result) || !pQualSet)  return uuid;

        VARIANT qualVariant;
        long qualFlavor = 0;
        result = pQualSet->Get(L"UUID", 0, &qualVariant, &qualFlavor);
        if (FAILED(result))  return uuid;

        if (qualVariant.vt != VT_NULL)
        {
            uuid = qualVariant.bstrVal;
            VariantClear(&qualVariant);
        }

        pQualSet->Release();

        return uuid;
    }

} // namespace

#endif // USE_DRIVE_NOTIFICATIONS
