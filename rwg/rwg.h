#ifndef RWG_H
#define RWG_H

#include <memory>
#include <unordered_map>
#include "command.h"

class Who : public Command
{
public:
    Who(Client *client, std::shared_ptr<CommandManager> cmdmgr)
    : Command(client, cmdmgr) {}
    virtual void exec();
    virtual ~Who() {}
};

class Tell : public Command
{
public:
    Tell(int uid, const std::string& msg, Client *client, std::shared_ptr<CommandManager> cmdmgr)
    : uid(uid), msg(msg), Command(client, cmdmgr) {}
    virtual void exec();
    virtual ~Tell() {}

private:
    int uid;
    std::string msg;
};

class Yell : public Command
{
public:
    Yell(const std::string& msg, Client *client, std::shared_ptr<CommandManager> cmdmgr)
    : msg(msg), Command(client, cmdmgr) {}
    virtual void exec();
    virtual ~Yell() {}

private:
    std::string msg;
};

class Name : public Command
{
public:
    Name(const std::string& name, Client *client, std::shared_ptr<CommandManager> cmdmgr)
    : name(name), Command(client, cmdmgr) {}
    virtual void exec();
    virtual ~Name() {}

private:
    std::string name;
};

#endif