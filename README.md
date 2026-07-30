[![🖥️ Builds](https://github.com/OpenVicProject/OpenVic-Simulation/actions/workflows/builds.yml/badge.svg)](https://github.com/OpenVicProject/OpenVic-Simulation/actions/workflows/builds.yml)

# OpenVic-Simulation

> ### About this fork
> This is [Theo Chapman](https://github.com/theodorechapman)'s working fork of
> [OpenVic-Simulation](https://github.com/OpenVicProject/OpenVic-Simulation). Upstream has a complete
> Victoria 2 data parser and a deep economic simulation, but scripts (event triggers, effects, MTTH,
> `ai_will_do`, ...) only parse - they can't run. The `feature/condition-evaluation` branch adds that
> missing runtime: conditions evaluate against game state, effects execute and mutate it, conditional
> weights compute, and the daily tick spontaneously fires country/province/pulse events, chains events
> through effects, and runs a monthly AI decision pass - all deterministic (fixed-point math, seeded RNG)
> and unit tested end to end from real script text.
>
> The architecture - evaluation/execution function pointers bound once at condition/effect registration,
> no string dispatch at runtime, no script logic on game instance types - is a deliberate alternative to
> upstream PR [#698](https://github.com/OpenVicProject/OpenVic-Simulation/pull/698) and its review
> feedback, and is intended to be offered upstream. Coverage is a vertical slice: ~25 of ~245 conditions
> and ~15 effects so far; everything else is a warn-once no-op so unmodified game files still load.

Repo of the OpenVic-Simulation Library for [OpenVic](https://github.com/OpenVicProject/OpenVic)

## Quickstart Guide
For detailed instructions, view the OpenVic Contributor Quickstart Guide [here](https://github.com/OpenVicProject/OpenVic/blob/master/docs/contribution-quickstart-guide.md)

## Required
* [CMake](https://cmake.org/) 3.28+
* [Ninja](https://ninja-build.org/)
* Python 3 (used by the build for code generation)

## Build Instructions
1. Run the command `git submodule update --init` to retrieve the first-party submodules (openvic-dataloader, lexy-vdf). Third-party dependencies are fetched automatically by CMake.
2. Pick a configure preset from `CMakePresets.json` (`windows-x64-md`, `windows-x64-mt`, `linux-x64`, `macos-universal`) and run `cmake --preset <preset>` in the project root.
3. Run `cmake --build --preset <preset>-debug` (or `<preset>-release`). The static library, headless executable, and unit tests land in `out/build/<preset>/bin/<Config>/`.
4. Run the tests with `ctest --preset <preset>-debug`.

The headless executable and tests are built by default in standalone builds; disable with `-DOPENVIC_SIM_BUILD_HEADLESS=OFF` / `-DOPENVIC_SIM_BUILD_TESTS=OFF`. Benchmarks are opt-in: `-DOPENVIC_SIM_BUILD_BENCHMARKS=ON`.

## Link Instructions
Use CMake: `add_subdirectory(openvic-simulation)` and link against `openvic::simulation`.
