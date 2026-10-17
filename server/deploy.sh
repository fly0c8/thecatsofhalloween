#!/usr/bin/env bash
# Deploys the game server and the web build to a Linux host over SSH.
#
#   server/deploy.sh                      # runwayperformance.com, port 6060
#   DEPLOY_HOST=user@example.com DEPLOY_PORT=9000 server/deploy.sh
#
# Builds a static linux/amd64 binary locally (needs Go), copies it with server/web/ to
# ~/thecatsofhalloween on the host, and runs it as the systemd user service
# thecatsofhalloween.service. No sudo needed on the host, but the user needs lingering
# enabled (loginctl enable-linger) so the service keeps running after logout.
# The web build is uploaded as it is in server/web/; rebuild it with Emscripten first if needed.
set -euo pipefail

HOST="${DEPLOY_HOST:-runwayperformance.com}"
PORT="${DEPLOY_PORT:-6060}"
APP=thecatsofhalloween

cd "$(dirname "$0")"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

echo "Building server for linux/amd64..."
CGO_ENABLED=0 GOOS=linux GOARCH=amd64 go build -trimpath -ldflags="-s -w" -o "$tmp/$APP" .

echo "Uploading to $HOST..."
ssh "$HOST" "mkdir -p ~/$APP/web ~/.config/systemd/user"
# Upload next to the running binary, then swap it in (overwriting a running executable fails).
scp -q "$tmp/$APP" "$HOST:$APP/$APP.new"
scp -q web/* "$HOST:$APP/web/"
scp -q "$APP.service" "$HOST:.config/systemd/user/"

echo "Restarting service..."
ssh "$HOST" "set -e
	mv ~/$APP/$APP.new ~/$APP/$APP
	echo CATS_PORT=$PORT > ~/$APP/env
	systemctl --user daemon-reload
	systemctl --user enable $APP.service
	systemctl --user restart $APP.service
	sleep 1
	systemctl --user is-active $APP.service"

hostname="${HOST#*@}"
echo "Deployed: http://$hostname:$PORT"
