#include <sys/socket.h>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include "server.h"
#include "command.h"
#include "process.h"

void Server::run()
{
    int ret, fd; 
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        perror("socket failed");
        return;
    }

    int optval = 1;
    ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    if (ret == -1) {
        perror("setsockopt failed");
        close(fd);
        return;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = PF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    ret = bind(fd, reinterpret_cast<sockaddr *>(&server_addr), sizeof(server_addr));
    if (ret == -1) {
        perror("bind failed");
        close(fd);
        return;
    }
    ret = listen(fd, 30);
    if (ret == -1) {
        perror("listen failed");
        close(fd);
        return;
    }

    while (true) {
        int client_fd = accept(fd, nullptr, nullptr);
        if (client_fd == -1) {
            perror("accept failed");
            close(fd);
            return;
        }
        pid_t pid = fork();
        if (pid == -1) {
            perror("fork failed");
            close(fd);
            return;
        }
        else if (pid == 0) {
            close(fd);
            Client client(client_fd);
            while (client.ok()) {
                client.write("% ");
                if (not client.ok()) break;
                auto line = client.read();
                if (not client.ok()) break;
                auto cmdmgr = client.command_manager();
                auto cmd = cmdmgr->parse(line);
                cmd->exec();
                cmd->wait();
            }
            break;
        }
        else {
            close(client_fd);
        }
    }
}

Client::Client(int fd, int uid):
cmdmgr(std::make_shared<CommandManager>(this)), fd_(fd), ok_(true), fd_opened(true), buf{0}, uid_(uid),
name_("(no name)"), ip_(), port_()
{

}

Client::~Client()
{
    if (fd_opened) ::close(fd_);
}

std::string Client::read()
{
    if (not ok_) return std::string();
    int ret = ::read(fd_, buf, sizeof(buf));
    if (ret == 0) {
        ok_ = false;
        return std::string();
    }
    else if (ret == -1) {
        ok_ = false;
        perror("read failed");
        return std::string();
    }
    else {
        buf[ret] = 0;
        if (ret >= 1 and buf[ret-1] == '\n')
            buf[ret-1] = 0;
        if (ret >= 2 and buf[ret-2] == '\r')
            buf[ret-2] = 0;
    }
    return std::string(buf);
}

void Client::write(const std::string& str)
{
    int ret, n = 0;
    size_t len = str.length();
    const char *buf = str.c_str();
    if (not ok_) return;
    while (n < len) {
        ret = ::write(fd_, buf + n, len - n);
        if (ret == -1) {
            ok_ = false;
            perror("write failed");
            return;
        }
        else {
            n += ret;
        }
    }
}

void Client::close()
{
    ok_ = false;
}

void get_user_pipe_in_fd_fifo(Process *p, int uid, int *fd, std::string *fifo)
{

}

void get_user_pipe_out_fd_fifo(Process *p, int uid, int *fd, std::string *fifo)
{

}

void handle_used_fifo(pid_t pid, const std::string& fifo)
{
    
}

void sigchild_handler(int signo)
{
    pid_t pid;
    auto proc_mgr = ProcessManager::global();
    while ((pid = waitpid(-1, nullptr, WNOHANG)) > 0) {
        proc_mgr->process_died(pid);
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) return 1;
    signal(SIGCHLD, sigchild_handler);

    setenv("PATH", "bin:.", 1);
    int port = std::stoi(argv[1]);
    Server server(port);
    server.run();
}