# Convenience targets around CMake, Emscripten and the Go server (see README.md).
#
#   make           desktop build (build/)
#   make web       WebAssembly build, copied into server/web/ (needs the Emscripten SDK)
#   make server    run the game server locally on :8080
#   make deploy    build the WebAssembly version and deploy it with the server (server/deploy.sh)
#                  DEPLOY_HOST and DEPLOY_PORT reach the script from the command line or environment, e.g. make deploy DEPLOY_PORT=9000

BUILD_TYPE ?= Release

.PHONY: all desktop web server deploy clean

all: desktop

desktop:
	cmake -B build -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)
	cmake --build build --config $(BUILD_TYPE)

web:
	emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)
	cmake --build build-web

server:
	cd server && go run .

deploy: web
	server/deploy.sh

clean:
	rm -rf build build-web
