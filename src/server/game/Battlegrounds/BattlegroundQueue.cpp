/*
 * Copyright (C) 2010-2012 Project SkyFire <http://www.projectskyfire.org/>
 * Copyright (C) 2008-2012 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2005-2012 MaNGOS <http://getmangos.com/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
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

#include "BattlegroundQueue.h"
#include "ArenaTeam.h"
#include "BattlegroundMgr.h"
#include "Chat.h"
#include "ObjectMgr.h"
#include "Log.h"
#include "Group.h"

/*********************************************************/
/***            BATTLEGROUND QUEUE SYSTEM              ***/
/*********************************************************/

BattlegroundQueue::BattlegroundQueue()
{
    //queues are empty, we don't have to call clear()
/*    for (int i = 0; i < MAX_BATTLEGROUND_QUEUES; i++)
    {
        //m_QueuedPlayers[i].Horde = 0;
        //m_QueuedPlayers[i].Alliance = 0;
        //m_QueuedPlayers[i].AverageTime = 0;
    }*/
}

BattlegroundQueue::~BattlegroundQueue()
{
    for (int i = 0; i < MAX_BATTLEGROUND_QUEUES; i++)
    {
        m_QueuedPlayers[i].clear();
        for (QueuedGroupsList::iterator itr = m_QueuedGroups[i].begin(); itr != m_QueuedGroups[i].end(); ++itr)
        {
            delete (*itr);
        }
        m_QueuedGroups[i].clear();
    }
}

// initialize eligible groups from the given source matching the given specifications
void BattlegroundQueue::EligibleGroups::Init(BattlegroundQueue::QueuedGroupsList *source, uint32 BgTypeId, uint32 side, uint32 MaxPlayers, uint8 ArenaType, bool IsRated, uint32 MinRating, uint32 MaxRating, uint32 DisregardTime, uint32 excludeTeam)
{
    // clear from prev initialization
    clear();
    BattlegroundQueue::QueuedGroupsList::iterator itr, next;
    // iterate through the source
    for (itr = source->begin(); itr != source->end(); itr = next)
    {
        next = itr;
        ++next;
        if ((*itr)->BgTypeId == BgTypeId &&     // bg type must match
            (*itr)->ArenaType == ArenaType &&   // arena type must match
            (*itr)->IsRated == IsRated &&       // israted must match
            (*itr)->IsInvitedToBGInstanceGUID == 0 && // leave out already invited groups
            (*itr)->Team == side &&             // match side
            (*itr)->Players.size() <= MaxPlayers &&   // the group must fit in the bg
            (!excludeTeam || (*itr)->ArenaTeamId != excludeTeam) && // if excludeTeam is specified, leave out those arena team ids
            (!IsRated || (*itr)->Players.size() == MaxPlayers) &&   // if rated, then pass only if the player count is exact NEEDS TESTING! (but now this should never happen)
            (!DisregardTime || (*itr)->JoinTime <= DisregardTime              // pass if disregard time is greater than join time
               || (*itr)->ArenaTeamRating == 0                 // pass if no rating info
               || ((*itr)->ArenaTeamRating >= MinRating       // pass if matches the rating range
                     && (*itr)->ArenaTeamRating <= MaxRating)))
        {
            // the group matches the conditions
            // using push_back for proper selecting when inviting
            push_back((*itr));
        }
    }
}

// selection pool initialization, used to clean up from prev selection
void BattlegroundQueue::SelectionPool::Init(EligibleGroups * curr)
{
    m_CurrEligGroups = curr;
    SelectedGroups.clear();
    PlayerCount = 0;
}

// remove group info from selection pool
void BattlegroundQueue::SelectionPool::RemoveGroup(GroupQueueInfo *ginfo)
{
    // find what to remove
    for (std::list<GroupQueueInfo *>::iterator itr = SelectedGroups.begin(); itr != SelectedGroups.end(); ++itr)
    {
        if ((*itr) == ginfo)
        {
            SelectedGroups.erase(itr);
            // decrease selected players count
            PlayerCount -= ginfo->Players.size();
            return;
        }
    }
}

// add group to selection
// used when building selection pools
void BattlegroundQueue::SelectionPool::AddGroup(GroupQueueInfo * ginfo)
{
    SelectedGroups.push_back(ginfo);
    // increase selected players count
    PlayerCount+=ginfo->Players.size();
}

// add group to bg queue with the given leader and bg specifications
GroupQueueInfo * BattlegroundQueue::AddGroup(Player *leader, uint32 BgTypeId, uint8 ArenaType, bool isRated, uint32 arenaRating, uint32 arenateamid)
{
    uint32 queue_id = leader->GetBattlegroundQueueIdFromLevel();

    // create new ginfo
    // cannot use the method like in addplayer, because that could modify an in-queue group's stats
    // (e.g. leader leaving queue then joining as individual again)
    GroupQueueInfo* ginfo = new GroupQueueInfo;
    ginfo->BgTypeId                  = BgTypeId;
    ginfo->ArenaType                 = ArenaType;
    ginfo->ArenaTeamId               = arenateamid;
    ginfo->IsRated                   = isRated;
    ginfo->IsInvitedToBGInstanceGUID = 0;                       // maybe this should be modifiable by function arguments to enable selection of running instances?
    ginfo->JoinTime                  = getMSTime();
    ginfo->Team                      = leader->GetTeam();
    ginfo->ArenaTeamRating           = arenaRating;
    ginfo->OpponentsTeamRating       = 0;                       //initialize it to 0

    ginfo->Players.clear();

    m_QueuedGroups[queue_id].push_back(ginfo);

    // return ginfo, because it is needed to add players to this group info
    return ginfo;
}

