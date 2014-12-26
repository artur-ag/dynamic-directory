#ifndef CONTACT_H_INCLUDED
#define CONTACT_H_INCLUDED

#include <netinet/in.h>

#define NAME_LEN 128

/** \brief Information about a Contact. Contains its name, IP and ports.
 */
typedef struct Contact
{
    /** Full name of the contact, in the format 'name.surname'. */
	char name[NAME_LEN];

    /** IP address of the contact, in network endianess. */
	struct in_addr ip;

    /** TCP talk server port of the contact, in host endianess. */
	int talkPort;

    /** UDP DNS server port of the contact, in host endianess. */
	int dnsPort;

    /** Socket address of the DNS server of the contact. */
	struct sockaddr_in dnsAddr;

    /** Boolean. 1 if we are expecting to receive an OK from this contact. 0 otherwise. */
	int okExpected;
} Contact;

#endif
