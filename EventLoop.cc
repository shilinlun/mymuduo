#include "EventLoop.h"
#include "Logger.h"
#include "Poller.h"
#include "Channel.h"

#include <sys/eventfd.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <memory>

// __thread
// 防止一个线程创建多个EventLoop

__thread EventLoop *t_loopInThisThread = nullptr;

// poller超时时间 10s 启动后，你会发现每隔10s打印 [INFO]2021/04/21 20:25:26 : func = poll => fd total count:1
const int kPollTimeMs = 10000;

// 调用eventfd 创建weakupfd，用来通知subReactor
int createEventfd()
{
    int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (evtfd < 0)
    {
        LOG_FATAL("eventfd error:%d \n", errno);
    }
    return evtfd;
}

EventLoop::EventLoop()
    : looping_(false)
    , quit_(false)
    , callingPendingFunctors_(false)
    , threadId_(CurrentThread::tid())
    , poller_(Poller::newDefaultPoller(this))
    , wakeupFd_(createEventfd())
    , wakeupChannel_(new Channel(this, wakeupFd_))
{
    LOG_DEBUG("EventLoop created %p in thread %d \n", this, threadId_);
    if (t_loopInThisThread)
    {
        LOG_FATAL("Another EventLoop %p exists in this thread %d \n", t_loopInThisThread, threadId_);
    }
    else
    {
        t_loopInThisThread = this;
    }

    // 设置weakupfd的事件类型以及发生事件后的回调操作 当mainReactor有事件的时候，就会产生read事件
    wakeupChannel_->setReadCallback(std::bind(&EventLoop::handleRead, this));
    // 每一个eventloop都将监听wakeupchannel的epollin读事件了 ，当有read事件发生，就会收到
    wakeupChannel_->enableReading();
}

EventLoop::~EventLoop()
{
    wakeupChannel_->disableAll();
    wakeupChannel_->remove();
    ::close(wakeupFd_);
    t_loopInThisThread = nullptr;
}

// 开启事件循环
void EventLoop::loop()
{
    looping_ = true;
    quit_ = false;

    LOG_INFO("EventLoop %p start looping \n", this);

    while(!quit_)
    {
        activeChannels_.clear();
        // 监听两类fd   一种是client的fd，一种wakeupfd
        pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);
        for (Channel *channel : activeChannels_)
        {
            // poller监听哪些channel发生事件了，上报给eventloop，eventloop就通知channel处理相应的事件
            channel->handleEvent(pollReturnTime_);
        }
        // 执行当前eventloop事件循环需要处理的回调操作
        /**
         * IO线程 mainloop（mainReactor）接受新的fd，然后打包成channel分发给subloop（subReactor）
         * mainloop事先注册一个回调，这个回调需要subloop执行，所以mainreactor唤醒subreactor之后，subreactor既需要去执行poller_->poll，
         * 还要执行mainreactor事先注册的回调
         */
        doPendingFunctors();
    }

    LOG_INFO("EventLoop %p stop looping. \n", this);
    looping_ = false;
}

// 1 loop在自己的线程中调用quit
// 2 如果是在其他线程中调用的quit，比如在一个subloop中调用了mainloop的quit，则先wakeup对方，再去结束它
void EventLoop::quit()
{
    quit_ = true;

    // 如果是在其它线程中，调用的quit   在一个subloop(woker)中，调用了mainLoop(IO)的quit
    if (!isInLoopThread())  
    {
        wakeup();
    }
}

// 在当前loop中执行cb
void EventLoop::runInLoop(Functor cb)
{
	// 说明该loop就在当前线程中执行
    if (isInLoopThread()) 
    {
        cb();
    }
	// 需要先唤醒该线程中的loop，再执行，不是想象的去别人线程执行该cb
    else 
    {
        queueInLoop(cb);
    }
}
// 把cb放入队列中，唤醒loop所在的线程，执行cb
void EventLoop::queueInLoop(Functor cb)
{
	// 加个锁
    {
        std::unique_lock<std::mutex> lock(mutex_);
        pendingFunctors_.emplace_back(cb);
    }

    // 唤醒需要执行上述的cb的线程
    if (!isInLoopThread() || callingPendingFunctors_) 
    {
        wakeup(); // 唤醒loop所在线程
    }
}
// wakeup使用 其实不在乎里面干了什么，主要是mainreactor唤醒subreactor，需要一个事件，只是我们这里就是读事件
void EventLoop::handleRead()
{
  uint64_t one = 1;
  ssize_t n = read(wakeupFd_, &one, sizeof one);
  if (n != sizeof one)
  {
    LOG_ERROR("EventLoop::handleRead() reads %lu bytes instead of 8", n);
  }
}

// 唤醒loop所在线程 其实就是可以写一个数据就可以了，只要可以唤醒就行
void EventLoop::wakeup()
{
    uint64_t one = 1;
    ssize_t n = write(wakeupFd_, &one, sizeof one);
    if (n != sizeof one)
    {
        LOG_ERROR("EventLoop::wakeup() writes %lu bytes instead of 8 \n", n);
    }
}

// EventLoop中使用Channel的方法
void EventLoop::updateChannel(Channel *channel)
{
    poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel *channel)
{
    poller_->removeChannel(channel);
}

bool EventLoop::hasChannel(Channel *channel)
{
    return poller_->hasChannel(channel);
}

// 执行回调
void EventLoop::doPendingFunctors() 
{
	// 这里设计一个局部vector的巧妙之处：由于若直接挨着循环pendingFunctors_，再去执行里面每一个functor，太慢了，
    // 于是用一个局部的vector先把pendingFunctors_的东西换过来，这样pendingFunctors_就可以继续去装其他的functor，
    // 局部的vector再慢慢去执行里面的functor
    std::vector<Functor> functors;
    callingPendingFunctors_ = true;

    {
        std::unique_lock<std::mutex> lock(mutex_);
        functors.swap(pendingFunctors_);
    }

    for (const Functor &functor : functors)
    {
        functor(); // 执行当前loop需要执行的回调操作
    }

    callingPendingFunctors_ = false;
}