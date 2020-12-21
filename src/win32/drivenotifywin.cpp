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
        static bool GetWbemService(IWbemLocator** ppLocator, IWbemServices** ppService);

        static DriveInfo GetVolumeProperties(IWbemClassObject* pQueryObject);

        static uint32_t GetUi32Property(IWbemClassObject* pQueryObject, const wstring& name);
        static wstring GetStringProperty(IWbemClassObject* pQueryObject, const wstring& name);
    };



    //
    // DriveNotifyWin
    /////////////////////////////////////////////

    bool DriveNotifyWin::start(NotificationFunc driveDisconnected, NotificationFunc driveConnected)
    {
        if (mEventSinkThread.joinable() || (!driveDisconnected && !driveConnected))  return false;

        mEventSinkThread = thread(&DriveNotifyWin::doInThread, this, driveDisconnected, driveConnected);

        return true;
    }



    void DriveNotifyWin::stop()
    {
        if (!mEventSinkThread.joinable())  return;

        mStop.store(true);
        mEventSinkThread.join();
        mStop.store(false); // allow reusing this instance
    }



    bool DriveNotifyWin::doInThread(NotificationFunc driveConnected, NotificationFunc driveDisconnected)
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

            // send notification
            if (eventType == DRIVE_CONNECTED_EVENT && driveConnected)
            {
                p.connected = true;
                driveConnected(move(p));
            }

            else if (eventType == DRIVE_DISCONNECTED_EVENT && driveDisconnected)
            {
                driveDisconnected(move(p));
            }

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



    bool WinWmi::GetWbemService(IWbemLocator** ppLocator, IWbemServices** ppService)
    {
        HRESULT result = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER,
            IID_IWbemLocator, reinterpret_cast<LPVOID*>(ppLocator));

        if (FAILED(result))  return false;

        // BSTR is wchar_t*. Use the latter to avoid including even more obscure headers.
        wchar_t foolBstr[] = L"ROOT\\CIMV2";
        wchar_t* bstr = foolBstr; // avoid compiler warning
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
