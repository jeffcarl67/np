#ifndef PROCESS_H
#define PROCESS_H

#include <string>
#include <vector>
#include <sys/types.h>
#include <memory>
#include <unordered_map>

class Command;

struct Pipe
{
    int pipe[2];
};

class Process
{
public:
    Process(const std::vector<std::string>&& argv)
    : argv(argv), pid_(), fork_error_(), cmd(), pipe0(), pipe1(), 
      pipe1_reuse(), to_number_pipe_(), from_number_pipe_(),
      pipe_stderr(), filename(), has_user_pipe_in_(), has_user_pipe_out_(),
      user_pipe_in_uid_(), user_pipe_out_uid_() {}
    void add_argument(const std::string& arg);
    void exec();
    pid_t pid() { return pid_; }
    bool fork_error() { return fork_error_; }
    bool fork_success() { return (not fork_error_); }
    void set_command(Command *cmd) { this->cmd = cmd; }
    Command *command() { return cmd; }
    void set_pipe0(std::shared_ptr<Pipe> p) { pipe0 = p; }
    void set_pipe1(std::shared_ptr<Pipe> p) { pipe1 = p; }
    void set_pipe1_reuse() { pipe1_reuse = true; }
    void set_to_number_pipe() { to_number_pipe_ = true; }
    bool to_number_pipe() { return to_number_pipe_; }
    void set_from_number_pipe() { from_number_pipe_ = true; }
    bool from_number_pipe() { return from_number_pipe_; }
    void set_pipe_stderr() { pipe_stderr = true; }
    void set_filename(const std::string& f) { filename = f; }
    void set_has_user_pipe_in() { has_user_pipe_in_ = true; }
    void set_has_user_pipe_out() { has_user_pipe_out_ = true; }
    void set_user_pipe_in_uid(int uid) { user_pipe_in_uid_ = uid; }
    void set_user_pipe_out_uid(int uid) { user_pipe_out_uid_= uid; }

private:
    std::vector<std::string> argv;
    pid_t pid_;
    bool fork_error_;
    Command *cmd;
    // pipe0 comes from previous process, pipe1 created by current process
    std::shared_ptr<Pipe> pipe0, pipe1;
    bool pipe1_reuse;
    bool to_number_pipe_;
    bool from_number_pipe_;
    bool pipe_stderr;
    std::string filename;
    bool has_user_pipe_in_;
    bool has_user_pipe_out_;
    int user_pipe_in_uid_;
    int user_pipe_out_uid_;
};

void get_user_pipe_in_fd_fifo(Process *p, int uid, int *fd, std::string *fifo);
void get_user_pipe_out_fd_fifo(Process *p, int uid, int *fd, std::string *fifo);
void handle_used_fifo(pid_t pid, const std::string& fifo);

class Connection
{
public:
    virtual void connect() {}
    // this will modify iter
    virtual void add_processes(std::vector<std::shared_ptr<Process> >::iterator& iter) { }
    void add_process(std::shared_ptr<Process> p) { processes.push_back(p); }
    virtual ~Connection() {}

protected:
    std::vector<std::shared_ptr<Process> > processes;
};

class OrdinaryPipe : public Connection
{
public:
    virtual void connect();
    // this will modify iter
    virtual void add_processes(std::vector<std::shared_ptr<Process> >::iterator& iter);
    virtual ~OrdinaryPipe() {}
};

class NumberedPipe: public Connection
{
public:
    NumberedPipe(int n, bool pipe_stderr = false) : seqno(n), pipe_stderr(pipe_stderr) { }
    virtual void connect();
    // this will modify iter
    virtual void add_processes(std::vector<std::shared_ptr<Process> >::iterator& iter);
    virtual ~NumberedPipe() {}

private:
    int seqno;
    bool pipe_stderr;
};

class Redirection: public Connection
{
public:
    Redirection(const std::string& f) : filename(f) {}
    virtual void connect();
    // this will modify iter
    virtual void add_processes(std::vector<std::shared_ptr<Process> >::iterator& iter);
    virtual ~Redirection() {}

private:
    std::string filename;
};

class ProcessManager
{
public:
    static ProcessManager *global();
    void add_process(pid_t pid, std::shared_ptr<Process> p) { pid2process[pid] = p; }
    std::shared_ptr<Process> get_process(pid_t pid) { return pid2process[pid]; }
    void process_died(pid_t pid);

private:
    ProcessManager(): pid2process() {}
    std::unordered_map<pid_t, std::shared_ptr<Process> > pid2process;
};

#endif