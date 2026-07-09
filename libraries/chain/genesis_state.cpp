/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
 *
 * The MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <graphene/chain/genesis_state.hpp>
#include <graphene/protocol/fee_schedule.hpp>

#include <fc/io/raw.hpp>

#include <cctype>
#include <limits>

namespace graphene { namespace chain {

namespace {

constexpr uint64_t DEFISHARES_USD_PRICE_SCALE = 100000000ULL; // 1e-8 USD
constexpr size_t DEFISHARES_USD_PRICE_DECIMALS = 8;

uint64_t parse_defishares_usd_price_string( const string& amount_string, const char* field_name )
{ try {
   FC_ASSERT( !amount_string.empty(), "${field_name} must not be empty", ("field_name", field_name) );

   bool decimal_found = false;
   size_t digits_found = 0;
   size_t fractional_digits = 0;
   uint64_t scaled_value = 0;

   for( const char c : amount_string )
   {
      if( std::isdigit( static_cast<unsigned char>( c ) ) )
      {
         FC_ASSERT( scaled_value <= ( std::numeric_limits<uint64_t>::max() - uint64_t( c - '0' ) ) / 10,
                    "${field_name} is too large", ("field_name", field_name) );
         scaled_value = ( scaled_value * 10 ) + uint64_t( c - '0' );
         ++digits_found;
         if( decimal_found )
            ++fractional_digits;
         continue;
      }

      if( c == '.' && !decimal_found )
      {
         decimal_found = true;
         continue;
      }

      FC_ASSERT( false, "${field_name} must be a non-negative decimal string", ("field_name", field_name) );
   }

   FC_ASSERT( digits_found > 0, "${field_name} must contain digits", ("field_name", field_name) );
   FC_ASSERT( fractional_digits <= DEFISHARES_USD_PRICE_DECIMALS,
              "${field_name} supports at most 8 fractional digits",
              ("field_name", field_name) );

   while( fractional_digits < DEFISHARES_USD_PRICE_DECIMALS )
   {
      FC_ASSERT( scaled_value <= std::numeric_limits<uint64_t>::max() / 10,
                 "${field_name} is too large", ("field_name", field_name) );
      scaled_value *= 10;
      ++fractional_digits;
   }

   FC_ASSERT( scaled_value > 0, "${field_name} must be greater than 0", ("field_name", field_name) );
   FC_ASSERT( DEFISHARES_USD_PRICE_SCALE > 0 );

   return scaled_value;
} FC_CAPTURE_AND_RETHROW( (amount_string)(field_name) ) }

} // namespace

chain_id_type genesis_state_type::compute_chain_id() const
{
   return initial_chain_id;
}

uint64_t genesis_state_type::get_defishares_initial_bts_price_usd_scaled() const
{
   return parse_defishares_usd_price_string( defishares_initial_bts_price_usd,
                                             "defishares_initial_bts_price_usd" );
}

uint64_t genesis_state_type::get_defishares_initial_gold_price_usd_scaled() const
{
   return parse_defishares_usd_price_string( defishares_initial_gold_price_usd,
                                             "defishares_initial_gold_price_usd" );
}

void genesis_state_type::override_witness_signing_keys( const std::string& new_key )
{
   public_key_type new_pubkey( new_key );
   for( auto& wit : initial_witness_candidates )
   {
      wit.block_signing_key = new_pubkey;
   }
}

} } // graphene::chain

FC_REFLECT_DERIVED_NO_TYPENAME(graphene::chain::genesis_state_type::initial_account_type, BOOST_PP_SEQ_NIL,
           (name)(owner_key)(active_key)(is_lifetime_member) )

FC_REFLECT_DERIVED_NO_TYPENAME(graphene::chain::genesis_state_type::initial_asset_type, BOOST_PP_SEQ_NIL,
           (symbol)(issuer_name)(description)(precision)(max_supply)(accumulated_fees)(is_bitasset)
           (collateral_records))

FC_REFLECT_DERIVED_NO_TYPENAME(graphene::chain::genesis_state_type::initial_asset_type::initial_collateral_position,
           BOOST_PP_SEQ_NIL, (owner)(collateral)(debt))

FC_REFLECT_DERIVED_NO_TYPENAME(graphene::chain::genesis_state_type::initial_balance_type, BOOST_PP_SEQ_NIL,
           (owner)(asset_symbol)(amount))

FC_REFLECT_DERIVED_NO_TYPENAME(graphene::chain::genesis_state_type::initial_vesting_balance_type, BOOST_PP_SEQ_NIL,
           (owner)(asset_symbol)(amount)(begin_timestamp)(vesting_duration_seconds)(begin_balance))

FC_REFLECT_DERIVED_NO_TYPENAME(graphene::chain::genesis_state_type::initial_witness_type, BOOST_PP_SEQ_NIL,
           (owner_name)(block_signing_key))

FC_REFLECT_DERIVED_NO_TYPENAME(graphene::chain::genesis_state_type::initial_committee_member_type, BOOST_PP_SEQ_NIL,
           (owner_name))

FC_REFLECT_DERIVED_NO_TYPENAME(graphene::chain::genesis_state_type::initial_worker_type, BOOST_PP_SEQ_NIL,
           (owner_name)(daily_pay))

FC_REFLECT_DERIVED_NO_TYPENAME(graphene::chain::genesis_state_type, BOOST_PP_SEQ_NIL,
           (initial_timestamp)(max_core_supply)
           (defishares_initial_bts_price_usd)(defishares_initial_gold_price_usd)
           (initial_parameters)(initial_accounts)(initial_assets)
           (initial_balances)(initial_vesting_balances)(initial_active_witnesses)(initial_witness_candidates)
           (initial_committee_candidates)(initial_worker_candidates)
           (immutable_parameters))

GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::genesis_state_type::initial_account_type )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::genesis_state_type::initial_asset_type )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::genesis_state_type::initial_asset_type::initial_collateral_position )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::genesis_state_type::initial_balance_type )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::genesis_state_type::initial_vesting_balance_type )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::genesis_state_type::initial_witness_type )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::genesis_state_type::initial_committee_member_type )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::genesis_state_type::initial_worker_type )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::genesis_state_type )
