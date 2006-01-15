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

#include "inspircd_config.h"
#include "base.h"
#include <time.h>
#include <map>
#include <deque>
#include <string>
#include "inspircd.h"
#include "modules.h"
#include "helperfuncs.h"

const int bitfields[]           =       {1,2,4,8,16,32,64,128};
const int inverted_bitfields[]  =       {~1,~2,~4,~8,~16,~32,~64,~128};

extern time_t TIME;

bool Extensible::Extend(std::string key, char* p)
{
	// only add an item if it doesnt already exist
	if (this->Extension_Items.find(key) == this->Extension_Items.end())
	{
		this->Extension_Items[key] = p;
		log(DEBUG,"Extending object with item %s",key.c_str());
		return true;
	}
	// item already exists, return false
	return false;
}

bool Extensible::Shrink(std::string key)
{
	// only attempt to remove a map item that exists
	if (this->Extension_Items.find(key) != this->Extension_Items.end())
	{
		this->Extension_Items.erase(this->Extension_Items.find(key));
		log(DEBUG,"Shrinking object with item %s",key.c_str());
		return true;
	}
	return false;
}

char* Extensible::GetExt(std::string key)
{
	if (this->Extension_Items.find(key) != this->Extension_Items.end())
	{
		return (this->Extension_Items.find(key))->second;
	}
	return NULL;
}

void Extensible::GetExtList(std::deque<std::string> &list)
{
	for (std::map<std::string,char*>::iterator u = Extension_Items.begin(); u != Extension_Items.end(); u++)
	{
		list.push_back(u->first);
	}
}

void BoolSet::Set(int number)
{
	this->bits |= bitfields[number];
}

void BoolSet::Unset(int number)
{
	this->bits &= inverted_bitfields[number];
}

void BoolSet::Invert(int number)
{
	this->bits ^= bitfields[number];
}

bool BoolSet::Get(int number)
{
	return ((this->bits | bitfields[number]) > 0);
}

bool BoolSet::operator==(BoolSet other)
{
	return (this->bits == other.bits);
}

BoolSet BoolSet::operator|(BoolSet other)
{
	BoolSet x(this->bits | other.bits);
	return x;
}

BoolSet BoolSet::operator&(BoolSet other)
{
	BoolSet x(this->bits & other.bits);
	return x;
}

BoolSet::BoolSet()
{
	this->bits = 0;
}

BoolSet::BoolSet(char bitmask)
{
	this->bits = bitmask;
}

bool BoolSet::operator=(BoolSet other)
{
        this->bits = other.bits;
        return true;
}
