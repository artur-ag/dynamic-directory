Projecto de Redes de Computadores e Internet 2014 - Dynamic Directory

A chat application with distributed username database.
Each username has the form "name.surname".
Each surname represents a "family", and there is one family member authorized to be the Name Server of that family: it replies to user find requests.
There is one global Surname Server, who knows about each Name Server of each family.
Chats are simple TCP connections. Username find requests and other communications are UDP-based.

Artur Gonçalves  69271
