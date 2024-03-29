# game-patcher

## Build

```
gradlew clean build
```

This will generate a `game-patcher.dll` file in `build/lib/main/debug`. The file needs to be injected into the game process.

The `EasyHook32.lib` comes from [here](https://easyhook.github.io/tutorials/nativemanuallyaddref.html), `user32.lib` and `WS2_32.Lib` come from the windows dev
kit 10.0.22000.0 (`C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22000.0\um\x86`).

## Patches

Uses [EasyHook](https://easyhook.github.io/) to patch the `gethostbyname` function and modifies the result for the following GameSpy and EA
addresses:

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

## References

- https://darkatra.dev/2023/10/28/fixing-online-gameplay-for-bfme2.html
- https://github.com/Aim4kill/Bug_OldProtoSSL
