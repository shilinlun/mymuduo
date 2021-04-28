#pragma once
#include "noncopyable.h"

#include <functional>
#include <string>
#include <vector>
#include <memory>

class EventLoop;
class EventLoopThread;

class EventLoopThreadPool : noncopyable
{
public:
    using ThreadInitCallback = std::function<void(EventLoop*)>; 

    EventLoopThreadPool(EventLoop *baseLoop, const std::string &nameArg);
    ~EventLoopThreadPool();

    void setThreadNum(int numThreads) { numThreads_ = numThreads; }

    void start(const ThreadInitCallback &cb = ThreadInitCallback());

    // 若工作在多线程中，则默认以轮询的方式分配channel给subreactor
    EventLoop* getNextLoop();

    std::vector<EventLoop*> getAllLoops();

    bool started() const { return started_; }
    const std::string name() const { return name_; }
private:
    // 比如设置了4个线程，则有一个是baseloop，其他4个就是subloop
    EventLoop *baseLoop_;  
    std::string name_;
    bool started_;
    int numThreads_;
	// 轮询的下标
    int next_;
    std::vector<std::unique_ptr<EventLoopThread>> threads_;
	// 每一个线程开启之后，都会返回一个loop指针，就存放在这里面
    std::vector<EventLoop*> loops_;
};