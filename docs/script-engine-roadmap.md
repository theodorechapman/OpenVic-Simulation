# Script Engine Roadmap

Working TODO for the `feature/condition-evaluation` branch: getting from the current vertical
slice (architecture complete, coverage partial) to full Victoria 2 script fidelity.

## How this list was generated

The data below comes from running the headless build against an unmodified Victoria 2 install
(base + DLC data, ~1.2 GB) and harvesting the engine's warn-once coverage logs, plus a key
frequency count over `events/*.txt` and `decisions/*.txt`:

```sh
./out/build/<preset>/bin/Debug/openvic-simulation.headless -b <path-to-victoria2> 2>&1 | tee /tmp/headless.log
grep -oE "Effect [a-z_]+ is not registered" /tmp/headless.log | awk '{print $2}' | sort -u
grep -oE "Condition [a-z_]+ has no evaluator" /tmp/headless.log | awk '{print $2}' | sort -u
```

Regenerate after every coverage batch - the lists shrink as evaluators land. Baseline run
(2026-07-30): full vanilla load SUCCEEDS, 7 errors (all the delayed `country_event` dict form),
two vanilla events fired spontaneously during test ticks (16451/TEX, 10230/SWE).

## Fidelity policy

**Match Victoria 2's observed behaviour, not what seems sensible.** When implementing any
mechanic below, check the wiki (vic2.paradoxwikis.com), the files themselves, and - where
genuinely ambiguous - test in the actual game. Document each fidelity decision in a comment at
the implementation site. Behaviours already implemented and their fidelity notes:

| Mechanic | Our behaviour | Status |
|---|---|---|
| Script months | 0-based (`month = 11` = December) | matches wiki; verify in-game |
| `NOT` | NOR over children | matches V2 |
| Empty `AND` / iterator-over-empty-set | vacuously true / `any_` false, `all_` true | logical convention; verify edge cases in-game |
| ConditionalWeight `group` | first modifier whose condition passes wins | matches V2 poptype semantics |
| MTTH | daily fire chance = 1 / MTTH-days | V2 actually checks each event every ~20 days with scaled chance - **known deviation**, see Staggering below |
| Event options (AI) | highest evaluated `ai_chance` wins, first on ties | V2 rolls weighted-random - **known deviation**, switch to weighted roll |
| `on_yearly/quarterly_pulse` | one weighted-random pick per country, trigger-gated | verify pulse cadence + whether trigger failure rerolls |
| Province events | owned land provinces only, owner = THIS | verify THIS binding in-game |

Anything marked "known deviation" or "verify" is a standing TODO even though code exists.

## Priority 0 - structural (unblocks everything else)

1. **Pending event queue.** Fixes the 7 vanilla load errors: `country_event = { id = X days = Y }`
   delays an event. Queue entries (event, target, fire date), drained in the daily tick. Also the
   foundation for *player-facing* events: human-controlled countries' events go to the queue as
   pending game actions instead of auto-resolving with the AI choice.
2. **Numeric setters on instances.** `prestige` is the single most used effect in vanilla (1,063
   occurrences); pop `militancy`/`consciousness` effects are ~1,150 combined; `treasury`,
   `badboy`, `plurality`, `war_exhaustion` follow. All need small mutation APIs on
   CountryInstance/Pop (reactive `MutableState` members are currently read-only outside their
   owners). Coordinate with upstream - wvpm owns these types. V2 fidelity: prestige effect adds
   (not sets); scaled_militancy/scaled_consciousness scale by pop support of an issue/ideology.
