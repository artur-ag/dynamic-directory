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
#include "commands.h"
#include "server.h"
#include "debug.h"
#include "list.h"

/** \brief Parses a command from the keyboard (STDIN) and handles it.
 *
 * \param line char* Line gotten with fgets from STDIN.
 * \param isRunning int* Running state of the program. Will be set to 0 if the user wants to exit the program.
 *
 */
void parseCommand(char* line, int* isRunning)
{
    int i;
    char command[32];
    char argument[512 - 32];

    // Get first word (command)
    sscanf(line, "%31s", command);
    command[31] = '\0';
    int size = strlen(command);
    for (i = 0; i < size; i++)
        command[i] = (char) tolower((int) command[i]);

    if (strcmp(command, "join") == 0)
    {
        join();
    }
    else if (strcmp(command, "find") == 0)
    {
        sscanf(line, "%*s %s", argument);
        find(argument, FindForFind);
    }
    else if (strcmp(command, "connect") == 0)
    {
        sscanf(line, "%*s %s", argument);
        find(argument, FindForConnect);
    }
    else if (strcmp(command, "message") == 0)
    {
        sendMessage(&(line[8]));
    }
    else if (strcmp(command, "m") == 0)
    {
        sendMessage(&(line[2]));
    }
    else if (strcmp(command, "mraw") == 0)
    {
        sendRawMessage(&(line[5]));
    }
    else if (strcmp(command, "disconnect") == 0)
    {
        disconnect();
    }
    else if (strcmp(command, "leave") == 0)
    {
        leave();
    }
    else if (strcmp(command, "exit") == 0)
    {
        // Simulate leave command before actually exiting
        if (joinStatus == Joined)
            leave();

        // This causes loop in main() to stop
        *isRunning = 0;
    }
    else if (strcmp(command, "verbose") == 0)
    {
        sscanf(line, "%*s %d", &verbose);
        printf("Verbose level changed to %d.\n", verbose);
    }
    else if (strcmp(command, "help") == 0)
    {
        help();
    }
    else if (strcmp(command, "list") == 0)
    {
        printList(contacts);
    }
    else if (strcmp(command, "rickroll") == 0)
    {
        rickroll();
    }
    else if (strcmp(command, "status") == 0)
    {
        printState();
    }
    else
    {
        printf("Unrecognized command. Type 'help' for a list of valid commands.\n");
    }
 }

/** \brief Starts a 'join' sequence.
 *
 * Sets the global variable saAddr to the Surname Server's address. Opens the DNS socket and binds it.
 * Sends a REG to the Surname Server.
 *
 */
void join()
{
    if (joinStatus != NotJoined)
    {
        printf("Cannot join again, already joined. DNS is %s.\n",
               (nameServer != NULL) ? nameServer->name : "being contacted");
        return;
    }

    int n;

    // Prepare address for Surname Server
    memset((void*)&saAddr, (int)'\0', sizeof(saAddr));
    saAddr.sin_family = AF_INET;
    saAddr.sin_addr = saIP;
    saAddr.sin_port = htons(saPort);

    // Open socket for given name DNS Server (SNP) and bind to dnsPort
    dnsSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (dnsSocket == -1)
    {
        perror("Could not open UDP server socket to DNS Server");
        return;
    }

    struct sockaddr_in dnsAddr;
    memset((void*)&dnsAddr, (int)'\0', sizeof(dnsAddr));
    dnsAddr.sin_family = AF_INET;
    dnsAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    dnsAddr.sin_port = htons(myDnsPort);

    n = bind(dnsSocket, (struct sockaddr*)&dnsAddr, sizeof(dnsAddr));
    if (n == -1)
    {
        perror("Could not bind DNS socket");
        printf("Port attempted: %d\n", myDnsPort);
        abortJoin();
        return;
    }

    // Prepare registration message and send it
    char buffer[128];
    n = getRegMessage((char*) buffer);
    n = sendto(dnsSocket, buffer, strlen(buffer), 0, (struct sockaddr*) &saAddr, sizeof(saAddr));
    if (n == -1)
    {
        perror("Could not send REG to Surname Server");
        abortJoin();
        return;
    }

    // Debug and logging
    logm(1, "%s", buffer);

    joinStatus = WaitForDNS;
    return;


}

/** \brief Prints this user's REG message into a buffer of 128 bytes.
 *
 * Exits if the message is too long to fit in the buffer.
 *
 * \param buffer char* Buffer to be written.
 * \return int Number of bytes written to buffer.
 *
 */
