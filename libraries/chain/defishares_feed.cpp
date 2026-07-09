/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
 *
 * The MIT License
 */

#include <graphene/chain/defishares_feed.hpp>
#include <graphene/chain/chain_property_object.hpp>
#include <graphene/chain/database.hpp>

#include <boost/rational.hpp>
#include <fc/uint128.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace graphene { namespace chain { namespace defishares {

namespace {

constexpr const char* DEFISHARES_GOLD_SYMBOL = "GOLD";

// BitShares settlement_price direction is debt / collateral, so GOLD's feed is GOLD / BTS.
// DefiShares inherits BTS as the genesis price anchor, but we store both BTS and GOLD using the
// same fixed-point USD scale, then derive the on-chain GOLD / DFS feed from those integers.
constexpr uint32_t DEFISHARES_GOLD_FEED_UPDATE_BLOCKS = 9600;
constexpr uint32_t DEFISHARES_BLOCKS_PER_YEAR = 28800 * 365;
constexpr uint64_t DEFISHARES_FEED_FACTOR_SCALE = 1000000000ULL;
constexpr uint16_t DEFISHARES_FIXED_INITIAL_COLLATERAL_RATIO = 2000;

price make_price_from_rational( fc::uint128_t numerator,
                                fc::uint128_t denominator,
                                asset_id_type base_id,
                                asset_id_type quote_id )
{
   FC_ASSERT( numerator > 0 && denominator > 0 );

   static const fc::uint128_t max( GRAPHENE_MAX_SHARE_SUPPLY );
   boost::rational<fc::uint128_t> value( numerator, denominator );

   while( value.numerator() > max || value.denominator() > max )
   {
      value = boost::rational<fc::uint128_t>(
         ( value.numerator() >> 1 ) + ( value.numerator() & 1U ),
         ( value.denominator() >> 1 ) + ( value.denominator() & 1U ) );
   }

   return asset( static_cast<int64_t>( value.numerator() ), base_id )
          / asset( static_cast<int64_t>( value.denominator() ), quote_id );
}

void apply_option_overrides( asset_bitasset_data_object& bitasset )
{
   const auto& exts = bitasset.options.extensions.value;
   if( exts.maintenance_collateral_ratio.valid() )
      bitasset.median_feed.maintenance_collateral_ratio = *exts.maintenance_collateral_ratio;
   if( exts.maximum_short_squeeze_ratio.valid() )
      bitasset.median_feed.maximum_short_squeeze_ratio = *exts.maximum_short_squeeze_ratio;
   if( manages_margin_positions( bitasset ) )
      bitasset.median_feed.initial_collateral_ratio = DEFISHARES_FIXED_INITIAL_COLLATERAL_RATIO;
   else if( exts.initial_collateral_ratio.valid() )
      bitasset.median_feed.initial_collateral_ratio = *exts.initial_collateral_ratio;
}

price multiply_feed_prices( const price& target_per_gold, const price& gold_per_core )
{
   FC_ASSERT( !target_per_gold.is_null() && !gold_per_core.is_null() );
   FC_ASSERT( target_per_gold.quote.asset_id == gold_per_core.base.asset_id );

   const fc::uint128_t numerator = fc::uint128_t( target_per_gold.base.amount.value )
                                   * gold_per_core.base.amount.value;
   const fc::uint128_t denominator = fc::uint128_t( target_per_gold.quote.amount.value )
                                     * gold_per_core.quote.amount.value;

   return make_price_from_rational( numerator, denominator,
                                    target_per_gold.base.asset_id,
                                    gold_per_core.quote.asset_id );
}

uint32_t gold_feed_elapsed_blocks( const database& db, const asset_object& gold_asset )
{
   const uint32_t head_block = db.head_block_num();
   if( head_block <= gold_asset.creation_block_num )
      return 0;

   const uint32_t elapsed_blocks = head_block - gold_asset.creation_block_num;
   return ( elapsed_blocks / DEFISHARES_GOLD_FEED_UPDATE_BLOCKS ) * DEFISHARES_GOLD_FEED_UPDATE_BLOCKS;
}

uint64_t gold_feed_growth_factor_scaled( uint32_t elapsed_blocks )
{
   if( elapsed_blocks == 0 )
      return DEFISHARES_FEED_FACTOR_SCALE;

   const long double years = static_cast<long double>( elapsed_blocks )
                             / static_cast<long double>( DEFISHARES_BLOCKS_PER_YEAR );
   const long double exponent = 2.0L * std::sqrt( years );
   const long double factor = std::pow( 10.0L, exponent );
   const long double scaled_factor = factor * static_cast<long double>( DEFISHARES_FEED_FACTOR_SCALE );

   if( !std::isfinite( scaled_factor )
       || scaled_factor >= static_cast<long double>( std::numeric_limits<uint64_t>::max() ) )
      return std::numeric_limits<uint64_t>::max();

   return std::max<uint64_t>( 1, static_cast<uint64_t>( std::round( scaled_factor ) ) );
}

} // namespace

