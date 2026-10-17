#include "PreCompile.h"
#include "AutoStart.h"
#include "AltTab.h"
#include "Logger.h"
#include "version.h"
#include <comdef.h>
#include <lmcons.h>
#include <taskschd.h>

#pragma comment(lib, "taskschd.lib")
#pragma comment(lib, "comsupp.lib")

namespace {

const DWORD USERNAME_DOMAIN_LEN = DNLEN + UNLEN + 2; // Domain Name + '\' + User Name + '\0'
const DWORD USERNAME_LEN = UNLEN + 1;                // User Name + '\0'

std::wstring BuildTaskName() {
    WCHAR username[USERNAME_LEN] = {};
    if (GetEnvironmentVariable(L"USERNAME", username, USERNAME_LEN) == 0) {
        return L"";
    }
    return std::wstring(L"Autorun for ") + username;
}

const wchar_t* REGISTRY_RUN_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

} // namespace

void RemoveStartupFromRegistry() {
    HKEY hKey = nullptr;
    LONG result = RegOpenKeyEx(HKEY_CURRENT_USER, REGISTRY_RUN_KEY, 0, KEY_SET_VALUE, &hKey);
    if (result == ERROR_SUCCESS) {
        RegDeleteValue(hKey, AT_PRODUCT_NAMEW);
        RegCloseKey(hKey);
    }
}

bool RunAtStartup(const bool runAtStartup, const bool withHighestPrivileges) {
    const bool isElevated = g_GeneralSettings.IsProcessElevated;
    AT_LOG_INFO("runAtStartup: %d, IsProcessElevated: %d", runAtStartup, isElevated);

    // AltTab now uses a Task Scheduler task; always drop any legacy registry value.
    HKEY hKey = nullptr;
    LONG result = RegOpenKeyEx(HKEY_CURRENT_USER, REGISTRY_RUN_KEY, 0, KEY_SET_VALUE, &hKey);
    if (result == ERROR_SUCCESS) {
        RegDeleteValue(hKey, AT_PRODUCT_NAMEW);
        RegCloseKey(hKey);
    }

    bool succeeded = false;
    if (runAtStartup) {
        DeleteAutoStartTask();
        if (isElevated && withHighestPrivileges) {
            succeeded = CreateAutoStartTask(true);
        } else {
            succeeded = CreateAutoStartTask(false);
        }
    } else {
        if (IsAutoStartTaskActive()) {
            succeeded = DeleteAutoStartTask();
        } else {
            succeeded = true; // No task to delete.
        }
    }

    return succeeded;
}

bool IsRunAtStartup() {
    if (IsAutoStartTaskActive()) {
        AT_LOG_INFO("Auto start task exists for this user.");
        return true;
    }

    HKEY hKey = nullptr;
    LONG result = RegOpenKeyEx(HKEY_CURRENT_USER, REGISTRY_RUN_KEY, 0, KEY_READ, &hKey);
    if (result != ERROR_SUCCESS) {
        return false;
    }

    DWORD dataType = 0;
    DWORD dataSize = 0;
    result = RegQueryValueExW(hKey, AT_PRODUCT_NAMEW, nullptr, &dataType, nullptr, &dataSize);
    if (result != ERROR_SUCCESS || dataType != REG_SZ) {
        RegCloseKey(hKey);
        return false;
    }

    std::wstring value;
    if (dataSize > sizeof(wchar_t)) {
        value.resize(dataSize / sizeof(wchar_t));
        result = RegQueryValueExW(
            hKey,
            AT_PRODUCT_NAMEW,
            nullptr,
            nullptr,
            reinterpret_cast<LPBYTE>(&value[0]),
            &dataSize);

        // Strip the terminating null written by the registry.
        const size_t nulPos = value.find(L'\0');
        value.resize(nulPos == std::wstring::npos ? value.size() : nulPos);
    }
    RegCloseKey(hKey);

    if (result != ERROR_SUCCESS || value.empty()) {
        AT_LOG_INFO("Run at startup is NOT enabled.");
        return false;
    }

    // Legacy registry mechanism: migrate it to a Task Scheduler task.
    AT_LOG_INFO("Migrating legacy run-at-startup registry value to Task Scheduler.");
    if (g_GeneralSettings.IsProcessElevated) {
        return CreateAutoStartTask(true);
    }
    return CreateAutoStartTask(false);
}

// ----------------------------------------------------------------------------
// Task Scheduler helpers (based on microsoft-altTab samples; see ReadMe.txt).
// ----------------------------------------------------------------------------

#define ExitOnFailure(x, s, ...)                                                                                       \
    if (FAILED(x)) {                                                                                                   \
        goto LExit;                                                                                                    \
    }

