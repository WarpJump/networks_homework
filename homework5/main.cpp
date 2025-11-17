#include <arpa/inet.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/ssl.h>

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

class SslCtxRAII {
private:
  SSL_CTX *ctx;

public:
  explicit SslCtxRAII(SSL_CTX *ctx) : ctx(ctx) {}
  ~SslCtxRAII() {
    if (ctx)
      SSL_CTX_free(ctx);
  }
  operator SSL_CTX *() const { return ctx; }
};

class SslRAII {
private:
  SSL *ssl;

public:
  explicit SslRAII(SSL *ssl) : ssl(ssl) {}
  ~SslRAII() {
    if (ssl) {
      SSL_shutdown(ssl);
      SSL_free(ssl);
    }
  }
  operator SSL *() const { return ssl; }
};

void error(const char *msg) {
  perror(msg);
  ERR_print_errors_fp(stderr);
  exit(EXIT_FAILURE);
}

FILE *keylog_file = nullptr;

void keylog_callback(const SSL *ssl, const char *line) {
  if (keylog_file) {
    fprintf(keylog_file, "%s\n", line);
    fflush(keylog_file);
  }
}

void setup_keylog() {
  const char *keylog_path = getenv("SSLKEYLOGFILE");
  if (keylog_path) {
    keylog_file = fopen(keylog_path, "a");
    if (!keylog_file) {
      perror("Could not open SSLKEYLOGFILE");
    } else {
      std::cout << "Logging SSL keys to " << keylog_path << std::endl;
    }
  }
}

SocketRAII setup_server_socket(int port) {
  SocketRAII server_fd(socket(AF_INET, SOCK_STREAM, 0));
  if (server_fd < 0)
    error("skill issue on socket creation");

  int opt = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
    error("skill issue on setsockopt");

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(port);

  if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    error("skill issue on bind");
  return server_fd;
}

SocketRAII setup_client_socket(const char *host, int port,
                               sockaddr_in &serv_addr) {
  SocketRAII sock_fd(socket(AF_INET, SOCK_STREAM, 0));
  if (sock_fd < 0)
    error("skill issue on socket creation");

  serv_addr = {};
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(port);

  if (inet_pton(AF_INET, host, &serv_addr.sin_addr) <= 0)
    error("invalid address");
  return sock_fd;
}

void handle_tcp_client_tls(SocketRAII client_socket, SSL_CTX *ctx) {
  std::cout << "Handling client on socket " << static_cast<int>(client_socket)
            << " with TLS" << std::endl;

  SslRAII ssl(SSL_new(ctx));
  if (!ssl)
    error("SSL_new failed");
  SSL_set_fd(ssl, client_socket);

  if (SSL_accept(ssl) <= 0) {
    return;
  }
  std::cout << "TLS handshake successful." << std::endl;

  char buffer[25000];
  while (true) {
    ssize_t bytes = SSL_read(ssl, buffer, sizeof(buffer) - 1);
    if (bytes <= 0) {
      int err = SSL_get_error(ssl, bytes);
      if (err == SSL_ERROR_ZERO_RETURN) {
        std::cout << "Client disconnected gracefully.\n";
      } else {
        std::cout << "SSL_read failed. Error: " << err << std::endl;
        ERR_print_errors_fp(stderr);
      }
      break;
    }
    buffer[bytes] = '\0';
    std::cout << "Client: " << buffer << std::flush;

    std::cout << "Server> ";
    std::string reply;
    if (!std::getline(std::cin, reply))
      break;
    reply += "\n";
    if (SSL_write(ssl, reply.c_str(), reply.length()) <= 0) {
      std::cout << "SSL_write failed.\n";
      ERR_print_errors_fp(stderr);
      break;
    }
  }
  std::cout << "Connection closed. Waiting for new client..." << std::endl;
}

void run_tcp_server(int port, const char *cert_path, const char *key_path) {
  SslCtxRAII ctx(SSL_CTX_new(TLS_server_method()));
  if (!ctx)
    error("SSL_CTX_new failed");

  SSL_CTX_use_certificate_file(ctx, cert_path, SSL_FILETYPE_PEM);

  SSL_CTX_use_PrivateKey_file(ctx, key_path, SSL_FILETYPE_PEM);

  SocketRAII server_fd = setup_server_socket(port);
  if (listen(server_fd, 5) < 0)
    error("listen failed");
  std::cout << "TCP Server with TLS listening on port " << port << std::endl;

  while (true) {
    SocketRAII client_socket(accept(server_fd, NULL, NULL));
    if (client_socket < 0) {
      perror("accept failed");
    } else {
      handle_tcp_client_tls(std::move(client_socket), ctx);
    }
  }
}

void run_tcp_client(const char *host, int port) {
  SslCtxRAII ctx(SSL_CTX_new(TLS_client_method()));

  sockaddr_in serv_addr;
  SocketRAII sock = setup_client_socket(host, port, serv_addr);

  if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    error("connection failed");

  SslRAII ssl(SSL_new(ctx));
  if (!ssl)
    error("SSL_new failed");
  SSL_set_fd(ssl, sock);

  if (SSL_connect(ssl) <= 0) {
    error("SSL_connect failed");
  }

  std::cout << "Connected to TCP server " << host << ":" << port << " with TLS"
            << std::endl;

  char buffer[25000];
  while (true) {
    std::cout << "Client> ";
    std::string message;
    if (!std::getline(std::cin, message))
      break;
    message += "\n";
    if (SSL_write(ssl, message.c_str(), message.length()) <= 0) {
      ERR_print_errors_fp(stderr);
      break;
    }

    ssize_t bytes = SSL_read(ssl, buffer, sizeof(buffer) - 1);
    if (bytes <= 0) {
      int err = SSL_get_error(ssl, bytes);
      if (err == SSL_ERROR_ZERO_RETURN) {
        std::cout << "Server disconnected gracefully.\n";
      } else {
        std::cout << "SSL_read failed. Error: " << err << std::endl;
        ERR_print_errors_fp(stderr);
      }
      break;
    }
    buffer[bytes] = '\0';
    std::cout << "Server: " << buffer << std::flush;
  }
}

void usage(const char *program_name) {
  std::cerr << "Usage:\n"
            << "  server: " << program_name
            << " server <port> <cert_file> <key_file>\n"
            << "  client: " << program_name << " client <host> <port>\n";
  exit(EXIT_FAILURE);
}

int main(int argc, char const *argv[]) {
  SSL_library_init();
  OpenSSL_add_all_algorithms();
  SSL_load_error_strings();

  setup_keylog();
  if (keylog_file) {
    atexit([] {
      if (keylog_file)
        fclose(keylog_file);
    });
  }

  if (argc < 2)
    usage(argv[0]);

  std::string mode = argv[1];

  if (mode == "server") {
    if (argc != 5)
      usage(argv[0]);
    int port = std::atoi(argv[2]);
    const char *cert_file = argv[3];
    const char *key_file = argv[4];
    run_tcp_server(port, cert_file, key_file);
  } else if (mode == "client") {
    if (argc != 4)
      usage(argv[0]);
    const char *host = argv[2];
    int port = std::atoi(argv[3]);
    run_tcp_client(host, port);
  } else {
    usage(argv[0]);
  }

  EVP_cleanup();
  ERR_free_strings();
}