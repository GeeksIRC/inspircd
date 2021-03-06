/*
 * InspIRCd -- Internet Relay Chat Daemon
 *
 *   Copyright (C) 2009-2010 Daniel De Graaf <danieldg@inspircd.org>
 *   Copyright (C) 2008 Dennis Friis <peavey@inspircd.org>
 *   Copyright (C) 2008 Robin Burchell <robin+git@viroteck.net>
 *   Copyright (C) 2008 Craig Edwards <craigedwards@brainbox.cc>
 *
 * This file is part of InspIRCd.  InspIRCd is free software: you can
 * redistribute it and/or modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#include "inspircd.h"
#include "xline.h"
#include "bancache.h"
#include "iohook.h"

UserManager::UserManager()
	: clientlist(new user_hash)
	, uuidlist(new user_hash)
	, unregistered_count(0)
{
}

UserManager::~UserManager()
{
	for (user_hash::iterator i = clientlist->begin(); i != clientlist->end(); ++i)
	{
		delete i->second;
	}

	delete clientlist;
	delete uuidlist;
}

/* add a client connection to the sockets list */
void UserManager::AddUser(int socket, ListenSocket* via, irc::sockets::sockaddrs* client, irc::sockets::sockaddrs* server)
{
	/* NOTE: Calling this one parameter constructor for User automatically
	 * allocates a new UUID and places it in the hash_map.
	 */
	LocalUser* New = NULL;
	try
	{
		New = new LocalUser(socket, client, server);
	}
	catch (...)
	{
		ServerInstance->Logs->Log("USERS", LOG_DEFAULT, "*** WTF *** Duplicated UUID! -- Crack smoking monkeys have been unleashed.");
		ServerInstance->SNO->WriteToSnoMask('a', "WARNING *** Duplicate UUID allocated!");
		return;
	}
	UserIOHandler* eh = &New->eh;

	// If this listener has an IO hook provider set then tell it about the connection
	if (via->iohookprov)
		via->iohookprov->OnAccept(eh, client, server);

	ServerInstance->Logs->Log("USERS", LOG_DEBUG, "New user fd: %d", socket);

	this->unregistered_count++;

	/* The users default nick is their UUID */
	New->nick = New->uuid;
	(*(this->clientlist))[New->nick] = New;

	New->registered = REG_NONE;
	New->signon = ServerInstance->Time() + ServerInstance->Config->dns_timeout;
	New->lastping = 1;

	ServerInstance->Users->AddLocalClone(New);
	ServerInstance->Users->AddGlobalClone(New);

	this->local_users.push_front(New);

	if ((this->local_users.size() > ServerInstance->Config->SoftLimit) || (this->local_users.size() >= (unsigned int)ServerInstance->SE->GetMaxFds()))
	{
		ServerInstance->SNO->WriteToSnoMask('a', "Warning: softlimit value has been reached: %d clients", ServerInstance->Config->SoftLimit);
		this->QuitUser(New,"No more connections allowed");
		return;
	}

	/*
	 * First class check. We do this again in FullConnect after DNS is done, and NICK/USER is recieved.
	 * See my note down there for why this is required. DO NOT REMOVE. :) -- w00t
	 */
	New->SetClass();

	/*
	 * Check connect class settings and initialise settings into User.
	 * This will be done again after DNS resolution. -- w00t
	 */
	New->CheckClass(ServerInstance->Config->CCOnConnect);
	if (New->quitting)
		return;

	/*
	 * even with bancache, we still have to keep User::exempt current.
	 * besides that, if we get a positive bancache hit, we still won't fuck
	 * them over if they are exempt. -- w00t
	 */
	New->exempt = (ServerInstance->XLines->MatchesLine("E",New) != NULL);

	if (BanCacheHit *b = ServerInstance->BanCache->GetHit(New->GetIPString()))
	{
		if (!b->Type.empty() && !New->exempt)
		{
			/* user banned */
			ServerInstance->Logs->Log("BANCACHE", LOG_DEBUG, "BanCache: Positive hit for " + New->GetIPString());
			if (!ServerInstance->Config->XLineMessage.empty())
				New->WriteNotice("*** " +  ServerInstance->Config->XLineMessage);
			this->QuitUser(New, b->Reason);
			return;
		}
		else
		{
			ServerInstance->Logs->Log("BANCACHE", LOG_DEBUG, "BanCache: Negative hit for " + New->GetIPString());
		}
	}
	else
	{
		if (!New->exempt)
		{
			XLine* r = ServerInstance->XLines->MatchesLine("Z",New);

			if (r)
			{
				r->Apply(New);
				return;
			}
		}
	}

	if (!ServerInstance->SE->AddFd(eh, FD_WANT_FAST_READ | FD_WANT_EDGE_WRITE))
	{
		ServerInstance->Logs->Log("USERS", LOG_DEBUG, "Internal error on new connection");
		this->QuitUser(New, "Internal error handling connection");
	}

	if (ServerInstance->Config->RawLog)
		New->WriteNotice("*** Raw I/O logging is enabled on this server. All messages, passwords, and commands are being recorded.");

	FOREACH_MOD(OnSetUserIP, (New));
	if (New->quitting)
		return;

	FOREACH_MOD(OnUserInit, (New));
}

