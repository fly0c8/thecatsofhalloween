# The Cats of Halloween

A two-player **online** Amiga-style Halloween platformer in C++20 and [raylib 5.5](https://www.raylib.com),
with a game server written in Go. Mortimer (black cat, witch hat) and Ginger (pirate tabby) share one
large side-scrolling world. Each player joins from their own computer (or browser) over a websocket,
uses the radar to hunt the other down, and scratches until one of them is King of Halloween.

Everything is generated in code: graphics, world, music and sound effects. There are no asset files.

## Running a game

1. **Start the server** (needs [Go](https://go.dev/dl/) 1.22 or newer):
   ```
   cd server
   go run .
   ```
   It listens on port 8080 (`-addr :9000` changes that).
2. **Start two game clients.** Open http://localhost:8080 in two browser tabs, or press F5 in Visual Studio,
   run the .exe, or mix them, on one computer or two.
   Press fire on the title screen to join. The first client is Mortimer, the second is Ginger.
   A third client gets "server full".
3. **Playing on two computers:** start the client with the server's address as its argument
   (or set the `CATS_SERVER` environment variable):
   ```
   TheCatsOfHalloween.exe ws://192.168.0.10:8080/ws
   ```
   In Visual Studio, put the address under Project → Properties → Debugging → Command Arguments.
   You may need to allow port 8080 through the firewall on the server machine.

## Building

### Visual Studio 2026
Open `TheCatsOfHalloween.slnx` and build. raylib is compiled from `vendor/raylib` as a static library
project in the same solution, so you don't need to install anything. Release builds have no console window.

### CMake (Windows / Linux / macOS)
```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

### WebAssembly (Emscripten)
A ready-built copy is already in `server/web/`, so just run the server and open **http://localhost:8080**.
To rebuild it after changing the code (needs the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html)):
```
emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=Release
cmake --build build-web
```
The build copies `TheCatsOfHalloween.html/.js/.wasm` into `server/web/` automatically. The page itself is
`web/shell.html`. The page connects to the websocket of the server it was loaded from, so browser players
and desktop players can play against each other. Click the game once so it gets keyboard focus.
Browsers only start audio after the first key press.

## Deploying
`server/deploy.sh` builds the server for Linux and runs it with the web build on a host over SSH, as the
systemd user service `thecatsofhalloween` (no sudo needed). By default it deploys to
http://runwayperformance.com:6060:
```
make deploy                     # rebuilds the WebAssembly version, then runs server/deploy.sh
make deploy DEPLOY_HOST=user@example.com DEPLOY_PORT=9000
```
`server/deploy.sh` on its own uploads `server/web/` as it is, without rebuilding it.
The Makefile also has `make` (desktop build), `make web`, `make server` (run locally) and `make clean`.

## Controls
| | Move | Jump | Drop through platform | Scratch | Pause | Leave (while paused) |
|---|---|---|---|---|---|---|
| Keyboard | A / D or ← / → | W, ↑ or Space | S/↓ + jump | F, X, Ctrl or Shift | P or Esc | Esc |
| Xbox controller | left stick or D-pad | A (or D-pad up) | stick down + A | X, B, RB or RT | Menu | B or View |

On the title screen, A joins the game. The controller **rumbles** when you get scratched, land a hit, fall into
a pit, get knocked out or win the match. In the desktop build this uses XInput on Windows; in the browser it
uses the Gamepad API (Chrome and Edge). In a browser, press a controller button once after the page loads,
because browsers only reveal a controller after it has been used on the page.
Other keys: **M** music on/off, **F2** scanlines.

**Screen size:** the desktop game starts maximized. **F11** or **Alt+Enter** switches to fullscreen
(borderless, so Alt+Tab is instant). Start straight in fullscreen with `TheCatsOfHalloween.exe --fullscreen`
(this works together with a server address). In the browser the game fills the window. Use **F11**,
**Alt+Enter**, a double-click or the button in the top-right corner for fullscreen. The 320×256 Amiga screen
is blown up by a whole number and then fitted to any screen size, so pixels stay sharp and equally sized.

**Rules:** each cat has 9 lives (hearts). Scratching or jumping on your rival's head costs them a life,
and so does falling into a pit. Every 10 candy corns gives you an extra life. First to 3 knockouts wins.

## Networking
* **Each client owns its own cat.** It simulates its cat at a fixed 60 Hz and sends the state 30 times a second
  (`Cat::Serialize`). The rival's cat is smoothed with dead reckoning (`Cat::TickRemote`).
* **The attacker judges a hit, the victim applies it.** Your client checks whether your scratch or stomp
  connects and sends `HIT`. The rival's client applies the damage, respecting its own invulnerability.
* **The server referees the shared state** (`server/main.go`): two player slots, who ate which candy corn
  (first come, first served; candy respawns after 30 s), knockouts, the score, and the end of the match.
* **Same world everywhere:** the server sends the world seed in `WELCOME`, and the generator uses its own
  deterministic RNG.
* **Protocol:** one space-separated text message per websocket frame, documented at the top of `server/main.go`.
* **Client transport** (`src/Net.cpp`): a small built-in RFC 6455 client on plain sockets for desktop
  (ws:// only), and the browser's WebSocket for the web build.

## The Amiga look and sound
* 320×256 PAL low-res buffer, scaled up with optional scanlines
* 12-bit OCS colour palette, with copper-bar gradients that keep the 4-bit banding
* Dithered moon and shading, parallax hills, a witch on a broom, a sine scroller and a wobbling logo
* Music from a 4-channel ProTracker-style synth (`src/Music.cpp`):
  * 8-bit wavetables without interpolation, a 50 Hz tick and hard-panned Paula stereo
  * an "LED" low-pass filter
  * an original A-minor tune with a haunted organ, a theremin section, chip arpeggios and an echo on the lead

## Code map
| File | What |
|---|---|
| `TheCatsOfHalloween.cpp` | entry point (desktop loop / emscripten main loop), server address |
| `src/Game.*` | states, joining, network messages, the shared view, HUD + radar, title, pause, match over |
| `src/Net.*` | websocket client (desktop sockets / browser WebSocket) |
| `src/World.*` | seeded world generator (graveyard, pumpkin patch, haunted forest, castle ruins), tiles, decor, candy, bats |
| `src/Cat.*` | platform physics (coyote time, jump buffer, one-way platforms), combat, state sync |
| `src/CatSprites.*`, `src/PixelArt.*` | procedural pixel-art sprites with automatic outlines |
| `src/Background.*` | copper sky, moon, parallax layers |
| `src/Music.*` | tracker synth, song data, sound effects |
| `src/Input.*` | `PlayerInput` snapshot and menu buttons (keyboard / Xbox controller) |
| `src/Rumble.*` | controller vibration (XInput on Windows, Gamepad API in the browser) |
| `server/main.go` | Go websocket game server (gorilla/websocket), also serves the web build |
| `server/web/` | the built WebAssembly game (generated) |
| `web/shell.html` | the HTML page around the WebAssembly game |
