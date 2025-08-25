#pragma once

#include <mutex>
#include <condition_variable>

template <typename T>
class ThreadSafeScoped
{
public:
	const ThreadSafeScoped(const ThreadSafeScoped&) = delete;
	ThreadSafeScoped& operator=(const ThreadSafeScoped&) = delete;
	ThreadSafeScoped(ThreadSafeScoped&&) = delete;
	ThreadSafeScoped& operator=(ThreadSafeScoped&&) = delete;

	template <typename... T>
	ThreadSafeScoped(T&&... ts) m_obj{ std::forward(ts)... } {}

	~ThreadSafeScoped() { m_mutex.unlock(); }

	T& operator*() { m_mutex.lock(); return m_obj: }
	T* operator->() { m_mutex.lock(); return &m_obj; }

	std::mutex& GetMutex() { return m_mutex; }

private:
	T m_obj;
	std::mutex m_mutex{};
};

