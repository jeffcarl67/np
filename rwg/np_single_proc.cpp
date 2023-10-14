#include <sys/socket.h>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/select.h>
#include <set>
#include <arpa/inet.h>
#include <utility>
#include <map>
#include <fcntl.h>
#include "server.h"
#include "command.h"
#include "process.h"
#include "rwg.h"

class ClientManager
{
public:
    static ClientManager *global();
    std::shared_ptr<Client> new_client(int fd);
    std::shared_ptr<Client> get_client(int fd);
    std::shared_ptr<Client> get_client_by_uid(int uid);
    void delete_client(int uid);
    void broadcast(const std::string& msg);
    const std::unordered_map<int, std::shared_ptr<Client> >& users();

private:
    ClientManager(): uids{false}, uid2client(), fd2uid() {}
    int get_uid();
    bool uids[31]; // 0 not used
    std::unordered_map<int, std::shared_ptr<Client> > uid2client;
    std::unordered_map<int, int> fd2uid;
};

ClientManager *ClientManager::global()
{
    static ClientManager client_mgr;
    return &client_mgr;
}

std::shared_ptr<Client> ClientManager::get_client(int fd)
{
    int uid = fd2uid[fd];
    return uid2client[uid];
}

std::shared_ptr<Client> ClientManager::get_client_by_uid(int uid)
{
    auto it = uid2client.find(uid);
    if (it == uid2client.end())
        return std::shared_ptr<Client>();
    return it->second;
}

int ClientManager::get_uid()
{
    int i;
    for (i = 1; i <= 30; i++)
        if (uids[i] == false) {
            uids[i] = true;
            return i;
        }
    return 0;
}

const std::unordered_map<int, std::shared_ptr<Client> >& ClientManager::users()
{
    return uid2client;
}

class AFDS
{
public:
    static AFDS *global();
    fd_set afds() { return afds_; }
    std::set<int> fds() { return fds_; }
    void set(int fd);
    void clr(int fd);
    int max();

private:
    AFDS(): afds_(), fds_() { FD_ZERO(&afds_); }
    fd_set afds_;
    std::set<int> fds_;
};

AFDS *AFDS::global()
{
    static AFDS afds;
    return &afds;
}

void AFDS::set(int fd)
{
    FD_SET(fd, &afds_);
    fds_.insert(fd);
}

void AFDS::clr(int fd)
{
    FD_CLR(fd, &afds_);
    fds_.erase(fd);
}

int AFDS::max()
{
    if (not fds_.empty())
        return *(fds_.rbegin()) + 1;
    return 0;
}

class Environment
{
public:
    static Environment *global();
    void new_env(int uid);
    void delete_env(int uid);
    void rollback(int uid);
    void switch_env(int uid);
    void set(int uid, const std::string& key, const std::string& value);

private:
    Environment() {}
    std::unordered_map<std::string, std::string> origin;
    std::unordered_map<int, std::unordered_map<std::string, std::string> > uid2env;
};

Environment *Environment::global()
{
    static Environment env;
    return &env;
}

void Environment::new_env(int uid)
{
    uid2env[uid] = std::unordered_map<std::string, std::string>();
}

void Environment::delete_env(int uid)
{
    uid2env.erase(uid);
}

void Environment::rollback(int uid)
{
    const auto& env = uid2env[uid];
    for (const auto& p : env) {
        const auto& key = p.first;
        const auto& value = p.second;
        if (origin.count(key) > 0)
            setenv(key.c_str(), origin[key].c_str(), 1);
        else
            unsetenv(key.c_str());
    }
}

void Environment::switch_env(int uid)
{
    const auto& env = uid2env[uid];
    for (const auto& p : env) {
        const auto& key = p.first;
        const auto& value = p.second;
        setenv(key.c_str(), value.c_str(), 1);
    }
}

void Environment::set(int uid, const std::string& key, const std::string& value)
{
    auto& env = uid2env[uid];
    auto v = getenv(key.c_str());
    if (v and origin.count(key) == 0 and env.count(key) == 0)
        origin[key] = v;
    env[key] = value;
    setenv(key.c_str(), value.c_str(), 1);
}

