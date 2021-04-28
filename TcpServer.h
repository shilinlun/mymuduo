#pragma once

/**
 * 用户使用muduo编写服务器程序 
 */

/**
 * 
 * 
 *  mainloop中创建一个TcpServer，TcpServer构造函数中会初始化一个Acceptor，
 *  同时向Acceptor注册一个回调newConnection，Acceptor会初始化一个listenfd，
 *  同时将该listenfd打包为一个channel给Poller，Poller就去监听该listenfd，
 *  同时Acceptor会向Poller注册一个回调函数（handleRead），该回调函数的就是：
 *  当Poller发现listenfd有事件的时候，Poller就去让Acceptor执行该回调函数，
 *  回调函数中，Acceptor会去返回新连接的connfd，然后执行newConnection回调函数，
 *  把新的connfd打包为一个channel，让TcpServer所在的mainloop分发给其他subloop
 * 
 */
#include "EventLoop.h"
#include "Acceptor.h"
#include "InetAddress.h"
#include "noncopyable.h"
#include "EventLoopThreadPool.h"
#include "Callbacks.h"
#include "TcpConnection.h"
#include "Buffer.h"

#include <functional>
#include <string>
#include <memory>
#include <atomic>
#include <unordered_map>

// 对外的服务器编程使用的类
class TcpServer : noncopyable
{
public:
    using ThreadInitCallback = std::function<void(EventLoop*)>;
	// 是否重用端口	
    enum Option
    {
        kNoReusePort,
        kReusePort,
    };

    TcpServer(EventLoop *loop,
                const InetAddress &listenAddr,
                const std::string &nameArg,
                Option option = kNoReusePort);
    ~TcpServer();
	
	// 连接建立和断开都会调用这个方法
    void setThreadInitcallback(const ThreadInitCallback &cb) { threadInitCallback_ = cb; }
    void setConnectionCallback(const ConnectionCallback &cb) { connectionCallback_ = cb; }
    void setMessageCallback(const MessageCallback &cb) { messageCallback_ = cb; }
    void setWriteCompleteCallback(const WriteCompleteCallback &cb) { writeCompleteCallback_ = cb; }

    // 设置底层subloop的个数
    void setThreadNum(int numThreads);

    // 开启服务器监听
    void start();
private:
    void newConnection(int sockfd, const InetAddress &peerAddr);
    void removeConnection(const TcpConnectionPtr &conn);
    void removeConnectionInLoop(const TcpConnectionPtr &conn);

    using ConnectionMap = std::unordered_map<std::string, TcpConnectionPtr>;
	// 用户定义的loop	baseLoop
    EventLoop *loop_; 

    const std::string ipPort_;
    const std::string name_;
	
	// 运行在mainloop，任务就是监听新连接
    std::unique_ptr<Acceptor> acceptor_; 

    std::shared_ptr<EventLoopThreadPool> threadPool_; // one loop per thread

    ConnectionCallback connectionCallback_; // 有新连接时的回调
    MessageCallback messageCallback_; // 有读写消息时的回调
    WriteCompleteCallback writeCompleteCallback_; // 消息发送完成以后的回调

    ThreadInitCallback threadInitCallback_; // loop线程初始化的回调

    std::atomic_int started_;

    int nextConnId_;
    ConnectionMap connections_; // 保存所有的连接
};