const char* gold_symbol()
{
   return DEFISHARES_GOLD_SYMBOL;
}

uint16_t fixed_initial_collateral_ratio()
{
   return DEFISHARES_FIXED_INITIAL_COLLATERAL_RATIO;
}

bool is_gold_asset( const asset_object& asset_obj )
{
   return asset_obj.is_market_issued() && asset_obj.symbol == DEFISHARES_GOLD_SYMBOL;
}

bool manages_margin_positions( const asset_bitasset_data_object& bitasset )
{
   return !bitasset.is_prediction_market;
}

bool settlements_disabled( const asset_bitasset_data_object& bitasset )
{
   return manages_margin_positions( bitasset );
}

bool margin_calls_disabled( const asset_bitasset_data_object& bitasset )
{
   return manages_margin_positions( bitasset );
}

price margin_call_settlement_price( const asset_bitasset_data_object& bitasset, bool prefer_median_feed )
{
   if( prefer_median_feed && !bitasset.median_feed.settlement_price.is_null() )
   {
      const price& median_price = bitasset.median_feed.settlement_price;
      const bool matches_backing_asset =
            ( median_price.base.asset_id == bitasset.asset_id
              && median_price.quote.asset_id == bitasset.options.short_backing_asset )
         || ( median_price.quote.asset_id == bitasset.asset_id
              && median_price.base.asset_id == bitasset.options.short_backing_asset );
      if( matches_backing_asset )
         return median_price;
   }

   return bitasset.current_feed.settlement_price;
}

const asset_object* find_gold_asset( const database& db )
{
   const auto& idx = db.get_index_type<asset_index>().indices().get<by_symbol>();
   const auto itr = idx.find( DEFISHARES_GOLD_SYMBOL );
   if( itr == idx.end() || !itr->is_market_issued() )
      return nullptr;
   return &*itr;
}

bool accepts_gold_quote_feed( const database& db,
                              const asset_object& asset_obj,
                              const asset_bitasset_data_object& bitasset,
                              const price_feed& feed )
{
   if( feed.settlement_price.is_null() )
      return false;

   const asset_object* gold_asset = find_gold_asset( db );
   if( !gold_asset )
      return false;

   return !is_gold_asset( asset_obj )
          && bitasset.options.short_backing_asset == asset_id_type()
          && feed.settlement_price.base.asset_id == asset_obj.get_id()
          && feed.settlement_price.quote.asset_id == gold_asset->get_id();
}

