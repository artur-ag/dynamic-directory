#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "debug.h"
#include "globals.h"
#include "contact.h"
#include "list.h"

/** \brief Creates a new, empty list.
 * Empty lists are a Node that points to NULL.
 * The "contact" of the first Node is always NULL.
 * \return Node* Header node of an empty list.
 *
 */
Node* newList()
{
    Node* new = malloc(sizeof(Node));
    new->next = NULL;
    new->c = NULL;

    return new;
}

/** \brief Adds an already-existing dynamically allocated contact to a list.
 *
 * The contact is added in the beginning of the list.
 *
 * \param list Node* Header of a list
 * \param c Contact* Contact to be added
 */
void add(Node* list, Contact* c)
{
    Node* newnode = malloc(sizeof(Node));
    newnode->c = c;

    // By default, no OKs are expected
    c->okExpected = 0;

    // Point new node to the previously-first node
    newnode->next = list->next;

    // Hook up list header to the new node
    list->next = newnode;
    return;
}

/** \brief Removes a contact from the list.
 *
 * Removes a certain contact from the list. The contact is found by its name.surname.
 * This function frees the node and the contact, and assumes contact was allocated dynamically.
 *
 * \param list Header of a list
 * \param name Name of a contact, in the format name.surname
 *
 * \return int 0 if the removal was successful. -1 if no contact with the provided name exist in the list.
 */
int removeFrom(Node* list, char* name)
{
    Node* p = list->next;
    Node* prevP = list;

    // Find desired contact node, and also node before it
    while (p != NULL && strcmp(p->c->name, name) != 0)
    {
        prevP = p;
        p = p->next;
    }

    if (p != NULL)
    {
        logm(1, "Removed node with contact %s.\n", p->c->name);

        // Name found, remove it
        prevP->next = p->next;
        free(p->c);
        free(p);
        return 0;
    }

    // Contact did not exist in list
    return -1;
}

/** \brief Finds a contact in the list by its name.
 *
 * Returns a pointer to the contact itself, or NULL if the contact does not exist.
 *
 * \param list Node* List to be searched.
 * \param name char* Name of the contact to be found.
 * \return Contact* Pointer to the found contact, or NULL if it was not found.
 *
 */
Contact* get(Node* list, char* name)
{
    Node* p = list->next;

    while (p != NULL && strcmp(p->c->name, name) != 0)
        p = p->next;

    if (p != NULL)
        return p->c;
    else
        return NULL;
}

/** \brief Finds a contact in the list by its name.
 *
 * Returns a pointer to the contact itself, or NULL if the contact does not exist.
 *
 * \param list Node* List to be searched.
 * \param addr struct sockaddr_in* Address to be found. Will be compared byte by byte.
 * \param addrlen socklen_t Length of addr.
 * \return Contact* Pointer to the found contact, or NULL if it was not found.
 *
 */
Contact* getByAddr(Node* list, struct sockaddr_in* addr, socklen_t addrlen)
{
    Node* p = list->next;

    while (p != NULL && memcmp(addr, &(p->c->dnsAddr), addrlen))
        p = p->next;

    if (p != NULL)
        return p->c;
    else
        return NULL;
}

/** \brief Empties list and frees its contents (contacts).
 *
 * Assumes contacts were allocated dynamically.
 * Does NOT free the list itself (ie. the list header).
 *
 * \param list Node* Header of the list
 *
 */
void emptyList(Node* list)
{
    Node* p = list -> next;
    Node* nextp = p;

    while (nextp != NULL)
    {
        p = nextp;
        nextp = p->next;

        // Free contact, and then node
        free(p->c);
        free(p);
    }

    // Leave list marked as empty
    list->next = NULL;
}

/** \brief Prints the contents of the list to STDOUT in a table format. Debug function.
 *
 * \param list Node* List to be printed.
 *
 */
void printList(Node* list)
{
    printf("%2s   %22s  %15s  %8s  %9s\n", "No", "Name", "IP Address", "DNS Port", "Talk Port");

    int i;
    for(i = 1; list->next != NULL; i++)
    {
        list = list->next;
        printf("%2d:  %22s  %15s  %8d  %9d", i, list->c->name, inet_ntoa(list->c->ip), list->c->dnsPort, list->c->talkPort);

        if (strcmp(list->c->name, myName) == 0)
            printf(" Myself");

        if (nameServer != NULL && strcmp(list->c->name, nameServer->name) == 0)
            printf(" DNS");

        printf("\n");
    }
}

/** \brief Updates the socket dnsAddr field of a contact.
 *
 * Uses the other contact fields: ip and dnsPort.
 *
 * \param c Contact* Contact to be updated.
 *
 */
void setDnsAddr(Contact* c)
{
    memset((void*) &(c->dnsAddr), (int)'\0', sizeof(c->dnsAddr));

    c->dnsAddr.sin_family = AF_INET;
    c->dnsAddr.sin_addr = c->ip;
    c->dnsAddr.sin_port = htons(c->dnsPort);
}

/** \brief Checks if a list has one and only one element.
 *
 * \param list Node* List to test
 * \return int 1 if the list has one and only one element. 0 otherwise.
 *
 */
int hasOneElement(Node* list)
{
    return list->next != NULL && list->next->next == NULL;
}
