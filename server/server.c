#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef __linux__
    #include <sys/sendfile.h>
#endif

struct Users {
   char name[8];
   char passwd[8];
   char root;
   struct Users *prevUser;
   struct Users *nextUser;
};

struct Users *getUserByName(struct Users *head, const char userName[])
{
    while(1)
    {   
        if (!strcmp(head->name, userName)) return head;
        head = head->nextUser;
    }
    return NULL;
}

const char checkUserName(struct Users *head, const char userName[])
{
    while(head)
    {
        if (!strcmp(head->name, userName)) return 1;
        head = head->nextUser;
    }
    return 0;
}

const char checkPassword(struct Users *head, struct Users *user, const char passwd[])
{
    if (user)
    {
        return !strcmp(user->passwd, passwd); // if user name and password matched then return 1 otherwise return 0
    } else {
        while(head)
        {
            if (!strcmp(head->passwd, passwd)) return 2; // +Send account
            head = head->nextUser;
        }
        return 0; // -Wrong password, try again
    }
}

const char checkFileValidity(const char path[])
{
    if (!strlen(path)) return 0;
    FILE *fp;
    char buffer[1024];
    sprintf(buffer, "test -f %s && echo $?", path);
    fp = popen(buffer, "r");
    memset(buffer, 0, sizeof(buffer));
    fgets(buffer, sizeof(buffer), fp);
    if (strlen(buffer) && strcmp(buffer, "0")) return 1;
    else return 0;
}

const char dirHandle(char workPath[], const char userArgs[])
{
    FILE *fp;
    char tmpWP[1024], tmpUA[1024];
    char pathBuffer[1024]; char CmdBuffer[1024]; char resultBuffer[1024];
    strcpy(tmpWP, workPath); strcpy(tmpUA, userArgs);
    if (strncmp(tmpUA, "/", 1) && strncmp(tmpUA, "~", 1) && strncmp(tmpUA, "--", 2))
    {
        strcpy(pathBuffer, tmpWP);
        strcat(pathBuffer, tmpUA);
    } else strcpy(pathBuffer, tmpUA);
    sprintf(CmdBuffer, "cd %s && pwd", pathBuffer);
    fp = popen(CmdBuffer, "r");
    fgets(resultBuffer, sizeof(resultBuffer), fp);
    if (!strlen(resultBuffer))
    {
        memset(tmpWP, 0, sizeof(tmpWP)); memset(tmpUA, 0, sizeof(tmpUA));
        memset(pathBuffer, 0, sizeof(pathBuffer)); memset(CmdBuffer, 0, sizeof(CmdBuffer));
        memset(resultBuffer, 0, sizeof(resultBuffer));
        return 0;
    }
    else
    {
        if (strlen(resultBuffer) == 2) resultBuffer[strlen(resultBuffer)-1] = 0;
        else resultBuffer[strlen(resultBuffer)-1] = '/';
        strcpy(workPath, resultBuffer);
        memset(tmpWP, 0, sizeof(tmpWP)); memset(tmpUA, 0, sizeof(tmpUA));
        memset(pathBuffer, 0, sizeof(pathBuffer)); memset(CmdBuffer, 0, sizeof(CmdBuffer));
        memset(resultBuffer, 0, sizeof(resultBuffer));
        return 1;
    }
}

void newFileNameGen(char path[])
{
    // if file exist, add _new and check the new name
    if (checkFileValidity(path))
    {
        strcat(path, "_new");
        newFileNameGen(path);
    }
    else return;
}