#define ExitWithLastError(x, s, ...)                                                                                   \
    {                                                                                                                  \
        DWORD Dutil_er = ::GetLastError();                                                                             \
        x = HRESULT_FROM_WIN32(Dutil_er);                                                                              \
        if (!FAILED(x)) {                                                                                              \
            x = E_FAIL;                                                                                                \
        }                                                                                                              \
        goto LExit;                                                                                                    \
    }

#define ExitFunction()                                                                                                 \
    { goto LExit; }

bool CreateAutoStartTask(bool runElevated) {
    AT_LOG_INFO("Creating auto start task for this user. Elevated: %d", runElevated);
    HRESULT hr = S_OK;

    WCHAR username[USERNAME_LEN];
    WCHAR username_domain[USERNAME_DOMAIN_LEN];

    std::wstring wstrTaskName = BuildTaskName();
    if (wstrTaskName.empty()) {
        return false;
    }

    ITaskService*       pService           = nullptr;
    ITaskFolder*        pTaskFolder        = nullptr;
    ITaskDefinition*    pTask              = nullptr;
    IRegistrationInfo*  pRegInfo           = nullptr;
    ITaskSettings*      pSettings          = nullptr;
    ITriggerCollection* pTriggerCollection = nullptr;
    IRegisteredTask*    pRegisteredTask    = nullptr;

    // ------------------------------------------------------
    // Get the Domain/Username for the trigger.
    if (!GetEnvironmentVariable(L"USERNAME", username, USERNAME_LEN)) {
        ExitWithLastError(hr, "Getting username failed: %x", hr);
    }
    if (!GetEnvironmentVariable(L"USERDOMAIN", username_domain, USERNAME_DOMAIN_LEN)) {
        ExitWithLastError(hr, "Getting the user's domain failed: %x", hr);
    }
    wcscat_s(username_domain, L"\\");
    wcscat_s(username_domain, username);

    // Get the executable path.
    WCHAR wszExecutablePath[MAX_PATH];
    GetModuleFileName(nullptr, wszExecutablePath, MAX_PATH);

    // ------------------------------------------------------
    // Create an instance of the Task Service.
    hr = CoCreateInstance(
        CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_ITaskService, reinterpret_cast<void**>(&pService));
    ExitOnFailure(hr, "Failed to create an instance of ITaskService: %x", hr);

    // Connect to the task service.
    hr = pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
    ExitOnFailure(hr, "ITaskService::Connect failed: %x", hr);

    // ------------------------------------------------------
    // Get the AltTab task folder. Creates it if it doesn't exist.
    hr = pService->GetFolder(_bstr_t(L"\\AltTab"), &pTaskFolder);
    if (FAILED(hr)) {
        ITaskFolder* pRootFolder = nullptr;
        hr = pService->GetFolder(_bstr_t(L"\\"), &pRootFolder);
        ExitOnFailure(hr, "Cannot get Root Folder pointer: %x", hr);
        hr = pRootFolder->CreateFolder(_bstr_t(L"\\AltTab"), _variant_t(L""), &pTaskFolder);
        pRootFolder->Release();
        ExitOnFailure(hr, "Cannot create AltTab task folder: %x", hr);
    }

    // If the task exists, just enable it.
    {
        IRegisteredTask* pExistingRegisteredTask = nullptr;
        hr = pTaskFolder->GetTask(_bstr_t(wstrTaskName.c_str()), &pExistingRegisteredTask);
        if (SUCCEEDED(hr)) {
            hr = pExistingRegisteredTask->put_Enabled(VARIANT_TRUE);
            pExistingRegisteredTask->Release();
            if (SUCCEEDED(hr)) {
                ExitFunction();
            }
        }
    }

    // Create the task builder object.
    hr = pService->NewTask(0, &pTask);
    ExitOnFailure(hr, "Failed to create a task definition: %x", hr);

    // ------------------------------------------------------
    // Registration info.
    hr = pTask->get_RegistrationInfo(&pRegInfo);
    ExitOnFailure(hr, "Cannot get identification pointer: %x", hr);
    hr = pRegInfo->put_Author(_bstr_t(username_domain));
    ExitOnFailure(hr, "Cannot put identification info: %x", hr);

    // ------------------------------------------------------
    // Task settings.
    hr = pTask->get_Settings(&pSettings);
    ExitOnFailure(hr, "Cannot get settings pointer: %x", hr);

    hr = pSettings->put_StartWhenAvailable(VARIANT_FALSE);
    ExitOnFailure(hr, "Cannot put_StartWhenAvailable setting info: %x", hr);
    hr = pSettings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
    ExitOnFailure(hr, "Cannot put_StopIfGoingOnBatteries setting info: %x", hr);
    hr = pSettings->put_ExecutionTimeLimit(_bstr_t(L"PT0S")); // Unlimited
    ExitOnFailure(hr, "Cannot put_ExecutionTimeLimit setting info: %x", hr);
    hr = pSettings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
    ExitOnFailure(hr, "Cannot put_DisallowStartIfOnBatteries setting info: %x", hr);
    hr = pSettings->put_Priority(4);
    ExitOnFailure(hr, "Cannot put_Priority setting info : %x", hr);

    // ------------------------------------------------------
    // Logon trigger with a small delay so explorer is up.
    hr = pTask->get_Triggers(&pTriggerCollection);
    ExitOnFailure(hr, "Cannot get trigger collection: %x", hr);

    {
        ITrigger* pTrigger = nullptr;
        ILogonTrigger* pLogonTrigger = nullptr;
        hr = pTriggerCollection->Create(TASK_TRIGGER_LOGON, &pTrigger);
        ExitOnFailure(hr, "Cannot create the trigger: %x", hr);

        hr = pTrigger->QueryInterface(IID_ILogonTrigger, reinterpret_cast<void**>(&pLogonTrigger));
        pTrigger->Release();
        ExitOnFailure(hr, "QueryInterface call failed for ILogonTrigger: %x", hr);

        hr = pLogonTrigger->put_Id(_bstr_t(L"Trigger1"));
        hr = pLogonTrigger->put_Delay(_bstr_t(L"PT03S"));
        hr = pLogonTrigger->put_UserId(_bstr_t(username_domain));
        pLogonTrigger->Release();
        ExitOnFailure(hr, "Cannot add user ID to logon trigger: %x", hr);
    }

    // ------------------------------------------------------
    // Executable action.
    {
        IActionCollection* pActionCollection = nullptr;
        IAction* pAction = nullptr;
        IExecAction* pExecAction = nullptr;

        hr = pTask->get_Actions(&pActionCollection);
        ExitOnFailure(hr, "Cannot get Task collection pointer: %x", hr);

        hr = pActionCollection->Create(TASK_ACTION_EXEC, &pAction);
        pActionCollection->Release();
        ExitOnFailure(hr, "Cannot create the action: %x", hr);

        hr = pAction->QueryInterface(IID_IExecAction, reinterpret_cast<void**>(&pExecAction));
        pAction->Release();
        ExitOnFailure(hr, "QueryInterface call failed for IExecAction: %x", hr);

        hr = pExecAction->put_Path(_bstr_t(wszExecutablePath));
        pExecAction->Release();
        ExitOnFailure(hr, "Cannot set path of executable: %x", hr);
    }

    // ------------------------------------------------------
    // Principal (run level).
    {
        IPrincipal* pPrincipal = nullptr;
        hr = pTask->get_Principal(&pPrincipal);
        ExitOnFailure(hr, "Cannot get principal pointer: %x", hr);

        hr = pPrincipal->put_Id(_bstr_t(L"Principal1"));
        hr = pPrincipal->put_UserId(_bstr_t(username_domain));
        hr = pPrincipal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN);
        hr = pPrincipal->put_RunLevel(runElevated ? TASK_RUNLEVEL_HIGHEST : TASK_RUNLEVEL_LUA);
        pPrincipal->Release();
        ExitOnFailure(hr, "Cannot put principal run level: %x", hr);
    }

    // ------------------------------------------------------
    // Register the task.
    {
        _variant_t SDDL_FULL_ACCESS_FOR_EVERYONE = L"D:(A;;FA;;;WD)";
        hr = pTaskFolder->RegisterTaskDefinition(
            _bstr_t(wstrTaskName.c_str()),
            pTask,
            TASK_CREATE_OR_UPDATE,
            _variant_t(username_domain),
            _variant_t(),
            TASK_LOGON_INTERACTIVE_TOKEN,
            SDDL_FULL_ACCESS_FOR_EVERYONE,
            &pRegisteredTask);
        ExitOnFailure(hr, "Error saving the Task : %x", hr);
    }

