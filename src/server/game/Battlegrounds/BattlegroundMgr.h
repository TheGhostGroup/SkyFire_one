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

#ifndef __BATTLEGROUNDMGR_H
#define __BATTLEGROUNDMGR_H

#include "Battleground.h"
#include "BattlegroundQueue.h"

#include <ace/Singleton.h>

class Battleground;

//TODO it is not possible to have this structure, because we should have BattlegroundSet for each queue
//so i propose to change this type to array 1..MAX_BATTLEGROUND_TYPES of sets or maps..
typedef std::map<uint32, Battleground*> BattlegroundSet;

#define MAX_BATTLEGROUND_TYPES 9                            // each BG type will be in array
#define BATTLEGROUND_ARENA_POINT_DISTRIBUTION_DAY    86400     // seconds in a day

class BattlegroundMgr
{
    /// Todo: Thread safety?
    /* Construction */
    friend class ACE_Singleton<BattlegroundMgr, ACE_Null_Mutex>;
    BattlegroundMgr();
    public:
        ~BattlegroundMgr();
        void Update(time_t diff);

        /* Packet Building */
        void BuildPlayerJoinedBattlegroundPacket(WorldPacket *data, Player *plr);
        void BuildPlayerLeftBattlegroundPacket(WorldPacket *data, const uint64& guid);
        void BuildBattlegroundListPacket(WorldPacket *data, uint64 guid, Player *plr, uint32 bgTypeId);
        void BuildGroupJoinedBattlegroundPacket(WorldPacket *data, uint32 bgTypeId);
        void BuildUpdateWorldStatePacket(WorldPacket *data, uint32 field, uint32 value);
        void BuildPvpLogDataPacket(WorldPacket *data, Battleground *bg);
        void BuildBattlegroundStatusPacket(WorldPacket *data, Battleground *bg, uint32 team, uint8 QueueSlot, uint8 StatusID, uint32 Time1, uint32 Time2, uint32 arenatype = 0, uint8 israted = 0);
        void BuildPlaySoundPacket(WorldPacket *data, uint32 soundid);

        /* Player invitation */
        // called from Queue update, or from Addplayer to queue
        void InvitePlayer(Player* plr, uint32 bgInstanceGUID, uint32 team);

        /* Battlegrounds */
        BattlegroundSet::iterator GetBattlegroundsBegin() { return m_Battlegrounds.begin(); };
        BattlegroundSet::iterator GetBattlegroundsEnd() { return m_Battlegrounds.end(); };

        Battleground* GetBattleground(uint32 InstanceID)
        {
            if (!InstanceID)
                return NULL;
            BattlegroundSet::iterator i = m_Battlegrounds.find(InstanceID);
            if (i != m_Battlegrounds.end())
                return i->second;
            else
                return NULL;
        };

        Battleground * GetBattlegroundTemplate(uint32 bgTypeId);
        Battleground * CreateNewBattleground(uint32 bgTypeId, uint8 arenaType, bool isRated);

        uint32 CreateBattleground(uint32 bgTypeId, uint32 MinPlayersPerTeam, uint32 MaxPlayersPerTeam, uint32 LevelMin, uint32 LevelMax, char* BattlegroundName, uint32 MapID, float Team1StartLocX, float Team1StartLocY, float Team1StartLocZ, float Team1StartLocO, float Team2StartLocX, float Team2StartLocY, float Team2StartLocZ, float Team2StartLocO);

        void AddBattleground(uint32 ID, Battleground* BG) { m_Battlegrounds[ID] = BG; };
        void RemoveBattleground(uint32 instanceID);

        void CreateInitialBattlegrounds();
        void DeleteAlllBattlegrounds();

        void SendToBattleground(Player *pl, uint32 bgTypeId);

        /* Battleground queues */
        //these queues are instantiated when creating BattlegroundMrg
        BattlegroundQueue m_BattlegroundQueues[MAX_BATTLEGROUND_QUEUE_TYPES]; // public, because we need to access them in BG handler code
        BGFreeSlotQueueType BGFreeSlotQueue[MAX_BATTLEGROUND_TYPES];

        void SendAreaSpiritHealerQueryOpcode(Player *pl, Battleground *bg, uint64 guid);

        bool IsArenaType(uint32 bgTypeId) const;
        bool IsBattlegroundType(uint32 bgTypeId) const;
        static uint32 BGQueueTypeId(uint32 bgTypeId, uint8 arenaType);
        uint32 BGTemplateId(uint32 bgQueueTypeId) const;
        uint8 BGArenaType(uint32 bgQueueTypeId) const;

        uint32 GetMaxRatingDifference() const {return m_MaxRatingDifference;}
        uint32 GetRatingDiscardTimer() const {return m_RatingDiscardTimer;}

        void InitAutomaticArenaPointDistribution();
        void DistributeArenaPoints();
        uint32 GetPrematureFinishTime() const {return m_PrematureFinishTimer;}
        void ToggleArenaTesting();
        void ToggleTesting();
        bool isArenaTesting() const { return m_ArenaTesting; }
        bool isTesting() const { return m_Testing; }

        void SetHolidayWeekends(uint32 mask);
    private:

        /* Battlegrounds */
        BattlegroundSet m_Battlegrounds;
        uint32 m_MaxRatingDifference;
        uint32 m_RatingDiscardTimer;
        uint32 m_NextRatingDiscardUpdate;
        bool   m_AutoDistributePoints;
        uint64 m_NextAutoDistributionTime;
        uint32 m_AutoDistributionTimeChecker;
        uint32 m_PrematureFinishTimer;
        bool   m_ArenaTesting;
        bool   m_Testing;
};

#define sBattlegroundMgr ACE_Singleton<BattlegroundMgr, ACE_Null_Mutex>::instance()
#endif
