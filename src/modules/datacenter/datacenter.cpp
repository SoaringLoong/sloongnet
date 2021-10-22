/*** 
 * @Author: Chuanbin Wang - wcb@sloong.com
 * @Date: 1970-01-01 08:00:00
 * @LastEditTime: 2021-09-02 17:27:13
 * @LastEditors: Chuanbin Wang
 * @FilePath: /engine/src/modules/datacenter/datacenter.cpp
 * @Copyright 2015-2020 Sloong.com. All Rights Reserved
 * @Description: 
 */
/*** 
 * @......................................&&.........................
 * @....................................&&&..........................
 * @.................................&&&&............................
 * @...............................&&&&..............................
 * @.............................&&&&&&..............................
 * @...........................&&&&&&....&&&..&&&&&&&&&&&&&&&........
 * @..................&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&..............
 * @................&...&&&&&&&&&&&&&&&&&&&&&&&&&&&&.................
 * @.......................&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&.........
 * @...................&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&...............
 * @..................&&&   &&&&&&&&&&&&&&&&&&&&&&&&&&&&&............
 * @...............&&&&&@  &&&&&&&&&&..&&&&&&&&&&&&&&&&&&&...........
 * @..............&&&&&&&&&&&&&&&.&&....&&&&&&&&&&&&&..&&&&&.........
 * @..........&&&&&&&&&&&&&&&&&&...&.....&&&&&&&&&&&&&...&&&&........
 * @........&&&&&&&&&&&&&&&&&&&.........&&&&&&&&&&&&&&&....&&&.......
 * @.......&&&&&&&&.....................&&&&&&&&&&&&&&&&.....&&......
 * @........&&&&&.....................&&&&&&&&&&&&&&&&&&.............
 * @..........&...................&&&&&&&&&&&&&&&&&&&&&&&............
 * @................&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&............
 * @..................&&&&&&&&&&&&&&&&&&&&&&&&&&&&..&&&&&............
 * @..............&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&....&&&&&............
 * @...........&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&......&&&&............
 * @.........&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&.........&&&&............
 * @.......&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&...........&&&&............
 * @......&&&&&&&&&&&&&&&&&&&...&&&&&&...............&&&.............
 * @.....&&&&&&&&&&&&&&&&............................&&..............
 * @....&&&&&&&&&&&&&&&.................&&...........................
 * @...&&&&&&&&&&&&&&&.....................&&&&......................
 * @...&&&&&&&&&&.&&&........................&&&&&...................
 * @..&&&&&&&&&&&..&&..........................&&&&&&&...............
 * @..&&&&&&&&&&&&...&............&&&.....&&&&...&&&&&&&.............
 * @..&&&&&&&&&&&&&.................&&&.....&&&&&&&&&&&&&&...........
 * @..&&&&&&&&&&&&&&&&..............&&&&&&&&&&&&&&&&&&&&&&&&.........
 * @..&&.&&&&&&&&&&&&&&&&&.........&&&&&&&&&&&&&&&&&&&&&&&&&&&.......
 * @...&&..&&&&&&&&&&&&.........&&&&&&&&&&&&&&&&...&&&&&&&&&&&&......
 * @....&..&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&...........&&&&&&&&.....
 * @.......&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&..............&&&&&&&....
 * @.......&&&&&.&&&&&&&&&&&&&&&&&&..&&&&&&&&...&..........&&&&&&....
 * @........&&&.....&&&&&&&&&&&&&.....&&&&&&&&&&...........&..&&&&...
 * @.......&&&........&&&.&&&&&&&&&.....&&&&&.................&&&&...
 * @.......&&&...............&&&&&&&.......&&&&&&&&............&&&...
 * @........&&...................&&&&&&.........................&&&..
 * @.........&.....................&&&&........................&&....
 * @...............................&&&.......................&&......
 * @................................&&......................&&.......
 * @.................................&&..............................
 * @..................................&..............................
 */

#include "datacenter.h"
#include "IData.h"

#include "protocol/manager.pb.h"

unique_ptr<CDataCenter> Sloong::CDataCenter::Instance = nullptr;

extern "C" PackageResult RequestPackageProcesser(void *env, Package *pack)
{
	auto pDB = STATIC_TRANS<DBHub *>(env);
	if (pDB)
		return pDB->RequestPackageProcesser(pack);
	else
		return PackageResult::Make_Error("RequestPackageProcesser error, Environment convert failed.");
}

extern "C" PackageResult ResponsePackageProcesser(void *env, Package *pack)
{
	/*auto pDB = STATIC_TRANS<DBHub *>(env);
	if (pDB)
		return pDB->ResponsePackageProcesser(pack);
	else
		return CResult::Make_Error("ResponsePackageProcesser error, Environment convert failed.");*/
	return PackageResult::Make_Error("NO SUPPORT!");
}

extern "C" CResult EventPackageProcesser(Package *pack)
{
	CDataCenter::Instance->EventPackageProcesser(pack);
	return CResult::Succeed;
}

extern "C" CResult NewConnectAcceptProcesser(SOCKET sock)
{
	return CResult::Succeed;
}

extern "C" CResult ModuleInitialization(IControl *ic)
{
	CDataCenter::Instance = make_unique<CDataCenter>();
	return CDataCenter::Instance->Initialization(ic);
}

extern "C" CResult ModuleInitialized()
{
	return CDataCenter::Instance->Initialized();
}

extern "C" CResult CreateProcessEnvironment(void **out_env)
{
	return CDataCenter::Instance->CreateProcessEnvironmentHandler(out_env);
}

CResult CDataCenter::Initialization(IControl *ic)
{
	IObject::Initialize(ic);
	IData::Initialize(ic);
	ic->Add( DATACENTER_DATAITEM::MapSessionIDToConnection , &m_mapSessionIDToDBConnections );
	ic->Add( DATACENTER_DATAITEM::MapDBNameToSessionID , &m_mapDBNameToSessioinID );
	return CResult::Succeed;
}

CResult CDataCenter::Initialized()
{
	m_pConfig = IData::GetGlobalConfig();
	m_pModuleConfig = IData::GetModuleConfig();
	if (m_pModuleConfig == nullptr)
	{
		return CResult::Make_Error("Initialize error. no config data.");
	}

	return CResult::Succeed;
}

CResult CDataCenter::CreateProcessEnvironmentHandler(void **out_env)
{
	auto item = make_unique<DBHub>();
	auto res = item->Initialize(m_iC);
	if (res.IsFialed())
		return res;
	(*out_env) = item.get();
	m_listDBHub.emplace_back(std::move(item));
	return CResult::Succeed;
}

void Sloong::CDataCenter::EventPackageProcesser(Package *data_pack)
{
	auto event = (Manager::Events)data_pack->function();
	if (!Manager::Events_IsValid(event))
	{
		m_pLog->error(format("EventPackageProcesser is called.but the fucntion[{}] check error.", event));
		return;
	}

	switch (event)
	{
	default:
	{
		m_pLog->error(format("Event is no processed. [{}][{}].", Manager::Events_Name(event), event));
	}
	break;
	}
}