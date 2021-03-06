/**
 * Copyright (c) 2004-2008 Blue Whale Systems Ltd. All Rights Reserved. 
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER 
 *  
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License version 
 * 2 only, as published by the Free Software Foundation.  
 *  
 * This software is provided "as is," and the copyright holder makes no representations or warranties, express or
 * implied, including but not limited to warranties of merchantability or fitness for any particular purpose or that the
 * use of this software or documentation will not infringe any third party patents, copyrights, trademarks or other
 * rights.
 * 
 * The copyright holder will not be liable for any direct, indirect special or consequential damages arising out of any
 * use of this software or documentation.
 * 
 * See the GNU  General Public License version 2 for more details 
 * (a copy is included at /legal/license.txt).  
 *  
 * You should have received a copy of the GNU General Public License 
 * version 2 along with this work; if not, write to the Free Software 
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 
 * 02110-1301 USA  
 *  
 * Please contact Blue Whale Systems Ltd, Suite 123, The LightBox
 * 111 Power Road, Chiswick, London, W4 5PY, United Kingdom or visit 
 * www.bluewhalesystems.com if you need additional 
 * information or have any questions.  
 */ 
#include <e32std.h>
#include <ApUtils.h>
#include "APNManager.h"
#include "Properties.h"
#include "CommDBUtil.h"
#include "ManagementObjectFactory.h"

#ifdef __WINSCW__
#define EXPORT_DECL EXPORT_C 
#else
#define EXPORT_DECL 
#endif


// some useful debugging and logging macros
//#define __APN_LOGGING__
//#define __DONT_USE_ALL_IAP__
//#define __DONT_USE_WLAN__
//#define __FORCE_ASK_USER__

#ifdef __APN_LOGGING__
#define __ENABLE_LOG__
_LIT(KLogFileName,"APNManager.txt");
#endif
#include "FileLogger.h"

EXPORT_DECL CAPNManager* CAPNManager::NewL(MProperties* aProperties)
{
	CAPNManager* self = new (ELeave) CAPNManager(aProperties);
	CleanupStack::PushL(self);
	self->ConstructL();
	CleanupStack::Pop(self);
	return self;
}

CAPNManager::CAPNManager()
{}

CAPNManager::CAPNManager(MProperties* aProperties):iProperties(aProperties)
{
	iProperties->AddRef();
}

CAPNManager::~CAPNManager()
{
	delete iNetWorkInfo;
	delete iIAPInfo;
	iDatabase.Close();
	iCurrentIAPList.Close();
	if(iProperties)
	{
		iProperties->Release();
		iProperties = NULL;
	}
	if(iCommDBUtil)
	{
		iCommDBUtil->Release();
		iCommDBUtil = NULL;
	}
}

void CAPNManager::ConstructL()
{
	DEBUGMSG(_L("ConstructL"));
#ifdef __WINSCW__
	iBeingTested = EFalse;
#endif

	TRAPD(ignore,iRuntimeDatabase.LoadL(iProperties));
	iAPNCreationEnabled = EFalse;

	const TDesC8* shortcut = NULL;
	TRAP(ignore,shortcut = &(iProperties->GetString8L(KPropertyString8ShortcutName)));
	if(shortcut)
	{
		iShortCut.Copy(*shortcut);
	}
	else
	{
		iShortCut.Copy(KBlueWhaleAPNMatch());
	}
	

	iCommDBUtil = CCommDBUtil::NewL();
	
	iNetWorkInfo = CNetworkInfoManager::NewL();
	iNetWorkInfo->SetObserver(this);
	iNetWorkInfo->StartL();
	iDatabase.LoadDatabaseL();
	iIAPInfo = CIAPInfo::NewL();
	iIAPInfo->ScanL();
}

void CAPNManager::LoadPropertiesL(TDes& aCountry,TDes& aNetwork)
{
	const TDesC* countryCode = &(iProperties->GetStringL(PROPERTY_STRING_COUNTRY_CODE));
	const TDesC* networkId = &(iProperties->GetStringL(PROPERTY_STRING_NETWORK_ID));
	aCountry.Copy(*countryCode);
	aNetwork.Copy(*networkId);
	DEBUGMSG2(_L("LoadPropertiesL %S %S"),&aCountry,&aNetwork);
}

