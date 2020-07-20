#include "globalfunction.h"

#include "utility.h"
#include "version.h"
#include "epollex.h"
#include "DataTransPackage.h"
#include "SendPackageEvent.hpp"
#include "IData.h"

#include "snowflake.h"

#include "protocol/datacenter.pb.h"
#include "protocol/manager.pb.h"

using namespace std;

#define ARRAYSIZE(a) (sizeof(a) / sizeof(a[0]))

using namespace Sloong;
using namespace Sloong::Universal;
using namespace Sloong::Events;

unique_ptr<CGlobalFunction> Sloong::CGlobalFunction::Instance = make_unique<CGlobalFunction>();

LuaFunctionRegistr g_LuaFunc[] =
    {
        {"ShowLog", CGlobalFunction::Lua_ShowLog},
        {"GetEngineVer", CGlobalFunction::Lua_GetEngineVer},
        {"Base64Encode", CGlobalFunction::Lua_Base64_Encode},
        {"Base64Decode", CGlobalFunction::Lua_Base64_Decode},
        {"HashEncode", CGlobalFunction::Lua_Hash_Encode},
        {"ReloadScript", CGlobalFunction::Lua_ReloadScript},
        {"GetConfig", CGlobalFunction::Lua_GetConfig},
        {"GenUUID", CGlobalFunction::Lua_GenUUID},
        {"SetCommData", CGlobalFunction::Lua_SetCommData},
        {"GetCommData", CGlobalFunction::Lua_GetCommData},
        {"SetExtendData", CGlobalFunction::Lua_SetExtendData},
        {"SetExtendDataByFile", CGlobalFunction::Lua_SetExtendDataByFile},
        {"ConnectToDBCenter", CGlobalFunction::Lua_ConnectToDBCenter},
        {"SQLQueryToDBCenter", CGlobalFunction::Lua_SQLQueryToDBCenter},
        {"SQLInsertToDBCenter", CGlobalFunction::Lua_SQLInsertToDBCenter},
        {"SQLDeleteToDBCenter", CGlobalFunction::Lua_SQLDeleteToDBCenter},
        {"SQLUpdateToDBCenter", CGlobalFunction::Lua_SQLUpdateToDBCenter},
};

CResult Sloong::CGlobalFunction::Initialize(IControl *ic)
{
    IObject::Initialize(ic);
    IData::Initialize(ic);
    m_pModuleConfig = IData::GetModuleConfig();
    ReferenceDataCenterConnection();
    return CResult::Succeed();
}

void Sloong::CGlobalFunction::ReferenceDataCenterConnection()
{
    int manager_socket = IData::GetManagerSocket();
    if (manager_socket == INVALID_SOCKET)
    {
        CGlobalFunction::Instance->m_pLog->Error("Get manager socket error in ReferenceDataCenterConnection.");
        return;
    }

    Manager::QueryTemplateRequest request;
    request.add_templatetype(Core::MODULE_TYPE::DataCenter);
    auto package_id = snowflake::Instance->nextid();
    auto req = make_shared<CSendPackageEvent>();
    req->SetCallbackFunc(std::bind(&CGlobalFunction::OnQueryDBCenterTemplateResponse, CGlobalFunction::Instance.get(), std::placeholders::_1, std::placeholders::_2));
    req->SetRequest(manager_socket, IData::GetRuntimeData()->nodeuuid(), package_id, Base::HEIGHT_LEVEL, Manager::Functions::QueryTemplate, ConvertObjToStr(&request), "", DataPackage_PackageType::DataPackage_PackageType_RequestPackage);
    CGlobalFunction::Instance->m_iC->SendMessage(req);
}

