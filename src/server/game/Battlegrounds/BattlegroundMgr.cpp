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

#include "Common.h"
#include "Player.h"
#include "BattlegroundMgr.h"
#include "BattlegroundAV.h"
#include "BattlegroundAB.h"
#include "BattlegroundEY.h"
#include "BattlegroundWS.h"
#include "BattlegroundNA.h"
#include "BattlegroundBE.h"
#include "BattlegroundAA.h"
#include "BattlegroundRL.h"
#include "SharedDefines.h"
#include "MapManager.h"
#include "Map.h"
#include "MapInstanced.h"
#include "ObjectMgr.h"

#include "World.h"
#include "Chat.h"
#include "ArenaTeam.h"

/*********************************************************/
/***            BATTLEGROUND MANAGER                   ***/
/*********************************************************/

BattlegroundMgr::BattlegroundMgr()
{
    m_Battlegrounds.clear();
    m_AutoDistributePoints = (bool)sWorld->getConfig(CONFIG_ARENA_AUTO_DISTRIBUTE_POINTS);
    m_MaxRatingDifference = sWorld->getConfig(CONFIG_ARENA_MAX_RATING_DIFFERENCE);
    m_RatingDiscardTimer = sWorld->getConfig(CONFIG_ARENA_RATING_DISCARD_TIMER);
    m_PrematureFinishTimer = sWorld->getConfig(CONFIG_BATTLEGROUND_PREMATURE_FINISH_TIMER);
    m_NextRatingDiscardUpdate = m_RatingDiscardTimer;
    m_AutoDistributionTimeChecker = 0;
    m_ArenaTesting = false;
    m_Testing = false;
}

BattlegroundMgr::~BattlegroundMgr()
{
    DeleteAlllBattlegrounds();
}

void BattlegroundMgr::DeleteAlllBattlegrounds()
{
    for (BattlegroundSet::iterator itr = m_Battlegrounds.begin(); itr != m_Battlegrounds.end();)
    {
        Battleground * bg = itr->second;
        m_Battlegrounds.erase(itr++);
        delete bg;
    }

    // destroy template battlegrounds that listed only in queues (other already terminated)
    for (uint32 bgTypeId = 0; bgTypeId < MAX_BATTLEGROUND_TYPE_ID; ++bgTypeId)
    {
        // ~Battleground call unregistring BG from queue
        while (!BGFreeSlotQueue[bgTypeId].empty())
            delete BGFreeSlotQueue[bgTypeId].front();
    }
}

// used to update running battlegrounds, and delete finished ones
void BattlegroundMgr::Update(time_t diff)
{
    BattlegroundSet::iterator itr, next;
    for (itr = m_Battlegrounds.begin(); itr != m_Battlegrounds.end(); itr = next)
    {
        next = itr;
        ++next;
        itr->second->Update(diff);
        // use the SetDeleteThis variable
        // direct deletion caused crashes
        if (itr->second->m_SetDeleteThis)
        {
            Battleground * bg = itr->second;
            m_Battlegrounds.erase(itr);
            delete bg;
        }
    }
    // if rating difference counts, maybe force-update queues
    if (m_MaxRatingDifference)
    {
        // it's time to force update
        if (m_NextRatingDiscardUpdate < diff)
        {
            // forced update for level 70 rated arenas
            m_BattlegroundQueues[BATTLEGROUND_QUEUE_2v2].Update(BATTLEGROUND_AA, 6, ARENA_TYPE_2v2, true, 0);
            m_BattlegroundQueues[BATTLEGROUND_QUEUE_3v3].Update(BATTLEGROUND_AA, 6, ARENA_TYPE_3v3, true, 0);
            m_BattlegroundQueues[BATTLEGROUND_QUEUE_5v5].Update(BATTLEGROUND_AA, 6, ARENA_TYPE_5v5, true, 0);
            m_NextRatingDiscardUpdate = m_RatingDiscardTimer;
        }
        else
            m_NextRatingDiscardUpdate -= diff;
    }
    if (m_AutoDistributePoints)
    {
        if (m_AutoDistributionTimeChecker < diff)
        {
            if (time(NULL) > m_NextAutoDistributionTime)
            {
                DistributeArenaPoints();
                m_NextAutoDistributionTime = time(NULL) + BATTLEGROUND_ARENA_POINT_DISTRIBUTION_DAY * sWorld->getConfig(CONFIG_ARENA_AUTO_DISTRIBUTE_INTERVAL_DAYS);
                CharacterDatabase.PExecute("UPDATE saved_variables SET NextArenaPointDistributionTime = '"UI64FMTD"'", m_NextAutoDistributionTime);
            }
            m_AutoDistributionTimeChecker = 600000; // check 10 minutes
        }
        else
            m_AutoDistributionTimeChecker -= diff;
    }
}