3. **Generic family evaluators.** One evaluator each wipes out most of the 96-condition gap:
   - technologies (`electricity`, `early_railroad`, ... - BOOLEAN_INT, key_item = Technology)
   - inventions (`invention = X` + invention names)
   - reforms (`health_care`, `wage_reform`, ... - value = active reform in that group)
   - party policies / stances (`citizenship_policy`, `protectionism`, `jingoism`, ...)
   - ideology support (`liberal`, `socialist`, ... - upper house / pop support %)
   - goods produced (`produces = X`, good names as conditions)
   - pop type percentages (`has_pop_type`, pop type names as country conditions)
   The parse layer already resolves the key to the definition object (`condition_key_item`), so
   each family evaluator dispatches on that. Fidelity: check whether V2 compares >= vs > for the
   percentage forms, and what base each percentage is over (total pops vs relevant subset).
4. **RNG in ExecutionContext.** `random_country`, `random_state`, `random_owned`, `random_pop`,
   `random_province`, `random_list` effect scopes all need a deterministic random source at
   execution time (thread the seeded Xoshiro through). Fidelity: `random_list` weights are
   integer shares; `random_owned` picks one owned province uniformly.

## Priority 1 - high-frequency singles

Conditions (hit during live ticks, roughly by content frequency):
`average_consciousness`, `average_militancy` (PopsAggregate already tracks these),
`government`, `capital` (province id compare), `any_neighbor_country` (needs adjacency via
MapInstance), `any_state` / `state_scope` iterators, `relation` (COMPLEX who/value),
`is_core` (dual tag/province form), `is_accepted_culture`, `is_primary_culture`,
`ruling_party_ideology`, `upper_house` (COMPLEX ideology/value), `literacy`, `plurality`,
`unemployment`, `num_of_cities`, `military_score`, `is_colonial` (state scope),
`controlled_by`, `owned_by`, `has_country_modifier` / `has_province_modifier`,
`part_of_sphere`, `religion`, `nationalvalue`, `is_ideology_enabled`, `crisis_exist` (false
until crises exist).

Effects (by frequency): `country_event` dict form (P0 queue), `prestige` (P0 setters),
`scaled_militancy` / `scaled_consciousness` (P0 setters + issue support), `add_country_modifier`
/ `add_province_modifier` / `remove_*` (timed modifier lists exist on instances - check
expiry fidelity), `add_accepted_culture` / `remove_accepted_culture`, `dominant_issue`,
`ruling_party_ideology`, `change_tag` (big: country switch), `secede_province`, `inherit`,
`add_casus_belli` / `casus_belli` (needs CB storage), `political_reform` / `social_reform` /
`economic_reform` (next-reform stepping), `war_exhaustion`, `treasury`, `plurality`,
`capital` (move capital), `enable_ideology`, `enable_canal`, `election`, `nationalize`,
`release_vassal`, `annex_to`, `end_war`, `country` / `any_country` / `any_owned` /
`any_greater_power` / `all_core` / `state_scope` / `sphere_owner` effect scopes.

## Priority 2 - correctness & infrastructure

- **Event check staggering.** V2 checks each event per target every ~20 days with chance scaled
  accordingly, not daily. Match once content is broadly firing; affects both perf and observed
  event rates. Verify the exact pulse interval against the game/defines.
- **AI option choice**: switch to ai_chance-weighted random roll (see fidelity table).
- **Trigger-only + `on_action` completeness**: remaining on_actions (`on_startup`,
  `on_election_tick`, `on_battle_won`, ...) fire from their respective systems as those systems
  come online.
- **THIS/FROM propagation** through `country_event` chains (FROM = sender) - verify against game.
- **Parse-time event id binding** (add EVENT to `identifier_type_t`) instead of runtime
  to_chars lookup.
- **Limit blocks on iterating conditions** (`any_owned_province = { limit = ... }` appears in
  some mods) - currently conditions have no limit support (effects do).
- **Diagnostics**: replace warn-once with a coverage counter dump on exit (id -> hit count) so
  each headless run emits the ranked TODO directly.

## Raw coverage data (baseline 2026-07-30)

**119 unregistered effects** (note: entries like good names (`cotton`, `iron`) and pop types
(`farmers`, `aristocrats`) are sub-keys of unparsed dict-form effects - they disappear once the
enclosing effect parses properly):

