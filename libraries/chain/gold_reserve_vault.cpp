/*
 * Copyright (c) 2026 DefiShares contributors.
 *
 * The MIT License
 */
#include <graphene/chain/database.hpp>

#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/defishares_feed.hpp>
#include <graphene/chain/gold_reserve_vault_object.hpp>

#include <fc/uint128.hpp>

namespace graphene { namespace chain {

namespace {

uint32_t budget_day( fc::time_point_sec when )
{
   return static_cast<uint32_t>( when.sec_since_epoch() / fc::days( 1 ).to_seconds() );
}

} // namespace

const gold_reserve_vault_object* database::find_gold_reserve_vault()const
{
   return find( gold_reserve_vault_id_type() );
}

void database::initialize_gold_reserve_vault()
{
   if( find_gold_reserve_vault() )
      return;

   const asset_object& core = get_core_asset();
   const asset_object* gold = defishares::find_gold_asset( *this );
   if( !gold )
      return;

   const share_type collateral = core.reserved( *this );
   FC_ASSERT( collateral >= 0, "Core reserve cannot be negative" );

   create<gold_reserve_vault_object>( [this, &core, gold, collateral]( gold_reserve_vault_object& vault )
   {
      vault.collateral_asset = core.get_id();
      vault.debt_asset = gold->get_id();
      vault.dfs_locked_collateral = collateral;
      const uint32_t today = budget_day( head_block_time() );
      vault.last_budget_day = today == 0 ? 0 : today - 1;
      vault.enabled = true;
   } );

   // The hardfork creates the treasury from the feed already in consensus
   // state. Later issuances are only performed after the eight-hour update.
   rebalance_gold_reserve_vault();
}

void database::rebalance_gold_reserve_vault()
{
   const gold_reserve_vault_object* vault = find_gold_reserve_vault();
   if( !vault || !vault->enabled || vault->issuance_paused || vault->emergency_mode )
      return;

   const asset_object* gold = defishares::find_gold_asset( *this );
   if( !gold || gold->get_id() != vault->debt_asset )
      return;

   const asset_bitasset_data_object& bitasset = gold->bitasset_data( *this );
   const price& gold_per_dfs = bitasset.current_feed.settlement_price;
   if( gold_per_dfs.is_null()
       || gold_per_dfs.base.asset_id != gold->get_id()
       || gold_per_dfs.quote.asset_id != vault->collateral_asset )
      return;

   // The reserve is the currently unissued DFS supply. It can change between
   // feed updates, so the protocol collateral commitment must be refreshed at
   // every eight-hour rebalance rather than remaining a hardfork snapshot.
   const share_type current_reserve = get_core_asset().reserved( *this );
   const share_type max_debt = defishares::calculate_gold_debt_for_cr(
      current_reserve, gold_per_dfs, vault->target_collateral_ratio );

   if( max_debt < vault->gold_debt )
   {
      modify( *vault, [this, &gold_per_dfs, current_reserve]( gold_reserve_vault_object& mutable_vault )
      {
         mutable_vault.dfs_locked_collateral = current_reserve;
         mutable_vault.issuance_paused = true;
         mutable_vault.emergency_mode = true;
         mutable_vault.last_rebalance_price = gold_per_dfs;
         mutable_vault.last_rebalance_time = head_block_time();
         mutable_vault.last_feed_epoch = head_block_num();
      } );
      return;
   }

   const asset_dynamic_data_object& gold_dynamic = gold->dynamic_data( *this );
   const share_type headroom = gold->options.max_supply - gold_dynamic.current_supply;
   FC_ASSERT( headroom >= 0, "GOLD current supply exceeds max supply" );
   const share_type issue_amount = std::min( max_debt - vault->gold_debt, headroom );

   if( issue_amount > 0 )
   {
      modify( gold_dynamic, [issue_amount]( asset_dynamic_data_object& dynamic_data )
      {
         dynamic_data.current_supply += issue_amount;
      } );
   }

   modify( *vault, [this, &gold_per_dfs, current_reserve, issue_amount]( gold_reserve_vault_object& mutable_vault )
   {
      mutable_vault.dfs_locked_collateral = current_reserve;
      mutable_vault.gold_debt += issue_amount;
      mutable_vault.gold_pool_balance += issue_amount;
      mutable_vault.last_rebalance_price = gold_per_dfs;
      mutable_vault.last_rebalance_time = head_block_time();
      mutable_vault.last_feed_epoch = head_block_num();
   } );
}

bool database::spend_gold_reserve( share_type amount )
{
   FC_ASSERT( amount >= 0, "Cannot spend a negative GOLD amount" );
   if( amount == 0 )
      return true;

   const gold_reserve_vault_object* vault = find_gold_reserve_vault();
   if( !vault || !vault->enabled || vault->issuance_paused || vault->emergency_mode )
      return false;

   const uint32_t today = budget_day( head_block_time() );
   const bool new_day = vault->last_budget_day != today;
   const share_type daily_limit = new_day
      ? vault->gold_pool_balance / vault->daily_spending_divisor
      : vault->gold_daily_spending_limit;
   const share_type spent_today = new_day ? share_type() : vault->gold_spent_today;

   if( amount > vault->gold_pool_balance || amount > daily_limit - spent_today )
      return false;

   modify( *vault, [today, new_day, daily_limit, amount]( gold_reserve_vault_object& mutable_vault )
   {
      if( new_day )
      {
         mutable_vault.last_budget_day = today;
         mutable_vault.gold_spent_today = 0;
         mutable_vault.gold_daily_spending_limit = daily_limit;
      }
      mutable_vault.gold_pool_balance -= amount;
      mutable_vault.gold_spent_today += amount;
      mutable_vault.gold_committed_rewards += amount;
   } );
   return true;
}

} } // graphene::chain
