### Server in ```C```
Simple TLS server and client in C using openssl library.

List of content:
* server implementation without TLS,
* server implementation with TLS,
* client implementation with TLS.

Technical notes:
* main process waits for new connections and when one comes it forks to serve response,
* client forks to listener and sender.

To test server without TLS use: ```nc localhost 7777 -v```.

To test server with TLS use: ```openssl s_client -connect localhost:7777```.
