#include <iostream>
#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <atomic>
#include <vector>
#include <memory>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <sstream>
#include <mutex>

#pragma comment(lib, "ws2_32.lib")

using namespace std;
using namespace std::chrono;

// Configurações
#define BUFFER_SIZE 4096
#define LOG_FILE "proxy_log.txt"
#define _CRT_SECURE_NO_WARNINGS  // Para desativar avisos de funções seguras

// Variáveis globais
atomic<bool> g_running(true);
mutex log_mutex;

// Estrutura para estatísticas de conexão
struct ConnectionStats {
    time_point<system_clock> start_time;
    time_point<system_clock> end_time;
    size_t client_to_server_bytes = 0;
    size_t server_to_client_bytes = 0;
    vector<pair<time_point<system_clock>, string>> events;
};

// Estrutura para gerenciar conexões
struct ClientConnection {
    SOCKET client;
    SOCKET server;
    thread client_thread;
    thread server_thread;
    shared_ptr<ConnectionStats> stats;
    string client_ip;
    int client_port;
};

// Função para converter dados em hexadecimal
string to_hex(const char* data, size_t length) {
    stringstream ss;
    ss << hex << setfill('0');
    for (size_t i = 0; i < length; ++i) {
        ss << setw(2) << static_cast<int>(static_cast<unsigned char>(data[i])) << " ";
        if ((i + 1) % 16 == 0) ss << "\n";
    }
    return ss.str();
}

// Função segura para formatar tempo
string format_time(const system_clock::time_point& time) {
    auto ms = duration_cast<milliseconds>(time.time_since_epoch()) % 1000;
    time_t tt = system_clock::to_time_t(time);
    tm tm;

    // Usando localtime_s em vez de localtime
    localtime_s(&tm, &tt);

    stringstream ss;
    ss << put_time(&tm, "%Y-%m-%d %H:%M:%S") << "."
        << setfill('0') << setw(3) << ms.count();
    return ss.str();
}

// Função para registrar logs com timestamp
void log_event(const string& message, shared_ptr<ConnectionStats> stats = nullptr) {
    auto now = system_clock::now();
    string timestamp = format_time(now);

    lock_guard<mutex> guard(log_mutex);

    ofstream logfile(LOG_FILE, ios::app);
    if (logfile.is_open()) {
        logfile << timestamp << " - " << message << endl;

        if (stats) {
            stats->events.emplace_back(now, message);
        }
    }
    cout << timestamp << " - " << message << endl;
}

// Função para encaminhar dados com logging
void forward_data(SOCKET from, SOCKET to, const string& direction,
    shared_ptr<ConnectionStats> stats, const string& conn_id) {
    char buffer[BUFFER_SIZE];
    int bytes_read;

    while (g_running && (bytes_read = recv(from, buffer, BUFFER_SIZE, 0)) > 0) {
        auto start = high_resolution_clock::now();

        if (send(to, buffer, bytes_read, 0) == SOCKET_ERROR) {
            log_event("[" + conn_id + "] Erro ao enviar dados (" + direction + "): " + to_string(WSAGetLastError()), stats);
            break;
        }

        auto end = high_resolution_clock::now();
        auto duration = duration_cast<microseconds>(end - start);

        // Atualizar estatísticas
        if (direction == "client->server") {
            stats->client_to_server_bytes += bytes_read;
        }
        else {
            stats->server_to_client_bytes += bytes_read;
        }

        // Log detalhado
        stringstream ss;
        ss << "[" << conn_id << "] " << direction << " " << bytes_read << " bytes\n";
        ss << "Tempo de transmissão: " << duration.count() << " ?s\n";
        ss << "Dados hex:\n" << to_hex(buffer, min(32, bytes_read)) << (bytes_read > 32 ? "...\n" : "\n");

        log_event(ss.str(), stats);
    }

    shutdown(from, SD_RECEIVE);
    shutdown(to, SD_SEND);
}

