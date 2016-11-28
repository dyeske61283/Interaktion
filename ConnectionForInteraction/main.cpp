#include <stdlib.h>
#include <stdio.h>
#include "Connect.h"
#include "control.h"
#define DEFAULT_PORT 44444
#define IP_ADRESS "192.168.2.1"
#pragma comment(lib, "ws2_32.lib")
#pragma warning (disable:4996)


int main(int argc, char **argv)
{
	int port = DEFAULT_PORT;
	char *ip_adress = IP_ADRESS;
	//Connect c(port, ip_adress);

	//c.connectToHost();

	//c.closeConnection();

	sumo::Control k;
	if (!k.open())
	{
		getchar();
		return EXIT_FAILURE;
	}
	k.move(-20, 10);
	k.close();
	


	return 0;

}