int main(int argc,char *argv[])
{
    if (argc!=2)
    {
        puts("Usage: ./server port\nExample:./server 5005\n"); return -1;
    }
    puts("Simple FTP Server, Press Ctrl+C to exit");

    // Create admin, users
    struct Users admin;
    struct Users *user1 = (struct Users *) malloc(sizeof(struct Users));
    struct Users *user2 = (struct Users *) malloc(sizeof(struct Users));

    strcpy(admin.name, "admin"); strcpy(admin.passwd, "admin");
    admin.root = 1;
    admin.prevUser = NULL;
    admin.nextUser = user1;

    strcpy(user1->name, "user1"); strcpy(user1->passwd, "user1");
    user1->root = 0;
    user1->prevUser = &admin;
    user1->nextUser = user2;

    strcpy(user2->name, "user2"); strcpy(user2->passwd, "user2");
    user2->root = 0;
    user2->prevUser = user1;
    user2->nextUser = NULL;
    
    // Create socket of server
    int listenfd;
    if ( (listenfd = socket(AF_INET,SOCK_STREAM,0))==-1) { perror("socket"); return -1; }

    // Bind address and port of server
    struct sockaddr_in servaddr;
    memset(&servaddr,0,sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY); // Any IP address
    //servaddr.sin_addr.s_addr = inet_addr("192.168.190.134"); // Specify the IP address
    servaddr.sin_port = htons(atoi(argv[1])); // Define the port
    if (bind(listenfd,(struct sockaddr *)&servaddr,sizeof(servaddr)) != 0 )
    { perror("bind"); close(listenfd); return -1; }

    // Set socket to listening mode
    if (listen(listenfd,5) != 0 ) { perror("listen"); close(listenfd); return -1; }
    
    while(1)
    {
        // Accept incoming connection
        int clientfd; // Client socket。
        int socklen=sizeof(struct sockaddr_in);
        struct sockaddr_in clientaddr; // Client address
        clientfd=accept(listenfd,(struct sockaddr *)&clientaddr,(socklen_t*)&socklen);
        printf("=== Client (%s) is Connected ===\n", inet_ntoa(clientaddr.sin_addr));
        
        // Flags and temp variables
        int fd, iret;
        long offset;
        long remainData; // SOTR & SIZE
        unsigned long sentBytes = 0;
        char loginVerification = 0; // 0: Not login, 1: Login successfully
        char cdirFlag = 0; // USER & ACCT & PASS & CDIR
        char absolutePath = 0; // NAME & TOBE
        char retrFound = 0; // RETR & SEND
        char fileExist = 0; // STOR
        char currentUser[8] = {};
        char currentPass[8] = {};
        char lsPwd[BUFSIZ]; // used for LIST command
        char workPwd[BUFSIZ]; // Saving cwd dir
        char oldPwd[BUFSIZ]; // Saving previous cwd, used for "-"
        char messageBuffer[BUFSIZ];
        char tmpPathBuffer[BUFSIZ];
        char renameBuffer[BUFSIZ]; // TOBE
        char oldFileNameBuffer[BUFSIZ]; // NAME & TOBE, Also use as flag
        char rsFilePathBuffer[BUFSIZ]; // RETR & SEND
        char rsFileSize[BUFSIZ];
        char storFilePathBuffer[BUFSIZ]; // STOR & SIZE
        char freeSpace[BUFSIZ]; // SIZE
        char storMode[8] = {}; // STOR
        char pipeArgs[BUFSIZ];
        char *p; // general pointer
        char *userArgs;
        struct stat fileStat;
        FILE *pathFp;

        // get cwd
        getcwd(lsPwd, sizeof(lsPwd));
        lsPwd[strlen(lsPwd)] = '/';
        strcpy(workPwd, lsPwd); strcpy(oldPwd, lsPwd);
        // Greeting
        char *hostname = (char*) calloc(1024, sizeof(char));
        gethostname(hostname, sizeof(hostname));
        sprintf(messageBuffer, "+You are connected, greeting from %s", hostname);
        if ( (iret=send(clientfd,messageBuffer,strlen(messageBuffer),0))<=0) { perror("send"); break; }
        puts("Greeting Message Sent");

        // Communication
        while (1)
        {
            memset(messageBuffer, 0, sizeof(messageBuffer));
            if ((iret=recv(clientfd, messageBuffer, sizeof(messageBuffer), 0)) <= 0) // Accept incoming message
            {
                printf("iret=%d\n", iret); break;
            }
            printf("Receive: %s\n", messageBuffer);

            // Get first argument
            userArgs = strtok(messageBuffer," \r\n\t");

            if (!strcmp(userArgs, "DONE"))
            {
                sprintf(messageBuffer, "+%s closing connection", hostname);
                if ( (iret=send(clientfd,messageBuffer,strlen(messageBuffer),0))<=0) { perror("send"); break; }
                printf("Send: %s\n\n=== Client (%s) Exit ===\n", messageBuffer, inet_ntoa(clientaddr.sin_addr));
                free(hostname);
                break;
            }

            // Process message ==============================================================================
            // If not login
            if (!loginVerification)
            {
                // USER
                if (!strcmp(userArgs, "USER") && !cdirFlag)
                {
                    // Get the second argument
                    userArgs = strtok(NULL, " \r\n\t");
                    if (userArgs)
                    {
                        // Check if username is valid
                        if (checkUserName(&admin, userArgs))
                        {
                            // Check root status
                            if (getUserByName(&admin, userArgs)->root)
                            {
                                strcpy(currentUser, userArgs);
                                sprintf(messageBuffer, "!<%s> logged in", userArgs);
                                loginVerification = 1;
                            } else {
                                strcpy(messageBuffer, "+User-id valid, send account and password");
                                loginVerification = 0;
                            }    
                        } else {
                            strcpy(messageBuffer, "-Invalid user-id, try again");
                            loginVerification = 0;
                        }
                    }
                    else strcpy(messageBuffer, "-Invalid user-id, try again");
                }
                // ACCT
                else if (!strcmp(userArgs, "ACCT"))
                {
                    // Get the second argument
                    userArgs = strtok(NULL, " \r\n\t");
                    if (userArgs)
                    {
                        // Check if username is valid
                        if (checkUserName(&admin, userArgs))
                        {   
                            // If passwd was entered
                            if (strlen(currentPass))
                            {
                                // Check if the current username matches saved passwd
                                if (!strcmp(getUserByName(&admin, userArgs)->passwd, currentPass))
                                {
                                    // Save the current user
                                    strcpy(currentUser, userArgs);
                                    if (cdirFlag) sprintf(messageBuffer, "!Changed working dir to <%s>", workPwd);
                                    else strcpy(messageBuffer, "!Account valid, logged-in");
                                    loginVerification = 1;
                                } else {
                                    strcpy(messageBuffer, "-Invalid account, try again");
                                    loginVerification = 0;
                                }
                            } else {
                                // Save the current user
                                strcpy(currentUser, userArgs);
                                //  Check root status
                                if (getUserByName(&admin, userArgs)->root)
                                {
                                    if (cdirFlag) sprintf(messageBuffer, "!Changed working dir to <%s>", workPwd);
                                    else strcpy(messageBuffer, "!Account valid, logged-in");
                                    loginVerification = 1;
                                } else {
                                    strcpy(messageBuffer, "+Account valid, send password");
                                    loginVerification = 0;
                                }
                            }   
                        } else {
                            strcpy(messageBuffer, "-Invalid account, try again");
                            loginVerification = 0;
                        }
                    }
                    else strcpy(messageBuffer, "-Invalid account, try again");
                } 
                // PASS
                else if (!strcmp(userArgs, "PASS"))
                {
                    // Get the second argument
                    userArgs = strtok(NULL, " \r\n\t");
                    if (userArgs)
                    {   
                        // If username was entered
                        if (strlen(currentUser))
                        {
                            // Check passwd
                            if (checkPassword(&admin, getUserByName(&admin, currentUser), userArgs) == 1)
                            {   
                                if (cdirFlag) sprintf(messageBuffer, "!Changed working dir to <%s>", workPwd);
                                else strcpy(messageBuffer, "!Logged in");
                                loginVerification = 1;
                            }
                            else
                            {
                                strcpy(messageBuffer, "-Wrong password, try again");
                                loginVerification = 0;
                            }
                        } else {
                            // No username, check passwd
                            if (checkPassword(&admin, NULL, userArgs) == 2)
                            {
                                strcpy(currentPass, userArgs);
                                strcpy(messageBuffer, "+Send account");
                                loginVerification = 0;
                            } else {
                                strcpy(messageBuffer, "-Wrong password, try again");
                                loginVerification = 0;
                            }
                        }
                    } else strcpy(messageBuffer, "-Wrong password, try again");
                }
                // Invalid command
                else strcpy(messageBuffer,"-Invalid operation, correct spelling or try to login");
            }
            // Login require section
            else
            {
                // TYPE
                if (!strcmp(userArgs, "TYPE"))
                {
                    // Get the second argument
                    userArgs = strtok(NULL, " \r\n\t");
                    if (userArgs)
                    {
                        if (!strcmp(userArgs, "A"))
                        {
                            strcpy(messageBuffer, "+Using Ascii mode");
                        } else if (!strcmp(userArgs, "B")) {
                            strcpy(messageBuffer, "+Using Binary mode");
                        } else if (!strcmp(userArgs, "C")) {
                            strcpy(messageBuffer, "+Using Continuous mode");
                        } else {
                            strcpy(messageBuffer, "-Type not valid");
                        }
                    } else strcpy(messageBuffer, "-Type not valid");
                }
                // LIST
                else if (!strcmp(userArgs, "LIST"))
                {
                    // Get the second argument
                    userArgs = strtok(NULL, " \r\n\t");
                    if (userArgs)
                    {
                        if (!strcmp(userArgs, "F"))
                        {
                            // Get the third argument
                            userArgs = strtok(NULL, " \r\n\t");
                            // Check if path is valid
                            if (!userArgs || dirHandle(lsPwd, userArgs))
                            {
                                sprintf(pipeArgs, "%s %s", "ls", lsPwd);
                                pathFp = popen(pipeArgs, "r");
                                memset(pipeArgs, 0, sizeof(pipeArgs));
                                strcpy(messageBuffer, "+Listing Files in <");
                                strcat(messageBuffer, lsPwd); strcat(messageBuffer, ">\n");
                                while (fgets(tmpPathBuffer, sizeof(tmpPathBuffer), pathFp) != NULL) { strcat(messageBuffer, "\t\t"); strcat(messageBuffer, tmpPathBuffer); }
                                memset(tmpPathBuffer, 0, sizeof(tmpPathBuffer));
                                messageBuffer[strlen(messageBuffer)-1] = 0;
                                pclose(pathFp);
                            } else {
                                strcpy(messageBuffer, "-LIST error, no such directory");
                            }
                            strcpy(lsPwd, workPwd); // Reset lsPwd to workPwd
                        } else if (!strcmp(userArgs, "V")) {
                            // Get the third argument
                            userArgs = strtok(NULL, " \r\n\t");
                            // Check if path is valid
                            if (!userArgs || dirHandle(lsPwd, userArgs))
                            {
                                #ifdef __linux__
                                    // sym link might cause error
                                    if (!strcmp(lsPwd, "/")) sprintf(pipeArgs, "%s %s", "ls", lsPwd);
                                    else sprintf(pipeArgs, "%s %s", "ls -l", lsPwd);
                                #else
                                    // __CYGWIN__
                                    sprintf(pipeArgs, "%s %s", "ls -l", lsPwd);
                                #endif
                                pathFp = popen(pipeArgs, "r");
                                memset(pipeArgs, 0, sizeof(pipeArgs));
                                strcpy(messageBuffer, "+Listing Files in <");
                                strcat(messageBuffer, lsPwd); strcat(messageBuffer, ">\n");
                                while (fgets(tmpPathBuffer, sizeof(tmpPathBuffer), pathFp) != NULL) { strcat(messageBuffer, "\t\t"); strcat(messageBuffer, tmpPathBuffer); }
                                memset(tmpPathBuffer, 0, sizeof(tmpPathBuffer));
                                messageBuffer[strlen(messageBuffer)-1] = 0;
                                pclose(pathFp);
                            } else {
                                strcpy(messageBuffer, "-LIST error, no such directory");
                            }
                            strcpy(lsPwd, workPwd); // Reset lsPwd to workPwd
                        } else {
                            strcpy(messageBuffer, "-Invalid argument, please use { F | V }");
                        }
                    } else strcpy(messageBuffer, "-Invalid argument, please use { F | V }");
                }
                // CDIR
                else if (!strcmp(userArgs, "CDIR"))
                {
                    // Get the second argument
                    userArgs = strtok(NULL, " \r\n\t");
                    if (userArgs && strcmp(userArgs, ".") && strcmp(userArgs, "./"))
                    {
                        strcpy(tmpPathBuffer, workPwd);
                        if (dirHandle(tmpPathBuffer, userArgs) || !strcmp(userArgs, "-"))
                        {
                            if (!strcmp(userArgs, "-"))
                            {
                                strcpy(lsPwd, oldPwd);
                                strcpy(oldPwd, workPwd);
                                strcpy(workPwd, lsPwd);
                            } else {
                                strcpy(lsPwd, tmpPathBuffer);
                                strcpy(oldPwd, workPwd);
                                strcpy(workPwd, lsPwd);
                            }
                            if (getUserByName(&admin, currentUser)->root)
                            {
                                sprintf(messageBuffer, "!Changed working dir to <%s>", workPwd);
                            } else {
                                strcpy(messageBuffer, "+directory ok, send account/password");
                                loginVerification = 0; // Relogin required
                                cdirFlag = 1; // Changing dir
                            }
                        } else {
                            strcpy(messageBuffer, "-Can’t connect to directory because: no such directory");
                        }
                        memset(tmpPathBuffer, 0, sizeof(tmpPathBuffer));
                    } else strcpy(messageBuffer, "-Can’t connect to directory because: invalid input arguemnt or \".\"");
                }
                // KILL
                else if (!strcmp(userArgs, "KILL"))
                {
                    // Get the second argument
                    userArgs = strtok(NULL, " \r\n\t");
                    if (userArgs)
                    {
                        if (checkFileValidity(userArgs)) strcpy(tmpPathBuffer, userArgs);
                        else {
                            strcpy(tmpPathBuffer, workPwd); strcat(tmpPathBuffer, userArgs);
                        }
                        if(remove(tmpPathBuffer))
                        {
                            strcpy(messageBuffer, "-Not deleted because: no such file or directory or permission denied");
                        } else {
                            sprintf(messageBuffer, "+<%s> deleted", userArgs);
                        }
                        memset(tmpPathBuffer, 0, sizeof(tmpPathBuffer));
                    } else strcpy(messageBuffer, "-Invalid empty input argument");
                }
                // NAME
                else if (!strcmp(userArgs, "NAME"))
                {
                    // Get the second argument
                    userArgs = strtok(NULL, " \r\n\t");
                    if (userArgs)
                    {
                        if (checkFileValidity(userArgs))
                        {
                            strcpy(tmpPathBuffer, userArgs);
                            absolutePath = 1;
                        } else {
                            strcpy(tmpPathBuffer, workPwd); strcat(tmpPathBuffer, userArgs);
                        }
                        // Check if file exist
                        if(checkFileValidity(tmpPathBuffer))
                        {
                            strcpy(oldFileNameBuffer, userArgs); // save old name
                            strcpy(messageBuffer, "+File exists");
                        } else {
                            strcpy(tmpPathBuffer, userArgs);
                            sprintf(messageBuffer, "-Can’t find <%s>", tmpPathBuffer);
                            memset(oldFileNameBuffer, 0, sizeof(oldFileNameBuffer)); // reset old name
                        }
                        memset(tmpPathBuffer, 0, sizeof(tmpPathBuffer));
                    } else { strcpy(messageBuffer, "-Invalid empty input argument"); memset(oldFileNameBuffer, 0, sizeof(oldFileNameBuffer)); } // reset old name
                }
                // TOBE
                else if (!strcmp(userArgs, "TOBE") && strlen(oldFileNameBuffer))
                {
                    // Get the second argument
                    userArgs = strtok(NULL, " \r\n\t");
                    if (userArgs)
                    {
                        if (absolutePath)
                        {
                            strcpy(tmpPathBuffer, oldFileNameBuffer); strcpy(renameBuffer, userArgs);
                        } else {
                            strcpy(tmpPathBuffer, workPwd); strcat(tmpPathBuffer, oldFileNameBuffer);
                            strcpy(renameBuffer, workPwd); strcat(renameBuffer, userArgs);
                        }
                        if(!rename(tmpPathBuffer, renameBuffer))
                        {
                            strcpy(renameBuffer, userArgs);
                            sprintf(messageBuffer, "+<%s> renamed to <%s>", oldFileNameBuffer, renameBuffer);
                            memset(oldFileNameBuffer, 0, sizeof(oldFileNameBuffer)); // reset old name
                            absolutePath = 0;
                        } else {
                            strcpy(messageBuffer, "-File wasn’t renamed because used reserved characters or permission denied");
                        }
                        memset(tmpPathBuffer, 0, sizeof(tmpPathBuffer));
                        memset(renameBuffer, 0, sizeof(renameBuffer));
                    } else strcpy(messageBuffer, "-Invalid empty input argument");
                }
                // RETR
                else if (!strcmp(userArgs, "RETR"))
                {
                    // Get the second argument
                    userArgs = strtok(NULL, " \r\n\t");
                    if (userArgs)
                    {
                        if (checkFileValidity(userArgs)) strcpy(tmpPathBuffer, userArgs);
                        else {
                            strcpy(tmpPathBuffer, workPwd); strcat(tmpPathBuffer, userArgs);
                        }
                        if(checkFileValidity(tmpPathBuffer))
                        {
                            // saving file path
                            strcpy(rsFilePathBuffer, tmpPathBuffer);
                            // return number of bytes that will be sent
                            sprintf(pipeArgs, "du -b %s | cut -f1", tmpPathBuffer);
                            pathFp = popen(pipeArgs, "r");
                            memset(pipeArgs, 0, sizeof(pipeArgs));
                            memset(tmpPathBuffer, 0, sizeof(tmpPathBuffer));
                            while (fgets(tmpPathBuffer, sizeof(tmpPathBuffer), pathFp) != NULL) strcpy(messageBuffer, tmpPathBuffer);
                            memset(tmpPathBuffer, 0, sizeof(tmpPathBuffer));
                            messageBuffer[strlen(messageBuffer)-1] = 0;
                            pclose(pathFp);
                            strcpy(rsFileSize, messageBuffer); // save file size
                            retrFound = 1;
                        } else {
                            strcpy(messageBuffer, "-File doesn’t exist");
                        }
                    } else strcpy(messageBuffer, "-Invalid empty input argument");
                }
                // SEND
                else if (!strcmp(userArgs, "SEND") && retrFound)
                {
                    // send file 
                    // check file again in case it was deleted
                    if(checkFileValidity(rsFilePathBuffer))
                    {;
                        if (strtoul(rsFileSize, 0, 10))
                        {
                            fd = open(rsFilePathBuffer, O_RDONLY);
                            if (fd == -1 || fstat(fd, &fileStat) < 0)
                            {
                                fprintf(stderr, "FATAL ERROR: FILE CAN NOT BE OPENED\n");
                                break;
                            } else {
                                remainData = fileStat.st_size;
                                #ifdef __linux__
                                    offset = 0;
                                    do
                                    {
                                        sentBytes = sendfile(clientfd, fd, &offset, BUFSIZ);
                                        remainData -= sentBytes;
                                    } while (sentBytes > 0 && remainData > 0);
                                #else
                                char sendBuffer[BUFSIZ];
                                unsigned readAmount;
                                while ((readAmount = read(fd, sendBuffer, BUFSIZ)) > 0)
                                {
                                    unsigned totalWritten = 0;
                                    do
                                    {
                                        unsigned actualWritten;
                                        actualWritten = write(clientfd, sendBuffer + totalWritten, readAmount - totalWritten);
                                        if( actualWritten == - 1 ) break; // some error occured - quit
                                        totalWritten += actualWritten;
                                    } while( totalWritten < readAmount );
                                }
                                bzero(sendBuffer, BUFSIZ);
                            #endif
                                fprintf(stdout, "MSG: FILE SENT\n");
                            }
                        } else fprintf(stdout, "MSG: FILE CREATED\n");    
                    } else {
                        fprintf(stderr, "FATAL ERROR: FILE DOES NOT EXIST\n");
                        break;
                    }
                    memset(rsFilePathBuffer, 0, sizeof(rsFilePathBuffer));
                    retrFound = 0;
                    continue;
                }
                // STOP
                else if (!strcmp(userArgs, "STOP") && retrFound)
                {
                    strcpy(messageBuffer, "+ok, RETR aborted");
                    retrFound = 0;
                }
                // SOTR
                else if (!strcmp(userArgs, "STOR"))
                {
                    // Get the second argument
                    userArgs = strtok(NULL, " \r\n\t");
                    if (userArgs)
                    {
                        // use the storMode to store the second argument
                        strcpy(storMode, userArgs);
                        // Get the third argument
                        userArgs = strtok(NULL, " \r\n\t");
                        if (userArgs)
                        {
                            // get file name only
                            p = strrchr(userArgs, '/');
                            if(p)
                            {
                                ++p;
                                strcpy(storFilePathBuffer, workPwd); strcat(storFilePathBuffer, p);
                            } else {
                                strcpy(storFilePathBuffer, workPwd); strcat(storFilePathBuffer, userArgs);
                            }
                            p = NULL;

                            if(checkFileValidity(storFilePathBuffer)) fileExist = 1;
                            else fileExist = 0;

                            if (!strcmp(storMode, "NEW"))
                            {
                                if (fileExist)
                                {
                                    newFileNameGen(storFilePathBuffer);
                                    strcpy(messageBuffer, "+File exists, will create new generation of file");
                                } else strcpy(messageBuffer, "+File does not exist, will create new file");
                                /*
                                * -File exists, but system doesn’t support generations
                                * This option seems will never happen in the system
                                * I will ignore this
                                */

                            } else if (!strcmp(storMode, "OLD"))
                            {
                                if (fileExist) strcpy(messageBuffer, "+Will write over old file");
                                else strcpy(messageBuffer, "+Will create new file");
                            } else if (!strcmp(storMode, "APP")) {
                                if (fileExist) strcpy(messageBuffer, "+Will append to file");
                                else strcpy(messageBuffer, "+Will create new file");
                            } else {
                                strcpy(messageBuffer, "-Invalid input argument");
                                memset(storFilePathBuffer, 0, sizeof(storFilePathBuffer));
                            }
                            fileExist = 0;
                        } else strcpy(messageBuffer, "-Invalid argument, please use { NEW | OLD | APP }");
                    } else strcpy(messageBuffer, "-Invalid argument, please use { NEW | OLD | APP }");
                }
                // SIZE
                else if (!strcmp(userArgs, "SIZE") && strlen(storFilePathBuffer))
                {
                    // Get the second argument
                    userArgs = strtok(NULL, " \r\n\t");
                    if (userArgs)
                    {
                        remainData = strtoul(userArgs, 0, 10);

                        // check space
                        strcpy(pipeArgs, "df --output=avail -B 1 \"$PWD\" | tail -n 1");
                        pathFp = popen(pipeArgs, "r");
                        memset(pipeArgs, 0, sizeof(pipeArgs));
                        while (fgets(tmpPathBuffer, sizeof(tmpPathBuffer), pathFp) != NULL) strcpy(freeSpace, tmpPathBuffer);
                        memset(tmpPathBuffer, 0, sizeof(tmpPathBuffer));
                        freeSpace[strlen(freeSpace)-1] = 0;
                        pclose(pathFp);

                        if (strtoul(freeSpace, 0, 10) > remainData)
                        {
                            if (!strcmp(storMode, "NEW")) strcpy(storMode, "wb");
                            else if (!strcmp(storMode, "OLD")) strcpy(storMode, "wb");
                            else if (!strcmp(storMode, "APP")) strcpy(storMode, "ab");
                            else break; // should not go here

                            // getting file from client
                            pathFp = fopen(storFilePathBuffer, storMode);
                            memset(storMode, 0, sizeof(storMode));
                            if (pathFp)
                            {
                                strcpy(messageBuffer, "+ok, waiting for file");
                                if ((iret=send(clientfd, messageBuffer, strlen(messageBuffer), 0)) <= 0) // Send response to client
                                { perror("send"); break; }
                                printf("Send: %s\n", messageBuffer);

                                if (remainData)
                                {
                                    while ((remainData > 0) && ((iret=recv(clientfd, messageBuffer, BUFSIZ, 0)) > 0))
                                    {
                                        fwrite(messageBuffer, sizeof(char), iret, pathFp);
                                        remainData -= iret;
                                    }
                                }
                                fclose(pathFp);
                                sprintf(messageBuffer, "+Saved <%s>", storFilePathBuffer);
                            } else strcpy(messageBuffer, "-Couldn’t save because permission denied");
                        } else strcpy(messageBuffer, "-Not enough room, don’t send it");    
                        memset(storFilePathBuffer, 0, sizeof(storFilePathBuffer));
                        memset(storMode, 0, sizeof(storMode));
                    }
                }
                // Invalid command
                else strcpy(messageBuffer,"-Invalid operation, please check spelling");
            }

            //========================================================================================//
            if ((iret=send(clientfd, messageBuffer, strlen(messageBuffer), 0)) <= 0) // Send response to client
            { perror("send"); break; }
            printf("Send: %s\n", messageBuffer);
        }
        // Close the socket, free the resources
        close(clientfd); puts("");
    }
    close(listenfd); 
}