LExit:
    if (pService)
        pService->Release();
    if (pTaskFolder)
        pTaskFolder->Release();
    if (pTask)
        pTask->Release();
    if (pRegInfo)
        pRegInfo->Release();
    if (pSettings)
        pSettings->Release();
    if (pTriggerCollection)
        pTriggerCollection->Release();
    if (pRegisteredTask)
        pRegisteredTask->Release();

    return SUCCEEDED(hr);
}

bool DeleteAutoStartTask() {
    AT_LOG_INFO("Deleting auto start task for this user.");
    HRESULT hr = S_OK;

    std::wstring wstrTaskName = BuildTaskName();
    if (wstrTaskName.empty()) {
        return false;
    }

    ITaskService* pService = nullptr;
    ITaskFolder* pTaskFolder = nullptr;

    hr = CoCreateInstance(
        CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_ITaskService, reinterpret_cast<void**>(&pService));
    ExitOnFailure(hr, "Failed to create an instance of ITaskService: %x", hr);

    hr = pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
    ExitOnFailure(hr, "ITaskService::Connect failed: %x", hr);

    hr = pService->GetFolder(_bstr_t(L"\\AltTab"), &pTaskFolder);
    if (FAILED(hr)) {
        // Folder doesn't exist. No need to delete a non-existing task.
        hr = S_OK;
        ExitFunction();
    }

    hr = pTaskFolder->DeleteTask(_bstr_t(wstrTaskName.c_str()), 0);

LExit:
    if (pService)
        pService->Release();
    if (pTaskFolder)
        pTaskFolder->Release();

    return SUCCEEDED(hr);
}

