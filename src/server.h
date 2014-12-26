#ifndef SERVER_H_INCLUDED
#define SERVER_H_INCLUDED

int prepareTalkServer();

void parseServerCommand();

void replyToQuery(char* argument, struct sockaddr_in* addr, socklen_t addrLen);
void acceptCall();
void registerNewUser(char* newUserREG, struct sockaddr_in* addr, socklen_t addrLen);
void unregisterUser(char* userUNR, struct sockaddr_in* addr, socklen_t addrLen);
void receiveList();

void continueJoin(char* buffer);
void continueJoinOK(struct sockaddr_in* addr, socklen_t addrLen);

void continueLeave(char* buffer, struct sockaddr_in* addr, socklen_t addrLen);

void continueFindFW(char* buffer);
void continueFindRPL(char* buffer);
void startChatCall(char* name, struct sockaddr_in peerAddr);

void receiveMessage(char* buffer);

void becomeDNS(char* buffer, struct sockaddr_in* addr, socklen_t addrLen);

int getContactFromMsg(char* message, Contact* out_contact);

#endif // SERVER_H_INCLUDED