activate_technology add_accepted_culture add_casus_belli add_country_modifier
add_crisis_interest add_crisis_temperature add_province_modifier add_tax_relative_income
add_war_goal ai_chance all_core ammunition any_country any_greater_power any_neighbor_country
any_pop any_state aristocrats artillery artisans badboy build_factory_in_capital_state
build_fort_in_capital build_railway_in_capital bureaucrats canned_food capital capitalists
casus_belli cattle cement change_province_name change_tag change_tag_no_core_switch civilized
clergymen clerks clipper_convoy cotton country craftsmen define_general diplomatic_influence
dominant_issue dye economic_reform election enable_canal enable_ideology explosives farmers
fertilizer fish fuel furniture government great_wars_enabled inherit iron is_slave labourers
leave_alliance liquor lumber luxury_clothes luxury_furniture machine_parts middle_strata
modify_relation money move_issue_percentage name nationalize officers plurality
political_reform poor_strata prestige prestige_factor radio random_country random_list
random_owned random_pop random_province random_state regular_clothes relation release_vassal
remove_country_modifier remove_province_modifier remove_random_economic_reforms
remove_random_military_reforms rich_strata ruling_party_ideology scaled_consciousness
scaled_militancy secede_province silk slaves small_arms social_reform soldiers sphere_owner
state_scope steamer_convoy steel sulphur tea tech_school tobacco trade_goods treasury
upper_house war war_exhaustion wine world_wars_enabled years_of_research

**~96 conditions hit during ticks without evaluators** (family members marked by example):

any_neighbor_country any_neighbor_province any_state average_consciousness average_militancy
breech_loaded_rifles(tech) business_banks(tech) business_regulations(tech) capital
citizenship_policy(policy) colonial_nation constructing_cb_progress controlled_by
crime_fighting crisis_exist diplomatic_influence early_railroad(tech) economic_policy(policy)
electricity(tech) freedom_of_trade(tech) functionalism(tech) government
government_interventionism(tech) great_wars_enabled has_country_modifier has_pop_type
has_province_modifier health_care(reform) inorganic_chemistry(tech) invention
involved_in_crisis is_accepted_culture is_colonial is_core is_ideology_enabled is_independant
is_primary_culture jingoism(policy) liberal(ideology) liquor(good) literacy
market_regulations(tech) medicine(tech) middle_strata_everyday_needs military_score
moralism(tech) nationalism_n_imperialism(tech) nationalvalue num_of_cities
organic_chemistry(tech) owned_by pacifism(policy) part_of_sphere pensions(reform) plurality
political_parties(tech) political_reform_want poor_strata_everyday_needs
poor_strata_life_needs poor_strata_militancy press_rights(reform) pro_military(policy)
private_bank_money_bill_printing(tech) produces protectionism(policy) public_meetings(reform)
radio(tech) relation religion religious_policy(policy) rich_strata_life_needs
ruling_party_ideology safety_regulations(reform) slavery(reform) social_reform_want
social_science(tech) socialist(ideology) state_scope steel_steamers(tech) stock_exchange(tech)
tobacco(good) trade_policy(policy) trade_unions(reform) unemployment unemployment_subsidies
universal_voting(reform) upper_house upper_house_composition vote_franschise(reform)
voting_system(reform) wage_reform(reform) war_policy(policy) work_hours(reform)
world_wars_enabled yes_meeting(reform)

## Upstream plan

1. Small standalone PRs first: IndexedFlatMap empty-span fix, Script move assignment,
   ConditionNode value_item constructor fix.
2. Then the evaluator architecture as an RFC/PR referencing the review feedback on upstream
   PR #698 (evaluation must not live on instance types; this branch's function-pointer
   registration satisfies that) - crediting #698's groundwork.
3. Setter additions to CountryInstance/Pop proposed as their own small PRs, since those types
   are actively being refactored upstream.