MIAPSession* CAPNManager::StartIAPSession()
{
	MIAPSession* ret = NULL;
	TRAPD(err,ret = DoStartIAPSessionL());
	return ret;
}

MIAPSession* CAPNManager::DoStartIAPSessionL()
{
	TRAPD(err,BuildCurrentValidIAPsL());
	DEBUGMSG1(_L("BuildCurrentValidIAPsL err %d"),err);
	CMIAPSessionImpl* ret = CMIAPSessionImpl::NewL(this);
	DEBUGMSG1(_L("New IAP session 0x%08x"),ret);
	return ret;
}

void CAPNManager::BuildCurrentValidIAPsL()
{
	DEBUGMSG(_L("BuildCurrentValidIAPsL->"));
	iCurrentIAPList.Reset();
	RArray<TUint>& iaps = iIAPInfo->IAPs();
	CDesC16Array& wlans = iIAPInfo->WlanNames();
	// find wlans with IAPS
	TUint32 network = 0;
#if __WINSCW__
	if(!iBeingTested)
	{
		TInt iap = iCommDBUtil->FindIAPL(_L("Winsock"),network);
		if(iap != 0)
		{
			iCurrentIAPList.AppendL(TIAPWithPort(iap,0,KMaxTInt32));
		}
	}
#endif
	iWlanCount = 0;
	TInt wCount = wlans.Count();
	DEBUGMSG1(_L("WLAN count %d"),wCount);
	for(TInt i=0;i<wCount;i++)
	{
		TUint32 iapCheck  = iCommDBUtil->FindIAPL(wlans[i],network);
		DEBUGMSG3(_L("IAP %S %d net %d"),&wlans[i],iapCheck,network);
		if(iapCheck != 0) // wlan has an IAP
		{
			TInt index = iaps.Find(iapCheck);
			if(index != KErrNotFound)
			{
	#ifndef __DONT_USE_WLAN__
				DEBUGMSG1(_L("Adding wlan %d"),iapCheck);
				iCurrentIAPList.AppendL(TIAPWithPort(iapCheck,0,KWlanPriority));
	#endif
				iaps.Remove(index);
				iWlanCount++;
			}
		}
	}
	// add bluewhale iap after the wlans
	if(iAPNCreationEnabled && iNetWorkInfo->IsHomeNetwork())
	{
		DEBUGMSG(_L("Auto APN creation enabled"));
		TUint32 blueWhaleIAP = iCommDBUtil->MatchIAPL(iShortCut,network);
		TInt count = 0;
		while(blueWhaleIAP != 0)
		{
			count++;
			blueWhaleIAP = iCommDBUtil->NextMatchIAPL(iShortCut,network);
		}
		iCommDBUtil->CloseMatchIAP();
		
		DEBUGMSG1(_L("BWM IAP %d"),blueWhaleIAP);
		if(count == 0)
		{
			CreateBlueWhaleIAPL();
		}
		blueWhaleIAP = iCommDBUtil->MatchIAPL(iShortCut,network);
		count = 0;
		while(blueWhaleIAP != 0)
		{
			iCurrentIAPList.AppendL(TIAPWithPort(blueWhaleIAP,0,KBlueWhalePriority));
			count++;
			blueWhaleIAP = iCommDBUtil->NextMatchIAPL(iShortCut,network);
		}
		iCommDBUtil->CloseMatchIAP();
				
		if(blueWhaleIAP != 0)
		{
			DEBUGMSG1(_L("Adding BWM %d"),blueWhaleIAP);
		}
	}

	// Add the known good IAPs
	const RArray<TIAPWithPort>& current(iRuntimeDatabase.WorkingSet());
	DEBUGMSG1(_L("Workingset count %d"),current.Count());
	for(TInt i = 0;i<current.Count();i++)
	{
		DEBUGMSG1(_L("Adding known %d"),current[i].iIAP);
		iCurrentIAPList.AppendL(current[i]);
	}
	
	// add the rest of the iaps
#ifndef __DONT_USE_ALL_IAP__
	TInt iapCount = iaps.Count();
	DEBUGMSG1(_L("IAP count %d"),iapCount);
	for(TInt i=0;i<iapCount;i++)
	{
		DEBUGMSG1(_L("Adding rest %d"),iaps[i]);
		iCurrentIAPList.AppendL(TIAPWithPort(iaps[i],0,KTherestPriority));
	}
#endif
	// if all else fails, ask the user
	// iap of zero prompts the user
	// only do this if there are no stored APNs
	if(current.Count() == 0)
	{
		iCurrentIAPList.AppendL(TIAPWithPort(0,0,KUserPickPriority));
	}
	DEBUGMSG(_L("BuildCurrentValidIAPsL<-"));
		
}

