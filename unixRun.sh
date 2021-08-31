#!/bin/bash
gcc client/client.c -o client/client && gcc server/server.c -o server/server

terminal -e ./server/server 5005

clear
./client/client localhost 5005
