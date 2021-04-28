#pragma once

#include <functional>
#include <vector>
#include <atomic>
#include <memory>
#include <mutex>

#include "noncopyable.h"
#include "Timestamp.h"
#include "CurrentThread.h"

class Channel;
class Poller;

// 把这个类想成，既可以作为mainReactor，又可以作为subReactor

// one loop one thread

// 想清楚eventloop、channel、poller三者之间的关系
// 一个eventlooo中有一个poller和多个channel，poller可以监听很多个channel
// 事件循环类，主要包含channel poller（就是epoll）
class EventLoop : noncopyable
{
public:
    using Functor = std::function<void()>;

    EventLoop();
    ~EventLoop();

    // 开启事件循环
    void loop();
    // 退出事件循环
    void quit();

    Timestamp pollReturnTime() const { return pollReturnTime_; }
    
    // 在当前loop中执行cb
    void runInLoop(Functor cb);
    // 把cb放入队列中，唤醒loop所在的线程，执行cb
    void queueInLoop(Functor cb);

    // 用来唤醒loop所在的线程的
    void wakeup();

    // EventLoop中使用Channel的方法
    void updateChannel(Channel *channel);
    void removeChannel(Channel *channel);
    bool hasChannel(Channel *channel);

    // 若返回真，则说明该EventLoop在创建这个EventLoop线程中，若为假，则执行queueInLoop
    bool isInLoopThread() const { return threadId_ ==  CurrentThread::tid(); }
private:
    void handleRead(); // wakeup使用
    void doPendingFunctors(); // 执行回调

    using ChannelList = std::vector<Channel*>;

    std::atomic_bool looping_;  // 原子操作，通过CAS实现的
    std::atomic_bool quit_; // 标识退出loop循环
    
	// 记录当前loop所在线程的id
    /**
     * muduo中有一个mainReactor和多个subReactor，mainReactor主要接受用户的连接，然后将fd和对应event打包为一个channel，
     * 然后采用轮询的方式将channel派发给subReactor，一个subReactor就是一个EventLoop，所以需要记住自己的tid，然后每个channel都只能在
     * 该tid所在的EventLoop下被操作 
     */
    const pid_t threadId_; 
    // poller返回发生事件的activatechannels的时间点
    Timestamp pollReturnTime_; 
    std::unique_ptr<Poller> poller_;
    // 当mainReactor获取到一个新的用户的连接任务，通过轮回算法选择一个subReactor
    // 通过该成员唤醒subReactor处理channel
    // 用的eventfd()函数 libevent用的是socketpair函数
    int wakeupFd_; 
	// 这个weakchannel不是用户连接的fd和对应的事件组成的channel，该channel的fd是wakeupfd，
    // 是mainReactor和subReactor通信的一个fd，然后该channel的事件是mainReactor有请求到来，需要分发到subReactor
    // 这个channel就只负责mainReactor和subReactor直接的通信
    std::unique_ptr<Channel> wakeupChannel_;

    ChannelList activeChannels_;

    std::atomic_bool callingPendingFunctors_; // 标识当前loop是否有需要执行的回调操作
    std::vector<Functor> pendingFunctors_; // 存储loop需要执行的所有的回调操作
    std::mutex mutex_; // 互斥锁，用来保护上面vector容器的线程安全操作
};