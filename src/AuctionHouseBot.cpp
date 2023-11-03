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

#include "ObjectMgr.h"
#include "AuctionHouseMgr.h"
#include "AuctionHouseBot.h"
#include "Config.h"
#include "Player.h"
#include "WorldSession.h"
#include "GameTime.h"
#include "DatabaseEnv.h"
#include "StringConvert.h"
#include "StringFormat.h"
#include <vector>

AuctionHouseBot::AuctionHouseBot()
{
    DisableItemsBelowLevel = 0;
    DisableItemsAboveLevel = 0;
    DisableTGsBelowLevel = 0;
    DisableTGsAboveLevel = 0;
    DisableItemsBelowGUID = 0;
    DisableItemsAboveGUID = 0;
    DisableTGsBelowGUID = 0;
    DisableTGsAboveGUID = 0;
    DisableItemsBelowReqLevel = 0;
    DisableItemsAboveReqLevel = 0;
    DisableTGsBelowReqLevel = 0;
    DisableTGsAboveReqLevel = 0;
    DisableItemsBelowReqSkillRank = 0;
    DisableItemsAboveReqSkillRank = 0;
    DisableTGsBelowReqSkillRank = 0;
    DisableTGsAboveReqSkillRank = 0;

    //End Filters

    _lastUpdateAlliance = GameTime::GetGameTime();
    _lastUpdateHorde = GameTime::GetGameTime();
    _lastUpdateNeutral = GameTime::GetGameTime();

    AllianceConfig = AHBConfig(AUCTIONHOUSE_ALLIANCE);
    HordeConfig = AHBConfig(AUCTIONHOUSE_HORDE);
    NeutralConfig = AHBConfig(AUCTIONHOUSE_NEUTRAL);
}

/*static*/ AuctionHouseBot* AuctionHouseBot::instance()
{
    static AuctionHouseBot instance;
    return &instance;
}

void AuctionHouseBot::AddNewAuctions(Player* AHBplayer, AHBConfig* config)
{
    if (!AHBSeller)
    {
        LOG_DEBUG("module.ahbot", "AHSeller: Disabled");
        return;
    }

    uint32 minItems = config->GetMinItems();
    uint32 maxItems = config->GetMaxItems();

    if (maxItems == 0)
    {
        LOG_DEBUG("module.ahbot", "Auctions disabled");
        return;
    }

    AuctionHouseEntry const* ahEntry =  sAuctionMgr->GetAuctionHouseEntry(config->GetAuctionHouseFactionID());
    if (!ahEntry)
    {
        return;
    }

    AuctionHouseObject* auctionHouse =  sAuctionMgr->GetAuctionsMap(config->GetAuctionHouseFactionID());
    if (!auctionHouse)
    {
        return;
    }

    uint32 auctions = auctionHouse->Getcount();
    uint32 items = 0;

    if (auctions >= minItems)
    {
        LOG_ERROR("module.ahbot", "AHSeller: Auctions above minimum");
        return;
    }

    if (auctions >= maxItems)
    {
        LOG_ERROR("module.ahbot", "AHSeller: Auctions at or above maximum");
        return;
    }

    if ((maxItems - auctions) >= ItemsPerCycle)
        items = ItemsPerCycle;
    else
        items = (maxItems - auctions);

    LOG_DEBUG("module.ahbot", "AHSeller: Adding {} Auctions", items);
    LOG_DEBUG("module.ahbot", "AHSeller: Current house id is {}", config->GetAuctionHouseID());

    std::array<uint32, AHB_MAX_QUALITY> percents = *config->GetPercents();
    std::array<uint32, AHB_MAX_QUALITY> itemsCount = *config->GetItemCounts();

    LOG_DEBUG("module.ahbot", "AHSeller: {} items", items);

    // only insert a few at a time, so as not to peg the processor
    for (uint32 cnt = 1; cnt <= items; cnt++)
    {
        LOG_DEBUG("module.ahbot", "AHSeller: {} count", cnt);

        uint32 itemID = 0;
        uint32 itemColor = 99;
        uint32 loopbreaker = 0;

        while (itemID == 0 && loopbreaker <= 50)
        {
            ++loopbreaker;
            uint32 choice = urand(0, 13);
            itemColor = choice;

            auto const& itemsBin = _itemsBin[choice];

            if (!itemsBin.empty() && itemsCount[choice] < percents[choice])
                itemID = Acore::Containers::SelectRandomContainerElement(itemsBin);
            else
                continue;

            if (!itemID)
            {
                LOG_ERROR("module.ahbot", "AHSeller: Item::CreateItem() - ItemID is 0");
                continue;
            }

            ItemTemplate const* prototype = sObjectMgr->GetItemTemplate(itemID);
            if (!prototype)
            {
                LOG_ERROR("module.ahbot", "AHSeller: ItemTemplate is nullptr!");
                continue;
            }

            Item* item = Item::CreateItem(itemID, 1, AHBplayer);
            if (!item)
            {
                LOG_ERROR("module.ahbot", "AHSeller: Item not created!");
                break;
            }

            item->AddToUpdateQueueOf(AHBplayer);

            uint32 randomPropertyId = Item::GenerateItemRandomPropertyId(itemID);
            if (randomPropertyId != 0)
                item->SetItemRandomProperties(randomPropertyId);

            uint64 buyoutPrice = 0;
            uint64 bidPrice = 0;
            uint32 stackCount = 1;

            if (SellMethod)
                buyoutPrice = prototype->BuyPrice;
            else
                buyoutPrice = prototype->SellPrice;

            if (prototype->Quality <= AHB_MAX_DEFAULT_QUALITY)
            {
                if (config->GetMaxStack(prototype->Quality) > 1 && item->GetMaxStackCount() > 1)
                    stackCount = urand(1, minValue(item->GetMaxStackCount(), config->GetMaxStack(prototype->Quality)));
                else if (config->GetMaxStack(prototype->Quality) == 0 && item->GetMaxStackCount() > 1)
                    stackCount = urand(1, item->GetMaxStackCount());
                else
                    stackCount = 1;

                buyoutPrice *= urand(config->GetMinPrice(prototype->Quality), config->GetMaxPrice(prototype->Quality));
                buyoutPrice /= 100;
                bidPrice = buyoutPrice * urand(config->GetMinBidPrice(prototype->Quality), config->GetMaxBidPrice(prototype->Quality));
                bidPrice /= 100;
            }
            else
            {
                // quality is something it shouldn't be, let's get out of here
                LOG_ERROR("module.ahbot", "AHBuyer: Quality {} not Supported", prototype->Quality);
                item->RemoveFromUpdateQueueOf(AHBplayer);
                continue;
            }

            uint32 etime = urand(1,3);
            Seconds elapsedTime = 24h;

            switch(etime)
            {
            case 1:
                elapsedTime = 12h;
                break;
            case 2:
                elapsedTime = 24h;
                break;
            case 3:
                elapsedTime = 48h;
                break;
            default:
                elapsedTime = 24h;
                break;
            }

            item->SetCount(stackCount);

            uint32 dep =  sAuctionMgr->GetAuctionDeposit(ahEntry, elapsedTime.count(), item, stackCount);

            auto trans = CharacterDatabase.BeginTransaction();

            AuctionEntry* auctionEntry = new AuctionEntry();
            auctionEntry->Id = sObjectMgr->GenerateAuctionID();
            auctionEntry->houseId = config->GetAuctionHouseID();
			auctionEntry->item_guid = item->GetGUID();
            auctionEntry->item_template = item->GetEntry();
            auctionEntry->itemCount = item->GetCount();
            auctionEntry->owner = AHBplayer->GetGUID();
            auctionEntry->startbid = bidPrice * stackCount;
            auctionEntry->buyout = buyoutPrice * stackCount;
            auctionEntry->bid = 0;
            auctionEntry->deposit = dep;
            auctionEntry->expire_time = elapsedTime.count() + GameTime::GetGameTime().count();
            auctionEntry->auctionHouseEntry = ahEntry;

            item->SaveToDB(trans);
            item->RemoveFromUpdateQueueOf(AHBplayer);

            sAuctionMgr->AddAItem(item);

            auctionHouse->AddAuction(auctionEntry);
            auctionEntry->SaveToDB(trans);

            CharacterDatabase.CommitTransaction(trans);

            itemsCount[itemColor]++;
        }
    }
}

void AuctionHouseBot::AddNewAuctionBuyerBotBid(std::shared_ptr<Player> player, std::shared_ptr<WorldSession> session, AHBConfig* config)
{
    if (!AHBBuyer)
    {
        LOG_ERROR("module.ahbot", "AHBuyer: Disabled");
        return;
    }

    auto sharedConfig = std::make_shared<AHBConfig>(*config);

    _queryProcessor.AddCallback(CharacterDatabase.AsyncQuery(Acore::StringFormatFmt("SELECT id FROM auctionhouse WHERE itemowner<>{} AND buyguid<>{}", AHBplayerGUID, AHBplayerGUID)).
        WithCallback(std::bind(&AuctionHouseBot::AddNewAuctionBuyerBotBidCallback, this, player, session, sharedConfig, std::placeholders::_1)));
}

