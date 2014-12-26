#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <ctype.h>
#include <errno.h>
extern int errno;

#include "globals.h"
#include "server.h"
#include "debug.h"

/** Contact who was requested to become the new DNS. Used during leave by the current DNS. */
Node* potentialDnsNode = NULL;

/** \brief Parses a message on the dnsSocket (Given Name Server).
 *
 * Checks the first word of the message and calls the appropriate handler.
 *
 */
void parseServerCommand()
{
    int ret;
    char buffer[2048];
    char cmd[16];

    // Receive message from DNS socket
    struct sockaddr_in addr;
    memset((void*)&addr, (int)'\0', sizeof(addr));
    socklen_t addrLen = sizeof(addr);

    ret = recvfrom(dnsSocket, buffer, 2047, 0, (struct sockaddr*) &addr, &addrLen);
    if (ret == -1)
    {
        perror("Error: could not receive message on dnsSocket");
        return;
    }

    // Always terminate the buffer with \0. This will never overwrite the received message.
    buffer[ret] = '\0';
    logm(2, "Received from %s: %s\n", inet_ntoa(addr.sin_addr), buffer);

    // Parse command (first word)
    ret = sscanf(buffer, "%15s", cmd);
    if (ret != 1)
    {
        printf("DNS Server got malformed message.\n");
        return;
    }

    // Switch command and call respective handler function
    if (strcmp("QRY", cmd) == 0)
    {
        replyToQuery(buffer, &addr, addrLen);
    }
    else if (strcmp("REG", cmd) == 0)
    {
        registerNewUser(buffer, &addr, addrLen);
    }
    else if (strcmp("UNR", cmd) == 0)
    {
        unregisterUser(buffer, &addr, addrLen);
    }
    else if (strcmp("LST", cmd) == 0)
    {
        receiveList(buffer, &addr, addrLen);
    }
    else if (strcmp("DNS", cmd) == 0)
    {
        if (joinStatus == WaitForDNS)
            continueJoin(buffer);
        else
            becomeDNS(buffer, &addr, addrLen);
    }
    else if (strcmp("OK", cmd) == 0)
    {
        if (joinStatus == LeavingUsers || joinStatus == LeavingDNS || joinStatus == SearchingNewDns)
            continueLeave(buffer, &addr, addrLen);

        else if (joinStatus == WaitForOK)
            continueJoinOK(&addr, addrLen);
    }
    else if (strcmp("FW", cmd) == 0)
    {
        continueFindFW(buffer);
    }
    else if (strcmp("RPL", cmd) == 0)
    {
        continueFindRPL(buffer);
    }
    else
    {
        if (joinStatus == SearchingNewDns)
            continueLeave(buffer, &addr, addrLen);
        else
            printf("DNS Server got unknown/unexpected message: %s\n", cmd);
    }

    return;
}

/** \brief Prepares the talk server socket.
 *
 * Opens the talkSocket global socket and binds it to the port myTalkPort.
 * This will be called when the program starts.
 *
 * \return int The file descriptor for the opened Talk Server socket
 *
 */
int prepareTalkServer()
{
    int ret;
    int serverSocket;
    struct sockaddr_in serverAddr;

    memset((void*)&serverAddr, (int)'\0', sizeof(serverAddr));

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(myTalkPort);

    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1)
    {
        perror("Could not open TCP server socket");
        exit(-1);
    }

    ret = bind(serverSocket, (struct sockaddr*) &serverAddr, sizeof(serverAddr));
    if (ret == -1)
    {
        perror("Could not bind TCP server socket");
        exit(-1);
    }

    // Keep up to 5 people in queue
    ret = listen(serverSocket, 5);
    if (ret == -1)
    {
        perror("Could not listen on TCP server socket");
        exit(-1);
    }

    return serverSocket;
}

/** \brief Responds to a QRY request.
 *
 * Searches for the requested contact and sends a RPL message back the requester
 * through the dnsSocket.
 *
 * \param buffer char* Contents of the QRY message. Will be overwritten with the RPL message.
 * \param addr struct sockaddr_in* Address of the sender. RPL will be sent to this.
 * \param addrLen socklen_t Length of the addr parameter.
 *
 */
