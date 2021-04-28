#pragma once

#include <vector>
#include <string>
#include <algorithm>
/**
 *  
 * 
 *  
 *  见buffer.png
 *  
 * 
 * 
 */

// 缓冲区 比如要发送的数据大于能够发送的数据，就先放一些到缓冲区，
// 读到的数据大于处理的数据，就先放在缓冲区
class Buffer
{
public:
    // 8个字节来装一个数字，表示包的大小
    static const size_t kCheapPrepend = 8;
    static const size_t kInitialSize = 1024;

    explicit Buffer(size_t initialSize = kInitialSize)
        : buffer_(kCheapPrepend + initialSize)
        , readerIndex_(kCheapPrepend)
        , writerIndex_(kCheapPrepend)
    {}
	
    // 可读的数据 就是存放的是即发送的数据
    size_t readableBytes() const 
    {
        return writerIndex_ - readerIndex_;
    }
	
    // 可写的数据
    size_t writableBytes() const
    {
        return buffer_.size() - writerIndex_;
    }
	
    // 已经读取的数据
    size_t prependableBytes() const
    {
        return readerIndex_;
    }

    // 返回可读数据的起始地址
    const char* peek() const
    {
        return begin() + readerIndex_;
    }

    // 将缓冲区len的长度进行复位
    void retrieve(size_t len)
    {
		// 表示还没有读完数据
        if (len < readableBytes())
        {
            readerIndex_ += len; // 应用只读取了刻度缓冲区数据的一部分，就是len，还剩下readerIndex_ += len -> writerIndex_
        }
        else   // len == readableBytes()
        {
            retrieveAll();
        }
    }

    void retrieveAll()
    {
        readerIndex_ = writerIndex_ = kCheapPrepend;
    }

    // 把onMessage函数上报的buffer内容转为string
    std::string retrieveAllAsString()
    {
        return retrieveAsString(readableBytes()); // 应用可读取数据的长度
    }

    std::string retrieveAsString(size_t len)
    {
		// 从可读数据开始位置，长度为len的char构造为一个string
        std::string result(peek(), len);
        retrieve(len); // 上面一句把缓冲区中可读的数据，已经读取出来，这里肯定要对缓冲区进行复位操作
        return result;
    }

    // 要写len长度的数据
    void ensureWriteableBytes(size_t len)
    {
        if (writableBytes() < len)
        {
            makeSpace(len); // 扩容函数
        }
    }

    // 向缓冲区添加数据
    void append(const char *data, size_t len)
    {
        ensureWriteableBytes(len);
        std::copy(data, data+len, beginWrite());
        writerIndex_ += len;
    }

    char* beginWrite()
    {
        return begin() + writerIndex_;
    }

    const char* beginWrite() const
    {
        return begin() + writerIndex_;
    }

    // 从fd上读取数据
    ssize_t readFd(int fd, int* saveErrno);
    // 通过fd发送数据
    ssize_t writeFd(int fd, int* saveErrno);
private:

    // vector数组首元素的地址
    char* begin()
    {
        // it.operator*()
        return &*buffer_.begin();  // vector底层数组首元素的地址，也就是数组的起始地址
    }
    const char* begin() const
    {
        return &*buffer_.begin();
    }
    void makeSpace(size_t len)
    {
        if (writableBytes() + prependableBytes() < len + kCheapPrepend)
        {
            buffer_.resize(writerIndex_ + len);
        }
        else
        {
            size_t readalbe = readableBytes();
            std::copy(begin() + readerIndex_, 
                    begin() + writerIndex_,
                    begin() + kCheapPrepend);
            readerIndex_ = kCheapPrepend;
            writerIndex_ = readerIndex_ + readalbe;
        }
    }

    std::vector<char> buffer_;
    size_t readerIndex_;
    size_t writerIndex_;
};