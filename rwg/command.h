#ifndef COMMAND_H
#define COMMAND_H

#include <string>
#include <memory>
#include <unordered_map>
#include <sys/types.h>
#include <list>

class Client;
class Process;
struct Pipe;
class CommandManager;

class Command
{
public:
    Command(Client *client, std::shared_ptr<CommandManager> cmdmgr):
    seqno_(), pipe0(), client_(client), cmdmgr(cmdmgr), cmdline() {}
    virtual void exec() {}
    virtual void wait() {}
    void set_seqno(int n) { seqno_ = n; }
    int seqno() { return seqno_; }
    virtual void cleanup(std::shared_ptr<Process> p) {}
    virtual bool finished() { return true; }
    void set_pipe0(std::shared_ptr<Pipe> p) { pipe0 = p; }
    std::shared_ptr<Pipe> get_pipe0() { return pipe0; }
    virtual ~Command() {}
    Client *client() { return client_; }
    void set_cmdline(const std::string& l) { cmdline = l; }
    std::string get_cmdline() { return cmdline; }

protected:
    int seqno_;
    // pipe0 comes from numbered pipe
    std::shared_ptr<Pipe> pipe0;
    Client *client_;
    std::shared_ptr<CommandManager> cmdmgr;
    std::string cmdline;
};

class Setenv : public Command
{
public:
    Setenv(const std::string& var, const std::string& value, Client *client, std::shared_ptr<CommandManager> cmdmgr)
    : var(var), value(value), Command(client, cmdmgr) {}
    virtual void exec();
    virtual ~Setenv() {}

private:
    std::string var;
    std::string value;
};

class Printenv : public Command
{
public:
    Printenv(const std::string& var, Client *client, std::shared_ptr<CommandManager> cmdmgr)
    : var(var), Command(client, cmdmgr) {}
    virtual void exec();
    virtual ~Printenv() {}

private:
    std::string var;
};

class Exit : public Command
{
public:
    Exit(Client *client, std::shared_ptr<CommandManager> cmdmgr): Command(client, cmdmgr) {}
    virtual void exec();
    virtual ~Exit() {}
};

class Executables : public Command
{
public:
    Executables(Client *client, std::shared_ptr<CommandManager> cmdmgr)
    : processes(), to_number_pipe_(), to_user_pipe_(), Command(client, cmdmgr) {}
    virtual void exec();
    virtual void wait();
    virtual void cleanup(std::shared_ptr<Process> p);
    virtual bool finished();
    void add_process(std::shared_ptr<Process> p);
    void set_to_number_pipe() { to_number_pipe_ = true; }
    void set_to_user_pipe() { to_user_pipe_ = true; }
    virtual ~Executables() {}

private:
    std::list<std::shared_ptr<Process> > processes;
    bool to_number_pipe_;
    bool to_user_pipe_;
};

class CommandManager
{
public:
    CommandManager(Client *client) : seqno(), seqno2command(), client(client) {}
    std::shared_ptr<Command> parse(const std::string& line);
    void cleanup(int seqno) { seqno2command.erase(seqno); }
    bool command_exist(int n) { return seqno2command.count(n) > 0; }
    void put_command(int n, std::shared_ptr<Command> cmd) { seqno2command[n] = cmd; }
    std::shared_ptr<Command> get_command(int n) { return seqno2command[n]; }

private:
    int seqno;
    std::unordered_map<int, std::shared_ptr<Command> > seqno2command;
    Client *client;
};

#endif