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
    error("socket creation failed");

  int opt = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
    error("setsockopt failed");

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(port);

  if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    error("bind failed");
  return server_fd;
}

SocketRAII setup_client_socket(const char *host, int port,
                               sockaddr_in &serv_addr) {
  SocketRAII sock_fd(socket(AF_INET, SOCK_STREAM, 0));
  if (sock_fd < 0)
    error("socket creation failed");

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
    ERR_print_errors_fp(stderr);
    return;
  }
  std::cout << "TLS handshake successful." << std::endl;

  char buffer[25000];
  while (true) {
    ssize_t bytes = SSL_read(ssl, buffer, sizeof(buffer) - 1);
    if (bytes <= 0) {
      int err = SSL_get_error(ssl, bytes);
      if (err == SSL_ERROR_ZERO_RETURN) {
        std::cout
            << "Client disconnected gracefully (received close_notify).\n";
      } else {
        std::cout << "SSL_read failed or conn lost. Error code: " << err
                  << std::endl;
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
    if (SSL_write(ssl, reply.c_str(), reply.length()) <= 0)
      break;
  }
  std::cout << "Connection closed. Waiting for new client..." << std::endl;
}

void run_tcp_server(int port, const char *cert_path, const char *key_path) {
  SslCtxRAII ctx(SSL_CTX_new(TLS_server_method()));
  if (!ctx)
    error("SSL_CTX_new failed");

  if (keylog_file)
    SSL_CTX_set_keylog_callback(ctx, keylog_callback);

  if (SSL_CTX_use_certificate_file(ctx, cert_path, SSL_FILETYPE_PEM) <= 0)
    error("Failed to load certificate");
  if (SSL_CTX_use_PrivateKey_file(ctx, key_path, SSL_FILETYPE_PEM) <= 0)
    error("Failed to load private key");
  if (!SSL_CTX_check_private_key(ctx))
    error("Private key does not match certificate");

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

void run_tcp_client(const char *host, int port, const char *ca_cert_path) {
  SslCtxRAII ctx(SSL_CTX_new(TLS_client_method()));
  if (!ctx)
    error("SSL_CTX_new failed");

  if (keylog_file)
    SSL_CTX_set_keylog_callback(ctx, keylog_callback);

  if (SSL_CTX_load_verify_locations(ctx, ca_cert_path, nullptr) != 1)
    error("Failed to load CA certificate for verification");

  SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);

  sockaddr_in serv_addr;
  SocketRAII sock = setup_client_socket(host, port, serv_addr);

  if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    error("connection failed");

  SslRAII ssl(SSL_new(ctx));
  if (!ssl)
    error("SSL_new failed");
  SSL_set_fd(ssl, sock);

  SSL_set_tlsext_host_name(ssl, host);

  if (SSL_connect(ssl) <= 0) {
    error("SSL_connect failed");
  }

  std::cout << "Connected to TCP server " << host << ":" << port << " with TLS"
            << std::endl;

  char buffer[25000];
  while (true) {
    std::cout << "Client> ";
    std::string message;
    if (!std::getline(std::cin, message)) {
      std::cout << "\nSending graceful shutdown..." << std::endl;
      break;
    }
    message += "\n";
    if (SSL_write(ssl, message.c_str(), message.length()) <= 0)
      break;

    ssize_t bytes = SSL_read(ssl, buffer, sizeof(buffer) - 1);
    if (bytes <= 0) {
      int err = SSL_get_error(ssl, bytes);
      if (err == SSL_ERROR_ZERO_RETURN) {
        std::cout << "Server disconnected gracefully.\n";
      } else {
        std::cout << "Connection lost.\n";
      }
      break;
    }
    buffer[bytes] = '\0';
    std::cout << "Server: " << buffer << std::flush;
  }
}

void usage(const char *prog) {
  std::cerr << "Usage:\n"
            << "  Generate keys: ./gen_keys.sh\n"
            << "  Server: " << prog << " server <port> <cert_file> <key_file>\n"
            << "  Client: " << prog << " client <host> <port> <ca_cert_file>\n";
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
    run_tcp_server(std::atoi(argv[2]), argv[3], argv[4]);
  } else if (mode == "client") {
    if (argc != 5)
      usage(argv[0]);
    run_tcp_client(argv[2], std::atoi(argv[3]), argv[4]);
  } else {
    usage(argv[0]);
  }

  EVP_cleanup();
  ERR_free_strings();
}