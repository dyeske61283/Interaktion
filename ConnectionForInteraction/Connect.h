#pragma once
#include <winsock2.h>
#include <winsock.h>
#include <ws2tcpip.h> 
#include <iostream>
#include <windows.h>


#define SCK_VERSION1            0x0101
#define SCK_VERSION2            0x0202


#define DEFAULT_BUFFER 512


class Connect
{

private:
	SOCKET s;
	WSADATA wsadata;
	int iResult;
	char recvBuffer[DEFAULT_BUFFER];
	char sendBuffer[DEFAULT_BUFFER];

public:
	bool connectToHost(int PortNo, char* IPAddress)
	{
		iResult = WSAStartup(SCK_VERSION2, &wsadata);

		if (iResult)
			return false;

		if (wsadata.wVersion != 0x0202)
		{
			WSACleanup(); //Clean up Winsock
			return false;
		}
		//Fill out the information needed to initialize a socket…
		SOCKADDR_IN target; //Socket address information

		target.sin_family = AF_INET; // address family Internet
		target.sin_port = htons(PortNo); //Port to connect on
										 //target.sin_addr.s_addr = inet_addr(IPAddress); //Target IP //this was not allowed anymore! used InetPton instead.
		InetPton(AF_INET, IPAddress, &target.sin_addr.s_addr);
		s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); //Create socket
		if (s == INVALID_SOCKET)
		{
			return false; //Couldn't create the socket
		}

		//Try connecting...

		if (connect(s, (SOCKADDR *)&target, sizeof(target)) == SOCKET_ERROR)
		{
			return false; //Couldn't connect
		}
		else
			return true; //Success
	}
	//CLOSECONNECTION – shuts down the socket and closes any connection on it
	void CloseConnection()
	{
		//Close the socket if it exists
		if (s)
			closesocket(s);

		WSACleanup(); //Clean up Winsock
	}

	void recvData()
	{
		//Clear the buffer
		memset(recvBuffer, 0, sizeof(recvBuffer));

		//Put the incoming text into our buffer
		recv(s, recvBuffer, sizeof(recvBuffer) - 1, 0);
	}

	void sendData(char *buffer)
	{
		send(s, sendBuffer, DEFAULT_BUFFER, 0);
	}

};