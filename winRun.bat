gcc client\client.c -o client\client
gcc server\server.c -o server\server
cd server
start server 5005
clear
cd ..\client && client localhost 5005

PAUSE
