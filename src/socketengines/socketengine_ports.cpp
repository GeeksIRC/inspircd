/*
 * InspIRCd -- Internet Relay Chat Daemon
 *
 *   Copyright (C) 2009 Daniel De Graaf <danieldg@inspircd.org>
 *   Copyright (C) 2007-2008 Craig Edwards <craigedwards@brainbox.cc>
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
#include "exitcodes.h"

#ifndef __sun
# error You need Solaris 10 or later to make use of this code.
#endif

#include <vector>
#include <string>
#include <map>
#include "inspircd.h"
#include "socketengine.h"
#include <port.h>
#include <iostream>
#include <ulimit.h>

/** A specialisation of the SocketEngine class, designed to use solaris 10 I/O completion ports
 */
class PortsEngine : public SocketEngine
{
private:
	/** These are used by ports to hold socket events
	 */
	std::vector<port_event_t> events;
	int EngineHandle;
public:
	/** Create a new PortsEngine
	 */
	PortsEngine();
	/** Delete a PortsEngine
	 */
	virtual ~PortsEngine();
	virtual bool AddFd(EventHandler* eh, int event_mask);
	virtual void OnSetEvent(EventHandler* eh, int old_mask, int new_mask);
	virtual void DelFd(EventHandler* eh);
	virtual int DispatchEvents();
	virtual std::string GetName();
};

PortsEngine::PortsEngine() : events(1)
{
	int max = ulimit(4, 0);
	if (max > 0)
	{
		MAX_DESCRIPTORS = max;
	}
	else
	{
		ServerInstance->Logs->Log("SOCKET", LOG_DEFAULT, "ERROR: Can't determine maximum number of open sockets!");
		std::cout << "ERROR: Can't determine maximum number of open sockets!" << std::endl;
		ServerInstance->QuickExit(EXIT_STATUS_SOCKETENGINE);
	}
	EngineHandle = port_create();

	if (EngineHandle == -1)
	{
		ServerInstance->Logs->Log("SOCKET", LOG_SPARSE, "ERROR: Could not initialize socket engine: %s", strerror(errno));
		ServerInstance->Logs->Log("SOCKET", LOG_SPARSE, "ERROR: This is a fatal error, exiting now.");
		std::cout << "ERROR: Could not initialize socket engine: " << strerror(errno) << std::endl;
		std::cout << "ERROR: This is a fatal error, exiting now." << std::endl;
		ServerInstance->QuickExit(EXIT_STATUS_SOCKETENGINE);
	}
	CurrentSetSize = 0;
}

PortsEngine::~PortsEngine()
{
	this->Close(EngineHandle);
}

static int mask_to_events(int event_mask)
{
	int rv = 0;
	if (event_mask & (FD_WANT_POLL_READ | FD_WANT_FAST_READ))
		rv |= POLLRDNORM;
	if (event_mask & (FD_WANT_POLL_WRITE | FD_WANT_FAST_WRITE | FD_WANT_SINGLE_WRITE))
		rv |= POLLWRNORM;
	return rv;
}

bool PortsEngine::AddFd(EventHandler* eh, int event_mask)
{
	int fd = eh->GetFd();
	if ((fd < 0) || (fd > GetMaxFds() - 1))
		return false;

	if (!SocketEngine::AddFd(eh))
		return false;

	SocketEngine::SetEventMask(eh, event_mask);
	port_associate(EngineHandle, PORT_SOURCE_FD, fd, mask_to_events(event_mask), eh);

	ServerInstance->Logs->Log("SOCKET", LOG_DEBUG, "New file descriptor: %d", fd);
	CurrentSetSize++;
	ResizeDouble(events);

	return true;
}

void PortsEngine::OnSetEvent(EventHandler* eh, int old_mask, int new_mask)
{
	if (mask_to_events(new_mask) != mask_to_events(old_mask))
		port_associate(EngineHandle, PORT_SOURCE_FD, eh->GetFd(), mask_to_events(new_mask), eh);
}

void PortsEngine::DelFd(EventHandler* eh)
{
	int fd = eh->GetFd();
	if ((fd < 0) || (fd > GetMaxFds() - 1))
		return;

	port_dissociate(EngineHandle, PORT_SOURCE_FD, fd);

	CurrentSetSize--;
	SocketEngine::DelFd(eh);

	ServerInstance->Logs->Log("SOCKET", LOG_DEBUG, "Remove file descriptor: %d", fd);
}

int PortsEngine::DispatchEvents()
{
	struct timespec poll_time;

	poll_time.tv_sec = 1;
	poll_time.tv_nsec = 0;

	unsigned int nget = 1; // used to denote a retrieve request.
	int ret = port_getn(EngineHandle, &events[0], events.size(), &nget, &poll_time);
	ServerInstance->UpdateTime();

	// first handle an error condition
	if (ret == -1)
		return -1;

	TotalEvents += nget;

	unsigned int i;
	for (i = 0; i < nget; i++)
	{
		port_event_t& ev = events[i];

		if (ev.portev_source != PORT_SOURCE_FD)
			continue;

		int fd = ev.portev_object;
		EventHandler* eh = GetRef(fd);
		if (!eh)
			continue;

		int mask = eh->GetEventMask();
		if (ev.portev_events & POLLWRNORM)
			mask &= ~(FD_WRITE_WILL_BLOCK | FD_WANT_FAST_WRITE | FD_WANT_SINGLE_WRITE);
		if (ev.portev_events & POLLRDNORM)
			mask &= ~FD_READ_WILL_BLOCK;
		// reinsert port for next time around, pretending to be one-shot for writes
		SetEventMask(eh, mask);
		port_associate(EngineHandle, PORT_SOURCE_FD, fd, mask_to_events(mask), eh);
		if (ev.portev_events & POLLRDNORM)
		{
			ReadEvents++;
			eh->HandleEvent(EVENT_READ);
			if (eh != GetRef(fd))
				continue;
		}
		if (ev.portev_events & POLLWRNORM)
		{
			WriteEvents++;
			eh->HandleEvent(EVENT_WRITE);
		}
	}

	return (int)i;
}

std::string PortsEngine::GetName()
{
	return "ports";
}

SocketEngine* CreateSocketEngine()
{
	return new PortsEngine;
}
