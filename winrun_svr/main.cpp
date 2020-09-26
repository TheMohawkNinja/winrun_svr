#include <iostream>
#include <stdarg.h>
#include <WS2tcpip.h>
#include <string>
#include <time.h>
#include <thread>
#include <mutex>
#include <fstream>

#pragma comment (lib,"ws2_32.lib")

const int bufsize = 4096;
int basePort;
bool* threadIsWorking;
std::string logfile;
std::mutex mtx;

void output(FILE* stream, int _thread, const char* format, ...)
{
	mtx.lock();

	FILE* logWriter;
	fopen_s(&logWriter,logfile.c_str(), "a");

	//Create and output a date/time stamp
	SYSTEMTIME st;
	std::string result;
	GetLocalTime(&st);
	int y = st.wYear;
	int M = st.wMonth;
	int d = st.wDay;
	int h = st.wHour;
	int m = st.wMinute;
	int s = st.wSecond;
	int ms = st.wMilliseconds;
	fprintf(stream, "[%d-%02d-%02d %02d:%02d:%02d.%03d](%d) ", y, M, d, h, m, s, ms, _thread);
	fprintf(logWriter, "[%d-%02d-%02d %02d:%02d:%02d.%03d](%d) ", y, M, d, h, m, s, ms, _thread);

	//Output the actual args
	va_list args;
	va_start(args, format);
	vfprintf(stream, format, args);
	vfprintf(logWriter, format, args);
	va_end(args);

	fclose(logWriter);

	mtx.unlock();
}
void getCmdOut(int t, std::string clientID, std::string cmd, int s, std::string b)
{
	int line = 0;
	int recvBytes, rv;
	std::string data;
	std::string recvStr;
	FILE* stream;
	char buffer[bufsize];
	char recvBuffer[bufsize];
	fd_set readfds, masterfds;
	timeval timeout;
	timeout.tv_sec = 1;
	timeout.tv_usec = 0;

	//If user doesn't specify any output redirection, redirect stderr to stdout.
	if (cmd.find(" 1>") == std::string::npos && cmd.find(" 2>") == std::string::npos)
	{
		cmd.append(" 2>&1");
	}
	
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

					output(stdout, t, "\t%s", data.c_str());

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
						output(stderr, t, "Socket error during select() on PID \"%s\"\n",clientID.c_str());
						break;
					}
					else if (rv == 0)
					{
						output(stderr, t, "Timeout (>%ld.%06ld seconds) while waiting for continue signal for PID %s\n", timeout.tv_sec, timeout.tv_usec, clientID.c_str() );
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
		send(s, (clientID + "-" + b).c_str(), (clientID.length()+ std::string("-").length()+b.length() + 1), 0);
		output(stdout, t, "Command completed!\n");

		_pclose(stream);
	}
}
int winrun_svr_controller(int port)
{
	int threadID = port - basePort;

	//Initialize winsock
	WSADATA wsData;
	WORD ver = MAKEWORD(2, 2);	
	int wsok = WSAStartup(ver, &wsData);
	std::string breakCode, command, pid;

	if (wsok != 0)
	{
		output(stderr, threadID, "Unable to initialize Winsock! Quitting...\n" );
		return -1;
	}
	else
	{
		output(stdout, threadID, "Inititalized Winsock for port %d\n",port);
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
			output(stderr, threadID, "Unable to create socket! Quitting...\n" );
			return -2;
		}
		else
		{
			output(stdout, threadID, "Created socket %d for port %d\n", listening, port);
		}

		hint.sin_family = AF_INET;
		hint.sin_port = htons(port);
		hint.sin_addr.S_un.S_addr = INADDR_ANY;

		bind(listening, (sockaddr*)&hint, sizeof(hint));

		//Tell winsock that socket is for listening
		output(stdout, threadID, "Waiting for client to connect on port %d...\n", port);
		listen(listening, SOMAXCONN);

		clientSocket = accept(listening, (sockaddr*)&client, &clientSize);

		if (clientSocket == INVALID_SOCKET)
		{
			output(stderr, threadID, "Invalid socket! Quitting...\n");
			return -3;
		}

		ZeroMemory(host, NI_MAXHOST);
		ZeroMemory(service, NI_MAXSERV);

		//Attempt to resolve client machine name, otherwise resort to IP
		if (getnameinfo((sockaddr*)&client, sizeof(client), host, NI_MAXHOST, service, NI_MAXSERV, 0) == 0)
		{
			output(stdout, threadID, "%s connected on port %s\n",host,service);
		}
		else
		{
			inet_ntop(AF_INET, &client.sin_addr, host, NI_MAXHOST);

			output(stdout, threadID, "%s connected on port %d\n", host, ntohs(client.sin_port));
		}

		//Close listening socket
		closesocket(listening);

		//Main loop for running commands
		while (true)
		{
			//Wait for client to send data
			ZeroMemory(buf, bufsize);
			bytesReceived = recv(clientSocket, buf, bufsize, 0);

			if (bytesReceived == SOCKET_ERROR)
			{
				output(stderr, threadID, "Socket error recieved on port %d, did winrund stop?\n", port);
				break;
			}
			else if (bytesReceived == 0)
			{
				output(stdout, threadID, "Client disconnected from port %d\n",port);
				break;
			}
			else
			{
				//Run command and echo output back to daemon
				output(stdout, threadID, "Checking state of thread %s\n", std::string(buf, 0, bytesReceived).c_str());
				if (threadIsWorking[(std::stoi(std::string(buf, 0, bytesReceived)))])
				{
					output(stdout, threadID, "Thread %s is busy\n", std::string(buf, 0, bytesReceived).c_str());
					send(clientSocket, std::string("1").c_str(), std::string("1").size(), 0);
				}
				else
				{
					output(stdout, threadID, "Thread %s is idle\n", std::string(buf, 0, bytesReceived).c_str());
					send(clientSocket, std::string("0").c_str(), std::string("0").size(), 0);
				}
			}
		}
	}

	//Close socket
	output(stdout, threadID, "Closing socket %d on port %d\n", clientSocket, port);
	closesocket(clientSocket);

	//Cleanup winsock
	WSACleanup();

	return 0;
}
int winrun_svr_child(int port)
{
	int threadID = port - basePort;

	//Initialize winsock
	WSADATA wsData;
	WORD ver = MAKEWORD(2, 2);
	int wsok = WSAStartup(ver, &wsData);
	std::string breakCode, command, pid;

	if (wsok != 0)
	{
		output(stderr, threadID, "Unable to initialize Winsock! Quitting...\n");
		return -1;
	}
	else
	{
		output(stdout, threadID, "Inititalized Winsock for port %d\n", port);
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
			output(stderr, threadID, "Unable to create socket! Quitting...\n");
			return -2;
		}
		else
		{
			output(stdout, threadID, "Created socket %d for port %d\n", listening, port);
		}

		hint.sin_family = AF_INET;
		hint.sin_port = htons(port);
		hint.sin_addr.S_un.S_addr = INADDR_ANY;

		bind(listening, (sockaddr*)&hint, sizeof(hint));

		//Tell winsock that socket is for listening
		output(stdout, threadID, "Waiting for client to connect on port %d...\n", port);
		listen(listening, SOMAXCONN);

		clientSocket = accept(listening, (sockaddr*)&client, &clientSize);

		if (clientSocket == INVALID_SOCKET)
		{
			output(stderr, threadID, "Invalid socket! Quitting...\n");
			return -3;
		}

		ZeroMemory(host, NI_MAXHOST);
		ZeroMemory(service, NI_MAXSERV);

		//Attempt to resolve client machine name, otherwise resort to IP
		if (getnameinfo((sockaddr*)&client, sizeof(client), host, NI_MAXHOST, service, NI_MAXSERV, 0) == 0)
		{
			output(stdout, threadID, "%s connected on port %s\n", host, service);
		}
		else
		{
			inet_ntop(AF_INET, &client.sin_addr, host, NI_MAXHOST);

			output(stdout, threadID, "%s connected on port %d\n", host, ntohs(client.sin_port));
		}

		//Close listening socket
		closesocket(listening);

		//Generate 64 digit code to signal the completion of the command execution and inform client.
		srand(port);
		for (int i = 0; i < 64; i++)
		{
			breakCode += std::to_string(rand() % 10);
		}
		output(stdout, threadID, "Break code for port %d is %s\n",port, breakCode.c_str());
		send(clientSocket, breakCode.c_str(), breakCode.length(), 0);

		//Main loop for running commands
		while (true)
		{
			//Wait for client to send data
			ZeroMemory(buf, bufsize);
			bytesReceived = recv(clientSocket, buf, bufsize, 0);

			if (bytesReceived == SOCKET_ERROR)
			{
				output(stderr, threadID, "Socket error recieved on port %d, did winrund stop?\n", port);
				break;
			}
			else if (bytesReceived == 0)
			{
				output(stdout, threadID, "Client disconnected from port %d\n", port);
				break;
			}
			else
			{
				//Run command and echo output back to daemon
				if (std::string(buf, 0, bytesReceived).substr(0, breakCode.length()) == breakCode)
				{
					pid = std::string(buf, 0, bytesReceived).substr(breakCode.length(), (std::string(buf, 0, bytesReceived).find("\"") - breakCode.length()));
					output(stdout, threadID, "Recieved command for PID %s on port %d\n",pid.c_str(), port);

					command = std::string(buf, 0, bytesReceived).substr(breakCode.length() + pid.length(), (std::string(buf, 0, bytesReceived).length() - (breakCode.length() + pid.length()))).c_str();
					output(stdout, threadID, "Running command %s on port %d\n",command.c_str(), port);

					threadIsWorking[port - basePort] = true;
					getCmdOut(threadID, pid, command, clientSocket, breakCode);
					threadIsWorking[port - basePort] = false;
				}
			}
		}
	}

	//Close socket
	output(stdout, threadID, "Closing socket %d on port %d\n", clientSocket, port);
	closesocket(clientSocket);

	//Cleanup winsock
	WSACleanup();

	return 0;
}
int main(int argc, char *argv[])
{
	int maxThreads;
	std::string line;
	std::ifstream configReader;

	//Resize window a bit so most output doesn't wrap
	HWND window = GetConsoleWindow();
	MoveWindow(window, 100, 100, 750, 250, TRUE);

	//Get configuration info
	if (argv[2])
	{
		fprintf(stderr, "Unknown arg \"%s\"\n", argv[2]);
		return -1;
	}
	else
	{
		try
		{
			configReader.open(argv[1]);
			while (!configReader.eof())
			{
				getline(configReader, line);

				if (line.substr(0, 1) != "#")//Hashtag denotes comments
				{
					if (line.find("threads") == 0)
					{
						maxThreads = stoi(line.substr(line.find("=")+1, line.length() - line.find("=")));
						fprintf(stdout, "Setting thread count to \"%d\"\n", maxThreads);

						threadIsWorking = new bool[maxThreads];
					}
					else if (line.find("port") == 0)
					{
						basePort = stoi(line.substr(line.find("=")+1, line.length() - line.find("=")));
						fprintf(stdout, "Setting base port to \"%d\"\n", basePort);
					}
					else if (line.find("log") == 0)
					{
						logfile = line.substr(line.find("=")+1, line.length() - line.find("="));
						fprintf(stdout, "Setting log file location to \"%s\"\n", logfile.c_str());
					}
				}
			}
		}
		catch (...)
		{
			fprintf(stderr, "Error while attempting to read config file at \"%s\"\n", argv[1]);
			return -2;
		}
	}

	for (int i = 0; i < sizeof(threadIsWorking); i++)
	{
		threadIsWorking[i] = false;
	}

	//Create process threads
	output(stdout, 0, "Spawning controller thread on port %d\n",basePort);
	std::thread winrun_svr_controller_thread(winrun_svr_controller, basePort);
	winrun_svr_controller_thread.detach();

	for (int i = 1; i <= maxThreads; i++)
	{
		output(stdout, 0, "Spawning child thread on port %d\n", (basePort + i));
		std::thread winrun_svr_child_thread(winrun_svr_child,(basePort + i));
		winrun_svr_child_thread.detach();
	}

	//Check for signal from daemon to report on idle threads
	while (true)
	{
		Sleep(1000);
	}

	return 0;
}