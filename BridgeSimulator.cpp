#include <iostream>
#include <string>
#include <vector>
#include <sstream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>  // Required for `inet_pton`
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#define closesocket close
#endif

const int SERVER_PORT = 28000;
const int HEADER_SIZE = 40;  // RTCB (4 bytes) + MessageType (32 bytes) + MessageLength (4 bytes)

void SendFormattedMessage(SOCKET clientSocket)
{
    while (true)
    {
        std::string messageType;
        std::string jsonMessage;
        std::string line;
        std::stringstream jsonBuffer;

        // Get message type
        std::cout << "Enter message type (max 32 chars, or 'exit' to quit): ";
        std::getline(std::cin, messageType);
        if (messageType == "exit")
            break;

        if (messageType.length() > 32)
        {
            messageType = messageType.substr(0, 32);
        }

        // Get JSON data (multi-line until an empty line is entered)
        std::cout << "Enter JSON message (end with an empty line):\n";
        while (true)
        {
            std::getline(std::cin, line);
            if (line.empty()) break;
            jsonBuffer << line << "\n";
        }

        jsonMessage = jsonBuffer.str();
        if (jsonMessage.empty()) continue;  // Skip empty messages

        // Convert message to UTF-8 if necessary
        std::vector<uint8_t> utf8Json(jsonMessage.begin(), jsonMessage.end());
        int32_t jsonLength = static_cast<int32_t>(utf8Json.size());

        // Construct the final packet
        std::vector<uint8_t> packet(HEADER_SIZE + jsonLength);

        // Add "RTCB" header
        std::memcpy(packet.data(), "RTCB", 4);

        // Add message type (32 bytes, padded with spaces)
        std::memset(packet.data() + 4, ' ', 32);
        std::memcpy(packet.data() + 4, messageType.c_str(), messageType.length());

        // Add JSON length (4 bytes, little-endian)
        std::memcpy(packet.data() + 36, &jsonLength, sizeof(int32_t));

        // Add JSON data
        std::memcpy(packet.data() + HEADER_SIZE, utf8Json.data(), jsonLength);

        // Send the data
        if (send(clientSocket, reinterpret_cast<const char*>(packet.data()), packet.size(), 0) < 0)
        {
            std::cerr << "Failed to send data.\n";
            break;
        }

        std::cout << "Sent Message Type: " << messageType << "\n";
        std::cout << "Sent JSON Data:\n" << jsonMessage << "\n";
    }
}

void StartServer()
{
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "WSAStartup failed.\n";
        return;
    }
#endif

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == INVALID_SOCKET)
    {
        std::cerr << "Failed to create socket.\n";
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(SERVER_PORT);

    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0)
    {
        std::cerr << "Bind failed.\n";
        closesocket(serverSocket);
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

    if (listen(serverSocket, 1) < 0)
    {
        std::cerr << "Listen failed.\n";
        closesocket(serverSocket);
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

    std::cout << "Server is listening on port " << SERVER_PORT << "...\n";

    sockaddr_in clientAddr{};
    int clientLen = sizeof(clientAddr);
    SOCKET clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &clientLen);
    if (clientSocket == INVALID_SOCKET)
    {
        std::cerr << "Accept failed.\n";
        closesocket(serverSocket);
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

    std::cout << "Client connected!\n";
    SendFormattedMessage(clientSocket);

    closesocket(clientSocket);
    closesocket(serverSocket);

#ifdef _WIN32
    WSACleanup();
#endif
}

int main()
{
    StartServer();
    return 0;
}
