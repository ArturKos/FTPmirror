#include "mirror.h"
#include <stdlib.h>
#include <ncurses.h>
#include <stdio.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>


int main(int argc, char* argv[])
{
	int   gniazdo;
	int   gniazdodanych;
	int   portdanych;
	char  ip_danych[17];
	char  dir[512];
	if ((argc != 4) && (argc != 3))
	{
		printf("%s \n", "Program wymaga podania 2 argumentow, 3 argument -n jest opcjonalny.");
		printf("%s \n", "mirror user@ftp.server/dir/dir localdir -n");
		return 1;
	}
	printf("Program tworzy mirror ze zdalnego katalogu /dir/dir do localnego katalogu localdir");
	printf(" ostatni parametr -n jest niobowiązkowy i oznacza zagniezdzenie sciaganych katalogow.\n");


	gniazdo = init(argv[1], dir); // próba połączenia z wybranym serwerem

	if (gniazdo < 0)
	{ //nie udało się połączyć z serwerem
		printf("%s %s\n", "Nie mogę się połączyć z wybranym serwerem:", argv[1]);
		return 1;
	}
	printf("%s %s oraz przechodzę do trybu pasywnego\n", "Logowanie do serwera:", argv[1]);
	if (logowanie(gniazdo, ip_danych, &portdanych)) //próba logowania do serwera
	{
		printf("%s\n", "Zalogowano.");
		gniazdodanych = initpasive(ip_danych, portdanych);

		if (cd(gniazdo, dir) == true)
		{
			printf("Przechodze do katalogu: %s\n", dir);
			mkdir(argv[2], 0777);
			lista(gniazdo);
		}
		else printf("%s %s.\n", "Wsytapily problemy z przejsciem do katalogu: ", dir);
		finito(gniazdodanych);
	}
	else
	{ // nie udało się zalogować
		printf("%s\n", "Logowanie zakonczylo się niepowodzeniem. Sprawdz login i haslo.\n");
		return 1;
	}
	wyloguj(gniazdo);// wylogowanie z serwera
	finito(gniazdo); // zamknięcie gniazda

	return 0;
}