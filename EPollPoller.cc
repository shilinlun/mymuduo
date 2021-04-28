#include "EPollPoller.h"
#include "Logger.h"
#include "Channel.h"

#include <errno.h>
#include <unistd.h>
#include <strings.h>

/**
 *  首先epollpoller中有一个channels_（继承poller）里面装的就是fd和对应的channe，同时
 *  epollpoller中还使用epoll_create1创建了一个epollfd_，这个epollfd_可以初始化存放kInitEventListSize个epoll_event（构成events_）
 *  ，所以要记住channels_中的channel个数是大于等于events_（每一个channel都有一个epoll_event）的个数，
 *  因为只有注册了这个channel（index==kAdded或者kDeleted）才会将channel的event添加到event_中，而只要有一个channel，就会添加到channel_中
 */
 
// 表示一个channel都没有添加到poller中  注意和channel的index初始化为-1 类似
const int kNew = -1;  // channel的成员index_ = -1
// 表示一个channel已经添加到poller中
const int kAdded = 1;
// 表示一个channel已经从poller中删除了
const int kDeleted = 2;

// 用的是epoll_create1
EPollPoller::EPollPoller(EventLoop *loop)
    : Poller(loop)
    , epollfd_(::epoll_create1(EPOLL_CLOEXEC))
    , events_(kInitEventListSize)  
{
    if (epollfd_ < 0)
    {
        LOG_FATAL("epoll_create error:%d \n", errno);
    }
}

EPollPoller::~EPollPoller() 
{
    ::close(epollfd_);
}

Timestamp EPollPoller::poll(int timeoutMs, ChannelList *activeChannels)
{
    // 实际上应该用LOG_DEBUG更为合理
    LOG_INFO("func=%s => fd total count:%lu \n", __FUNCTION__, channels_.size());
	
    // &(*events_.begin()) 就是event_这个vector首个元素的地址,
    // counts = epoll_wait(epfd,events,20,500); 一般我们这样使用，其中events是一个数组，数组名就是数组的首地址
    // 我们这里使用的是vector
    int numEvents = ::epoll_wait(epollfd_, &*events_.begin(), static_cast<int>(events_.size()), timeoutMs);
	// 执行完上面一行，events_中存放的就全是发生事件的fd对应的event

    // errno 是一个全局的，有可能其他地方的函数使得errno改变，所以先保存
    int saveErrno = errno;
    Timestamp now(Timestamp::now());
   // 有发生事件
    if (numEvents > 0)
    {
        LOG_INFO("%d events happened \n", numEvents);
        fillActiveChannels(numEvents, activeChannels);
		// 扩容
        if (numEvents == events_.size())
        {
            events_.resize(events_.size() * 2);
        }
    }
	/ 超时
    else if (numEvents == 0)
    {
        LOG_DEBUG("%s timeout! \n", __FUNCTION__);
    }
	// 出现错误
    else
    {
        if (saveErrno != EINTR)
        {
            errno = saveErrno;
            LOG_ERROR("EPollPoller::poll() err!");
        }
    }
    return now;
}

 
void EPollPoller::updateChannel(Channel *channel)
{
    const int index = channel->index();
    LOG_INFO("func=%s => fd=%d events=%d index=%d \n", __FUNCTION__, channel->fd(), channel->events(), index);

    if (index == kNew || index == kDeleted)
    {
        if (index == kNew)
        {
            int fd = channel->fd();
			// 出现错误
            channels_[fd] = channel;
        }

        channel->set_index(kAdded);
        update(EPOLL_CTL_ADD, channel);
    }
	// 已经在poller上注册了
    else  
    {
        int fd = channel->fd();
		// 表示该channel不再关注事件，删除
        if (channel->isNoneEvent())
        {
            update(EPOLL_CTL_DEL, channel);
            channel->set_index(kDeleted);
        }
        else
        {
            update(EPOLL_CTL_MOD, channel);
        }
    }
}

// 从poller中删除channel
void EPollPoller::removeChannel(Channel *channel) 
{
    int fd = channel->fd();
    channels_.erase(fd);

    LOG_INFO("func=%s => fd=%d\n", __FUNCTION__, fd);
    // 还要看是否添加了，若添加了，则还要再epollfd_中去除
    int index = channel->index();
    if (index == kAdded)
    {
        update(EPOLL_CTL_DEL, channel);
    }
    channel->set_index(kNew);
}

// 填写活跃的连接
void EPollPoller::fillActiveChannels(int numEvents, ChannelList *activeChannels) const
{
	// 这时候的events_中全是发生事件的
    for (int i=0; i < numEvents; ++i)
    {
		// 注意之前就把channel放在了event.data.ptr    // 注意这一句event.data.ptr = channel;
        Channel *channel = static_cast<Channel*>(events_[i].data.ptr);
        channel->set_revents(events_[i].events);
        activeChannels->push_back(channel); // EventLoop就拿到了它的poller给它返回的所有发生事件的channel列表了
    }
}

// 更新channel ，其实就是调用epoll_ctl
void EPollPoller::update(int operation, Channel *channel)
{
    epoll_event event;
    bzero(&event, sizeof event);
    
    int fd = channel->fd();

    event.events = channel->events();
    event.data.fd = fd; 
	// 注意这一句
    event.data.ptr = channel;
    // fd和event有关，event中的data的ptr又指向channel，channel中又包含刚刚那个fd，其实就是到时候就可以通过fd就可以找到对应的channel
    if (::epoll_ctl(epollfd_, operation, fd, &event) < 0)
    {
        if (operation == EPOLL_CTL_DEL)
        {
            LOG_ERROR("epoll_ctl del error:%d\n", errno);
        }
		// 添加或者删除出错就是致命的错误
        else
        {
            LOG_FATAL("epoll_ctl add/mod error:%d\n", errno);
        }
    }
}