void CGlobalFunction::OnQueryDBCenterTemplateResponse(IEvent *event, CDataTransPackage *pack)
{
    auto res_str = pack->GetRecvMessage();
    auto res = ConvertStrToObj<Manager::QueryTemplateResponse>(res_str);
    Manager::QueryNodeRequest query_node_req;
    for (auto &item : res->templateinfos())
    {
        query_node_req.add_templateid(item.id());
    }
    if (query_node_req.templateid_size() > 0)
    {
        int manager_socket = IData::GetManagerSocket();
        if (manager_socket == INVALID_SOCKET)
        {
            CGlobalFunction::Instance->m_pLog->Error("Get manager socket error in query dbcenter work node.");
            return;
        }

        auto package_id = snowflake::Instance->nextid();
        auto req = make_shared<CSendPackageEvent>();
        req->SetCallbackFunc([](IEvent *event, CDataTransPackage *pack) {
            auto res_str = pack->GetRecvMessage();
            auto res = ConvertStrToObj<Manager::QueryNodeResponse>(res_str);
            if (res->nodeinfos_size() > 0)
            {
                auto info = res->nodeinfos();
                if (CGlobalFunction::Instance->m_SocketDBCenter != nullptr)
                    return;

                CGlobalFunction::Instance->m_SocketDBCenter = make_unique<EasyConnect>();
                CGlobalFunction::Instance->m_pLog->Verbos(Helper::Format("Try connect to datacenter[%s:%d]", info[0].address().c_str(), info[0].port()));
                CGlobalFunction::Instance->m_SocketDBCenter->Initialize(info[0].address(), info[0].port());
                if (!CGlobalFunction::Instance->m_SocketDBCenter->Connect())
                {
                    CGlobalFunction::Instance->m_pLog->Error("Connect to datacenter fialed.");
                    return;
                }
                auto reg_event = make_shared<CNetworkEvent>(EVENT_TYPE::RegisteConnection);
                reg_event->SetSocketID(CGlobalFunction::Instance->m_SocketDBCenter->GetSocketID());
                CGlobalFunction::Instance->m_iC->SendMessage(reg_event);
            }
        });
        req->SetRequest(manager_socket, IData::GetRuntimeData()->nodeuuid(), package_id, Base::HEIGHT_LEVEL, Manager::Functions::QueryNode, ConvertObjToStr(&query_node_req), "", DataPackage_PackageType::DataPackage_PackageType_RequestPackage);
        CGlobalFunction::Instance->m_iC->SendMessage(req);
    }
}

void Sloong::CGlobalFunction::RegistFuncToLua(CLua *pLua)
{
    vector<LuaFunctionRegistr> funcList(g_LuaFunc, g_LuaFunc + ARRAYSIZE(g_LuaFunc));
    pLua->AddFunctions(&funcList);
}

int Sloong::CGlobalFunction::Lua_GetEngineVer(lua_State *l)
{
    CLua::PushString(l, VERSION_TEXT);
    return 1;
}

int Sloong::CGlobalFunction::Lua_Base64_Encode(lua_State *l)
{
    string req = CLua::GetString(l, 1, "");
    string res = CBase64::Encode(req);
    CLua::PushString(l, res);
    return 1;
}

int Sloong::CGlobalFunction::Lua_Base64_Decode(lua_State *l)
{
    string req = CLua::GetString(l, 1, "");
    string res = CBase64::Decode(req);
    CLua::PushString(l, res);
    return 1;
}

/**
 * @Remarks: 
 * @Params: 1 string > string data. if file mode, is the file path
 * 			2 string > hash type. support : 0(MD5), 1(SHA-1),2(SHA-256), 3(SHA-512). default by SHA-1
 * 			3 boolen > file mode. default by false
 * @Return: Hash result
 */
int Sloong::CGlobalFunction::Lua_Hash_Encode(lua_State *l)
{
    string data = CLua::GetString(l, 1, "");
    int hash_type = CLua::GetInteger(l, 2, 1);
    bool file_mode = CLua::GetBoolen(l, 3);
    string result("");

    switch (hash_type)
    {
    case HashType::MD5:
        result = CMD5::Encode(data, file_mode);
        break;
    case HashType::SHA_1:
        result = CSHA1::Encode(data, file_mode);
        break;
    case HashType::SHA_256:
        result = CSHA256::Encode(data, file_mode);
        break;
    case HashType::SHA_512:
        result = CSHA512::Encode(data, file_mode);
        break;
    default:
        result = "Hash type error. support : 0(MD5), 1(SHA-1),2(SHA-256), 3(SHA-512).";
        break;
    }
    CLua::PushString(l, result);
    return 1;
}

