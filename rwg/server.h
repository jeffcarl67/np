#ifndef SERVER_H
#define SERVER_H

#include <memory>
#include <string>

class CommandManager;

class Server
{
public:
    Server(int port): port(port) {}
    void run();

private:
    int port;
};

class Client
{
public:
    Client(int fd): Client(fd, 0) {}
    Client(int fd, int uid);
    ~Client();
    std::shared_ptr<CommandManager> command_manager() { return cmdmgr; }
    int fd() { return fd_; }
    bool ok() { return ok_; }
    std::string read();
    void write(const std::string& buf);
    void close();
    int uid() { return uid_; }
    std::string name() { return name_; }
    void set_name(const std::string& name) { name_ = name; }
    std::string ip() { return ip_; }
    void set_ip(const std::string ip) { ip_ = ip; }
    uint16_t port() { return port_; }
    void set_port(uint16_t port) { port_ = port; }

private:
    std::shared_ptr<CommandManager> cmdmgr;
    int fd_;
    bool ok_;
    bool fd_opened;
    char buf[15001];
    int uid_;
    std::string name_;
    std::string ip_;
    uint16_t port_;
};

#endif