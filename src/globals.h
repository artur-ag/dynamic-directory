#ifndef GLOBALS_H_INCLUDED
#define GLOBALS_H_INCLUDED

#include <arpa/inet.h>

#include "contact.h"
#include "list.h"

extern char* myName;
extern struct in_addr myIP;

extern struct in_addr saIP;
extern int saPort;
extern struct sockaddr_in saAddr;

/** UDP socket used to receive and request name queries to/from the Given Name Server and Surname Server. */
extern int dnsSocket;

/** UDP port for the DNS server. dnsSocket binds to this port. */
extern int myDnsPort;



/** TCP socket used to initiate a chat session. -1 when not in use. */
extern int talkSocket;

/** TCP server socket used to accept incoming chat sessions. -1 when not initialized. */
extern int talkServerSocket;

/** TCP port for the chat server. talkServerSocket binds to this port. */
extern int myTalkPort;


/** Linked list of all contacts with the same surname as ours. */
extern Node* contacts;

/** Contact who is the authorized Given Name Server (DNS) for our family. */
extern Contact* nameServer;
Contact* getNameServer();

void abortJoin();

typedef enum
{
    NotJoined,
    WaitForDNS,
    WaitForLST,
    WaitForOK,
    Joined,
    LeavingDNS,
    LeavingUsers,
    SearchingNewDns,
    LeavingForGood
} JoinStatus;

typedef enum
{
    NotFinding,
    WaitForFW,
    WaitForRPL
} FindStatus;

typedef enum
{
    FindForFind,
    FindForConnect
} FindMode;

/* State variables that store the state between select() cycles. */

extern JoinStatus joinStatus;
extern FindStatus findStatus;
extern FindMode findMode;
extern int oksExpected;
extern char nameToFind[NAME_LEN];

/** Indicates verbose mode. 0 prints nothing debug-related. Higher values print more info. */
extern int verbose;

#endif // GLOBALS_H_INCLUDED
