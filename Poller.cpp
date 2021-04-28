#include "Poller.h"
#include "Channel.h"

Poller::Poller(EventLoop *loop) : ownerLoop_(loop)
{
}

// 判断当前channel是否在当前poller中
bool Poller::hasChannel(Channel *channel) const
{
    auto it = channels_.find(channel->fd());
    // 当通过channel对应的sockfd找到了，找到的it的channel就是要查询的channel，才说明该channel存在于该poller中
    return it != channels_.end() && it->second == channel;
}