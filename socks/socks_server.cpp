#include <iostream>
#include <memory>
#include <boost/asio.hpp>
#include <unistd.h>
#include <fstream>
#include <sys/types.h>
#include <sys/wait.h>

struct FirewallRule
{
    bool is_valid;
    bool is_num[4];
    unsigned char ip[4];
    unsigned char op;

    bool is_number(const std::string& s)
    {
        std::string::const_iterator it = s.begin();
        while (it != s.end() && std::isdigit(*it)) ++it;
        return !s.empty() && it == s.end();
    }

    void parse(const std::string& op_str, const std::string& ip_str) {
        if (op_str == "c")
            op = 1;
        else if (op_str == "b")
            op = 2;
        else {
            op = 0;
            is_valid = false;
            return;
        }
        std::vector<std::string> num;
        size_t pos = 0, n = 0;
        while ((n = ip_str.find('.', pos)) != std::string::npos) {
            num.push_back(ip_str.substr(pos, n - pos));
            pos = n + 1;
        }
        num.push_back(ip_str.substr(pos));

        if (num.size() != 4) {
            is_valid = false;
            return;
        }

        for (unsigned int i = 0; i < num.size(); i++) {
            if (is_number(num[i]))
                is_valid = true;
            else if (num[i] == "*")
                is_valid = true;
            else {
                is_valid = false;
                return;
            }
        }

        for (unsigned int i = 0; i < num.size(); i++) {
            if (is_number(num[i])) {
                is_num[i] = true;
                ip[i] = std::stoul(num[i]);
            }
            else {
                is_num[i] = false;
                ip[i] = 0;
            }
        }
    }

    bool match(unsigned char op1, const std::string ip_str) const {
        auto addr = boost::asio::ip::address::from_string(ip_str).to_v4().to_bytes();
        if (not is_valid)
            return false;

        if (op1 != op)
            return false;

        for (int i = 0; i < 4; i++) {
            if (is_num[i] and addr[i] != ip[i])
                return false;
        }
        return true;
    }
};

class Firewall
{
public:
    Firewall(): conf("socks.conf"), rules() {}
    
    void parse() {
        std::string line;
        std::string permit;
        std::string op;
        std::string ip;
        size_t pos;
        size_t n;

        while (conf) {
            std::getline(conf, line);
            pos = 0;
            n = line.find(' ', pos);
            permit = line.substr(pos, n - pos);
            pos = n + 1;
            n = line.find(' ', pos);
            op = line.substr(pos, n - pos);
            pos = n + 1;
            ip = line.substr(pos);

            if (permit != "permit") {
                continue;
            }
            if (op != "c" and op != "b") {
                continue;
            }

            FirewallRule rule;
            rule.parse(op, ip);
            rules.push_back(rule);
        }
    }
    
    bool check(unsigned char op, const std::string& ip) {
        parse();
        for (const auto& rule : rules) {
            if (rule.match(op, ip))
                return true;
        }
        return false;
    }

private:
    std::ifstream conf;
    std::vector<FirewallRule> rules;
};

class Session : public std::enable_shared_from_this<Session>
{
public:
    Session(boost::asio::ip::tcp::socket socket)
    : socket_(std::move(socket)), socket2_(socket_.get_executor()), data_{}, data2_{} {}

