#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstring> // for memset, memcpy

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>  // Required for inet_pton
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#define closesocket close
#endif

// Global flag to control the advertisement loop.
std::atomic<bool> gKeepAdvertising(true);

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
        if (send(clientSocket, reinterpret_cast<const char*>(packet.data()), static_cast<int>(packet.size()), 0) < 0)
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

    std::cout << "Server listening on port " << SERVER_PORT << "...\n";

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

void AdvertiseServer()
{
#ifdef _WIN32
    // On Windows, WSAStartup is required if not already initialized.
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "WSAStartup failed for advertising.\n";
        return;
    }
#endif

    // Create UDP socket for IPv4 advertising.
    SOCKET udpSocket4 = socket(AF_INET, SOCK_DGRAM, 0);
    if (udpSocket4 == INVALID_SOCKET)
    {
        std::cerr << "Failed to create IPv4 UDP socket for advertising.\n";
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

    // Create UDP socket for IPv6 advertising.
    SOCKET udpSocket6 = socket(AF_INET6, SOCK_DGRAM, 0);
    if (udpSocket6 == INVALID_SOCKET)
    {
        std::cerr << "Failed to create IPv6 UDP socket for advertising.\n";
        closesocket(udpSocket4);
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

    // Set multicast TTL/hop limit for both sockets.
    int ttl = 1;
    if (setsockopt(udpSocket4, IPPROTO_IP, IP_MULTICAST_TTL, (char*)&ttl, sizeof(ttl)) < 0)
    {
        std::cerr << "Failed to set multicast TTL for IPv4.\n";
    }
    if (setsockopt(udpSocket6, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, (char*)&ttl, sizeof(ttl)) < 0)
    {
        std::cerr << "Failed to set multicast hop limit for IPv6.\n";
    }

    // Set up the IPv4 multicast address.
    sockaddr_in multicastAddr4{};
    multicastAddr4.sin_family = AF_INET;
    multicastAddr4.sin_addr.s_addr = inet_pton(AF_INET, "239.255.0.1", &multicastAddr4.sin_addr);
    multicastAddr4.sin_port = htons(3334);

    // Set up the IPv6 multicast address.
    sockaddr_in6 multicastAddr6{};
    multicastAddr6.sin6_family = AF_INET6;
    inet_pton(AF_INET6, "FF01:0:0:0:0:0:0:100", &multicastAddr6.sin6_addr);
    multicastAddr6.sin6_port = htons(3334);

    // Define the advertisement message.
    std::string advertMessage = "28000";

    // Periodically send out the advertisement.
    while (gKeepAdvertising)
    {
        int sent4 = sendto(udpSocket4, advertMessage.c_str(), static_cast<int>(advertMessage.size()), 0,
            reinterpret_cast<sockaddr*>(&multicastAddr4), sizeof(multicastAddr4));
        if (sent4 < 0)
        {
            std::cerr << "Failed to send IPv4 advertisement.\n";
        }

        int sent6 = sendto(udpSocket6, advertMessage.c_str(), static_cast<int>(advertMessage.size()), 0,
            reinterpret_cast<sockaddr*>(&multicastAddr6), sizeof(multicastAddr6));
        if (sent6 < 0)
        {
            std::cerr << "Failed to send IPv6 advertisement.\n";
        }

        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    closesocket(udpSocket4);
    closesocket(udpSocket6);
#ifdef _WIN32
    WSACleanup();
#endif
}

int main()
{
    // Start the UDP advertising in a separate thread.
    std::thread advThread(AdvertiseServer);

    // Start the TCP server.
    StartServer();

    // When the server stops, signal the advertiser to stop.
    gKeepAdvertising = false;
    advThread.join();

    return 0;
}
