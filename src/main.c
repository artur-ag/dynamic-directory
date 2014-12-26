#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <netdb.h>
#include <errno.h>
#include <signal.h>
extern int errno;

#include "globals.h"
#include "server.h"
#include "contact.h"
#include "list.h"
#include "commands.h"
#include "debug.h"

/**
 *  True (1) while the program runs. Set to 0 if user types 'exit'.
 *  May be set by the signal handler for SIGINT as well.
 */
int isRunning = 1;

void printPrompt()
{
    printf("\ndd> ");
    fflush(stdout);
}

void sigintHandler()
{
    logm(1, "Got Ctrl+C\n");

    // Program is on select() loop - attempt to exit gracefully
    if (joinStatus == Joined)
        leave();

    isRunning = 0;
}

/** \brief Gets the default Surname Server's IP, hosted at tejo.ist.utl.pt.
 *
 * \param a_out struct in_addr* Out parameter. Where the tejo.ist.utl.pt Surname Server IP will be stored.
 *
 */
void getDefaultSS(struct in_addr *a_out)
{
    struct hostent *h;
    struct in_addr *t;

    if ((h = gethostbyname("tejo.ist.utl.pt")) == NULL)
    {
        perror("Could not find tejo.ist.utl.pt");
        exit(-1);
    }

    t = (struct in_addr*) h -> h_addr_list[0];
    *a_out = *t;

    logm(1, "Found %s at %s\n", h->h_name, inet_ntoa(*t));
}

int main(int argc, char** argv)
{
    #ifdef printauthor
    printf("artur goncalves, 69271\n");
    #endif // printauthor

    if (argc < 3 || argc % 2 != 1)
    {
        printf("Usage: %s name.surname IP [-t talkport] [-d dnsport] [-i saIP] [-p saport]\n", argv[0]);
        exit(-2);
    }

    // Set default values for arguments
    myTalkPort = 30000;
    myDnsPort = 30000;
    saPort = 58000;
    saIP.s_addr = 0;

    myName = argv[1];
    if (strstr(myName, ".") == NULL)
    {
        printf("Error on argument 'name.surname'. Must be separated by '.'\n");
        exit(-2);
    }
    if (inet_aton(argv[2], &myIP) == 0)
    {
        perror("Error parsing argument IP. Should be on dot-decimal notation.\n");
        exit(-2);
    }

    // Parse optional arguments
    int i;
    for (i = 3; i < argc - 1; i += 2)
    {
        if (strcmp(argv[i], "-t") == 0)
            myTalkPort = atoi(argv[i+1]);

        if (strcmp(argv[i], "-d") == 0)
            myDnsPort = atoi(argv[i+1]);

        if (strcmp(argv[i], "-i") == 0)
        {
            if (inet_aton(argv[i+1], &saIP) == 0)
            {
                perror("Error parsing saIP");
                exit(-2);
            }
        }

        if (strcmp(argv[i], "-p") == 0)
            saPort = atoi(argv[i+1]);
    }

    // Get default IP if it has not been set
    if (saIP.s_addr == 0)
        getDefaultSS(&saIP);

    fd_set rfds;
    int ret;
    char buffer[2048];
    buffer[2047] = '\0';    // Ensure buffer is always null-terminated.
    int max = 0;

    contacts = newList();
    talkServerSocket = prepareTalkServer();

    // Set handler the SIGINT (Ctrl+C) signal, which automatically leaves before terminating
    signal(SIGINT, sigintHandler);

    printPrompt();

    // Run while in normal conditions, or while we're in the process of leaving and exiting
    while (isRunning || joinStatus != NotJoined)
    {
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        max = (STDIN_FILENO > max) ? STDIN_FILENO : max;

        // Add different sockets to rfds if they are ready to be used

        /* Always add the TCP server socket for starting chats
           There's no problem if we're not even joined yet, the chats only depend on the
           rest of the program to find the IP+port. */
        FD_SET(talkServerSocket, &rfds);
        max = (talkServerSocket > max) ? talkServerSocket : max;

        // Chat socket, possibly open with a contact
        if (talkSocket != -1)
        {
            FD_SET(talkSocket, &rfds);
            max = (talkSocket > max) ? talkSocket : max;
        }

        // DNS socket, used to trade behind-the-scenes messages like queries, etc
        if (dnsSocket != -1)
        {
            FD_SET(dnsSocket, &rfds);
            max = (dnsSocket > max) ? dnsSocket : max;
        }

        // Set timeout if state is not stable (if we are waiting for OKs, etc)
        struct timeval timeout;
        struct timeval* pTimeout = NULL;
        if ((joinStatus != Joined && joinStatus != NotJoined)
            ||
            (findStatus != NotFinding))
        {
            pTimeout = &timeout;
            timeout.tv_sec = 10;
            timeout.tv_usec = 0;
        }

        // Multiplex all possible inputs
        ret = select(max + 1, &rfds, NULL, NULL, pTimeout);
        if (ret < 0)
        {
            // If user pressed Ctrl+C (INTerRuption), don't leave the loop yet
            // We must wait the 'leave' messages and OKs to be sent
            // ...unless we are not joined to begin with
            if (errno == EINTR)
            {
                if (joinStatus == NotJoined)
                    break;
                else
                    continue;
            }

            perror("Error on select()");
            exit(-1);
        }

        // Handle a timeout
        if (ret == 0)
        {
            if (joinStatus != Joined)
            {
                if (joinStatus < Joined)
                    printf("Join timed out. Aborted join. Please try again.\n");
                else
                    printf("Leave timed out. Forced leave.\n");
                    logm(1, "Other members' state may be inconsistent. Sent UNR to Surname Server just in case.\n");

                abortJoin();
            }

            if (findStatus != NotFinding)
            {
                findStatus = NotFinding;
                printf("Find timed out. Find cancelled.\n");
            }
        }

        // Read user commands from stdin
        if (FD_ISSET(STDIN_FILENO, &rfds))
        {
            fgets(buffer, 2047, stdin);
            parseCommand(buffer, &isRunning);

            printPrompt();
        }

        // Read messages from current call
        if (talkSocket != -1 && FD_ISSET(talkSocket, &rfds))
        {
            receiveMessage(buffer);
        }

        // Accept an imcoming call from the Call Server
        if (talkServerSocket != -1 && FD_ISSET(talkServerSocket, &rfds))
        {
            acceptCall();
        }

        // Handle a query or reply on the DNS
        if (dnsSocket != -1 && FD_ISSET(dnsSocket, &rfds))
        {
            parseServerCommand(&isRunning);
        }
    }

    // Loop ends when user wants to close program

    // Free memory
    emptyList(contacts);
    free(contacts);

    logm(1, "Exiting gracefully.\n");
    exit(0);
}


