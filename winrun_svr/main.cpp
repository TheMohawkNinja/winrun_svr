#include <iostream>
#include <WS2tcpip.h>
#include <string>
#include <time.h> 

#pragma comment (lib,"ws2_32.lib")

using namespace std;

const int bufsize = 4096;
string id;

void getCmdOut(string cmd, int s, string b)
{
	int line = 0;
	string data;
	FILE* stream;
	char buffer[bufsize];
	char recbuffer[bufsize];
	cmd.append(" 2>&1");
	stream = _popen(cmd.c_str(), "r");

	//Send id
	send(s, id.c_str(), id.length() + 1, 0);

	if (stream)
	{
		while (!feof(stream))
		{
			//cout<<fgets(buffer,max_buffer,stream);
			if (fgets(buffer, bufsize, stream) != NULL)
			{
				data.append(buffer);

				if (data.find("\n") != std::string::npos)
				{
					data = std::to_string(line) + "-" + data;
					send(s, data.c_str(), data.length() + 1, 0);
					cout << "\t" + data;
					data = "";
					line++;

					ZeroMemory(recbuffer, bufsize);
					recv(s, recbuffer, bufsize, 0);
				}
			}
		}

		//Send break code to client to signal completion of command execution
		send(s, b.c_str(), (b.length() + 1), 0);

		_pclose(stream);
	}
}
int main()
{
	//Initialize winsock
	WSADATA wsData;
	WORD ver = MAKEWORD(2, 2);
	int wsok = WSAStartup(ver, &wsData);
	string output, breakCode, command;

	if (wsok != 0)
	{
		cerr << "Unable to initialize Winsock! Quitting..." << endl;
		return -1;
	}
	else
	{
		cout << "Inititalized Winsock" << endl;
	}
	
	//Create other needed vars
	SOCKET listening;
	sockaddr_in hint;
	sockaddr_in client;
	int clientSize = sizeof(client);
	int bytesReceived = 0;
	SOCKET clientSocket;
	char host[NI_MAXHOST];		//Client name
	char service[NI_MAXSERV];	//Service the client is connected on
	char buf[bufsize];

	while (bytesReceived == 0)
	{
		//Initialize listening socket
		listening = socket(AF_INET, SOCK_STREAM, 0);
		if (listening == INVALID_SOCKET)
		{
			cerr << "Unable to create socket! Quitting..." << endl;
			return -2;
		}
		else
		{
			cout << "Created socket " + to_string(listening) << endl;
		}

		hint.sin_family = AF_INET;
		hint.sin_port = htons(55000);
		hint.sin_addr.S_un.S_addr = INADDR_ANY;

		bind(listening, (sockaddr*)&hint, sizeof(hint));

		//Tell winsock that socket is for listening
		cout << "Waiting for client to connect..." << endl;
		listen(listening, SOMAXCONN);

		clientSocket = accept(listening, (sockaddr*)&client, &clientSize);

		if (clientSocket == INVALID_SOCKET)
		{
			cerr << "Invalid socket! Quitting..." << endl;
			return -3;
		}

		ZeroMemory(host, NI_MAXHOST);
		ZeroMemory(service, NI_MAXSERV);

		//Attempt to resolve client machine name, otherwise resort to IP
		if (getnameinfo((sockaddr*)&client, sizeof(client), host, NI_MAXHOST, service, NI_MAXSERV, 0) == 0)
		{
			cout << endl;
			cout << host << " connected on port " << service << endl;
		}
		else
		{
			inet_ntop(AF_INET, &client.sin_addr, host, NI_MAXHOST);
			cout << endl;
			cout << host << " connected on port " << ntohs(client.sin_port) << endl;
		}

		//Close listening socket
		closesocket(listening);

		//Generate 64 digit code to signal the completion of the command execution and inform client.
		srand(time(NULL));
		for (int i = 0; i < 64; i++)
		{
			breakCode += to_string(rand() % 10);
		}
		cout << "breakCode = " + breakCode << endl;
		send(clientSocket, breakCode.c_str(), (breakCode.length() + 1), 0);

		//Main loop for running commands
		while (true)
		{
			//Get data and run command
			ZeroMemory(buf, bufsize);

			//Wait for client to send data
			bytesReceived = recv(clientSocket, buf, bufsize, 0);

			if (bytesReceived == SOCKET_ERROR)
			{
				cout << "Socket Error recieved, did winrund stop?" << endl;
			}
			else if (bytesReceived == 0)
			{
				cout << "Client disconected." << endl;
				break;
			}
			else
			{
				//Run command and echo output back to client
				id = string(buf, 0, bytesReceived).substr(0, string(buf, 0, bytesReceived).find("\""));
				cout << "Recieved command for ID: " + id << endl;

				command = string(buf, 0, bytesReceived).substr(id.length(), (string(buf, 0, bytesReceived).length() - id.length())).c_str();
				cout << "Running command " + command << endl;
				getCmdOut(command, clientSocket, breakCode);

				cout << "Command completed!" << endl;
			}
		}
	}

	//Close socket
	cout << "Closing socket..." << endl;
	closesocket(clientSocket);

	//Cleanup winsock
	WSACleanup();

	return 0;
}