void BattlegroundMgr::BuildBattlegroundStatusPacket(WorldPacket *data, Battleground *bg, uint32 team, uint8 QueueSlot, uint8 StatusID, uint32 Time1, uint32 Time2, uint32 arenatype, uint8 israted)
{
    // we can be in 3 queues in same time...
    if (StatusID == 0)
    {
        data->Initialize(SMSG_BATTLEFIELD_STATUS, 4*3);
        *data << uint32(QueueSlot);                         // queue id (0...2)
        *data << uint64(0);
        return;
    }

    data->Initialize(SMSG_BATTLEFIELD_STATUS, (4+1+1+4+2+4+1+4+4+4));
    *data << uint32(QueueSlot);                             // queue id (0...2) - player can be in 3 queues in time
    // The following segment is read as uint64 in client but can be appended as their original type.
    *data << uint8(arenatype ? arenatype : bg->GetArenaType());
    *data << uint8(bg->isArena() ? 0x0D : 0x2);
    *data << uint32(bg->GetTypeID());
    *data << uint16(0x1F90);
    // End of uint64 segment, decomposed this way for simplicity
    *data << uint32(0);                                   // unknown
    // alliance/horde for BG and skirmish/rated for Arenas
    // following displays the minimap-icon 0 = faction icon 1 = arenaicon
    *data << uint8(israted ? israted : bg->isRated());                              // 1 for rated match, 0 for bg or non rated match
/*    *data << uint8(arenatype ? arenatype : bg->GetArenaType());                     // team type (0=BG, 2=2x2, 3=3x3, 5=5x5), for arenas    // NOT PROPER VALUE IF ARENA ISN'T RUNNING YET!!!!
    switch (bg->GetTypeID())                                 // value depends on bg id
    {
        case BATTLEGROUND_AV:
            *data << uint8(1);
            break;
        case BATTLEGROUND_WS:
            *data << uint8(2);
            break;
        case BATTLEGROUND_AB:
            *data << uint8(3);
            break;
        case BATTLEGROUND_NA:
            *data << uint8(4);
            break;
        case BATTLEGROUND_BE:
            *data << uint8(5);
            break;
        case BATTLEGROUND_AA:
            *data << uint8(6);
            break;
        case BATTLEGROUND_EY:
            *data << uint8(7);
            break;
        case BATTLEGROUND_RL:
            *data << uint8(8);
            break;
        default:                                            // unknown
            *data << uint8(0);
            break;
    }

    if (bg->isArena() && (StatusID == STATUS_WAIT_QUEUE))
        *data << uint32(BATTLEGROUND_AA);                   // all arenas   I don't think so.
    else
    *data << uint32(bg->GetTypeID());                   // BG id from DBC

    *data << uint16(0x1F90);                                // unk value 8080
    *data << uint32(bg->GetInstanceID());                   // instance id

    if (bg->isBattleground())
        *data << uint8(bg->GetTeamIndexByTeamId(team));     // team
    else
        *data << uint8(israted?israted:bg->isRated());                      // is rated battle
*/
    *data << uint32(StatusID);                              // status
    switch (StatusID)
    {
        case STATUS_WAIT_QUEUE:                             // status_in_queue
            *data << uint32(Time1);                         // average wait time, milliseconds
            *data << uint32(Time2);                         // time in queue, updated every minute?
            break;
        case STATUS_WAIT_JOIN:                              // status_invite
            *data << uint32(bg->GetMapId());                // map id
            *data << uint32(Time1);                         // time to remove from queue, milliseconds
            break;
        case STATUS_IN_PROGRESS:                            // status_in_progress
            *data << uint32(bg->GetMapId());                // map id
            *data << uint32(Time1);                         // 0 at bg start, 120000 after bg end, time to bg auto leave, milliseconds
            *data << uint32(Time2);                         // time from bg start, milliseconds
            *data << uint8(0x1);                            // Lua_GetBattlefieldArenaFaction (bool)
            break;
        default:
            sLog->outError("Unknown BG status!");
            break;
    }
}

