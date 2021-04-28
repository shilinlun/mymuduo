#include "Buffer.h"

#include <errno.h>
#include <sys/uio.h>
#include <unistd.h>

/**
 *  从fd上读取数据,buffer缓冲区是有大小的，但是从fd上读数据的时候，却不知道要读多少数据 就相当于是若读到的数据为a，若现在可写的数据大小b小于65536，则把a的大小分为b+另外一部分
 *  大小为b的直接写到可写的部分，其余写道extrabuf，若可写的数据b大于65536，则直接全部写道可写数据中，由此可以看到，muduo每次读的数据都是65536，若可写的数据小于65536，则分两部分写入
 *  若可写的数据大于65536，则直接写入可写部分
 *  
 */
ssize_t Buffer::readFd(int fd, int* saveErrno)
{
    char extrabuf[65536] = {0}; // 栈上的内存空间  64K
    
    struct iovec vec[2];
    
	// 这是buffer可写的数据
    const size_t writable = writableBytes(); 
    vec[0].iov_base = begin() + writerIndex_;
    vec[0].iov_len = writable;

    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof extrabuf;
    
    const int iovcnt = (writable < sizeof extrabuf) ? 2 : 1;
	// 去百度下readv
    const ssize_t n = ::readv(fd, vec, iovcnt);
    if (n < 0)
    {
        *saveErrno = errno;
    }
    else if (n <= writable) // Buffer的可写缓冲区已经够存储读出来的数据了
    {
        writerIndex_ += n;
    }
	// extrabuf 也写了数据
    else 
    {
        writerIndex_ = buffer_.size();
        append(extrabuf, n - writable);  // writerIndex_开始写 n - writable大小的数据
    }

    return n;
}

// 通过fd发送数据
ssize_t Buffer::writeFd(int fd, int* saveErrno)
{
    ssize_t n = ::write(fd, peek(), readableBytes());
    if (n < 0)
    {
        *saveErrno = errno;
    }
    return n;
}