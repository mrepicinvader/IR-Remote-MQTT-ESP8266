#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#endif
#include "ESP8266WiFi.h"
#include <chrono>
#include <thread>
#include <cstdio>
HostESP ESP;
HostWiFi WiFi;
unsigned long millis() {
  static const auto start=std::chrono::steady_clock::now();
  return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count());
}
void yield() { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
namespace {
#ifdef _WIN32
using Socket=SOCKET;
void closeSocket(Socket fd) { closesocket(fd); }
void initialize() { static bool initialized=[] { WSADATA data{}; return WSAStartup(MAKEWORD(2,2),&data)==0; }(); (void)initialized; }
#else
using Socket=int;
void closeSocket(Socket fd) { close(fd); }
void initialize() {}
#endif
bool waitSocket(Socket fd,bool writing,uint32_t milliseconds) {
  fd_set set; FD_ZERO(&set); FD_SET(fd,&set);
  timeval timeout{}; timeout.tv_sec=milliseconds/1000; timeout.tv_usec=(milliseconds%1000)*1000;
  return select(static_cast<int>(fd)+1,writing?nullptr:&set,writing?&set:nullptr,nullptr,&timeout)>0;
}
}
int WiFiClient::connect(const char* host,uint16_t port) {
  stop(); initialize(); char service[8]; std::snprintf(service,sizeof(service),"%u",port);
  addrinfo hints{}; hints.ai_family=AF_INET; hints.ai_socktype=SOCK_STREAM; addrinfo* addresses=nullptr;
  if (getaddrinfo(host,service,&hints,&addresses)!=0) return 0;
  for (auto* address=addresses;address;address=address->ai_next) {
    Socket fd=::socket(address->ai_family,address->ai_socktype,address->ai_protocol);
    if (fd==static_cast<Socket>(-1)) continue;
#ifdef _WIN32
    u_long nonblocking=1; ioctlsocket(fd,FIONBIO,&nonblocking);
#else
    fcntl(fd,F_SETFL,O_NONBLOCK);
#endif
    int result=::connect(fd,address->ai_addr,static_cast<int>(address->ai_addrlen));
    if (result!=0 && waitSocket(fd,true,timeout_)) {
      int error=0;
#ifdef _WIN32
      int length=sizeof(error); getsockopt(fd,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&error),&length);
#else
      socklen_t length=sizeof(error); getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&length);
#endif
      result=error? -1:0;
    }
    if (!result) { socket_=static_cast<intptr_t>(fd); freeaddrinfo(addresses); return 1; }
    closeSocket(fd);
  }
  freeaddrinfo(addresses); return 0;
}
int WiFiClient::connect(IPAddress ip,uint16_t port) {
  char address[16]; std::snprintf(address,sizeof(address),"%u.%u.%u.%u",ip[0],ip[1],ip[2],ip[3]); return connect(address,port);
}
int WiFiClient::available() {
  if (socket_==-1) return 0;
#ifdef _WIN32
  u_long count=0; return ioctlsocket(static_cast<Socket>(socket_),FIONREAD,&count)==0?static_cast<int>(count):0;
#else
  int count=0; return ioctl(static_cast<Socket>(socket_),FIONREAD,&count)==0?count:0;
#endif
}
int WiFiClient::read() {
  unsigned char value=0;
  return socket_!=-1 && recv(static_cast<Socket>(socket_),reinterpret_cast<char*>(&value),1,0)==1?value:-1;
}
int WiFiClient::peek() {
  unsigned char value=0;
  return socket_!=-1 && recv(static_cast<Socket>(socket_),reinterpret_cast<char*>(&value),1,MSG_PEEK)==1?value:-1;
}
void WiFiClient::stop() { if (socket_!=-1) { closeSocket(static_cast<Socket>(socket_)); socket_=-1; } }
uint8_t WiFiClient::connected() {
  if (socket_==-1) return false;
  if (!waitSocket(static_cast<Socket>(socket_),false,0)) return true;
  return peek()!=-1;
}
size_t WiFiClient::write(const uint8_t* bytes,size_t length) {
  size_t count=0;
  while (socket_!=-1 && count<length) {
    if (!waitSocket(static_cast<Socket>(socket_),true,timeout_)) break;
    const int sent=send(static_cast<Socket>(socket_),reinterpret_cast<const char*>(bytes+count),static_cast<int>(length-count),0);
    if (sent<=0) { stop(); break; } count+=sent;
  }
  return count;
}