int Sloong::CGlobalFunction::Lua_ReloadScript(lua_State *l)
{
    CGlobalFunction::Instance->m_iC->SendMessage(EVENT_TYPE::ReloadLuaContext);
    return 0;
}

int Sloong::CGlobalFunction::Lua_GetConfig(lua_State *l)
{
    string section = CLua::GetString(l, 1);
    string key = CLua::GetString(l, 2);
    string def = CLua::GetString(l, 3);
    auto config = CGlobalFunction::Instance->m_pModuleConfig;
    if (config->isMember(section) && (*config)[section].isMember(key))
    {
        CLua::PushString(l, (*config)[section][key].asString());
    }
    else
    {
        CLua::PushString(l, def);
    }
    return 1;
}

int CGlobalFunction::Lua_GenUUID(lua_State *l)
{
    CLua::PushString(l, CUtility::GenUUID());
    return 1;
}

int CGlobalFunction::Lua_ShowLog(lua_State *l)
{
    string luaTitle = CLua::GetString(l, 2, "Info");
    string msg = CLua::GetString(l, 1);
    if (msg == "")
        return 0;
    else
        msg = Helper::Format("[Script]:[%s]", msg.c_str());

    auto log = CGlobalFunction::Instance->m_pLog;
    if (luaTitle == "Info")
        log->Info(msg);
    else if (luaTitle == "Warn")
        log->Warn(msg);
    else if (luaTitle == "Debug")
        log->Debug(msg);
    else if (luaTitle == "Error")
        log->Error(msg);
    else if (luaTitle == "Verbos")
        log->Verbos(msg);
    else if (luaTitle == "Assert")
        log->Assert(msg);
    else if (luaTitle == "Fatal")
        log->Fatal(msg);
    else
        log->Log(msg, luaTitle);
    return 0;
}

int CGlobalFunction::Lua_SetCommData(lua_State *l)
{
    auto key = CLua::GetString(l, 1, "");
    if (key.length() > 0)
    {
        Instance->m_mapCommData[key] = CLua::GetString(l, 2, "");
    }
    return 0;
}

int CGlobalFunction::Lua_GetCommData(lua_State *l)
{
    auto key = CLua::GetString(l, 1, "");
    if (key.length() > 0 && Instance->m_mapCommData.exist(key))
        CLua::PushString(l, Instance->m_mapCommData[key]);
    else
        CLua::PushString(l, "");
    return 1;
}

int CGlobalFunction::Lua_GetLogObject(lua_State *l)
{
    CLua::PushPointer(l, CGlobalFunction::Instance->m_pLog);
    return 1;
}

int CGlobalFunction::Lua_SetExtendData(lua_State *l)
{
    auto data = CLua::GetString(l, 1, "");
    auto uuid = "";
    if (data.length() > 0)
    {
        auto uuid = CUtility::GenUUID();
        Instance->m_iC->AddTempString(uuid, data);
    }

    CLua::PushString(l, uuid);
    return 1;
}

int CGlobalFunction::Lua_SetExtendDataByFile(lua_State *l)
{
    auto file = CLua::GetString(l, 1, "");
    auto uuid = "";
    int size = 0;
    auto pBuf = CUtility::ReadFile(file, &size);
    if (size > 0)
    {
        auto uuid = CUtility::GenUUID();
        Instance->m_iC->AddTempBytes(uuid, pBuf, size);
    }
    CLua::PushInteger(l, size);
    CLua::PushString(l, uuid);
    return 2;
}