void BattlegroundMgr::BuildPvpLogDataPacket(WorldPacket *data, Battleground *bg)
{
    uint8 type = (bg->isArena() ? 1 : 0);
                                                            // last check on 2.4.1
    data->Initialize(MSG_PVP_LOG_DATA, (1+1+4+40*bg->GetPlayerScoresSize()));
    *data << uint8(type);                                   // seems to be type (battleground=0/arena=1)

    if (type)                                                // arena
    {
        // it seems this must be according to BG_WINNER_A/H and _NOT_ BG_TEAM_A/H
        for (int8 i = 1; i >= 0; --i)
        {
            *data << uint32(3000-bg->m_ArenaTeamRatingChanges[i]);                      // rating change: showed value - 3000
            *data << uint32(3999);  // huge thanks for TOM_RUS for this!
            sLog->outDebug("rating change: %d", bg->m_ArenaTeamRatingChanges[i]);
        }
        for (int8 i = 1; i >= 0; --i)
        {
            uint32 at_id = bg->m_ArenaTeamIds[i];
            ArenaTeam* at = sObjectMgr->GetArenaTeamById(at_id);
            if (at)
                *data << at->GetName();
            else
                *data << uint8(0);
        }
    }

    if (bg->GetWinner() == 2)
    {
        *data << uint8(0);                                  // bg in progress
    }
    else
    {
        *data << uint8(1);                                  // bg ended
        *data << uint8(bg->GetWinner());                    // who win
    }

    size_t wpos = data->wpos();
    uint32 scoreCount = 0;
    *data << uint32(scoreCount);                            // placeholder

    Battleground::BattlegroundScoreMap::const_iterator itr2 = bg->GetPlayerScoresBegin();
    for (Battleground::BattlegroundScoreMap::const_iterator itr = itr2; itr != bg->GetPlayerScoresEnd();)
    {
        itr2 = itr++;
        if (!bg->IsPlayerInBattleground(itr2->first))
        {
            sLog->outError("Player " UI64FMTD " has scoreboard entry for battleground %u but is not in battleground!", itr->first, bg->GetTypeID());
            continue;
        }

        *data << uint64(itr2->first);
        *data << uint32(itr2->second->KillingBlows);
        if (type == 0)
        {
            *data << uint32(itr2->second->HonorableKills);
            *data << uint32(itr2->second->Deaths);
            *data << uint32(itr2->second->BonusHonor);
        }
        else
        {
            Player *plr = sObjectMgr->GetPlayer(itr2->first);
            uint32 team = bg->GetPlayerTeam(itr2->first);
            if (!team && plr)
                team = plr->GetBGTeam();
            *data << uint8(team == ALLIANCE ? 1 : 0); // green or yellow
        }
        *data << uint32(itr2->second->DamageDone);              // damage done
        *data << uint32(itr2->second->HealingDone);             // healing done
        switch (bg->GetTypeID())                                 // battleground specific things
        {
            case BATTLEGROUND_AV:
                *data << uint32(0x00000005);                    // count of next fields
                *data << uint32(((BattlegroundAVScore*)itr2->second)->GraveyardsAssaulted); // GraveyardsAssaulted
                *data << uint32(((BattlegroundAVScore*)itr2->second)->GraveyardsDefended);  // GraveyardsDefended
                *data << uint32(((BattlegroundAVScore*)itr2->second)->TowersAssaulted);     // TowersAssaulted
                *data << uint32(((BattlegroundAVScore*)itr2->second)->TowersDefended);      // TowersDefended
                *data << uint32(((BattlegroundAVScore*)itr2->second)->MinesCaptured);       // MinesCaptured
                break;
            case BATTLEGROUND_WS:
                *data << uint32(0x00000002);                    // count of next fields
                *data << uint32(((BattlegroundWGScore*)itr2->second)->FlagCaptures);        // flag captures
                *data << uint32(((BattlegroundWGScore*)itr2->second)->FlagReturns);         // flag returns
                break;
            case BATTLEGROUND_AB:
                *data << uint32(0x00000002);                    // count of next fields
                *data << uint32(((BattlegroundABScore*)itr2->second)->BasesAssaulted);      // bases asssulted
                *data << uint32(((BattlegroundABScore*)itr2->second)->BasesDefended);       // bases defended
                break;
            case BATTLEGROUND_EY:
                *data << uint32(0x00000001);                    // count of next fields
                *data << uint32(((BattlegroundEYScore*)itr2->second)->FlagCaptures);        // flag captures
                break;
            case BATTLEGROUND_NA:
            case BATTLEGROUND_BE:
            case BATTLEGROUND_AA:
            case BATTLEGROUND_RL:
                *data << uint32(0);
                break;
            default:
                sLog->outDebug("Unhandled MSG_PVP_LOG_DATA for BG id %u", bg->GetTypeID());
                *data << uint32(0);
                break;
        }
        // should never happen
        if (++scoreCount >= bg->GetMaxPlayers() && itr != bg->GetPlayerScoresEnd())
        {
            sLog->outError("Battleground %u scoreboard has more entries (%u) than allowed players in this bg (%u)", bg->GetTypeID(), bg->GetPlayerScoresSize(), bg->GetMaxPlayers());
            break;
        }
    }

    data->put(wpos, scoreCount);
}