void BattlegroundQueue::AddPlayer(Player *plr, GroupQueueInfo *ginfo)
{
    uint32 queue_id = plr->GetBattlegroundQueueIdFromLevel();

    //if player isn't in queue, he is added, if already is, then values are overwritten, no memory leak
    PlayerQueueInfo& info = m_QueuedPlayers[queue_id][plr->GetGUID()];
    info.InviteTime                 = 0;
    info.LastInviteTime             = 0;
    info.LastOnlineTime             = getMSTime();
    info.GroupInfo                  = ginfo;

    // add the pinfo to ginfo's list
    ginfo->Players[plr->GetGUID()]  = &info;

    if (sWorld->getConfig(CONFIG_BATTLEGROUND_QUEUE_ANNOUNCER_ENABLE))
    {
        //announce only once in a time
        if (!sWorld->getConfig(CONFIG_BATTLEGROUND_QUEUE_ANNOUNCER_PLAYERONLY) && m_QueuedPlayers[queue_id].size() % 5 != 0) return;
        Battleground * bg = sBattlegroundMgr->GetBattlegroundTemplate(ginfo->BgTypeId);
        if (!bg) return;

        char const* bgName = bg->GetName();

        uint32 q_min_level = Player::GetMinLevelForBattlegroundQueueId(queue_id);
        uint32 q_max_level = Player::GetMaxLevelForBattlegroundQueueId(queue_id);

        // replace hardcoded max level by player max level for nice output
        if (q_max_level > sWorld->getConfig(CONFIG_MAX_PLAYER_LEVEL))
            q_max_level = sWorld->getConfig(CONFIG_MAX_PLAYER_LEVEL);

        int32 MinPlayers = bg->GetMinPlayersPerTeam();
        int32 MaxPlayers = bg->GetMaxPlayersPerTeam();

        uint32 qHorde = 0;
        uint32 qAlliance = 0;

        for (std::map<uint64, PlayerQueueInfo>::iterator itr = m_QueuedPlayers[queue_id].begin(); itr != m_QueuedPlayers[queue_id].end(); ++itr)
        {
            Player *_player = sObjectMgr->GetPlayer((uint64)itr->first);
            if (_player)
            {
                if (_player->GetTeam() == ALLIANCE)
                    qAlliance++;
                else
                    qHorde++;
            }
        }

        // Show queue status to player only (when joining queue)
        if (sWorld->getConfig(CONFIG_BATTLEGROUND_QUEUE_ANNOUNCER_PLAYERONLY))
        {
            uint32 needAlliance = (MinPlayers < qAlliance) ? 0 : MinPlayers - qAlliance;
            uint32 needHorde = (MinPlayers < qHorde) ? 0 : MinPlayers - qHorde;
            ChatHandler(plr).PSendSysMessage(LANG_BG_QUEUE_ANNOUNCE_SELF,
                bgName, q_min_level, q_max_level, qAlliance, needAlliance, qHorde, needHorde);
        }
        // System message
        else
        {
            sWorld->SendWorldText(LANG_BG_QUEUE_ANNOUNCE_WORLD,
                bgName, q_min_level, q_max_level, qAlliance, MaxPlayers, qHorde, MaxPlayers);
        }
    }
}

