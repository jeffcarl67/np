#include <iostream>
#include <memory>
#include <boost/asio.hpp>
#include <unordered_map>
#include <list>
#include <algorithm>
#include <fstream>

std::string encode(const std::string& data) {
    std::string buffer;
    buffer.reserve(data.size());
    for(size_t pos = 0; pos != data.size(); ++pos) {
        switch(data[pos]) {
            case '&':  buffer.append("&amp;");       break;
            case '\"': buffer.append("&quot;");      break;
            case '\'': buffer.append("&apos;");      break;
            case '<':  buffer.append("&lt;");        break;
            case '>':  buffer.append("&gt;");        break;
            case '\n': buffer.append("&NewLine;");   break;
            default:   buffer.append(&data[pos], 1); break;
        }
    }
    return buffer;
}

void output_shell(const std::string& session, const std::string& content)
{
    auto str = encode(content);
    auto script = "<script>document.getElementById('" + session + "').innerHTML += '" + str + "';</script>\n"; 
    std::cout << script;
    std::cout.flush();
}

void output_command(const std::string& session, const std::string& content)
{
    auto str = encode(content);
    auto script = "<script>document.getElementById('" + session + "').innerHTML += '<b>" + str + "</b>';</script>\n"; 
    std::cout << script;
    std::cout.flush();
}

class Session : public std::enable_shared_from_this<Session>
{
public:
    Session(boost::asio::io_context& io_context, const std::string& host,
            const std::string& port, const std::string& file, int index)
    : resolver_(io_context), socket_(io_context), commands_(),
    host_(host), port_(port), file_(file), index_(index), data_{} {}

    void start() {
        if (host_.empty() or port_.empty() or file_.empty())
            return;
        std::ifstream f("test_case/" + file_);
        if (not f)
            return;
        std::string line;
        while (std::getline(f, line)) {
            if ((not line.empty()) and line.back() == '\r')
                line.pop_back();
            commands_.push_back(line);
        }
        do_resolve();
    }

    void do_resolve() {
        auto self(shared_from_this());
        resolver_.async_resolve(host_, port_,
            [this, self](boost::system::error_code ec, boost::asio::ip::tcp::resolver::results_type results){
                if (not ec) {
                    auto it = results.cbegin();
                    if (it == results.cend())
                        return;
                    self->do_connect(*it);
                }
            });
    }

    void do_connect(const boost::asio::ip::tcp::endpoint endpoint) {
        auto self(shared_from_this());
        socket_.async_connect(endpoint,
            [this, self](boost::system::error_code ec) {
                if (not ec) {
                    do_read();
                }
            }
        );
    }

    void do_read() {
        auto self(shared_from_this());
        socket_.async_read_some(boost::asio::buffer(data_, max_length),
            [this, self](boost::system::error_code ec, std::size_t length) {
                if (not ec) {
                    if (length > 0) {
                        std::string content(data_, length);
                        auto s = "s" + std::to_string(index_);
                        output_shell(s, content);
                        auto n = std::count(content.cbegin(), content.cend(), '%');
                        do_write(n);
                        do_read();
                    }
                }
            }
        );
    }

    void do_write(std::ptrdiff_t n) {
        auto self(shared_from_this());
        if (n > 0 and not commands_.empty()) {
            auto command = commands_.front();
            commands_.pop_front();
            command += "\n";
            boost::asio::async_write(socket_, boost::asio::buffer(command.c_str(), command.length()),
                [this, self, command](boost::system::error_code ec, std::size_t /*length*/) {
                    if (not ec) {
                        auto s = "s" + std::to_string(index_);
                        output_command(s, command);
                    }
                }
            );    
        }
    }

private:
    boost::asio::ip::tcp::resolver resolver_;
    boost::asio::ip::tcp::socket socket_;
    std::list<std::string> commands_;
    std::string host_, port_, file_;
    int index_;
    enum { max_length = 4096 };
    char data_[max_length];
};

