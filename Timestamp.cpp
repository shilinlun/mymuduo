#include "Timestamp.h"

#include <time.h>

Timestamp::Timestamp() : microSecondsSinceEpoch_(0) {}
Timestamp::Timestamp(int64_t microSecondsSinceEpoch_) : microSecondsSinceEpoch_(microSecondsSinceEpoch_)
{
}

Timestamp Timestamp::now()
{
    time_t ti = time(NULL);
    return Timestamp(ti);
}
std::string Timestamp::toString() const
{
    char buf[128] = {0};
    // struct tm *localtime(const time_t *timer) 使用 timer 的值来填充 tm 结构
    tm *tm_time = localtime(&microSecondsSinceEpoch_);
    snprintf(buf, 128, "%4d/%02d/%02d %02d:%02d:%02d",
             tm_time->tm_year + 1900,
             tm_time->tm_mon + 1,
             tm_time->tm_mday,
             tm_time->tm_hour,
             tm_time->tm_min,
             tm_time->tm_sec);

    return buf;
}

#include <iostream>


// int main(){

//     std::cout << Timestamp::now().toString() << std::endl;
//     return 0;
// }