int CGlobalFunction::Lua_ConnectToDBCenter(lua_State *l)
{
    auto DBName = CLua::GetString(l, 1, "");
    if (DBName.empty())
    {
        CLua::PushInteger(l, Base::ResultType::Error);
        CLua::PushString(l, "Database is empty");
        return 2;
    }

    if (CGlobalFunction::Instance->m_SocketDBCenter == nullptr)
    {
        CGlobalFunction::Instance->ReferenceDataCenterConnection();
        CLua::PushInteger(l, Base::ResultType::Error);
        CLua::PushString(l, "No connect to datacenter.");
        return 2;
    }

    if (CGlobalFunction::Instance->m_mapDBNameToSessionID.exist(DBName))
    {
        CLua::PushInteger(l, Base::ResultType::Succeed);
        CLua::PushInteger(l, CGlobalFunction::Instance->m_mapDBNameToSessionID[DBName]);
        return 2;
    }

    DataCenter::ConnectDatabaseRequest request;
    request.set_database(DBName);

    auto req = make_shared<CSendPackageEvent>();
    auto sync = make_shared<EasySync>();
    // TODO: If connect error, how process it?
    req->SetCallbackFunc([DBName, sync](IEvent *event, CDataTransPackage *pack) {
        auto res_str = pack->GetRecvMessage();
        auto res = ConvertStrToObj<DataCenter::ConnectDatabaseResponse>(res_str);

        CGlobalFunction::Instance->m_mapDBNameToSessionID[DBName] = res->sessionid();
        sync->notify_one();
    });
    req->SetRequest(CGlobalFunction::Instance->m_SocketDBCenter->GetSocketID(), IData::GetRuntimeData()->nodeuuid(), snowflake::Instance->nextid(), Base::HEIGHT_LEVEL, DataCenter::Functions::ConnectDatabase, ConvertObjToStr(&request), "", DataPackage_PackageType::DataPackage_PackageType_RequestPackage);
    CGlobalFunction::Instance->m_iC->SendMessage(req);

    if (!sync->wait_for(5000))
    {
        CLua::PushInteger(l, Base::ResultType::Error);
        CLua::PushString(l, "timtout");
        return 2;
    }

    CLua::PushInteger(l, Base::ResultType::Succeed);
    CLua::PushInteger(l, CGlobalFunction::Instance->m_mapDBNameToSessionID[DBName]);
    return 2;
}

bool CGlobalFunction::SQLFunctionPrepareCheck(lua_State *l, int sessionid, const string &sql)
{
    if (sessionid == -1)
    {
        CLua::PushInteger(l, -1);
        CLua::PushString(l, "Database session id is invalid, call ConnectDBCenter first.");
        return false;
    }

    if (sql.empty())
    {
        CLua::PushInteger(l, -1);
        CLua::PushString(l, "request data is empty");
        return false;
    }

    if (CGlobalFunction::Instance->m_SocketDBCenter == nullptr)
    {
        CLua::PushInteger(l, -1);
        CLua::PushString(l, "Work node no connect to DBCenter.");
        return false;
    }
    return true;
}

CResult CGlobalFunction::RunSQLFunction(const string &request_str, int func)
{
    auto response_str = make_shared<string>();
    auto req = make_shared<CSendPackageEvent>();
    auto result = make_shared<ResultType>(ResultType::Invalid);
    auto sync = make_shared<EasySync>();
    req->SetCallbackFunc([result, sync, response_str](IEvent *event, CDataTransPackage *trans_pack) {
        auto pack = trans_pack->GetDataPackage();
        (*result) = pack->result();

        *response_str = pack->content();
        sync->notify_one();
    });
    req->SetRequest(CGlobalFunction::Instance->m_SocketDBCenter->GetSocketID(), IData::GetRuntimeData()->nodeuuid(), snowflake::Instance->nextid(), Base::HEIGHT_LEVEL,
                    func, request_str, "", DataPackage_PackageType::DataPackage_PackageType_RequestPackage);
    CGlobalFunction::Instance->m_iC->CallMessage(req);

    if (!sync->wait_for(5000))
    {
        return CResult::Make_Error("Timeout");
    }
    return CResult::Make_OK(*response_str);
}

