#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include "debug.h"
#include "globals.h"
#include "list.h"

/** Definitions of global variables. */

char* myName;
struct in_addr myIP;

struct in_addr saIP;
int saPort;
struct sockaddr_in saAddr;

int dnsSocket = -1;
int myDnsPort;

int talkSocket = -1;
int talkServerSocket = -1;
int myTalkPort;


Node* contacts;
Contact* nameServer = NULL;

/** \brief Ensures the nameServer global variable is up to date.
 *
 * Immediately after the DNS leaves, the other users do not know who is the new DNS.
 * Calling this ensures the new DNS is known, by asking the Surname Server again.
 *
 * \return Contact* The nameServer global variable.
 *
 */
Contact* getNameServer()
{
    if (nameServer == NULL && joinStatus >= Joined)
    {
        logm(1, "DNS not known. Getting DNS again...");

        int ret;

        char buffer[128];
        sprintf(buffer, "QRY %s", myName);

        ret = sendto(dnsSocket, buffer, strlen(buffer), 0, (struct sockaddr*) &saAddr, sizeof(saAddr));
        if (ret == -1)
        {
            perror("Could not get DNS from SS");
        }

        ret = recvfrom(dnsSocket, buffer, 127, 0, NULL, NULL);
        if (ret == -1)
        {
            perror("Could not receive DNS from SS");
        }

        char dnsName[128];
        sscanf(buffer, "FW %[^;]", dnsName);
        nameServer = get(contacts, dnsName);

        if (nameServer == NULL)
        {
            logm(1, "Could not find DNS: SS indicated %s.", dnsName);
        }
    }

    return nameServer;
}

/** \brief Aborts Join and leaves program in a completely unjoined state.
 *
 * Closes the dnsSocket, empties the contact list, sets the global status variable to NotJoined.
 *
 */
void abortJoin()
{
    if (dnsSocket != -1)
    {
        // If we are the DNS, make a last attempt to leave the Surname Server consistent
        if (joinStatus >= WaitForDNS
            && nameServer != NULL
            && strcmp(nameServer->name, myName) == 0)
        {
            char buf[128];
            sprintf(buf, "UNR %s", myName);
            sendto(dnsSocket, buf, strlen(buf), 0, (struct sockaddr*) &saAddr, sizeof(saAddr));
        }

        close(dnsSocket);
        dnsSocket = -1;
    }
    emptyList(contacts);
    joinStatus = NotJoined;
}

JoinStatus joinStatus = NotJoined;
FindStatus findStatus = NotFinding;
FindMode findMode = FindForFind;
int oksExpected;

char nameToFind[128];

int verbose = 0;
