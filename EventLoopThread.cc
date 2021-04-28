#include "EventLoopThread.h"
#include "EventLoop.h"


EventLoopThread::EventLoopThread(const ThreadInitCallback &cb, 
        const std::string &name)
        : loop_(nullptr)
        , exiting_(false)
        , thread_(std::bind(&EventLoopThread::threadFunc, this), name)
        , mutex_()
        , cond_()
        , callback_(cb)
{

}

EventLoopThread::~EventLoopThread()
{
    exiting_ = true;
    if (loop_ != nullptr)
    {
        loop_->quit();
		// 等线程结束才结束
        thread_.join();
    }
}
// 开启循环
EventLoop* EventLoopThread::startLoop()
{
	// 启动线程，启动threadFunc函数
    thread_.start(); 
    // start函数中会去将 loop_ = &loop;
    // startloop需要访问loop_，所以用条件变量
    EventLoop *loop = nullptr;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        while ( loop_ == nullptr )
        {
			// 等待在这把锁上，因为下面notify_one之后，锁就会被揭开，这里才可以使用
            cond_.wait(lock);
        }
		// 到了这里说明loop_不是空了
        loop = loop_;
    }
    return loop;
}

// 下面这个方法，实在单独的新线程里面运行的
void EventLoopThread::threadFunc()
{
	// 创建一个独立的EventLoop，和上面的线程一一对应
    // one loop one thread
    // 这里可以在面试的时候说，muduo到底是怎么实现one loop one thread
    EventLoop loop; 
    // 如果构造的时候传入了ThreadInitCallback
    if (callback_)
    {
        callback_(&loop);
    }

    {
        std::unique_lock<std::mutex> lock(mutex_);
        loop_ = &loop;
        cond_.notify_one();
    }
    // 执行loop函数，开启底层的poll函数
    loop.loop(); 
	// 由于上面的loop是一个循环，若程序运行到这里，说明服务器结束了
    std::unique_lock<std::mutex> lock(mutex_);
    loop_ = nullptr;
}