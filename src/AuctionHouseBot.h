/*
 * Copyright (C) 2008-2010 Trinity <http://www.trinitycore.org/>
/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef AUCTION_HOUSE_BOT_H
#define AUCTION_HOUSE_BOT_H

#include "ObjectGuid.h"
#include "ItemTemplate.h"
#include "AuctionHouseBotConfig.h"
#include "DatabaseEnvFwd.h"
#include <vector>
#include <unordered_set>

struct AuctionEntry;
class Player;
class WorldSession;

class AuctionHouseBot
{
public:
    AuctionHouseBot();
    ~AuctionHouseBot() = default;

    static AuctionHouseBot* instance();

    void Update();
    void Initialize();
    void InitializeConfiguration();
    void LoadValues(AHBConfig*);
    void DecrementItemCounts(AuctionEntry* ah, uint32 itemEntry);
    void IncrementItemCounts(AuctionEntry* ah);
    void Commands(uint32, uint32, uint32, char*);
    ObjectGuid::LowType GetAHBplayerGUID() { return AHBplayerGUID; };

private:
    bool AHBSeller{ false };
    bool AHBBuyer{ false };
    bool BuyMethod{ false };
    bool SellMethod{ false };

    uint32 AHBplayerAccount;
    ObjectGuid::LowType AHBplayerGUID;
    uint32 ItemsPerCycle;

    bool Vendor_Items{ false };
    bool Loot_Items{ false };
    bool Other_Items{ false };
    bool Vendor_TGs{ false };
    bool Loot_TGs{ false };
    bool Other_TGs{ false };

    bool No_Bind{ false };
    bool Bind_When_Picked_Up{ false };
    bool Bind_When_Equipped{ false };
    bool Bind_When_Use{ false };
    bool Bind_Quest_Item{ false };

    bool DisablePermEnchant{ false };
    bool DisableConjured{ false };
    bool DisableGems{ false };
    bool DisableMoney{ false };
    bool DisableMoneyLoot{ false };
    bool DisableLootable{ false };;
    bool DisableKeys{ false };
    bool DisableDuration{ false };
    bool DisableBOP_Or_Quest_NoReqLevel{ false };

    bool DisableWarriorItems{ false };
    bool DisablePaladinItems{ false };
    bool DisableHunterItems{ false };
    bool DisableRogueItems{ false };
    bool DisablePriestItems{ false };
    bool DisableDKItems{ false };
    bool DisableShamanItems{ false };
    bool DisableMageItems{ false };
    bool DisableWarlockItems{ false };
    bool DisableUnusedClassItems{ false };
    bool DisableDruidItems{ false };

    uint32 DisableItemsBelowLevel{ 0 };
    uint32 DisableItemsAboveLevel{ 0 };
    uint32 DisableTGsBelowLevel{ 0 };
    uint32 DisableTGsAboveLevel{ 0 };
    uint32 DisableItemsBelowGUID{ 0 };
    uint32 DisableItemsAboveGUID{ 0 };
    uint32 DisableTGsBelowGUID{ 0 };
    uint32 DisableTGsAboveGUID{ 0 };
    uint32 DisableItemsBelowReqLevel{ 0 };
    uint32 DisableItemsAboveReqLevel{ 0 };
    uint32 DisableTGsBelowReqLevel{ 0 };
    uint32 DisableTGsAboveReqLevel{ 0 };
    uint32 DisableItemsBelowReqSkillRank{ 0 };
    uint32 DisableItemsAboveReqSkillRank{ 0 };
    uint32 DisableTGsBelowReqSkillRank{ 0 };
    uint32 DisableTGsAboveReqSkillRank{ 0 };

    std::unordered_set<uint32> DisableItemStore{};

    AHBConfig AllianceConfig;
    AHBConfig HordeConfig;
    AHBConfig NeutralConfig;

    Seconds _lastUpdateAlliance{ 0s };
    Seconds _lastUpdateHorde{ 0s };
    Seconds _lastUpdateNeutral{ 0s };

    std::array<std::vector<uint32>, AHB_MAX_QUALITY> _itemsBin{};
    std::vector<uint32> npcItems{};
    std::vector<uint32> lootItems{};

    inline uint32 minValue(uint32 a, uint32 b) { return a <= b ? a : b; };
    void AddNewAuctions(Player* AHBplayer, AHBConfig* config);
    void AddNewAuctionBuyerBotBid(std::shared_ptr<Player> player, std::shared_ptr<WorldSession> session, AHBConfig* config);
    void AddNewAuctionBuyerBotBidCallback(std::shared_ptr<Player> player, std::shared_ptr<WorldSession> session, std::shared_ptr<AHBConfig> config, QueryResult result);

    void ProcessQueryCallbacks();

    QueryCallbackProcessor _queryProcessor;
};

#define sAHBot AuctionHouseBot::instance()

#endif
