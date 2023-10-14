#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <sys/wait.h>
#include <signal.h>
#include <sys/shm.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include "server.h"
#include "command.h"
#include "process.h"
#include "rwg.h"

class Current
{
public:
    static Current *global();
    void set(Client *c) { ptr = c; }
    Client *get() { return ptr; }

private:
    Current(): ptr() {}
    Client *ptr;
};

Current *Current::global()
{
    static Current cur;
    return &cur;
}

class Message
{
public:
    static Message *global();
    void set_shm(void *buf);
    char *get_shm() { return ptr; }
    std::string get() { return ptr; }
    void write(const std::string& msg);
    void set_id(int id) { shmid = id; }
    int get_id() { return shmid; }

private:
    Message(): ptr(), shmid() {}
    char *ptr;
    int shmid;
};

Message *Message::global()
{
    static Message msg;
    return &msg;
}

void Message::set_shm(void *buf)
{
    ptr = static_cast<char *>(buf);
}

void Message::write(const std::string& msg)
{
    strncpy(ptr, msg.c_str(), 4095);
    ptr[4095] = 0;
}

struct ClientInfo
{
    bool valid;
    int uid;
    pid_t pid;
    char name[32];
    char ip[16];
    uint16_t port;
};

class ClientInfoManager
{
public:
    static ClientInfoManager *global();
    void set_shm(void *buf);
    ClientInfo *get_shm() { return ptr; }
    void set(ClientInfo ci);
    void set_name(int uid, const std::string& name);
    int get_uid();
    void release_uid(int uid);
    void add(pid_t pid, int uid);
    void process_died(pid_t pid);
    void broadcast(const std::string& m);
    std::string who(int uid);
    void tell(int uid, const std::string& m);
    bool rename(int uid, const std::string& name);
    bool user_exist(int uid);
    std::string get_name(int uid);
    void set_id(int id) { shmid = id; }
    int get_id() { return shmid; }

private:
    ClientInfoManager(): ptr(), uids{false}, pid2uid(), shmid() {}
    ClientInfo *ptr;
    bool uids[31];
    std::unordered_map<pid_t, int> pid2uid;
    int shmid;
};

ClientInfoManager *ClientInfoManager::global()
{
    static ClientInfoManager cim;
    return &cim;
}

void ClientInfoManager::set_shm(void *buf)
{
    ptr = static_cast<ClientInfo *>(buf);
    for (int i = 0; i < 31; i++) {
        ptr[i].valid = false;
        ptr[i].uid = i;
    }
}

void ClientInfoManager::set(ClientInfo ci)
{
    if (ci.uid >= 1 and ci.uid <= 30) {
        ptr[ci.uid] = ci;
    }
}

void ClientInfoManager::set_name(int uid, const std::string& name)
{
    if (uid >=1 and uid <= 30) {
        strncpy(ptr[uid].name, name.c_str(), 31);
        ptr[uid].name[31] = 0;
    }
}

void ClientInfoManager::add(pid_t pid, int uid)
{
    pid2uid[pid] = uid;
}

int ClientInfoManager::get_uid()
{
    int uid = 0;
    for (int i = 1; i <= 30; i++) {
        if (uids[i] == false) {
            uid = i;
            uids[i] = true;
            break;
        }
    }
    return uid;
}

void ClientInfoManager::process_died(pid_t pid)
{
    int uid = pid2uid[pid];
    uids[uid] = false;
    pid2uid.erase(pid);
    ptr[uid].valid = false;
}

void ClientInfoManager::broadcast(const std::string& m)
{
    auto msg = Message::global();
    auto clients = ptr;
    msg->write(m);
    for (int i = 1; i <= 30; i++) {
        if (clients[i].valid) {
            kill(clients[i].pid, SIGRTMIN);
        }
    }
}