bool IsAutoStartTaskActive() {
    AT_LOG_INFO("Checking if auto start task is active for this user.");
    HRESULT hr = S_OK;

    std::wstring wstrTaskName = BuildTaskName();
    if (wstrTaskName.empty()) {
        return false;
    }

    ITaskService* pService = nullptr;
    ITaskFolder* pTaskFolder = nullptr;

    hr = CoCreateInstance(
        CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_ITaskService, reinterpret_cast<void**>(&pService));
    ExitOnFailure(hr, "Failed to create an instance of ITaskService: %x", hr);

    hr = pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
    ExitOnFailure(hr, "ITaskService::Connect failed: %x", hr);

    hr = pService->GetFolder(_bstr_t(L"\\AltTab"), &pTaskFolder);
    ExitOnFailure(hr, "ITaskFolder doesn't exist: %x", hr);

    {
        IRegisteredTask* pExistingRegisteredTask = nullptr;
        hr = pTaskFolder->GetTask(_bstr_t(wstrTaskName.c_str()), &pExistingRegisteredTask);
        if (SUCCEEDED(hr)) {
            VARIANT_BOOL is_enabled = VARIANT_FALSE;
            hr = pExistingRegisteredTask->get_Enabled(&is_enabled);
            pExistingRegisteredTask->Release();
            if (SUCCEEDED(hr)) {
                hr = (is_enabled == VARIANT_TRUE) ? S_OK : E_FAIL;
                ExitFunction();
            }
        }
    }

LExit:
    if (pService)
        pService->Release();
    if (pTaskFolder)
        pTaskFolder->Release();

    return SUCCEEDED(hr);
}

bool IsTaskRunWithHighestPrivileges() {
    HRESULT hr = S_OK;

    std::wstring wstrTaskName = BuildTaskName();
    if (wstrTaskName.empty()) {
        return false;
    }

    bool result = false;
    ITaskService* pService = nullptr;
    ITaskFolder* pTaskFolder = nullptr;
    ITaskFolder* pRootFolder = nullptr;
    IRegisteredTask* pTask = nullptr;
    ITaskDefinition* pDef = nullptr;
    IPrincipal* pPrincipal = nullptr;

    hr = CoCreateInstance(
        CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_ITaskService, reinterpret_cast<void**>(&pService));
    ExitOnFailure(hr, "Failed to create an instance of ITaskService: %x", hr);

    hr = pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
    ExitOnFailure(hr, "ITaskService::Connect failed: %x", hr);

    hr = pService->GetFolder(_bstr_t(L"\\AltTab"), &pTaskFolder);
    if (FAILED(hr)) {
        hr = pService->GetFolder(_bstr_t(L"\\"), &pRootFolder);
        ExitOnFailure(hr, "Cannot get Root Folder pointer: %x", hr);
        hr = pRootFolder->CreateFolder(_bstr_t(L"\\AltTab"), _variant_t(L""), &pTaskFolder);
        if (FAILED(hr)) {
            ExitOnFailure(hr, "Cannot create AltTab task folder: %x", hr);
        }
    }

    hr = pTaskFolder->GetTask(_bstr_t(wstrTaskName.c_str()), &pTask);
    if (SUCCEEDED(hr)) {
        hr = pTask->get_Definition(&pDef);
        if (SUCCEEDED(hr)) {
            hr = pDef->get_Principal(&pPrincipal);
            if (SUCCEEDED(hr)) {
                _TASK_RUNLEVEL runLevel = TASK_RUNLEVEL_LUA;
                hr = pPrincipal->get_RunLevel(&runLevel);
                if (SUCCEEDED(hr) && runLevel == TASK_RUNLEVEL_HIGHEST) {
                    result = true;
                }
            }
        }
    }

LExit:
    if (pPrincipal)
        pPrincipal->Release();
    if (pDef)
        pDef->Release();
    if (pTask)
        pTask->Release();
    if (pRootFolder)
        pRootFolder->Release();
    if (pTaskFolder)
        pTaskFolder->Release();
    if (pService)
        pService->Release();
    return result;
}