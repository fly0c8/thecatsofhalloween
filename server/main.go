// The Cats of Halloween - game server.
//
// Two cats share one world. Each client simulates its own cat and streams its
// state; the server relays that state to the rival and is the referee for
// everything both players care about: candy pickups, knockouts and the score.
// It also serves the WebAssembly build of the game, so one binary is enough.
//
//	go run . -addr :8080 -web ../build-web
//
// Protocol: one text message per websocket frame, space separated.
//
//	server -> client                      client -> server
//	WELCOME <id> <seed>                   STATE <cat state...>
//	FULL                                  HIT <victimId> <fromX>
//	JOIN <id> / LEAVE <id>                CANDY <index>
//	STATE <id> <cat state...>             KO <reason...>
//	HIT <attackerId> <victimId> <fromX>
//	CANDY <index> <takerId>
//	CANDYBACK <index> / CANDYRESET
//	SCORE <wins0> <wins1>
//	KO <loserId> <reason...>
//	MATCH <winnerId>
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
	winsNeeded   = 3
	candyRespawn = 30 * time.Second
	writeTimeout = 5 * time.Second
	pingInterval = 20 * time.Second
	pongTimeout  = 60 * time.Second
	maxMessage   = 1024
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
	wins       [maxPlayers]int
	taken      map[int]bool
	generation int // bumped on reset so stale candy respawn timers are ignored
}

func newGame() *game { return &game{taken: map[int]bool{}} }

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

func (g *game) scoreLocked() string { return fmt.Sprintf("SCORE %d %d", g.wins[0], g.wins[1]) }

func (g *game) resetLocked() {
	g.wins = [maxPlayers]int{}
	g.taken = map[int]bool{}
	g.generation++
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
		c.push(fmt.Sprintf("WELCOME %d %d", id, *seed))
		c.push(g.scoreLocked())
		for idx := range g.taken {
			c.push(fmt.Sprintf("CANDY %d -1", idx))
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
		g.resetLocked() // empty server: fresh match for the next visitors
	}
	log.Printf("player %d left", c.id)
}

func (g *game) handle(c *client, msg string) {
	cmd, rest, _ := strings.Cut(msg, " ")
	g.mu.Lock()
	defer g.mu.Unlock()

	switch cmd {
	case "STATE":
		if o := g.otherLocked(c.id); o != nil {
			o.push(fmt.Sprintf("STATE %d %s", c.id, rest))
		}

	case "HIT":
		// HIT <victimId> <fromX>: the victim's client decides if it really hurts (invulnerability etc.)
		f := strings.Fields(rest)
		if len(f) != 2 {
			return
		}
		victim, err := strconv.Atoi(f[0])
		if err != nil || victim == c.id || victim < 0 || victim >= maxPlayers || g.players[victim] == nil {
			return
		}
		if _, err := strconv.ParseFloat(f[1], 64); err != nil {
			return
		}
		g.players[victim].push(fmt.Sprintf("HIT %d %d %s", c.id, victim, f[1]))

	case "CANDY":
		idx, err := strconv.Atoi(strings.TrimSpace(rest))
		if err != nil || idx < 0 || idx > 100000 || g.taken[idx] {
			return // already eaten by the rival - first come, first served
		}
		g.taken[idx] = true
		g.broadcastLocked(fmt.Sprintf("CANDY %d %d", idx, c.id))
		gen := g.generation
		time.AfterFunc(candyRespawn, func() {
			g.mu.Lock()
			defer g.mu.Unlock()
			if g.generation == gen && g.taken[idx] {
				delete(g.taken, idx)
				g.broadcastLocked(fmt.Sprintf("CANDYBACK %d", idx))
			}
		})

	case "KO":
		// the knocked-out cat reports it; the rival (if any) gets the point
		reason := strings.TrimSpace(rest)
		if reason == "" {
			reason = "WAS KNOCKED OUT"
		}
		g.broadcastLocked(fmt.Sprintf("KO %d %s", c.id, reason))
		winner := 1 - c.id
		if g.players[winner] == nil {
			return
		}
		g.wins[winner]++
		g.broadcastLocked(g.scoreLocked())
		if g.wins[winner] >= winsNeeded {
			g.broadcastLocked(fmt.Sprintf("MATCH %d", winner))
			g.resetLocked()
			g.broadcastLocked(g.scoreLocked())
			g.broadcastLocked("CANDYRESET")
			log.Printf("match won by player %d", winner)
		}
	}
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