void AuctionHouseBot::AddNewAuctionBuyerBotBidCallback(std::shared_ptr<Player> player, std::shared_ptr<WorldSession> session, std::shared_ptr<AHBConfig> config, QueryResult result)
{
    if (!result || !result->GetRowCount())
        return;

    // Fetches content of selected AH
    AuctionHouseObject* auctionHouse = sAuctionMgr->GetAuctionsMap(config->GetAuctionHouseFactionID());
    std::vector<uint32> possibleBids;

    do
    {
        uint32 tmpdata = result->Fetch()->Get<uint32>();
        possibleBids.push_back(tmpdata);
    } while (result->NextRow());

    for (uint32 count = 1; count <= config->GetBidsPerInterval(); ++count)
    {
        // Do we have anything to bid? If not, stop here.
        if (possibleBids.empty())
        {
            //if (debug_Out) sLog->outError( "AHBuyer: I have no items to bid on.");
            count = config->GetBidsPerInterval();
            continue;
        }

        // Choose random auction from possible auctions
        uint32 randomID = Acore::Containers::SelectRandomContainerElement(possibleBids);

        // from auctionhousehandler.cpp, creates auction pointer & player pointer
        AuctionEntry* auction = auctionHouse->GetAuction(randomID);

        // Erase the auction from the vector to prevent bidding on item in next iteration.
        std::erase(possibleBids, randomID);

        if (!auction)
            continue;

        // get exact item information
        Item* pItem = sAuctionMgr->GetAItem(auction->item_guid);
        if (!pItem)
        {
            LOG_DEBUG("module.ahbot", "AHBuyer: Item {} doesn't exist, perhaps bought already?", auction->item_guid.ToString());
            continue;
        }

        // get item prototype
        ItemTemplate const* prototype = sObjectMgr->GetItemTemplate(auction->item_template);

        // check which price we have to use, startbid or if it is bidded already
        uint32 currentprice;
        if (auction->bid)
            currentprice = auction->bid;
        else
            currentprice = auction->startbid;

        // Prepare portion from maximum bid
        float bidrate = frand(0.01f, 1.0f);
        float bidMax = 0;

        // check that bid has acceptable value and take bid based on vendorprice, stacksize and quality
        if (BuyMethod)
        {
            if (prototype->Quality <= AHB_MAX_DEFAULT_QUALITY)
            {
                if (currentprice < prototype->SellPrice * pItem->GetCount() * config->GetBuyerPrice(prototype->Quality))
                    bidMax = prototype->SellPrice * pItem->GetCount() * config->GetBuyerPrice(prototype->Quality);
            }
            else
            {
                // quality is something it shouldn't be, let's get out of here
                LOG_DEBUG("module.ahbot", "AHBuyer: Quality {} not Supported", prototype->Quality);
                continue;
            }
        }
        else
        {
            if (prototype->Quality <= AHB_MAX_DEFAULT_QUALITY)
            {
                if (currentprice < prototype->BuyPrice * pItem->GetCount() * config->GetBuyerPrice(prototype->Quality))
                    bidMax = prototype->BuyPrice * pItem->GetCount() * config->GetBuyerPrice(prototype->Quality);
            }
            else
            {
                // quality is something it shouldn't be, let's get out of here
                LOG_DEBUG("module.ahbot", "AHBuyer: Quality {} not Supported", prototype->Quality);
                continue;
            }
        }

        // check some special items, and do recalculating to their prices
        switch (prototype->Class)
        {
            // ammo
        case 6:
            bidMax = 0;
            break;
        default:
            break;
        }

        if (bidMax == 0)
        {
            // quality check failed to get bidmax, let's get out of here
            continue;
        }

        // Calculate our bid
        float bidvalue = currentprice + ((bidMax - currentprice) * bidrate);

        // Convert to uint32
        uint32 bidprice = static_cast<uint32>(bidvalue);

        // Check our bid is high enough to be valid. If not, correct it to minimum.
        if ((currentprice + auction->GetAuctionOutBid()) > bidprice)
            bidprice = currentprice + auction->GetAuctionOutBid();

        LOG_DEBUG("module.ahbot", "-------------------------------------------------");
        LOG_DEBUG("module.ahbot", "AHBuyer: Info for Auction #{}:", auction->Id);
        LOG_DEBUG("module.ahbot", "AHBuyer: AuctionHouse: {}", auction->GetHouseId());
        LOG_DEBUG("module.ahbot", "AHBuyer: Owner: {}", auction->owner.ToString());
        LOG_DEBUG("module.ahbot", "AHBuyer: Bidder: {}", auction->bidder.ToString());
        LOG_DEBUG("module.ahbot", "AHBuyer: Starting Bid: {}", auction->startbid);
        LOG_DEBUG("module.ahbot", "AHBuyer: Current Bid: {}", currentprice);
        LOG_DEBUG("module.ahbot", "AHBuyer: Buyout: {}", auction->buyout);
        LOG_DEBUG("module.ahbot", "AHBuyer: Deposit: {}", auction->deposit);
        LOG_DEBUG("module.ahbot", "AHBuyer: Expire Time: {}", uint32(auction->expire_time));
        LOG_DEBUG("module.ahbot", "AHBuyer: Bid Rate: {}", bidrate);
        LOG_DEBUG("module.ahbot", "AHBuyer: Bid Max: {}", bidMax);
        LOG_DEBUG("module.ahbot", "AHBuyer: Bid Value: {}", bidvalue);
        LOG_DEBUG("module.ahbot", "AHBuyer: Bid Price: {}", bidprice);
        LOG_DEBUG("module.ahbot", "AHBuyer: Item GUID: {}", auction->item_guid.ToString());
        LOG_DEBUG("module.ahbot", "AHBuyer: Item Template: {}", auction->item_template);
        LOG_DEBUG("module.ahbot", "AHBuyer: Item Info:");
        LOG_DEBUG("module.ahbot", "AHBuyer: Item ID: {}", prototype->ItemId);
        LOG_DEBUG("module.ahbot", "AHBuyer: Buy Price: {}", prototype->BuyPrice);
        LOG_DEBUG("module.ahbot", "AHBuyer: Sell Price: {}", prototype->SellPrice);
        LOG_DEBUG("module.ahbot", "AHBuyer: Bonding: {}", prototype->Bonding);
        LOG_DEBUG("module.ahbot", "AHBuyer: Quality: {}", prototype->Quality);
        LOG_DEBUG("module.ahbot", "AHBuyer: Item Level: {}", prototype->ItemLevel);
        LOG_DEBUG("module.ahbot", "AHBuyer: Ammo Type: {}", prototype->AmmoType);
        LOG_DEBUG("module.ahbot", "-------------------------------------------------");

        // Check whether we do normal bid, or buyout
        if (bidprice < auction->buyout || !auction->buyout)
        {
            if (auction->bidder && auction->bidder != player->GetGUID())
            {
                auto trans = CharacterDatabase.BeginTransaction();
                sAuctionMgr->SendAuctionOutbiddedMail(auction, bidprice, player.get(), trans);
                CharacterDatabase.CommitTransaction(trans);
            }

            auction->bidder = player->GetGUID();
            auction->bid = bidprice;

            // Saving auction into database
            CharacterDatabase.Execute("UPDATE auctionhouse SET buyguid = '{}', lastbid = '{}' WHERE id = '{}'", auction->bidder.GetCounter(), auction->bid, auction->Id);
        }
        else
        {
            auto trans = CharacterDatabase.BeginTransaction();

            // Buyout
            if (auction->bidder && player->GetGUID() != auction->bidder)
                sAuctionMgr->SendAuctionOutbiddedMail(auction, auction->buyout, player.get(), trans);

            auction->bidder = player->GetGUID();
            auction->bid = auction->buyout;

            // Send mails to buyer & seller
            //sAuctionMgr->SendAuctionSalePendingMail(auction, trans);
            sAuctionMgr->SendAuctionSuccessfulMail(auction, trans);
            sAuctionMgr->SendAuctionWonMail(auction, trans);
            auction->DeleteFromDB(trans);

            sAuctionMgr->RemoveAItem(auction->item_guid);
            auctionHouse->RemoveAuction(auction);
            CharacterDatabase.CommitTransaction(trans);
        }
    }
}

void AuctionHouseBot::Update()
{
    if (!AHBSeller && !AHBBuyer)
        return;

    if (!AHBplayerAccount || !AHBplayerGUID)
    {
        LOG_ERROR("module.ahbot", "{}: Invalid player data. Account {}. Guid {}", __FUNCTION__, AHBplayerAccount, AHBplayerGUID);
        return;
    }

    std::string accountName = "AuctionHouseBot_" + std::to_string(AHBplayerAccount);

    auto session = std::make_shared<WorldSession>(AHBplayerAccount, std::move(accountName), nullptr, SEC_PLAYER, sWorld->getIntConfig(CONFIG_EXPANSION), 0, LOCALE_enUS, 0, false, true, 0);

    std::shared_ptr<Player> playerBot(new Player(session.get()), [](Player* ptr)
    {
        ObjectAccessor::RemoveObject(ptr);
        delete ptr;
    });

    playerBot->Initialize(AHBplayerGUID);

    ObjectAccessor::AddObject(playerBot.get());

    Seconds newUpdate = GameTime::GetGameTime();

    // Add New Bids
    if (!sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_AUCTION))
    {
        AddNewAuctions(playerBot.get(), &AllianceConfig);
        if ((newUpdate - _lastUpdateAlliance >= AllianceConfig.GetBiddingInterval()) && AllianceConfig.GetBidsPerInterval() > 0)
        {
            LOG_DEBUG("module.ahbot", "AHBuyer: {} seconds have passed since last bid", newUpdate.count() - _lastUpdateAlliance.count());
            LOG_DEBUG("module.ahbot", "AHBuyer: Bidding on Alliance Auctions");
            AddNewAuctionBuyerBotBid(playerBot, session, &AllianceConfig);
            _lastUpdateAlliance = newUpdate;
        }

        AddNewAuctions(playerBot.get(), &HordeConfig);
        if ((newUpdate - _lastUpdateHorde >= HordeConfig.GetBiddingInterval()) && HordeConfig.GetBidsPerInterval() > 0)
        {
            LOG_DEBUG("module.ahbot", "AHBuyer: {} seconds have passed since last bid", newUpdate.count() - _lastUpdateHorde.count());
            LOG_DEBUG("module.ahbot", "AHBuyer: Bidding on Horde Auctions");
            AddNewAuctionBuyerBotBid(playerBot, session, &HordeConfig);
            _lastUpdateHorde = newUpdate;
        }
    }

    AddNewAuctions(playerBot.get(), &NeutralConfig);
    if ((newUpdate - _lastUpdateNeutral >= NeutralConfig.GetBiddingInterval()) && NeutralConfig.GetBidsPerInterval() > 0)
    {
        LOG_DEBUG("module.ahbot", "AHBuyer: {} seconds have passed since last bid", newUpdate.count() - _lastUpdateNeutral.count());
        LOG_DEBUG("module.ahbot", "AHBuyer: Bidding on Neutral Auctions");
        AddNewAuctionBuyerBotBid(playerBot, session, &NeutralConfig);
        _lastUpdateNeutral = newUpdate;
    }

    ProcessQueryCallbacks();
}

