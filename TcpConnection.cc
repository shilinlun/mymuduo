#include "TcpConnection.h"
#include "Logger.h"
#include "Socket.h"
#include "Channel.h"
#include "EventLoop.h"

#include <functional>
#include <errno.h>
#include <sys/types.h>         
#include <sys/socket.h>
#include <strings.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <string>

static EventLoop* CheckLoopNotNull(EventLoop *loop)
{
    if (loop == nullptr)
    {
        LOG_FATAL("%s:%s:%d TcpConnection Loop is null! \n", __FILE__, __FUNCTION__, __LINE__);
    }
    return loop;
}

TcpConnection::TcpConnection(EventLoop *loop, 
                const std::string &nameArg, 
                int sockfd,
                const InetAddress& localAddr,
                const InetAddress& peerAddr)
    : loop_(CheckLoopNotNull(loop))
    , name_(nameArg)
    , state_(kConnecting)
    , reading_(true)
    , socket_(new Socket(sockfd))
    , channel_(new Channel(loop, sockfd))
    , localAddr_(localAddr)
    , peerAddr_(peerAddr)
    , highWaterMark_(64*1024*1024) // 64M
{
    // 下面给channel设置相应的回调，poller给channel通知感兴趣的事件发生了，channel就会去执行相应的回调
    channel_->setReadCallback(
        std::bind(&TcpConnection::handleRead, this, std::placeholders::_1)
    );
    channel_->setWriteCallback(
        std::bind(&TcpConnection::handleWrite, this)
    );
    channel_->setCloseCallback(
        std::bind(&TcpConnection::handleClose, this)
    );
    channel_->setErrorCallback(
        std::bind(&TcpConnection::handleError, this)
    );

    LOG_INFO("TcpConnection::ctor[%s] at fd=%d\n", name_.c_str(), sockfd);
    socket_->setKeepAlive(true);
}


TcpConnection::~TcpConnection()
{
    LOG_INFO("TcpConnection::dtor[%s] at fd=%d state=%d \n", 
        name_.c_str(), channel_->fd(), (int)state_);
}
// 发送数据
void TcpConnection::send(const std::string &buf)
{
    if (state_ == kConnected)
    {
        if (loop_->isInLoopThread())
        {
            sendInLoop(buf.c_str(), buf.size());
        }
        else
        {
            loop_->runInLoop(std::bind(
                &TcpConnection::sendInLoop,
                this,
                buf.c_str(),
                buf.size()
            ));
        }
    }
}

// 应用写的快，而内核发送数据慢，需要把待发送数据写入缓冲区，而且设置水位回调
void TcpConnection::sendInLoop(const void* data, size_t len)
{
    ssize_t nwrote = 0;
	// 还没发送完的数据长度
    size_t remaining = len;
    bool faultError = false;

    // 之前调用过connenction的shutdown，不能再发送
    if (state_ == kDisconnected)
    {
        LOG_ERROR("disconnected, give up writing!");
        return;
    }

    // 表示channel第一次开始写数据，而且缓冲区没有待发送数据 因为readableBytes表示可读的数据，若没有
    // 则表示没有缓冲区没有要write的数据，直接write data
    // 因为是第一次，一般我们都是设置channel的isReading()，所以isWriting == false
    if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0)
    {
        nwrote = ::write(channel_->fd(), data, len);
        if (nwrote >= 0)
        {
            remaining = len - nwrote;
			// 若一次性发送完，就不用再给channel设置epollout事件
            if (remaining == 0 && writeCompleteCallback_)
            {
                
                loop_->queueInLoop(
                    std::bind(writeCompleteCallback_, shared_from_this())
                );
            }
        }
        else // nwrote < 0
        {
            nwrote = 0;
			// 非阻塞没有数据，正常返回，则是EWOULDBLOCK
            if (errno != EWOULDBLOCK)
            {
                LOG_ERROR("TcpConnection::sendInLoop");
				// 对端发送一个重置
                if (errno == EPIPE || errno == ECONNRESET) // SIGPIPE  RESET
                {
                    faultError = true;
                }
            }
        }
    }

    // 说明当前这一次write并没有把数据全部发送出去，剩余的数据需要保存到buffer中，然后给channel注册epollout事件，poller
    // 发现缓冲区发送数据区有空间，就通知相应的channel调用handlewrite回调方法，把发送缓冲区中数据全部发送
    if (!faultError && remaining > 0) 
    {
        // 目前发送缓冲区剩余的待发送的数据长度
        size_t oldLen = outputBuffer_.readableBytes();
        if (oldLen + remaining >= highWaterMark_
            && oldLen < highWaterMark_
            && highWaterMarkCallback_)
        {
            loop_->queueInLoop(
                std::bind(highWaterMarkCallback_, shared_from_this(), oldLen+remaining)
            );
        }
		 // 把数据继续放入缓冲区
        outputBuffer_.append((char*)data + nwrote, remaining);
        if (!channel_->isWriting())
        {
			// 注册写事件，否则poller不会给channel通知epollout事件
            channel_->enableWriting(); 
        }
    }
}

