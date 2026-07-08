DefiShares Smartcoin Feed and Risk Policy
=========================================

Overview
--------

This document describes the current DefiShares smartcoin behavior on branch `defishares`.

DefiShares no longer follows upstream BitShares smartcoin policy in three important areas:

1. the feed anchor is changed from the legacy direct `TARGET/DFS` core-feed route to a
   GOLD-centered model;
2. all DefiShares-managed bitassets use a fixed initial collateral ratio (`ICR`) of `2.0`;
3. settlement and margin-call style liquidation paths are disabled, and low-collateral positions
   are restricted to repair-only updates.

This document is intended for developers who need to understand the exact protocol behavior,
where the code was changed, and what tests cover the implementation.

Scope of the Current Implementation
-----------------------------------

Implemented in the current DefiShares branch:

- automatic chain-generated `GOLD/DFS` pricing;
- witness publication of `TARGET/GOLD` for other DFS-backed bitassets;
- derivation of effective `TARGET/DFS` from `TARGET/GOLD * GOLD/DFS`;
- fixed `ICR = 2.0` for DefiShares-managed bitassets;
- disabled user settlement and global settlement for DefiShares-managed bitassets;
- disabled margin-call processing for DefiShares-managed bitassets;
- repair-only update rules when a position falls below `ICR`;
- unit tests for feed behavior and the new collateral-policy behavior.

Out of scope for this change set:

- migration logic for legacy chains with pre-existing live positions;
- support for alternate anchor assets besides `GOLD`;
- protocol redesign for non-core-backed smartcoins.

Protocol-Level Behavior Changes
-------------------------------

Compared with upstream BitShares behavior, DefiShares changes smartcoin protocol semantics in the
following ways:

1. GOLD becomes the protocol feed anchor for DefiShares core-backed bitassets

   Witnesses no longer publish direct effective `TARGET/DFS` prices for DefiShares core-backed
   smartcoins once `GOLD` exists. Instead, `GOLD` is the common routing asset.

2. GOLD pricing is system-generated

   `GOLD/DFS` is calculated by deterministic chain logic. Witnesses do not control the effective
   GOLD settlement price.

3. Other DefiShares bitassets publish `TARGET/GOLD`

   For DefiShares core-backed bitassets other than `GOLD`, the witness entry point becomes
   `TARGET/GOLD`.
   The chain stores this witness median, then derives the effective `TARGET/DFS` current feed.

4. Direct `TARGET/DFS` witness publication is rejected after GOLD exists

   This is an intentional protocol change. Once the GOLD anchor exists, a non-GOLD DefiShares
   core-backed bitasset must use the GOLD route rather than the old direct core route.

5. Effective feed consumption now uses derived `current_feed`

   Witness median data may intentionally remain `TARGET/GOLD`, while the rest of the debt engine
   consumes the derived `TARGET/DFS` `current_feed`.

6. All DefiShares-managed bitassets use fixed `ICR = 2.0`

   Witnesses and asset options no longer control the effective initial collateral ratio for these
   bitassets. The chain forces `initial_collateral_ratio = 2000`.

7. Settlement is disabled

   DefiShares-managed bitassets reject both user settlement requests and global settlement.

8. Margin-call liquidation is disabled

   The automatic call-order liquidation path is bypassed for DefiShares-managed bitassets.

9. Sub-ICR positions enter repair-only mode

   If a debt position's collateral ratio is below `2.0`, the owner may still add collateral or
   repay debt, but may not borrow more or withdraw collateral.

Feed Model
----------

### 1. GOLD feed is system-generated

The chain treats a market-issued asset whose symbol is exactly `GOLD` as the anchor asset.

`GOLD` does not depend on witness-published settlement prices. Its settlement price is calculated
internally as:

`GOLD/DFS = V0 * 10^(2 * sqrt(years_since_creation))`

where:

- `V0` is currently anchored as `1 GOLD / 1 DFS`;
- `years_since_creation` is derived from elapsed blocks;
- elapsed time is quantized to `9600`-block intervals.

Implementation details:

- update interval: `9600` blocks;
- blocks per year constant: `28800 * 365`;
- price direction is kept in native BitShares settlement direction: `debt / collateral`.

