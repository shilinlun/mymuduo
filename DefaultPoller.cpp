// 这里可以在面试中提一下
// 单独实现Poller中的newDefaultPoller

#include "Poller.h"
#include "EpollPoller.h"

#include <stdlib.h>

Poller *Poller::newDefaultPoller(EventLoop *loop)
{
    if (::getenv("MUDUO_USE_POLL"))
    {
        // 生成poll实列
        return nullptr;
    }
    else
    {
        // 生成epoll实列
        return new EpollPoller(loop);
    }
}