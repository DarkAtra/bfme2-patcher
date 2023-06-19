# game-patcher

## Build

```
gradlew clean build
```

This will generate a `game-patcher.dll` file in `build/lib/main/debug`. The file needs to be injected into the game process.

To regenerate the `detours.lib`, checkout [microsoft/detours](https://github.com/microsoft/detours) and build it using an x86 Native Tools Command Prompt.
See [here](https://github.com/microsoft/detours/wiki/FAQ#compiling-with-detours-code) for more information. The `user32.lib` comes from the windows dev kit
10.0.22000.0 in `C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22000.0\um\x86`.

## Patches

This dll uses [microsoft/detours](https://github.com/microsoft/detours) to patch the `gethostbyname` function in `ws2_32.dll` and modifies the result for the
following Gamespy and EA addresses:

- `gpcm.gamespy.com` -> gpcm.server.cnc-online.net
- `peerchat.gamespy.com` -> peerchat.server.cnc-online.net
- `master.gamespy.com` -> master.server.cnc-online.net
- `gamestats.gamespy.com` -> gamestats2.server.cnc-online.net
- `psweb.gamespy.com` -> server.cnc-online.net
- `arenasdk.gamespy.com` -> server.cnc-online.net
- `lotrbfme.arenasdk.gamespy.com` -> server.cnc-online.net
- `ingamead.gamespy.com` -> server.cnc-online.net
- `lotrbme.available.gamespy.com` -> master.server.cnc-online.net
- `lotrbme.master.gamespy.com` -> master.server.cnc-online.net
- `lotrbme.ms13.gamespy.com` -> master.server.cnc-online.net
- `lotrbme2r.available.gamespy.com` -> master.server.cnc-online.net
- `lotrbme2r.master.gamespy.com` -> master.server.cnc-online.net
- `lotrbme2r.ms9.gamespy.com` -> master.server.cnc-online.net
- `lotrbme.gamestats.gamespy.com` -> gamestats2.server.cnc-online.net
- `lotrbme2r.gamestats.gamespy.com` -> gamestats2.server.cnc-online.net
- `lotrbme2wk.gamestats.gamespy.com` -> gamestats2.server.cnc-online.net
- `servserv.generals.ea.com` -> http.server.cnc-online.net
- `na.llnet.eadownloads.ea.com` -> http.server.cnc-online.net
- `bfme.fesl.ea.com` -> login.server.cnc-online.net
- `bfme2.fesl.ea.com` -> login.server.cnc-online.net
- `bfme2-ep1-pc.fesl.ea.com` -> login.server.cnc-online.net

## References

- https://aluigi.altervista.org/papers.htm#gsproto
