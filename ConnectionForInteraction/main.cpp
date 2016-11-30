#include <stdlib.h>
#include <stdio.h>
#include "control.h"

#pragma comment(lib, "ws2_32.lib")
#pragma warning (disable:4996)


int main(int argc, char **argv)
{
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