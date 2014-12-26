#ifndef COMMANDS_H_INCLUDED
#define COMMANDS_H_INCLUDED

void parseCommand(char* command, int* isRunning);

void join();
void registerAtDns(Contact* gns);
void leave();

void find(char* name, FindMode mode);

void sendMessage(char* message);
void sendRawMessage(char* message);
void disconnect();

void help();
void printState();

int getRegMessage(char* buffer);
int isServer();

void rickroll();


#endif // COMMANDS_H_INCLUDED
