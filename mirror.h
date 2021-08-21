#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include <unistd.h>
#define MAXRCVLEN 1000
#define PORTNUM 21


int init(const char* serwer, char* dir)
{


	int  mysocket;
	struct sockaddr_in dest;
	struct hostent* hostinfo;
	char serv[256]; int i = 0, idx = 0, b = 0;

	while (serwer[i] != '/') {
		serv[i] = serwer[i];
		i++;
	}
	serv[i] = '\0';
	memset(dir, '\0', sizeof(dir));
	for (idx = i; idx < strlen(serwer); idx++)
		dir[b++] = serwer[idx];
	dir[b] = '\0';

	//printf("dir %s\n",dir);
	hostinfo = gethostbyname(serv);

	if (hostinfo == 0)
		return -1;

	mysocket = socket(AF_INET, SOCK_STREAM, 0);

	memset(&dest, 0, sizeof(dest));
	dest.sin_family = AF_INET;
	dest.sin_addr = *(struct in_addr*)hostinfo->h_addr;
	dest.sin_port = htons(PORTNUM);

	if (connect(mysocket, (struct sockaddr*)&dest, sizeof(struct sockaddr)) < 0)
		return -1;
	return mysocket;

}
bool lista(int g)
{

	char cd[600] = "LIST\r\n";
	char buffer[MAXRCVLEN + 1];
	int len;
	send(g, cd, strlen(cd), 0);
	//printf("%s\n",komenda);
	len = recv(g, buffer, MAXRCVLEN, 0);
	strcat(buffer, "\r\n");
	buffer[len] = '\0';
	printf("lista %s\n", buffer);
	if (strncmp(buffer, "550", 3) == 0)
		return false;
	return true;
}
bool cd(int g, char d[512])
{
	char cd[600] = "CWD ";
	char buffer[MAXRCVLEN + 1];
	int len;
	strcat(cd, d);
	strcat(cd, "\r\n");
	send(g, cd, strlen(cd), 0);
	//printf("%s\n",komenda);
	len = recv(g, buffer, MAXRCVLEN, 0);
	strcat(buffer, "\r\n");
	buffer[len] = '\0';
	//printf("cwd %s\n",buffer);
	if (strncmp(buffer, "550", 3) == 0)
		return false;
	return true;
}
int initpasive(const char* ip, const int port)
{

	int  mysocket;
	struct sockaddr_in dest;

	mysocket = socket(AF_INET, SOCK_STREAM, 0);

	memset(&dest, 0, sizeof(dest));
	dest.sin_family = AF_INET;
	dest.sin_addr.s_addr = inet_addr(ip);
	dest.sin_port = htons(port);

	if (connect(mysocket, (struct sockaddr*)&dest, sizeof(struct sockaddr)) < 0)
		return -1;
	return mysocket;

}
bool extract_ip_port(const char* s, char* ip, int* port)
{

	int i = 0, b = 0, iip = 0; char lport[10], hport[10];
	while (s[i++] != '(');
	memset(ip, 0, sizeof(ip));
	memset(lport, 0, sizeof(lport));
	memset(hport, 0, sizeof(hport));
	i--;
	/* odczytuję numer ip */
	while ((b) != 4)
		if (s[++i] == ',') { b++; ip[iip++] = '.'; }
		else
			ip[iip++] = s[i];

	ip[iip - 1] = '\0';
	b = 0;
	/*odczytuję port */
	while (s[++i] != ',')
		lport[b++] = s[i];
	b = 0;
	while (s[++i] != ')')
		hport[b++] = s[i];

	*port = atoi(lport) * 256 + atoi(hport);
	/*printf("ip %s lport %s hport %s portnum %d\n", ip, lport, hport,atoi(lport)*256+atoi(hport));*/
	return true;
}
bool logowanie(int socket, char* hip, int* hport)
{
	char buffer[MAXRCVLEN + 1];
	int len;
	char komenda[] = "USER anonymous\r\n";
	char pass[] = "PASS projekt@zut.pl\r\n";
	char pasv[] = "PASV\r\n";

	send(socket, komenda, strlen(komenda), 0);
	//printf("%s\n",komenda);
	len = recv(socket, buffer, MAXRCVLEN, 0);
	strcat(buffer, "\r\n");
	buffer[len] = '\0';
	//printf("login %s\n",buffer);
	if (strncmp(buffer, "220", 3) != 0)
		return false;

	strcpy(komenda, pass);
	send(socket, komenda, strlen(komenda), 0);
	//printf("%s\n",komenda);

	memset(buffer, 0, sizeof(buffer));
	len = recv(socket, buffer, MAXRCVLEN, 0);
	strcat(buffer, "\r\n");
	buffer[len] = '\0';
	// printf("haslo %s\n",buffer);
	if (strncmp(buffer, "230", 3) != 0)
		return false;
	send(socket, pasv, strlen(pasv), 0);
	memset(buffer, 0, sizeof(buffer));
	len = recv(socket, buffer, MAXRCVLEN, 0);
	strcat(buffer, "\r\n");
	buffer[len] = '\0';
	/*printf("pasv %s\n",buffer);*/
	if (strncmp(buffer, "227", 3) != 0)
		return false;

	return extract_ip_port(buffer, hip, hport);
}
void wyloguj(int socket)
{
	char komenda[] = "QUIT\r\n";
	int len;
	char buffer[MAXRCVLEN + 1];
	send(socket, komenda, strlen(komenda), 0);
	len = recv(socket, buffer, MAXRCVLEN, 0);

	if (strncmp(buffer, "221", 3) != 0)
		printf("Wystąpiły problemy z poprawnym wylogowaniem.\n"); else
		printf("Wylogowano poprawnie.\n");
}

void finito(int socket)
{
	close(socket);
}

