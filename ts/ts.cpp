#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vector>
#include <thread>
#include <mutex>
#include <algorithm> // std::remove를 사용하기 위해 추가
#ifdef __linux__
#include <arpa/inet.h>
#include <sys/socket.h>
#endif // __linux__

std::vector<int> clients;
std::mutex clients_mutex;

#ifdef WIN32
void myerror(const char* msg) { fprintf(stderr, "%s %lu\n", msg, GetLastError()); }
#else
void myerror(const char* msg) { fprintf(stderr, "%s %s %d\n", msg, strerror(errno), errno); }
#endif

void usage() {
    printf("syntax: echo-server <port> [-e] [-b]\n");
    printf("sample: echo-server 1234 -e -b\n");
}

struct Param {
    bool echo{false};
    bool broadcast{false};
    uint16_t port{0};

    bool parse(int argc, char* argv[]) {
        for (int i = 1; i < argc;) {
            if (strcmp(argv[i], "-e") == 0) {
                echo = true;
                i++;
                continue;
            }
            if (strcmp(argv[i], "-b") == 0) {
                broadcast = true;
                i++;
                continue;
            }
            if (i < argc) port = atoi(argv[i++]);
        }
        return port != 0;
    }
} param;

void recvThread(int sd) {
    printf("connected\n");
    fflush(stdout);
    static const int BUFSIZE = 65536;
    char buf[BUFSIZE];
    while (true) {
        ssize_t res = ::recv(sd, buf, BUFSIZE - 1, 0);
        if (res == 0 || res == -1) {
            fprintf(stderr, "recv return %zd", res);
            myerror("recv");
            break;
        }
        buf[res] = '\0';
        printf("%s", buf);
        fflush(stdout);

        if (param.broadcast) {
            std::lock_guard<std::mutex> lock(clients_mutex);
            for (int client_sd : clients) {
                if (client_sd != sd) {
                    ::send(client_sd, buf, res, 0);
                }
            }
        }

        if (param.echo) {
            res = ::send(sd, buf, res, 0);
            if (res == 0 || res == -1) {
                fprintf(stderr, "send return %zd", res);
                myerror("send");
                break;
            }
        }
    }
    printf("disconnected\n");
    fflush(stdout);
    ::close(sd);

    std::lock_guard<std::mutex> lock(clients_mutex);
    clients.erase(std::remove(clients.begin(), clients.end(), sd), clients.end());
}

int main(int argc, char* argv[]) {
    if (!param.parse(argc, argv)) {
        usage();
        return -1;
    }

#ifdef WIN32
    WSAData wsaData;
    WSAStartup(0x0202, &wsaData);
#endif // WIN32

    int sd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sd == -1) {
        myerror("socket");
        return -1;
    }

#ifdef __linux__
    int optval = 1;
    if (::setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
        myerror("setsockopt");
        return -1;
    }
#endif // __linux__

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(param.port);

    if (::bind(sd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        myerror("bind");
        return -1;
    }

    if (listen(sd, 5) == -1) {
        myerror("listen");
        return -1;
    }

    while (true) {
        struct sockaddr_in addr;
        socklen_t len = sizeof(addr);
        int newsd = ::accept(sd, (struct sockaddr*)&addr, &len);
        if (newsd == -1) {
            myerror("accept");
            break;
        }

        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            clients.push_back(newsd);
        }

        std::thread* t = new std::thread(recvThread, newsd);
        t->detach();
    }
    ::close(sd);
}
