/*** 
 * @Author: Chuanbin Wang - wcb@sloong.com
 * @Date: 2015-12-04 17:40:06
 * @LastEditTime: 2021-01-12 14:45:35
 * @LastEditors: Chuanbin Wang
 * @FilePath: /engine/src/modules/core/ConnectSession.cpp
 * @Copyright 2015-2020 Sloong.com. All Rights Reserved
 * @Description: 对连接进一步进行封装，并保存会话相关信息，所有对连接的操作都由此进行，提供发送和接收队列以增强吞吐量
 * 在以主动发起端连接的时候使用者不需要关心连接信息，将会自动尝试重连，并在连接可用之后对队列进行处理。
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

#include "ConnectSession.h"
#include "IData.h"

#include "events/MonitorSendStatus.hpp"
using namespace Sloong::Events;

Sloong::ConnectSession::ConnectSession()
{
	m_pSendList = new queue_ex<UniquePackage>[s_PriorityLevel]();
}

ConnectSession::~ConnectSession()
{
	if (m_pConnection)
		m_pConnection->Close();
	for (int i = 0; i < s_PriorityLevel; i++)
	{
		while (!m_pSendList[i].empty())
		{
			m_pSendList[i].pop_move();
		}
	}
	SAFE_DELETE_ARR(m_pSendList);

	while (!m_oPrepareSendList.empty())
	{
		m_oPrepareSendList.pop_move();
	}
}

void Sloong::ConnectSession::Initialize(IControl *iMsg, UniqueConnection conn)
{
	IObject::Initialize(iMsg);
	m_ActiveTime = time(NULL);
	m_pConnection = std::move(conn);
}


ResultType Sloong::ConnectSession::SendDataPackage(UniquePackage pack)
{
	if (IsOverflowPackage(pack.get()))
	{
		m_pLog->Assert("The package size is to bigger, this's returned and replaced with an error message package.");
		pack->set_result(ResultType::Error);
		PackageHelper::SetContent(pack.get(), "The package size is to bigger." );
		pack->clear_extend();
	}

	m_pLog->Verbos(Helper::Format("SEND>>>[%d]>>No[%lld]>>[%d]byte", m_pConnection->GetSocketID() , pack->id(), pack->ByteSizeLongEx()));

	// if have exdata, directly add to epoll list.
	if (IsBigPackage(pack.get()) || m_pConnection->IsSending() || (m_bIsSendListEmpty == false && !m_oPrepareSendList.empty()) || m_oSockSendMutex.try_lock() == false)
	{
		AddToSendList(move(pack));
		return ResultType::Retry;
	}

	unique_lock<mutex> lck(m_oSockSendMutex, std::adopt_lock);
	// if code run here. the all list is empty. and no have exdata. try send message
	auto res = m_pConnection->SendPackage(std::move(pack));
	lck.unlock();
	if (res.GetResult() == ResultType::Succeed)
	{
		return ResultType::Succeed;
	}
	else if (res.GetResult() == ResultType::Error)
	{
		m_pLog->Warn(Helper::Format("Send data failed.[%s]", m_pConnection->m_strAddress.c_str()));
		return ResultType::Error;
	}
	else if (res.GetResult() == ResultType::Retry)
	{
		m_pLog->Verbos(Helper::Format("Send data done. wait next send time.[%d/%d]", m_pConnection->m_SendPackageSize, m_pConnection->m_SentSize));
		return ResultType::Retry;
	}
	else
	{
		m_pLog->Error(Helper::Format("Unintended result[%s] in SendDataPackage in SockInfo.", ResultType_Name(res.GetResult()).c_str()));
		return ResultType::Error;
	}
}

void Sloong::ConnectSession::AddToSendList(UniquePackage pack)
{
	auto events = make_shared<MonitorSendStatusEvent>(pack->sessionid());
	m_oPrepareSendList.push_move(std::move(pack));
	m_pLog->Debug(Helper::Format("Add send package to prepare send list. list size:[%d]", m_oPrepareSendList.size()));
	m_bIsSendListEmpty = false;
	m_iC->SendMessage(events);
}

ReceivePackageListResult Sloong::ConnectSession::OnDataCanReceive()
{
	unique_lock<mutex> srlck(m_oSockReadMutex, std::adopt_lock);

	ReceivePackageList readList;
	bool bLoop = false;
	do
	{
		auto res = m_pConnection->RecvPackage();
		if (res.GetResult() == ResultType::Succeed)
		{
			auto package = res.MoveResultObject();

			if (IsOverflowPackage(package.get()))
			{
				m_pLog->Warn("The package size is to bigger.");
				AddToSendList(PackageHelper::MakeErrorResponse(package.get(),"The package size is to bigger."));
			}
			else
			{
				m_pLog->Verbos(Helper::Format("RECV<<<[%d]<<No[%lld]<<[%d]byte", m_pConnection->GetSocketID(), package->id(), package->ByteSizeLongEx()));
				package->add_clocks(GetClock());
				package->set_sessionid(m_pConnection->GetHashCode());
				bLoop = true;
				m_ActiveTime = time(NULL);

				if( package->hash().length() != 32 )
				{
					auto msg = "Hash check error. Make sure the hash algorithm is SHA256";
					m_pLog->Verbos(msg);
					AddToSendList(PackageHelper::MakeErrorResponse(package.get(),msg ));
					continue;
				}
				string hash(package->hash());
				package->clear_hash();
				unsigned char buffer[32] = {0};
				CSHA256::Binary_Encoding(ConvertObjToStr(package.get()),buffer);
				if( string((char*)buffer,32) != hash )
				{
					auto msg =  Helper::Format("Hash check error. Package[%s]<->[%s]Calculate", ConvertToHexString(hash.c_str(),0,31).c_str(),ConvertToHexString((char*)buffer,0,31).c_str() );
					m_pLog->Verbos(msg);
					AddToSendList(PackageHelper::MakeErrorResponse(package.get(),msg));
					continue;
				}

				if (package->priority() > s_PriorityLevel || package->priority() < 0)
				{
					m_pLog->Warn(Helper::Format("Receive priority level error. the data is %d, the config level is %d. add this message to last list", package->priority(), s_PriorityLevel));
					package->set_priority(s_PriorityLevel);
				}

				readList.push(std::move(package));
			}
		}
		else if (res.GetResult() == ResultType::Warning )
		{
			// Receive function recved 0 length data. so this case may be no data can received, or the socket is closed.
			// In here we look as socket is closed. 
			return ReceivePackageListResult::Make_Error("Socket rece function returned 0, so may it is closed.");
		}
		else if (res.GetResult() == ResultType::Ignore)
		{
			// Receive data pageage length return 11(EAGAIN) error. so if in bLoop mode, this is OK.
			break;
		}
		else if (res.GetResult() == ResultType::Retry )
		{
			// Package is no receive done. need receive in next time.
			m_pLog->Verbos(Helper::Format("Receive package happened retry event. curent list[%d] ",readList.size()));
			break;
		}
		else if (res.GetResult() == ResultType::Error)
		{
			// Receive error, this connect weill be closed.
			return ReceivePackageListResult::Make_Error(res.GetMessage());
		}
		else
		{
			m_pLog->Error(Helper::Format("Unintended result[%s].Loop[%s].", ResultType_Name(res.GetResult()).c_str(), bLoop ? "true" : "false"));
			break;
		}
	} while (bLoop);

	srlck.unlock();
	return ReceivePackageListResult::Make_OKResult(std::move(readList));
}

ResultType Sloong::ConnectSession::OnDataCanSend()
{
	ProcessPrepareSendList();
	return ProcessSendList();
}

void Sloong::ConnectSession::ProcessPrepareSendList()
{
	// progress the prepare send list first
	auto pack = m_oPrepareSendList.TryMovePop();
	while (pack != nullptr)
	{
		auto priority = pack->priority();
		m_pSendList[priority].push_move(std::move(pack));
		m_pLog->Debug(Helper::Format("Add package to send list[%d]. send list size[%d], prepare send list size[%d]",
									 priority, m_pSendList[priority].size(), m_oPrepareSendList.size()));

		pack = m_oPrepareSendList.TryMovePop();
	}
}

ResultType Sloong::ConnectSession::ProcessSendList()
{
	unique_lock<mutex> srlck(m_oSockSendMutex, std::adopt_lock);
	if (m_pConnection->IsSending())
	{
		m_pLog->Debug(Helper::Format("Start send package : AllSize[%d],Sent[%d]", m_pConnection->m_SendPackageSize, m_pConnection->m_SentSize));
		auto res = m_pConnection->SendPackage(nullptr);
		if (res.GetResult() == ResultType::Error)
		{
			m_pLog->Error(Helper::Format("Send data package error. close connect:[%s:%d]", m_pConnection->m_strAddress.c_str(), m_pConnection->m_nPort));
			return ResultType::Error;
		}
		else if (res.GetResult() == ResultType::Retry)
		{
			m_pLog->Debug(Helper::Format("Send data package done but not all data is send All[%d]:Sent[%d]. wait next write sign.", m_pConnection->m_SendPackageSize, m_pConnection->m_SentSize));
			return ResultType::Retry;
		}
		else
		{
		}
	}

	bool bTrySend = true;
	while (bTrySend)
	{
		auto list = GetSendPackage();
		if (list == nullptr)
		{
			m_pLog->Debug("All data is sent, the send info list is empty.");
			m_bIsSendListEmpty = true;
			return ResultType::Succeed;
		}

		auto pack = list->TryMovePop();
		if (pack == nullptr)
			return ResultType::Retry;

		m_pLog->Debug(Helper::Format("Send new package, the list size[%d],Size[%d],", list->size(), pack->ByteSizeLongEx()));
		auto res = m_pConnection->SendPackage(move(pack));

		if (res.GetResult() == ResultType::Error)
		{
			m_pLog->Error(Helper::Format("Send data package error. close connect:[%s:%d]", m_pConnection->m_strAddress.c_str(), m_pConnection->m_nPort));
			return ResultType::Error;
		}
		else if (res.GetResult() == ResultType::Retry)
		{
			m_pLog->Debug(Helper::Format("Send data package done but not all data is send All[%d]:Sent[%d]. wait next write sign.", m_pConnection->m_SendPackageSize, m_pConnection->m_SentSize));
			bTrySend = false;
			return ResultType::Retry;
		}
		else
		{
			bTrySend = true;
		}
	}
	return ResultType::Error;
}

/// 获取发送信息列表
// 首先判断上次发送标志，如果不为-1，表示上次的发送列表没有发送完成。直接返回指定的列表
// 如果为-1，表示需要发送新的列表。按照优先级逐级的进行寻找。
queue_ex<UniquePackage> *Sloong::ConnectSession::GetSendPackage()
{
	for (int i = 0; i < s_PriorityLevel; i++)
	{
		if (m_pSendList[i].empty())
			continue;
		else
		{
			m_pLog->Debug(Helper::Format("Send list, Priority level:%d", i));
			return &m_pSendList[i];
		}
	}
	return nullptr;
}