void replyToQuery(char* buffer, struct sockaddr_in* addr, socklen_t addrLen)
{
    int ret;
    char name[128];

    // Format: QRY name.surname
    ret = sscanf(buffer, "%*s %127s", name);
    if (ret != 1)
    {
        printf("Malformed QRY message from %s.\n", inet_ntoa(addr->sin_addr));
        return;
    }

    // Find contact in local list
    Contact* c = get(contacts, name);

    // Prepare reply message
    if (c != NULL)
        sprintf(buffer, "RPL %s;%s;%d", name, inet_ntoa(c->ip), c->talkPort);
    else
        sprintf(buffer, "RPL");

    // Debug and logging
    logm(1, "%s\n", buffer);

    // Send back the reply
    ret = sendto(dnsSocket, buffer, strlen(buffer), 0, (struct sockaddr*) addr, addrLen);
    if (ret == -1)
    {
        perror("Could not send RPL to QRY");
        return;
    }

    return;
}

/** \brief Handles a request for a chat call.
 *
 * Accepts the chat call and sets the global variable talkSocket for the created socket.
 *
 * If there is a chat call already established, the call is accepted temporarily,
 * a human-readable rejection message is sent, and the temporary call is immediately closed.
 *
 */
void acceptCall()
{
    // If a call is in course, reject second call
    if (talkSocket != -1)
    {
        int rejectionSocket = accept(talkServerSocket, NULL, NULL);
        if (rejectionSocket == -1)
        {
            perror("Failed to properly reject second call");
            return;
        }

        // Send a message and immediately close socket
        char rejection[256];
        sprintf(rejection, "MSS %s;Sorry, I am busy right now.\n", myName);
        write(rejectionSocket, rejection, strlen(rejection));
        close(rejectionSocket);

        printf("Incoming call rejected.\n");
        return;
    }

    // Store address for pretty prints
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    memset((void*)&addr, (int)'\0', addrlen);

    // Accept call and store new socket in global variable
    talkSocket = accept(talkServerSocket, (struct sockaddr*) &addr, &addrlen);
    if (talkSocket == -1)
    {
        perror("Could not accept TCP call");
        printf("TCP request came from %s:%d\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
        return;
    }

    printf("Accepted call from %s:%d.\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
}

/** \brief Registers a new user at the local database.
 *
 * Sends an OK back if everything was ok. Sends a NOK if the user's surname is not ours.
 *
 * \param newUserREG char* REG message received.
 * \param addr struct sockaddr_in* Address of the sender. Will be used as new destination.
 * \param addrLen socklen_t Length of addr.
 *
 */
void registerNewUser(char* newUserREG, struct sockaddr_in* addr, socklen_t addrLen)
{
    int ret, ret2;

    // Add newly-received contact
    Contact* c = (Contact*) malloc(sizeof(Contact));

    // Skip first word, "REG".
    ret = sscanf(newUserREG, "%*s %127s", newUserREG);

    ret2 = getContactFromMsg(newUserREG, c);
    if (ret2 != 0 || ret != 1)
    {
        printf("Received malformed REG message.\n");
        free(c);
        return;
    }

    // Compare surnames, refuse if they do not match
    if (strcmp(strstr(myName, "."), strstr(c->name, ".")) != 0)
    {
        char nokMsg[192];
        // +1 to ignore '.' character
        sprintf(nokMsg, "NOK - You do not have my surname (%s)", strstr(myName, ".") + 1);

        ret = sendto(dnsSocket, nokMsg, strlen(nokMsg), 0, (struct sockaddr*) addr, addrLen);
        if (ret == -1)
        {
            perror("Could not send NOK message in reply to REG");
        }
        free(c);
        return;
    }

    // If received contact already exists on list, duplicate will be != NULL
    Contact* duplicate = get(contacts, c->name);

    if (duplicate == NULL)
    {
        add(contacts, c);
        logm(1, "Registered new user of same family: %s\n", c->name);
    }
    else
        logm(1, "User claims to be %s, but name already exists in database.\nSending empty LST.\n", c->name);

    // If we are the DNS, send LST to this contact
    if (nameServer != NULL && strcmp(myName, nameServer->name) == 0)
    {
        // Send this contact the current list of users
        char buffer[2048];
        char* caret = buffer;
        Node* n = contacts->next;

        caret += sprintf(caret, "LST\n");

        // If the contact is, in fact, a new user, send them the LST
        if (duplicate == NULL)
        {
            while (n != NULL)
            {
                // Format: name.surname;ipN;talkportN;dnsportN
                caret += sprintf(caret, "%s;%s;%d;%d\n",
                                 n->c->name,
                                 inet_ntoa(n->c->ip),
                                 n->c->talkPort,
                                 n->c->dnsPort);

                n = n->next;
            }
        }
        // Terminate LST with an empty line
        caret += sprintf(caret, "\n");

        ret = sendto(dnsSocket, buffer, strlen(buffer), 0, (struct sockaddr*) addr, addrLen);
        if (ret == -1)
        {
            perror("Could not send LST message");
            removeFrom(contacts, c->name);
            printf("Contact %s removed.\n", c->name);
            return;
        }

        logm(1, "Sent LST to contact %s.\n\n%s", c->name, buffer);
    }
    // If we are a regular user, just say OK
    else
    {
        char* okMsg = "OK";

        ret = sendto(dnsSocket, okMsg, strlen(okMsg), 0, (struct sockaddr*) addr, addrLen);
        if (ret == -1)
        {
            perror("Could not send OK message in reply to REG");
            removeFrom(contacts, c->name);
            printf("Contact %s removed.\n", c->name);
            return;
        }
    }
}

/** \brief Parses a contact data message and fills in a Contact data structure.
 *
 * The message should be of the format 'name.surname;IP;talkPort;dnsPort'.
 *
 * \param message char* Message of the specified format.
 * \param out_contact Contact* Pre-allocated Contact structure where the data will be written.
 * \return int 0 if the data was valid and the structure could be filled in. -1 if data could not be read.
 *
 */
int getContactFromMsg(char* message, Contact* out_contact)
{
    int ret;

    char nameBuf[128];
    char ipBuf[32];

    // Parse format: name.surname;IP;talkPort;dnsPort
    ret = sscanf(message, "%[^;];%[^;];%d;%d", nameBuf, ipBuf, &(out_contact->talkPort), &(out_contact->dnsPort));
    if (ret != 4)
    {
        logm(1, "Bad format on getContactFromMsg, format.\n%s\n", message);
        return -1;
    }

    // Verify name format: name.surname
    if (strstr(nameBuf, ".") == NULL)
    {
        logm(1, "Bad format on getContactFromMsg, name does not have .surname\n");
        return -1;
    }

    // Copy name
    strcpy(out_contact->name, nameBuf);

    // Copy IP address
    ret = inet_aton(ipBuf, &(out_contact->ip));
    if (ret == 0)
    {
        logm(1, "Bad format on getContactFromMsg, IP.\n%s\n", message);
        return -1;
    }

    return 0;
}

/** \brief Handles a UNR message.
 *
 * Removes the user in question from the local database and sends an OK back.
 * Sends OK even if user did not exist in local database.
 *
 * \param buffer char* Received UNR message in the format 'UNR name.surname'.
 * \param addr struct sockaddr_in* Address of the sender. Will send OK to this.
 * \param addrLen socklen_t Length of addr.
 *
 */
void unregisterUser(char* buffer, struct sockaddr_in* addr, socklen_t addrLen)
{
    int ret;
    char name[128];

    ret = sscanf(buffer, "%*s %127s", name);
    if (ret != 1)
    {
        printf("Malformated UNR message coming from %s. Ignoring:\n%s", inet_ntoa(addr->sin_addr), buffer);
        return;
    }

    // Our DNS is leaving. Delete its cached data
    if (nameServer != NULL && strcmp(name, nameServer->name) == 0)
    {
        logm(1, "My DNS %s is leaving. Gotta ask the SS who the new DNS is.\n", nameServer->name);

        nameServer = NULL;
    }

    Contact* cToRemove = get(contacts, name);
    if (joinStatus == SearchingNewDns
        && potentialDnsNode != NULL
        && strcmp(potentialDnsNode->c->name, cToRemove->name) == 0)
    {
        // Contact to which we sent the DNS request is also leaving. Consider next contact
        potentialDnsNode = potentialDnsNode->next;
    }

    // Remove contact from local database
    ret = removeFrom(contacts, name);
    if (ret == -1)
    {
        printf("Contact %s sent UNR message but does not exist in local database. Sending OK anyway.\n", name);
    }
    else
    {
        logm(1, "Unregistering %s.\n", name);
    }

    char* okMsg = "OK";

    // Send OK reply
    ret = sendto(dnsSocket, okMsg, strlen(okMsg), 0, (struct sockaddr*) addr, addrLen);
    if (ret == -1)
    {
        printf("Could not send OK to reply UNR to contact %s:", name); perror("");
    }
}

/** \brief Continues join sequence after REG to SS: handles DNS message.
 *
 * The DNS contact is added to the local database.
 *
 * If we are the DNS, the join sequence is completed.
 * Otherwise, this sends a REG message to the DNS and puts the program in WaitForLST state.
 *
 * \param buffer char* DNS message of the format 'DNS name.surname;ip.ip.ip.ip;dnsport'.
 *
 */
void continueJoin(char* buffer)
{
    int n;

    // Message should be of the format
    // DNS name.surname;ip.ip.ip.ip;dnsport

    Contact* server = malloc(sizeof(Contact));

    char ipBuf[128];

    // Parse name, dnsPort, save ip onto buffer
    n = sscanf(buffer, "DNS %[^;];%[^;];%d", (server->name), ipBuf, &(server->dnsPort));
    if (n != 3)
    {
        printf("Server replied abnormally.\n");
        joinStatus = NotJoined;
        emptyList(contacts);
        return;
    }

    n = inet_aton(ipBuf, &(server->ip));
    if (n == 0)
    {
        printf("Server replied abnormally: DNS IP invalid.\n");
        joinStatus = NotJoined;
        return;
    }

    nameServer = server;

    // Add DNS to contacts (could be ourselves)
    add(contacts, server);

    // Check who the Given Name Server is
    if (strcmp(server->name, myName) == 0)
    {
        // We are the first user with this surname
        joinStatus = Joined;

        // Only we know our own talk port at first
        server->talkPort = myTalkPort;

        printf("Joined successfully.\n");
    }
    else
    {
        // Add ourselves to list of contacts
        Contact* me = (Contact*) malloc(sizeof(Contact));
        strcpy(me->name, myName);
        me->ip = myIP;
        me->dnsPort = myDnsPort;
        me->talkPort = myTalkPort;
        setDnsAddr(me);
        add(contacts, me);

        // Someone else is the server: contact him to get the list of everyone with our surname
        joinStatus = WaitForLST;

        // Prepare REG message again, this time for DNS
        sprintf(buffer, "REG %s;%s;%d;%d", myName, inet_ntoa(myIP), myTalkPort, myDnsPort);

        // Prepare DNS's address
        struct sockaddr_in sendAddr;
        memset((void*)&sendAddr, (int)'\0', sizeof(sendAddr));
        sendAddr.sin_family = AF_INET;
        sendAddr.sin_addr = server->ip;
        sendAddr.sin_port = htons(server->dnsPort);

        // Send message to DNS, await for LST message
        n = sendto(dnsSocket, buffer, strlen(buffer), 0, (struct sockaddr*) &sendAddr, sizeof(sendAddr));
        if (n == -1)
        {
            perror("Could not send REG message to DNS");
            joinStatus = NotJoined;
            return;
        }

        joinStatus = WaitForLST;
    }
}

/** \brief Continues join sequence after REG to DNS: handles LST message.
 *
 * Parses the LST message and fills in the local database with the contact data.
 * Sends a registration (REG) message to every contact in the list except ourselves and the DNS.
 * Sets the global variable oksExpected accordingly. Puts the program in the WaitForOK state.
 *
 * \param buffer char* The received LST message.
 *
 */
void receiveList(char* buffer)
{
    int ret, i;

    if (joinStatus != WaitForLST)
    {
        logm(1, "Received LST without asking for one. Ignoring.\n");
        return;
    }

    char regBuffer[128];
    sprintf(regBuffer, "REG %s;%s;%d;%d", myName, inet_ntoa(myIP), myTalkPort, myDnsPort);

    // Pointer to the beginning of second line (ignore LST line)
    char* caret = strchr(buffer, '\n') + 1;

    if (caret == NULL)
    {
        printf("Received malformed LST. Cancelling join.\n");
        logm(1, "%s\n", buffer);
        joinStatus = NotJoined;
        close(dnsSocket);
        dnsSocket = -1;
        emptyList(contacts);
        nameServer = NULL;
        return;
    }

    // Reset OKs counter
    oksExpected = 0;

    Contact* c;

    // Check for empty LST
    if (*caret == '\n' || *caret == '\0')
    {
        // DNS refused to aknowledge us, we probably have a duplicated name
        printf("DNS refused registration. Another user has the name %s.\n", myName);
        abortJoin();
        return;
    }

    /* Format: name.surname;ipN;talkportN;dnsportN
        According to the specification, the last line should be a '\n' by itself,
        but everyone seems to have overlooked that detail. We better play safe
        and check for the normal '\0' termination as well. */
    for (i = 1; *caret != '\n' && *caret != '\0'; i++)
    {
        // Because it all comes in one single packet, we better impose a limit to avoid and endless loop
        if (i > 65535)
            break;

        c = (Contact*) malloc(sizeof(Contact));
        ret = getContactFromMsg(caret, c);
        if (ret != 0)
        {
            printf("Error on LST, line %d. Ignoring contact.\n", i);
            free(c);
            continue;
        }

        // Find the next line
        caret = strchr(caret + 1, '\n') + 1;
        if (caret == NULL)
        {
            printf("Error on LST, line %d. Join may have been left incomplete.\n", i);
        }

        // Add new contacts, skip ourselves and authorized DNS
        if (strcmp(c->name, myName) != 0 && strcmp(c->name, nameServer->name) != 0)
        {
            add(contacts, c);
        }
        else
        {
            // Store the DNS's talkport, since we didn't get it from the SS
            if (strcmp(c->name, nameServer->name) == 0)
                nameServer->talkPort = c->talkPort;

            // No need for two contacts with the DNS's info
            free(c);
            continue;
        }

        // Prepare contacts's address
        setDnsAddr(c);

        // Send REG message to contact, will receive OK later on
        ret = sendto(dnsSocket, regBuffer, strlen(regBuffer), 0, (struct sockaddr*) &(c->dnsAddr), sizeof(c->dnsAddr));
        if (ret == -1)
        {
            perror("Could not send REG to same-surname contact. Aborting join.");
            abortJoin();
            break;
        }

        logm(1, "Sent REG message to %s.\n", c->name);

        // We have to keep track of how many OKs we're expecting later
        c->okExpected = 1;
        oksExpected++;

        // Debug and logging
        logm(1, "Added contact %s to contact list.\n", c->name);
    }

    if (oksExpected == 0)
    {
        printf("Join into existing family successful.\n");
        joinStatus = Joined;
    }
    else
        joinStatus = WaitForOK;
}

/** \brief Continues join sequence after LST: handles OKs.
 *
 * Checks the sender address against the expected addresses. Decrements the oksExpected.
 * Once all OKs have been gotten, the join sequence is complete.
 *
 * \param addr struct sockaddr_in* Sender address.
 * \param addrLen socklen_t Length of addr.
 *
 */
void continueJoinOK(struct sockaddr_in* addr, socklen_t addrLen)
{
    Contact* c = getByAddr(contacts, addr, addrLen);

    if (c != NULL && c->okExpected == 1)
    {
        logm(1, "OK addr matched: came from %s\n", c->name);
        c->okExpected = 0;
        oksExpected--;
    }
    else
        logm(1, "OK addr did not match any contact...\n");

    if (oksExpected == 0)
    {
        printf("Joined successfully.\n");
        joinStatus = Joined;
    }
}

/** \brief Continues the leave sequence after UNRs: handles OKs.
 *
 * On LeavingUsers state, receives the OKs from every family member after sending out the UNR messages.
 * On LeavingDNS, sends a DNS message, requesting another user to become the new DNS.
 * On SearchingNewDNS, expects an OK and sends DNS update to Surname Server, or a NOK and asks another user to become the DNS.
 * On LeavingForGood, wraps up and completes the leave sequence.
 *
 * The original protocol did not define a rejection reply to the DNS request.
 * 'NOK' is used, but anything other than 'OK' will work as rejection.
 *
 * \param buffer char* Message received. Normally OK, but may be anything during SearchingNewDNS state.
 * \param addr struct sockaddr_in* Sender address.
 * \param addrLen socklen_t Length of addr.
 *
 */
void continueLeave(char* buffer, struct sockaddr_in* addr, socklen_t addrLen)
{
    int ret;

    logm(1, "%s\n", buffer);

    if (joinStatus == LeavingUsers)
    {
        Contact* c = getByAddr(contacts, addr, addrLen);

        if (c != NULL && c->okExpected == 1)
        {
            logm(1, "OK addr matched: came from %s. %d OKs left...\n", c->name, oksExpected);
            c->okExpected = 0;
            oksExpected--;
        }
        else
            logm(1, "OK addr did not match any contact...\n");

        if (oksExpected == 0)
            joinStatus = LeavingDNS;
    }

    if (joinStatus == SearchingNewDns)
    {
        char cmd[16];
        sscanf(buffer, "%15s", cmd);

        if (strcmp("OK", cmd) == 0)
        {
            // Peer accepted to be the new DNS. We can leave now
            joinStatus = LeavingForGood;
            nameServer = NULL;

            Contact* foundDns = potentialDnsNode->c;
            sprintf(buffer, "DNS %s;%s;%d", foundDns->name, inet_ntoa(foundDns->ip), foundDns->dnsPort);

            logm(1, "Peer %s is willing to be the new DNS. We can leave now.\n", foundDns->name);

            joinStatus = LeavingForGood;

            // Send DNS change to Surname Server
            ret = sendto(dnsSocket, buffer, strlen(buffer), 0, (struct sockaddr*) &saAddr, sizeof(saAddr));
            if (ret == -1)
            {
                perror("Could not send new DNS to Surname Server for leaving. Leaving anyway");
            }

            ret = recvfrom(dnsSocket, buffer, 16, 0, NULL, NULL);
        }
    }

    if (joinStatus == LeavingDNS || joinStatus == SearchingNewDns)
    {
        // We were this family's DNS. Nominate a new DNS.
        getNameServer();
        if (strcmp(nameServer->name, myName) == 0)
        {
            joinStatus = SearchingNewDns;

            if (potentialDnsNode == NULL)
            {
                potentialDnsNode = contacts->next;
            }
            else
            {
                potentialDnsNode = potentialDnsNode->next;
            }

            // Search for contact who is NOT us
            while (potentialDnsNode != NULL && strcmp(potentialDnsNode->c->name, myName) == 0)
            {
                potentialDnsNode = potentialDnsNode->next;
            }

            if (potentialDnsNode == NULL)
            {
                logm(1, "All peers refused to become DNS. Leaving anyway.\n");
                joinStatus = LeavingForGood;
            }
            else
            {
                logm(1, "Considering %s to be the new DNS.\n", potentialDnsNode->c->name);
                Contact* newDns = potentialDnsNode->c;

                char tmpBuffer[128];
                sprintf(tmpBuffer, "DNS %s;%s;%d", newDns->name, inet_ntoa(newDns->ip), newDns->dnsPort);

                // Propose a contact to become the DNS (he/she may refuse)
                struct sockaddr_in newDnsAddr;
                memset((void*)&newDnsAddr, (int)'\0', sizeof(newDnsAddr));
                newDnsAddr.sin_family = AF_INET;
                newDnsAddr.sin_addr = newDns->ip;
                newDnsAddr.sin_port = htons(newDns->dnsPort);

                ret = sendto(dnsSocket, tmpBuffer, strlen(tmpBuffer), 0, (struct sockaddr*) &newDnsAddr, sizeof(newDnsAddr));
                if (ret == -1)
                {
                    perror("Could not send DNS request to peer");
                    printf("Leaving forcefully.\n");
                    joinStatus = LeavingForGood;
                }

                // Await an OK
                joinStatus = SearchingNewDns;
            }
        }
        else
        {
            // We are not the DNS, we are just a regular user
            // And we've sent UNRs to everyone, our work is done
            joinStatus = LeavingForGood;
        }
    }

    if (joinStatus == LeavingForGood)
    {
        potentialDnsNode = NULL;

        emptyList(contacts);
        nameServer = NULL; // was in the list, has already been freed

        close(dnsSocket);
        dnsSocket = -1;

        joinStatus = NotJoined;

        printf("Left successfully.\n");
    }
}

/** \brief Continues the find sequence, after the Surname Server replies with a FW.
 *
 * Parses the FW, gets the target's DNS and sends it a new QRY.
 * May stop the find if the reply was simply "FW" without user data.
 *
 * \param buffer char* Buffer containing the FW from the SS.
 *
 */
void continueFindFW(char* buffer)
{
    int ret;
    char reply[16];
    char info[128 - 16];

    if (findStatus != WaitForFW)
    {
        logm(1, "Got unexpected FW. Ignoring.\n");
        return;
    }

    // Parse reply
    // Format: FW name.surname;authip;authdnsport
    // ....or: FW
    ret = sscanf(buffer, "%15s %111s", reply, info);
    if (ret != 1 && ret != 2)
    {
        printf("Abnormal FW message gotten. Find failed.\n");
        findStatus = NotFinding;
        return;
    }
    else if (ret == 1)
    {
        // User did not exist
        printf("User %s could not be found.\n", nameToFind);
        findStatus = NotFinding;
        return;
    }

    char seps[] = ";";

    strtok(info, seps); // ignore name
    char* ipStr = strtok(NULL, seps);
    char* tpStr = strtok(NULL, seps);

    // Prepare addr of the authorized DNS for the find-person's surname
    struct sockaddr_in dnsAddr;
    memset((void*)&dnsAddr, (int)'\0', sizeof(dnsAddr));
    dnsAddr.sin_family = AF_INET;
    inet_aton(ipStr, &dnsAddr.sin_addr);
    dnsAddr.sin_port = htons(atoi(tpStr));

    // Prepare QRY message again
    sprintf(buffer, "QRY %s", nameToFind);

    // Debug and logging
    logm(1, "Message:  %sDestination: %s : %d\n", buffer, inet_ntoa(dnsAddr.sin_addr), ntohs(dnsAddr.sin_port));

    ret = sendto(dnsSocket, buffer, strlen(buffer), 0, (struct sockaddr*) &dnsAddr, sizeof(dnsAddr));
    if (ret == -1)
    {
        perror("Could not send QRY to DNS");
        printf("User %s could not be found.\n", nameToFind);
        findStatus = NotFinding;
        return;
    }

    findStatus = WaitForRPL;
}

/** \brief Continues the find sequence, after the DNS replies with a RPL.
 *
 * Depending on the findMode, prints the found information, or uses it to start a chat call.
 * Prints warning if user was not found (empty RPL message).
 *
 * \param buffer char* Message of the form 'RPL[ name.surname;ip;talkport]'.
 *
 */
void continueFindRPL(char* buffer)
{
    int ret;

    char cmd[16];
    char info[128-16];

    findStatus = NotFinding;

    ret = sscanf(buffer, "%15s %111s", cmd, info);
    if (ret != 2)
    {
        printf("User %s not found.\n", nameToFind);
        return;
    }

    if (strcmp(cmd, "RPL") != 0)
    {
        printf("Got unexpected message during find from the DNS.\n");
        return;
    }

    if (strcmp(buffer, "RPL") == 0)
    {
        printf("User %s not found.\n", nameToFind);
        return;
    }

    // Format: RPL name.surname;ip;talkport
    char name[128];
    char ipStr[128];
    int talkPort;

    ret = sscanf(buffer, "RPL %[^;];%[^;];%d", name, ipStr, &talkPort);
    if (ret != 3)
    {
        printf("Given Name Server replied abnormally. User %s not found.\n", nameToFind);
        return;
    }

    if (findMode == FindForFind)
    {
        printf("User %s is at %s:%d.\n", name, ipStr, talkPort);
    }
    else if (findMode == FindForConnect)
    {
        struct sockaddr_in peerAddr;
        memset((void*) &peerAddr, (int) '\0', sizeof(peerAddr));
        peerAddr.sin_family = AF_INET;
        inet_aton(ipStr, &peerAddr.sin_addr);
        peerAddr.sin_port = htons(talkPort);

        startChatCall(name, peerAddr);
    }
}

/** \brief Starts a TCP call with a user.
 *
 * \param name char* Name of the user, for debug prints.
 * \param peerAddr struct sockaddr_in TCP Socket address of the peer.
 *
 */
void startChatCall(char* name, struct sockaddr_in peerAddr)
{
    int ret;

    if (talkSocket != -1)
    {
        printf("Already connected to someone. Only one chat session supported.\n");
        return;
    }

    talkSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (talkSocket == -1)
    {
        perror("Could not open TCP socket to user");
        return;
    }

    ret = connect(talkSocket, (struct sockaddr*) &peerAddr, sizeof(peerAddr));
    if (ret == -1)
    {
        perror("Could not connect TCP socket to user");
        close(talkSocket);
        talkSocket = -1;
        return;
    }

    printf("Connected to user %s.\n", name);
}

/** \brief Handles a request to become DNS.
 *
 * Sends OK and sets the global variable nameServer if request was accepted.
 * Sends NOK otherwise. Request will not be accepted if the user is already leaving.
 * Message will be ignored if it does not contain our name.
 *
 * \param buffer char* DNS message received
 * \param addr struct sockaddr_in* Address of the sender. (N)OK will be sent to this.
 * \param addrLen socklen_t Length of addr.
 *
 */
void becomeDNS(char* buffer, struct sockaddr_in* addr, socklen_t addrLen)
{
    // If we are not joined and stable, refuse the DNS promotion
    // Let the leaving DNS choose another user to be the DNS

    int ret;

    logm(1, "Got request to become DNS.\n");

    // Check message validity
    char otherName[NAME_LEN];
    ret = sscanf(buffer, "DNS %[^;]", otherName);
    if (ret != 1)
    {
        if (sendto(dnsSocket, "NOK", strlen("NOK"), 0, (struct sockaddr*) addr, addrLen) == -1)
        {
            perror("Could not send NOK to DNS request");
        }
        return;
    }

    if (joinStatus <= Joined)
    {
        // Check if the name is actually ours
        if (strcmp(myName, otherName) != 0)
        {
            logm(1, "DNS request did not have our name. Replying with NOK.\n");

            char* nokMsg = "NOK - That was not my name";
            ret = sendto(dnsSocket, nokMsg, strlen(nokMsg), 0, (struct sockaddr*) addr, addrLen);
            if (ret == -1)
                perror("Could not send NOK in reply to DNS request");
            return;
        }

        // From now on, we are the new DNS
        nameServer = get(contacts, otherName);

        char* okMsg = "OK";

        ret = sendto(dnsSocket, okMsg, strlen(okMsg), 0, (struct sockaddr*) addr, addrLen);
        if (ret == -1)
        {
            perror("Could not send OK to become the new DNS");
        }

        logm(1, "Became the DNS by request of %s.\n", inet_ntoa(addr->sin_addr));
    }
    else
    {
        char* okMsg = "NOK - Not fully joined, can't be DNS.";

        ret = sendto(dnsSocket, okMsg, strlen(okMsg), 0, (struct sockaddr*) addr, addrLen);
        if (ret == -1)
        {
            perror("Could not send NOK to reject becoming DNS");
        }

        logm(1, "Refused to become DNS, as %s requested.\n", inet_ntoa(addr->sin_addr));
    }
}

/** \brief Handles a received message on the Talk Socket during a chat call.
 *
 * Prints the received message in human-redable form to STDOUT.
 * Attempts to separate different messages marked by MSS.
 *
 * \param buffer char* Received message, including any MSS and name of the sender.
 *
 */
void receiveMessage(char* buffer)
{
    int nRead = read(talkSocket, buffer, 2047);

    if (nRead <= 0)
    {
        close(talkSocket);
        talkSocket = -1;

        if (nRead == 0)
            printf("Connection closed by partner.\n");
        else
            printf("Connection forcefully closed by partner.\n");

        return;
    }

    // Make absolutely sure we won't print garbage
    buffer[nRead] = '\0';

    char nameBuf[NAME_LEN];
    char messageBuf[2048];
    char* mssStart = strstr(buffer, "MSS ");

    // Print remainder of last message
    if (mssStart != NULL)
        *mssStart = '\0';

    printf("%s", buffer);

    if (mssStart != NULL)
        *mssStart = 'M';

    // Print any new MSSs
    while (mssStart != NULL)
    {
        printf("\n");

        int ret = sscanf(mssStart, "MSS %[^;];%[^;]", nameBuf, messageBuf);
        if (ret != 2)
        {
            printf("Incomplete MSS header\n");
            printf("%s", mssStart);
        }
        else
        {
            // Terminate current message at start of eventual next MSS
            char* nextMss = strstr(messageBuf, "MSS ");
            if (nextMss != NULL)
            {
                *nextMss = '\0';
            }

            // Print formatted message to user
            printf("%s: %s", nameBuf, messageBuf);
        }

        // Find next MSS (start beyond current MSS)
        mssStart = strstr(mssStart + 4, "MSS");
    }

    // Flush even if message did not contain '\n'
    fflush(stdout);
}