void AuctionHouseBot::Initialize()
{
    DisableItemStore.clear();
    QueryResult result = WorldDatabase.Query("SELECT item FROM mod_auctionhousebot_disabled_items");

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            DisableItemStore.emplace(fields[0].Get<uint32>());
        } while (result->NextRow());
    }

    // End Filters
    if (!sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_AUCTION))
    {
        LoadValues(&AllianceConfig);
        LoadValues(&HordeConfig);
    }

    LoadValues(&NeutralConfig);

    //
    // check if the AHBot account/GUID in the config actually exists
    //

    if (AHBplayerAccount || AHBplayerGUID)
    {
        QueryResult result = CharacterDatabase.Query("SELECT 1 FROM characters WHERE account = {} AND guid = {}", AHBplayerAccount, AHBplayerGUID);
        if (!result)
        {
            LOG_ERROR("module", "AuctionHouseBot: The account/GUID-information set for your AHBot is incorrect (account: {} guid: {})", AHBplayerAccount, AHBplayerGUID);
            return;
        }
    }

    if (AHBSeller)
    {
        std::string npcQuery = "SELECT distinct item FROM npc_vendor";
        QueryResult results = WorldDatabase.Query(npcQuery);
        if (results)
        {
            do
            {
                Field* fields = results->Fetch();
                npcItems.push_back(fields[0].Get<int32>());

            } while (results->NextRow());
        }
        else
            LOG_ERROR("module.ahbot", "AuctionHouseBot: \"{}\" failed", npcQuery);

        std::string lootQuery = "SELECT item FROM creature_loot_template UNION "
            "SELECT item FROM reference_loot_template UNION "
            "SELECT item FROM disenchant_loot_template UNION "
            "SELECT item FROM fishing_loot_template UNION "
            "SELECT item FROM gameobject_loot_template UNION "
            "SELECT item FROM item_loot_template UNION "
            "SELECT item FROM milling_loot_template UNION "
            "SELECT item FROM pickpocketing_loot_template UNION "
            "SELECT item FROM prospecting_loot_template UNION "
            "SELECT item FROM skinning_loot_template";

        results = WorldDatabase.Query(lootQuery);
        if (results)
        {
            do
            {
                Field* fields = results->Fetch();
                lootItems.push_back(fields[0].Get<uint32>());

            } while (results->NextRow());
        }
        else
            LOG_ERROR("module.ahbot", "AuctionHouseBot: \"{}\" failed", lootQuery);

        for (auto const& [itemID, itemTemplate] : *sObjectMgr->GetItemTemplateStore())
        {
            switch (itemTemplate.Bonding)
            {
            case NO_BIND:
                if (!No_Bind)
                    continue;
                break;
            case BIND_WHEN_PICKED_UP:
                if (!Bind_When_Picked_Up)
                    continue;
                break;
            case BIND_WHEN_EQUIPED:
                if (!Bind_When_Equipped)
                    continue;
                break;
            case BIND_WHEN_USE:
                if (!Bind_When_Use)
                    continue;
                break;
            case BIND_QUEST_ITEM:
                if (!Bind_Quest_Item)
                    continue;
                break;
            default:
                continue;
                break;
            }

            if (SellMethod)
            {
                if (!itemTemplate.BuyPrice)
                    continue;
            }
            else
            {
                if (!itemTemplate.SellPrice)
                    continue;
            }

            if (itemTemplate.Quality > ITEM_QUALITY_ARTIFACT)
                continue;

            if (!Vendor_Items && itemTemplate.Class != ITEM_CLASS_TRADE_GOODS)
            {
                bool isVendorItem = false;

                for (unsigned int i = 0; (i < npcItems.size()) && (!isVendorItem); i++)
                {
                    if (itemTemplate.ItemId == npcItems[i])
                        isVendorItem = true;
                }

                if (isVendorItem)
                    continue;
            }

            if (!Vendor_TGs && itemTemplate.Class != ITEM_CLASS_TRADE_GOODS)
            {
                bool isVendorTG = false;

                for (unsigned int i = 0; (i < npcItems.size()) && (!isVendorTG); i++)
                {
                    if (itemTemplate.ItemId == npcItems[i])
                        isVendorTG = true;
                }

                if (isVendorTG)
                    continue;
            }

            if (!Loot_Items && itemTemplate.Class != ITEM_CLASS_TRADE_GOODS)
            {
                bool isLootItem = false;

                for (unsigned int i = 0; (i < lootItems.size()) && (!isLootItem); i++)
                {
                    if (itemTemplate.ItemId == lootItems[i])
                        isLootItem = true;
                }

                if (isLootItem)
                    continue;
            }

            if (!Loot_TGs && itemTemplate.Class != ITEM_CLASS_TRADE_GOODS)
            {
                bool isLootTG = false;

                for (unsigned int i = 0; (i < lootItems.size()) && (!isLootTG); i++)
                {
                    if (itemTemplate.ItemId == lootItems[i])
                        isLootTG = true;
                }

                if (isLootTG)
                    continue;
            }

            if (Other_Items && itemTemplate.Class != ITEM_CLASS_TRADE_GOODS)
            {
                bool isVendorItem = false;
                bool isLootItem = false;

                for (unsigned int i = 0; (i < npcItems.size()) && (!isVendorItem); i++)
                {
                    if (itemTemplate.ItemId == npcItems[i])
                        isVendorItem = true;
                }

                for (unsigned int i = 0; (i < lootItems.size()) && (!isLootItem); i++)
                {
                    if (itemTemplate.ItemId == lootItems[i])
                        isLootItem = true;
                }

                if (!isLootItem && !isVendorItem)
                    continue;
            }

            if (Other_TGs && itemTemplate.Class != ITEM_CLASS_TRADE_GOODS)
            {
                bool isVendorTG = false;
                bool isLootTG = false;

                for (unsigned int i = 0; (i < npcItems.size()) && (!isVendorTG); i++)
                {
                    if (itemTemplate.ItemId == npcItems[i])
                        isVendorTG = true;
                }

                for (unsigned int i = 0; (i < lootItems.size()) && (!isLootTG); i++)
                {
                    if (itemTemplate.ItemId == lootItems[i])
                        isLootTG = true;
                }

                if (!isLootTG && !isVendorTG)
                    continue;
            }

            // Disable items by Id
            if (DisableItemStore.find(itemTemplate.ItemId) != DisableItemStore.end())
            {
                LOG_DEBUG("module.ahbot.filters", "AuctionHouseBot: Item {} disabled (PTR/Beta/Unused Item)", itemTemplate.ItemId);
                continue;
            }

            // Disable permanent enchants items
            if (DisablePermEnchant && itemTemplate.Class == ITEM_CLASS_PERMANENT)
            {
                LOG_DEBUG("module.ahbot.filters", "AuctionHouseBot: Item {} disabled (Permanent Enchant Item)", itemTemplate.ItemId);
                continue;
            }

            // Disable conjured items
            if (DisableConjured && itemTemplate.IsConjuredConsumable())
            {
                LOG_DEBUG("module.ahbot.filters", "AuctionHouseBot: Item {} disabled (Conjured Consumable)", itemTemplate.ItemId);
                continue;
            }

            // Disable gems
            if (DisableGems && itemTemplate.Class == ITEM_CLASS_GEM)
            {
                LOG_DEBUG("module.ahbot.filters", "AuctionHouseBot: Item {} disabled (Gem)", itemTemplate.ItemId);
                continue;
            }

            // Disable money
            if (DisableMoney && itemTemplate.Class == ITEM_CLASS_MONEY)
            {
                LOG_DEBUG("module.ahbot.filters", "AuctionHouseBot: Item {} disabled (Money)", itemTemplate.ItemId);
                continue;
            }

            // Disable moneyloot
            if (DisableMoneyLoot && itemTemplate.MinMoneyLoot)
            {
                LOG_DEBUG("module.ahbot.filters", "AuctionHouseBot: Item {} disabled (MoneyLoot)", itemTemplate.ItemId);
                continue;
            }

            // Disable lootable items
            if (DisableLootable && itemTemplate.Flags & 4)
            {
                LOG_DEBUG("module.ahbot.filters", "AuctionHouseBot: Item {} disabled (Lootable Item)", itemTemplate.ItemId);
                continue;
            }

            // Disable Keys
            if (DisableKeys && itemTemplate.Class == ITEM_CLASS_KEY)
            {
                LOG_DEBUG("module.ahbot.filters", "AuctionHouseBot: Item {} disabled (Quest Item)", itemTemplate.ItemId);
                continue;
            }

            // Disable items with duration
            if (DisableDuration && itemTemplate.Duration)
            {
                LOG_DEBUG("module.ahbot.filters", "AuctionHouseBot: Item {} disabled (Has a Duration)", itemTemplate.ItemId);
                continue;
            }

            // Disable items which are BOP or Quest Items and have a required level lower than the item level
            if (DisableBOP_Or_Quest_NoReqLevel && ((itemTemplate.Bonding == BIND_WHEN_PICKED_UP || itemTemplate.Bonding == BIND_QUEST_ITEM) && (itemTemplate.RequiredLevel < itemTemplate.ItemLevel)))
            {
                LOG_DEBUG("module.ahbot.filters", "AuctionHouseBot: Item {} disabled (BOP or BQI and Required Level is less than Item Level)", itemTemplate.ItemId);
                continue;
            }

            // Disable items specifically for Warrior
            if (DisableWarriorItems && itemTemplate.AllowableClass == 1)
            {
                LOG_DEBUG("module.ahbot.filters", "AuctionHouseBot: Item {} disabled (Warrior Item)", itemTemplate.ItemId);
                continue;
            }

            // Disable items specifically for Paladin
            if (DisablePaladinItems && itemTemplate.AllowableClass == 2)
            {
                LOG_DEBUG("module.ahbot.filters", "AuctionHouseBot: Item {} disabled (Paladin Item)", itemTemplate.ItemId);
                continue;
            }

            // Disable items specifically for Hunter
            if (DisableHunterItems && itemTemplate.AllowableClass == 4)
            {
                LOG_DEBUG("module.ahbot.filters", "AuctionHouseBot: Item {} disabled (Hunter Item)", itemTemplate.ItemId);
                continue;
            }

            // Disable items specifically for Rogue
            if (DisableRogueItems && itemTemplate.AllowableClass == 8)
            {
                LOG_DEBUG("module.ahbot.filters", "AuctionHouseBot: Item {} disabled (Rogue Item)", itemTemplate.ItemId);
                continue;
            }

            // Disable items specifically for Priest
            if (DisablePriestItems && itemTemplate.AllowableClass == 16)
            {
                LOG_DEBUG("module.ahbot.filters", "AuctionHouseBot: Item {} disabled (Priest Item)", itemTemplate.ItemId);
                continue;
            }

            // Disable items specifically for DK
            if (DisableDKItems && itemTemplate.AllowableClass == 32)
            {
                LOG_DEBUG("module.ahbot.filters", "AuctionHouseBot: Item {} disabled (DK Item)", itemTemplate.ItemId);
                continue;
            }

            // Disable items specifically for Shaman
            if (DisableShamanItems && itemTemplate.AllowableClass == 64)
            {
                LOG_DEBUG("module.ahbot.filters", "AuctionHouseBot: Item {} disabled (Shaman Item)", itemTemplate.ItemId);
                continue;
            }

            // Disable items specifically for Mage
            if (DisableMageItems && itemTemplate.AllowableClass == 128)
            {
                LOG_DEBUG("module.ahbot.filters", "AuctionHouseBot: Item {} disabled (Mage Item)", itemTemplate.ItemId);
                continue;
            }

            // Disable items specifically for Warlock
            if (DisableWarlockItems && itemTemplate.AllowableClass == 256)
            {
                LOG_DEBUG("module.ahbot.filters", "AuctionHouseBot: Item {} disabled (Warlock Item)", itemTemplate.ItemId);
                continue;
            }

            // Disable items specifically for Unused Class
            if (DisableUnusedClassItems && itemTemplate.AllowableClass == 512)
            {
                LOG_DEBUG("module.ahbot.filters", "AuctionHouseBot: Item {} disabled (Unused Item)", itemTemplate.ItemId);
                continue;
            }

            // Disable items specifically for Druid
            if (DisableDruidItems && itemTemplate.AllowableClass == 1024)
            {
                LOG_DEBUG("module.ahbot.filters", "AuctionHouseBot: Item {} disabled (Druid Item)", itemTemplate.ItemId);
                continue;
            }

            // Disable Items below level X
            if (DisableItemsBelowLevel && itemTemplate.Class != ITEM_CLASS_TRADE_GOODS && itemTemplate.ItemLevel < DisableItemsBelowLevel)
            {
                LOG_DEBUG("module.ahbot.filters", "AuctionHouseBot: Item {} disabled (Item Level = {})", itemTemplate.ItemId, itemTemplate.ItemLevel);
                continue;
            }

            // Disable Items above level X
            if (DisableItemsAboveLevel && itemTemplate.Class != ITEM_CLASS_TRADE_GOODS && itemTemplate.ItemLevel > DisableItemsAboveLevel)
            {
                LOG_DEBUG("module.ahbot.filters", "AuctionHouseBot: Item {} disabled (Item Level = {})", itemTemplate.ItemId, itemTemplate.ItemLevel);
                continue;
            }

            // Disable Trade Goods below level X
            if (DisableTGsBelowLevel && itemTemplate.Class == ITEM_CLASS_TRADE_GOODS && itemTemplate.ItemLevel < DisableTGsBelowLevel)
            {
                LOG_DEBUG("module.ahbot.filters", "AuctionHouseBot: Trade Good {} disabled (Trade Good Level = {})", itemTemplate.ItemId, itemTemplate.ItemLevel);
                continue;
            }

            // Disable Trade Goods above level X
            if (DisableTGsAboveLevel && itemTemplate.Class == ITEM_CLASS_TRADE_GOODS && itemTemplate.ItemLevel > DisableTGsAboveLevel)
            {
                LOG_DEBUG("module.ahbot.filters", "AuctionHouseBot: Trade Good {} disabled (Trade Good Level = {})", itemTemplate.ItemId, itemTemplate.ItemLevel);
                continue;
            }

            // Disable Items below GUID X
            if (DisableItemsBelowGUID && itemTemplate.Class != ITEM_CLASS_TRADE_GOODS && itemTemplate.ItemId < DisableItemsBelowGUID)
            {
                LOG_DEBUG("module.ahbot.filters", "AuctionHouseBot: Item {} disabled (Item Level = {})", itemTemplate.ItemId, itemTemplate.ItemLevel);
                continue;
            }

            // Disable Items above GUID X
            if (DisableItemsAboveGUID && itemTemplate.Class != ITEM_CLASS_TRADE_GOODS && itemTemplate.ItemId > DisableItemsAboveGUID)
            {
                LOG_DEBUG("module.ahbot.filters", "AuctionHouseBot: Item {} disabled (Item Level = {})", itemTemplate.ItemId, itemTemplate.ItemLevel);
                continue;
            }

            // Disable Trade Goods below GUID X
            if (DisableTGsBelowGUID && itemTemplate.Class == ITEM_CLASS_TRADE_GOODS && itemTemplate.ItemId < DisableTGsBelowGUID)
            {
                LOG_DEBUG("module.ahbot.filters", "AuctionHouseBot: Item {} disabled (Trade Good Level = {})", itemTemplate.ItemId, itemTemplate.ItemLevel);
                continue;
            }

            // Disable Trade Goods above GUID X
            if (DisableTGsAboveGUID && itemTemplate.Class == ITEM_CLASS_TRADE_GOODS && itemTemplate.ItemId > DisableTGsAboveGUID)
            {
                LOG_DEBUG("module.ahbot.filters", "AuctionHouseBot: Item {} disabled (Trade Good Level = {})", itemTemplate.ItemId, itemTemplate.ItemLevel);
                continue;
            }

            // Disable Items for level lower than X
            if (DisableItemsBelowReqLevel && itemTemplate.RequiredLevel < DisableItemsBelowReqLevel)
            {
                LOG_DEBUG("module.ahbot.filters", "AuctionHouseBot: Item {} disabled (RequiredLevel = {})", itemTemplate.ItemId, itemTemplate.RequiredLevel);
                continue;
            }

            // Disable Items for level higher than X
            if (DisableItemsAboveReqLevel && itemTemplate.RequiredLevel > DisableItemsAboveReqLevel)
            {
                LOG_DEBUG("module.ahbot.filters", "AuctionHouseBot: Item {} disabled (RequiredLevel = {})", itemTemplate.ItemId, itemTemplate.RequiredLevel);
                continue;
            }

            // Disable Trade Goods for level lower than X
            if (DisableTGsBelowReqLevel && itemTemplate.RequiredLevel < DisableTGsBelowReqLevel)
            {
                LOG_DEBUG("module.ahbot.filters", "AuctionHouseBot: Trade Good {} disabled (RequiredLevel = {})", itemTemplate.ItemId, itemTemplate.RequiredLevel);
                continue;
            }

            // Disable Trade Goods for level higher than X
            if (DisableTGsAboveReqLevel && itemTemplate.RequiredLevel > DisableTGsAboveReqLevel)
            {
                LOG_DEBUG("module.ahbot.filters", "AuctionHouseBot: Trade Good {} disabled (RequiredLevel = {})", itemTemplate.ItemId, itemTemplate.RequiredLevel);
                continue;
            }

            // Disable Items that require skill lower than X
            if (DisableItemsBelowReqSkillRank && itemTemplate.RequiredSkillRank < DisableItemsBelowReqSkillRank)
            {
                LOG_DEBUG("module.ahbot.filters", "AuctionHouseBot: Item {} disabled (RequiredSkillRank = {})", itemTemplate.ItemId, itemTemplate.RequiredSkillRank);
                continue;
            }

            // Disable Items that require skill higher than X
            if (DisableItemsAboveReqSkillRank && itemTemplate.RequiredSkillRank > DisableItemsAboveReqSkillRank)
            {
                LOG_DEBUG("module.ahbot.filters", "AuctionHouseBot: Item {} disabled (RequiredSkillRank = {})", itemTemplate.ItemId, itemTemplate.RequiredSkillRank);
                continue;
            }

            // Disable Trade Goods that require skill lower than X
            if (DisableTGsBelowReqSkillRank && itemTemplate.RequiredSkillRank < DisableTGsBelowReqSkillRank)
            {
                LOG_DEBUG("module.ahbot.filters", "AuctionHouseBot: Item {} disabled (RequiredSkillRank = {})", itemTemplate.ItemId, itemTemplate.RequiredSkillRank);
                continue;
            }

            // Disable Trade Goods that require skill higher than X
            if (DisableTGsAboveReqSkillRank && itemTemplate.RequiredSkillRank > DisableTGsAboveReqSkillRank)
            {
                LOG_DEBUG("module.ahbot.filters", "AuctionHouseBot: Item {} disabled (RequiredSkillRank = {})", itemTemplate.ItemId, itemTemplate.RequiredSkillRank);
                continue;
            }

            uint32 itemQualityIndexStart = itemTemplate.Class == ITEM_CLASS_TRADE_GOODS ? 0 : AHB_DEFAULT_QUALITY_SIZE;
            _itemsBin[itemQualityIndexStart + itemTemplate.Quality].emplace_back(itemTemplate.ItemId);
        }

        std::size_t totalItems = 0;
        for (auto const& itr : _itemsBin)
            totalItems += itr.size();

        if (!totalItems)
        {
            LOG_ERROR("module.ahbot", "AuctionHouseBot: No items");
            AHBSeller = 0;
        }

        LOG_INFO("module.ahbot", "AuctionHouseBot:");
        LOG_INFO("module.ahbot", "{} disabled items", DisableItemStore.size());
        LOG_INFO("module.ahbot", "Loaded {} grey trade goods",  _itemsBin[ITEM_QUALITY_POOR].size());
        LOG_INFO("module.ahbot", "Loaded {} white trade goods", _itemsBin[ITEM_QUALITY_NORMAL].size());
        LOG_INFO("module.ahbot", "Loaded {} green trade goods", _itemsBin[ITEM_QUALITY_UNCOMMON].size());
        LOG_INFO("module.ahbot", "Loaded {} blue trade goods",  _itemsBin[ITEM_QUALITY_RARE].size());
        LOG_INFO("module.ahbot", "Loaded {} purple trade goods", _itemsBin[ITEM_QUALITY_EPIC].size());
        LOG_INFO("module.ahbot", "Loaded {} orange trade goods", _itemsBin[ITEM_QUALITY_LEGENDARY].size());
        LOG_INFO("module.ahbot", "Loaded {} yellow trade goods", _itemsBin[ITEM_QUALITY_ARTIFACT].size());
        LOG_INFO("module.ahbot", "Loaded {} grey items", _itemsBin[AHB_ITEM_QUALITY_POOR].size());
        LOG_INFO("module.ahbot", "Loaded {} white items", _itemsBin[AHB_ITEM_QUALITY_NORMAL].size());
        LOG_INFO("module.ahbot", "Loaded {} green items", _itemsBin[AHB_ITEM_QUALITY_UNCOMMON].size());
        LOG_INFO("module.ahbot", "Loaded {} blue items", _itemsBin[AHB_ITEM_QUALITY_RARE].size());
        LOG_INFO("module.ahbot", "Loaded {} purple items", _itemsBin[AHB_ITEM_QUALITY_EPIC].size());
        LOG_INFO("module.ahbot", "Loaded {} orange items", _itemsBin[AHB_ITEM_QUALITY_LEGENDARY].size());
        LOG_INFO("module.ahbot", "Loaded {} yellow items", _itemsBin[AHB_ITEM_QUALITY_ARTIFACT].size());
    }

    LOG_INFO("module", "AuctionHouseBot and AuctionHouseBuyer have been loaded.");
}