std::string ClientInfoManager::who(int uid)
{
    auto clients = ptr;
    std::string msg = "<ID>\t<nickname>\t<IP:port>\t<indicate me>\n";
    for (int i = 1; i <= 30; i++) {
        if (clients[i].valid == false) continue;
        msg += std::to_string(clients[i].uid) + "\t" + clients[i].name + "\t" +
               clients[i].ip + ":" + std::to_string(clients[i].port);
        if (i == uid)
            msg += "\t<-me";
        msg += "\n";
    }
    return msg;
}

void ClientInfoManager::tell(int uid, const std::string& m)
{
    auto client = Current::global()->get();
    auto msg = Message::global();
    auto clients = ptr;
    if (uid < 1 or uid > 30) return;
    if (clients[uid].valid) {
        std::string s = "*** " + client->name() + " told you ***: " +
                        m + "\n";
        msg->write(s);
        kill(clients[uid].pid, SIGRTMIN);
    }
    else {
        std::string s = "*** Error: user #" + std::to_string(uid) +
                          " does not exist yet. ***\n";
        client->write(s);
    }
}

bool ClientInfoManager::rename(int uid, const std::string& name)
{
    auto client = Current::global()->get();
    auto clients = ptr;
    if (uid < 1 or uid > 30) return false;
    bool exist = false;
    for (int i = 1; i <= 30; i++) {
        if (clients[i].valid and strncmp(name.c_str(), clients[i].name, 31) == 0) {
            exist = true;
            break;
        }
    }
    if (exist)
        return false;
    strncpy(clients[uid].name, name.c_str(), 31);
    clients[uid].name[31] = 0;
    client->set_name(name);
    return true;
}

bool ClientInfoManager::user_exist(int uid)
{
    if (uid < 1 or uid > 30) return false;
    return ptr[uid].valid;
}

std::string ClientInfoManager::get_name(int uid)
{
    if (uid < 1 or uid > 30) return std::string();
    if (ptr[uid].valid)
        return ptr[uid].name;
    return std::string();
}

class FIFO
{
public:
    static FIFO *global();
    void add(pid_t pid, const std::string& fifo);
    void release(pid_t pid);
    void cleanup(int uid);

private:
    FIFO(): pid2fifo() {}
    std::unordered_map<pid_t, std::string> pid2fifo;
};

FIFO *FIFO::global()
{
    static FIFO fifo;
    return &fifo;
}

void FIFO::add(pid_t pid, const std::string& fifo)
{
    pid2fifo[pid] = fifo;
}

void FIFO::release(pid_t pid)
{
    auto path = pid2fifo[pid];
    if (path.empty()) return;
    unlink(path.c_str());
    pid2fifo.erase(pid);
}

void FIFO::cleanup(int uid)
{
    std::string dir = "user_pipe/";
    std::vector<std::string> fifos;
    DIR *dirp;
    struct dirent *entry;
    dirp = opendir(dir.c_str());
    if (dirp == nullptr) return;
    while (entry = readdir(dirp)) {
        std::string fifo = entry->d_name;
        if (fifo.rfind("fifo", 0) != 0) continue; // not start with 'fifo'
        std::string tmp;
        int i = 4;
        while (i < fifo.length() and std::isdigit(fifo[i])) {
            tmp += fifo[i];
            i += 1;
        }
        int uid1 = std::stoi(tmp);
        i += 1;
        tmp.clear();
        while (i < fifo.length() and std::isdigit(fifo[i])) {
            tmp += fifo[i];
            i += 1;
        }
        int uid2 = std::stoi(tmp);
        if (uid1 == uid or uid2 == uid)
            fifos.push_back(dir + fifo);
    }
    closedir(dirp);
    for (const auto& path : fifos) {
        int fd = open(path.c_str(), O_RDONLY);
        if (fd >= 0) close(fd); 
        unlink(path.c_str());
    }
}

void sigchild_handler(int signo)
{
    pid_t pid;
    auto proc_mgr = ProcessManager::global();
    auto f = FIFO::global();
    while ((pid = waitpid(-1, nullptr, WNOHANG)) > 0) {
        proc_mgr->process_died(pid);
        f->release(pid);
    }
}

