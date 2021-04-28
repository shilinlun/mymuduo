#pragma once

#include "noncopyable.h"
#include "Timestamp.h"

#include <functional>
#include <memory>

class EventLoop;

// 理解为通道，封装了sockfd和感兴趣的event，如EPOLLION、EPOLLOUT事件
// 还绑定了poller返回的具体事件
class Channel : noncopyable
{
public:
    using EventCallback = std::function<void()>;
    using ReadEventCallback = std::function<void(Timestamp)>;

    Channel(EventLoop *loop, int fd);
    ~Channel();

    // Channel(EventLoop *loop, int fd);进行对比，由于EventLoop *loop只占4个字节，不影响编译，所以可以不用申明头文件
    // void handleEvent(Timestamp receiveTime); 定义的是一个Timestamp类型，不知道多少字节，所以必须要声明头文件
    // fd得到poller通知以后，处理事件，调用相应的回调事件
    void handleEvent(Timestamp receiveTime);  

    // 设置回调操作，因为回调是private
    void setReadCallback(ReadEventCallback cb) { readCallback_ = std::move(cb); }
    void setWriteCallback(EventCallback cb) { writeCallback_ = std::move(cb); }
    void setCloseCallback(EventCallback cb) { closeCallback_ = std::move(cb); }
    void setErrorCallback(EventCallback cb) { errorCallback_ = std::move(cb); }

    // 防止当channel被手动释放之后，channel还在执行回调函数
    void tie(const std::shared_ptr<void>&);

    int fd() const { return fd_; }
    int events() const { return events_; }
    int set_revents(int revt) { revents_ = revt; }

    // 相当于epoll_ctl 添加一个读事件
    void enableReading() { events_ |= kReadEvent; update(); }
    void disableReading() { events_ &= ~kReadEvent; update(); }
    void enableWriting() { events_ |= kWriteEvent; update(); }
    void disableWriting() { events_ &= ~kWriteEvent; update(); }
    void disableAll() { events_ = kNoneEvent; update(); }

    // 返回当前fd所处的状态
    bool isNoneEvent() const { return events_ == kNoneEvent; }
    bool isWriting() const { return events_ & kWriteEvent; } // TcpConnection::sendInLoop使用
    bool isReading() const { return events_ & kReadEvent; }

    int index() { return index_; }
    void set_index(int idx) { index_ = idx; }

    // 表示该channel属于哪个eventloop
    EventLoop* ownerLoop() { return loop_; }
    void remove();
private:

    void update();
    void handleEventWithGuard(Timestamp receiveTime);
	
    // 3个状态，表示此时属于对读、写、none三个中哪个感兴趣
    static const int kNoneEvent;
    static const int kReadEvent;
    static const int kWriteEvent;

    EventLoop *loop_; // 事件循环
    const int fd_;    // fd, Poller监听的对象
    int events_; // 注册fd感兴趣的事件
    int revents_; // poller返回的具体发生的事件
    int index_;    // 对应的epoller的三个状态

    std::weak_ptr<void> tie_;
    bool tied_;

    // 因为通道里面能够获知sockfd最终发生的具体事件revents，所以它负责调用具体事件的回调操作
    ReadEventCallback readCallback_;
    EventCallback writeCallback_;
    EventCallback closeCallback_;
    EventCallback errorCallback_;
};

