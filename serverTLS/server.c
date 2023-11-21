#include <openssl/ssl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <errno.h>
void handle_sigchld(int sig) {
	int saved_errno = errno;
	while (waitpid((pid_t)(-1), 0, WNOHANG) > 0) {}
	errno = saved_errno;
}
int create_socket(int port) {
	int sock; /* socket is a file descriptor */
	struct sockaddr_in addr; /* cast in bind on generic struct sockaddr */
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY); /* 0.0.0.0 */
	if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		errx(EXIT_FAILURE, "Unable to create socket");
	if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0)
		errx(EXIT_FAILURE, "Unable to bind");
	if (listen(sock, 1) < 0)
		errx(EXIT_FAILURE, "Unable to listen");
	return sock;
}
SSL_CTX *create_context() {
	const SSL_METHOD *method = TLS_server_method();
	SSL_CTX *ctx = SSL_CTX_new(method);
	if (ctx == NULL) {
		perror("Unable to create SSL context");
		ERR_print_errors_fp(stderr);
		exit(EXIT_FAILURE);
	}
	return ctx;
}
void configure_context(SSL_CTX *ctx) {
	if (SSL_CTX_use_certificate_file(ctx, "cert.pem", SSL_FILETYPE_PEM) <= 0) {
		ERR_print_errors_fp(stderr);
		exit(EXIT_FAILURE);
	}
	if (SSL_CTX_use_PrivateKey_file(ctx, "key.pem", SSL_FILETYPE_PEM) <= 0) {
		ERR_print_errors_fp(stderr);
		exit(EXIT_FAILURE);
	}
}
#define PROPREQSIZE 65535
#define BUFSIZE 66000
char buff[BUFSIZE];
char req[BUFSIZE];
void die_with_error(SSL *ssl, int client){
	const char ERMSG[] = "Error occured\r\n";
	SSL_write(ssl, ERMSG, strlen(ERMSG));
	SSL_shutdown(ssl);
	SSL_free(ssl);
	close(client);
	exit(1);
}
int check(char *T, int len){
	if (len<2)	return 0;
	return T[len-2]=='\r' && T[len-1]=='\n' && T[len]==0;
}
int fixFormat(char* T, int len){
	if (len==1 || T[len-2]!='\r'){
		T[len-1]='\r', T[len]='\n', T[len+1]=0, len++;
	}
	return len;
}
void make_response(SSL *ssl, int client, char *R, int len){
	len = fixFormat(R, len);
	uint32_t sz = len-2;
	for (int i=0; i<sz/2; i++){
		char tmp = R[i];
		R[i] = R[sz-i-1];
		R[sz-i-1] = tmp;
	}
	if (!check(R, len)){
		printf("Serverside error in format found!\n");
		die_with_error(ssl, client);
	}
	SSL_write(ssl, R, len); /* ! */
}
int main(int argc, char *argv[]) {
	struct sigaction sa;
	sa.sa_handler = &handle_sigchld;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
	if (sigaction(SIGCHLD, &sa, 0) == -1) {
		perror(0);
		exit(1);
	}
	int sock, client;
	SSL_CTX *ctx;
	ctx = create_context();
	configure_context(ctx);
	sock = create_socket(7777); /* listening socket, not involved in SSL */
	while (1) {
		struct sockaddr_in addr;
		socklen_t len = sizeof(addr);
		SSL *ssl;
		if ((client = accept(sock, (struct sockaddr*)&addr, &len)) < 0)
			errx(EXIT_FAILURE, "Unable to accept");
		ssl = SSL_new(ctx); /* create new SSL structure */
		SSL_set_fd(ssl, client); /* wrap connected socket with SSL */
		if (SSL_accept(ssl) <= 0){ /* wait for client to set up SSL connection */
			ERR_print_errors_fp(stderr); /* if screwed up, diagnose the failure */
		} else {
			pid_t PID = fork();
			if (PID < 0)
				perror("Fork error!");
			if (PID == 0) {
				printf("Child[%d] work start\n", getpid());
				int served = 0;
				while (1) {
					uint32_t len = SSL_read(ssl, buff, BUFSIZE);
					buff[len] = 0;
					if (len==0)
						break;
					if (!(1<=len && len<PROPREQSIZE)){
						printf("Incorrect request length!\n");
						printf("Recived: %d, which is not in range [%d, %d]!\n", len, 1, PROPREQSIZE);
						die_with_error(ssl, client);
					}
					if (buff[len-1]!='\n'){
						printf("Incorrect request format!\n");
						printf("Request doesn`t end with \\n!\n");
						die_with_error(ssl, client);
					}
					int j = 0;
					for (int i=0; i<len; i++){
						req[j++] = buff[i];
						if (buff[i]=='\n'){
							req[j]=0;
							make_response(ssl, client, req, j);
							served++;
							j=0;
						}
					}
				}
				printf("Child[%d] work done served responses: %d\n", getpid(), served);
				SSL_shutdown(ssl); /* close SSL connection */
				SSL_free(ssl); /* recover some memory */
				close(client);
				exit(0);
			}
		}
		close(client);
	}
	close(sock);
	SSL_CTX_free(ctx);
}
