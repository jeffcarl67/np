#include <iostream>
#include <memory>
#include <boost/asio.hpp>
#include <unistd.h>

class Session : public std::enable_shared_from_this<Session>
{
public:
    Session(boost::asio::ip::tcp::socket socket)
    :socket_(std::move(socket)), streambuf_(),
    request_method_(), request_uri_(), server_protocal_(),
    query_string_() ,http_host_() {}

    void start() { http_request(); }

private:
    void http_request() {
        auto self(shared_from_this());
        boost::asio::async_read_until(socket_, streambuf_, "\r\n\r\n",
            [this, self](boost::system::error_code ec, std::size_t length) {
                if (not ec) {
                    auto success = self->parse(length);
                    if (success) {
                        auto pid = fork();
                        if (pid < 0) {
                            perror("fork ");
                            std::string response = self->server_protocal_ + " 500 Internal Server Error\r\n\r\n";
                            self->http_response(response);
                        }
                        else if (pid == 0) {
                            setenv("REQUEST_METHOD", self->request_method_.c_str(), 1);
                            setenv("REQUEST_URI", self->request_uri_.c_str(), 1);
                            setenv("QUERY_STRING", self->query_string_.c_str(), 1);
                            setenv("SERVER_PROTOCOL", self->server_protocal_.c_str(), 1);
                            setenv("HTTP_HOST", self->http_host_.c_str(), 1);
                            auto server_addr = self->socket_.local_endpoint().address().to_string();
                            setenv("SERVER_ADDR", server_addr.c_str(), 1);
                            auto server_port = self->socket_.local_endpoint().port();
                            setenv("SERVER_PORT", std::to_string(server_port).c_str(), 1);
                            auto remote_addr = self->socket_.remote_endpoint().address().to_string();
                            setenv("REMOTE_ADDR", remote_addr.c_str(), 1);
                            auto remote_port = self->socket_.remote_endpoint().port();
                            setenv("REMOTE_PORT", std::to_string(remote_port).c_str(), 1);

                            dup2(self->socket_.native_handle(), STDOUT_FILENO);

                            auto p = request_uri_.find("?");
                            auto path = "." + request_uri_.substr(0, p);
                            std::string name = "cgi";
                            auto pos = path.rfind("/");
                            if (pos != std::string::npos) {
                                name = path.substr(pos + 1);
                            }
                            std::string response = self->server_protocal_ + " 200 OK\r\n";
                            std::cout << response;
                            std::cout.flush();
                            int ret = execl(path.c_str(), name.c_str(), nullptr);
                            if (ret == -1) {
                                std::string response = "\r\n";
                                std::cout << response;
                                std::cout.flush();
                                exit(1);
                            }
                        }
                        else {

                        }
                    }
                    else {
                        std::string response = self->server_protocal_ + " 400 Bad Request\r\n\r\n";
                        self->http_response(response);
                    }
                }
            }
        );
    }

    void http_response(const std::string& response) {
        auto self(shared_from_this());
        boost::asio::async_write(socket_, boost::asio::buffer(response.c_str(), response.size()),
            [this, self](boost::system::error_code ec, std::size_t length) {
                
            }
        );
    }

    bool parse(std::size_t length) {
        boost::asio::streambuf::const_buffers_type bufs = streambuf_.data();
        std::string head(boost::asio::buffers_begin(bufs), boost::asio::buffers_begin(bufs) + length);
        std::string delimiter = "\r\n";

        std::size_t pos = 0;
        std::string line;

        pos = head.find(delimiter);
        if (pos == std::string::npos) {
            std::cerr << "No METHOD found\n";
            return false;
        }
        line = head.substr(0, pos);
        head.erase(0, pos + delimiter.length());

        pos = line.find(' ');
        if (pos == std::string::npos) {
            std::cerr << "expected METHOD\n";
            return false;
        }
        request_method_ = line.substr(0, pos);
        if (request_method_ != "GET") {
            std::cerr << "only support GET method\n";
            return false;
        }
        line.erase(0, pos + 1);

        pos = line.find(' ');
        if (pos == std::string::npos) {
            std::cerr << "expected URI & PROTOCAL\n";
            return false;
        }
        std::string path = line.substr(0, pos);
        line.erase(0, pos + 1);

        pos = path.find("?");
        if (pos != std::string::npos) {
            query_string_ = path.substr(pos + 1);
        }
        request_uri_ = path;

        server_protocal_ = line;

        while ((pos = head.find(delimiter)) != std::string::npos) {
            line = head.substr(0, pos);
            head.erase(0, pos + delimiter.length());

            pos = line.find(": ");
            if (pos == std::string::npos)
                continue;
            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 2);

            if (key == "Host") {
                http_host_ = value;
                break;
            }
        }

        return true;
    }

    boost::asio::ip::tcp::socket socket_;
    boost::asio::streambuf streambuf_;
    std::string request_method_, request_uri_, server_protocal_;
    std::string query_string_;
    std::string http_host_;
};

class Server
{
public:
    Server(boost::asio::io_context& io_context, unsigned short port)
    : acceptor_(io_context, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port))
    {
        do_accept();
    }

private:
    void do_accept() {
        acceptor_.async_accept(
            [this](boost::system::error_code ec, boost::asio::ip::tcp::socket socket) {
                if (not ec) {
                    std::make_shared<Session>(std::move(socket))->start();
                }
                do_accept();
            }
        );
    }

    boost::asio::ip::tcp::acceptor acceptor_;
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