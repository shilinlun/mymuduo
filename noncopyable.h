#pragma once

/*
    被继承之后，派生类可以正常地构造和析构，但不能进行拷贝和赋值操作
*/
class noncopyable
{
public:
    noncopyable(const noncopyable&) = delete;
    noncopyable& operator=(const noncopyable&) = delete;
protected:
    noncopyable() = default;
    ~noncopyable() = default;
};