For GOLD this means:

- `base = GOLD`
- `quote = DFS`

The implementation is intentionally written in this direction so the algorithm does not become the
inverse by mistake.

### 2. Other DefiShares core-backed bitassets publish TARGET/GOLD

Once a valid `GOLD` bitasset exists, non-GOLD DefiShares core-backed bitassets are expected to
publish:

`TARGET/GOLD`

Examples:

- `USDBIT/GOLD`
- `CNYBIT/GOLD`

These values are stored as witness median input. The chain then derives:

`TARGET/DFS = TARGET/GOLD * GOLD/DFS`

The derived `TARGET/DFS` is written to `current_feed`, which is the feed consumed by collateral
and debt logic.

### 3. Direct TARGET/DFS publishing is rejected after GOLD exists

After `GOLD` exists, a non-GOLD DFS-backed bitasset may no longer publish a direct `TARGET/DFS`
feed. This is enforced during feed evaluation.

Collateral and Risk Policy
--------------------------

### 1. Fixed ICR

All DefiShares-managed bitassets use:

- `ICR = 2000`
- effective ratio meaning `2.0x`

This is forced by chain logic and does not depend on witness feed extensions.

Important behavioral detail:

- `CR == 2.0` is allowed;
- only `CR < 2.0` is treated as under-collateralized for update restrictions.

### 2. Repair-only rule below ICR

If an existing debt position is below `ICR = 2.0`, the owner is restricted as follows:

- allowed: repay debt;
- allowed: add collateral;
- allowed: full close of the position;
- not allowed: borrow more debt;
- not allowed: withdraw collateral.

This rule is intentionally directional. A repair action is still allowed even if the position
remains below `2.0` after the operation, because otherwise users could get stuck in an
unrecoverable state.

### 3. Margin calls disabled

The automatic margin-call / strong-liquidation path is disabled for DefiShares-managed bitassets.
The engine does not process call orders through the traditional liquidation path for them.

### 4. Settlements disabled

The following operations are rejected for DefiShares-managed bitassets:

- force settlement by asset holder;
- global settlement by issuer / authorized path.

Current Feed and Cache Semantics
--------------------------------

This branch depends on a specific distinction:

- `median_feed` preserves the witness-supplied source feed, which may be `TARGET/GOLD`;
- `current_feed` stores the effective derived feed, which is `TARGET/DFS`.

For this reason, collateral caches must follow `current_feed`, not only `median_feed`.

The implementation refreshes derived collateralization caches from `current_feed` so the
following values stay aligned with the actual effective feed:

- `current_maintenance_collateralization`
- `current_initial_collateralization`

This is especially important for DefiShares because `median_feed` and `current_feed` are expected
to differ for non-GOLD bitassets.

Code Layout
-----------

Main implementation entry points:

- `libraries/chain/defishares_feed.cpp`
- `libraries/chain/include/graphene/chain/defishares_feed.hpp`

Integration points:

- `libraries/chain/asset_object.cpp`
- `libraries/chain/asset_evaluator.cpp`
- `libraries/chain/db_market.cpp`
- `libraries/chain/db_update.cpp`
- `libraries/chain/market_evaluator.cpp`
- `tests/tests/bitasset_tests.cpp`

Implementation Details
----------------------

### GOLD asset detection

The helper namespace `graphene::chain::defishares` identifies the anchor asset by:

- exact symbol match: `GOLD`
- asset must be market-issued

Relevant helpers:

- `defishares::gold_symbol()`
- `defishares::is_gold_asset()`
- `defishares::find_gold_asset()`

### Automatic GOLD initialization and refresh

When the `GOLD` bitasset is created, the feed update path initializes its effective feed so the
asset does not wait for a later scheduled refresh before becoming usable.

Scheduled recalculation happens from `database::update_global_dynamic_data()` on every block.

`defishares::refresh_scheduled_feeds()`:

- finds the GOLD asset;
- checks whether the current head block is aligned to a `9600`-block boundary relative to
  `gold.creation_block_num`;
