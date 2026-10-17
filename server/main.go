// The Cats of Halloween - game server.
//
// Two cats team up in one world to smash every jumping pumpkin in it. Each client simulates
// its own cat and streams its state; the server relays it to the partner and is
// the referee for everything both players care about: candy pickups, smashed
// pumpkins, knocked-out monsters, the magic wand, knockouts and the team score.
//
// The pumpkins and monsters themselves are simulated by one client, the "host" (the player who
// has been on the server longest). The host streams their positions and moods,
// and decides when pumpkins turn angry and where the magic wand appears.
// It also serves the WebAssembly build of the game, so one binary is enough.
//
//	go run . -addr :8080 -web ../build-web
//
// Protocol: one text message per websocket frame, space separated.
//
//	server -> client                      client -> server
//	WELCOME <id> <seed>                   STATE <cat state...>
//	FULL                                  GOAL <pumpkins in the world>
//	JOIN <id> / LEAVE <id>                CANDY <index>
//	HOST <id>                             SMASH <index>
//	STATE <id> <cat state...>             KO <reason...>
//	CANDY <index> <takerId>               WANDGET
//	CANDYBACK <index> / CANDYRESET        BONK <monster index>
//	SMASH <index> <takerId>               host only:
//	PUMPKINRESET                            PUMPKINS <pumpkin state...>
//	PUMPKINS <pumpkin state...>             MONSTERS <monster state...>
//	MONSTERS <monster state...>             WAND <x> <y>
//	BONK <index> <takerId>                  NOWAND
//	MONSTERBACK <index> / MONSTERRESET
//	WAND <x> <y> / NOWAND
//	CALM <takerId>
//	SCORE <smashed> <goal> <kos> <maxKos> <smashedBy0> <smashedBy1>
//	KO <loserId> <reason...>
//	OVER <won 0|1>
package main

import (
	"flag"
	"fmt"
	"log"
	"net/http"
	"os"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/gorilla/websocket"
)

const (
	maxPlayers   = 2
	maxKnockouts = 5 // team knockouts before the pumpkins win
	candyRespawn = 30 * time.Second
	monsterBack  = 20 * time.Second // a knocked-out ghost / skeleton / zombie / witch returns
	writeTimeout = 5 * time.Second
	pingInterval = 20 * time.Second
	pongTimeout  = 60 * time.Second
	maxMessage   = 8192 // a PUMPKINS snapshot can be a few hundred bytes
)

var (
	addr   = flag.String("addr", ":8080", "listen address")
	webDir = flag.String("web", "web", "directory with the WebAssembly build to serve at / (optional)")
	seed   = flag.Uint("seed", 0x0CA75, "world seed sent to clients")
)

var upgrader = websocket.Upgrader{
	ReadBufferSize:  1024,
	WriteBufferSize: 1024,
	// Desktop clients send no Origin; browsers may load the game from elsewhere during development.
	CheckOrigin: func(r *http.Request) bool { return true },
}

type client struct {
	id   int
	conn *websocket.Conn
	send chan string
}

type game struct {
	mu         sync.Mutex
	players    [maxPlayers]*client
	host       int // player id that simulates the pumpkins, -1 = nobody
	taken      map[int]bool
	smashed    map[int]bool
	bonked     map[int]bool // monsters that are knocked out right now
	smashCount int
	goal       int // pumpkins in the world: smash them all to win (the clients tell us)
	smashedBy  [maxPlayers]int
	knockouts  int
	wandActive bool
	wandX      int
	wandY      int
	generation int // bumped on reset so stale respawn timers are ignored
}

func newGame() *game {
	return &game{host: -1, taken: map[int]bool{}, smashed: map[int]bool{}, bonked: map[int]bool{}}
}

// queue a message for one client without blocking the game lock
func (c *client) push(msg string) {
	select {
	case c.send <- msg:
	default:
		log.Printf("player %d: send buffer full, dropping %q", c.id, firstWord(msg))
	}
}

func (g *game) broadcastLocked(msg string) {
	for _, p := range g.players {
		if p != nil {
			p.push(msg)
		}
	}
}

func (g *game) otherLocked(id int) *client { return g.players[1-id] }

func (g *game) scoreLocked() string {
	return fmt.Sprintf("SCORE %d %d %d %d %d %d", g.smashCount, g.goal, g.knockouts, maxKnockouts,
		g.smashedBy[0], g.smashedBy[1])
}

// a fresh round: every pumpkin and candy back, score cleared
func (g *game) resetLocked() {
	g.taken = map[int]bool{}
	g.smashed = map[int]bool{}
	g.bonked = map[int]bool{}
	g.smashCount = 0
	g.smashedBy = [maxPlayers]int{}
	g.knockouts = 0
	g.wandActive = false
	g.generation++
}