int getRegMessage(char* buffer)
{
    int n;
    n = snprintf(buffer, 128, "REG %s;%s;%d;%d", myName, inet_ntoa(myIP), myTalkPort, myDnsPort);
    if (n >= 128)
    {
        printf("Error: REG message is too long. Choose a shorter name.\n");
        exit(-1);
    }
    return n;
}

/** \brief Starts a 'find' sequence.
 *
 * Only works if the used is fully Joined.
 * Changes the global variable findMode to the parameter 'mode'.
 * If 'mode' is FindForFind, the user's address will be printed to screen.
 * If 'mode' is FindForConnect, a chat call will be established once the target address is known.
 *
 * \param name char* Name of the target.
 * \param mode FindMode FindForFind or FindForConnect
 * \return void
 *
 */
void find(char* name, FindMode mode)
{
    int ret;
    char buffer[128];
    char targetName[128];

    if (joinStatus != Joined)
    {
        printf("Not joined yet. Must join before finding contacts.\n");
        return;
    }

    if (findStatus != NotFinding)
    {
        printf("Already trying to find %s. Try again later.\n", nameToFind);
        return;
    }

    // Set the global variable so server.c can access it later
    findMode = mode;

    if (talkSocket != -1 && findMode == FindForConnect)
    {
        printf("Already connected to someone. Only one chat session supported.\n");
        return;
    }

    // If target has the same surname, we can ask our DNS directly, to spare the Surname Server.
    int targetIsFamily = 0;

    // Prepare QRY message, with our surname by default if none is written
    if (strstr(name, ".") != NULL)
    {
        sprintf(targetName, "%s", name);
        char* targetSurname = strstr(name, ".");
        char* mySurname = strstr(myName, ".");

        if (strcmp(targetSurname, mySurname) == 0)
            targetIsFamily = 1;
    }
    else
    {
        sprintf(targetName, "%s%s", name, strstr(myName, "."));
        targetIsFamily = 1;
    }

    if (!targetIsFamily)
    {
        // Target is a stranger. Gotta ask the SS

        sprintf(buffer, "QRY %s", targetName);

        // Debug and logging
        logm(1, "%s", buffer);

        ret = sendto(dnsSocket, buffer, strlen(buffer), 0, (struct sockaddr*) &saAddr, sizeof(saAddr));
        if (ret == -1)
        {
            perror("Could not send QRY to SS");
            exit(-1);
        }

        findStatus = WaitForFW;
    }
    else
    {
        // Target is from my family. We have the user data in our local database

        Contact* c = get(contacts, targetName);
        if (c == NULL)
        {
            printf("User %s not found.\n", targetName);
            return;
        }

        // Connect to user directly
        if (findMode == FindForConnect)
        {
            struct sockaddr_in peerAddr;
            memset((void*) &peerAddr, (int) '\0', sizeof(peerAddr));
            peerAddr.sin_family = AF_INET;
            peerAddr.sin_addr = c->ip;
            peerAddr.sin_port = htons(c->talkPort);

            startChatCall(targetName, peerAddr);
        }
        // Print found information
        else
        {
            printf("User %s is at %s:%d.\n", c->name, inet_ntoa(c->ip), c->talkPort);
        }

        findStatus = NotFinding;

        // Deprecated code. Sent a QRY to the family's DNS, which is unneeded and wasteful, but worked.
//        // Ensures we know who the DNS is, even after a leave
//        getNameServer();
//
//        // Prepare addr of the authorized DNS for the find-person's surname
//        struct sockaddr_in dnsAddr;
//        memset((void*)&dnsAddr, (int)'\0', sizeof(dnsAddr));
//        dnsAddr.sin_family = AF_INET;
//        dnsAddr.sin_addr = nameServer->ip;
//        dnsAddr.sin_port = htons(nameServer->dnsPort);
//
//        // Debug and logging
//        logm(1, "Message:  %sDestination: %s : %d\n", buffer, inet_ntoa(dnsAddr.sin_addr), ntohs(dnsAddr.sin_port));
//
//        ret = sendto(dnsSocket, buffer, strlen(buffer), 0, (struct sockaddr*) &dnsAddr, sizeof(dnsAddr));
//        if (ret == -1)
//        {
//            perror("Could not send QRY to DNS");
//            printf("User %s could not be found.\n", nameToFind);
//            findStatus = NotFinding;
//            return;
//        }
//
//        findStatus = WaitForRPL;
    }


    strcpy(nameToFind, targetName);
}

