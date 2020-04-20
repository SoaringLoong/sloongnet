/*
 * @Author: WCB
 * @Date: 2019-11-05 08:59:19
 * @LastEditors: WCB
 * @LastEditTime: 2020-04-17 18:22:59
 * @Description: file content
 */
#ifndef SLOONGNET_CONTROL_SERVICE_H
#define SLOONGNET_CONTROL_SERVICE_H


#include "core.h"
#include "export.h"
#include "servermanage.h"

extern "C" {
	CResult MessagePackageProcesser(CDataTransPackage*);
	//CResult NewConnectAcceptProcesser(CSockInfo*);
	CResult ModuleInitialization(GLOBAL_CONFIG*);
	CResult ModuleInitialized(IControl*);
}

namespace Sloong
{
	
	typedef std::function<bool(Functions, string, CDataTransPackage*)> FuncHandler;

	class CConfiguation;
	class SloongControlService
	{
	public:
		SloongControlService() {}
		virtual ~SloongControlService() {}

		CResult Initialization(GLOBAL_CONFIG*);
		CResult Initialized(IControl*);

		CResult MessagePackageProcesser(CDataTransPackage*);

	protected:
		void ResetControlConfig(GLOBAL_CONFIG* config);
		
		void RegistFunctionHandler(Functions func, FuncHandler handler);


	protected:
		unique_ptr<CServerManage>	m_pServer = make_unique<CServerManage>();
		map_ex<Functions, FuncHandler> m_oFunctionHandles;
		IControl* 	m_pControl = nullptr;
		CLog*		m_pLog =nullptr;
		
		GLOBAL_CONFIG* m_pConfig;

	public:
		static unique_ptr<SloongControlService> Instance;
	};

}



#endif //SLOONGNET_CONTROL_SERVICE_H
