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

#ifndef __BATTLEGROUNDQUEUE_H
#define __BATTLEGROUNDQUEUE_H

#include "Common.h"
#include "DBCEnums.h"
#include "Battleground.h"
#include "EventProcessor.h"

#define MAX_BATTLEGROUND_QUEUES 7        // for level ranges 10-19, 20-29, 30-39, 40-49, 50-59, 60-69, 70+
#define MAX_BATTLEGROUND_QUEUE_TYPES 8

//typedef std::map<uint32, BattlegroundQueue*> BattlegroundQueueSet;
typedef std::list<Battleground*> BGFreeSlotQueueType;

struct GroupQueueInfo;                                      // type predefinition
struct PlayerQueueInfo                                      // stores information for players in queue
{
    uint32  InviteTime;                                     // first invite time
    uint32  LastInviteTime;                                 // last invite time
    uint32  LastOnlineTime;                                 // for tracking and removing offline players from queue after 5 minutes
    GroupQueueInfo * GroupInfo;                             // pointer to the associated groupqueueinfo
};

struct GroupQueueInfo                                       // stores information about the group in queue (also used when joined as solo!)
{
    std::map<uint64, PlayerQueueInfo*> Players;             // player queue info map
    uint32  Team;                                           // Player team (ALLIANCE/HORDE)
    uint32  BgTypeId;                                       // battleground type id
    bool    IsRated;                                        // rated
    uint8   ArenaType;                                      // 2v2, 3v3, 5v5 or 0 when BG
    uint32  ArenaTeamId;                                    // team id if rated match
    uint32  JoinTime;                                       // time when group was added
    uint32  IsInvitedToBGInstanceGUID;                      // was invited to certain BG
    uint32  ArenaTeamRating;                                // if rated match, inited to the rating of the team
    uint32  OpponentsTeamRating;                            // for rated arena matches
};

class Battleground;
class BattlegroundQueue
{
    public:
        BattlegroundQueue();
        ~BattlegroundQueue();

        void Update(uint32 bgTypeId, uint32 queue_id, uint8 arenatype = 0, bool isRated = false, uint32 minRating = 0);

        GroupQueueInfo * AddGroup(Player * leader, uint32 BgTypeId, uint8 ArenaType, bool isRated, uint32 ArenaRating, uint32 ArenaTeamId = 0);
        void AddPlayer(Player *plr, GroupQueueInfo *ginfo);
        void RemovePlayer(uint64 guid, bool decreaseInvitedCount);
        void DecreaseGroupLength(uint32 queueId, uint32 AsGroup);
        void BGEndedRemoveInvites(Battleground * bg);

        typedef std::map<uint64, PlayerQueueInfo> QueuedPlayersMap;
        QueuedPlayersMap m_QueuedPlayers[MAX_BATTLEGROUND_QUEUES];

        typedef std::list<GroupQueueInfo*> QueuedGroupsList;
        QueuedGroupsList m_QueuedGroups[MAX_BATTLEGROUND_QUEUES];

        // class to hold pointers to the groups eligible for a specific selection pool building mode
        class EligibleGroups : public std::list<GroupQueueInfo *>
        {
        public:
            void Init(QueuedGroupsList * source, uint32 BgTypeId, uint32 side, uint32 MaxPlayers, uint8 ArenaType = 0, bool IsRated = false, uint32 MinRating = 0, uint32 MaxRating = 0, uint32 DisregardTime = 0, uint32 excludeTeam = 0);
        };

        EligibleGroups m_EligibleGroups;

        // class to select and invite groups to bg
        class SelectionPool
        {
        public:
            void Init(EligibleGroups * curr);
            void AddGroup(GroupQueueInfo * group);
            void RemoveGroup(GroupQueueInfo * group);
            uint32 GetPlayerCount() const {return PlayerCount;}
            bool Build(uint32 MinPlayers, uint32 MaxPlayers, EligibleGroups::iterator startitr);
        public:
            std::list<GroupQueueInfo *> SelectedGroups;
        private:
            uint32 PlayerCount;
            EligibleGroups * m_CurrEligGroups;
        };

        enum SelectionPoolBuildMode
        {
            NORMAL_ALLIANCE,
            NORMAL_HORDE,
            ONESIDE_ALLIANCE_TEAM1,
            ONESIDE_ALLIANCE_TEAM2,
            ONESIDE_HORDE_TEAM1,
            ONESIDE_HORDE_TEAM2,

            NUM_SELECTION_POOL_TYPES
        };

        SelectionPool m_SelectionPools[NUM_SELECTION_POOL_TYPES];

        bool BuildSelectionPool(uint32 bgTypeId, uint32 queue_id, uint32 MinPlayers, uint32 MaxPlayers, SelectionPoolBuildMode mode, uint8 ArenaType = 0, bool isRated = false, uint32 MinRating = 0, uint32 MaxRating = 0, uint32 DisregardTime = 0, uint32 excludeTeam = 0);

    private:

        bool InviteGroupToBG(GroupQueueInfo * ginfo, Battleground * bg, uint32 side);
};

/*
    This class is used to invite player to BG again, when minute lasts from his first invitation
    it is capable to solve all possibilities
*/
class BGQueueInviteEvent : public BasicEvent
{
    public:
        BGQueueInviteEvent(uint64 pl_guid, uint32 BgInstanceGUID) : m_PlayerGuid(pl_guid), m_BgInstanceGUID(BgInstanceGUID) {};
        virtual ~BGQueueInviteEvent() {};

        virtual bool Execute(uint64 e_time, uint32 p_time);
        virtual void Abort(uint64 e_time);
    private:
        uint64 m_PlayerGuid;
        uint32 m_BgInstanceGUID;
};

/*
    This class is used to remove player from BG queue after 2 minutes from first invitation
*/
class BGQueueRemoveEvent : public BasicEvent
{
    public:
        BGQueueRemoveEvent(uint64 pl_guid, uint32 bgInstanceGUID, uint32 playersTeam) : m_PlayerGuid(pl_guid), m_BgInstanceGUID(bgInstanceGUID), m_PlayersTeam(playersTeam) {};
        virtual ~BGQueueRemoveEvent() {};

        virtual bool Execute(uint64 e_time, uint32 p_time);
        virtual void Abort(uint64 e_time);
    private:
        uint64 m_PlayerGuid;
        uint32 m_BgInstanceGUID;
        uint32 m_PlayersTeam;
};

#endif
