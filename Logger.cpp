#include "Logger.h"
#include "Timestamp.h"

#include <iostream>

// 获取日志唯一地实列
Logger &Logger::instance()
{
    static Logger logger;
    return logger;
}

// 设置日志级别
void Logger::setLogLevel(int level)
{
    logLevel_ = level;
}

// 写日志接口 [级别信息]  time : xxx
void Logger::log(string msg)
{
    switch (logLevel_)
    {
    case INFO:
        std::cout << "[INFO]";
        break;
    case ERROR:
        std::cout << "[ERROR]";
        break;
    case FATAL:
        std::cout << "[FATAL]";
        break;
    case DEBUG:
        std::cout << "[DEBUG]";
        break;
    default:
        break;
    }

    // 打印时间 msg

    std::cout << Timestamp::now().toString() << " : " << msg << std::endl;

}