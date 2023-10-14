#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <cstdlib>
#include <memory>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <cctype>
#include <cstdio>
#include "command.h"
#include "process.h"
#include "server.h"

void Setenv::exec()
{
    setenv(var.c_str(), value.c_str(), 1);
}

void Printenv::exec()
{
    char *value = getenv(var.c_str());
    if (value) {
        std::string str = std::string(value) + "\n";
        write(client_->fd(), str.c_str(), str.length());
    }
}

void Exit::exec()
{
    client_->close();
}

void Executables::exec()
{
    for (auto& process : processes) {
        process->set_command(this);
        process->exec();
        if (process->fork_success()) {
            auto proc_mgr = ProcessManager::global();
            proc_mgr->add_process(process->pid(), process);
        }
    }
}

void Executables::wait()
{
    if (to_number_pipe_) return;
    while (not finished()) {
        pause();
    }
    cmdmgr->cleanup(seqno_);
}

void Executables::cleanup(std::shared_ptr<Process> p)
{
    processes.remove(p);
    if (to_number_pipe_ and processes.empty())
        cmdmgr->cleanup(seqno_);
}

bool Executables::finished()
{
    return processes.empty();
}

void Executables::add_process(std::shared_ptr<Process> p)
{
    if (pipe0 != nullptr and processes.empty()) {
        p->set_pipe0(pipe0);
        p->set_from_number_pipe();
    }
    processes.push_back(p);
}

bool is_number_after_char(const std::string& s, char c)
{
    auto iter = s.begin();
    if (*iter != c)
        return false;
    ++iter;
    if (iter == s.end())
        return false;
    while (iter != s.end() and std::isdigit(*iter))
        ++iter;
    return iter == s.end();
}

struct StringGroup
{
    std::vector<std::string> strings;
    void add(std::string s) { strings.push_back(s); }
    bool empty() { return strings.empty(); }
    void clear() { strings.clear(); }
    int scan(std::vector<std::string> &token, int index);
    void print();
    bool is_ordinary_pipe();
    bool is_numbered_pipe();
    bool is_stderr_numbered_pipe();
    bool is_redirection();
    int number();
};

int StringGroup::scan(std::vector<std::string> &token, int index)
{
    int i = index + 1;
    strings.push_back(token[index]);
    if (is_ordinary_pipe()) {
        return i;
    }
    if (is_numbered_pipe()) {
        return i;
    }
    if (is_stderr_numbered_pipe()) {
        return i;
    }
    if (is_redirection()) {
        return i;
    }

    for (; i < token.size(); i++) {
        auto& s = token[i];
        if (s == "|" or s == ">") {
            break;
        }
        if (is_number_after_char(s, '|')) {
            break;
        }
        if (is_number_after_char(s, '!')) {
            break;
        }
        else {
            strings.push_back(s);
        }
    }
    return i;
}

void StringGroup::print()
{
    for (auto& s : strings)
        std::cout << s << " ";
    std::cout << std::endl;
}

bool StringGroup::is_ordinary_pipe()
{
    return (strings.size() == 1 and strings[0] == "|");
}

bool StringGroup::is_numbered_pipe()
{
    if (strings.size() != 1)
        return false;
    auto& s = strings[0];
    return is_number_after_char(s, '|');
}

bool StringGroup::is_stderr_numbered_pipe()
{
    if (strings.size() != 1)
        return false;
    auto& s = strings[0];
    return is_number_after_char(s, '!');
}

bool StringGroup::is_redirection()
{
    return (strings.size() == 1 and strings[0] == ">");
}

int StringGroup::number()
{
    if (is_numbered_pipe() or is_stderr_numbered_pipe())
        return std::stoi(strings[0].substr(1));
    return 0;
}

std::shared_ptr<Command> CommandManager::parse(const std::string& line)
{
    std::vector<std::string> token;
    std::string tmp;
    std::stringstream ss(line);
    auto cmdmgr = client->command_manager();
    while (std::getline(ss, tmp, ' ')) {
        if (tmp.empty()) continue;
        token.push_back(tmp);
    }
    if (token.empty()) return std::make_shared<Command>(client, cmdmgr);
    seqno++;
    if (token[0] == "setenv") {
        if (token.size() >= 3)
            return std::make_shared<Setenv>(token[1], token[2], client, cmdmgr);
        else
            return std::make_shared<Command>(client, cmdmgr);
    }
    else if (token[0] == "printenv") {
        if (token.size() >= 2)
            return std::make_shared<Printenv>(token[1], client, cmdmgr);
        else
            return std::make_shared<Command>(client, cmdmgr);
    }
    else if (token[0] == "exit") {
        return std::make_shared<Exit>(client, cmdmgr);
    }
    else {
        std::vector<std::shared_ptr<StringGroup> >groups;
        int i = 0;
        while (i < token.size()) {
            auto sg = std::make_shared<StringGroup>();
            i = sg->scan(token, i);
            groups.push_back(sg);
        }

        std::vector<std::shared_ptr<Connection> > conns;
        std::vector<std::shared_ptr<Process> > procs;
        i = 0;
        while (i < groups.size()) {
            auto& g = groups[i];
            if (g->is_ordinary_pipe()) {
                conns.push_back(std::make_shared<OrdinaryPipe>());
            }
            else if (g->is_numbered_pipe() or g->is_stderr_numbered_pipe()) {
                bool handle_stderr = g->is_stderr_numbered_pipe();
                int n = g->number();
                if (n > 0)
                    conns.push_back(std::make_shared<NumberedPipe>(seqno + n, handle_stderr));
            }
            else if (g->is_redirection()) {
                if (i + 1 < groups.size()) {
                    i++;
                    auto filename = groups[i]->strings[0];
                    conns.push_back(std::make_shared<Redirection>(filename));
                }
            }
            else {
                procs.push_back(std::make_shared<Process>(std::move(g->strings)));
            }
            i++;
        }

        auto cmd = std::make_shared<Executables>(client, cmdmgr);
        if (groups.back()->is_numbered_pipe()) {
            cmd->set_to_number_pipe();
        }
        // if number pipe from previous command
        if (seqno2command.count(seqno) > 0) {
            auto p = seqno2command[seqno]->get_pipe0();
            cmd->set_pipe0(p);
        }
        cmd->set_seqno(seqno);
        seqno2command[seqno] = cmd;
        for (auto& p : procs) {
            cmd->add_process(p);
            p->set_command(cmd.get());
        }

        auto piter = procs.begin();
        // Not handle erroneous format yet
        for (auto& conn : conns) {
            conn->add_processes(piter);
            conn->connect();
        }

        return cmd;
    }
}