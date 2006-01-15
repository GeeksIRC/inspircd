/*       +------------------------------------+
 *       | Inspire Internet Relay Chat Daemon |
 *       +------------------------------------+
 *
 *  InspIRCd is copyright (C) 2002-2006 ChatSpike-Dev.
 *                       E-mail:
 *                <brain@chatspike.net>
 *           	  <Craig@chatspike.net>
 *     
 * Written by Craig Edwards, Craig McLure, and others.
 * This program is free but copyrighted software; see
 *            the file COPYING for details.
 *
 * ---------------------------------------------------
 */

using namespace std;

#include <stdio.h>
#include <string>
#include "users.h"
#include "channels.h"
#include "modules.h"

/* $ModDesc: Provides support for unreal-style umode +B */

class ModuleBotMode : public Module
{
	Server *Srv; 
 public:
	ModuleBotMode(Server* Me)
		: Module::Module(Me)
	{
		Srv = Me;
		
		if (!Srv->AddExtendedMode('B',MT_CLIENT,false,0,0))
		{
			Srv->Log(DEFAULT,"*** m_botmode: ERROR, failed to allocate user mode +B!");
			return;
		}
	}

	void Implements(char* List)
	{
		List[I_OnWhois] = List[I_OnExtendedMode] = 1;
	}
	
	virtual ~ModuleBotMode()
	{
	}
	
	virtual Version GetVersion()
	{
		return Version(1,0,0,0,VF_STATIC|VF_VENDOR);
	}
	
	virtual int OnExtendedMode(userrec* user, void* target, char modechar, int type, bool mode_on, string_list &params)
	{
		if ((modechar == 'B') && (type == MT_CLIENT))
  		{
			return 1;
		}
		else
		{
			return 0;
		}
	}

	virtual void OnWhois(userrec* src, userrec* dst)
	{
		if (strchr(dst->modes,'B'))
		{
			Srv->SendTo(NULL,src,"335 "+std::string(src->nick)+" "+std::string(dst->nick)+" :is a \2bot\2 on "+Srv->GetNetworkName());
		}
	}

};

// stuff down here is the module-factory stuff. For basic modules you can ignore this.

class ModuleBotModeFactory : public ModuleFactory
{
 public:
	ModuleBotModeFactory()
	{
	}
	
	~ModuleBotModeFactory()
	{
	}
	
	virtual Module * CreateModule(Server* Me)
	{
		return new ModuleBotMode(Me);
	}
	
};


extern "C" void * init_module( void )
{
	return new ModuleBotModeFactory;
}