void UserManager::QuitUser(User* user, const std::string& quitreason, const std::string* operreason)
{
	if (user->quitting)
	{
		ServerInstance->Logs->Log("USERS", LOG_DEFAULT, "ERROR: Tried to quit quitting user: " + user->nick);
		return;
	}

	if (IS_SERVER(user))
	{
		ServerInstance->Logs->Log("USERS", LOG_DEFAULT, "ERROR: Tried to quit server user: " + user->nick);
		return;
	}

	user->quitting = true;

	ServerInstance->Logs->Log("USERS", LOG_DEBUG, "QuitUser: %s=%s '%s'", user->uuid.c_str(), user->nick.c_str(), quitreason.c_str());
	user->Write("ERROR :Closing link: (%s@%s) [%s]", user->ident.c_str(), user->host.c_str(), operreason ? operreason->c_str() : quitreason.c_str());

	std::string reason;
	reason.assign(quitreason, 0, ServerInstance->Config->Limits.MaxQuit);
	if (!operreason)
		operreason = &reason;

	ServerInstance->GlobalCulls.AddItem(user);

	if (user->registered == REG_ALL)
	{
		FOREACH_MOD(OnUserQuit, (user, reason, *operreason));
		user->WriteCommonQuit(reason, *operreason);
	}
	else
		unregistered_count--;

	if (IS_LOCAL(user))
	{
		LocalUser* lu = IS_LOCAL(user);
		FOREACH_MOD(OnUserDisconnect, (lu));
		lu->eh.Close();

		if (lu->registered == REG_ALL)
			ServerInstance->SNO->WriteToSnoMask('q',"Client exiting: %s (%s) [%s]", user->GetFullRealHost().c_str(), user->GetIPString().c_str(), operreason->c_str());
	}

	user_hash::iterator iter = this->clientlist->find(user->nick);

	if (iter != this->clientlist->end())
		this->clientlist->erase(iter);
	else
		ServerInstance->Logs->Log("USERS", LOG_DEFAULT, "ERROR: Nick not found in clientlist, cannot remove: " + user->nick);

	uuidlist->erase(user->uuid);
	user->PurgeEmptyChannels();
}

void UserManager::AddLocalClone(User *user)
{
	local_clones[user->GetCIDRMask()]++;
}

void UserManager::AddGlobalClone(User *user)
{
	global_clones[user->GetCIDRMask()]++;
}