void Server::run()
{
    int ret, server_fd; 
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket failed");
        return;
    }

    int optval = 1;
    ret = setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    if (ret == -1) {
        perror("setsockopt failed");
        close(server_fd);
        return;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = PF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    ret = bind(server_fd, reinterpret_cast<sockaddr *>(&server_addr), sizeof(server_addr));
    if (ret == -1) {
        perror("bind failed");
        close(server_fd);
        return;
    }
    ret = listen(server_fd, 30);
    if (ret == -1) {
        perror("listen failed");
        close(server_fd);
        return;
    }

    auto afds = AFDS::global();
    afds->set(server_fd);

    auto client_mgr = ClientManager::global();
    auto env_mgr = Environment::global();

    while (true) {
        fd_set rfds = afds->afds();
        int max = afds->max();
        struct timeval t = {1, 0};
        int ret = select(max, &rfds, nullptr, nullptr, &t);
        if (ret == -1) {
            if (errno == EINTR) continue;
            perror("select failed");
            close(server_fd);
            return;
        }
        else if (ret == 0) {
            continue;
        }
        else {
            for (auto fd : afds->fds()) {
                if (not FD_ISSET(fd, &rfds)) continue;
                if (fd == server_fd) {
                    struct sockaddr_in client_addr;
                    socklen_t addr_len = sizeof(client_addr);
                    int client_fd = accept(fd, reinterpret_cast<sockaddr *>(&client_addr), &addr_len);
                    if (client_fd == -1) {
                        perror("accept failed");
                        close(server_fd);
                        return;
                    }
                    uint16_t port = ntohs(client_addr.sin_port);
                    char ip[INET_ADDRSTRLEN] = {0};
                    inet_ntop(AF_INET, &(client_addr.sin_addr), ip, INET_ADDRSTRLEN);
                    
                    auto client = client_mgr->new_client(client_fd);
                    client->set_ip(ip);
                    client->set_port(port);
                    if (client->ok()) {
                        std::string welcome = 
                        "****************************************\n"
                        "** Welcome to the information server. **\n"
                        "****************************************\n";
                        std::string login = "*** User '" + client->name() + "' entered from " +
                                            client->ip() + ":" + std::to_string(client->port()) + ". ***\n";
                        client->write(welcome);
                        client_mgr->broadcast(login);
                        client->write("% ");
                    }
                }
                else {
                    auto client = client_mgr->get_client(fd);
                    env_mgr->switch_env(client->uid());
                    if (client->ok()) {
                        auto line = client->read();
                        if (not client->ok()) {
                            env_mgr->rollback(client->uid());
                            continue;
                        }
                        auto cmdmgr = client->command_manager();
                        auto cmd = cmdmgr->parse(line);
                        cmd->exec();
                        cmd->wait();
                        client->write("% ");
                    }
                    env_mgr->rollback(client->uid());
                }
            }
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

void Setenv::exec()
{
    auto env_mgr = Environment::global();
    env_mgr->set(client_->uid(), var, value);
}

void Who::exec()
{
    auto client_mgr = ClientManager::global();
    auto users = client_mgr->users();
    std::string msg = "<ID>\t<nickname>\t<IP:port>\t<indicate me>\n";
    for (int id = 1; id <=30; id++) {
        auto it = users.find(id);
        if (it == users.end()) continue;
        auto c = it->second;
        msg += std::to_string(c->uid()) + "\t" + c->name() + "\t" +
               c->ip() + ":" + std::to_string(c->port());
        if (id == client_->uid())
            msg += "\t<-me";
        msg += "\n";
    }
    client_->write(msg);
}

void Tell::exec()
{
    auto client_mgr = ClientManager::global();
    auto users = client_mgr->users();
    auto it = users.find(uid);
    if (it == users.end()) {
        std::string m = "*** Error: user #" + std::to_string(uid) +
                          " does not exist yet. ***\n";
        client_->write(m);
    }
    else {
        auto c = it->second;
        std::string m = "*** " + client_->name() + " told you ***: " +
                        msg + "\n";
        c->write(m);
    }
}

void Yell::exec()
{
    auto client_mgr = ClientManager::global();
    std::string m = "*** " + client_->name() + " yelled ***: " + msg + "\n";
    client_mgr->broadcast(m);
}

void Name::exec()
{
    auto client_mgr = ClientManager::global();
    auto users = client_mgr->users();
    bool exist = false;
    for (const auto& p : users) {
        if (p.second->name() == name) {
            exist = true;
            break;
        }
    }
    if (exist) {
        std::string m = "*** User '" + name + "' already exists. ***\n";
        client_->write(m);
    }
    else {
        std::string m = "*** User from " + client_->ip() + ":" +
                        std::to_string(client_->port()) + " is named '" +
                        name + "'. ***\n";
        client_->set_name(name);
        client_mgr->broadcast(m);
    }
}

std::shared_ptr<Client> ClientManager::new_client(int fd)
{
    int uid = get_uid();
    auto client = std::make_shared<Client>(fd, uid);
    auto afds = AFDS::global();
    auto env_mgr = Environment::global();
    afds->set(fd);
    uid2client[uid] = client;
    fd2uid[fd] = uid;
    env_mgr->new_env(uid);
    return client;
}

void ClientManager::delete_client(int uid)
{
    auto client = uid2client[uid];
    auto afds = AFDS::global();
    auto env_mgr = Environment::global();
    afds->clr(client->fd());
    fd2uid.erase(client->fd());
    uids[uid] = false;
    env_mgr->rollback(uid);
    env_mgr->delete_env(uid);
    uid2client.erase(uid);
}

void ClientManager::broadcast(const std::string& msg)
{
    for (const auto& p : uid2client) {
        p.second->write(msg);
    }
}

class UserPipeManager
{
public:
    static UserPipeManager *global();
    std::map<std::pair<int, int>, Pipe> pipes;

private:
    UserPipeManager(): pipes() {}
};

UserPipeManager *UserPipeManager::global()
{
    static UserPipeManager up_mgr;
    return &up_mgr;
}

void Exit::exec()
{
    auto client_mgr = ClientManager::global();
    auto up_mgr = UserPipeManager::global();
    std::string logout = "*** User '" + client_->name() + "' left. ***\n";
    auto uid = client_->uid();
    auto it = up_mgr->pipes.begin();
    while (it != up_mgr->pipes.end()) {
        if (it->first.first == uid or it->first.second == uid) {
            close(it->second.pipe[0]); // close read fd
            it = up_mgr->pipes.erase(it);
        }
        else {
            ++it;
        }
    }
    client_->close();
    client_mgr->delete_client(client_->uid());
    client_mgr->broadcast(logout);
}

void get_user_pipe_in_fd_fifo(Process *p, int uid, int *fd, std::string *fifo)
{
    auto up_mgr = UserPipeManager::global();
    auto client_mgr = ClientManager::global();
    auto client = p->command()->client();
    auto self_uid = client->uid();
    auto sender = client_mgr->get_client_by_uid(uid);
    if (sender == nullptr) { // no such user
        std::string m = "*** Error: user #" + std::to_string(uid) +
                        " does not exist yet. ***\n";
        int null = open("/dev/null", O_RDONLY);
        if (null == -1)
            perror("open /dev/null failed");
        client->write(m);
        *fd = null;
        return;
    }
    auto it = up_mgr->pipes.find({uid, self_uid});
    if (it == up_mgr->pipes.end()) { // pipe not exist
        std::string m = "*** Error: the pipe #" + std::to_string(uid) + "->#" +
                        std::to_string(self_uid) + " does not exist yet. ***\n";
        int null = open("/dev/null", O_RDONLY);
        if (null == -1)
            perror("open /dev/null failed");
        client->write(m);
        *fd = null;
        return;
    }
    std::string cmdline = p->command()->get_cmdline();
    std::string m = "*** " + client->name() + " (#" + std::to_string(client->uid()) +
                    ") just received from " + sender->name() + " (#" +
                    std::to_string(sender->uid()) + ") by '" + cmdline + "' ***\n";
    client_mgr->broadcast(m);
    *fd = it->second.pipe[0];
    up_mgr->pipes.erase({uid, self_uid});
}

void get_user_pipe_out_fd_fifo(Process *p, int uid, int *fd, std::string *fifo)
{
    auto up_mgr = UserPipeManager::global();
    auto client_mgr = ClientManager::global();
    auto client = p->command()->client();
    auto self_uid = client->uid();
    auto receiver = client_mgr->get_client_by_uid(uid);
    if (receiver == nullptr) { // no such user
        std::string m = "*** Error: user #" + std::to_string(uid) +
                        " does not exist yet. ***\n";
        int null = open("/dev/null", O_WRONLY);
        if (null == -1)
            perror("open /dev/null failed");
        client->write(m);
        *fd = null;
        return;
    }
    auto it = up_mgr->pipes.find({self_uid, uid});
    if (it != up_mgr->pipes.end()) { // pipe exist
        std::string m = "*** Error: the pipe #" + std::to_string(self_uid) + "->#" +
                        std::to_string(uid) + " already exists. ***\n";
        int null = open("/dev/null", O_WRONLY);
        if (null == -1)
            perror("open /dev/null failed");
        client->write(m);
        *fd = null;
        return;
    }
    Pipe up = {-1, -1};
    int ret = pipe(up.pipe);
    if (ret == -1)
        perror("pipe failed");
    up_mgr->pipes[{self_uid, uid}] = up;
    std::string cmdline = p->command()->get_cmdline();
    std::string m = "*** " + client->name() + " (#" + std::to_string(client->uid()) + 
                    ") just piped '" + cmdline + "' to " + receiver->name() +
                    " (#" + std::to_string(receiver->uid()) + ") ***\n";
    client_mgr->broadcast(m);
    *fd = up.pipe[1];
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