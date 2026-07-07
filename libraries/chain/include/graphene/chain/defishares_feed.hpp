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

bool is_gold_asset( const asset_object& asset_obj );

const asset_object* find_gold_asset( const database& db );

bool accepts_gold_quote_feed( const database& db,
                              const asset_object& asset_obj,
                              const asset_bitasset_data_object& bitasset,
                              const price_feed& feed );

price calculate_gold_settlement_price( const database& db, const asset_object& gold_asset );

void apply_feed_policy( const database& db, asset_bitasset_data_object& bitasset );

void refresh_scheduled_feeds( database& db );

} } } // graphene::chain::defishares