price calculate_gold_settlement_price( const database& db, const asset_object& gold_asset )
{
   FC_ASSERT( is_gold_asset( gold_asset ), "DefiShares automatic GOLD feed can only be calculated for GOLD" );

   const asset_object& core_asset = asset_id_type()( db );
   const chain_property_object& chain_properties = db.get_chain_properties();
   const uint64_t initial_bts_price_usd_scaled = chain_properties.defishares_initial_bts_price_usd_scaled;
   const uint64_t initial_gold_price_usd_scaled = chain_properties.defishares_initial_gold_price_usd_scaled;
   const uint64_t growth_factor = gold_feed_growth_factor_scaled( gold_feed_elapsed_blocks( db, gold_asset ) );

   const fc::uint128_t gold_amount = fc::uint128_t( asset::scaled_precision( gold_asset.precision ).value )
                                     * initial_bts_price_usd_scaled
                                     * growth_factor;
   const fc::uint128_t core_amount = fc::uint128_t( asset::scaled_precision( core_asset.precision ).value )
                                     * initial_gold_price_usd_scaled
                                     * DEFISHARES_FEED_FACTOR_SCALE;

   FC_ASSERT( initial_bts_price_usd_scaled > 0 );
   FC_ASSERT( initial_gold_price_usd_scaled > 0 );

   return make_price_from_rational( gold_amount, core_amount, gold_asset.get_id(), core_asset.get_id() );
}

void apply_feed_policy( const database& db, asset_bitasset_data_object& bitasset )
{
   const asset_object& asset_obj = bitasset.asset_id( db );
   const asset_object* gold_asset = find_gold_asset( db );

   if( is_gold_asset( asset_obj ) )
   {
      bitasset.median_feed.settlement_price = calculate_gold_settlement_price( db, asset_obj );
      bitasset.median_feed.core_exchange_rate = bitasset.median_feed.settlement_price;
      bitasset.current_feed_publication_time = db.head_block_time();
      apply_option_overrides( bitasset );
      bitasset.current_feed = bitasset.median_feed;
      bitasset.refresh_cache();
      return;
   }

   if( !gold_asset || bitasset.median_feed.settlement_price.is_null() )
   {
      bitasset.current_feed = bitasset.median_feed;
      if( manages_margin_positions( bitasset ) )
         bitasset.current_feed.initial_collateral_ratio = DEFISHARES_FIXED_INITIAL_COLLATERAL_RATIO;
      bitasset.refresh_cache();
      return;
   }

   price_feed_with_icr current_feed = bitasset.median_feed;

   if( bitasset.median_feed.settlement_price.base.asset_id == bitasset.asset_id
       && bitasset.median_feed.settlement_price.quote.asset_id == gold_asset->get_id()
       && bitasset.options.short_backing_asset == asset_id_type() )
   {
      const price gold_per_core = calculate_gold_settlement_price( db, *gold_asset );
      const price target_per_core = multiply_feed_prices( bitasset.median_feed.settlement_price, gold_per_core );
      current_feed.settlement_price = target_per_core;
      current_feed.core_exchange_rate = target_per_core;
   }

   if( manages_margin_positions( bitasset ) )
      current_feed.initial_collateral_ratio = DEFISHARES_FIXED_INITIAL_COLLATERAL_RATIO;

   bitasset.current_feed = current_feed;
   bitasset.refresh_cache();
}

void refresh_scheduled_feeds( database& db )
{
   const asset_object* gold_asset = find_gold_asset( db );
   if( !gold_asset )
      return;

   const uint32_t head_block = db.head_block_num();
   if( head_block <= gold_asset->creation_block_num )
      return;

   if( ( head_block - gold_asset->creation_block_num ) % DEFISHARES_GOLD_FEED_UPDATE_BLOCKS != 0 )
      return;

   const auto& idx = db.get_index_type<asset_bitasset_data_index>().indices();
   auto itr = idx.begin();
   while( itr != idx.end() )
   {
      const asset_bitasset_data_object& bitasset = *itr;
      ++itr;

      const price old_current_price = bitasset.current_feed.settlement_price;
      const asset_id_type asset_id = bitasset.asset_id;
      const asset_object& asset_obj = asset_id( db );
      db.update_bitasset_current_feed( bitasset );
      const asset_bitasset_data_object& updated_bitasset = asset_obj.bitasset_data( db );
      if( !updated_bitasset.current_feed.settlement_price.is_null()
          && old_current_price != updated_bitasset.current_feed.settlement_price )
         db.check_call_orders( asset_obj, true, false, &updated_bitasset, true );
   }
}

} } } // graphene::chain::defishares
