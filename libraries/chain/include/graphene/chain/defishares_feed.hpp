/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
 *
 * The MIT License
 */
#pragma once

#include <graphene/chain/asset_object.hpp>
#include <graphene/protocol/asset.hpp>

namespace graphene { namespace chain {

class database;

namespace defishares {

const char* gold_symbol();

uint16_t fixed_initial_collateral_ratio();

bool is_gold_asset( const asset_object& asset_obj );

bool manages_margin_positions( const asset_bitasset_data_object& bitasset );

bool settlements_disabled( const asset_bitasset_data_object& bitasset );

bool margin_calls_disabled( const asset_bitasset_data_object& bitasset );

price margin_call_settlement_price( const asset_bitasset_data_object& bitasset, bool prefer_median_feed );

const asset_object* find_gold_asset( const database& db );

bool accepts_gold_quote_feed( const database& db,
                              const asset_object& asset_obj,
                              const asset_bitasset_data_object& bitasset,
                              const price_feed& feed );

price calculate_gold_settlement_price( const database& db, const asset_object& gold_asset );

share_type calculate_gold_debt_for_cr( share_type dfs_collateral,
                                       const price& gold_per_dfs,
                                       uint16_t collateral_ratio );

void apply_feed_policy( const database& db, asset_bitasset_data_object& bitasset );

bool refresh_scheduled_feeds( database& db );

} } } // graphene::chain::defishares
