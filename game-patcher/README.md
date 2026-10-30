# game-patcher

## Build

```
gradlew clean build
```

This will generate a `game-patcher.dll` file in `build/lib/main/release`. The file needs to be injected into the game process.

The `EasyHook32.lib` comes from [here](https://easyhook.github.io/tutorials/nativemanuallyaddref.html), `user32.lib` and `WS2_32.Lib` come from the Windows Dev
Kit 10.0.22000.0 (`C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22000.0\um\x86`).

## Patches

Uses [EasyHook](https://easyhook.github.io/) to patch the `gethostbyname` function and modifies the result for the following GameSpy and EA addresses:

- `gpcm.gamespy.com` -> `gpcm.server.cnc-online.net`
- `peerchat.gamespy.com` -> `peerchat.server.cnc-online.net`
- `master.gamespy.com` -> `master.server.cnc-online.net`
- `gamestats.gamespy.com` -> `gamestats2.server.cnc-online.net`
- `psweb.gamespy.com` -> `server.cnc-online.net`
- `arenasdk.gamespy.com` -> `server.cnc-online.net`
- `lotrbfme.arenasdk.gamespy.com` -> `server.cnc-online.net`
- `ingamead.gamespy.com` -> `server.cnc-online.net`
- `lotrbme.available.gamespy.com` -> `master.server.cnc-online.net`
- `lotrbme.master.gamespy.com` -> `master.server.cnc-online.net`
- `lotrbme.ms13.gamespy.com` -> `master.server.cnc-online.net`
- `lotrbme2r.available.gamespy.com` -> `master.server.cnc-online.net`
- `lotrbme2r.master.gamespy.com` -> `master.server.cnc-online.net`
- `lotrbme2r.ms9.gamespy.com` -> `master.server.cnc-online.net`
- `lotrbme.gamestats.gamespy.com` -> `gamestats2.server.cnc-online.net`
- `lotrbme2r.gamestats.gamespy.com` -> `gamestats2.server.cnc-online.net`
- `lotrbme2wk.gamestats.gamespy.com` -> `gamestats2.server.cnc-online.net`
- `servserv.generals.ea.com` -> `http.server.cnc-online.net`
- `na.llnet.eadownloads.ea.com` -> `http.server.cnc-online.net`
- `bfme.fesl.ea.com` -> `login.server.cnc-online.net`
- `bfme2.fesl.ea.com` -> `login.server.cnc-online.net`
- `bfme2-ep1-pc.fesl.ea.com` -> `login.server.cnc-online.net`

Also modifies the game to skip certificate validation to allow connections to custom FESL servers.

### Gameplay and performance patches

Every patch can be switched off on its own in `src/main/headers/features.h` (e.g. `/DGAME_PATCHER_RENDER_LOCK=0`). All are on by default except
`GAME_PATCHER_PRODUCTION_IDS`. Patches that change the simulation must be the same for every player in a match.

| Patch                                                         | What it does                                                                                                                                                                     |
|---------------------------------------------------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `RENDER_RATE`                                                 | Draws 60 frames per second at the stock game speed; camera, particles and animations keep their 30 fps speed.                                                                    |
| `FRAME_LIMITER_SLEEP`                                         | The frame limiter sleeps instead of spinning, so the game no longer keeps a CPU core busy.                                                                                       |
| `RENDER_SCOPE_NAMES`                                          | Skips building profiler names that nothing reads on every render call.                                                                                                           |
| `RENDER_LOCK`                                                 | Uses a user mode lock instead of a kernel mutex around every render call.                                                                                                        |
| `SSE_MATH`                                                    | Faster `floor`, `ceil`, `sqrt` and `fabs` with identical results.                                                                                                                |
| `PATHFINDER_CELL_CENTRE`                                      | The pathfinder's crowd cost computes the cell centre once per call instead of once per unit.                                                                                     |
| `PATHFINDER_CROWD_COST_MEMO`, `PATHFINDER_CROWD_COST_REWRITE` | Caches per-unit answers during a path search, so path finding with big armies is much faster.                                                                                    |
| `COLLISION_PAIR_TABLE`                                        | Gives collision detection a bigger lookup table, so big armies no longer slow it down.                                                                                           |
| `COLLISION_CONTACT_HANDLER`                                   | Skips module calls that always return nothing when two objects touch.                                                                                                            |
| `HEALTH_BAR_CRASH_FIX`, `HEALTH_BAR_CRASH_FIX_FLUSH`          | Fixes the crash when too many health bars are on screen.                                                                                                                         |
| `AI_CONSTRUCTION_SITE_PRODUCTION`                             | The AI no longer produces units in buildings that are still under construction.                                                                                                  |
| `AI_ABILITY_CONDITIONS`                                       | The AI no longer casts abilities whose button is disabled, e.g. by a silenced hero.                                                                                              |
| `RENDER_INTERPOLATION`                                        | Smooth movement, turning and animations between two logic frames.                                                                                                                |
| `PRODUCTION_IDS`                                              | Off by default. Recruiting a hero at a second building no longer takes the money and starts nothing.                                                                             |
| `AI_BUILD_QUEUE`                                              | An AI build order that cannot get a builder no longer blocks every order behind it.                                                                                              |
| `MOUNT_TOGGLE_CRASH_FIX`                                      | Fixes a crash when a unit mounts or changes form after its move target is gone.                                                                                                  |
| `EVACUATE_CONTAINED_HEROES`                                   | EVACUATE also sends out heroes that can carry the One Ring.                                                                                                                      |
| `SIEGE_WALL_TELEPORT_FIX`                                     | Siege engines pushed against a wall are no longer lifted onto it.                                                                                                                |
| `AI_REBUILD_TARGETS`                                          | The AI no longer casts Rebuild, Snowbind or Untamed Allegiance on buildings under construction.                                                                                  |
| `GROUP_ABILITY_CONDITIONS`                                    | In a group cast, units whose own ability button is disabled no longer cast.                                                                                                      |
| `PATHFINDER_CLOSED_GATES`                                     | Units no longer walk through a gate that closed after their path was planned.                                                                                                    |
| `TOOLTIP_UPGRADE_DESCRIPTION`                                 | A researched upgrade's tooltip keeps its description.                                                                                                                            |
| `TOOLTIP_KILL_COUNT`                                          | A unit's hover tooltip shows its kills (needs the mod string `TOOLTIP:Kills`).                                                                                                   |
| `MOUNT_HEALTH_RATIO`                                          | Mounting and dismounting keep the health percentage instead of the hit points.                                                                                                   |
| `HERO_RECRUIT_QUEUE`                                          | A hero being recruited no longer holds up the units and upgrades queued behind it.                                                                                               |
| `HERO_BATTALION_ABSORPTION`                                   | A hero leaving a building no longer joins a battalion walking out of it.                                                                                                         |
| `CONSTRUCTION_INITIAL_HEALTH`                                 | Structures start construction at 10 % health instead of 1 hit point.                                                                                                             |
| `COOLDOWN_MODIFIERS`                                          | A recharge modifier gained or lost during a cooldown also changes the rest of that cooldown.                                                                                     |
| `GROUP_ABILITY_COOLDOWN`                                      | A group ability stays usable while any selected unit has it ready.                                                                                                               |
| `LIFE_DRAIN`                                                  | Life drain also heals on delayed hits and on every damage-over-time tick.                                                                                                        |
| `TOOLTIP_TIMERS`                                              | Tooltips show cooldowns, build times and research times (needs the mod strings `TOOLTIP:Cooldown`, `TOOLTIP:CooldownRemaining`, `TOOLTIP:BuildTime` and `TOOLTIP:ResearchTime`). |

## References

- https://darkatra.dev/2023/10/28/fixing-online-gameplay-for-bfme2.html
- https://github.com/Aim4kill/Bug_OldProtoSSL
