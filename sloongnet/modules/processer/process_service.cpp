/* File Name: server.c */
#include "process_service.h"
#include "utility.h"
#include "NetworkEvent.hpp"
using namespace Sloong;
using namespace Sloong::Events;


void SloongNetProcess::AfterInit()
{
	m_oConfig.ParseFromString(m_pServerConfig->exconfig());
	m_pControl->Add(DATA_ITEM::ServerConfiguation, &m_oConfig);
	RegistFunctionHandler(Functions::ProcessMessage, std::bind(&SloongNetProcess::ProcessMessageHanlder, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
	m_pControl->RegisterEventHandler(SocketClose, std::bind(&SloongNetProcess::OnSocketClose, this, std::placeholders::_1));
	m_pProcess->Initialize(m_pControl.get());
}

bool Sloong::SloongNetProcess::ProcessMessageHanlder(Functions func, string sender, SmartPackage pack)
{	
	string strRes("");
	char* pExData = nullptr;
	int nExSize;

	auto msg = pack->GetRecvPackage();
	string uuid = msg->extend();
	auto infoItem = m_mapUserInfoList.find(uuid);
	if (infoItem == m_mapUserInfoList.end())
	{
		m_mapUserInfoList[uuid] = make_unique<CLuaPacket>();
		infoItem = m_mapUserInfoList.find(uuid);
	}
	if (m_pProcess->MsgProcess(infoItem->second.get(), msg->content(), strRes, pExData, nExSize)) {
		msg->set_content(strRes);
	}
	else {
		m_pLog->Error("Error in process");
		msg->set_content("{\"errno\": \"-1\",\"errmsg\" : \"server process happened error\"}");
	}
	pack->ResponsePackage(msg);
	auto response_event = make_shared<CNetworkEvent>(EVENT_TYPE::SendMessage);
	response_event->SetSocketID(pack->GetSocketID());
	response_event->SetDataPackage(pack);
	m_pControl->CallMessage(response_event);
	return true;
}

void Sloong::SloongNetProcess::OnSocketClose(SmartEvent event)
{
	auto net_evt = dynamic_pointer_cast<CNetworkEvent>(event);
	auto info = net_evt->GetUserInfo();
	if (!info)
	{
		m_pLog->Error(CUniversal::Format("Get socket info from socket list error, the info is NULL. socket id is: %d", net_evt->GetSocketID()));
		return;
	}
	// call close function.
	//m_pProcess->CloseSocket(info);
	//net_evt->CallCallbackFunc(net_evt);
}