/** \brief Sends a message through the chat call.
 *
 * \param message char* Message to be sent.
 *
 */
void sendMessage(char* message)
{
    char buffer[2048 + 512];

    if (talkSocket == -1)
    {
        printf("Cannot send message before connecting.\n");
        return;
    }

    sprintf(buffer, "MSS %s;%s", myName, message);

    int len = strlen(buffer);
    int sentbytes = 0;

    // Ensure 'len' bytes are sent
    while (sentbytes < len)
    {
        sentbytes = send(talkSocket, buffer, len - sentbytes, 0);

        if (sentbytes == -1)
        {
            perror("Could not send chat-message");
            break;
        }
    }

    logm(1, "Message sent.\n");
}

/** \brief Sends a raw string through the chat call. For debug purposes.
 *
 * \param message char* Message to be sent.
 *
 */
void sendRawMessage(char* message)
{
    if (talkSocket == -1)
    {
        printf("Cannot send message before connecting.\n");
        return;
    }

    int len = strlen(message);
    int sentbytes = 0;

    // Ensure 'len' bytes are sent
    while (sentbytes < len)
    {
        sentbytes = send(talkSocket, message, len - sentbytes, 0);

        if (sentbytes == -1)
        {
            perror("Could not send raw message on Chat socket");
            break;
        }
    }

    logm(1, "Raw string sent.\n");
}

/** \brief Ends the current chat call.
 *
 * Marks the talkSocket as unused and closes it.
 * This will terminate the call for the partner used as well.
 *
 */
void disconnect()
{
    if (talkSocket == -1)
    {
        printf("There is no call to disconnect.\n");
        return;
    }

    int ret;

    ret = close(talkSocket);
    if (ret == -1)
    {
        perror("Could not close call socket");
    }

    // Mark global variable as unused
    talkSocket = -1;
}

/** \brief Disconnects the user. Starts the 'leave' sequence.
 *
 * Puts the program in the LeavingDNS or LeavingUsers states.
 * Sets the global variable oksExpected to the number of UNRs sent.
 *
 * If we are alone:
 *     Sends UNR to SS.
 *
 * Else
 *     If we are not the DNS:
 *         Sends UNR to DNS, and then to every other member.
 *     If we are the DNS:
 *         Sends UNR to every member in no particular order.
 *
 */
void leave()
{
    int ret;
    char buffer[128];
    sprintf(buffer, "UNR %s\n", myName);

    // Can't leave if haven't joined before
    if (joinStatus != Joined)
    {
        printf("Not joined yet!\n");
        return;
    }

    // Reset the global variable 'OKs expected', which will be decremented on server.c
    oksExpected = 0;

    if (hasOneElement(contacts))
    {
        // Debug and logging
        logm(1, "%s", buffer);

        // We are the last user with this surname. Contact Surname Server.
        ret = sendto(dnsSocket, buffer, strlen(buffer), 0, (struct sockaddr*) &saAddr, sizeof(saAddr));
        if (ret == -1)
        {
            perror("Could not send UNR to SS");
            return;
        }

        oksExpected++;

        joinStatus = LeavingDNS;
    }
    else
    {
        // We must contact the DNS, and then every single family member

        struct sockaddr_in sendAddr;
        memset((void*)&sendAddr, (int)'\0', sizeof(sendAddr));

        // Confirm who is the DNS right now
        getNameServer();

        sendAddr.sin_family = AF_INET;
        sendAddr.sin_addr = nameServer->ip;
        sendAddr.sin_port = htons(nameServer->dnsPort);

        // Debug and logging
        logm(1, "%s\n", buffer);

        // If we are not the DNS, unregister ourselves from the DNS
        if (!isServer())
        {
            setDnsAddr(nameServer);

            ret = sendto(dnsSocket, buffer, strlen(buffer), 0, (struct sockaddr*) &sendAddr, sizeof(sendAddr));
            if (ret == -1)
            {
                perror("Could not send UNR to SS");
                return;
            }

            nameServer->okExpected = 1;
            oksExpected++;
        }

        joinStatus = LeavingUsers;

        Node* p = contacts->next;
        int i;
        Contact* contact;

        for (i = 0; p != NULL; i++, p = p->next)
        {
            contact = p->c;

            // Don't send UNR to ourselves or the DNS
            if (strcmp(contact->name, myName) == 0 || strcmp(contact->name, nameServer->name) == 0)
                continue;

            sendAddr.sin_family = AF_INET;
            sendAddr.sin_addr = contact->ip;
            sendAddr.sin_port = htons(contact->dnsPort);

            setDnsAddr(contact);

            // Debug and logging
            logm(1, "%s", buffer);

            ret = sendto(dnsSocket, buffer, strlen(buffer), 0, (struct sockaddr*) &sendAddr, sizeof(sendAddr));
            if (ret == -1)
            {
                printf("Could not send UNR to contact %s (%d):", contact->name, i); perror("");
                continue;
            }

            logm(1, "Sent UNR to %s (%d).\n", contact->name, i);

            // Only expect OKs in the same number as UNRs sent.
            contact->okExpected = 1;
            oksExpected++;
        }
    }
}

