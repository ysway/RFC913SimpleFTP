#!/bin/bash
gcc client/client.c -o client/client && gcc server/server.c -o server/server

cd server && ./server 5005 &>/dev/null &

pwd
cd client && ./client localhost 5005