void AuctionHouseBot::InitializeConfiguration()
{
    AHBSeller = sConfigMgr->GetOption<bool>("AuctionHouseBot.EnableSeller", false);
    AHBBuyer = sConfigMgr->GetOption<bool>("AuctionHouseBot.EnableBuyer", false);
    SellMethod = sConfigMgr->GetOption<bool>("AuctionHouseBot.UseBuyPriceForSeller", false);
    BuyMethod = sConfigMgr->GetOption<bool>("AuctionHouseBot.UseBuyPriceForBuyer", false);

    AHBplayerAccount = sConfigMgr->GetOption<uint32>("AuctionHouseBot.Account", 0);
    AHBplayerGUID = sConfigMgr->GetOption<uint32>("AuctionHouseBot.GUID", 0);
    ItemsPerCycle = sConfigMgr->GetOption<uint32>("AuctionHouseBot.ItemsPerCycle", 200);

    // Begin Filters

    Vendor_Items = sConfigMgr->GetOption<bool>("AuctionHouseBot.VendorItems", false);
    Loot_Items = sConfigMgr->GetOption<bool>("AuctionHouseBot.LootItems", true);
    Other_Items = sConfigMgr->GetOption<bool>("AuctionHouseBot.OtherItems", false);
    Vendor_TGs = sConfigMgr->GetOption<bool>("AuctionHouseBot.VendorTradeGoods", false);
    Loot_TGs = sConfigMgr->GetOption<bool>("AuctionHouseBot.LootTradeGoods", true);
    Other_TGs = sConfigMgr->GetOption<bool>("AuctionHouseBot.OtherTradeGoods", false);

    No_Bind = sConfigMgr->GetOption<bool>("AuctionHouseBot.No_Bind", true);
    Bind_When_Picked_Up = sConfigMgr->GetOption<bool>("AuctionHouseBot.Bind_When_Picked_Up", false);
    Bind_When_Equipped = sConfigMgr->GetOption<bool>("AuctionHouseBot.Bind_When_Equipped", true);
    Bind_When_Use = sConfigMgr->GetOption<bool>("AuctionHouseBot.Bind_When_Use", true);
    Bind_Quest_Item = sConfigMgr->GetOption<bool>("AuctionHouseBot.Bind_Quest_Item", false);

    DisablePermEnchant = sConfigMgr->GetOption<bool>("AuctionHouseBot.DisablePermEnchant", false);
    DisableConjured = sConfigMgr->GetOption<bool>("AuctionHouseBot.DisableConjured", false);
    DisableGems = sConfigMgr->GetOption<bool>("AuctionHouseBot.DisableGems", false);
    DisableMoney = sConfigMgr->GetOption<bool>("AuctionHouseBot.DisableMoney", false);
    DisableMoneyLoot = sConfigMgr->GetOption<bool>("AuctionHouseBot.DisableMoneyLoot", false);
    DisableLootable = sConfigMgr->GetOption<bool>("AuctionHouseBot.DisableLootable", false);
    DisableKeys = sConfigMgr->GetOption<bool>("AuctionHouseBot.DisableKeys", false);
    DisableDuration = sConfigMgr->GetOption<bool>("AuctionHouseBot.DisableDuration", false);
    DisableBOP_Or_Quest_NoReqLevel = sConfigMgr->GetOption<bool>("AuctionHouseBot.DisableBOP_Or_Quest_NoReqLevel", false);

    DisableWarriorItems = sConfigMgr->GetOption<bool>("AuctionHouseBot.DisableWarriorItems", false);
    DisablePaladinItems = sConfigMgr->GetOption<bool>("AuctionHouseBot.DisablePaladinItems", false);
    DisableHunterItems = sConfigMgr->GetOption<bool>("AuctionHouseBot.DisableHunterItems", false);
    DisableRogueItems = sConfigMgr->GetOption<bool>("AuctionHouseBot.DisableRogueItems", false);
    DisablePriestItems = sConfigMgr->GetOption<bool>("AuctionHouseBot.DisablePriestItems", false);
    DisableDKItems = sConfigMgr->GetOption<bool>("AuctionHouseBot.DisableDKItems", false);
    DisableShamanItems = sConfigMgr->GetOption<bool>("AuctionHouseBot.DisableShamanItems", false);
    DisableMageItems = sConfigMgr->GetOption<bool>("AuctionHouseBot.DisableMageItems", false);
    DisableWarlockItems = sConfigMgr->GetOption<bool>("AuctionHouseBot.DisableWarlockItems", false);
    DisableUnusedClassItems = sConfigMgr->GetOption<bool>("AuctionHouseBot.DisableUnusedClassItems", false);
    DisableDruidItems = sConfigMgr->GetOption<bool>("AuctionHouseBot.DisableDruidItems", false);

    DisableItemsBelowLevel = sConfigMgr->GetOption<uint32>("AuctionHouseBot.DisableItemsBelowLevel", 0);
    DisableItemsAboveLevel = sConfigMgr->GetOption<uint32>("AuctionHouseBot.DisableItemsAboveLevel", 0);
    DisableTGsBelowLevel = sConfigMgr->GetOption<uint32>("AuctionHouseBot.DisableTGsBelowLevel", 0);
    DisableTGsAboveLevel = sConfigMgr->GetOption<uint32>("AuctionHouseBot.DisableTGsAboveLevel", 0);
    DisableItemsBelowGUID = sConfigMgr->GetOption<uint32>("AuctionHouseBot.DisableItemsBelowGUID", 0);
    DisableItemsAboveGUID = sConfigMgr->GetOption<uint32>("AuctionHouseBot.DisableItemsAboveGUID", 0);
    DisableTGsBelowGUID = sConfigMgr->GetOption<uint32>("AuctionHouseBot.DisableTGsBelowGUID", 0);
    DisableTGsAboveGUID = sConfigMgr->GetOption<uint32>("AuctionHouseBot.DisableTGsAboveGUID", 0);
    DisableItemsBelowReqLevel = sConfigMgr->GetOption<uint32>("AuctionHouseBot.DisableItemsBelowReqLevel", 0);
    DisableItemsAboveReqLevel = sConfigMgr->GetOption<uint32>("AuctionHouseBot.DisableItemsAboveReqLevel", 0);
    DisableTGsBelowReqLevel = sConfigMgr->GetOption<uint32>("AuctionHouseBot.DisableTGsBelowReqLevel", 0);
    DisableTGsAboveReqLevel = sConfigMgr->GetOption<uint32>("AuctionHouseBot.DisableTGsAboveReqLevel", 0);
    DisableItemsBelowReqSkillRank = sConfigMgr->GetOption<uint32>("AuctionHouseBot.DisableItemsBelowReqSkillRank", 0);
    DisableItemsAboveReqSkillRank = sConfigMgr->GetOption<uint32>("AuctionHouseBot.DisableItemsAboveReqSkillRank", 0);
    DisableTGsBelowReqSkillRank = sConfigMgr->GetOption<uint32>("AuctionHouseBot.DisableTGsBelowReqSkillRank", 0);
    DisableTGsAboveReqSkillRank = sConfigMgr->GetOption<uint32>("AuctionHouseBot.DisableTGsAboveReqSkillRank", 0);
}

