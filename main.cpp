#pragma comment(lib, "Ws2_32.lib")
#include <iostream>
#include <fstream>
#include <WinSock2.h>
#include <WS2tcpip.h>
int extractBlockNum(const char* buf) {
    int BlockNum;
    if (buf[3] == 0xff && buf[4] == 0xff && buf[5] == 0xff) {
        BlockNum = (buf[2] << 8) + (buf[6] & 0xff);
    } else {
        BlockNum = (buf[2] << 8) + (buf[3] & 0xff);
    }
    return BlockNum;
}

void download_file(SOCKET sock, const std::string& server_address, int port, const std::string& filename) {
    std::ofstream logFile("log/download_log.txt", std::ios::app); // Open a log file in append mode
    logFile << "Start downloading file: " << filename << '\n';

    sockaddr_in server_sockaddr;
    server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_port = htons(port);
    inet_pton(AF_INET, server_address.c_str(), &server_sockaddr.sin_addr);

    char packet[516] = {0};
    packet[1] = 1; // RRQ opcode
    strcpy(packet + 2, filename.c_str());
    strcpy(packet + 2 + filename.length() + 1, "octet");

    sendto(sock, packet, 4 + filename.length() + 1 + 5 + 1, 0, (sockaddr*)&server_sockaddr, sizeof(server_sockaddr));
    logFile << "Sent RRQ for file: " << filename << '\n';

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
                    logFile << "Timeout. Resending last ACK. Retry count: " << retry_count << '\n';
                    if (last_ack_packet[0] != 0 || last_ack_packet[1] != 0) {
                        sendto(sock, last_ack_packet, 4, 0, (sockaddr*)&server_sockaddr, sizeof(server_sockaddr));
                    }
                    continue;
                } else {
                    logFile << "Maximum retries reached. Exiting.\n";
                    std::cerr << "Maximum retries reached. Exiting.\n";
                    break;
                }
            } else {
                logFile << "An error occurred: " << WSAGetLastError() << '\n';
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
            logFile << "File download completed.\n";
            break;
        }
    }

    ofs.close();
    logFile.close(); // Close the log file
}


void upload_file(SOCKET sock, const std::string& server_address, int port, const std::string& filename, int timeout_ms = 5000) {
    std::ofstream logFile("log/upload_log.txt", std::ios::app); // Open a log file in append mode
    logFile << "Start uploading file: " << filename << '\n';

    sockaddr_in server_sockaddr;
    server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_port = htons(port);
    inet_pton(AF_INET, server_address.c_str(), &server_sockaddr.sin_addr);

    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout_ms, sizeof(timeout_ms));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout_ms, sizeof(timeout_ms));

    char packet[516] = {0};
    packet[1] = 2; // WRQ opcode
    strcpy(packet + 2, filename.c_str());
    strcpy(packet + 2 + filename.length() + 1, "octet");

    sendto(sock, packet, 4 + filename.length() + 1 + 5 + 1, 0, (sockaddr*)&server_sockaddr, sizeof(server_sockaddr));
    logFile << "Sent WRQ for file: " << filename << '\n';

    char ack_packet[4];
    int server_sockaddr_len = sizeof(server_sockaddr);
    if (recvfrom(sock, ack_packet, 4, 0, (sockaddr*)&server_sockaddr, &server_sockaddr_len) == SOCKET_ERROR) {
        if (WSAGetLastError() == WSAETIMEDOUT) {
            logFile << "Receive ACK timeout. Exiting.\n";
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
            logFile << "End of file reached. Exiting loop.\n";
            break;
        }

        char data_packet[516] = {0};
        data_packet[1] = 3; // DATA opcode

        // Set block number
        data_packet[2] = (unsigned char)((block_number >> 8) & 0xFF);
        data_packet[3] = (unsigned char)(block_number & 0xFF);

        memcpy(data_packet + 4, file_buf, read_bytes);

        if (sendto(sock, data_packet, 4 + read_bytes, 0, (sockaddr*)&server_sockaddr, sizeof(server_sockaddr)) == SOCKET_ERROR) {
            if (WSAGetLastError() == WSAETIMEDOUT) {
                logFile << "Send DATA timeout. Exiting.\n";
                std::cerr << "Send DATA timeout. Exiting.\n";
                break;
            }
        }

        logFile << "Sent DATA block: " << block_number << '\n';

        if (recvfrom(sock, ack_packet, 4, 0, (sockaddr*)&server_sockaddr, &server_sockaddr_len) == SOCKET_ERROR) {
            if (WSAGetLastError() == WSAETIMEDOUT) {
                logFile << "Receive ACK timeout. Retrying...\n";
                std::cerr << "Receive ACK timeout. Retrying...\n";
                continue;
            }
        }

        // Verify if ACK is for the current block
        for(auto i:ack_packet){printf("%02x",i);}
 if (extractBlockNum(ack_packet) == block_number) {
            logFile << "Received ACK for block: " << block_number << '\n';
            block_number++; // Increment block number for the next loop
            //if(block_number>127) block_number=1;
        }
    }

    ifs.close();
    logFile.close(); // Close the log file
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
    upload_file(sock, "127.0.0.1", 69, "name.jpg");

    closesocket(sock);
    WSACleanup();
    return 0;
}