    void start() {
        auto source_ip = socket_.remote_endpoint().address().to_string();
        auto source_port = socket_.remote_endpoint().port();
        unsigned char info[8] = {0};
        boost::system::error_code ec;
        boost::asio::read(socket_, boost::asio::buffer(info, 8), ec);
        if (ec) {
            return;
        }
        auto VN = info[0];
        auto CD = info[1];
        auto DSTPORT = ntohs(*reinterpret_cast<uint16_t *>(info + 2));
        auto DSTIP = boost::asio::ip::address_v4({info[4], info[5], info[6], info[7]}).to_string();
        std::string buf;
        std::string USERID;
        std::stringstream ss;
        ss << "\n";

        if (VN != 4) {
            return;
        }
        if (CD != 1 and CD != 2) {
            return;
        }
        auto n = boost::asio::read_until(socket_, boost::asio::dynamic_buffer(buf), '\0', ec);
        USERID = buf.substr(0, n-1);
        buf.erase(0, n);

        ss << "<S_IP>: " << source_ip << "\n";
        ss << "<S_PORT>: " << source_port << "\n";
        ss << "<D_IP>: " << DSTIP << "\n";
        ss << "<D_PORT>: " << DSTPORT << "\n";
        if (CD == 1)
            ss << "<Command>: CONNECT\n";
        else if (CD == 2)
            ss << "<Command>: BIND\n";
        else;

        if (info[4] == 0 and info[5] == 0 and info[6] == 0 and info[7] != 0) {
            
            auto n = boost::asio::read_until(socket_, boost::asio::dynamic_buffer(buf), '\0', ec);
            auto domain = buf.substr(0, n-1);
            buf.erase(0, n);
            boost::asio::ip::tcp::resolver resolver(socket_.get_executor());
            boost::system::error_code ec;
            auto results = resolver.resolve(domain, std::to_string(DSTPORT), ec);
            if (ec) {
                unsigned char reply[8] = {0};
                reply[1] = 91;

                ss << "<Reply>: Reject\n";
                boost::asio::write(socket_, boost::asio::buffer(reply, 8));
                std::cout << ss.str();
                return;
            } else {
                auto it = results.cbegin();
                if (it == results.cend()) {
                    unsigned char reply[8] = {0};
                    reply[1] = 91;

                    ss << "<Reply>: Reject\n";
                    boost::asio::write(socket_, boost::asio::buffer(reply, 8));
                    std::cout << ss.str();
                    return;
                }
                else {
                    DSTIP = it->endpoint().address().to_string();
                }
            }
        }

        Firewall firewall;
        bool passed = firewall.check(CD, DSTIP);
        if (not passed) {
            unsigned char reply[8] = {0};
            reply[1] = 91;

            ss << "<Reply>: Reject\n";
            boost::asio::write(socket_, boost::asio::buffer(reply, 8));
            std::cout << ss.str();
            return;
        }

        if (CD == 1) {
            boost::asio::ip::tcp::endpoint endpoint(
                boost::asio::ip::address::from_string(DSTIP), DSTPORT);
            boost::system::error_code ec;
            
            socket2_.connect(endpoint, ec);
            if (ec) {
                unsigned char reply[8] = {0};
                reply[1] = 91;

                ss << "<Reply>: Reject\n";
                boost::asio::write(socket_, boost::asio::buffer(reply, 8));
            }
            else {
                unsigned char reply[8] = {0};
                reply[1] = 90;

                ss << "<Reply>: Accept\n";
                boost::asio::write(socket_, boost::asio::buffer(reply, 8));
                if (not buf.empty()) {
                    boost::asio::write(socket2_, boost::asio::buffer(buf.c_str(), buf.length()));
                }
                
                connect(&socket_, &socket2_, data_);
                connect(&socket2_, &socket_, data2_);
            }
        }
        else if (CD == 2) {
            boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::tcp::v4(), 0);
            boost::system::error_code ec;
            boost::asio::ip::tcp::acceptor acceptor(socket_.get_executor(), endpoint);
            
            auto addr = acceptor.local_endpoint().address().to_v4().to_bytes();
            auto port = acceptor.local_endpoint().port();
            unsigned char reply[8] = {0};
            reply[1] = 90;
            reply[2] = port / 256;
            reply[3] = port % 256;
            reply[4] = addr[0];
            reply[5] = addr[1];
            reply[6] = addr[2];
            reply[7] = addr[3];

            ss << "<Reply>: Accept\n";
            boost::asio::write(socket_, boost::asio::buffer(reply, 8));

            acceptor.accept(socket2_, ec);
            if (ec) {
                unsigned char reply[8] = {0};
                reply[1] = 91;

                boost::asio::write(socket_, boost::asio::buffer(reply, 8));
            }
            else {
                if (socket2_.remote_endpoint().address().to_string() != DSTIP) {
                    unsigned char reply[8] = {0};
                    reply[1] = 91;

                    boost::asio::write(socket_, boost::asio::buffer(reply, 8));
                }
                else {
                    unsigned char reply[8] = {0};
                    reply[1] = 90;

                    boost::asio::write(socket_, boost::asio::buffer(reply, 8));
                    if (not buf.empty()) {
                        boost::asio::write(socket2_, boost::asio::buffer(buf.c_str(), buf.length()));
                    }
                
                    connect(&socket_, &socket2_, data_);
                    connect(&socket2_, &socket_, data2_);
                }
            }
        }
        else {

        }
        std::cout << ss.str();
    }