// Função para lidar com a conexão do cliente
void handle_client(shared_ptr<ClientConnection> conn, const string& server_ip, int server_port) {
    // Registrar início da conexão
    conn->stats->start_time = system_clock::now();
    string conn_id = conn->client_ip + ":" + to_string(conn->client_port);

    log_event("[" + conn_id + "] Conexão iniciada", conn->stats);

    // Configurar endereço do servidor de destino
    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr);

    // Medir tempo de conexão ao servidor
    auto connect_start = high_resolution_clock::now();

    if (connect(conn->server, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        log_event("[" + conn_id + "] Erro ao conectar ao servidor: " + to_string(WSAGetLastError()), conn->stats);
        closesocket(conn->client);
        closesocket(conn->server);
        return;
    }

    auto connect_end = high_resolution_clock::now();
    auto connect_time = duration_cast<milliseconds>(connect_end - connect_start);

    log_event("[" + conn_id + "] Conectado ao servidor em " + to_string(connect_time.count()) + " ms", conn->stats);

    // Criar threads para comunicação bidirecional
    conn->client_thread = thread(forward_data, conn->client, conn->server, "client->server", conn->stats, conn_id);
    conn->server_thread = thread(forward_data, conn->server, conn->client, "server->client", conn->stats, conn_id);

    // Aguardar término das threads
    if (conn->client_thread.joinable()) conn->client_thread.join();
    if (conn->server_thread.joinable()) conn->server_thread.join();

    // Registrar fim da conexão
    conn->stats->end_time = system_clock::now();
    auto duration = duration_cast<milliseconds>(conn->stats->end_time - conn->stats->start_time);

    stringstream stats_ss;
    stats_ss << "[" << conn_id << "] Conexão encerrada\n";
    stats_ss << "Duração: " << duration.count() << " ms\n";
    stats_ss << "Bytes cliente->servidor: " << conn->stats->client_to_server_bytes << "\n";
    stats_ss << "Bytes servidor->cliente: " << conn->stats->server_to_client_bytes;

    log_event(stats_ss.str(), conn->stats);

    // Fechar sockets
    closesocket(conn->client);
    closesocket(conn->server);
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        cerr << "Uso: " << argv[0] << " [porta_proxy] [ip_servidor] [porta_servidor]" << endl;
        cerr << "Exemplo: " << argv[0] << " 8080 127.0.0.1 80" << endl;
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

    // Configurar opções do socket
    int opt = 1;
    setsockopt(proxy_sock, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    // Configurar endereço do proxy
    sockaddr_in proxy_addr;
    proxy_addr.sin_family = AF_INET;
    proxy_addr.sin_addr.s_addr = INADDR_ANY;
    proxy_addr.sin_port = htons(proxy_port);

    // Vincular socket
    if (bind(proxy_sock, (sockaddr*)&proxy_addr, sizeof(proxy_addr)) == SOCKET_ERROR) {
        cerr << "Erro ao vincular socket: " << WSAGetLastError() << endl;
        closesocket(proxy_sock);
        WSACleanup();
        return 1;
    }

    // Escutar por conexões
    if (listen(proxy_sock, SOMAXCONN) == SOCKET_ERROR) {
        cerr << "Erro ao escutar no socket: " << WSAGetLastError() << endl;
        closesocket(proxy_sock);
        WSACleanup();
        return 1;
    }

    log_event("Proxy TCP iniciado na porta " + to_string(proxy_port) +
        ", encaminhando para " + server_ip + ":" + to_string(server_port));

    vector<shared_ptr<ClientConnection>> connections;

    // Loop principal para aceitar conexões
    while (g_running) {
        sockaddr_in client_addr;
        int client_addr_size = sizeof(client_addr);

        SOCKET client_sock = accept(proxy_sock, (sockaddr*)&client_addr, &client_addr_size);
        if (client_sock == INVALID_SOCKET) {
            if (g_running) {
                cerr << "Erro ao aceitar conexão: " << WSAGetLastError() << endl;
            }
            continue;
        }

        // Obter informações do cliente
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        int client_port = ntohs(client_addr.sin_port);

        // Criar socket para o servidor
        SOCKET server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (server_sock == INVALID_SOCKET) {
            log_event("Erro ao criar socket do servidor: " + to_string(WSAGetLastError()));
            closesocket(client_sock);
            continue;
        }

        // Configurar estrutura de conexão
        auto conn = make_shared<ClientConnection>();
        conn->client = client_sock;
        conn->server = server_sock;
        conn->client_ip = client_ip;
        conn->client_port = client_port;
        conn->stats = make_shared<ConnectionStats>();

        connections.push_back(conn);

        // Iniciar thread para lidar com a conexão
        thread(handle_client, conn, server_ip, server_port).detach();
    }

    // Limpeza
    log_event("Encerrando proxy...");
    closesocket(proxy_sock);

    for (auto& conn : connections) {
        shutdown(conn->client, SD_BOTH);
        shutdown(conn->server, SD_BOTH);
        closesocket(conn->client);
        closesocket(conn->server);
    }

    WSACleanup();
    return 0;
}