void BattlegroundQueue::RemovePlayer(uint64 guid, bool decreaseInvitedCount)
{
    Player *plr = sObjectMgr->GetPlayer(guid);

    int32 queue_id = 0;                                     // signed for proper for-loop finish
    QueuedPlayersMap::iterator itr;
    GroupQueueInfo * group;
    QueuedGroupsList::iterator group_itr;
    bool IsSet = false;
    if (plr)
    {
        queue_id = plr->GetBattlegroundQueueIdFromLevel();

        itr = m_QueuedPlayers[queue_id].find(guid);
        if (itr != m_QueuedPlayers[queue_id].end())
            IsSet = true;
    }

    if (!IsSet)
    {
        // either player is offline, or he levelled up to another queue category
        // sLog->outError("Battleground: removing offline player from BG queue - this might not happen, but it should not cause crash");
        for (uint32 i = 0; i < MAX_BATTLEGROUND_QUEUES; i++)
        {
            itr = m_QueuedPlayers[i].find(guid);
            if (itr != m_QueuedPlayers[i].end())
            {
                queue_id = i;
                IsSet = true;
                break;
            }
        }
    }

    // couldn't find the player in bg queue, return
    if (!IsSet)
    {
        sLog->outError("Battleground: couldn't find player to remove.");
        return;
    }

    group = itr->second.GroupInfo;

    for (group_itr=m_QueuedGroups[queue_id].begin(); group_itr != m_QueuedGroups[queue_id].end(); ++group_itr)
    {
        if (group == (GroupQueueInfo*)(*group_itr))
            break;
    }

    // variables are set (what about leveling up when in queue????)
    // remove player from group
    // if only player there, remove group

    // remove player queue info from group queue info
    std::map<uint64, PlayerQueueInfo*>::iterator pitr = group->Players.find(guid);

    if (pitr != group->Players.end())
        group->Players.erase(pitr);

    // check for iterator correctness
    if (group_itr != m_QueuedGroups[queue_id].end() && itr != m_QueuedPlayers[queue_id].end())
    {
        // used when player left the queue, NOT used when porting to bg
        if (decreaseInvitedCount)
        {
            // if invited to bg, and should decrease invited count, then do it
            if (group->IsInvitedToBGInstanceGUID)
            {
                Battleground* bg = sBattlegroundMgr->GetBattleground(group->IsInvitedToBGInstanceGUID);
                if (bg)
                    bg->DecreaseInvitedCount(group->Team);
                if (bg && !bg->GetPlayersSize() && !bg->GetInvitedCount(ALLIANCE) && !bg->GetInvitedCount(HORDE))
                {
                    // no more players on battleground, set delete it
                    bg->SetDeleteThis();
                }
            }
            // update the join queue, maybe now the player's group fits in a queue!
            // not yet implemented (should store bgTypeId in group queue info?)
        }
        //if player leaves queue and he is invited to rated arena match, then he has to loose
        if (group->IsInvitedToBGInstanceGUID && group->IsRated && decreaseInvitedCount)
        {
            ArenaTeam * at = sObjectMgr->GetArenaTeamById(group->ArenaTeamId);
            if (at)
            {
                sLog->outDebug("UPDATING memberLost's personal arena rating for %u by opponents rating: %u", GUID_LOPART(guid), group->OpponentsTeamRating);
                Player *plr = sObjectMgr->GetPlayer(guid);
                if (plr)
                    at->MemberLost(plr, group->OpponentsTeamRating);
                else
                    at->OfflineMemberLost(guid, group->OpponentsTeamRating);
                at->SaveToDB();
            }
        }
        // remove player queue info
        m_QueuedPlayers[queue_id].erase(itr);
        // remove group queue info if needed
        if (group->Players.empty())
        {
            m_QueuedGroups[queue_id].erase(group_itr);
            delete group;
        }
        // NEEDS TESTING!
        // group wasn't empty, so it wasn't deleted, and player have left a rated queue -> everyone from the group should leave too
        // don't remove recursively if already invited to bg!
        else if (!group->IsInvitedToBGInstanceGUID && decreaseInvitedCount && group->IsRated)
        {
            // remove next player, this is recursive
            // first send removal information
            if (Player *plr2 = sObjectMgr->GetPlayer(group->Players.begin()->first))
            {
                Battleground * bg = sBattlegroundMgr->GetBattlegroundTemplate(group->BgTypeId);
                uint32 bgQueueTypeId = sBattlegroundMgr->BGQueueTypeId(group->BgTypeId, group->ArenaType);
                uint32 queueSlot = plr2->GetBattlegroundQueueIndex(bgQueueTypeId);
                plr2->RemoveBattlegroundQueueId(bgQueueTypeId); // must be called this way, because if you move this call to queue->removeplayer, it causes bugs
                WorldPacket data;
                sBattlegroundMgr->BuildBattlegroundStatusPacket(&data, bg, plr2->GetTeam(), queueSlot, STATUS_NONE, 0, 0);
                plr2->GetSession()->SendPacket(&data);
            }
            // then actually delete, this may delete the group as well!
            RemovePlayer(group->Players.begin()->first, decreaseInvitedCount);
        }
    }
}

bool BattlegroundQueue::InviteGroupToBG(GroupQueueInfo * ginfo, Battleground * bg, uint32 side)
{
    // set side if needed
    if (side)
        ginfo->Team = side;

    if (!ginfo->IsInvitedToBGInstanceGUID)
    {
        // not yet invited
        // set invitation
        ginfo->IsInvitedToBGInstanceGUID = bg->GetInstanceID();
        uint32 bgQueueTypeId = sBattlegroundMgr->BGQueueTypeId(bg->GetTypeID(), bg->GetArenaType());
        // loop through the players
        for (std::map<uint64, PlayerQueueInfo*>::iterator itr = ginfo->Players.begin(); itr != ginfo->Players.end(); ++itr)
        {
            // set status
            itr->second->InviteTime = getMSTime();
            itr->second->LastInviteTime = getMSTime();

            // get the player
            Player* plr = sObjectMgr->GetPlayer(itr->first);
            // if offline, skip him
            if (!plr)
                continue;

            // invite the player
            sBattlegroundMgr->InvitePlayer(plr, bg->GetInstanceID(), ginfo->Team);

            WorldPacket data;

            uint32 queueSlot = plr->GetBattlegroundQueueIndex(bgQueueTypeId);

            sLog->outDebug("Battleground: invited plr %s (%u) to BG instance %u queueindex %u bgtype %u, I can't help it if they don't press the enter battle button.", plr->GetName(), plr->GetGUIDLow(), bg->GetInstanceID(), queueSlot, bg->GetTypeID());

            // send status packet
            sBattlegroundMgr->BuildBattlegroundStatusPacket(&data, bg, side?side:plr->GetTeam(), queueSlot, STATUS_WAIT_JOIN, INVITE_ACCEPT_WAIT_TIME, 0);
            plr->GetSession()->SendPacket(&data);
        }
        return true;
    }

    return false;
}

// used to recursively select groups from eligible groups
bool BattlegroundQueue::SelectionPool::Build(uint32 MinPlayers, uint32 MaxPlayers, EligibleGroups::iterator startitr)
{
    // start from the specified start iterator
    for (EligibleGroups::iterator itr1 = startitr; itr1 != m_CurrEligGroups->end(); ++itr1)
    {
        // if it fits in, select it
        if (GetPlayerCount() + (*itr1)->Players.size() <= MaxPlayers)
        {
            EligibleGroups::iterator next = itr1;
            ++next;
            AddGroup((*itr1));
            if (GetPlayerCount() >= MinPlayers)
            {
                // enough players are selected
                return true;
            }
            // try building from the rest of the elig. groups
            // if that succeeds, return true
            if (Build(MinPlayers, MaxPlayers, next))
                return true;
            // the rest didn't succeed, so this group cannot be included
            RemoveGroup((*itr1));
        }
    }
    // build didn't succeed
    return false;
}