// after the given delay, call fn - unless the round was reset in the meantime
func (g *game) laterLocked(d time.Duration, fn func()) {
	gen := g.generation
	time.AfterFunc(d, func() {
		g.mu.Lock()
		defer g.mu.Unlock()
		if g.generation == gen {
			fn()
		}
	})
}

func (g *game) join(conn *websocket.Conn) *client {
	g.mu.Lock()
	defer g.mu.Unlock()
	for id := range g.players {
		if g.players[id] != nil {
			continue
		}
		c := &client{id: id, conn: conn, send: make(chan string, 256)}
		g.players[id] = c
		if g.host < 0 {
			g.host = id
		}
		c.push(fmt.Sprintf("WELCOME %d %d", id, *seed))
		c.push(fmt.Sprintf("HOST %d", g.host))
		c.push(g.scoreLocked())
		for idx := range g.taken {
			c.push(fmt.Sprintf("CANDY %d -1", idx))
		}
		for idx := range g.smashed {
			c.push(fmt.Sprintf("SMASH %d -1", idx))
		}
		for idx := range g.bonked {
			c.push(fmt.Sprintf("BONK %d -1", idx))
		}
		if g.wandActive {
			c.push(fmt.Sprintf("WAND %d %d", g.wandX, g.wandY))
		}
		if o := g.otherLocked(id); o != nil {
			c.push(fmt.Sprintf("JOIN %d", o.id))
			o.push(fmt.Sprintf("JOIN %d", id))
		}
		log.Printf("player %d joined from %s", id, conn.RemoteAddr())
		return c
	}
	return nil
}

func (g *game) leave(c *client) {
	g.mu.Lock()
	defer g.mu.Unlock()
	if g.players[c.id] != c {
		return
	}
	g.players[c.id] = nil
	close(c.send)
	g.broadcastLocked(fmt.Sprintf("LEAVE %d", c.id))
	if g.players[0] == nil && g.players[1] == nil {
		g.host = -1
		g.resetLocked() // empty server: fresh game for the next visitors
	} else if g.host == c.id {
		g.host = 1 - c.id // the partner takes over the pumpkins where they are
		g.broadcastLocked(fmt.Sprintf("HOST %d", g.host))
	}
	log.Printf("player %d left", c.id)
}

func (g *game) handle(c *client, msg string) {
	cmd, rest, _ := strings.Cut(msg, " ")
	g.mu.Lock()
	defer g.mu.Unlock()
	isHost := c.id == g.host

	switch cmd {
	case "STATE":
		if o := g.otherLocked(c.id); o != nil {
			o.push(fmt.Sprintf("STATE %d %s", c.id, rest))
		}

	case "PUMPKINS", "MONSTERS":
		// the host's pumpkin / monster snapshot, relayed as-is to the partner
		if !isHost {
			return
		}
		if o := g.otherLocked(c.id); o != nil {
			o.push(cmd + " " + rest)
		}

	case "BONK":
		idx, err := strconv.Atoi(strings.TrimSpace(rest))
		if err != nil || idx < 0 || idx > 100000 || g.bonked[idx] {
			return // already knocked out
		}
		g.bonked[idx] = true
		g.broadcastLocked(fmt.Sprintf("BONK %d %d", idx, c.id))
		g.laterLocked(monsterBack, func() {
			if g.bonked[idx] {
				delete(g.bonked, idx)
				g.broadcastLocked(fmt.Sprintf("MONSTERBACK %d", idx))
			}
		})

	case "CANDY":
		idx, err := strconv.Atoi(strings.TrimSpace(rest))
		if err != nil || idx < 0 || idx > 100000 || g.taken[idx] {
			return // already eaten by the partner - first come, first served
		}
		g.taken[idx] = true
		g.broadcastLocked(fmt.Sprintf("CANDY %d %d", idx, c.id))
		g.laterLocked(candyRespawn, func() {
			if g.taken[idx] {
				delete(g.taken, idx)
				g.broadcastLocked(fmt.Sprintf("CANDYBACK %d", idx))
			}
		})

	case "SMASH":
		idx, err := strconv.Atoi(strings.TrimSpace(rest))
		if err != nil || idx < 0 || idx > 100000 || g.smashed[idx] {
			return // the other cat got there first
		}
		g.smashed[idx] = true
		g.smashCount++
		g.smashedBy[c.id]++
		g.broadcastLocked(fmt.Sprintf("SMASH %d %d", idx, c.id))
		g.broadcastLocked(g.scoreLocked())
		if g.goal > 0 && g.smashCount >= g.goal {
			g.overLocked(true) // not a single pumpkin left
		}

	case "GOAL":
		// every client counts the pumpkins of the (seeded, identical) world after joining
		n, err := strconv.Atoi(strings.TrimSpace(rest))
		if err != nil || n <= 0 || n > 100000 || n == g.goal {
			return
		}
		g.goal = n
		g.broadcastLocked(g.scoreLocked())

	case "WAND":
		// the host saw pumpkins turn angry and hid a magic wand somewhere
		f := strings.Fields(rest)
		if !isHost || g.wandActive || len(f) != 2 {
			return
		}
		x, err1 := strconv.Atoi(f[0])
		y, err2 := strconv.Atoi(f[1])
		if err1 != nil || err2 != nil {
			return
		}
		g.wandActive, g.wandX, g.wandY = true, x, y
		g.broadcastLocked(fmt.Sprintf("WAND %d %d", x, y))

	case "NOWAND":
		// no angry pumpkins left (all respawned calm), the wand fades away
		if isHost && g.wandActive {
			g.wandActive = false
			g.broadcastLocked("NOWAND")
		}

	case "WANDGET":
		if !g.wandActive {
			return // the partner grabbed it first
		}
		g.wandActive = false
		g.broadcastLocked(fmt.Sprintf("CALM %d", c.id))

	case "KO":
		// a knocked-out cat reports it; it costs the whole team
		reason := strings.TrimSpace(rest)
		if reason == "" {
			reason = "WAS KNOCKED OUT"
		}
		g.knockouts++
		g.broadcastLocked(fmt.Sprintf("KO %d %s", c.id, reason))
		g.broadcastLocked(g.scoreLocked())
		if g.knockouts >= maxKnockouts {
			g.overLocked(false)
		}
	}
}

