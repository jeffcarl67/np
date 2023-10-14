#include "process.h"
#include <unistd.h>
#include <memory>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include "command.h"
#include <iostream>
#include <fcntl.h>
#include "server.h"

void Process::add_argument(const std::string& arg)
{
    argv.push_back(arg);
}

void Process::exec()
{
    if (pipe1 != nullptr and not pipe1_reuse) {
        int ret = pipe(pipe1->pipe);
        if (ret == -1) {
            perror("pipe failed");
            return;
        }
    }

    if (from_number_pipe_)
        close(pipe0->pipe[1]);

    int user_pipe_in_fd = -1, user_pipe_out_fd = -1;
    std::string in_fifo, out_fifo;
    if (has_user_pipe_in_) {
        get_user_pipe_in_fd_fifo(this, user_pipe_in_uid_, &user_pipe_in_fd, &in_fifo);
    }
    if (has_user_pipe_out_) {
        get_user_pipe_out_fd_fifo(this, user_pipe_out_uid_, &user_pipe_out_fd, &out_fifo);
    }

    pid_ = fork();
    while (pid_ < 0) {
        fork_error_ = true;
        pause();
        pid_ = fork();
    }
    if (pid_ == 0) {
        int fd = cmd->client()->fd();
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);

        if (pipe0 != nullptr) {
            dup2(pipe0->pipe[0], STDIN_FILENO);
            close(pipe0->pipe[0]);
        }
        if (pipe1 != nullptr) {
            dup2(pipe1->pipe[1], STDOUT_FILENO);
            if (pipe_stderr)
                dup2(pipe1->pipe[1], STDERR_FILENO);
            close(pipe1->pipe[1]);
        }

        if (not filename.empty()) {
            int fd = creat(filename.c_str(), S_IRUSR | S_IWUSR);
            if (fd >= 0)
                dup2(fd, STDOUT_FILENO);
        }

        if (has_user_pipe_in_) {
            if (in_fifo.empty()) {
                dup2(user_pipe_in_fd, STDIN_FILENO);
            }
            else {
                int fd = open(in_fifo.c_str(), O_RDONLY);
                if (fd == -1) {
                    perror("open fifo failed");
                }
                else {
                    dup2(fd, STDIN_FILENO);
                }
            }
        }

        if (has_user_pipe_out_) {
            if (out_fifo.empty()) {
                dup2(user_pipe_out_fd, STDOUT_FILENO);
            }
            else {
                int fd = open(out_fifo.c_str(), O_WRONLY);
                if (fd == -1) {
                    perror("open fifo failed");
                }
                else {
                    dup2(fd, STDOUT_FILENO);
                }
            }
        }

        std::vector<char*> v;
        for (auto& arg : argv)
            v.push_back(strdup(arg.c_str()));
        v.push_back(nullptr);
        int ret = execvp(v[0], v.data());
        if (ret == -1) {
            for (auto arg : v)
                free(arg);
        }
        std::cerr << "Unknown command: [" << argv[0] << "].\n";
        exit(1);
    }
    else {
        fork_error_ = false;
        if (pipe0 != nullptr)
            close(pipe0->pipe[0]);
        if (pipe1 != nullptr and not to_number_pipe_)
            close(pipe1->pipe[1]);
        if (has_user_pipe_in_ and in_fifo.empty())
            close(user_pipe_in_fd);
        if (has_user_pipe_out_ and out_fifo.empty())
            close(user_pipe_out_fd);
        if (has_user_pipe_in_ and not in_fifo.empty())
            handle_used_fifo(pid_, in_fifo);
    }
}

void OrdinaryPipe::connect()
{
    if (processes.size() == 2) {
        auto p = std::make_shared<Pipe>();
        processes[0]->set_pipe1(p);
        processes[1]->set_pipe0(p);
    }
}

void OrdinaryPipe::add_processes(std::vector<std::shared_ptr<Process> >::iterator& iter)
{
    processes.push_back(*iter);
    ++iter;
    processes.push_back(*iter);
}

void NumberedPipe::connect()
{
    if (processes.size() == 1) {
        std::shared_ptr<Pipe> p;
        auto client = processes[0]->command()->client();
        auto cmdmgr = client->command_manager();
        auto cmd = cmdmgr->get_command(seqno);
        if (cmd == nullptr) {
            p = std::make_shared<Pipe>();
            cmd = std::make_shared<Command>(client, cmdmgr);
            cmd->set_seqno(seqno);
            cmd->set_pipe0(p);
            cmdmgr->put_command(seqno, cmd);
        } else {
            p = cmd->get_pipe0();
            processes[0]->set_pipe1_reuse();
        }
        processes[0]->set_pipe1(p);
        processes[0]->set_to_number_pipe();
        if (pipe_stderr)
            processes[0]->set_pipe_stderr();
    }
}

void NumberedPipe::add_processes(std::vector<std::shared_ptr<Process> >::iterator& iter)
{
    processes.push_back(*iter);
    if (processes.size() == 1)
        ++iter;
}

void Redirection::connect()
{
    if (processes.size() == 1 and not filename.empty()) {
        processes[0]->set_filename(filename);
    }
}

void Redirection::add_processes(std::vector<std::shared_ptr<Process> >::iterator& iter)
{
    processes.push_back(*iter);
    ++iter;
}

ProcessManager *ProcessManager::global()
{
    static ProcessManager proc_mgr;
    return &proc_mgr;
}

void ProcessManager::process_died(pid_t pid)
{
    auto process = pid2process[pid];
    if (process == nullptr) return;
    auto cmd = process->command();
    cmd->cleanup(process);
    pid2process.erase(pid);
}