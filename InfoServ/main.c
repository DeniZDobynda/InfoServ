//
//  main.c
//  InfoServ
//
//  Created by Denis Dobanda on 19.10.18.
//  Copyright © 2018 Denis Dobanda. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

#define PORT_NO 57171;

struct Processes {
    unsigned short total;
    unsigned short running;
    unsigned short sleeping;
    unsigned short threads;
};
struct CPU {
    unsigned short user;
    unsigned short system;
    unsigned short idle;
};

typedef struct Processes Proc;
typedef struct CPU CPU;

int listening = 0;
int newsockfd;
unsigned short receivedNumber = 10;
unsigned short run = 0;
unsigned short ready = 1;

void error(const char *msg) {
    perror(msg);
    exit(1);
}

FILE* readCPUInfo(int times) {
    FILE *fp;
    char *command;
    asprintf(&command, "top -stats pid,command,cpu,mem -u -l %i", times);
    fp = popen(command, "r");
    if (fp == NULL) {
        printf("Failed to run command\n" );
        return NULL;
    }
    return fp;
}

Proc* parseProc(char* str) {
    unsigned short count, current = 1, out = 0;
    Proc *pr;
    pr = malloc(sizeof(Proc));
    pr->running = pr->sleeping = pr->threads = pr->total = 0;
    count = (unsigned short)strlen(str) + 1; current = 1; out = 0;
    while (--count) {
        if (str[count] >= 48 && str[count] <= 57) {
            out += (str[count] - 48) * current;
            current *= 10;
        } else if (out) {
            if (!pr->threads) { pr->threads = out; }
            else if (!pr->sleeping) { pr->sleeping = out; }
            else if (!pr->running) { pr->running = out; }
            else if (!pr->total) { pr->total = out; }
            out = 0;
            current = 1;
        }
    }
    return pr;
}

CPU * parseCPU(char * str) {
    CPU * cpu = malloc(sizeof(CPU));
    unsigned short count, current = 1, out = 0;
    cpu->idle = cpu->system = cpu->user = 0;
    count = (unsigned short)strlen(str) + 1; current = 1; out = 0;
    while (--count) {
        if (str[count] == 46) --count;
        if (str[count] >= 48 && str[count] <= 57) {
            out += (str[count] - 48) * current;
            current *= 10;
        } else if (out) {
            if (!cpu->idle) { cpu->idle = out; }
            else if (!cpu->system) { cpu->system = out; }
            else if (!cpu->user) { cpu->user = out; }
            out = 0; current = 1;
        }
    }
    return cpu;
}

void strip_char(char *str, char strip)
{
    char *p, *q;
    for (q = p = str; *p; p++)
        if (*p != strip)
            *q++ = *p;
    *q = '\0';
}

char * readProcess(char * str) {
    char *p;
    short f = 0;
    for (p = str; *p; p++) {
        if (*p == 32 && !f) {
            *p = 124;
            f = 1;
        } else if (*p != 32) {
            f = 0;
        }
    }
    strip_char(str, 32);
    
    return str;
}

void printProcInfo(Proc * info) {
    printf("Total: %d\tRunning: %d\tSleeping: %d\n", info->total, info->running, info->sleeping);
}

void sendProcInfo(Proc * pr) {
    char *message = malloc(100);
    asprintf(&message, "P|%d|%d|%d|%d\n", pr->total, pr->running, pr->sleeping, pr->threads);
    if ((int)write(newsockfd,message,strlen(message)) < 0) error("ERROR writing to socket");
    free(message);
}

void sendCPUInfo(CPU * cpu) {
    char *message = malloc(100);
    asprintf(&message, "C|%d|%d|%d\n", cpu->user, cpu->system, cpu->idle);
    if ((int)write(newsockfd,message,strlen(message)) < 0) error("ERROR writing to socket");
    free(message);
}

void *readInThread(void *arg) {
    char buffer[256];
    while (listening) {
        if ((int)read(newsockfd,buffer,255) < 0) error("ERROR reading from socket");
        if (strncmp(buffer, "close connection", 16) == 0 || strncmp(buffer, "close", 5) == 0) {
            listening = receivedNumber = 0;
            if ((int)write(newsockfd,"terminate",9) < 0) error("ERROR writing to socket");
        } else if (buffer[0] >= 48 && buffer[0] <= 57) {
            receivedNumber = buffer[0] - 48;
        } else if (strncmp(buffer, "stop", 4) == 0) {
            run = 0;
        } else if (strncmp(buffer, "shut down", 9) == 0 ) {
            ready = listening = run = 0;
            if ((int)write(newsockfd,"terminate",9) < 0) error("ERROR writing to socket");
        }
    }
    return NULL;
}

void printInfo() {
    FILE *fp;
    char *command, str[50];
    asprintf(&command, "ipconfig getifaddr en0");
    fp = popen(command, "r");
    if (fp == NULL) {
        printf("Failed to determine IP\n" );
    } else {
        if (fgets(str, sizeof(str)-1, fp) != NULL) {
            str[strlen(str)-1] = '\0';
            printf("Current IP: %s:57171\nPID: %d\n", str, getpid());
        }
    }
}

int main( int argc, char *argv[] )
{
    FILE* fp = NULL;
    char str[1035], *m;
    Proc *pr;
    CPU * cpu;
    pthread_t pth = NULL;
    
    int sockfd, portno, procON;
    socklen_t clilen;
    char buffer[256];
    struct sockaddr_in serv_addr, cli_addr;
    
    printInfo();
    
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) error("ERROR opening socket");
    bzero((char *) &serv_addr, sizeof(serv_addr));
    portno = PORT_NO;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) error("ERROR on binding");
    
    while (ready) {
        listen(sockfd,5);
        listening = 1;
        clilen = sizeof(cli_addr);
        newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
        if (newsockfd < 0) error("ERROR on accept");
        bzero(buffer,256);
        
        while (listening) {
            if (pth == NULL) { pthread_create(&pth,NULL,readInThread,NULL); }
            while (receivedNumber == 10 && listening) { sleep(1); }
            if (listening) {
                fp = readCPUInfo(receivedNumber);
                run = 1; receivedNumber = 10; procON = 0;
                if (fp) {
                    while (fgets(str, sizeof(str)-1, fp) != NULL && listening && run) {
                        if (strncmp(str, "Processes: ", 11) == 0) {
                            procON = 0;
                            pr = parseProc(&str[0]); sendProcInfo(pr); free(pr);
                        } else if (strncmp(str, "CPU usage: ", 11) == 0) {
                            procON = 0;
                            cpu = parseCPU(&str[0]); sendCPUInfo(cpu); free(cpu);
                        } else if (strncmp(str, "PID ", 4) == 0) {
                            // here goes processes
                            procON = 1;
                        } else if (procON && procON < 20) {
                            m = malloc(100);
                            asprintf(&m, "L|%s", readProcess(&str[0]));
                            if ((int)write(newsockfd,m,strlen(m)) < 0) error("ERROR writing to socket");
                            ++procON;
                        }
                    }
                    pclose(fp);
                }
            }
        }
        
        pthread_join(pth, NULL);
        pth = NULL;
        receivedNumber = 10;
        close(newsockfd);
    }
    close(sockfd);
    return 0;
}
