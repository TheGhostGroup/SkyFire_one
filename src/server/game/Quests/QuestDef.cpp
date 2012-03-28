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

#include "QuestDef.h"
#include "Player.h"
#include "World.h"

Quest::Quest(Field * questRecord)
{
    QuestId = questRecord[0].GetUInt32();
    QuestMethod = questRecord[1].GetUInt32();
    ZoneOrSort = questRecord[2].GetInt32();
    SkillOrClass = questRecord[3].GetInt32();
    MinLevel = questRecord[4].GetUInt32();
    QuestLevel = questRecord[5].GetInt32();
    Type = questRecord[6].GetUInt32();
    RequiredRaces = questRecord[7].GetUInt32();
    RequiredSkillValue = questRecord[8].GetUInt32();
    RepObjectiveFaction = questRecord[9].GetUInt32();
    RepObjectiveValue = questRecord[10].GetInt32();
    RepObjectiveFaction2 = questRecord[11].GetUInt32();
    RepObjectiveValue2 = questRecord[12].GetInt32();
    RequiredMinRepFaction = questRecord[13].GetUInt32();
    RequiredMinRepValue = questRecord[14].GetInt32();
    RequiredMaxRepFaction = questRecord[15].GetUInt32();
    RequiredMaxRepValue = questRecord[16].GetInt32();
    SuggestedPlayers = questRecord[17].GetUInt32();
    LimitTime = questRecord[18].GetUInt32();
    QuestFlags = questRecord[19].GetUInt16();
    uint32 SpecialFlags = questRecord[20].GetUInt16();
    CharTitleId = questRecord[21].GetUInt32();
    PrevQuestId = questRecord[22].GetInt32();
    NextQuestId = questRecord[23].GetInt32();
    ExclusiveGroup = questRecord[24].GetInt32();
    NextQuestInChain = questRecord[25].GetUInt32();
    SrcItemId = questRecord[26].GetUInt32();
    SrcItemCount = questRecord[27].GetUInt32();
    SrcSpell = questRecord[28].GetUInt32();
    Title = questRecord[29].GetCppString();
    Details = questRecord[30].GetCppString();
    Objectives = questRecord[31].GetCppString();
    OfferRewardText = questRecord[32].GetCppString();
    RequestItemsText = questRecord[33].GetCppString();
    EndText = questRecord[34].GetCppString();

    for (int i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        ObjectiveText[i] = questRecord[35+i].GetCppString();

    for (int i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        ReqItemId[i] = questRecord[37+i].GetUInt32();

    for (int i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        ReqItemCount[i] = questRecord[41+i].GetUInt32();

    for (int i = 0; i < QUEST_SOURCE_ITEM_IDS_COUNT; ++i)
        ReqSourceId[i] = questRecord[45+i].GetUInt32();

    for (int i = 0; i < QUEST_SOURCE_ITEM_IDS_COUNT; ++i)
        ReqSourceCount[i] = questRecord[49+i].GetUInt32();

    for (int i = 0; i < QUEST_SOURCE_ITEM_IDS_COUNT; ++i)
        ReqSourceRef[i] = questRecord[53+i].GetUInt32();

    for (int i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        ReqCreatureOrGOId[i] = questRecord[57+i].GetInt32();

    for (int i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        ReqCreatureOrGOCount[i] = questRecord[61+i].GetUInt32();

    for (int i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        ReqSpell[i] = questRecord[65+i].GetUInt32();

    for (int i = 0; i < QUEST_REWARD_CHOICES_COUNT; ++i)
        RewChoiceItemId[i] = questRecord[69+i].GetUInt32();

    for (int i = 0; i < QUEST_REWARD_CHOICES_COUNT; ++i)
        RewChoiceItemCount[i] = questRecord[75+i].GetUInt32();

    for (int i = 0; i < QUEST_REWARDS_COUNT; ++i)
        RewItemId[i] = questRecord[81+i].GetUInt32();

    for (int i = 0; i < QUEST_REWARDS_COUNT; ++i)
        RewItemCount[i] = questRecord[85+i].GetUInt32();

    for (int i = 0; i < QUEST_REPUTATIONS_COUNT; ++i)
        RewRepFaction[i] = questRecord[89+i].GetUInt32();

    for (int i = 0; i < QUEST_REPUTATIONS_COUNT; ++i)
        RewRepValue[i] = questRecord[94+i].GetInt32();

    RewHonorableKills = questRecord[99].GetUInt32();
    RewOrReqMoney = questRecord[100].GetInt32();
    RewMoneyMaxLevel = questRecord[101].GetUInt32();
    RewSpell = questRecord[102].GetUInt32();
    RewSpellCast = questRecord[103].GetUInt32();
    RewMailTemplateId = questRecord[104].GetUInt32();
    RewMailDelaySecs = questRecord[105].GetUInt32();
    PointMapId = questRecord[106].GetUInt32();
    PointX = questRecord[107].GetFloat();
    PointY = questRecord[108].GetFloat();
    PointOpt = questRecord[109].GetUInt32();

    for (int i = 0; i < QUEST_EMOTE_COUNT; ++i)
        DetailsEmote[i] = questRecord[110+i].GetUInt32();

    IncompleteEmote = questRecord[114].GetUInt32();
    CompleteEmote = questRecord[115].GetUInt32();

    for (int i = 0; i < QUEST_EMOTE_COUNT; ++i)
        OfferRewardEmote[i] = questRecord[116+i].GetInt32();

    QuestStartScript = questRecord[120].GetUInt32();
    QuestCompleteScript = questRecord[121].GetUInt32();

    QuestFlags |= SpecialFlags << 16;

    m_reqitemscount = 0;
    m_reqCreatureOrGOcount = 0;
    m_rewitemscount = 0;
    m_rewchoiceitemscount = 0;

    for (int i = 0; i < QUEST_OBJECTIVES_COUNT; i++)
    {
        if (ReqItemId[i])
            ++m_reqitemscount;
        if (ReqCreatureOrGOId[i])
            ++m_reqCreatureOrGOcount;
    }

    for (int i = 0; i < QUEST_REWARDS_COUNT; i++)
    {
        if (RewItemId[i])
            ++m_rewitemscount;
    }

    for (int i = 0; i < QUEST_REWARD_CHOICES_COUNT; i++)
    {
        if (RewChoiceItemId[i])
            ++m_rewchoiceitemscount;
    }
}

uint32 Quest::XPValue (Player* player) const
{
    if (player)
    {
        if (RewMoneyMaxLevel > 0)
        {
            uint32 pLevel = player->getLevel();
            uint32 qLevel = (QuestLevel > 0) ? QuestLevel : pLevel;
            float fullxp = 0;
            if (qLevel >= 65)
                fullxp = RewMoneyMaxLevel / 6.0f;
            else if (qLevel == 64)
                fullxp = RewMoneyMaxLevel / 4.8f;
            else if (qLevel == 63)
                fullxp = RewMoneyMaxLevel / 3.6f;
            else if (qLevel == 62)
                fullxp = RewMoneyMaxLevel / 2.4f;
            else if (qLevel == 61)
                fullxp = RewMoneyMaxLevel / 1.2f;
            else if (qLevel > 0 && qLevel <= 60)
                fullxp = RewMoneyMaxLevel / 0.6f;

            if (pLevel <= qLevel + 5)
                return (uint32)fullxp;
            else if (pLevel == qLevel + 6)
                return (uint32)(fullxp * 0.8f);
            else if (pLevel == qLevel + 7)
                return (uint32)(fullxp * 0.6f);
            else if (pLevel == qLevel + 8)
                return (uint32)(fullxp * 0.4f);
            else if (pLevel == qLevel + 9)
                return (uint32)(fullxp * 0.2f);
            else
                return (uint32)(fullxp * 0.1f);
        }
    }
    return 0;
}

int32 Quest::GetRewOrReqMoney() const
{
    if (RewOrReqMoney <= 0)
        return RewOrReqMoney;

    return int32(RewOrReqMoney * sWorld->getRate(RATE_DROP_MONEY));
}

