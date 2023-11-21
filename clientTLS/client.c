#include <readline/readline.h>
#include <readline/history.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>


#define PROPREQSIZE 65535
#define BUFSIZE 66000

SSL_CTX *init_ssl_context(const char *ca_certificate){
	SSL_library_init();
	SSL_load_error_strings();
	OpenSSL_add_all_algorithms();
	const SSL_METHOD *method = TLS_client_method();
	SSL_CTX *ctx = SSL_CTX_new(method);
	if(!ctx){
		perror("Error creating SSL context.");
		exit(1);
	}
	if (SSL_CTX_load_verify_locations(ctx, ca_certificate, NULL) != 1){
		perror("Error loading CA file.");
		exit(1);
	}
	return ctx;
}

SSL *create_ssl_connection(int sock, SSL_CTX *ctx){
	SSL *ssl = SSL_new(ctx);
	if(!ssl){
		perror("Error creating SSL connection.");
		exit(1);
	}
	SSL_set_fd(ssl, sock);
	if(SSL_connect(ssl) != 1){
		perror("Error establishing SSL connection.");
		exit(1);
	}
	return ssl;
}
char buff[BUFSIZE];
int main(int argc, char* argv[]){
	if (argc!=4){
		printf("usage: %s cert_file ip_addres port\n", argv[0]);
		exit(1);
	}
	const char* cafile = argv[1]; //"cert.pem";
	const char* ip = argv[2]; //"127.0.0.1";
	int port = atoi(argv[3]); //7777;

	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == -1){
		perror("Error creating socket.\n");
		exit(1);
	}
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(ip);
	addr.sin_port = htons(port);
	if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == -1){
		perror("Error connecting to server.\n");
		exit(1);
	}
	SSL_CTX *ssl_ctx = init_ssl_context(cafile);
	SSL *ssl = create_ssl_connection(sock, ssl_ctx);

	pid_t PID = fork();
	if (PID<0){
		perror("Fork error");
		exit(1);
	}
	if (PID==0){
		int cnt=0;
		while (1){
			int len = SSL_read(ssl, buff, BUFSIZE);
			buff[len] = 0;
			printf("[%d]: %s", cnt, buff); cnt++;
		}
	} else {
		int cnt=0;
		while (1){
			char *req = readline("");
			if (strlen(req)==0){
				SSL_write(ssl, req, strlen(req));
				free(req);
				break;
			}
			int len = strlen(req);
			for (int i=0; i<len; i++)
				buff[i] = req[i];
			free(req);
			buff[len++]='\r', buff[len++]='\n', buff[len]=0;
			SSL_write(ssl, buff, strlen(buff));
			sleep(1);
		}
	}

	kill(PID, SIGTERM);
	int status;
	waitpid(PID, &status, 0);

	SSL_shutdown(ssl);
	SSL_free(ssl);
	SSL_CTX_free(ssl_ctx);
	close(sock);
	exit(0);
}