void AuctionHouseBot::IncrementItemCounts(AuctionEntry* ah)
{
    // get exact item information
    Item *pItem =  sAuctionMgr->GetAItem(ah->item_guid);
    if (!pItem)
    {
        LOG_ERROR("module.ahbot", "AHBot: Item {} doesn't exist, perhaps bought already?", ah->item_guid.ToString());
        return;
    }

    // get item prototype
    ItemTemplate const* prototype = sObjectMgr->GetItemTemplate(ah->item_template);

    AHBConfig* config = nullptr;

    AuctionHouseEntry const* ahEntry = sAuctionHouseStore.LookupEntry(ah->GetHouseId());
    if (!ahEntry)
    {
        LOG_DEBUG("module.ahbot", "AHBot: {} returned as House Faction. Neutral", ah->GetHouseId());
        config = &NeutralConfig;
    }
    else if (ahEntry->houseId == AUCTIONHOUSE_ALLIANCE)
    {
        LOG_DEBUG("module.ahbot", "AHBot: {} returned as House Faction. Alliance", ah->GetHouseId());
        config = &AllianceConfig;
    }
    else if (ahEntry->houseId == AUCTIONHOUSE_HORDE)
    {
        LOG_DEBUG("module.ahbot", "AHBot: {} returned as House Faction. Horde", ah->GetHouseId());
        config = &HordeConfig;
    }
    else
    {
        LOG_DEBUG("module.ahbot", "AHBot: {} returned as House Faction. Neutral", ah->GetHouseId());
        config = &NeutralConfig;
    }

    config->IncreaseItemCounts(prototype->Class, prototype->Quality);
}

void AuctionHouseBot::DecrementItemCounts(AuctionEntry* ah, uint32 itemEntry)
{
    // get item prototype
    ItemTemplate const* prototype = sObjectMgr->GetItemTemplate(itemEntry);

    AHBConfig* config = nullptr;

    AuctionHouseEntry const* ahEntry = sAuctionHouseStore.LookupEntry(ah->GetHouseId());
    if (!ahEntry)
    {
        LOG_DEBUG("module.ahbot", "AHBot: {} returned as House Faction. Neutral", ah->GetHouseId());
        config = &NeutralConfig;
    }
    else if (ahEntry->houseId == AUCTIONHOUSE_ALLIANCE)
    {
        LOG_DEBUG("module.ahbot", "AHBot: {} returned as House Faction. Alliance", ah->GetHouseId());
        config = &AllianceConfig;
    }
    else if (ahEntry->houseId == AUCTIONHOUSE_HORDE)
    {
        LOG_DEBUG("module.ahbot", "AHBot: {} returned as House Faction. Horde", ah->GetHouseId());
        config = &HordeConfig;
    }
    else
    {
        LOG_DEBUG("module.ahbot", "AHBot: {} returned as House Faction. Neutral", ah->GetHouseId());
        config = &NeutralConfig;
    }

    config->DecreaseItemCounts(prototype->Class, prototype->Quality);
}