void sigrtmin_handler(int signo)
{
    auto client = Current::global()->get();
    auto msg = Message::global();
    if (client == nullptr) return;
    client->write(msg->get());
}

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

    key_t key1 = 8086;
    int id1 = shmget(key1, sizeof(ClientInfo) * 31, IPC_CREAT | 0666);
    if (id1 < 0) {
        perror("shmget failed");
        close(fd);
        return;
    }

    void *buf1;
    buf1 = shmat(id1, nullptr, 0);
    if (buf1 == (void *) -1) {
        perror("shmat failed");
        close(fd);
        return;
    }

    key_t key2 = 8087;
    int id2 = shmget(key2, 4096, IPC_CREAT | 0666);
    if (id2 < 0) {
        perror("shmget failed");
        close(fd);
        return;
    }

    void *buf2;
    buf2 = shmat(id2, nullptr, 0);
    if (buf2 == (void *) -1) {
        perror("shmat failed");
        close(fd);
        return;
    }

    auto cimgr = ClientInfoManager::global();
    cimgr->set_shm(buf1);
    cimgr->set_id(id1);

    auto msg = Message::global();
    msg->set_shm(buf2);
    msg->set_id(id2);

    while (true) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(fd, reinterpret_cast<sockaddr *>(&client_addr), &addr_len);
        if (client_fd == -1) {
            perror("accept failed");
            close(fd);
            return;
        }

        uint16_t port = ntohs(client_addr.sin_port);
        char ip[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &(client_addr.sin_addr), ip, INET_ADDRSTRLEN);
        int uid = cimgr->get_uid();

        pid_t pid = fork();
        if (pid == -1) {
            perror("fork failed");
            close(fd);
            return;
        }
        else if (pid == 0) {
            signal(SIGCHLD, sigchild_handler);
            signal(SIGRTMIN, sigrtmin_handler);
            signal(SIGINT, SIG_DFL);
            close(fd);
            Client client(client_fd, uid);
            Current::global()->set(&client);

            client.set_ip(ip);
            client.set_port(port);

            ClientInfo ci;
            ci.valid = true;
            ci.uid = uid;
            ci.pid = getpid();
            strncpy(ci.name, "(no name)", 31);
            ci.name[31] = 0;
            strncpy(ci.ip, ip, 16);
            ci.ip[15] = 0;
            ci.port = port;
            cimgr->set(ci);

            if (client.ok()) {
                std::string welcome = 
                "****************************************\n"
                "** Welcome to the information server. **\n"
                "****************************************\n";
                std::string login = "*** User '" + client.name() + "' entered from " +
                                    client.ip() + ":" + std::to_string(client.port()) + ". ***\n";
                client.write(welcome);
                cimgr->broadcast(login);
            }

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
            shmdt(buf1);
            shmdt(buf2);
            Current::global()->set(nullptr);
            break;
        }
        else {
            close(client_fd);
            cimgr->add(pid, uid);
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
    setenv(var.c_str(), value.c_str(), 1);
}

void Exit::exec()
{
    auto cimgr = ClientInfoManager::global();
    auto f = FIFO::global();
    std::string logout = "*** User '" + client_->name() + "' left. ***\n";
    client_->close();
    cimgr->broadcast(logout);
    f->cleanup(client_->uid());
}

void Who::exec()
{
    auto cimgr = ClientInfoManager::global();
    auto msg = cimgr->who(client_->uid());
    client_->write(msg);
}

void Tell::exec()
{
    auto cimgr = ClientInfoManager::global();
    cimgr->tell(uid, msg);
}

void Yell::exec()
{
    auto cimgr = ClientInfoManager::global();
    std::string m = "*** " + client_->name() + " yelled ***: " + msg + "\n";
    cimgr->broadcast(m);
}