void BattlegroundMgr::BuildGroupJoinedBattlegroundPacket(WorldPacket *data, uint32 bgTypeId)
{
    /*bgTypeId is:
    0 - Your group has joined a battleground queue, but you are not eligible
    1 - Your group has joined the queue for AV
    2 - Your group has joined the queue for WS
    3 - Your group has joined the queue for AB
    4 - Your group has joined the queue for NA
    5 - Your group has joined the queue for BE Arena
    6 - Your group has joined the queue for All Arenas
    7 - Your group has joined the queue for EotS*/
    data->Initialize(SMSG_GROUP_JOINED_BATTLEGROUND, 4);
    *data << uint32(bgTypeId);
}

void BattlegroundMgr::BuildUpdateWorldStatePacket(WorldPacket *data, uint32 field, uint32 value)
{
    data->Initialize(SMSG_UPDATE_WORLD_STATE, 4+4);
    *data << uint32(field);
    *data << uint32(value);
}

void BattlegroundMgr::BuildPlaySoundPacket(WorldPacket *data, uint32 soundid)
{
    data->Initialize(SMSG_PLAY_SOUND, 4);
    *data << uint32(soundid);
}

void BattlegroundMgr::BuildPlayerLeftBattlegroundPacket(WorldPacket *data, const uint64& guid)
{
    data->Initialize(SMSG_BATTLEGROUND_PLAYER_LEFT, 8);
    *data << uint64(guid);
}

void BattlegroundMgr::BuildPlayerJoinedBattlegroundPacket(WorldPacket *data, Player *plr)
{
    data->Initialize(SMSG_BATTLEGROUND_PLAYER_JOINED, 8);
    *data << uint64(plr->GetGUID());
}

void BattlegroundMgr::InvitePlayer(Player* plr, uint32 bgInstanceGUID, uint32 team)
{
    // set invited player counters:
    Battleground* bg = GetBattleground(bgInstanceGUID);
    if (!bg)
        return;
    bg->IncreaseInvitedCount(team);

    plr->SetInviteForBattlegroundQueueType(BGQueueTypeId(bg->GetTypeID(), bg->GetArenaType()), bgInstanceGUID);

    // set the arena teams for rated matches
    if (bg->isArena() && bg->isRated())
    {
        switch (bg->GetArenaType())
        {
        case ARENA_TYPE_2v2:
            bg->SetArenaTeamIdForTeam(team, plr->GetArenaTeamId(0));
            break;
        case ARENA_TYPE_3v3:
            bg->SetArenaTeamIdForTeam(team, plr->GetArenaTeamId(1));
            break;
        case ARENA_TYPE_5v5:
            bg->SetArenaTeamIdForTeam(team, plr->GetArenaTeamId(2));
            break;
        default:
            break;
        }
    }

    // create invite events:
    //add events to player's counters ---- this is not good way - there should be something like global event processor, where we should add those events
    BGQueueInviteEvent* inviteEvent = new BGQueueInviteEvent(plr->GetGUID(), bgInstanceGUID);
    plr->m_Events.AddEvent(inviteEvent, plr->m_Events.CalculateTime(INVITE_ACCEPT_WAIT_TIME/2));
    BGQueueRemoveEvent* removeEvent = new BGQueueRemoveEvent(plr->GetGUID(), bgInstanceGUID, team);
    plr->m_Events.AddEvent(removeEvent, plr->m_Events.CalculateTime(INVITE_ACCEPT_WAIT_TIME));
}

Battleground * BattlegroundMgr::GetBattlegroundTemplate(uint32 bgTypeId)
{
    return BGFreeSlotQueue[bgTypeId].empty() ? NULL : BGFreeSlotQueue[bgTypeId].back();
}

