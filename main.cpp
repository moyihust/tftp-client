#pragma comment(lib, "Ws2_32.lib")
#include <iostream>
#include <fstream>
#include <WinSock2.h>
#include <WS2tcpip.h>
void download_file(SOCKET sock, const std::string& server_address, int port, const std::string& filename) {
    sockaddr_in server_sockaddr;
    server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_port = htons(port);
    inet_pton(AF_INET, server_address.c_str(), &server_sockaddr.sin_addr);

    char packet[516] = {0};
    packet[1] = 1; // RRQ opcode
    strcpy(packet + 2, filename.c_str());
    strcpy(packet + 2 + filename.length() + 1, "octet");

    sendto(sock, packet, 4 + filename.length() + 1 + 5 + 1, 0, (sockaddr*)&server_sockaddr, sizeof(server_sockaddr));

    std::ofstream ofs(filename, std::ios::binary);

    int timeout = 5000; // 5000 ms or 5 seconds
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    int retry_count = 0;
    const int max_retry_count = 3;

    char last_ack_packet[4] = {0};

    while (true) {
        char data_packet[516];
        int server_sockaddr_len = sizeof(server_sockaddr);
        int received = recvfrom(sock, data_packet, 516, 0, (sockaddr*)&server_sockaddr, &server_sockaddr_len);

        if (received == SOCKET_ERROR) {
            if (WSAGetLastError() == WSAETIMEDOUT) {
                if (retry_count < max_retry_count) {
                    ++retry_count;
                    // Resend the last ack packet
                    if (last_ack_packet[0] != 0 || last_ack_packet[1] != 0) {
                        sendto(sock, last_ack_packet, 4, 0, (sockaddr*)&server_sockaddr, sizeof(server_sockaddr));
                    }
                    continue;
                } else {
                    std::cerr << "Maximum retries reached. Exiting.\n";
                    break;
                }
            } else {
                std::cerr << "An error occurred: " << WSAGetLastError() << '\n';
                break;
            }
        }

        // Reset the retry counter upon successful reception
        retry_count = 0;

        ofs.write(data_packet + 4, received - 4);

        char ack_packet[4] = {0};
        ack_packet[1] = 4; // ACK opcode
        ack_packet[2] = data_packet[2];
        ack_packet[3] = data_packet[3];

        // Store this ACK packet in case we need to resend it
        memcpy(last_ack_packet, ack_packet, 4);

        sendto(sock, ack_packet, 4, 0, (sockaddr*)&server_sockaddr, sizeof(server_sockaddr));

        if (received < 516) {
            break;
        }
    }

    ofs.close();
}


void upload_file(SOCKET sock, const std::string& server_address, int port, const std::string& filename, int timeout_ms = 5000) {
    sockaddr_in server_sockaddr;
    server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_port = htons(port);
    inet_pton(AF_INET, server_address.c_str(), &server_sockaddr.sin_addr);

    // 设置接收超时
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout_ms, sizeof(timeout_ms));
    // 设置发送超时
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout_ms, sizeof(timeout_ms));

    char packet[516] = {0};
    packet[1] = 2; // WRQ opcode
    strcpy(packet + 2, filename.c_str());
    strcpy(packet + 2 + filename.length() + 1, "octet");

    sendto(sock, packet, 4 + filename.length() + 1 + 5 + 1, 0, (sockaddr*)&server_sockaddr, sizeof(server_sockaddr));

    char ack_packet[4];
    int server_sockaddr_len = sizeof(server_sockaddr);
    if (recvfrom(sock, ack_packet, 4, 0, (sockaddr*)&server_sockaddr, &server_sockaddr_len) == SOCKET_ERROR) {
        if (WSAGetLastError() == WSAETIMEDOUT) {
            std::cerr << "Receive ACK timeout. Exiting.\n";
            return;
        }
    }

    std::ifstream ifs(filename, std::ios::binary);
    char file_buf[512];
    unsigned short block_number = 1;

    while (true) {
        ifs.read(file_buf, 512);
        int read_bytes = ifs.gcount();

        if (read_bytes <= 0) {
            break;
        }

        char data_packet[516] = {0};
        data_packet[1] = 3; // DATA opcode

        // Set block number
        data_packet[2] = (block_number >> 8) & 0xFF;
        data_packet[3] = block_number & 0xFF;

        memcpy(data_packet + 4, file_buf, read_bytes);

        if (sendto(sock, data_packet, 4 + read_bytes, 0, (sockaddr*)&server_sockaddr, sizeof(server_sockaddr)) == SOCKET_ERROR) {
            if (WSAGetLastError() == WSAETIMEDOUT) {
                std::cerr << "Send DATA timeout. Exiting.\n";
                break;
            }
        }

        if (recvfrom(sock, ack_packet, 4, 0, (sockaddr*)&server_sockaddr, &server_sockaddr_len) == SOCKET_ERROR) {
            if (WSAGetLastError() == WSAETIMEDOUT) {
                std::cerr << "Receive ACK timeout. Retrying...\n";
                continue;
            }
        }

        // Verify if ACK is for the current block
        if (ack_packet[2] == ((block_number >> 8) & 0xFF) && ack_packet[3] == (block_number & 0xFF)) {
            block_number++; // Increment block number for the next loo
        
        }
        

    }

    ifs.close();
}


int main() {
    // Initialize Winsock, create socket, etc.
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
    std::cerr << "WSAStartup failed." << std::endl;
    return 1;
    }

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);

    //download_file(sock, "127.0.0.1", 69, "light.png");
    upload_file(sock, "127.0.0.1", 69, "light.png");

    closesocket(sock);
    WSACleanup();
    return 0;
}
