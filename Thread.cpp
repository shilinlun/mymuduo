#include "Thread.h"
#include "CurrentThread.h"
#include <semaphore.h>

// 这样初始化为0，不能=0
std::atomic_int Thread::numCreated_(0);

Thread::Thread(ThreadFunc func, const std::string &name)
    : started_(false),
      joined_(false),
      tid_(0),
      func_(std::move(func)),
      name_(name)
{
    setDefaultName();
}

// 一个Thread对象就是记录了一个线程的详细信息
void Thread::start()
{
    started_ = true;
    sem_t sem;
    // 注意sem_init的参数，去百度下
    // 初始为0，下面的wait就会等待
    sem_init(&sem, false, 0);
    // 开启线程 执行func_函数
    thread_ = std::shared_ptr<std::thread>(new std::thread([&]() {
        // 获取线程的tid
        tid_ = CurrentThread::tid();
        sem_post(&sem);
        func_();
    }));

    // 这里必须等待上面新线程的tid_,所以用一个信号量,
    // 因为一个线程start之后，别人有可能会去访问它的tid_，所以必须保证有这个
    sem_wait(&sem);
}

void Thread::join()
{
    joined_ = true;
    thread_->join();
}

void Thread::setDefaultName()
{
    int num = ++numCreated_;
    if (name_.empty())
    {
        char buf[32] = {0};
        snprintf(buf, sizeof(buf), "Thread%d", num);
    }
}

Thread::~Thread()
{
    if (started_ && !joined_)
    {
        thread_->detach(); // thread类提供的分离线程 底层还是pthread_detach
    }
}