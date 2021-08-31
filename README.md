[//]: <>        (Open this document with a)
[comment]: <>   (markdown reader to get the)
[//]: #         (best reading experience)

# RFC913 C Implementation

## 0. Structure

    root
        ├── client
        │   ├── app         # can be used for append test
        │   ├── client.c    # client source code
        │   ├── empty       # general file
        │   └── rmF         # general file
        ├── server
        │   ├── apps        # can be used for append test
        │   ├── empty       # general file
        │   ├── rms         # general file
        │   ├── server.c    # server source code
        │   └── srm         # general file
        ├── unixRun.sh      # see section 3
        ├── winRun.bat      # see section 3
        └── README.md       # readme file, this document

## 1. Compilation

### Linux

- Server: `gcc server.c -o server`
- Client: `gcc client.c -o client`

### Cygwin Environment (Windows)

> *cygwin env must be in the path, otherwise run following command in cygwin terminal*

- Server: `gcc server.c -o server.exe`
- Client: `gcc client.c -o client.exe`

---

## 2. Run

> Go to the directory which contains the programs, running in Windows CMD needs to remove "./"

- Server: `./server [port]`
  - *e.g: `./server 5005`*
- Client: `./client [serverName/address] [port]`
  - *e.g: `./client localhost 5005` or `./client 127.0.0.1 5005`*

---

## 3. Use shell/batch script

> Go to the root directory

- unixRun.sh: unix system only, with terminal application installed
  - Open terminal, execute `chmod +x unnixRun.sh && ./unixRun.sh`

- winRun.bat: windows system with **cygwin env in path**
  - Double click the batch file to run

## 4. Usage

> The file transfer protocol described in RFC 913 (<https://tools.ietf.org/html/rfc913>)
>
>> *Because arguments are sparerated by space, the program does not support the path that contains space character*

      <command> : = <cmd> [<SPACE> <args>] <NULL>

      <cmd> : =  USER ! ACCT ! PASS ! TYPE ! LIST ! CDIR
                 KILL ! NAME ! DONE ! RETR ! STOR

      <response> : = <response-code> [<message>] <NULL>

      <response-code> : =  + | - |   | !

      <message> can contain <CRLF>

### Users

- There are three users avaliable in this project, in which **admin is the root user** who does not need to enter password to login and change directory.

- In contrast, **user1 and user2 are standard users**, when changing the working directory, re-entering account and password is required.

    | USER  | ACCT  |  PASS |
    | ----- | :---: | ----: |
    | admin | admin | admin |
    | user1 | user1 | user1 |
    | user2 | user2 | user2 |

### Protection

- The program has password protection, which means you need to login to perform certain actions
  - You only can send following commands if you are not login
    1. `DONE`
    2. `USER`
    3. `ACCT`
    4. `PASS`
  - Re-login will be required after perform `CDIR` without using root user

## 5. Example

> The following is an example of file transfer

        Simple FTP Server, Enter Ctrl+C and press Enter to exit
        >SFTP Server：+You are connected, greeting from [serverName]
        >SFTP Client: USER user1
        >SFTP Server：+User-id valid, send account and password
        >SFTP Client: ACCT user1
        >SFTP Server：+Account valid, send password
        >SFTP Client: PASS user2
        >SFTP Server：-Wrong password, try again
        >SFTP Client: PASS user1
        >SFTP Server：!Logged in
        >SFTP Client: TYPE B
        >SFTP Server：+Using Binary mode
        >SFTP Client: LIST F
        >SFTP Server：+Listing Files in </home/username/sftp/server/>
                        apps
                        empty
                        rms
                        server
                        server.c
                        srm
        >SFTP Client: CDIR ../client
        >SFTP Server：+directory ok, send account/password
        >SFTP Client: ACCT admin
        >SFTP Server：!Changed working dir to </home/username/sftp/client/>
        >SFTP Client: LIST F
        >SFTP Server：+Listing Files in </home/username/sftp/client/>
                        app
                        client
                        client.c
                        empty
                        rmF
        >SFTP Client: KILL rmF
        >SFTP Server：+<rmF> deleted
        >SFTP Client: NAME empty
        >SFTP Server：+File exists
        >SFTP Client: TOBE empty_new
        >SFTP Server：+<empty> renamed to <empty_new>
        >SFTP Client: LIST V
        >SFTP Server：+Listing Files in </home/username/sftp/client/>
                        total 40
                        -rw-rw-r-- 1 user1 user1     9 Aug 28 19:27 app
                        -rwxrwxr-x 1 user1 user1 18288 Aug 31 05:09 client
                        -rw-rw-r-- 1 user1 user1 13880 Aug 31 05:12 client.c
                        -rw-rw-r-- 1 user1 user1     0 Aug 28 19:26 empty
        >SFTP Client: CDIR ../server
        >SFTP Server：!Changed working dir to </home/username/sftp/server/>
        >SFTP Client: RETR rms
        >SFTP Server：6
        >SFTP Client: SEND
        >SFTP Client: RETR app
        >SFTP Server：-File doesn’t exist
        >SFTP Client: LIST F
        >SFTP Server：+Listing Files in </home/username/sftp/server/>
                        apps
                        empty_new
                        rms
                        server
                        server.c
                        srm
        >SFTP Client: RETR apps
        >SFTP Server：7
        >SFTP Client: STOP
        >SFTP Server：+ok, RETR aborted
        >SFTP Client: STOR NEW app
        >SFTP Server：+File does not exist, will create new file
        >SFTP Client: SIZE [fileSize]
        >SFTP Server：+ok, waiting for file
        >SFTP Server：+Saved </home/username/sftp/server/app>
        >SFTP Client: DONE
        >SFTP Server：+[serverName] closing connection

        == EXIT ==

## 6. Debug commands

> Display the process running on the specified port

- Linux
    1. `netstat -ano | grep port`
    2. `lsof -i :port`
- Windows
    1. `netstat -ano`

After find the PID of the process, you can stop the program by using kill command

> Kill the process

- Linux
  - `kill -9 [PID]`
- Windows
  - `kill -f [PID]`