// Request sql query cmd to dbcenter.
// Params:
//     1> SessionID
//     2> Cmd
int CGlobalFunction::Lua_SQLQueryToDBCenter(lua_State *l)
{
    auto SessionID = CLua::GetInteger(l, 1, -1);
    auto query_cmd = CLua::GetString(l, 2, "");
    if (!SQLFunctionPrepareCheck(l, SessionID, query_cmd))
        return 2;

    DataCenter::QuerySQLCmdRequest request;
    request.set_sessionid(SessionID);
    request.set_sqlcmd(query_cmd);

    auto res = RunSQLFunction(ConvertObjToStr(&request), (int)DataCenter::Functions::QuerySQLCmd);
    if (res.IsSucceed())
    {
        auto response = ConvertStrToObj<DataCenter::QuerySQLCmdResponse>(res.GetMessage());
        CLua::PushInteger(l, response->lines_size());
        list<list<string>> res;
        for (auto &item : response->lines())
        {
            list<string> row;
            for (auto &j : item.rawdataitem())
                row.push_back(j);
            res.push_back(row);
        }
        CLua::Push2DTable(l, res);
        return 2;
    }
    else
    {
        CLua::PushInteger(l, -1);
        CLua::PushString(l, res.GetMessage());
        return 2;
    }
}

// Request sql query cmd to dbcenter.
// Params:
//     1> SessionID
//     2> Cmd
int CGlobalFunction::Lua_SQLInsertToDBCenter(lua_State *l)
{
    auto SessionID = CLua::GetInteger(l, 1, -1);
    auto sql_cmd = CLua::GetString(l, 2, "");
    if (!SQLFunctionPrepareCheck(l, SessionID, sql_cmd))
        return 2;

    DataCenter::InsertSQLCmdRequest request;
    request.set_sessionid(SessionID);
    request.set_sqlcmd(sql_cmd);

    auto res = RunSQLFunction(ConvertObjToStr(&request), (int)DataCenter::Functions::InsertSQLCmd);
    if (res.IsSucceed())
    {
        auto response = ConvertStrToObj<DataCenter::InsertSQLCmdResponse>(res.GetMessage());
        if (response->affectedrows() > 0)
        {
            CLua::PushBoolen(l, true);
            CLua::PushInteger(l, response->identity());
        }
        else
        {
            CLua::PushBoolen(l, false);
            CLua::PushString(l, "SQL Run succeed, but affectedrows is 0");
        }
        return 2;
    }
    else
    {
        CLua::PushBoolen(l, false);
        CLua::PushString(l, res.GetMessage());
        return 2;
    }
}

int CGlobalFunction::Lua_SQLDeleteToDBCenter(lua_State *l)
{
    auto SessionID = CLua::GetInteger(l, 1, -1);
    auto sql_cmd = CLua::GetString(l, 2, "");
    if (!SQLFunctionPrepareCheck(l, SessionID, sql_cmd))
        return 2;

    DataCenter::DeleteSQLCmdRequest request;
    request.set_sessionid(SessionID);
    request.set_sqlcmd(sql_cmd);

    auto res = RunSQLFunction(ConvertObjToStr(&request), (int)DataCenter::Functions::DeleteSQLCmd);
    if (res.IsSucceed())
    {
        auto response = ConvertStrToObj<DataCenter::DeleteSQLCmdResponse>(res.GetMessage());
        CLua::PushInteger(l, response->affectedrows());
        CLua::PushString(l, "");
        return 2;
    }
    else
    {
        CLua::PushBoolen(l, -1);
        CLua::PushString(l, res.GetMessage());
        return 2;
    }
}

int CGlobalFunction::Lua_SQLUpdateToDBCenter(lua_State *l)
{
    auto SessionID = CLua::GetInteger(l, 1, -1);
    auto sql_cmd = CLua::GetString(l, 2, "");
    if (!SQLFunctionPrepareCheck(l, SessionID, sql_cmd))
        return 2;

    DataCenter::UpdateSQLCmdRequest request;
    request.set_sessionid(SessionID);
    request.set_sqlcmd(sql_cmd);


    auto res = RunSQLFunction(ConvertObjToStr(&request), (int)DataCenter::Functions::UpdateSQLCmd);
    if (res.IsSucceed())
    {
        auto response = ConvertStrToObj<DataCenter::UpdateSQLCmdResponse>(res.GetMessage());
        CLua::PushInteger(l, response->affectedrows());
        CLua::PushString(l, "");
        return 2;
    }
    else
    {
        CLua::PushBoolen(l, -1);
        CLua::PushString(l, res.GetMessage());
        return 2;
    }
}