// this function is responsible for the selection of queued groups when trying to create new battlegrounds
bool BattlegroundQueue::BuildSelectionPool(uint32 bgTypeId, uint32 queue_id, uint32 MinPlayers, uint32 MaxPlayers,  SelectionPoolBuildMode mode, uint8 ArenaType, bool isRated, uint32 MinRating, uint32 MaxRating, uint32 DisregardTime, uint32 excludeTeam)
{
    uint32 side;
    switch (mode)
    {
        case NORMAL_ALLIANCE:
        case ONESIDE_ALLIANCE_TEAM1:
        case ONESIDE_ALLIANCE_TEAM2:
            side = ALLIANCE;
            break;
        case NORMAL_HORDE:
        case ONESIDE_HORDE_TEAM1:
        case ONESIDE_HORDE_TEAM2:
            side = HORDE;
            break;
        default:
            //unknown mode, return false
            sLog->outDebug("Battleground: unknown selection pool build mode, returning...");
            return false;
            break;
    }

    // initiate the groups eligible to create the bg
    m_EligibleGroups.Init(&(m_QueuedGroups[queue_id]), bgTypeId, side, MaxPlayers, ArenaType, isRated, MinRating, MaxRating, DisregardTime, excludeTeam);
    // init the selected groups (clear)
    // and set m_CurrEligGroups pointer
    // we set it this way to only have one EligibleGroups object to save some memory
    m_SelectionPools[mode].Init(&m_EligibleGroups);
    // build succeeded
    if (m_SelectionPools[mode].Build(MinPlayers, MaxPlayers, m_EligibleGroups.begin()))
    {
        // the selection pool is set, return
        sLog->outDebug("Battleground-debug: pool build succeeded, return true");
        sLog->outDebug("Battleground-debug: Player size for mode %u is %u", mode, m_SelectionPools[mode].GetPlayerCount());
        for (std::list<GroupQueueInfo* >::iterator itr = m_SelectionPools[mode].SelectedGroups.begin(); itr != m_SelectionPools[mode].SelectedGroups.end(); ++itr)
        {
            sLog->outDebug("Battleground-debug: queued group in selection with %u players", (*itr)->Players.size());
            for (std::map<uint64, PlayerQueueInfo * >::iterator itr2 = (*itr)->Players.begin(); itr2 != (*itr)->Players.end(); ++itr2)
                sLog->outDebug("Battleground-debug:    player in above group GUID %u", (uint32)(itr2->first));
        }
        return true;
    }
    // failed to build a selection pool matching the given values
    return false;
}

// used to remove the Enter Battle window if the battle has already, but someone still has it
// (this can happen in arenas mainly, since the preparation is shorter than the timer for the bgqueueremove event
void BattlegroundQueue::BGEndedRemoveInvites(Battleground *bg)
{
    uint32 queue_id = bg->GetQueueType();
    uint32 bgInstanceId = bg->GetInstanceID();
    uint32 bgQueueTypeId = sBattlegroundMgr->BGQueueTypeId(bg->GetTypeID(), bg->GetArenaType());
    QueuedGroupsList::iterator itr, next;
    for (itr = m_QueuedGroups[queue_id].begin(); itr != m_QueuedGroups[queue_id].end(); itr = next)
    {
        // must do this way, because the groupinfo will be deleted when all playerinfos are removed
        GroupQueueInfo * ginfo = (*itr);
        next = itr;
        ++next;
        // if group was invited to this bg instance, then remove all references
        if (ginfo->IsInvitedToBGInstanceGUID == bgInstanceId)
        {
            // after removing this much playerinfos, the ginfo will be deleted, so we'll use a for loop
            uint32 to_remove = ginfo->Players.size();
            uint32 team = ginfo->Team;
            for (uint32 i = 0; i < to_remove; ++i)
            {
                // always remove the first one in the group
                std::map<uint64, PlayerQueueInfo * >::iterator itr2 = ginfo->Players.begin();
                if (itr2 == ginfo->Players.end())
                {
                    sLog->outError("Empty Players in ginfo, this should never happen!");
                    return;
                }

                // get the player
                Player * plr = sObjectMgr->GetPlayer(itr2->first);
                if (!plr)
                {
                    sLog->outError("Player offline when trying to remove from GroupQueueInfo, this should never happen.");
                    continue;
                }

                // get the queueslot
                uint32 queueSlot = plr->GetBattlegroundQueueIndex(bgQueueTypeId);
                if (queueSlot < PLAYER_MAX_BATTLEGROUND_QUEUES) // player is in queue
                {
                    plr->RemoveBattlegroundQueueId(bgQueueTypeId);
                    // remove player from queue, this might delete the ginfo as well! don't use that pointer after this!
                    RemovePlayer(itr2->first, true);
                    // this is probably unneeded, since this player was already invited -> does not fit when initing eligible groups
                    // but updating the queue can't hurt
                    Update(bgQueueTypeId, bg->GetQueueType());
                    // send info to client
                    WorldPacket data;
                    sBattlegroundMgr->BuildBattlegroundStatusPacket(&data, bg, team, queueSlot, STATUS_NONE, 0, 0);
                    plr->GetSession()->SendPacket(&data);
                }
            }
        }
    }
}