private:
    void connect(boost::asio::ip::tcp::socket *from, boost::asio::ip::tcp::socket *to, char *data) {
        do_read(from, to, data);
    }

    void do_read(boost::asio::ip::tcp::socket *from, boost::asio::ip::tcp::socket *to, char *data) {
        auto self(shared_from_this());
        from->async_read_some(boost::asio::buffer(data, max_length),
            [this, self, from, to, data](boost::system::error_code ec, std::size_t length) {
                if (not ec) {
                    if (length > 0) {
                        do_write(from, to, data, length);
                    }
                }
            }
        );
    }

    void do_write(boost::asio::ip::tcp::socket *from, boost::asio::ip::tcp::socket *to, char *data, std::size_t length) {
        auto self(shared_from_this());
        boost::asio::async_write(*to, boost::asio::buffer(data, length),
            [this, self, from, to, data](boost::system::error_code ec, std::size_t /*length*/) {
                if (not ec)
                    do_read(from, to, data);
            }
        );
    }

    boost::asio::ip::tcp::socket socket_, socket2_;
    enum { max_length = 4096 };
    char data_[max_length], data2_[max_length];
};

class Server
{
public:
    Server(boost::asio::io_context& io_context, unsigned short port)
    : context_(io_context), 
    acceptor_(io_context, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)),
    signal_(io_context, SIGCHLD) {
        wait_for_signal();
        accept();
    }

    void wait_for_signal() {
        signal_.async_wait(
            [this](boost::system::error_code /*ec*/, int /*signo*/) {
                if (acceptor_.is_open()) {
                    int status = 0;
                    while (waitpid(-1, &status, WNOHANG) > 0) {}

                    wait_for_signal();
                }
            }
        );
    }

    void accept() {
        acceptor_.async_accept(
            [this](boost::system::error_code ec, boost::asio::ip::tcp::tcp::socket socket) {
                if (not ec) {
                    context_.notify_fork(boost::asio::io_context::fork_prepare);
                    pid_t pid = fork();
                    if (pid < 0) {
                        context_.notify_fork(boost::asio::io_context::fork_parent);
                        socket.close();
                        perror("fork");
                        accept();
                    }
                    else if (pid == 0) {
                        context_.notify_fork(boost::asio::io_context::fork_child);
                        acceptor_.close();
                        signal_.cancel();
                        auto session = std::make_shared<Session>(std::move(socket));
                        session->start();
                    }
                    else {
                        context_.notify_fork(boost::asio::io_context::fork_parent);
                        socket.close();
                        accept();
                    }
                }
            }
        );
    }

private:
    
    boost::asio::io_context& context_;
    boost::asio::ip::tcp::acceptor acceptor_;
    boost::asio::signal_set signal_;
};

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::cerr << "Usage: http_server <port>\n";
        return 1;
    }

    int port = std::stoi(argv[1]);

    try {
        boost::asio::io_context io_context;
        Server server(io_context, port);
        io_context.run();
    }
    catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }
}