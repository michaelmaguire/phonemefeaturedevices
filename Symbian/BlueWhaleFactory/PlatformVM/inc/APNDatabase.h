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

#ifndef __APNDATABASE_H__
#define __APNDATABASE_H__

#include <etel3rdparty.h>

struct TOperatorAPN
{
	TOperatorAPN(const TDesC& aCountryCode,const TDesC& aNetworkId,const TDesC& aName,const TDesC& aAPN,const TDesC& aUser,const TDesC& aPasswd) : 
		iCountryCode(aCountryCode),iNetworkId(aNetworkId),iName(aName),iAPN(aAPN),iUser(aUser),iPasswd(aPasswd){}
	TBuf<CTelephony::KNetworkCountryCodeSize> 	iCountryCode;
	TBuf<CTelephony::KNetworkIdentitySize> 		iNetworkId;
	TBuf<32> iName;
	TBuf<32> iAPN;
	TBuf<32> iUser;
	TBuf<32> iPasswd;
};


class MAPNDatabase
{
public:
	virtual void LoadDatabaseL() = 0;
	virtual TInt Count() = 0;
	virtual void Close() = 0;
	virtual TOperatorAPN& GetEntry(TInt aIndex) = 0;
	virtual TInt GetEntry(const TDesC& aCountryCode,const TDesC& aOperatorCode) = 0;
	virtual TInt GetNext(const TDesC& aCountryCode,const TDesC& aOperatorCode, TInt aIndex) = 0;
protected:
	virtual ~MAPNDatabase(){}
};

class CAPNDatabase : public CBase, public MAPNDatabase
{
public:
	CAPNDatabase();
	virtual ~CAPNDatabase();
	virtual void LoadDatabaseL();
	virtual TInt Count();
	virtual void Close();
	virtual TOperatorAPN& GetEntry(TInt aIndex);
	virtual TInt GetEntry(const TDesC& aCountryCode,const TDesC& aOperatorCode);
	virtual TInt GetNext(const TDesC& aCountryCode,const TDesC& aOperatorCode, TInt aIndex);
private:
	static TBool ByOperator(const TOperatorAPN& aLeft,const TOperatorAPN& aRight);
private:
	RArray<TOperatorAPN> iDatabase;
};

#endif /*__APNDATABASE_H__*/