/*
this method is called when group is inserted, or player / group is removed from BG Queue - there is only one player's status changed, so we don't use while (true) cycles to invite whole queue
it must be called after fully adding the members of a group to ensure group joining
should be called after removeplayer functions in some cases
*/
void BattlegroundQueue::Update(uint32 bgTypeId, uint32 queue_id, uint8 arenatype, bool isRated, uint32 arenaRating)
{
    if (queue_id >= MAX_BATTLEGROUND_QUEUES)
    {
        //this is error, that caused crashes (not in , but now it shouldn't)
        sLog->outError("BattlegroundQueue::Update() called for invalid queue type - this can cause crash, pls report problem, if this is the last line of error log before crash");
        return;
    }

    //if no players in queue ... do nothing
    if (m_QueuedGroups[queue_id].empty())
        return;

    uint32 bgQueueTypeId = sBattlegroundMgr->BGQueueTypeId(bgTypeId, arenatype);

    //battleground with free slot for player should be always the last in this queue
    BGFreeSlotQueueType::iterator itr, next;
    for (itr = sBattlegroundMgr->BGFreeSlotQueue[bgTypeId].begin(); itr != sBattlegroundMgr->BGFreeSlotQueue[bgTypeId].end(); itr = next)
    {
        next = itr;
        ++next;
        // battleground is running, so if:
        // DO NOT allow queue manager to invite new player to running arena
        if ((*itr)->isBattleground() && (*itr)->GetTypeID() == bgTypeId && (*itr)->GetQueueType() == queue_id && (*itr)->GetStatus() > STATUS_WAIT_QUEUE && (*itr)->GetStatus() < STATUS_WAIT_LEAVE)
        {
            //we must check both teams
            Battleground* bg = *itr; //we have to store battleground pointer here, because when battleground is full, it is removed from free queue (not yet implemented!!)
            // and iterator is invalid

            for (QueuedGroupsList::iterator itr = m_QueuedGroups[queue_id].begin(); itr != m_QueuedGroups[queue_id].end(); ++itr)
            {
                // did the group join for this bg type?
                if ((*itr)->BgTypeId != bgTypeId)
                    continue;
                // if so, check if fits in
                if (bg->GetFreeSlotsForTeam((*itr)->Team) >= (*itr)->Players.size())
                {
                    // if group fits in, invite it
                    InviteGroupToBG((*itr), bg, (*itr)->Team);
                }
            }

            if (!bg->HasFreeSlots())
            {
                //remove BG from BGFreeSlotQueue
                bg->RemoveFromBGFreeSlotQueue();
            }
        }
    }

    // finished iterating through the bgs with free slots, maybe we need to create a new bg

    Battleground * bg_template = sBattlegroundMgr->GetBattlegroundTemplate(bgTypeId);
    if (!bg_template)
    {
        sLog->outError("Battleground: Update: bg template not found for %u", bgTypeId);
        return;
    }

    // get the min. players per team, properly for larger arenas as well. (must have full teams for arena matches!)
    uint32 MinPlayersPerTeam = bg_template->GetMinPlayersPerTeam();
    uint32 MaxPlayersPerTeam = bg_template->GetMaxPlayersPerTeam();
    if (bg_template->isArena())
    {
        if (sBattlegroundMgr->isArenaTesting())
        {
            MaxPlayersPerTeam = 1;
            MinPlayersPerTeam = 1;
        }
        else
        {
            switch (arenatype)
            {
            case ARENA_TYPE_2v2:
                MaxPlayersPerTeam = 2;
                MinPlayersPerTeam = 2;
                break;
            case ARENA_TYPE_3v3:
                MaxPlayersPerTeam = 3;
                MinPlayersPerTeam = 3;
                break;
            case ARENA_TYPE_5v5:
                MaxPlayersPerTeam = 5;
                MinPlayersPerTeam = 5;
                break;
            }
        }
    }
    else
    {
        if (sBattlegroundMgr->isTesting())
            MinPlayersPerTeam = 1;
    }

    // found out the minimum and maximum ratings the newly added team should battle against
    // arenaRating is the rating of the latest joined team
    uint32 arenaMinRating = (arenaRating <= sBattlegroundMgr->GetMaxRatingDifference()) ? 0 : arenaRating - sBattlegroundMgr->GetMaxRatingDifference();
    // if no rating is specified, set maxrating to 0
    uint32 arenaMaxRating = (arenaRating == 0)? 0 : arenaRating + sBattlegroundMgr->GetMaxRatingDifference();
    uint32 discardTime = 0;
    // if max rating difference is set and the time past since server startup is greater than the rating discard time
    // (after what time the ratings aren't taken into account when making teams) then
    // the discard time is current_time - time_to_discard, teams that joined after that, will have their ratings taken into account
    // else leave the discard time on 0, this way all ratings will be discarded
    if (sBattlegroundMgr->GetMaxRatingDifference() && getMSTime() >= sBattlegroundMgr->GetRatingDiscardTimer())
        discardTime = getMSTime() - sBattlegroundMgr->GetRatingDiscardTimer();

    // try to build the selection pools
    bool bAllyOK = BuildSelectionPool(bgTypeId, queue_id, MinPlayersPerTeam, MaxPlayersPerTeam, NORMAL_ALLIANCE, arenatype, isRated, arenaMinRating, arenaMaxRating, discardTime);
    if (bAllyOK)
        sLog->outDebug("Battleground: ally pool successfully built");
    else
        sLog->outDebug("Battleground: ally pool wasn't created");
    bool bHordeOK = BuildSelectionPool(bgTypeId, queue_id, MinPlayersPerTeam, MaxPlayersPerTeam, NORMAL_HORDE, arenatype, isRated, arenaMinRating, arenaMaxRating, discardTime);
    if (bHordeOK)
        sLog->outDebug("Battleground: horde pool successfully built");
    else
        sLog->outDebug("Battleground: horde pool wasn't created");

    // if selection pools are ready, create the new bg
    if ((bAllyOK && bHordeOK) || (sBattlegroundMgr->isTesting() && (bAllyOK || bHordeOK)))
    {
        Battleground * bg2 = 0;
        // special handling for arenas
        if (bg_template->isArena())
        {
            // Find a random arena, that can be created
            uint8 arenas[] = {BATTLEGROUND_NA, BATTLEGROUND_BE, BATTLEGROUND_RL};
            uint32 arena_num = urand(0, 2);
            if (!(bg2 = sBattlegroundMgr->CreateNewBattleground(arenas[arena_num%3], arenatype, isRated)) &&
                !(bg2 = sBattlegroundMgr->CreateNewBattleground(arenas[(arena_num+1)%3], arenatype, isRated)) &&
                !(bg2 = sBattlegroundMgr->CreateNewBattleground(arenas[(arena_num+2)%3], arenatype, isRated)))
            {
                sLog->outError("Battleground: couldn't create any arena instance!");
                return;
            }

            // set the MaxPlayersPerTeam values based on arenatype
            // setting the min player values isn't needed, since we won't be using that value later on.
            if (sBattlegroundMgr->isArenaTesting())
            {
                bg2->SetMaxPlayersPerTeam(1);
                bg2->SetMaxPlayers(2);
            }
            else
            {
                switch (arenatype)
                {
                case ARENA_TYPE_2v2:
                    bg2->SetMaxPlayersPerTeam(2);
                    bg2->SetMaxPlayers(4);
                    break;
                case ARENA_TYPE_3v3:
                    bg2->SetMaxPlayersPerTeam(3);
                    bg2->SetMaxPlayers(6);
                    break;
                case ARENA_TYPE_5v5:
                    bg2->SetMaxPlayersPerTeam(5);
                    bg2->SetMaxPlayers(10);
                    break;
                default:
                    break;
                }
            }
        }
        else
        {
            // create new battleground
            bg2 = sBattlegroundMgr->CreateNewBattleground(bgTypeId, arenatype, false);
        }

        if (!bg2)
        {
            sLog->outError("Battleground: couldn't create bg %u", bgTypeId);
            return;
        }

        bg2->SetQueueType(queue_id);

        std::list<GroupQueueInfo* >::iterator itr;

        // Send amount of invites based on the difference between the sizes of the two faction's queues
        uint32 QUEUED_HORDE = m_SelectionPools[NORMAL_HORDE].SelectedGroups.size();
        uint32 QUEUED_ALLIANCE = m_SelectionPools[NORMAL_ALLIANCE].SelectedGroups.size();
        uint16 maxbginvites = 0;

        if (QUEUED_ALLIANCE <= QUEUED_HORDE)
            maxbginvites = QUEUED_ALLIANCE;
        else
            maxbginvites = QUEUED_HORDE;

        // invite groups from horde selection pool
        uint16 invitecounter = 0;
        for (itr = m_SelectionPools[NORMAL_HORDE].SelectedGroups.begin(); itr != m_SelectionPools[NORMAL_HORDE].SelectedGroups.end(); ++itr)
        {
            if (invitecounter >= maxbginvites)
                return;
            InviteGroupToBG((*itr), bg2, HORDE);
            ++invitecounter;
        }

        // invite groups from ally selection pool
        invitecounter = 0;
        for (itr = m_SelectionPools[NORMAL_ALLIANCE].SelectedGroups.begin(); itr != m_SelectionPools[NORMAL_ALLIANCE].SelectedGroups.end(); ++itr)
        {
            if (invitecounter >= maxbginvites)
                return;
            InviteGroupToBG((*itr), bg2, ALLIANCE);
            ++invitecounter;
        }

        if (isRated)
        {
            std::list<GroupQueueInfo* >::iterator itr_alliance = m_SelectionPools[NORMAL_ALLIANCE].SelectedGroups.begin();
            std::list<GroupQueueInfo* >::iterator itr_horde = m_SelectionPools[NORMAL_HORDE].SelectedGroups.begin();
            (*itr_alliance)->OpponentsTeamRating = (*itr_horde)->ArenaTeamRating;
            sLog->outDebug("setting opposite team rating for team %u to %u", (*itr_alliance)->ArenaTeamId, (*itr_alliance)->OpponentsTeamRating);
            (*itr_horde)->OpponentsTeamRating = (*itr_alliance)->ArenaTeamRating;
            sLog->outDebug("setting opposite team rating for team %u to %u", (*itr_horde)->ArenaTeamId, (*itr_horde)->OpponentsTeamRating);
        }

        // start the battleground
        bg2->StartBattleground();
    }

    // there weren't enough players for a "normal" match
    // if arena, enable horde versus horde or alliance versus alliance teams here

    else if (bg_template->isArena())
    {
        bool bOneSideHordeTeam1 = false, bOneSideHordeTeam2 = false;
        bool bOneSideAllyTeam1 = false, bOneSideAllyTeam2 = false;
        bOneSideHordeTeam1 = BuildSelectionPool(bgTypeId, queue_id, MaxPlayersPerTeam, MaxPlayersPerTeam, ONESIDE_HORDE_TEAM1, arenatype, isRated, arenaMinRating, arenaMaxRating, discardTime);
        if (bOneSideHordeTeam1)
        {
            // one team has been selected, find out if other can be selected too
            std::list<GroupQueueInfo* >::iterator itr;
            // temporarily change the team side to enable building the next pool excluding the already selected groups
            for (itr = m_SelectionPools[ONESIDE_HORDE_TEAM1].SelectedGroups.begin(); itr != m_SelectionPools[ONESIDE_HORDE_TEAM1].SelectedGroups.end(); ++itr)
                (*itr)->Team=ALLIANCE;

            bOneSideHordeTeam2 = BuildSelectionPool(bgTypeId, queue_id, MaxPlayersPerTeam, MaxPlayersPerTeam, ONESIDE_HORDE_TEAM2, arenatype, isRated, arenaMinRating, arenaMaxRating, discardTime, (*(m_SelectionPools[ONESIDE_HORDE_TEAM1].SelectedGroups.begin()))->ArenaTeamId);

            // change back the team to horde
            for (itr = m_SelectionPools[ONESIDE_HORDE_TEAM1].SelectedGroups.begin(); itr != m_SelectionPools[ONESIDE_HORDE_TEAM1].SelectedGroups.end(); ++itr)
                (*itr)->Team=HORDE;

            if (!bOneSideHordeTeam2)
                bOneSideHordeTeam1 = false;
        }
        if (!bOneSideHordeTeam1)
        {
            // check for one sided ally
            bOneSideAllyTeam1 = BuildSelectionPool(bgTypeId, queue_id, MaxPlayersPerTeam, MaxPlayersPerTeam, ONESIDE_ALLIANCE_TEAM1, arenatype, isRated, arenaMinRating, arenaMaxRating, discardTime);
            if (bOneSideAllyTeam1)
            {
                // one team has been selected, find out if other can be selected too
                std::list<GroupQueueInfo* >::iterator itr;
                // temporarily change the team side to enable building the next pool excluding the already selected groups
                for (itr = m_SelectionPools[ONESIDE_ALLIANCE_TEAM1].SelectedGroups.begin(); itr != m_SelectionPools[ONESIDE_ALLIANCE_TEAM1].SelectedGroups.end(); ++itr)
                    (*itr)->Team=HORDE;

                bOneSideAllyTeam2 = BuildSelectionPool(bgTypeId, queue_id, MaxPlayersPerTeam, MaxPlayersPerTeam, ONESIDE_ALLIANCE_TEAM2, arenatype, isRated, arenaMinRating, arenaMaxRating, discardTime, (*(m_SelectionPools[ONESIDE_ALLIANCE_TEAM1].SelectedGroups.begin()))->ArenaTeamId);

                // change back the team to ally
                for (itr = m_SelectionPools[ONESIDE_ALLIANCE_TEAM1].SelectedGroups.begin(); itr != m_SelectionPools[ONESIDE_ALLIANCE_TEAM1].SelectedGroups.end(); ++itr)
                    (*itr)->Team=ALLIANCE;
            }

            if (!bOneSideAllyTeam2)
                bOneSideAllyTeam1 = false;
        }
        // 1-sided BuildSelectionPool() will work, because the MinPlayersPerTeam == MaxPlayersPerTeam in every arena!!!!
        if ((bOneSideHordeTeam1 && bOneSideHordeTeam2) ||
            (bOneSideAllyTeam1 && bOneSideAllyTeam2))
        {
            // which side has enough players?
            uint32 side = 0;
            SelectionPoolBuildMode mode1, mode2;
            // find out what pools are we using
            if (bOneSideAllyTeam1 && bOneSideAllyTeam2)
            {
                side = ALLIANCE;
                mode1 = ONESIDE_ALLIANCE_TEAM1;
                mode2 = ONESIDE_ALLIANCE_TEAM2;
            }
            else
            {
                side = HORDE;
                mode1 = ONESIDE_HORDE_TEAM1;
                mode2 = ONESIDE_HORDE_TEAM2;
            }

            // create random arena
            uint8 arenas[] = {BATTLEGROUND_NA, BATTLEGROUND_BE, BATTLEGROUND_RL};
            uint32 arena_num = urand(0, 2);
            Battleground* bg2 = NULL;
            if (!(bg2 = sBattlegroundMgr->CreateNewBattleground(arenas[arena_num%3], arenatype, isRated)) &&
                !(bg2 = sBattlegroundMgr->CreateNewBattleground(arenas[(arena_num+1)%3], arenatype, isRated)) &&
                !(bg2 = sBattlegroundMgr->CreateNewBattleground(arenas[(arena_num+2)%3], arenatype, isRated)))
            {
                sLog->outError("Could not create arena.");
                return;
            }

            sLog->outDebug("Battleground: One-faction arena created.");
            // init stats
            if (sBattlegroundMgr->isArenaTesting())
            {
                bg2->SetMaxPlayersPerTeam(1);
                bg2->SetMaxPlayers(2);
            }
            else
            {
                switch (arenatype)
                {
                case ARENA_TYPE_2v2:
                    bg2->SetMaxPlayersPerTeam(2);
                    bg2->SetMaxPlayers(4);
                    break;
                case ARENA_TYPE_3v3:
                    bg2->SetMaxPlayersPerTeam(3);
                    bg2->SetMaxPlayers(6);
                    break;
                case ARENA_TYPE_5v5:
                    bg2->SetMaxPlayersPerTeam(5);
                    bg2->SetMaxPlayers(10);
                    break;
                default:
                    break;
                }
            }

            // assigned team of the other group
            uint32 other_side;
            if (side == ALLIANCE)
                other_side = HORDE;
            else
                other_side = ALLIANCE;

            bg2->SetQueueType(queue_id);

            std::list<GroupQueueInfo* >::iterator itr;

            // invite players from the first group as horde players (actually green team)
            for (itr = m_SelectionPools[mode1].SelectedGroups.begin(); itr != m_SelectionPools[mode1].SelectedGroups.end(); ++itr)
            {
                InviteGroupToBG((*itr), bg2, HORDE);
            }

            // invite players from the second group as ally players (actually gold team)
            for (itr = m_SelectionPools[mode2].SelectedGroups.begin(); itr != m_SelectionPools[mode2].SelectedGroups.end(); ++itr)
            {
                InviteGroupToBG((*itr), bg2, ALLIANCE);
            }

            if (isRated)
            {
                std::list<GroupQueueInfo* >::iterator itr_alliance = m_SelectionPools[mode1].SelectedGroups.begin();
                std::list<GroupQueueInfo* >::iterator itr_horde = m_SelectionPools[mode2].SelectedGroups.begin();
                (*itr_alliance)->OpponentsTeamRating = (*itr_horde)->ArenaTeamRating;
                (*itr_horde)->OpponentsTeamRating = (*itr_alliance)->ArenaTeamRating;
            }

            bg2->StartBattleground();
        }
    }
}