TInt CAPNManager::GetIAP(TInt aIndex,TInt /*aPort*/)
{
#ifndef __FORCE_ASK_USER__
	TInt iap = KErrNotFound;
	TBuf<50> iapName;
	TIAPWithPort runTime;
	TIAPWithPort fromList;
#if 0
	if(aPort != 0)
	{
		DEBUGMSG1(_L("checking runtime for %d"),aPort);
		TIAPWithPort iapFind(0,aPort,0);
		DEBUGMSG1(_L("runtime count %d"),iRuntimeDatabase.Count());
		TInt index = iRuntimeDatabase.FindByPort(iapFind);
		if(index != KErrNotFound)
		{
			runTime = iRuntimeDatabase.WorkingSet()[index];
			iap =  runTime.iIAP;
		}
		DEBUGMSG2(_L("Found %d at %d"),iap,index);
	}
#endif
	if(iNetWorkInfo->IsCallOngoing())
	{
		DEBUGMSG(_L("Ongoing call"));
		return iap;
	}
	if( ( iNetWorkInfo->IsRegisteredOnNetwork() || iWlanCount > 0  
#ifdef __WINSCW__
		||	!iBeingTested
#endif
		) && iCurrentIAPList.Count() > 0)
	{
		if(aIndex < iCurrentIAPList.Count())
		{
			fromList = iCurrentIAPList[aIndex];
			DEBUGMSG2(_L("pri %d %d"),fromList.iPriority,runTime.iPriority);
			iap = fromList.iIAP;
		}
	}
	DEBUGMSG1(_L("Trying iap %d"),iap);
	return iap;
#else
	return 0;
#endif
}

EXPORT_DECL void CAPNManager::SetAutoAPN()
{
	iAPNCreationEnabled = ETrue;
}
void CAPNManager::Reset()
{
	TRAPD(ignore,iRuntimeDatabase.ResetL(iProperties));
}
void CAPNManager::UdateBlueWhaleIAPL(const TDesC& aCountryCode,const TDesC& aNetworkId)
{
	DEBUGMSG2(_L("UdateBlueWhaleIAPL %S %S"),&aCountryCode,&aNetworkId);
			
	User::LeaveIfError(iCommDBUtil->BeginTransaction());
	TInt networkIndex = iDatabase.GetEntry(aCountryCode,aNetworkId);
	TUint number = 1;
	TBuf<32> APNName;

	if(networkIndex != KErrNotFound)
	{
		TOperatorAPN& APNInfo = iDatabase.GetEntry(networkIndex);
		DEBUGMSG2(_L("Updating %d %S"),networkIndex,&APNInfo.iAPN);
		APNName.Format(_L("%S%d"),&iShortCut,number);
		TUint32 FindIAPL(const TDesC& aName,TUint32& aNetwork);
		iCommDBUtil->UpdateOutgoingGprsL(APNName,APNInfo.iAPN,APNInfo.iUser,APNInfo.iPasswd);
	}
	iCommDBUtil->CommitTransaction();
}

