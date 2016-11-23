#include <stdlib.h>
#include <stdio.h>
#include "Connect.h"

#define DEFAULT_PORT 27015
#define IP_ADRESS "192.168.0.1"


int main(int argc, char **argv)
{

	Connect c;
	int port = DEFAULT_PORT;
	char *ip_adress = IP_ADRESS;
	c.connectToHost(port, ip_adress);
	return 0;
}