/*********************************************************/
/***            BATTLEGROUND QUEUE EVENTS              ***/
/*********************************************************/

bool BGQueueInviteEvent::Execute(uint64 /*e_time*/, uint32 /*p_time*/)
{
    Player* plr = sObjectMgr->GetPlayer(m_PlayerGuid);

    // player logged off (we should do nothing, he is correctly removed from queue in another procedure)
    if (!plr)
        return true;

    // Player can be in another BG queue and must be removed in normal way in any case
    // // player is already in battleground ... do nothing (battleground queue status is deleted when player is teleported to BG)
    // if (plr->GetBattlegroundId() > 0)
    //    return true;

    Battleground* bg = sBattlegroundMgr->GetBattleground(m_BgInstanceGUID);
    if (!bg)
        return true;

    uint32 queueSlot = plr->GetBattlegroundQueueIndex(bg->GetTypeID());
    if (queueSlot < PLAYER_MAX_BATTLEGROUND_QUEUES)         // player is in queue
    {
        uint32 bgQueueTypeId = sBattlegroundMgr->BGQueueTypeId(bg->GetTypeID(), bg->GetArenaType());
        uint32 queueSlot = plr->GetBattlegroundQueueIndex(bgQueueTypeId);
        if (queueSlot < PLAYER_MAX_BATTLEGROUND_QUEUES) // player is in queue
        {
            // check if player is invited to this bg ... this check must be here, because when player leaves queue and joins another, it would cause a problems
            BattlegroundQueue::QueuedPlayersMap const& qpMap = sBattlegroundMgr->m_BattlegroundQueues[bgQueueTypeId].m_QueuedPlayers[plr->GetBattlegroundQueueIdFromLevel()];
            BattlegroundQueue::QueuedPlayersMap::const_iterator qItr = qpMap.find(m_PlayerGuid);
            if (qItr != qpMap.end() && qItr->second.GroupInfo->IsInvitedToBGInstanceGUID == m_BgInstanceGUID)
            {
                WorldPacket data;
                sBattlegroundMgr->BuildBattlegroundStatusPacket(&data, bg, qItr->second.GroupInfo->Team, queueSlot, STATUS_WAIT_JOIN, INVITE_ACCEPT_WAIT_TIME/2, 0);
                plr->GetSession()->SendPacket(&data);
            }
        }
    }
    return true;                                            //event will be deleted
}

