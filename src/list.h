#ifndef LIST_H_INCLUDED
#define LIST_H_INCLUDED

#include "contact.h"

/** \brief A single node of a list of contacts.
 */
typedef struct Node
{
    struct Node* next;
    Contact* c;
} Node;

Node* newList();
void add(Node* list, Contact* c);
int removeFrom(Node* list, char* name);

Contact* get(Node* list, char* name);
Contact* getByAddr(Node* list, struct sockaddr_in* addr, socklen_t addrlen);

void setDnsAddr(Contact* c);

void emptyList(Node* list);
void printList(Node* list);

int hasOneElement(Node* list);

#endif
