/*
 * Copyright (c) 2026 DefiShares contributors.
 *
 * The MIT License
 */
#pragma once

#include <graphene/chain/types.hpp>
#include <graphene/db/simple_index.hpp>

namespace graphene { namespace chain {

/**
 * Protocol-owned GOLD treasury and its DFS reserve commitment.
 *
 * This is an implementation object deliberately not associated with an
 * account. It has no owner, authority, balance object, or private key.
 */
class gold_reserve_vault_object
   : public abstract_object< gold_reserve_vault_object, implementation_ids,
                             impl_gold_reserve_vault_object_type >
{
public:
   asset_id_type collateral_asset;
   asset_id_type debt_asset;

   share_type dfs_locked_collateral;
   share_type gold_debt;
   share_type gold_pool_balance;
   share_type gold_committed_rewards;

   share_type gold_spent_today;
   share_type gold_daily_spending_limit;

   uint16_t target_collateral_ratio = 2000;
   uint16_t minimum_collateral_ratio = 2000;
   uint32_t daily_spending_divisor = 2608;
   uint32_t last_feed_epoch = 0;
   uint32_t last_budget_day = 0;

   price last_rebalance_price;
   time_point_sec last_rebalance_time;

   bool enabled = false;
   bool issuance_paused = false;
   bool emergency_mode = false;
   uint16_t version = 1;
};

using gold_reserve_vault_index = simple_index< gold_reserve_vault_object >;

} } // graphene::chain

MAP_OBJECT_ID_TO_TYPE( graphene::chain::gold_reserve_vault_object )

FC_REFLECT_TYPENAME( graphene::chain::gold_reserve_vault_object )

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::chain::gold_reserve_vault_object )