// 关闭连接
void TcpConnection::shutdown()
{
    if (state_ == kConnected)
    {
        setState(kDisconnecting);
        loop_->runInLoop(
            std::bind(&TcpConnection::shutdownInLoop, this)
        );
    }
}

void TcpConnection::shutdownInLoop()
{
	// 当前channel已经发送完数据了
    if (!channel_->isWriting()) 
    {
        socket_->shutdownWrite(); // 关闭写端
    }
}

// 连接建立了
void TcpConnection::connectEstablished()
{
    setState(kConnected);
	// 就把channel和TcpConnection绑定在一起，因为TcpConnection是给用户的，若用户不小心把TcpConnection删除了，则按道理就不能使用channel了
    // 所以绑定在一起，你去看channel执行回调的时候Channel::handleEvent，只有当绑定了，才执行回调，若TcpConnection被删除了，
    // 则不能绑定，则channel的tied_ = false；
    channel_->tie(shared_from_this());
	// 向poller注册读事件epollin
    channel_->enableReading(); 

    // 新连接建立，执行回调（用户设置的onConnection）
    connectionCallback_(shared_from_this());
}

// 连接销毁
void TcpConnection::connectDestroyed()
{
    if (state_ == kConnected)
    {
        setState(kDisconnected);
        channel_->disableAll(); 
		// 断开的时候，也会调用onConnection方法
        connectionCallback_(shared_from_this());
    }
	// 把channel从poller中删除
    channel_->remove(); 
}

void TcpConnection::handleRead(Timestamp receiveTime)
{
    int savedErrno = 0;
    ssize_t n = inputBuffer_.readFd(channel_->fd(), &savedErrno);
    if (n > 0)
    {
        // 已建立连接的用户有可读事件发生，调用用户传入的回调操作 onMessage
        messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
    }
	// 客户断开
    else if (n == 0)
    {
        handleClose();
    }
    else
    {
        errno = savedErrno;
        LOG_ERROR("TcpConnection::handleRead");
        handleError();
    }
}
// 有epollout事件才写
void TcpConnection::handleWrite()
{
    if (channel_->isWriting())
    {
        int savedErrno = 0;
        ssize_t n = outputBuffer_.writeFd(channel_->fd(), &savedErrno);
        if (n > 0)
        {
            outputBuffer_.retrieve(n);
			// 表示全部发送了
            if (outputBuffer_.readableBytes() == 0)
            {
                channel_->disableWriting();
				 // 若用户提供了这个回调
                if (writeCompleteCallback_)
                {
                    // 唤醒loop_对应的thread线程，执行回调
                    loop_->queueInLoop(
                        std::bind(writeCompleteCallback_, shared_from_this())
                    );
                }
                if (state_ == kDisconnecting)
                {
                    shutdownInLoop();
                }
            }
        }
        else
        {
            LOG_ERROR("TcpConnection::handleWrite");
        }
    }
    else
    {
        LOG_ERROR("TcpConnection fd=%d is down, no more writing \n", channel_->fd());
    }
}


void TcpConnection::handleClose()
{
    LOG_INFO("TcpConnection::handleClose fd=%d state=%d \n", channel_->fd(), (int)state_);
    setState(kDisconnected);
    channel_->disableAll();

    TcpConnectionPtr connPtr(shared_from_this());
	// 执行连接关闭的回调 其实和下面类似
    connectionCallback_(connPtr); 
	// 关闭连接的回调
    closeCallback_(connPtr); 
}

void TcpConnection::handleError()
{
    int optval;
    socklen_t optlen = sizeof optval;
    int err = 0;
    if (::getsockopt(channel_->fd(), SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0)
    {
        err = errno;
    }
    else
    {
        err = optval;
    }
    LOG_ERROR("TcpConnection::handleError name:%s - SO_ERROR:%d \n", name_.c_str(), err);
}