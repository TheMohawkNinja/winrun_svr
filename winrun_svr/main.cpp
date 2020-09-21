#include <iostream>
#include <WS2tcpip.h>
#include <string>
#include <time.h>
#include <thread>

#pragma comment (lib,"ws2_32.lib")

using namespace std;

const int bufsize = 4096;

void getCmdOut(string clientID, string cmd, int s, string b)
{
	int line = 0;
	int recvBytes, rv;
	string data;
	string recvStr;
	FILE* stream;
	char buffer[bufsize];
	char recvBuffer[bufsize];
	fd_set readfds, masterfds;
	timeval timeout;
	timeout.tv_sec = 5;
	timeout.tv_usec = 0;

	cmd.append(" 2>&1");
	stream = _popen(cmd.c_str(), "r");

	//Send id
	send(s, clientID.c_str(), clientID.length() + 1, 0);

	if (stream)
	{
		while (!feof(stream))
		{
			if (fgets(buffer, bufsize, stream) != NULL)
			{
				data.append(buffer);

				if (data.find("\n") != std::string::npos)
				{
					data = clientID + "-" + std::to_string(line) + "-" + data;
					send(s, data.c_str(), data.length() + 1, 0);
					cout << "\t" + data;
					data = "";
					line++;

					ZeroMemory(recvBuffer, bufsize);

					//Wait for signal to come in from client to continue working.
					FD_ZERO(&masterfds);
					FD_SET(s, &masterfds);
					memcpy(&readfds, &masterfds, sizeof(fd_set));
					rv = select(s + 1, &readfds, NULL, NULL, &timeout);

					if (rv == SOCKET_ERROR)
					{
						cout << "Socket error during select() on PID \"" << clientID << "\"" << endl;
						break;
					}
					else if (rv == 0)
					{
						cout << "Timeout ("<< timeout.tv_sec << " sec, " << timeout.tv_usec << " usec) while waiting for continue signal for PID \"" << clientID << "\"" << endl;
						break;
					}
					else
					{
						recvBytes = recv(s, recvBuffer, bufsize, 0);
						recvStr = std::string(recvBuffer, 0, recvBytes);
					}
				}
			}
		}

		//Send break code to client to signal completion of command execution
		send(s, (clientID + "-" + b).c_str(), (clientID.length()+string("-").length()+b.length() + 1), 0);
		cout << "Command completed!" << endl;

		_pclose(stream);
	}
}
int winrun_svr_child(int port)
{
	//Initialize winsock
	WSADATA wsData;
	WORD ver = MAKEWORD(2, 2);
	int wsok = WSAStartup(ver, &wsData);
	string output, breakCode, command, pid;

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
		hint.sin_port = htons(port);
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
		cout << "breakCode: " + breakCode << endl;
		send(clientSocket, breakCode.c_str(), breakCode.length(), 0);

		//Main loop for running commands
		while (true)
		{
			//Wait for client to send data
			ZeroMemory(buf, bufsize);
			bytesReceived = recv(clientSocket, buf, bufsize, 0);

			if (bytesReceived == SOCKET_ERROR)
			{
				cout << "Socket Error recieved, did winrund stop?" << endl;
				break;
			}
			else if (bytesReceived == 0)
			{
				cout << "Client disconected." << endl;
				break;
			}
			else if (string(buf, 0, bytesReceived) == "ready")
			{
				cout << "Returning ready signal" << endl;
				send(clientSocket, string(buf, 0, bytesReceived).c_str(), string(buf, 0, bytesReceived).length() + 1, 0);
			}
			else
			{
				//Run command and echo output back to daemon
				cout << "Recieved: " << string(buf, 0, bytesReceived) << endl;
				if (string(buf, 0, bytesReceived).substr(0, breakCode.length()) == breakCode)
				{
					pid = string(buf, 0, bytesReceived).substr(breakCode.length(), (string(buf, 0, bytesReceived).find("\"") - breakCode.length()));
					cout << "Recieved command for PID: " + pid << endl;

					command = string(buf, 0, bytesReceived).substr(breakCode.length() + pid.length(), (string(buf, 0, bytesReceived).length() - (breakCode.length() + pid.length()))).c_str();
					cout << "Running command " + command << endl;

					getCmdOut(pid, command, clientSocket, breakCode);
				}
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
int main()
{
	//Create process threads
	for (int i = 0; i < 8; i++)
	{
		cout << "Spawning child thread on port: " << (55000 + i) << endl;
		std::thread winrun_svr_child_thread(winrun_svr_child,55000 + i);
		winrun_svr_child_thread.detach();
	}

	//Keep program from exiting
	while (true)
	{
		Sleep(1000);
	}

	return 0;
}