- refreshes GOLD and dependent current feeds;
- runs downstream current-feed-dependent processing as needed.

### Feed derivation behavior

`apply_feed_policy()` is called from `database::update_bitasset_current_feed()`.

Behavior:

1. If asset is `GOLD`
   - recompute automatic `GOLD/DFS`;
   - write it to both `median_feed` and `current_feed`;
   - mirror it into `core_exchange_rate`.

2. If asset is not `GOLD`
   - preserve witness median input;
   - if median feed is `TARGET/GOLD` and the backing asset is DFS, derive `TARGET/DFS`;
   - write the effective derived value into `current_feed`.

3. If asset is DefiShares-managed
   - force `current_feed.initial_collateral_ratio = 2000` on the effective feed path.

This last point matters. During implementation, one bug came from narrowing the derived feed to a
plain `price_feed` temporary, which dropped the `initial_collateral_ratio` field. The corrected
implementation keeps the effective feed as `price_feed_with_icr`.

Validation Rules
----------------

`asset_publish_feed` validation is adjusted so DefiShares feed flow can accept the GOLD-centered
source format.

During evaluation:

- direct backing-asset feed is still accepted in the traditional case;
- after GOLD exists, non-GOLD DFS-backed assets must publish `TARGET/GOLD`;
- GOLD itself uses automatic chain-generated pricing.

For debt-position updates:

- if the position is not below ICR, normal borrow / repay / collateral update rules continue;
- if the position is below ICR and not being fully closed, only debt-reducing or
  collateral-increasing updates are allowed.

Testing
-------

The following unit tests are present in `tests/tests/bitasset_tests.cpp`:

- `defishares_gold_feed_is_automatic`
- `defishares_target_gold_feed_is_derived_to_core`
- `defishares_gold_manual_feed_does_not_override_automatic`
- `defishares_target_gold_feed_uses_median_before_derivation`
- `defishares_rejects_direct_core_feed_once_gold_exists`
- `defishares_fixed_icr_allows_exact_two_times_collateralization`
- `defishares_disables_settlement_and_margin_calls`
- `defishares_low_cr_position_can_only_repair`

These tests verify:

- GOLD initializes automatically;
- GOLD reprices on the expected scheduled boundary;
- `TARGET/GOLD` is converted into `TARGET/DFS`;
- witness median is taken before derivation;
- manual direct `TARGET/DFS` publication is rejected once GOLD exists;
- the effective `ICR` is fixed to `2.0`;
- exact `2.0x` collateralization is accepted;
- settlement and margin-call paths are disabled;
- a sub-ICR position can only perform repair operations.

Test command used during implementation:

    cd build/tests
    ./chain_test --run_test="bitasset_tests/defishares_*" --log_level=test_suite

Current Boundaries and Developer Notes
--------------------------------------

1. This design currently targets DFS-backed market-issued assets managed by the DefiShares policy
   helpers.

2. The symbol `GOLD` is a hard-coded anchor. If future work wants multiple anchors or a different
   naming policy, the current implementation will need refactoring.

3. The initial anchor value is currently fixed at `1 GOLD / 1 DFS`. If the business rule changes,
   update:

   - `DEFISHARES_INITIAL_GOLD_PER_BTS_NUMERATOR`
   - `DEFISHARES_INITIAL_GOLD_PER_BTS_DENOMINATOR`

4. Scheduled updates use block-height alignment, not wall-clock timestamps.

5. DefiShares intentionally diverges from upstream smartcoin risk handling by disabling settlement
   and margin-call liquidation. Any future reintroduction of those paths will need an explicit
   protocol decision and fresh test coverage.

Files Changed by the Current DefiShares Behavior
------------------------------------------------

- `defishares-docs/Gold-Feed-Policy.md`
- `libraries/chain/asset_evaluator.cpp`
- `libraries/chain/asset_object.cpp`
- `libraries/chain/db_market.cpp`
- `libraries/chain/db_update.cpp`
- `libraries/chain/defishares_feed.cpp`
- `libraries/chain/include/graphene/chain/defishares_feed.hpp`
- `libraries/chain/market_evaluator.cpp`
- `tests/tests/bitasset_tests.cpp`
