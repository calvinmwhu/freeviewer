#include "stdafx.h"
#include "networking.h"
#include "errorHandler.h"


const int nPort = 5000;
const int MAXDATASIZE = 1024;
// float vAngle = 0.0f;

int listeningForMsg(){
	SOCKET hSocket,hAccept;
	struct sockaddr_in addr;
	int len = sizeof(addr);
	int nRet; char Temp[MAXDATASIZE];
	char ok[] = "OK\n";

	// Initialize winsock
	WSADATA stack_info ;
	WSAStartup(MAKEWORD(2,0), &stack_info) ;

	//Create socket
	hSocket = socket( PF_INET, SOCK_STREAM, 0 );
	if( hSocket == INVALID_SOCKET ){
		printf( "socket() error %d\n", SOCKET_ERRNO );
		getchar();
		exit(1);
	}

	//Listen to the socket
	addr.sin_family = AF_INET ;
	addr.sin_addr.s_addr = htonl (INADDR_ANY);
	addr.sin_port = htons ((unsigned short)nPort );
	if ( bind( hSocket, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR ){
		printf("bind() error\n");
		int c = getchar();
		printf("Got "+c);
		exit(1);
	}

	if ( listen( hSocket, 100) == SOCKET_ERROR ){
		printf("listen() error\n");
		int c = getchar();
		printf("Got2 "+c);
		closesocket(hSocket);
		WSACleanup();
		exit(1);
	}

	printf("Waiting for glass's head orientation data...\n");

	while(true){
		hAccept = accept(hSocket, NULL, NULL);
		if(hAccept == INVALID_SOCKET){
			printf("Accept failed: %d\n", WSAGetLastError());
			closesocket(hAccept);
			continue;
		}
		printf( "Accept incomming connection\n" );
		// Read request
		
		while((nRet = recv( hAccept, Temp, sizeof(Temp)-1, 0 ))>0){
			// std::cout<<"receiving "<<nRet<<" byte"<<std::endl;
			Temp[nRet]='\0';
			// std::string msg(Temp);
			// std::cout<<msg<<std::endl;
			
			float angle = (float)atof(Temp);
			
			viewAngle = angle;
			nRet = send( hAccept, ok, sizeof(ok)-1 , 0 );
			if( nRet == SOCKET_ERROR ){
				printf( "send() error %d\n", SOCKET_ERRNO );
				exit(1);
			}
		}
		if(nRet==0){
			printf("Connection closed!\n");
		}
		if(nRet<0){
			printf("recv failed! %d \n", WSAGetLastError());
			closesocket(hAccept);
			closesocket(hSocket);
			WSACleanup();
			exit(1);
		}

		closesocket( hAccept );
		// }
	}
	closesocket( hSocket );
	return 0;
}