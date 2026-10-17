# The Cats of Halloween

A two-player **online co-op** Amiga-style Halloween platformer in C++20 and [raylib 5.5](https://www.raylib.com),
with a game server written in Go. Mortimer (black cat, witch hat) and Ginger (pirate tabby) start side by side
in one large side-scrolling world. Each player joins from their own computer (or browser) over a websocket,
and together they smash the jumping pumpkins. When pumpkins get angry and start chasing them, the cats must
race to a magic wand to calm them down. You can also play alone; a partner can join at any time.

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
   A third client gets "server full". After changing the protocol (as in this version) rebuild the
   web build too, so browser and desktop clients speak the same language.
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

## Controls
| | Move | Jump | Drop through platform | Scratch / smash | Pause | Leave (while paused) |
|---|---|---|---|---|---|---|
| Keyboard | A / D or ← / → | W, ↑ or Space | S/↓ + jump | F, X, Ctrl or Shift | P or Esc | Esc |
| Xbox controller | left stick or D-pad | A (or D-pad up) | stick down + A | X, B, RB or RT | Menu | B or View |

On the title screen, A joins the game. The controller **rumbles** when a pumpkin bites you, you smash one, fall into
a pit, get knocked out, grab the magic wand or win the round. In the desktop build this uses XInput on Windows; in the browser it
uses the Gamepad API (Chrome and Edge). In a browser, press a controller button once after the page loads,
because browsers only reveal a controller after it has been used on the page.
Other keys: **M** music on/off, **F2** scanlines.

**Screen size:** the desktop game starts maximized. **F11** or **Alt+Enter** switches to fullscreen
(borderless, so Alt+Tab is instant). Start straight in fullscreen with `TheCatsOfHalloween.exe --fullscreen`
(this works together with a server address). In the browser the game fills the window. Use **F11**,
**Alt+Enter**, a double-click or the button in the top-right corner for fullscreen. The 320×256 Amiga screen
is blown up by a whole number and then fitted to any screen size, so pixels stay sharp and equally sized.

**Rules:**
* The team wins by smashing **every pumpkin in the world** together: the ones jumping around and the ones
  just sitting in the patch (31 in the default world). Scratch a pumpkin or jump on it to smash it. Smashed pumpkins stay gone. The top of the screen shows the
  team total, the HUD shows how many each cat smashed. When only 3 are left, an orange arrow points to the
  nearest one, and the radar shows them all.
* Every 20-30 seconds a few pumpkins near the cats **get angry** (sitting ones wake up too): they turn red, steam and chase the
  nearest cat. Touching one costs a life, but you can still smash them if you're brave and quick.
  The longer the mob stays angry, the more pumpkins join it.
* At the same moment a **magic wand** appears 40-80 m away, with a beam of light, a yellow arrow on the edge of
  the screen and a star on the radar. Whoever reaches it first calms every angry pumpkin at once.
  If you smash the whole mob before anyone reaches it, the wand fades away.
* Each cat has 9 lives (hearts). Pumpkin bites and pits cost a life; every 10 candy corns gives one back.
  A cat that loses all 9 is knocked out and gets back up next to its partner.
  **5 knockouts** for the team and the pumpkins win the round.
* **Monsters** haunt the world. Touching one costs a life; a scratch or a jump on its head knocks it out
  for 20 seconds. They don't count towards the goal.
  * **Ghosts** float through walls towards you. Every few seconds they fade out, and then they're harmless
    (and can't be hit).
  * **Skeletons** patrol back and forth. **Zombies** shuffle around and slowly come after a cat that is near.
  * **Witches** fly high on their brooms and swoop down at a cat right below them, cackling.
* **Teamwork:** land on your partner's head for a super jump to reach high platforms.

## Networking
* **Each client owns its own cat.** It simulates its cat at a fixed 60 Hz and sends the state 30 times a second
  (`Cat::Serialize`). The rival's cat is smoothed with dead reckoning (`Cat::TickRemote`).
* **One client simulates the pumpkins and monsters.** The server names a *host* (the player who has been on the
  server longest, `HOST`). The host hops the pumpkins around, decides when they get angry and where the wand
  appears, and streams compact snapshots 20 times a second (`PUMPKINS`, `MONSTERS`). The other client smooths it.
  If the host leaves, the partner takes over the pumpkins from the last snapshot.
* **Each client judges its own cat.** It checks its own scratches and stomps against the pumpkins (`SMASH`)
  and monsters (`BONK`), bites and scares, and touching the wand (`WANDGET`).
* **The server referees the shared state** (`server/main.go`): two player slots, the host, who ate which
  candy corn (it grows back after 30 s) and who smashed which pumpkin (first come, first served), the
  wand, the team score, knockouts and the end of the round. Each client counts the pumpkins of the
  generated world and tells the server (`GOAL`), so it knows when the last one is gone.
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
| `src/Game.*` | states, joining, network messages, pumpkin moods + magic wand, HUD + radar, title, pause, round over |
| `src/Net.*` | websocket client (desktop sockets / browser WebSocket) |
| `src/World.*` | seeded world generator (graveyard, pumpkin patch, haunted forest, castle ruins), tiles, decor, candy, bats, where the pumpkins live |
| `src/Cat.*` | platform physics (coyote time, jump buffer, one-way platforms), scratching, state sync |
| `src/Pumpkins.*` | the jumping pumpkins: hopping and chasing, snapshot sync, sprites |
| `src/Monsters.*` | ghosts, skeletons, zombies and witches: behaviour, snapshot sync, sprites |
| `src/CatSprites.*`, `src/PixelArt.*` | procedural pixel-art sprites with automatic outlines |
| `src/Background.*` | copper sky, moon, parallax layers |
| `src/Music.*` | tracker synth, song data, sound effects |
| `src/Input.*` | `PlayerInput` snapshot and menu buttons (keyboard / Xbox controller) |
| `src/Rumble.*` | controller vibration (XInput on Windows, Gamepad API in the browser) |
| `server/main.go` | Go websocket game server (gorilla/websocket), also serves the web build |
| `server/web/` | the built WebAssembly game (generated) |
| `web/shell.html` | the HTML page around the WebAssembly game |