/** \brief Checks if this user is the authorized DNS for his/her surname.
 *
 * Immediately after a 'leave', the DNS is unknown except for the authorized DNS itself.
 *
 * \return int True (not 0) if this user is the authorized DNS.
 *
 */
int isServer()
{
    return (nameServer != NULL && strcmp(nameServer->name, myName) == 0);
}

/** \brief Prints a list of commands to the user.
 */
void help()
{
    // trailing \ escapes the source code's newline
    printf("\
Command list:\n\
              join                    register at the Surname Server\n\
              leave                   unregister from the Surname Server\n\
              find name.surname       find a user's IP and port\n\
              connect name.surname    initiate a call\n\
              disconnect              terminate a call\n\
              message string          send message through call\n\
              exit                    leave if necessary, and exit\n\
              help                    show this message\n\
              \n\
              m string                same as message  \n\
              verbose level           0=normal, 1=more info\n\
              list                    print local database of contacts\n\
              rickroll                try it during a call... :)\n");
}

/** \brief Prints the global state variables to the screen. For debug purposes.
 */
void printState()
{
    printf("Join status: ");
    switch (joinStatus)
    {
        case NotJoined: printf("NotJoined"); break;
        case WaitForDNS: printf("WaitForDNS"); break;
        case WaitForLST: printf("WaitForLST"); break;
        case WaitForOK: printf("WaitForOK"); break;
        case Joined: printf("Joined"); break;
        case LeavingDNS: printf("LeavingDNS"); break;
        case LeavingUsers: printf("LeavingUsers"); break;
        case SearchingNewDns: printf("SearchingNewDns"); break;
        case LeavingForGood: printf("LeavingForGood"); break;
    }
    printf("\n");

    printf("Find status: ");
    switch (findStatus)
    {
        case NotFinding: printf("NotFinding"); break;
        case WaitForFW: printf("WaitForFW"); break;
        case WaitForRPL: printf("WaitForRPL"); break;
    }
    printf("\n");

    printf("OKs expected (may be invalid): %d\n", oksExpected);

    printf("Connect status: %s\n", (talkSocket == -1 ? "Disconnected" : "Connected"));
}

/** \brief Rickrolls the chat peer.
 *
 * Automatically sends the chorus of Rick Astley's 'Never Gonna Give You Up'
 * to the contact in the current chat call as a practical joke. :)
 *
 * Complete with sing-along timer!
 *
 */
void rickroll()
{
    if (talkSocket == -1)
    {
        printf("Start a call first!\n");
        return;
    }

    char* line1 = "Never gonna give you up\n";
    char* line2 = "Never gonna let you down\n";
    char* line3 = "Never gonna run around and ";
    char* line4 = "desert you\n";

    char* line5 = "Never gonna make you cry\n";
    char* line6 = "Never gonna say goodbye\n";
    char* line7 = "Never gonna tell a lie ";
    char* line8 = "and hurt you\n";

    printf("Sing along for maximum fun!!\n");
    sleep(1);

    printf("%s", line1); sendMessage(line1); sleep(2);
    printf("%s", line2); sendMessage(line2); sleep(2);
    printf("%s", line3); sendMessage(line3); sleep(2);
    printf("%s", line4); sendRawMessage(line4); sleep(2);

    printf("%s", line5); sendMessage(line5); sleep(2);
    printf("%s", line6); sendMessage(line6); sleep(2);
    printf("%s", line7); sendMessage(line7); sleep(2);
    printf("%s", line8); sendRawMessage(line8);

}