void UserManager::RemoveCloneCounts(User *user)
{
	if (IS_LOCAL(user))
	{
		clonemap::iterator x = local_clones.find(user->GetCIDRMask());
		if (x != local_clones.end())
		{
			x->second--;
			if (!x->second)
			{
				local_clones.erase(x);
			}
		}
	}

	clonemap::iterator y = global_clones.find(user->GetCIDRMask());
	if (y != global_clones.end())
	{
		y->second--;
		if (!y->second)
		{
			global_clones.erase(y);
		}
	}
}

unsigned long UserManager::GlobalCloneCount(User *user)
{
	clonemap::iterator x = global_clones.find(user->GetCIDRMask());
	if (x != global_clones.end())
		return x->second;
	else
		return 0;
}

unsigned long UserManager::LocalCloneCount(User *user)
{
	clonemap::iterator x = local_clones.find(user->GetCIDRMask());
	if (x != local_clones.end())
		return x->second;
	else
		return 0;
}

void UserManager::ServerNoticeAll(const char* text, ...)
{
	std::string message;
	VAFORMAT(message, text, text);
	message = "NOTICE $" + ServerInstance->Config->ServerName + " :" + message;

	for (LocalUserList::const_iterator i = local_users.begin(); i != local_users.end(); i++)
	{
		User* t = *i;
		t->WriteServ(message);
	}
}

void UserManager::GarbageCollect()
{
	// Reset the already_sent IDs so we don't wrap it around and drop a message
	LocalUser::already_sent_id = 0;
	for (LocalUserList::const_iterator i = this->local_users.begin(); i != this->local_users.end(); i++)
	{
		(**i).already_sent = 0;
		(**i).RemoveExpiredInvites();
	}
}

/* this returns true when all modules are satisfied that the user should be allowed onto the irc server
 * (until this returns true, a user will block in the waiting state, waiting to connect up to the
 * registration timeout maximum seconds)
 */
bool UserManager::AllModulesReportReady(LocalUser* user)
{
	ModResult res;
	FIRST_MOD_RESULT(OnCheckReady, res, (user));
	return (res == MOD_RES_PASSTHRU);
}

/**
 * This function is called once a second from the mainloop.
 * It is intended to do background checking on all the user structs, e.g.
 * stuff like ping checks, registration timeouts, etc.
 */
void UserManager::DoBackgroundUserStuff()
{
	/*
	 * loop over all local users..
	 */
	for (LocalUserList::iterator i = local_users.begin(); i != local_users.end(); ++i)
	{
		LocalUser* curr = *i;

		if (curr->quitting)
			continue;

		if (curr->CommandFloodPenalty || curr->eh.getSendQSize())
		{
			unsigned int rate = curr->MyClass->GetCommandRate();
			if (curr->CommandFloodPenalty > rate)
				curr->CommandFloodPenalty -= rate;
			else
				curr->CommandFloodPenalty = 0;
			curr->eh.OnDataReady();
		}

		switch (curr->registered)
		{
			case REG_ALL:
				if (ServerInstance->Time() > curr->nping)
				{
					// This user didn't answer the last ping, remove them
					if (!curr->lastping)
					{
						time_t time = ServerInstance->Time() - (curr->nping - curr->MyClass->GetPingTime());
						const std::string message = "Ping timeout: " + ConvToStr(time) + (time != 1 ? " seconds" : " second");
						this->QuitUser(curr, message);
						continue;
					}

					curr->Write("PING :" + ServerInstance->Config->ServerName);
					curr->lastping = 0;
					curr->nping = ServerInstance->Time() + curr->MyClass->GetPingTime();
				}
				break;
			case REG_NICKUSER:
				if (AllModulesReportReady(curr))
				{
					/* User has sent NICK/USER, modules are okay, DNS finished. */
					curr->FullConnect();
					continue;
				}
				break;
		}

		if (curr->registered != REG_ALL && (ServerInstance->Time() > (curr->age + curr->MyClass->GetRegTimeout())))
		{
			/*
			 * registration timeout -- didnt send USER/NICK/HOST
			 * in the time specified in their connection class.
			 */
			this->QuitUser(curr, "Registration timeout");
			continue;
		}
	}
}