void CAPNManager::CreateBlueWhaleIAPL()
{
	DEBUGMSG(_L("->CreateBlueWhaleIAPL"));
	// create the BlueWhaleAPN
	CCommDBUtil* commDBUtil = CCommDBUtil::NewL();
	CleanupReleasePushL(*commDBUtil);
	TUint32 service = 0;
	TUint32 bearer = 0;
#ifdef __USE_WINSOCK__
	const TPtrC bearerType(TPtrC(LAN_BEARER));
	service = commDBUtil->FindServiceL(TPtrC(OUTGOING_GPRS),_L("Winsock Service"));
	bearer = commDBUtil->FindBearerL(bearerType,_L("Winsock"));
#else
	const TPtrC bearerType(TPtrC(MODEM_BEARER));
	TInt networkIndex = KErrNotFound;
	TBuf<32> countryCode;
	TBuf<23> networkId;
	TRAPD(loadError,LoadPropertiesL(countryCode,networkId));
	if(loadError != KErrNone)
	{
		networkId.Copy(iNetWorkInfo->CurrentNetworkID());
		countryCode.Copy(iNetWorkInfo->CurrentCountryCode());
	}

	networkIndex = iDatabase.GetEntry(countryCode,networkId);
	TUint number = 1;
	TBuf<32> APNName;

	while(networkIndex != KErrNotFound)
	{
	    User::LeaveIfError(commDBUtil->BeginTransaction());
		DEBUGMSG1(_L("Getting entry %d"),networkIndex);
		TOperatorAPN& APNInfo = iDatabase.GetEntry(networkIndex);
		APNName.Format(_L("%S%d"),&iShortCut,number);
		DEBUGMSG1(_L("Creating %S"),&APNName);
		service = commDBUtil->CreateNewOutgoingGprsL(APNName,APNInfo.iAPN,APNInfo.iUser,APNInfo.iPasswd);
		bearer = commDBUtil->FindBearerL(bearerType,_L("GPRS Modem"));
#endif
		if(service !=0 && bearer != 0)
		{
			TUint32 wap_id = commDBUtil->CreateNewWAPAccessPointL(APNName);
			TUint32 network = commDBUtil->CreateNewNetworkL(APNName);
			TUint32 iap = commDBUtil->CreateNewInternetAccessPointL(APNName,service,bearer,bearerType,network);
			commDBUtil->CreateNewWAPBearerL(wap_id,iap);
			User::LeaveIfError(commDBUtil->CommitTransaction());
		}
		else
		{
			commDBUtil->RollbackTransaction();
			break;
		}
		networkIndex = iDatabase.GetNext(countryCode,networkId,networkIndex + 1);
		number++;
	}
	CleanupStack::PopAndDestroy(commDBUtil);
	DEBUGMSG(_L("<-CreateBlueWhaleIAPL"));
}

void CAPNManager::NetworkChanged(const TDesC& aCountryCode,const TDesC& aNetworkId)
{
	DEBUGMSG2(_L("NetworkChanged %S %S"),&aCountryCode,&aNetworkId);
	TRAPD(ignore,DoNetworkChangedL(aCountryCode,aNetworkId));
}

void CAPNManager::DoNetworkChangedL(const TDesC& aCountryCode,const TDesC& aNetworkId)
{
	TBuf<32> countryCode;
	TBuf<32> networkId;
	TRAPD(loadErr,LoadPropertiesL(countryCode,networkId));
	if(loadErr == KErrNone)
	{
		DEBUGMSG2(_L("current network %S %S"),&countryCode,&networkId);
		if(aCountryCode.Length() != 0 && aNetworkId.Length() != 0
				&& (countryCode.Compare(aCountryCode) != 0 
				|| networkId.Compare(aNetworkId) != 0))
		{
			//the network is different to the one we have setup for
			DEBUGMSG2(_L("New network %S %S"),&aCountryCode,&aNetworkId);
			SaveSettingsL(aCountryCode,aNetworkId);
			UdateBlueWhaleIAPL(aCountryCode,aNetworkId);
		}
	}
	else
	{
		if(aCountryCode.Length() != 0 && aNetworkId.Length() != 0)
		{
			SaveSettingsL(aCountryCode,aNetworkId);
		}		
	}
}

