#include <arpa/inet.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

class SocketRAII {
private:
  int fd;

public:
  explicit SocketRAII(int fd = -1) : fd(fd) {}
  ~SocketRAII() {
    if (fd != -1)
      close(fd);
  }
  SocketRAII(const SocketRAII &) = delete;
  SocketRAII &operator=(const SocketRAII &) = delete;
  SocketRAII(SocketRAII &&other) noexcept : fd(other.fd) { other.fd = -1; }
  SocketRAII &operator=(SocketRAII &&other) noexcept = delete;
  operator int() const { return fd; }
};

void error(const char *msg) {
  perror(msg);
  exit(EXIT_FAILURE);
}

SocketRAII setup_server_socket(int port, int type) {
  SocketRAII server_fd(socket(AF_INET, type, 0));
  if (server_fd < 0)
    error("skill issue");

  if (type == SOCK_STREAM) {
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
      error("skill issue");
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(port);

  if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    error("skill issue");
  return server_fd;
}

SocketRAII setup_client_socket(int type, const char *host, int port,
                               sockaddr_in &serv_addr) {
  SocketRAII sock_fd(socket(AF_INET, type, 0));
  if (sock_fd < 0)
    error("skill issue");

  serv_addr = {};
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(port);

  if (inet_pton(AF_INET, host, &serv_addr.sin_addr) <= 0)
    error("invalid address");
  return sock_fd;
}

void handle_tcp_client(SocketRAII client_socket) {
  std::cout << "Handling client on socket " << static_cast<int>(client_socket)
            << std::endl;
  char buffer[25000];
  while (true) {
    ssize_t bytes = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes == 0) {
      std::cout << "Client disconnected.\n";
      break;
    }
    if (bytes < 0) {
      std::cout << "Recv failed.\n";
      break;
    }
    buffer[bytes] = '\0';
    std::cout << "Client: " << buffer << std::flush;

    std::cout << "Server> ";
    std::string reply;
    if (!std::getline(std::cin, reply))
      break;
    reply += "\n";
    if (send(client_socket, reply.c_str(), reply.length(), 0) < 0)
      break;
  }
  std::cout << "Connection closed. Waiting for new client..." << std::endl;
}

void run_tcp_server(int port) {
  SocketRAII server_fd = setup_server_socket(port, SOCK_STREAM);
  if (listen(server_fd, 5) < 0)
    error("skill issue");
  std::cout << "TCP Server listening on port " << port << std::endl;

  while (true) {
    SocketRAII client_socket(accept(server_fd, NULL, NULL));
    if (client_socket < 0) {
      perror("skill issue");
    } else {
      handle_tcp_client(std::move(client_socket));
    }
  }
}

void run_tcp_client(const char *host, int port) {
  sockaddr_in serv_addr;
  SocketRAII sock = setup_client_socket(SOCK_STREAM, host, port, serv_addr);

  if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    error("connection failed");
  std::cout << "Connected to TCP server " << host << ":" << port << std::endl;

  char buffer[25000];
  while (true) {
    std::cout << "Client> ";
    std::string message;
    if (!std::getline(std::cin, message))
      break;
    message += "\n";
    if (send(sock, message.c_str(), message.length(), 0) < 0)
      break;

    ssize_t bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (bytes == 0) {
      std::cout << "Server disconnected.\n";
      break;
    }
    if (bytes < 0) {
      std::cout << "skill issue\n";
      break;
    }
    buffer[bytes] = '\0';
    std::cout << "Server: " << buffer << std::flush;
  }
}

void run_udp_server(int port) {
  SocketRAII sockfd = setup_server_socket(port, SOCK_DGRAM);
  std::cout << "UDP Server listening on port " << port << std::endl;

  char buffer[1024];
  sockaddr_in cliaddr;
  socklen_t len = sizeof(cliaddr);
  while (true) {
    ssize_t n = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0,
                         (sockaddr *)&cliaddr, &len);
    if (n < 0)
      continue;
    buffer[n] = '\0';

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &cliaddr.sin_addr, client_ip, INET_ADDRSTRLEN);
    std::cout << "Client (" << client_ip << "): " << buffer << std::endl;

    const char *reply = "Message received";
    sendto(sockfd, reply, strlen(reply), 0, (sockaddr *)&cliaddr, len);
  }
}

void run_udp_client(const char *host, int port) {
  sockaddr_in servaddr;
  SocketRAII sockfd = setup_client_socket(SOCK_DGRAM, host, port, servaddr);
  std::cout << "UDP Client sending to " << host << ":" << port << std::endl;

  char buffer[1024];
  while (true) {
    std::cout << "Client> ";
    std::string message;
    if (!std::getline(std::cin, message) || message == "exit")
      break;

    sendto(sockfd, message.c_str(), message.length(), 0, (sockaddr *)&servaddr,
           sizeof(servaddr));

    ssize_t n = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0, NULL, NULL);
    if (n < 0)
      continue;
    buffer[n] = '\0';
    std::cout << "Server: " << buffer << std::endl;
  }
}

void usage(const char *program_name) {
  std::cerr << "Usage:\n"
            << "  server: " << program_name << " server <tcp|udp> <port>\n"
            << "  client: " << program_name
            << " client <tcp|udp> <host> <port>\n";
  exit(EXIT_FAILURE);
}

int main(int argc, char const *argv[]) {
  if (argc < 4)
    usage(argv[0]);

  std::string mode = argv[1];
  std::string protocol = argv[2];

  if (mode == "server" && argc == 4) {
    int port = std::atoi(argv[3]);
    if (protocol == "tcp") {
      run_tcp_server(port);
    } else if (protocol == "udp") {
      run_udp_server(port);
    } else {
      usage(argv[0]);
    }
  } else if (mode == "client" && argc == 5) {
    const char *host = argv[3];
    int port = std::atoi(argv[4]);
    if (protocol == "tcp")
      run_tcp_client(host, port);
    else if (protocol == "udp")
      run_udp_client(host, port);
    else
      usage(argv[0]);
  } else {
    usage(argv[0]);
  }
}