// the round is over: tell everybody, then set up a fresh one
func (g *game) overLocked(won bool) {
	w := 0
	if won {
		w = 1
	}
	g.broadcastLocked(fmt.Sprintf("OVER %d", w))
	g.resetLocked()
	g.broadcastLocked(g.scoreLocked())
	g.broadcastLocked("CANDYRESET")
	g.broadcastLocked("PUMPKINRESET")
	g.broadcastLocked("MONSTERRESET")
	g.broadcastLocked("NOWAND")
	log.Printf("round over, team won: %v", won)
}

func firstWord(s string) string {
	w, _, _ := strings.Cut(s, " ")
	return w
}

func (g *game) serveWS(w http.ResponseWriter, r *http.Request) {
	conn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Printf("upgrade: %v", err)
		return
	}
	c := g.join(conn)
	if c == nil {
		conn.SetWriteDeadline(time.Now().Add(writeTimeout))
		conn.WriteMessage(websocket.TextMessage, []byte("FULL"))
		conn.WriteMessage(websocket.CloseMessage, websocket.FormatCloseMessage(websocket.ClosePolicyViolation, "server full"))
		conn.Close()
		return
	}
	go c.writeLoop()
	c.readLoop(g)
}

func (c *client) readLoop(g *game) {
	defer func() {
		g.leave(c)
		c.conn.Close()
	}()
	c.conn.SetReadLimit(maxMessage)
	c.conn.SetReadDeadline(time.Now().Add(pongTimeout))
	c.conn.SetPongHandler(func(string) error {
		c.conn.SetReadDeadline(time.Now().Add(pongTimeout))
		return nil
	})
	for {
		_, data, err := c.conn.ReadMessage()
		if err != nil {
			return
		}
		c.conn.SetReadDeadline(time.Now().Add(pongTimeout))
		g.handle(c, string(data))
	}
}

func (c *client) writeLoop() {
	ticker := time.NewTicker(pingInterval)
	defer func() {
		ticker.Stop()
		c.conn.Close()
	}()
	for {
		select {
		case msg, ok := <-c.send:
			c.conn.SetWriteDeadline(time.Now().Add(writeTimeout))
			if !ok {
				c.conn.WriteMessage(websocket.CloseMessage, []byte{})
				return
			}
			if err := c.conn.WriteMessage(websocket.TextMessage, []byte(msg)); err != nil {
				return
			}
		case <-ticker.C:
			c.conn.SetWriteDeadline(time.Now().Add(writeTimeout))
			if err := c.conn.WriteMessage(websocket.PingMessage, nil); err != nil {
				return
			}
		}
	}
}

func main() {
	flag.Parse()
	g := newGame()

	mux := http.NewServeMux()
	mux.HandleFunc("/ws", g.serveWS)
	if st, err := os.Stat(*webDir); err == nil && st.IsDir() {
		files := http.FileServer(http.Dir(*webDir))
		mux.Handle("/", http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			if r.URL.Path == "/" {
				r.URL.Path = "/TheCatsOfHalloween.html"
			}
			files.ServeHTTP(w, r)
		}))
		log.Printf("serving web build from %s", *webDir)
	} else {
		mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
			fmt.Fprintln(w, "The Cats of Halloween server. Websocket endpoint: /ws")
		})
	}

	log.Printf("The Cats of Halloween server listening on %s (websocket: /ws)", *addr)
	log.Fatal(http.ListenAndServe(*addr, mux))
}
