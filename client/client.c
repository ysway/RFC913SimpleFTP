#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
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

static volatile char RUNNING = 1;
void intHandler(int dummy) {
    RUNNING = 0;
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

int main(int argc,char *argv[])
{
    if (argc!=3)
    {
        puts("Usage: ./client ip port\nExample:./client 127.0.0.1 5005\n"); return -1;
    }

    puts("Simple FTP Client, Enter Ctrl+C and press Enter to exit");
    
    signal(SIGINT, intHandler);

    // create socket
    int sockfd;
    if ( (sockfd = socket(AF_INET, SOCK_STREAM, 0))==-1) { perror("socket"); return -1; }

    // send connection request
    struct hostent* h;
    if ( (h = gethostbyname(argv[1])) == 0 ) // server IP
    { printf("gethostbyname failed.\n"); close(sockfd); return -1; }
    struct sockaddr_in servaddr;
    memset(&servaddr,0,sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(atoi(argv[2])); // server port
    memcpy(&servaddr.sin_addr,h->h_addr,h->h_length);
    if (connect(sockfd, (struct sockaddr *)&servaddr,sizeof(servaddr)) != 0)
    { perror("connect"); close(sockfd); return -1; }
    
    // Flags and temp variables
    int fd, iret;
    long offset;
    long remainData;
    unsigned long sentBytes = 0;
    unsigned long retrFileSize = 0;
    char workPwd[BUFSIZ];
    char messageBuffer[BUFSIZ];
    char retrFilePathBuffer[BUFSIZ];
    char storFilePathBuffer[BUFSIZ];
    char storFileSize[BUFSIZ];
    char tmpMessageBuffer[BUFSIZ];
    char tmpPathBuffer[BUFSIZ];
    char pipeArgs[BUFSIZ];
    char retrFlag = 0;
    char storFlag = 0;
    char sizeFlag = 0;
    char validFileRetr = 0;
    char validFileSize = 0;
    char *userArgs;
    char *p; // general pointer
    struct stat fileStat;
    FILE *pathFp;

    memset(messageBuffer, 0, sizeof(messageBuffer));
    if ( (iret=recv(sockfd, messageBuffer, sizeof(messageBuffer), 0)) <=0 ) // recv greeting
    {
        printf("iret=%d\n", iret);
    }
    printf(">SFTP Server：%s\n", messageBuffer);
    
    // get cwd
    getcwd(workPwd, sizeof(workPwd));
    workPwd[strlen(workPwd)] = '/';

    while(RUNNING)
    {
        memset(messageBuffer,0,sizeof(messageBuffer));
        fprintf(stdout, ">SFTP Client: ");
        fgets(messageBuffer, sizeof(messageBuffer)/sizeof(char), stdin);
        messageBuffer[strcspn(messageBuffer, "\n")] = 0;
        if (!strlen(messageBuffer)) strcpy(messageBuffer, "NULL");

        // DONE
        if (!RUNNING || !strncmp(messageBuffer, "DONE", 4)) 
        {
            if ( (iret=send(sockfd, "DONE", 4, 0)) <=0 ) // DONE
            { perror("send"); break; }
            if ( (iret=recv(sockfd, messageBuffer, sizeof(messageBuffer), 0)) <=0 )
            {
                printf("iret=%d\n", iret);
            }
            printf(">SFTP Server：%s\n", messageBuffer);
            break;
        }

        // RETR
        if (!strncmp(messageBuffer, "RETR", 4))
        {
            // cache the saving file path
            strcpy(tmpMessageBuffer, messageBuffer);
            // Get first argument
            userArgs = strtok(tmpMessageBuffer," \r\n\t");
            // Get the second argument
            userArgs = strtok(NULL, " \r\n\t");

            // get file name only
            p = strrchr(userArgs, '/');
            if(p)
            {
                ++p;
                strcpy(retrFilePathBuffer, workPwd); strcat(retrFilePathBuffer, p);
            } else {
                strcpy(retrFilePathBuffer, workPwd); strcat(retrFilePathBuffer, userArgs);
            }
            p = NULL;

            // reset tmpMessageBuffer and user Args
            memset(tmpMessageBuffer,0,sizeof(tmpMessageBuffer));
            validFileRetr = 0;
            retrFlag = 1;
        }
        // SEND
        else if (!strncmp(messageBuffer, "SEND", 4) && validFileRetr)
        {
            if ( (iret=send(sockfd, messageBuffer, strlen(messageBuffer), 0)) <=0 )
            { perror("send"); break; }
            // receiving file
            if (retrFileSize)
            {
                pathFp = fopen(retrFilePathBuffer, "w");
                if (pathFp)
                {
                    while ((retrFileSize > 0) && ((iret=recv(sockfd, messageBuffer, BUFSIZ, 0)) > 0))
                    {
                        fwrite(messageBuffer, sizeof(char), iret, pathFp);
                        retrFileSize -= iret;
                    }
                    fclose(pathFp);
                } else fprintf(stderr, ">SFTP Server：%s\n", "Fatal Error, cannot create file");
            } else {
                sprintf(pipeArgs, "touch %s", retrFilePathBuffer);
                memset(retrFilePathBuffer, 0, sizeof(retrFilePathBuffer));      
                pathFp = popen(pipeArgs, "r");
                memset(pipeArgs, 0, sizeof(pipeArgs));
                while (fgets(pipeArgs, sizeof(pipeArgs), pathFp) != NULL);
                memset(pipeArgs, 0, sizeof(pipeArgs));
                pclose(pathFp);
            }
            validFileRetr = 0;
            continue;
        }
        // STOP
        else if (!strncmp(messageBuffer, "STOP", 4) && validFileRetr)
        {
            validFileRetr = 0;
        }

        // STOR
        else if (!strncmp(messageBuffer, "STOR", 4))
        {
            // cache the stor file path
            strcpy(tmpMessageBuffer, messageBuffer);
            // Get the first argument
            userArgs = strtok(tmpMessageBuffer, " \r\n\t");
            // Get the second argument
            userArgs = strtok(NULL, " \r\n\t");

            if (userArgs)
            {
                // reconstruct massageBuffer
                strcpy(messageBuffer, "STOR "); strcat(messageBuffer, userArgs); strcat(messageBuffer, " ");

                // Get the third argument
                userArgs = strtok(NULL, " \r\n\t");
                if (userArgs)
                {
                    // check if arg is absolute path
                    if (checkFileValidity(userArgs)) strcpy(storFilePathBuffer, userArgs);
                    else {
                        strcpy(storFilePathBuffer, workPwd); strcat(storFilePathBuffer, userArgs);
                    }    
                    memset(tmpMessageBuffer,0,sizeof(tmpMessageBuffer));

                    while(!checkFileValidity(storFilePathBuffer))
                    {
                        memset(storFilePathBuffer, 0, sizeof(storFilePathBuffer));
                        fprintf(stderr, ">SFTP Client ERR: Please Enter A Valid File Path\n");
                        fprintf(stdout, ">SFTP Client: STOR File Path = ");
                        fgets(storFilePathBuffer, sizeof(storFilePathBuffer)/sizeof(char), stdin);
                        storFilePathBuffer[strcspn(storFilePathBuffer, "\n")] = 0;
                        if (!strlen(storFilePathBuffer)) strcpy(storFilePathBuffer, "NULL");

                        // handle exit signal
                        if (!RUNNING) break;
                    }
                    // handle exit signal
                    if (!RUNNING)
                    {
                        if ( (iret=send(sockfd, "DONE", 4, 0)) <=0 ) // DONE
                        { perror("send"); break; }
                        if ( (iret=recv(sockfd, messageBuffer, sizeof(messageBuffer), 0)) <=0 )
                        {
                            printf("iret=%d\n", iret);
                        }
                        printf(">SFTP Server：%s\n", messageBuffer);
                        break;
                    }
                    
                    // reconstruct messageBuffer continue
                    strcat(messageBuffer, storFilePathBuffer);
                    storFlag = 1;
                }
            }
        }
        // SIZE
        else if (!strncmp(messageBuffer, "SIZE", 4) && validFileSize)
        {
            // reconstruct messageBuffer
            strcpy(messageBuffer, "SIZE ");

            // check if file is exist
            if (!checkFileValidity(storFilePathBuffer))
            {
                fprintf(stderr, ">SFTP Client ERR: FATAL FILE MISSING\n");
                if ( (iret=send(sockfd, "DONE", 4, 0)) <=0 ) // send DONE
                { perror("send"); break; }
            }

            // get size of file in bytes
            strcpy(tmpPathBuffer, storFilePathBuffer);
            sprintf(pipeArgs, "du -b %s | cut -f1", tmpPathBuffer);
            pathFp = popen(pipeArgs, "r");
            memset(pipeArgs, 0, sizeof(pipeArgs));
            memset(tmpPathBuffer, 0, sizeof(tmpPathBuffer));
            while (fgets(tmpPathBuffer, sizeof(tmpPathBuffer), pathFp) != NULL) strcat(messageBuffer, tmpPathBuffer);
            messageBuffer[strlen(messageBuffer)-1] = 0;
            pclose(pathFp);
            strcpy(storFileSize, tmpPathBuffer); // save file size
            memset(tmpPathBuffer, 0, sizeof(tmpPathBuffer));

            validFileSize = 0;
            sizeFlag = 1;
        }

        /***************************************/

        if ( (iret=send(sockfd, messageBuffer, strlen(messageBuffer), 0)) <=0 ) // send command
        { perror("send"); break; }

        memset(messageBuffer, 0, sizeof(messageBuffer));
        if ( (iret=recv(sockfd, messageBuffer, sizeof(messageBuffer), 0)) <=0 ) // recv message
        {
            printf("iret=%d\n", iret); break;
        }

        if (retrFlag && strncmp(messageBuffer, "-", 1))
        {
            if (!strncmp(messageBuffer, "-", 1)) memset(retrFilePathBuffer, 0, sizeof(retrFilePathBuffer));
            else
            {
                // catch file size
                retrFileSize = strtoul(messageBuffer, 0, 10);
                validFileRetr = 1;
            }
            retrFlag = 0;
        }
        else if (storFlag)
        {
            // if typed argument was wrong
            if (!strncmp(messageBuffer, "-", 1)) memset(storFilePathBuffer, 0, sizeof(storFilePathBuffer));
            else validFileSize = 1;
            storFlag = 0;
        }
        else if (sizeFlag)
        {
            printf(">SFTP Server：%s\n", messageBuffer);
            // Couldn't save or not enough space
            if (!strncmp(messageBuffer, "-", 1))
            {
                memset(storFilePathBuffer, 0, sizeof(storFilePathBuffer));
                sizeFlag = 0;
                continue;
            }
            else
            {
                // stor file in remote
                // check file again in case it was deleted
                if(checkFileValidity(storFilePathBuffer))
                {
                    if (strtoul(storFileSize, 0, 10))
                    {
                        fd = open(storFilePathBuffer, O_RDONLY);
                        if (fd == -1 || fstat(fd, &fileStat) < 0)
                        {
                            fprintf(stderr, ">SFTP Client ERR: FATAL FILE CAN NOT BE OPENED\n");
                            if ( (iret=send(sockfd, "DONE", 4, 0)) <=0 ) // send DONE
                            { perror("send"); break; }
                        } else {
                            remainData = fileStat.st_size;
                            #ifdef __linux__
                                offset = 0;
                                do
                                {
                                    sentBytes = sendfile(sockfd, fd, &offset, BUFSIZ);
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
                                        actualWritten = write(sockfd, sendBuffer + totalWritten, readAmount - totalWritten);
                                        if( actualWritten == - 1 ) break; // some error occured - quit
                                        totalWritten += actualWritten;
                                    } while( totalWritten < readAmount );
                                }
                                bzero(sendBuffer, BUFSIZ);
                            #endif
                        }
                    }
                } else {
                    fprintf(stderr, ">SFTP Client ERR: FATAL FILE MISSING\n");
                    if ( (iret=send(sockfd, "DONE", 4, 0)) <=0 ) // send DONE
                    { perror("send"); break; }
                }
                memset(storFilePathBuffer, 0, sizeof(storFilePathBuffer));
            }
            sizeFlag = 0;
            
            // finish transfer
            memset(messageBuffer, 0, sizeof(messageBuffer));
            if ( (iret=recv(sockfd, messageBuffer, sizeof(messageBuffer), 0)) <=0 ) // recv message
            {
                printf("iret=%d\n", iret); break;
            }
        }
        printf(">SFTP Server：%s\n", messageBuffer);
    }

    // close socket
    puts("\n== EXIT ==\n");
    close(sockfd);
}