void CAPNManager::SaveSettingsL(const TDesC& aCountryCode,const TDesC& aNetworkId)
{
	iProperties->DeleteString(PROPERTY_STRING_COUNTRY_CODE,0);
	iProperties->DeleteString(PROPERTY_STRING_NETWORK_ID,0);
	iProperties->SetStringL(PROPERTY_STRING_COUNTRY_CODE,aCountryCode,0);
	iProperties->SetStringL(PROPERTY_STRING_NETWORK_ID,aNetworkId,0);
}

void CAPNManager::IAPReport(TBool aSuccess,TInt aIAP,TInt aPort)
{
	TRAPD(ignore, IAPReportL(aSuccess, aIAP, aPort));
}

void CAPNManager::IAPReportL(TBool aSuccess,TInt aIAP,TInt aPort)
{
	DEBUGMSG3(_L("IAPReport iap %d port %d success %d"),aIAP,aPort,aSuccess);
	if(aSuccess)
	{
		if(aIAP == 0)
		{
			TBuf<50> iapName;
			iIAPInfo->ActiveIAP(iapName);
			if(iapName.Length() > 0)
			{
				if(iapName.Left(iShortCut.Length()).Compare(iShortCut) != 0)
				{
					// user chose APN that wasn't ours
					TUint32 network;
					TInt iap = iCommDBUtil->FindIAPL(iapName,network);
					iRuntimeDatabase.AddWorkingIAP(TIAPWithPort(iap,aPort,KTherestPriority));
					iRuntimeDatabase.SaveL(iProperties);
				}
				else
				{
					TUint32 network;
					TInt iap = iCommDBUtil->FindIAPL(iapName,network);
					iRuntimeDatabase.AddWorkingIAP(TIAPWithPort(iap,aPort,KBlueWhalePriority));
					iRuntimeDatabase.SaveL(iProperties);
				}
			}
		}
		else
		{
			TInt found = iCurrentIAPList.Find(TIAPWithPort(aIAP,aPort,0));
			if(found != KErrNotFound)
			{
				TInt iap = iCurrentIAPList[found].iIAP;
				TInt port = iCurrentIAPList[found].iPort;
				TInt priority = iCurrentIAPList[found].iPriority;
				iRuntimeDatabase.AddWorkingIAP(TIAPWithPort(iap,port,priority));
				iRuntimeDatabase.SaveL(iProperties);
			}
		}
	}
	else
	{
		iRuntimeDatabase.RemoveIAP(TIAPWithPort(aIAP,aPort,0));
		iRuntimeDatabase.SaveL(iProperties);
	}
	BuildCurrentValidIAPsL();
}
////////////////////////////////////////////////////////////////////////////////////////
CMIAPSessionImpl::CMIAPSessionImpl()
{}

CMIAPSessionImpl::CMIAPSessionImpl(CAPNManager* aParent) :iParent(aParent)
{}

CMIAPSessionImpl::~CMIAPSessionImpl()
{}

CMIAPSessionImpl* CMIAPSessionImpl::NewL(CAPNManager* aParent)
{
	CMIAPSessionImpl* self = new (ELeave) CMIAPSessionImpl(aParent);
	CleanupStack::PushL(self);
	self->ConstructL();
	CleanupStack::Pop(self);
	return self;
}

void CMIAPSessionImpl::ConstructL()
{
	iIndex = 0;
}

TInt CMIAPSessionImpl::GetNextIAP(TInt aPort)
{
	return iParent->GetIAP(iIndex,aPort);
}

void CMIAPSessionImpl::ReportStatus(TBool aSuccess,TInt aIAP,TInt aPort)
{
	iParent->IAPReport(aSuccess,aIAP,aPort);
	if(!aSuccess) // IAP didn't work
	{
		iIndex++;
	}
}

void CMIAPSessionImpl::Release()
{
	delete this;
}