void Name::exec()
{
    auto cimgr = ClientInfoManager::global();
    auto success = cimgr->rename(client_->uid(), name);
    if (success) {
        std::string m = "*** User from " + client_->ip() + ":" +
                        std::to_string(client_->port()) + " is named '" +
                        name + "'. ***\n";
        cimgr->broadcast(m);
    }
    else {
        std::string m = "*** User '" + name + "' already exists. ***\n";
        client_->write(m);
    }
}

void get_user_pipe_in_fd_fifo(Process *p, int uid, int *fd, std::string *fifo)
{
    auto cimgr = ClientInfoManager::global();
    auto client = p->command()->client();
    auto self_uid = client->uid();
    std::string fifo_path = "user_pipe/fifo" + std::to_string(uid) + "_" + std::to_string(self_uid);
    auto exist = cimgr->user_exist(uid);
    if (not exist) { // no such user
        std::string m = "*** Error: user #" + std::to_string(uid) +
                        " does not exist yet. ***\n";
        *fifo = "/dev/null";
        client->write(m);
        return;
    }
    if (access(fifo_path.c_str(), R_OK) == -1) { // fifo not exist
        std::string m = "*** Error: the pipe #" + std::to_string(uid) + "->#" +
                        std::to_string(self_uid) + " does not exist yet. ***\n";
        *fifo = "/dev/null";
        client->write(m);
        return;
    }
    auto sender_name = cimgr->get_name(uid);
    std::string cmdline = p->command()->get_cmdline();
    std::string m = "*** " + client->name() + " (#" + std::to_string(client->uid()) +
                    ") just received from " + sender_name + " (#" +
                    std::to_string(uid) + ") by '" + cmdline + "' ***\n";
    cimgr->broadcast(m);
    *fifo = fifo_path;
}

void get_user_pipe_out_fd_fifo(Process *p, int uid, int *fd, std::string *fifo)
{
    auto cimgr = ClientInfoManager::global();
    auto client = p->command()->client();
    auto self_uid = client->uid();
    std::string fifo_path = "user_pipe/fifo" + std::to_string(self_uid) + "_" + std::to_string(uid);
    auto exist = cimgr->user_exist(uid);
    if (not exist) {
        std::string m = "*** Error: user #" + std::to_string(uid) +
                        " does not exist yet. ***\n";
        *fifo = "/dev/null";
        client->write(m);
        return;
    }
    int ret = mkfifo(fifo_path.c_str(), 0666);
    if (ret == -1) {
        if (errno == EEXIST) { // fifo exist
            std::string m = "*** Error: the pipe #" + std::to_string(self_uid) + "->#" +
                            std::to_string(uid) + " already exists. ***\n";
            client->write(m);
        }
        else
            perror("mkfifo failed");
        *fifo = "/dev/null";
        return;
    }
    auto receiver_name = cimgr->get_name(uid);
    std::string cmdline = p->command()->get_cmdline();
    std::string m = "*** " + client->name() + " (#" + std::to_string(client->uid()) + 
                    ") just piped '" + cmdline + "' to " + receiver_name +
                    " (#" + std::to_string(uid) + ") ***\n";
    cimgr->broadcast(m);
    *fifo = fifo_path;
}

void handle_used_fifo(pid_t pid, const std::string& fifo)
{
    auto f = FIFO::global();
    f->add(pid, fifo);
}

void client_handler(int signo)
{
    pid_t pid;
    auto ci_mgr = ClientInfoManager::global();
    while ((pid = waitpid(-1, nullptr, WNOHANG)) > 0) {
        ci_mgr->process_died(pid);
    }
}

void sigint_handler(int signo)
{
    auto cimgr = ClientInfoManager::global();
    auto msg = Message::global();
    shmctl(cimgr->get_id(), IPC_RMID, nullptr);
    shmctl(msg->get_id(), IPC_RMID, nullptr);
    exit(0);
}

int main(int argc, char **argv)
{
    if (argc < 2) return 1;
    signal(SIGCHLD, client_handler);
    signal(SIGINT, sigint_handler);

    setenv("PATH", "bin:.", 1);
    int port = std::stoi(argv[1]);
    Server server(port);
    server.run();
}