// create a new battleground that will really be used to play
Battleground * BattlegroundMgr::CreateNewBattleground(uint32 bgTypeId, uint8 arenaType, bool isRated)
{
    // get the template BG
    Battleground *bg_template = GetBattlegroundTemplate(bgTypeId);

    if (!bg_template)
    {
        sLog->outError("Battleground: CreateNewBattleground - bg template not found for %u", bgTypeId);
        return 0;
    }

    Battleground *bg = NULL;

    // create a copy of the BG template
    switch (bgTypeId)
    {
        case BATTLEGROUND_AV:
            bg = new BattlegroundAV(*(BattlegroundAV*)bg_template);
            break;
        case BATTLEGROUND_WS:
            bg = new BattlegroundWS(*(BattlegroundWS*)bg_template);
            break;
        case BATTLEGROUND_AB:
            bg = new BattlegroundAB(*(BattlegroundAB*)bg_template);
            break;
        case BATTLEGROUND_NA:
            bg = new BattlegroundNA(*(BattlegroundNA*)bg_template);
            break;
        case BATTLEGROUND_BE:
            bg = new BattlegroundBE(*(BattlegroundBE*)bg_template);
            break;
        case BATTLEGROUND_AA:
            bg = new BattlegroundAA(*(BattlegroundAA*)bg_template);
            break;
        case BATTLEGROUND_EY:
            bg = new BattlegroundEY(*(BattlegroundEY*)bg_template);
            break;
        case BATTLEGROUND_RL:
            bg = new BattlegroundRL(*(BattlegroundRL*)bg_template);
            break;
        default:
            //bg = new Battleground;
            return 0;
            break;             // placeholder for non implemented BG
    }

    // generate a new instance id
    bg->SetInstanceID(sMapMgr->GenerateInstanceId()); // set instance id

    // reset the new bg (set status to status_wait_queue from status_none)
    bg->Reset();

    /*   will be setup in BG::Update() when the first player is ported in
    if (!(bg->SetupBattleground()))
    {
        sLog->outError("Battleground: CreateNewBattleground: SetupBattleground failed for bg %u", bgTypeId);
        delete bg;
        return 0;
    }
    */

    // add BG to free slot queue
    bg->AddToBGFreeSlotQueue();

    bg->SetStatus(STATUS_WAIT_JOIN);
    bg->SetArenaType(arenaType);
    bg->SetRated(isRated);
    bg->SetTypeID(bgTypeId);

    // add bg to update list
    AddBattleground(bg->GetInstanceID(), bg);

    return bg;
}

// used to create the BG templates
uint32 BattlegroundMgr::CreateBattleground(uint32 bgTypeId, uint32 MinPlayersPerTeam, uint32 MaxPlayersPerTeam, uint32 LevelMin, uint32 LevelMax, char* BattlegroundName, uint32 MapID, float Team1StartLocX, float Team1StartLocY, float Team1StartLocZ, float Team1StartLocO, float Team2StartLocX, float Team2StartLocY, float Team2StartLocZ, float Team2StartLocO)
{
    // Create the BG
    Battleground *bg = NULL;

    switch (bgTypeId)
    {
        case BATTLEGROUND_AV: bg = new BattlegroundAV; break;
        case BATTLEGROUND_WS: bg = new BattlegroundWS; break;
        case BATTLEGROUND_AB: bg = new BattlegroundAB; break;
        case BATTLEGROUND_NA: bg = new BattlegroundNA; break;
        case BATTLEGROUND_BE: bg = new BattlegroundBE; break;
        case BATTLEGROUND_AA: bg = new BattlegroundAA; break;
        case BATTLEGROUND_EY: bg = new BattlegroundEY; break;
        case BATTLEGROUND_RL: bg = new BattlegroundRL; break;
        default:              bg = new Battleground;   break;                           // placeholder for non implemented BG
    }

    bg->SetMapId(MapID);

    bg->Reset();

    BattlemasterListEntry const *bl = sBattlemasterListStore.LookupEntry(bgTypeId);
    //in previous method is checked if exists entry in sBattlemasterListStore, so no check needed
    if (bl)
    {
        bg->SetArenaorBGType(bl->type == TYPE_ARENA);
    }

    bg->SetTypeID(bgTypeId);
    bg->SetInstanceID(0);                               // template bg, instance id is 0
    bg->SetMinPlayersPerTeam(MinPlayersPerTeam);
    bg->SetMaxPlayersPerTeam(MaxPlayersPerTeam);
    bg->SetMinPlayers(MinPlayersPerTeam*2);
    bg->SetMaxPlayers(MaxPlayersPerTeam*2);
    bg->SetName(BattlegroundName);
    bg->SetTeamStartLoc(ALLIANCE, Team1StartLocX, Team1StartLocY, Team1StartLocZ, Team1StartLocO);
    bg->SetTeamStartLoc(HORDE,    Team2StartLocX, Team2StartLocY, Team2StartLocZ, Team2StartLocO);
    bg->SetLevelRange(LevelMin, LevelMax);

    //add Battleground instance to FreeSlotQueue (.back() will return the template!)
    bg->AddToBGFreeSlotQueue();

    // do NOT add to update list, since this is a template battleground!

    // return some not-null value, bgTypeId is good enough for me
    return bgTypeId;
}