void AuctionHouseBot::Commands(uint32 command, uint32 ahMapID, uint32 col, char* args)
{
    AHBConfig* config = nullptr;
    switch (ahMapID)
    {
    case AUCTIONHOUSE_ALLIANCE:
        config = &AllianceConfig;
        break;
    case AUCTIONHOUSE_HORDE:
        config = &HordeConfig;
        break;
    case AUCTIONHOUSE_NEUTRAL:
        config = &NeutralConfig;
        break;
    }

    std::string color;
    switch (col)
    {
    case ITEM_QUALITY_POOR:
        color = "grey";
        break;
    case ITEM_QUALITY_NORMAL:
        color = "white";
        break;
    case ITEM_QUALITY_UNCOMMON:
        color = "green";
        break;
    case ITEM_QUALITY_RARE:
        color = "blue";
        break;
    case ITEM_QUALITY_EPIC:
        color = "purple";
        break;
    case ITEM_QUALITY_LEGENDARY:
        color = "orange";
        break;
    case ITEM_QUALITY_ARTIFACT:
        color = "yellow";
        break;
    default:
        break;
    }

    switch (command)
    {
    case 0:     //ahexpire
        {
            AuctionHouseObject* auctionHouse = sAuctionMgr->GetAuctionsMap(config->GetAuctionHouseFactionID());

            for (auto const& [__, auction] : auctionHouse->GetAuctions())
            {
                if (auction->owner.GetCounter() == AHBplayerGUID)
                {
                    auction->expire_time = GameTime::GetGameTime().count();
                    uint32 id = auction->Id;
                    uint32 expire_time = auction->expire_time;
                    CharacterDatabase.Execute("UPDATE auctionhouse SET time = '{}' WHERE id = '{}'", expire_time, id);
                }
            }
        }
        break;
    case 1:     //min items
        {
            char * param1 = strtok(args, " ");
            uint32 minItems = (uint32) strtoul(param1, NULL, 0);
            WorldDatabase.Execute("UPDATE mod_auctionhousebot SET minitems = '{}' WHERE auctionhouse = '{}'", minItems, ahMapID);
            config->SetMinItems(minItems);
        }
        break;
    case 2:     //max items
        {
            char * param1 = strtok(args, " ");
            uint32 maxItems = (uint32) strtoul(param1, NULL, 0);
			WorldDatabase.Execute("UPDATE mod_auctionhousebot SET maxitems = '{}' WHERE auctionhouse = '{}'", maxItems, ahMapID);
            config->SetMaxItems(maxItems);
            config->CalculatePercents();
        }
        break;
    case 3:     //min time Deprecated (Place holder for future commands)
        break;
    case 4:     //max time Deprecated (Place holder for future commands)
        break;
    case 5:     //percentages
        {
            char * param1 = strtok(args, " ");
            char * param2 = strtok(NULL, " ");
            char * param3 = strtok(NULL, " ");
            char * param4 = strtok(NULL, " ");
            char * param5 = strtok(NULL, " ");
            char * param6 = strtok(NULL, " ");
            char * param7 = strtok(NULL, " ");
            char * param8 = strtok(NULL, " ");
            char * param9 = strtok(NULL, " ");
            char * param10 = strtok(NULL, " ");
            char * param11 = strtok(NULL, " ");
            char * param12 = strtok(NULL, " ");
            char * param13 = strtok(NULL, " ");
            char * param14 = strtok(NULL, " ");
            uint32 greytg = (uint32) strtoul(param1, NULL, 0);
            uint32 whitetg = (uint32) strtoul(param2, NULL, 0);
            uint32 greentg = (uint32) strtoul(param3, NULL, 0);
            uint32 bluetg = (uint32) strtoul(param4, NULL, 0);
            uint32 purpletg = (uint32) strtoul(param5, NULL, 0);
            uint32 orangetg = (uint32) strtoul(param6, NULL, 0);
            uint32 yellowtg = (uint32) strtoul(param7, NULL, 0);
            uint32 greyi = (uint32) strtoul(param8, NULL, 0);
            uint32 whitei = (uint32) strtoul(param9, NULL, 0);
            uint32 greeni = (uint32) strtoul(param10, NULL, 0);
            uint32 bluei = (uint32) strtoul(param11, NULL, 0);
            uint32 purplei = (uint32) strtoul(param12, NULL, 0);
            uint32 orangei = (uint32) strtoul(param13, NULL, 0);
            uint32 yellowi = (uint32) strtoul(param14, NULL, 0);

            std::array<uint32, AHB_MAX_QUALITY> percentages =
            {
                greytg, whitetg, greentg, bluetg, purpletg, orangetg, yellowtg, greyi, whitei, greeni, bluei, purplei, orangei, yellowi
            };

			auto trans = WorldDatabase.BeginTransaction();
            trans->Append("UPDATE mod_auctionhousebot SET percentgreytradegoods = '{}' WHERE auctionhouse = '{}'", greytg, ahMapID);
            trans->Append("UPDATE mod_auctionhousebot SET percentwhitetradegoods = '{}' WHERE auctionhouse = '{}'", whitetg, ahMapID);
            trans->Append("UPDATE mod_auctionhousebot SET percentgreentradegoods = '{}' WHERE auctionhouse = '{}'", greentg, ahMapID);
            trans->Append("UPDATE mod_auctionhousebot SET percentbluetradegoods = '{}' WHERE auctionhouse = '{}'", bluetg, ahMapID);
            trans->Append("UPDATE mod_auctionhousebot SET percentpurpletradegoods = '{}' WHERE auctionhouse = '{}'", purpletg, ahMapID);
            trans->Append("UPDATE mod_auctionhousebot SET percentorangetradegoods = '{}' WHERE auctionhouse = '{}'", orangetg, ahMapID);
            trans->Append("UPDATE mod_auctionhousebot SET percentyellowtradegoods = '{}' WHERE auctionhouse = '{}'", yellowtg, ahMapID);
            trans->Append("UPDATE mod_auctionhousebot SET percentgreyitems = '{}' WHERE auctionhouse = '{}'", greyi, ahMapID);
            trans->Append("UPDATE mod_auctionhousebot SET percentwhiteitems = '{}' WHERE auctionhouse = '{}'", whitei, ahMapID);
            trans->Append("UPDATE mod_auctionhousebot SET percentgreenitems = '{}' WHERE auctionhouse = '{}'", greeni, ahMapID);
            trans->Append("UPDATE mod_auctionhousebot SET percentblueitems = '{}' WHERE auctionhouse = '{}'", bluei, ahMapID);
            trans->Append("UPDATE mod_auctionhousebot SET percentpurpleitems = '{}' WHERE auctionhouse = '{}'", purplei, ahMapID);
            trans->Append("UPDATE mod_auctionhousebot SET percentorangeitems = '{}' WHERE auctionhouse = '{}'", orangei, ahMapID);
            trans->Append("UPDATE mod_auctionhousebot SET percentyellowitems = '{}' WHERE auctionhouse = '{}'", yellowi, ahMapID);
			WorldDatabase.CommitTransaction(trans);
            config->SetPercentages(percentages);
        }
        break;
    case 6:     //min prices
        {
            char * param1 = strtok(args, " ");
            uint32 minPrice = (uint32) strtoul(param1, NULL, 0);
			WorldDatabase.Execute("UPDATE mod_auctionhousebot SET minprice{} = '{}' WHERE auctionhouse = '{}'", color, minPrice, ahMapID);
            config->SetMinPrice(col, minPrice);
        }
        break;
    case 7:     //max prices
        {
            char * param1 = strtok(args, " ");
            uint32 maxPrice = (uint32) strtoul(param1, NULL, 0);
			WorldDatabase.Execute("UPDATE mod_auctionhousebot SET maxprice{} = '{}' WHERE auctionhouse = '{}'", color, maxPrice, ahMapID);
            config->SetMaxPrice(col, maxPrice);
        }
        break;
    case 8:     //min bid price
        {
            char * param1 = strtok(args, " ");
            uint32 minBidPrice = (uint32) strtoul(param1, NULL, 0);
			WorldDatabase.Execute("UPDATE mod_auctionhousebot SET minbidprice{} = '{}' WHERE auctionhouse = '{}'", color, minBidPrice, ahMapID);
            config->SetMinBidPrice(col, minBidPrice);
        }
        break;
    case 9:     //max bid price
        {
            char * param1 = strtok(args, " ");
            uint32 maxBidPrice = (uint32) strtoul(param1, NULL, 0);
			WorldDatabase.Execute("UPDATE mod_auctionhousebot SET maxbidprice{} = '{}' WHERE auctionhouse = '{}'", color, maxBidPrice, ahMapID);
            config->SetMaxBidPrice(col, maxBidPrice);
        }
        break;
    case 10:        //max stacks
        {
            char * param1 = strtok(args, " ");
            uint32 maxStack = (uint32) strtoul(param1, NULL, 0);
			WorldDatabase.Execute("UPDATE mod_auctionhousebot SET maxstack{} = '{}' WHERE auctionhouse = '{}'", color, maxStack, ahMapID);
            config->SetMaxStack(col, maxStack);
        }
        break;
    case 11:        //buyer bid prices
        {
            char * param1 = strtok(args, " ");
            uint32 buyerPrice = (uint32) strtoul(param1, NULL, 0);
			WorldDatabase.Execute("UPDATE mod_auctionhousebot SET buyerprice{} = '{}' WHERE auctionhouse = '{}'", color, buyerPrice, ahMapID);
            config->SetBuyerPrice(col, buyerPrice);
        }
        break;
    case 12:        //buyer bidding interval
        {
            char * param1 = strtok(args, " ");
            uint32 bidInterval = (uint32) strtoul(param1, NULL, 0);
			WorldDatabase.Execute("UPDATE mod_auctionhousebot SET buyerbiddinginterval = '{}' WHERE auctionhouse = '{}'", bidInterval, ahMapID);
            config->SetBiddingInterval(Minutes(bidInterval));
        }
        break;
    case 13:        //buyer bids per interval
        {
            char * param1 = strtok(args, " ");
            uint32 bidsPerInterval = (uint32) strtoul(param1, NULL, 0);
			WorldDatabase.Execute("UPDATE mod_auctionhousebot SET buyerbidsperinterval = '{}' WHERE auctionhouse = '{}'", bidsPerInterval, ahMapID);
            config->SetBidsPerInterval(bidsPerInterval);
        }
        break;
    default:
        break;
    }
}

