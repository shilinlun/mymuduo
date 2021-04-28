#pragma once

#include "noncopyable.h"
#include "Timestamp.h"

#include <vector>
#include <unordered_map>

class Channel;
class EventLoop;

// muduo做一个poller就是因为muduo实现了poll和epoll，都继承自poller
class Poller : noncopyable
{
public:
    using ChannelList = std::vector<Channel*>;

    Poller(EventLoop *loop);
    virtual ~Poller() = default;

    // 给所有IO复用保留统一的接口
    virtual Timestamp poll(int timeoutMs, ChannelList *activeChannels) = 0; // 相当于epoll_wait
    virtual void updateChannel(Channel *channel) = 0; // 相当于epoll_ctl
    virtual void removeChannel(Channel *channel) = 0; // 相当于epoll_ctl del

    
    // 判断当前channel是否在当前poller中
    bool hasChannel(Channel *channel) const;

    // eventloop可以通过该接口获取默认的IO复用的具体实现,这里在派生类实现,因为通过派生类实现一个类，该类指向基类Poller的指针
    // muduo中的实现是这样的：
    // 将该函数的实现单独写在一个文件中，该文件就只有一个这个函数的实现
    // 函数中主要的功能是，若在系统环境变量中设置了一个变量叫MUDUO_USE_POLL,则new 一个PollPoller
    // 否则则new一个EPOLLPoller
    static Poller* newDefaultPoller(EventLoop *loop);
protected:
    // map的key就是sockfd，value就是sockfd所属的channel
    using ChannelMap = std::unordered_map<int, Channel*>;
    ChannelMap channels_;
private:
    // 表示poller所属的eventloop
    EventLoop *ownerLoop_; 
};