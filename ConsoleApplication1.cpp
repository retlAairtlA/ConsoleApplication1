// ConsoleApplication1.cpp : This file contains the 'main' function. Program execution begins and ends there.
//
//
//#include <iostream>
//
//int main()
//{
//    std::cout << "Hello World!\n";
//}
//
// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu
//
// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
#include <iostream>
#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>

#pragma comment(lib, "ws2_32.lib")

using namespace std;

// Tamanho do buffer para transferência de dados
#define BUFFER_SIZE 4096

// Função para lidar com a comunicação entre cliente e servidor
void handle_client(SOCKET client_sock, const string& server_ip, int server_port) {
    // Criar socket para o servidor de destino
    SOCKET server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_sock == INVALID_SOCKET) {
        cerr << "Erro ao criar socket do servidor: " << WSAGetLastError() << endl;
        closesocket(client_sock);
        return;
    }

    // Configurar endereço do servidor de destino
    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr);

    // Conectar ao servidor de destino
    if (connect(server_sock, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        cerr << "Erro ao conectar ao servidor de destino: " << WSAGetLastError() << endl;
        closesocket(client_sock);
        closesocket(server_sock);
        return;
    }

    // Função para encaminhar dados em uma direção
    auto forward_data = [](SOCKET from, SOCKET to) {
        char buffer[BUFFER_SIZE];
        int bytes_read;

        while ((bytes_read = recv(from, buffer, BUFFER_SIZE, 0)) > 0) {
            if (send(to, buffer, bytes_read, 0) == SOCKET_ERROR) {
                cerr << "Erro ao enviar dados: " << WSAGetLastError() << endl;
                break;
            }
        }

        // Encerrar as conexões parcialmente
        shutdown(from, SD_RECEIVE);
        shutdown(to, SD_SEND);
        };

    // Criar threads para comunicação bidirecional
    thread client_to_server(forward_data, client_sock, server_sock);
    thread server_to_client(forward_data, server_sock, client_sock);

    client_to_server.detach();
    server_to_client.detach();
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        cerr << "Uso: " << argv[0] << " [porta_proxy] [ip_servidor] [porta_servidor]" << endl;
        return 1;
    }

    int proxy_port = stoi(argv[1]);
    string server_ip = argv[2];
    int server_port = stoi(argv[3]);

    // Inicializar Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "Falha ao inicializar Winsock: " << WSAGetLastError() << endl;
        return 1;
    }

    // Criar socket do proxy
    SOCKET proxy_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (proxy_sock == INVALID_SOCKET) {
        cerr << "Erro ao criar socket do proxy: " << WSAGetLastError() << endl;
        WSACleanup();
        return 1;
    }

    // Configurar endereço do proxy
    sockaddr_in proxy_addr;
    proxy_addr.sin_family = AF_INET;
    proxy_addr.sin_addr.s_addr = INADDR_ANY;
    proxy_addr.sin_port = htons(proxy_port);

    // Vincular socket à porta
    if (bind(proxy_sock, (sockaddr*)&proxy_addr, sizeof(proxy_addr)) == SOCKET_ERROR) {
        cerr << "Erro ao vincular socket: " << WSAGetLastError() << endl;
        closesocket(proxy_sock);
        WSACleanup();
        return 1;
    }

    // Começar a escutar por conexões
    if (listen(proxy_sock, SOMAXCONN) == SOCKET_ERROR) {
        cerr << "Erro ao escutar no socket: " << WSAGetLastError() << endl;
        closesocket(proxy_sock);
        WSACleanup();
        return 1;
    }

    cout << "Proxy rodando na porta " << proxy_port
        << ", encaminhando para " << server_ip << ":" << server_port << endl;

    // Aceitar conexões de clientes
    while (true) {
        sockaddr_in client_addr;
        int client_addr_size = sizeof(client_addr);

        SOCKET client_sock = accept(proxy_sock, (sockaddr*)&client_addr, &client_addr_size);
        if (client_sock == INVALID_SOCKET) {
            cerr << "Erro ao aceitar conexão: " << WSAGetLastError() << endl;
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        cout << "Conexão aceita de " << client_ip << ":" << ntohs(client_addr.sin_port) << endl;

        // Lidar com o cliente em uma nova thread
        thread(handle_client, client_sock, server_ip, server_port).detach();
    }

    // Limpeza (teórica - o loop acima é infinito)
    closesocket(proxy_sock);
    WSACleanup();
    return 0;
}