void BattlegroundMgr::CreateInitialBattlegrounds()
{
    float AStartLoc[4];
    float HStartLoc[4];
    uint32 MaxPlayersPerTeam, MinPlayersPerTeam, MinLvl, MaxLvl, start1, start2;
    BattlemasterListEntry const *bl;
    WorldSafeLocsEntry const *start;

    uint32 count = 0;

    //                                                       0   1                 2                 3      4      5                6              7             8
    QueryResult_AutoPtr result = WorldDatabase.Query("SELECT id, MinPlayersPerTeam, MaxPlayersPerTeam, MinLvl, MaxLvl, AllianceStartLoc, AllianceStartO, HordeStartLoc, HordeStartO FROM battleground_template");

    if (!result)
    {
        sLog->outString();
        sLog->outErrorDb(">> Loaded 0 battlegrounds. DB table battleground_template is empty.");
        return;
    }

    do
    {
        Field *fields = result->Fetch();
        uint32 bgTypeID = fields[0].GetUInt32();

        // can be overwritten by values from DB
        bl = sBattlemasterListStore.LookupEntry(bgTypeID);
        if (!bl)
        {
            sLog->outError("Battleground ID %u not found in BattlemasterList.dbc. Battleground not created.", bgTypeID);
            continue;
        }

        MaxPlayersPerTeam = bl->maxplayersperteam;
        MinPlayersPerTeam = bl->maxplayersperteam/2;
        MinLvl = bl->minlvl;
        MaxLvl = bl->maxlvl;

        if (fields[1].GetUInt32())
            MinPlayersPerTeam = fields[1].GetUInt32();

        if (fields[2].GetUInt32())
            MaxPlayersPerTeam = fields[2].GetUInt32();

        if (fields[3].GetUInt32())
            MinLvl = fields[3].GetUInt32();

        if (fields[4].GetUInt32())
            MaxLvl = fields[4].GetUInt32();

        start1 = fields[5].GetUInt32();

        start = sWorldSafeLocsStore.LookupEntry(start1);
        if (start)
        {
            AStartLoc[0] = start->x;
            AStartLoc[1] = start->y;
            AStartLoc[2] = start->z;
            AStartLoc[3] = fields[6].GetFloat();
        }
        else if (bgTypeID == BATTLEGROUND_AA)
        {
            AStartLoc[0] = 0;
            AStartLoc[1] = 0;
            AStartLoc[2] = 0;
            AStartLoc[3] = fields[6].GetFloat();
        }
        else
        {
            sLog->outErrorDb("Table battleground_template for id %u has invalid WorldSafeLocs.dbc id %u in field AllianceStartLoc. BG not created.", bgTypeID, start1);
            continue;
        }

        start2 = fields[7].GetUInt32();

        start = sWorldSafeLocsStore.LookupEntry(start2);
        if (start)
        {
            HStartLoc[0] = start->x;
            HStartLoc[1] = start->y;
            HStartLoc[2] = start->z;
            HStartLoc[3] = fields[8].GetFloat();
        }
        else if (bgTypeID == BATTLEGROUND_AA)
        {
            HStartLoc[0] = 0;
            HStartLoc[1] = 0;
            HStartLoc[2] = 0;
            HStartLoc[3] = fields[8].GetFloat();
        }
        else
        {
            sLog->outErrorDb("Table battleground_template for id %u has invalid WorldSafeLocs.dbc id %u in field HordeStartLoc. BG not created.", bgTypeID, start2);
            continue;
        }

        //sLog->outDetail("Creating battleground %s, %u-%u", bl->name[sWorld->GetDBClang()], MinLvl, MaxLvl);
        if (!CreateBattleground(bgTypeID, MinPlayersPerTeam, MaxPlayersPerTeam, MinLvl, MaxLvl, bl->name[sWorld->GetDefaultDbcLocale()], bl->mapid[0], AStartLoc[0], AStartLoc[1], AStartLoc[2], AStartLoc[3], HStartLoc[0], HStartLoc[1], HStartLoc[2], HStartLoc[3]))
            continue;

        ++count;
    } while (result->NextRow());

    sLog->outString();
    sLog->outString(">> Loaded %u battlegrounds", count);
}

void BattlegroundMgr::InitAutomaticArenaPointDistribution()
{
    if (m_AutoDistributePoints)
    {
        sLog->outDebug("Initializing Automatic Arena Point Distribution");
        QueryResult_AutoPtr result = CharacterDatabase.Query("SELECT NextArenaPointDistributionTime FROM saved_variables");
        if (!result)
        {
            sLog->outDebug("Battleground: Next arena point distribution time not found in SavedVariables, reseting it now.");
            m_NextAutoDistributionTime = time(NULL) + BATTLEGROUND_ARENA_POINT_DISTRIBUTION_DAY * sWorld->getConfig(CONFIG_ARENA_AUTO_DISTRIBUTE_INTERVAL_DAYS);
            CharacterDatabase.PExecute("INSERT INTO saved_variables (NextArenaPointDistributionTime) VALUES ('"UI64FMTD"')", m_NextAutoDistributionTime);
        }
        else
            m_NextAutoDistributionTime = (*result)[0].GetUInt64();

        sLog->outDebug("Automatic Arena Point Distribution initialized.");
    }
}