void AuctionHouseBot::LoadValues(AHBConfig* config)
{
    LOG_DEBUG("module.ahbot", "Start Settings for Auctionhouses");

    if (AHBSeller)
    {
        std::string selectColumns = "minitems, maxitems,"; // min/max
        selectColumns.append("percentgreytradegoods, percentwhitetradegoods, percentgreentradegoods, percentbluetradegoods, percentpurpletradegoods, percentorangetradegoods, percentyellowtradegoods,"); // tg items
        selectColumns.append("percentgreyitems, percentwhiteitems, percentgreenitems, percentblueitems, percentpurpleitems, percentorangeitems, percentyellowitems,"); // default items
        selectColumns.append("minpricegrey, minpricewhite, minpricegreen, minpriceblue, minpricepurple, minpriceorange, minpriceyellow,"); // min price
        selectColumns.append("maxpricegrey, maxpricewhite, maxpricegreen, maxpriceblue, maxpricepurple, maxpriceorange, maxpriceyellow,"); // max price
        selectColumns.append("minbidpricegrey, minbidpricewhite, minbidpricegreen, minbidpriceblue, minbidpricepurple, minbidpriceorange, minbidpriceyellow,"); // min bid prices
        selectColumns.append("maxbidpricegrey, maxbidpricewhite, maxbidpricegreen, maxbidpriceblue, maxbidpricepurple, maxbidpriceorange, maxbidpriceyellow,"); // max bid prices
        selectColumns.append("maxstackgrey, maxstackwhite, maxstackgreen, maxstackblue, maxstackpurple, maxstackorange, maxstackyellow,"); // max bid prices
        selectColumns.append("name"); // auction name

        auto result = WorldDatabase.Query("SELECT {} FROM mod_auctionhousebot WHERE auctionhouse = {}", selectColumns, config->GetAuctionHouseID());
        if (!result)
        {
            LOG_ERROR("module.ahbot", "> Empty or invalid sql query for Auctionhouse: {}", config->GetAuctionHouseID());
            return;
        }

        auto const& [minitems, maxitems,
            percentgreytradegoods, percentwhitetradegoods, percentgreentradegoods, percentbluetradegoods, percentpurpletradegoods, percentorangetradegoods, percentyellowtradegoods,
            percentgreyitems, percentwhiteitems, percentgreenitems, percentblueitems, percentpurpleitems, percentorangeitems, percentyellowitems,
            minpricegrey, minpricewhite, minpricegreen, minpriceblue, minpricepurple, minpriceorange, minpriceyellow,
            maxpricegrey, maxpricewhite, maxpricegreen, maxpriceblue, maxpricepurple, maxpriceorange, maxpriceyellow,
            minbidpricegrey, minbidpricewhite, minbidpricegreen, minbidpriceblue, minbidpricepurple, minbidpriceorange, minbidpriceyellow,
            maxbidpricegrey, maxbidpricewhite, maxbidpricegreen, maxbidpriceblue, maxbidpricepurple, maxbidpriceorange, maxbidpriceyellow,
            maxstackgrey, maxstackwhite, maxstackgreen, maxstackblue, maxstackpurple, maxstackorange, maxstackyellow,
            auctionName]
            = result->FetchTuple<uint32, uint32,
            uint32, uint32, uint32, uint32, uint32, uint32, uint32,
            uint32, uint32, uint32, uint32, uint32, uint32, uint32,
            uint32, uint32, uint32, uint32, uint32, uint32, uint32,
            uint32, uint32, uint32, uint32, uint32, uint32, uint32,
            uint32, uint32, uint32, uint32, uint32, uint32, uint32,
            uint32, uint32, uint32, uint32, uint32, uint32, uint32,
            uint32, uint32, uint32, uint32, uint32, uint32, uint32,
            std::string_view>();

        // Load min and max items
		config->SetMinItems(minitems);
		config->SetMaxItems(maxitems);

        std::array<uint32, AHB_MAX_QUALITY> percetages = { percentgreytradegoods, percentwhitetradegoods, percentgreentradegoods, percentbluetradegoods, percentpurpletradegoods, percentorangetradegoods, percentyellowtradegoods,
            percentgreyitems, percentwhiteitems, percentgreenitems, percentblueitems, percentpurpleitems, percentorangeitems, percentyellowitems };

        config->SetPercentages(percetages);

        // Load min and max prices
		config->SetMinPrice(ITEM_QUALITY_POOR, minpricegrey);
		config->SetMaxPrice(ITEM_QUALITY_POOR, maxpricegrey);
        config->SetMinPrice(ITEM_QUALITY_NORMAL, minpricewhite);
		config->SetMaxPrice(ITEM_QUALITY_NORMAL, maxpricewhite);
		config->SetMinPrice(ITEM_QUALITY_UNCOMMON, minpricegreen);
		config->SetMaxPrice(ITEM_QUALITY_UNCOMMON, maxpricegreen);
		config->SetMinPrice(ITEM_QUALITY_RARE, minpriceblue);
		config->SetMaxPrice(ITEM_QUALITY_RARE, maxpriceblue);
		config->SetMinPrice(ITEM_QUALITY_EPIC, minpricepurple);
		config->SetMaxPrice(ITEM_QUALITY_EPIC, maxpricepurple);
		config->SetMinPrice(ITEM_QUALITY_LEGENDARY, minpriceorange);
		config->SetMaxPrice(ITEM_QUALITY_LEGENDARY, maxpriceorange);
		config->SetMinPrice(ITEM_QUALITY_ARTIFACT, minpriceyellow);
		config->SetMaxPrice(ITEM_QUALITY_ARTIFACT, maxpriceyellow);

        // Load min and max bid prices
		config->SetMinBidPrice(ITEM_QUALITY_POOR, minbidpricegrey);
		config->SetMaxBidPrice(ITEM_QUALITY_POOR, maxbidpricegrey);
		config->SetMinBidPrice(ITEM_QUALITY_NORMAL, minbidpricewhite);
		config->SetMaxBidPrice(ITEM_QUALITY_NORMAL, maxbidpricewhite);
		config->SetMinBidPrice(ITEM_QUALITY_UNCOMMON, minbidpricegreen);
		config->SetMaxBidPrice(ITEM_QUALITY_UNCOMMON, maxbidpricegreen);
		config->SetMinBidPrice(ITEM_QUALITY_RARE, minbidpriceblue);
		config->SetMaxBidPrice(ITEM_QUALITY_RARE, maxbidpriceblue);
		config->SetMinBidPrice(ITEM_QUALITY_EPIC, minbidpricepurple);
		config->SetMaxBidPrice(ITEM_QUALITY_EPIC, maxbidpricepurple);
		config->SetMinBidPrice(ITEM_QUALITY_LEGENDARY, minbidpriceorange);
		config->SetMaxBidPrice(ITEM_QUALITY_LEGENDARY, maxbidpriceorange);
		config->SetMinBidPrice(ITEM_QUALITY_ARTIFACT, minbidpriceyellow);
		config->SetMaxBidPrice(ITEM_QUALITY_ARTIFACT, maxbidpriceyellow);

        // Load max stacks
		config->SetMaxStack(ITEM_QUALITY_POOR, maxstackgrey);
		config->SetMaxStack(ITEM_QUALITY_NORMAL, maxstackwhite);
		config->SetMaxStack(ITEM_QUALITY_UNCOMMON, maxstackgreen);
		config->SetMaxStack(ITEM_QUALITY_RARE, maxstackblue);
		config->SetMaxStack(ITEM_QUALITY_EPIC, maxstackpurple);
		config->SetMaxStack(ITEM_QUALITY_LEGENDARY, maxstackorange);
		config->SetMaxStack(ITEM_QUALITY_ARTIFACT, maxstackyellow);

        LOG_DEBUG("module.ahbot", "minItems                = {}", config->GetMinItems());
        LOG_DEBUG("module.ahbot", "maxItems                = {}", config->GetMaxItems());
        LOG_DEBUG("module.ahbot", "percentGreyTradeGoods   = {}", config->GetPercentages(ITEM_QUALITY_POOR));
        LOG_DEBUG("module.ahbot", "percentWhiteTradeGoods  = {}", config->GetPercentages(ITEM_QUALITY_NORMAL));
        LOG_DEBUG("module.ahbot", "percentGreenTradeGoods  = {}", config->GetPercentages(ITEM_QUALITY_UNCOMMON));
        LOG_DEBUG("module.ahbot", "percentBlueTradeGoods   = {}", config->GetPercentages(ITEM_QUALITY_RARE));
        LOG_DEBUG("module.ahbot", "percentPurpleTradeGoods = {}", config->GetPercentages(ITEM_QUALITY_EPIC));
        LOG_DEBUG("module.ahbot", "percentOrangeTradeGoods = {}", config->GetPercentages(ITEM_QUALITY_LEGENDARY));
        LOG_DEBUG("module.ahbot", "percentYellowTradeGoods = {}", config->GetPercentages(ITEM_QUALITY_ARTIFACT));
        LOG_DEBUG("module.ahbot", "percentGreyItems        = {}", config->GetPercentages(AHB_ITEM_QUALITY_POOR));
        LOG_DEBUG("module.ahbot", "percentWhiteItems       = {}", config->GetPercentages(AHB_ITEM_QUALITY_NORMAL));
        LOG_DEBUG("module.ahbot", "percentGreenItems       = {}", config->GetPercentages(AHB_ITEM_QUALITY_UNCOMMON));
        LOG_DEBUG("module.ahbot", "percentBlueItems        = {}", config->GetPercentages(AHB_ITEM_QUALITY_RARE));
        LOG_DEBUG("module.ahbot", "percentPurpleItems      = {}", config->GetPercentages(AHB_ITEM_QUALITY_EPIC));
        LOG_DEBUG("module.ahbot", "percentOrangeItems      = {}", config->GetPercentages(AHB_ITEM_QUALITY_LEGENDARY));
        LOG_DEBUG("module.ahbot", "percentYellowItems      = {}", config->GetPercentages(AHB_ITEM_QUALITY_ARTIFACT));
        LOG_DEBUG("module.ahbot", "minPriceGrey            = {}", config->GetMinPrice(ITEM_QUALITY_POOR));
        LOG_DEBUG("module.ahbot", "maxPriceGrey            = {}", config->GetMaxPrice(ITEM_QUALITY_POOR));
        LOG_DEBUG("module.ahbot", "minPriceWhite           = {}", config->GetMinPrice(ITEM_QUALITY_NORMAL));
        LOG_DEBUG("module.ahbot", "maxPriceWhite           = {}", config->GetMaxPrice(ITEM_QUALITY_NORMAL));
        LOG_DEBUG("module.ahbot", "minPriceGreen           = {}", config->GetMinPrice(ITEM_QUALITY_UNCOMMON));
        LOG_DEBUG("module.ahbot", "maxPriceGreen           = {}", config->GetMaxPrice(ITEM_QUALITY_UNCOMMON));
        LOG_DEBUG("module.ahbot", "minPriceBlue            = {}", config->GetMinPrice(ITEM_QUALITY_RARE));
        LOG_DEBUG("module.ahbot", "maxPriceBlue            = {}", config->GetMaxPrice(ITEM_QUALITY_RARE));
        LOG_DEBUG("module.ahbot", "minPricePurple          = {}", config->GetMinPrice(ITEM_QUALITY_EPIC));
        LOG_DEBUG("module.ahbot", "maxPricePurple          = {}", config->GetMaxPrice(ITEM_QUALITY_EPIC));
        LOG_DEBUG("module.ahbot", "minPriceOrange          = {}", config->GetMinPrice(ITEM_QUALITY_LEGENDARY));
        LOG_DEBUG("module.ahbot", "maxPriceOrange          = {}", config->GetMaxPrice(ITEM_QUALITY_LEGENDARY));
        LOG_DEBUG("module.ahbot", "minPriceYellow          = {}", config->GetMinPrice(ITEM_QUALITY_ARTIFACT));
        LOG_DEBUG("module.ahbot", "maxPriceYellow          = {}", config->GetMaxPrice(ITEM_QUALITY_ARTIFACT));
        LOG_DEBUG("module.ahbot", "minBidPriceGrey         = {}", config->GetMinBidPrice(ITEM_QUALITY_POOR));
        LOG_DEBUG("module.ahbot", "maxBidPriceGrey         = {}", config->GetMaxBidPrice(ITEM_QUALITY_POOR));
        LOG_DEBUG("module.ahbot", "minBidPriceWhite        = {}", config->GetMinBidPrice(ITEM_QUALITY_NORMAL));
        LOG_DEBUG("module.ahbot", "maxBidPriceWhite        = {}", config->GetMaxBidPrice(ITEM_QUALITY_NORMAL));
        LOG_DEBUG("module.ahbot", "minBidPriceGreen        = {}", config->GetMinBidPrice(ITEM_QUALITY_UNCOMMON));
        LOG_DEBUG("module.ahbot", "maxBidPriceGreen        = {}", config->GetMaxBidPrice(ITEM_QUALITY_UNCOMMON));
        LOG_DEBUG("module.ahbot", "minBidPriceBlue         = {}", config->GetMinBidPrice(ITEM_QUALITY_RARE));
        LOG_DEBUG("module.ahbot", "maxBidPriceBlue         = {}", config->GetMinBidPrice(ITEM_QUALITY_RARE));
        LOG_DEBUG("module.ahbot", "minBidPricePurple       = {}", config->GetMinBidPrice(ITEM_QUALITY_EPIC));
        LOG_DEBUG("module.ahbot", "maxBidPricePurple       = {}", config->GetMaxBidPrice(ITEM_QUALITY_EPIC));
        LOG_DEBUG("module.ahbot", "minBidPriceOrange       = {}", config->GetMinBidPrice(ITEM_QUALITY_LEGENDARY));
        LOG_DEBUG("module.ahbot", "maxBidPriceOrange       = {}", config->GetMaxBidPrice(ITEM_QUALITY_LEGENDARY));
        LOG_DEBUG("module.ahbot", "minBidPriceYellow       = {}", config->GetMinBidPrice(ITEM_QUALITY_ARTIFACT));
        LOG_DEBUG("module.ahbot", "maxBidPriceYellow       = {}", config->GetMaxBidPrice(ITEM_QUALITY_ARTIFACT));
        LOG_DEBUG("module.ahbot", "maxStackGrey            = {}", config->GetMaxStack(ITEM_QUALITY_POOR));
        LOG_DEBUG("module.ahbot", "maxStackWhite           = {}", config->GetMaxStack(ITEM_QUALITY_NORMAL));
        LOG_DEBUG("module.ahbot", "maxStackGreen           = {}", config->GetMaxStack(ITEM_QUALITY_UNCOMMON));
        LOG_DEBUG("module.ahbot", "maxStackBlue            = {}", config->GetMaxStack(ITEM_QUALITY_RARE));
        LOG_DEBUG("module.ahbot", "maxStackPurple          = {}", config->GetMaxStack(ITEM_QUALITY_EPIC));
        LOG_DEBUG("module.ahbot", "maxStackOrange          = {}", config->GetMaxStack(ITEM_QUALITY_LEGENDARY));
        LOG_DEBUG("module.ahbot", "maxStackYellow          = {}", config->GetMaxStack(ITEM_QUALITY_ARTIFACT));

        AuctionHouseObject* auctionHouse = sAuctionMgr->GetAuctionsMap(config->GetAuctionHouseFactionID());

        config->ResetItemCounts();
        uint32 auctions = auctionHouse->Getcount();

        if (auctions)
        {
            for (auto const& [__, auction] : auctionHouse->GetAuctions())
            {
				Item* item = sAuctionMgr->GetAItem(auction->item_guid);
                if (!item)
                    continue;

                ItemTemplate const* prototype = item->GetTemplate();
                if (!prototype)
                    continue;

                switch (prototype->Quality)
                {
                case 0:
                    if (prototype->Class == ITEM_CLASS_TRADE_GOODS)
                        config->IncreaseItemCounts(ITEM_QUALITY_POOR);
                    else
                        config->IncreaseItemCounts(AHB_ITEM_QUALITY_POOR);
                    break;
                case 1:
                    if (prototype->Class == ITEM_CLASS_TRADE_GOODS)
                        config->IncreaseItemCounts(ITEM_QUALITY_NORMAL);
                    else
                        config->IncreaseItemCounts(AHB_ITEM_QUALITY_NORMAL);
                    break;
                case 2:
                    if (prototype->Class == ITEM_CLASS_TRADE_GOODS)
                        config->IncreaseItemCounts(ITEM_QUALITY_UNCOMMON);
                    else
                        config->IncreaseItemCounts(AHB_ITEM_QUALITY_UNCOMMON);
                    break;
                case 3:
                    if (prototype->Class == ITEM_CLASS_TRADE_GOODS)
                        config->IncreaseItemCounts(ITEM_QUALITY_RARE);
                    else
                        config->IncreaseItemCounts(AHB_ITEM_QUALITY_RARE);
                    break;
                case 4:
                    if (prototype->Class == ITEM_CLASS_TRADE_GOODS)
                        config->IncreaseItemCounts(ITEM_QUALITY_EPIC);
                    else
                        config->IncreaseItemCounts(AHB_ITEM_QUALITY_EPIC);
                    break;
                case 5:
                    if (prototype->Class == ITEM_CLASS_TRADE_GOODS)
                        config->IncreaseItemCounts(ITEM_QUALITY_LEGENDARY);
                    else
                        config->IncreaseItemCounts(AHB_ITEM_QUALITY_LEGENDARY);
                    break;
                case 6:
                    if (prototype->Class == ITEM_CLASS_TRADE_GOODS)
                        config->IncreaseItemCounts(ITEM_QUALITY_ARTIFACT);
                    else
                        config->IncreaseItemCounts(AHB_ITEM_QUALITY_ARTIFACT);
                    break;
                }
            }
        }

        LOG_DEBUG("module.ahbot", "Current Settings for {} Auctionhouses:", auctionName);
        LOG_DEBUG("module.ahbot", "Grey Trade Goods\t{}\tGrey Items\t{}", config->GetItemCounts(ITEM_QUALITY_POOR), config->GetItemCounts(AHB_ITEM_QUALITY_POOR));
        LOG_DEBUG("module.ahbot", "White Trade Goods\t{}\tWhite Items\t{}", config->GetItemCounts(ITEM_QUALITY_NORMAL), config->GetItemCounts(AHB_ITEM_QUALITY_NORMAL));
        LOG_DEBUG("module.ahbot", "Green Trade Goods\t{}\tGreen Items\t{}", config->GetItemCounts(ITEM_QUALITY_UNCOMMON), config->GetItemCounts(AHB_ITEM_QUALITY_UNCOMMON));
        LOG_DEBUG("module.ahbot", "Blue Trade Goods\t{}\tBlue Items\t{}", config->GetItemCounts(ITEM_QUALITY_RARE), config->GetItemCounts(AHB_ITEM_QUALITY_RARE));
        LOG_DEBUG("module.ahbot", "Purple Trade Goods\t{}\tPurple Items\t{}", config->GetItemCounts(ITEM_QUALITY_EPIC), config->GetItemCounts(AHB_ITEM_QUALITY_EPIC));
        LOG_DEBUG("module.ahbot", "Orange Trade Goods\t{}\tOrange Items\t{}", config->GetItemCounts(ITEM_QUALITY_LEGENDARY), config->GetItemCounts(AHB_ITEM_QUALITY_LEGENDARY));
        LOG_DEBUG("module.ahbot", "Yellow Trade Goods\t{}\tYellow Items\t{}", config->GetItemCounts(ITEM_QUALITY_ARTIFACT), config->GetItemCounts(AHB_ITEM_QUALITY_ARTIFACT));
    }

    if (AHBBuyer)
    {
        auto result = WorldDatabase.Query("SELECT buyerpricegrey, buyerpricewhite, buyerpricegreen, buyerpriceblue, buyerpricepurple, buyerpriceorange, buyerpriceyellow, buyerbiddinginterval, buyerbidsperinterval "
            "FROM mod_auctionhousebot WHERE auctionhouse = {}", config->GetAuctionHouseID());

        if (!result)
        {
            LOG_ERROR("module.ahbot", "> Empty or invalid sql query for Auctionhouse: {}", config->GetAuctionHouseID());
            return;
        }

        auto const& [buyerpricegrey, buyerpricewhite, buyerpricegreen, buyerpriceblue, buyerpricepurple, buyerpriceorange, buyerpriceyellow,
            buyerbiddinginterval, buyerbidsperinterval]
            = result->FetchTuple<uint32, uint32, uint32, uint32, uint32, uint32, uint32, uint32, uint32>();

        // Load buyer bid prices
		config->SetBuyerPrice(ITEM_QUALITY_POOR, buyerpricegrey);
		config->SetBuyerPrice(ITEM_QUALITY_NORMAL, buyerpricewhite);
		config->SetBuyerPrice(ITEM_QUALITY_UNCOMMON, buyerpricegreen);
		config->SetBuyerPrice(ITEM_QUALITY_RARE, buyerpriceblue);
		config->SetBuyerPrice(ITEM_QUALITY_EPIC, buyerpricepurple);
		config->SetBuyerPrice(ITEM_QUALITY_LEGENDARY, buyerpriceorange);
		config->SetBuyerPrice(ITEM_QUALITY_ARTIFACT, buyerpriceyellow);

        // Load bidding interval
		config->SetBiddingInterval(Minutes(buyerbiddinginterval));

        // Load bids per interval
		config->SetBidsPerInterval(buyerbidsperinterval);

        LOG_DEBUG("module.ahbot", "buyerPriceGrey          = {}", config->GetBuyerPrice(ITEM_QUALITY_POOR));
        LOG_DEBUG("module.ahbot", "buyerPriceWhite         = {}", config->GetBuyerPrice(ITEM_QUALITY_NORMAL));
        LOG_DEBUG("module.ahbot", "buyerPriceGreen         = {}", config->GetBuyerPrice(ITEM_QUALITY_UNCOMMON));
        LOG_DEBUG("module.ahbot", "buyerPriceBlue          = {}", config->GetBuyerPrice(ITEM_QUALITY_RARE));
        LOG_DEBUG("module.ahbot", "buyerPricePurple        = {}", config->GetBuyerPrice(ITEM_QUALITY_EPIC));
        LOG_DEBUG("module.ahbot", "buyerPriceOrange        = {}", config->GetBuyerPrice(ITEM_QUALITY_LEGENDARY));
        LOG_DEBUG("module.ahbot", "buyerPriceYellow        = {}", config->GetBuyerPrice(ITEM_QUALITY_ARTIFACT));
        LOG_DEBUG("module.ahbot", "buyerBiddingInterval    = {}", config->GetBiddingInterval().count());
        LOG_DEBUG("module.ahbot", "buyerBidsPerInterval    = {}", config->GetBidsPerInterval());
    }

    LOG_DEBUG("module.ahbot", "End Settings for Auctionhouses");
}

void AuctionHouseBot::ProcessQueryCallbacks()
{
    _queryProcessor.ProcessReadyCallbacks();
}