void BGQueueInviteEvent::Abort(uint64 /*e_time*/)
{
    //this should not be called
    sLog->outError("Battleground invite event ABORTED!");
}

bool BGQueueRemoveEvent::Execute(uint64 /*e_time*/, uint32 /*p_time*/)
{
    Player* plr = sObjectMgr->GetPlayer(m_PlayerGuid);
    if (!plr)
        // player logged off (we should do nothing, he is correctly removed from queue in another procedure)
        return true;

    Battleground* bg = sBattlegroundMgr->GetBattleground(m_BgInstanceGUID);
    if (!bg)
        return true;

    sLog->outDebug("Battleground: removing player %u from bg queue for instance %u because of not pressing enter battle in time.", plr->GetGUIDLow(), m_BgInstanceGUID);

    uint32 bgQueueTypeId = sBattlegroundMgr->BGQueueTypeId(bg->GetTypeID(), bg->GetArenaType());
    uint32 queueSlot = plr->GetBattlegroundQueueIndex(bgQueueTypeId);
    if (queueSlot < PLAYER_MAX_BATTLEGROUND_QUEUES) // player is in queue
    {
        // check if player is invited to this bg ... this check must be here, because when player leaves queue and joins another, it would cause a problems
        BattlegroundQueue::QueuedPlayersMap::iterator qMapItr = sBattlegroundMgr->m_BattlegroundQueues[bgQueueTypeId].m_QueuedPlayers[plr->GetBattlegroundQueueIdFromLevel()].find(m_PlayerGuid);
        if (qMapItr != sBattlegroundMgr->m_BattlegroundQueues[bgQueueTypeId].m_QueuedPlayers[plr->GetBattlegroundQueueIdFromLevel()].end() && qMapItr->second.GroupInfo && qMapItr->second.GroupInfo->IsInvitedToBGInstanceGUID == m_BgInstanceGUID)
        {
            plr->RemoveBattlegroundQueueId(bgQueueTypeId);
            sBattlegroundMgr->m_BattlegroundQueues[bgQueueTypeId].RemovePlayer(m_PlayerGuid, true);
            sBattlegroundMgr->m_BattlegroundQueues[bgQueueTypeId].Update(bgQueueTypeId, bg->GetQueueType());
            WorldPacket data;
            sBattlegroundMgr->BuildBattlegroundStatusPacket(&data, bg, m_PlayersTeam, queueSlot, STATUS_NONE, 0, 0);
            plr->GetSession()->SendPacket(&data);
        }
    }
    else
        sLog->outDebug("Battleground: Player was already removed from queue");

    //event will be deleted
    return true;
}

void BGQueueRemoveEvent::Abort(uint64 /*e_time*/)
{
    //this should not be called
    sLog->outError("Battleground remove event ABORTED!");
}