void BattlegroundMgr::DistributeArenaPoints()
{
    // used to distribute arena points based on last week's stats
    sWorld->SendGlobalText("Flushing Arena points based on team ratings, this may take a few minutes. Please stand by...", NULL);

    sWorld->SendGlobalText("Distributing arena points to players...", NULL);

    //temporary structure for storing maximum points to add values for all players
    std::map<uint32, uint32> PlayerPoints;

    //at first update all points for all team members
    for (ObjectMgr::ArenaTeamMap::iterator team_itr = sObjectMgr->GetArenaTeamMapBegin(); team_itr != sObjectMgr->GetArenaTeamMapEnd(); ++team_itr)
    {
        if (ArenaTeam * at = team_itr->second)
        {
            at->UpdateArenaPointsHelper(PlayerPoints);
        }
    }

    //cycle that gives points to all players
    for (std::map<uint32, uint32>::iterator plr_itr = PlayerPoints.begin(); plr_itr != PlayerPoints.end(); ++plr_itr)
    {
        //update to database
        CharacterDatabase.PExecute("UPDATE characters SET arenaPoints = arenaPoints + '%u' WHERE guid = '%u'", plr_itr->second, plr_itr->first);
        //add points if player is online
        Player* pl = sObjectMgr->GetPlayer(plr_itr->first);
        if (pl)
            pl->ModifyArenaPoints(plr_itr->second);
    }

    PlayerPoints.clear();

    sWorld->SendGlobalText("Finished setting arena points for online players.", NULL);

    sWorld->SendGlobalText("Modifying played count, arena points etc. for loaded arena teams, sending updated stats to online players...", NULL);
    for (ObjectMgr::ArenaTeamMap::iterator titr = sObjectMgr->GetArenaTeamMapBegin(); titr != sObjectMgr->GetArenaTeamMapEnd(); ++titr)
    {
        if (ArenaTeam * at = titr->second)
        {
            at->FinishWeek();                              // set played this week etc values to 0 in memory, too
            at->SaveToDB();                                // save changes
            at->NotifyStatsChanged();                      // notify the players of the changes
        }
    }

    sWorld->SendGlobalText("Modification done.", NULL);

    sWorld->SendGlobalText("Done flushing Arena points.", NULL);
}

void BattlegroundMgr::BuildBattlegroundListPacket(WorldPacket *data, uint64 guid, Player* plr, uint32 bgTypeId)
{
    uint32 PlayerLevel = 10;

    if (plr)
        PlayerLevel = plr->getLevel();

    data->Initialize(SMSG_BATTLEFIELD_LIST);
    *data << uint64(guid);                                  // battlemaster guid
    *data << uint32(bgTypeId);                              // battleground id
    if (bgTypeId == BATTLEGROUND_AA)                         // arena
    {
        *data << uint8(5);                                  // unk
        *data << uint32(0);                                 // unk
    }
    else                                                    // battleground
    {
        *data << uint8(0x00);                               // unk

        size_t count_pos = data->wpos();
        uint32 count = 0;
        *data << uint32(0x00);                              // number of bg instances

        for (std::map<uint32, Battleground*>::iterator itr = m_Battlegrounds.begin(); itr != m_Battlegrounds.end(); ++itr)
        {
            if (itr->second->GetTypeID() == bgTypeId && (PlayerLevel >= itr->second->GetMinLevel()) && (PlayerLevel <= itr->second->GetMaxLevel()))
            {
                *data << uint32(itr->second->GetInstanceID());
                ++count;
            }
        }
        data->put<uint32>(count_pos , count);
    }
}

void BattlegroundMgr::SendToBattleground(Player *pl, uint32 instanceId)
{
    Battleground *bg = GetBattleground(instanceId);
    if (bg)
    {
        uint32 mapid = bg->GetMapId();
        float x, y, z, O;
        uint32 team = pl->GetBGTeam();
        if (team == 0)
            team = pl->GetTeam();
        bg->GetTeamStartLoc(team, x, y, z, O);

        sLog->outDetail("BATTLEGROUND: Sending %s to map %u, X %f, Y %f, Z %f, O %f", pl->GetName(), mapid, x, y, z, O);
        pl->TeleportTo(mapid, x, y, z, O);
    }
    else
    {
        sLog->outError("player %u trying to port to non-existent bg instance %u", pl->GetGUIDLow(), instanceId);
    }
}

