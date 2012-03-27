/*
 * Copyright (C) 2010-2012 Project SkyFire <http://www.projectskyfire.org/>
 * Copyright (C) 2010-2012 Oregon <http://www.oregoncore.com/>
 * Copyright (C) 2008-2012 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2005-2012 MaNGOS <http://getmangos.com/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "OutdoorPvPMgr.h"
#include "OutdoorPvPHP.h"
#include "OutdoorPvPNA.h"
#include "OutdoorPvPTF.h"
#include "OutdoorPvPZM.h"
#include "OutdoorPvPSI.h"
#include "OutdoorPvPEP.h"
#include "ObjectMgr.h"
#include "Player.h"

OutdoorPvPMgr::OutdoorPvPMgr()
{
    m_UpdateTimer = 0;
    //sLog->outDebug("Instantiating OutdoorPvPMgr");
}

OutdoorPvPMgr::~OutdoorPvPMgr()
{
    //sLog->outDebug("Deleting OutdoorPvPMgr");
    for (OutdoorPvPSet::iterator itr = m_OutdoorPvPSet.begin(); itr != m_OutdoorPvPSet.end(); ++itr)
        delete *itr;
}

void OutdoorPvPMgr::InitOutdoorPvP()
{
    // create new opvp
    OutdoorPvP * pOP = new OutdoorPvPHP;
    // respawn, init variables
    if (!pOP->SetupOutdoorPvP())
    {
        sLog->outDebug("OutdoorPvP : HP init failed.");
        delete pOP;
    }
    else
    {
        m_OutdoorPvPSet.push_back(pOP);
        sLog->outDebug("OutdoorPvP : HP successfully initiated.");
    }

    pOP = new OutdoorPvPNA;
    // respawn, init variables
    if (!pOP->SetupOutdoorPvP())
    {
        sLog->outDebug("OutdoorPvP : NA init failed.");
        delete pOP;
    }
    else
    {
        m_OutdoorPvPSet.push_back(pOP);
        sLog->outDebug("OutdoorPvP : NA successfully initiated.");
    }

    pOP = new OutdoorPvPTF;
    // respawn, init variables
    if (!pOP->SetupOutdoorPvP())
    {
        sLog->outDebug("OutdoorPvP : TF init failed.");
        delete pOP;
    }
    else
    {
        m_OutdoorPvPSet.push_back(pOP);
        sLog->outDebug("OutdoorPvP : TF successfully initiated.");
    }

    pOP = new OutdoorPvPZM;
    // respawn, init variables
    if (!pOP->SetupOutdoorPvP())
    {
        sLog->outDebug("OutdoorPvP : ZM init failed.");
        delete pOP;
    }
    else
    {
        m_OutdoorPvPSet.push_back(pOP);
        sLog->outDebug("OutdoorPvP : ZM successfully initiated.");
    }

    pOP = new OutdoorPvPSI;
    // respawn, init variables
    if (!pOP->SetupOutdoorPvP())
    {
        sLog->outDebug("OutdoorPvP : SI init failed.");
        delete pOP;
    }
    else
    {
        m_OutdoorPvPSet.push_back(pOP);
        sLog->outDebug("OutdoorPvP : SI successfully initiated.");
    }

    pOP = new OutdoorPvPEP;
    // respawn, init variables
    if (!pOP->SetupOutdoorPvP())
    {
        sLog->outDebug("OutdoorPvP : EP init failed.");
        delete pOP;
    }
    else
    {
        m_OutdoorPvPSet.push_back(pOP);
        sLog->outDebug("OutdoorPvP : EP successfully initiated.");
    }
}

void OutdoorPvPMgr::AddZone(uint32 zoneid, OutdoorPvP *handle)
{
    m_OutdoorPvPMap[zoneid] = handle;
}

void OutdoorPvPMgr::HandlePlayerEnterZone(Player* player, uint32 zoneid)
{
    OutdoorPvPMap::iterator itr = m_OutdoorPvPMap.find(zoneid);
    if (itr == m_OutdoorPvPMap.end())
        return;

    if (itr->second->HasPlayer(player))
        return;

    itr->second->HandlePlayerEnterZone(player, zoneid);
    sLog->outDebug("Player %u entered outdoorpvp id %u", player->GetGUIDLow(), itr->second->GetTypeId());
}

void OutdoorPvPMgr::HandlePlayerLeaveZone(Player* player, uint32 zoneid)
{
    OutdoorPvPMap::iterator itr = m_OutdoorPvPMap.find(zoneid);
    if (itr == m_OutdoorPvPMap.end())
        return;

    // teleport: remove once in removefromworld, once in updatezone
    if (!itr->second->HasPlayer(player))
        return;

    itr->second->HandlePlayerLeaveZone(player, zoneid);
    sLog->outDebug("Player %u left outdoorpvp id %u", player->GetGUIDLow(), itr->second->GetTypeId());
}

OutdoorPvP * OutdoorPvPMgr::GetOutdoorPvPToZoneId(uint32 zoneid)
{
    OutdoorPvPMap::iterator itr = m_OutdoorPvPMap.find(zoneid);
    if (itr == m_OutdoorPvPMap.end())
    {
        // no handle for this zone, return
        return NULL;
    }
    return itr->second;
}

void OutdoorPvPMgr::Update(uint32 diff)
{
    m_UpdateTimer += diff;
    if (m_UpdateTimer > OUTDOORPVP_OBJECTIVE_UPDATE_INTERVAL)
    {
        for (OutdoorPvPSet::iterator itr = m_OutdoorPvPSet.begin(); itr != m_OutdoorPvPSet.end(); ++itr)
            (*itr)->Update(m_UpdateTimer);
        m_UpdateTimer = 0;
    }
}

bool OutdoorPvPMgr::HandleCustomSpell(Player* player, uint32 spellId, GameObject * go)
{
    for (OutdoorPvPSet::iterator itr = m_OutdoorPvPSet.begin(); itr != m_OutdoorPvPSet.end(); ++itr)
    {
        if ((*itr)->HandleCustomSpell(player, spellId, go))
            return true;
    }
    return false;
}

ZoneScript * OutdoorPvPMgr::GetZoneScript(uint32 zoneId)
{
    OutdoorPvPMap::iterator itr = m_OutdoorPvPMap.find(zoneId);
    if (itr != m_OutdoorPvPMap.end())
        return itr->second;
    else
        return NULL;
}

bool OutdoorPvPMgr::HandleOpenGo(Player* player, uint64 guid)
{
    for (OutdoorPvPSet::iterator itr = m_OutdoorPvPSet.begin(); itr != m_OutdoorPvPSet.end(); ++itr)
    {
        if ((*itr)->HandleOpenGo(player, guid))
            return true;
    }
    return false;
}

void OutdoorPvPMgr::HandleGossipOption(Player* player, uint64 guid, uint32 gossipid)
{
    for (OutdoorPvPSet::iterator itr = m_OutdoorPvPSet.begin(); itr != m_OutdoorPvPSet.end(); ++itr)
    {
        if ((*itr)->HandleGossipOption(player, guid, gossipid))
            return;
    }
}

bool OutdoorPvPMgr::CanTalkTo(Player* player, Creature * c, GossipMenuItems gso)
{
    for (OutdoorPvPSet::iterator itr = m_OutdoorPvPSet.begin(); itr != m_OutdoorPvPSet.end(); ++itr)
    {
        if ((*itr)->CanTalkTo(player, c, gso))
            return true;
    }
    return false;
}

void OutdoorPvPMgr::HandleDropFlag(Player* player, uint32 spellId)
{
    for (OutdoorPvPSet::iterator itr = m_OutdoorPvPSet.begin(); itr != m_OutdoorPvPSet.end(); ++itr)
    {
        if ((*itr)->HandleDropFlag(player, spellId))
            return;
    }
}

