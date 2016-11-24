#include <stdlib.h>
#include <stdio.h>
#include "Connect.h"

#define DEFAULT_PORT 44444
#define IP_ADRESS "192.168.2.1"


int main(int argc, char **argv)
{
	int port = DEFAULT_PORT;
	char *ip_adress = IP_ADRESS;
	Connect c(port, ip_adress);

	c.connectToHost();
	c.CloseConnection();
	return 0;

}