void BattlegroundMgr::SendAreaSpiritHealerQueryOpcode(Player *pl, Battleground *bg, uint64 guid)
{
    WorldPacket data(SMSG_AREA_SPIRIT_HEALER_TIME, 12);
    uint32 time_ = 30000 - bg->GetLastResurrectTime();      // resurrect every 30 seconds
    if (time_ == uint32(-1))
        time_ = 0;
    data << guid << time_;
    pl->GetSession()->SendPacket(&data);
}

void BattlegroundMgr::RemoveBattleground(uint32 instanceID)
{
    BattlegroundSet::iterator itr = m_Battlegrounds.find(instanceID);
    if (itr != m_Battlegrounds.end())
        m_Battlegrounds.erase(itr);
}

bool BattlegroundMgr::IsArenaType(uint32 bgTypeId) const
{
    return (bgTypeId == BATTLEGROUND_AA ||
        bgTypeId == BATTLEGROUND_BE ||
        bgTypeId == BATTLEGROUND_NA ||
        bgTypeId == BATTLEGROUND_RL);
}

bool BattlegroundMgr::IsBattlegroundType(uint32 bgTypeId) const
{
    return !IsArenaType(bgTypeId);
}

uint32 BattlegroundMgr::BGQueueTypeId(uint32 bgTypeId, uint8 arenaType)
{
    switch (bgTypeId)
    {
        case BATTLEGROUND_WS:
            return BATTLEGROUND_QUEUE_WS;
        case BATTLEGROUND_AB:
            return BATTLEGROUND_QUEUE_AB;
        case BATTLEGROUND_AV:
            return BATTLEGROUND_QUEUE_AV;
        case BATTLEGROUND_EY:
            return BATTLEGROUND_QUEUE_EY;
        case BATTLEGROUND_AA:
        case BATTLEGROUND_NA:
        case BATTLEGROUND_RL:
        case BATTLEGROUND_BE:
            switch (arenaType)
            {
                case ARENA_TYPE_2v2:
                    return BATTLEGROUND_QUEUE_2v2;
                case ARENA_TYPE_3v3:
                    return BATTLEGROUND_QUEUE_3v3;
                case ARENA_TYPE_5v5:
                    return BATTLEGROUND_QUEUE_5v5;
                default:
                    return 0;
            }
        default:
            return 0;
    }
}

uint32 BattlegroundMgr::BGTemplateId(uint32 bgQueueTypeId) const
{
    switch (bgQueueTypeId)
    {
        case BATTLEGROUND_QUEUE_WS:
            return BATTLEGROUND_WS;
        case BATTLEGROUND_QUEUE_AB:
            return BATTLEGROUND_AB;
        case BATTLEGROUND_QUEUE_AV:
            return BATTLEGROUND_AV;
        case BATTLEGROUND_QUEUE_EY:
            return BATTLEGROUND_EY;
        case BATTLEGROUND_QUEUE_2v2:
        case BATTLEGROUND_QUEUE_3v3:
        case BATTLEGROUND_QUEUE_5v5:
            return BATTLEGROUND_AA;
        default:
            return 0;
    }
}

uint8 BattlegroundMgr::BGArenaType(uint32 bgQueueTypeId) const
{
    switch (bgQueueTypeId)
    {
        case BATTLEGROUND_QUEUE_2v2:
            return ARENA_TYPE_2v2;
        case BATTLEGROUND_QUEUE_3v3:
            return ARENA_TYPE_3v3;
        case BATTLEGROUND_QUEUE_5v5:
            return ARENA_TYPE_5v5;
        default:
            return 0;
    }
}

void BattlegroundMgr::ToggleTesting()
{
    m_Testing = !m_Testing;
    if (m_Testing)
        sWorld->SendGlobalText("Battlegrounds are set to 1v0 for debugging.", NULL);
    else
        sWorld->SendGlobalText("Battlegrounds are set to normal playercount.", NULL);
}

void BattlegroundMgr::ToggleArenaTesting()
{
    m_ArenaTesting = !m_ArenaTesting;
    if (m_ArenaTesting)
        sWorld->SendGlobalText("Arenas are set to 1v1 for debugging. So, don't join as group.", NULL);
    else
        sWorld->SendGlobalText("Arenas are set to normal playercount.", NULL);
}

void BattlegroundMgr::SetHolidayWeekends(uint32 mask)
{
    for (uint32 bgtype = 1; bgtype <= 8; ++bgtype)
    {
        if (Battleground * bg = GetBattlegroundTemplate(bgtype))
        {
            bg->SetHoliday(mask & (1 << bgtype));
        }
    }
}