class Clients
{
public:
    Clients(boost::asio::io_context& io_context): queries_() {
        char *q = getenv("QUERY_STRING");
        if (q == nullptr) return;
        std::string query(q);
        std::size_t pos;

        while ((pos = query.find("&")) != std::string::npos) {
            auto kv = query.substr(0, pos);
            std::string k, v;

            query.erase(0, pos + 1);
            pos = kv.find("=");
            if (pos != std::string::npos) {
                v = kv.substr(pos + 1);
            }
            k = kv.substr(0, pos);
            std::cerr << k << " " << v << "\n";
            queries_[k] = v;
        }

        if (not query.empty()) {
            auto kv = query;
            std::string k, v;

            pos = kv.find("=");
            if (pos != std::string::npos) {
                v = kv.substr(pos + 1);
            }
            k = kv.substr(0, pos);
            std::cerr << k << " " << v << "\n";
            queries_[k] = v;
        }

        for (int i = 0; i < 5; i++) {
            auto num = std::to_string(i);
            auto h = queries_["h" + num];
            auto p = queries_["p" + num];
            auto f = queries_["f" + num];
            if (h.empty() or p.empty() or f.empty())
                continue;
            std::string tr = "          <th scope=\"col\">" + h + ":" + p + "</th>\n";
            std::cout << tr;
            std::cout.flush();
        }
        const char *html1 =
"        </tr>\n"
"      </thead>\n"
"      <tbody>\n"
"        <tr>\n";
        std::cout << html1;
        std::cout.flush();

        for (int i = 0; i < 5; i++) {
            auto num = std::to_string(i);
            auto h = queries_["h" + num];
            auto p = queries_["p" + num];
            auto f = queries_["f" + num];
            if (h.empty() or p.empty() or f.empty())
                continue;
            std::string tr = "          <td><pre id=\"s" + num + "\" class=\"mb-0\"></pre></td>\n";
            std::cout << tr;
            std::cout.flush();
        }
        const char *html2 =
"        </tr>\n"
"      </tbody>\n"
"    </table>\n"
"  </body>\n"
"</html>\n";
        std::cout << html2;
        std::cout.flush();

        for (int i = 0; i < 5; i++) {
            auto num = std::to_string(i);
            auto session = std::make_shared<Session>(io_context, queries_["h" + num],
                                                     queries_["p" + num], queries_["f" + num], i);
            session->start();
        }
    }

private:
    std::unordered_map<std::string, std::string> queries_;
};

const char *html =
"<!DOCTYPE html>\n"
"<html lang=\"en\">\n"
"  <head>\n"
"    <meta charset=\"UTF-8\" />\n"
"    <title>NP Project 3 Sample Console</title>\n"
"    <link\n"
"      rel=\"stylesheet\"\n"
"      href=\"https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css\"\n"
"      integrity=\"sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2\"\n"
"      crossorigin=\"anonymous\"\n"
"    />\n"
"    <link\n"
"      href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\"\n"
"      rel=\"stylesheet\"\n"
"    />\n"
"    <link\n"
"      rel=\"icon\"\n"
"      type=\"image/png\"\n"
"      href=\"https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png\"\n"
"    />\n"
"    <style>\n"
"      * {\n"
"        font-family: 'Source Code Pro', monospace;\n"
"        font-size: 1rem !important;\n"
"      }\n"
"      body {\n"
"        background-color: #212529;\n"
"      }\n"
"      pre {\n"
"        color: #cccccc;\n"
"      }\n"
"      b {\n"
"        color: #01b468;\n"
"      }\n"
"    </style>\n"
"  </head>\n"
"  <body>\n"
"    <table class=\"table table-dark table-bordered\">\n"
"      <thead>\n"
"        <tr>\n";

int main()
{
    std::cout << "Content-type: text/html\r\n\r\n";
    std::cout.flush();
    std::cout << html;
    std::cout.flush();

    try {
        boost::asio::io_context io_context;
        Clients clients(io_context);
        io_context.run();
    }
    catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }
}