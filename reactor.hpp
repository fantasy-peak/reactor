#ifndef _REACTOR_HPP_INCLUDED_
#define _REACTOR_HPP_INCLUDED_

#include <fcntl.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>
#include <chrono>
#include <ctime>
#include <iomanip>

#include <chrono>
#include <functional>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <system_error>
#include <thread>

namespace fantasy {

class NonCopyable {
protected:
	NonCopyable() = default;
	~NonCopyable() = default;

	NonCopyable(const NonCopyable&) = delete;
	NonCopyable& operator=(const NonCopyable&) = delete;

	NonCopyable(NonCopyable&&) noexcept(true) = default;
	NonCopyable& operator=(NonCopyable&&) noexcept(true) = default;
};

struct Time {
	Time() = default;

	Time(uint8_t hour, uint8_t minute, uint8_t second, uint32_t microsecond)
		: hour(hour)
		, minute(minute)
		, second(second)
		, microsecond(microsecond) {
	}

	uint8_t hour{};
	uint8_t minute{};
	uint8_t second{};
	uint32_t microsecond{};
};

class Shutdown {};

class Reactor : public NonCopyable {
public:
	enum class CallStatus : int8_t {
		Ok = 0,
		Remove = 1,
	};

	using EventCallback = std::function<void()>;
	using SingleCall = std::function<void()>;
	using Functor = std::function<void()>;
	using RepeatCall = std::function<CallStatus()>;

	enum class FdStatus : int8_t {
		New = -1,
		Added = 1,
		Deleted = 2,
	};

	class Channel : public NonCopyable {
	public:
		Channel(Reactor& reactor, int fd)
			: m_reactor(reactor)
			, m_fd(fd)
			, m_events(0)
			, m_revents(0)
			, m_fd_state(FdStatus::New) {
		}

		void setEventCallback(EventCallback&& cb) {
			m_event_callback = std::move(cb);
		}

		void setReadCallback(EventCallback&& cb) {
			m_read_callback = std::move(cb);
		}

		void setErrorCallback(EventCallback&& cb) {
			m_error_callback = std::move(cb);
		}

		void setWriteCallback(EventCallback&& cb) {
			m_write_callback = std::move(cb);
		}

		void setCloseCallback(EventCallback&& cb) {
			m_close_callback = std::move(cb);
		}

		void setEventCallback(const EventCallback& cb) {
			m_event_callback = cb;
		}

		void setReadCallback(const EventCallback& cb) {
			m_read_callback = cb;
		}

		void setErrorCallback(const EventCallback& cb) {
			m_error_callback = cb;
		}

		void setWriteCallback(const EventCallback& cb) {
			m_write_callback = cb;
		}

		void setCloseCallback(const EventCallback& cb) {
			m_close_callback = cb;
		}

		void enableReading() {
			m_events |= ReadEvent;
			m_reactor.updateChannel(this);
		}

		void disableReading() {
			m_events &= ~ReadEvent;
			m_reactor.updateChannel(this);
		}

		void enableWriting() {
			m_events |= WriteEvent;
			m_reactor.updateChannel(this);
		}

		void disableWriting() {
			m_events &= ~WriteEvent;
			m_reactor.updateChannel(this);
		}

		void disableAll() {
			m_events = NoneEvent;
			m_reactor.updateChannel(this);
		}

		bool isWriting() const {
			return m_events & WriteEvent;
		}

		bool isReading() const {
			return m_events & ReadEvent;
		}

		int fd() const {
			return m_fd;
		}

		int events() const {
			return m_events;
		}

		auto& state() {
			return m_fd_state;
		}

		bool isNoneEvent() const {
			return m_events == 0;
		}

		auto& reactor() {
			return m_reactor;
		}

		friend class Reactor;

	private:
		int setRevents(int revt) {
			m_revents = revt;
			return revt;
		};

		void setFdStatus(const FdStatus& fd_state) {
			m_fd_state = fd_state;
		}

		void handleEvent() {
			if (m_event_callback) {
				m_event_callback();
				return;
			}
			if ((m_revents & EPOLLHUP) && !(m_revents & EPOLLIN)) {
				if (m_close_callback)
					m_close_callback();
			}
			if (m_revents & (EPOLLIN | EPOLLPRI | EPOLLRDHUP)) {
				if (m_read_callback)
					m_read_callback();
			}
			if (m_revents & EPOLLOUT) {
				if (m_write_callback)
					m_write_callback();
			}
			if (m_revents & EPOLLERR) {
				if (m_error_callback)
					m_error_callback();
			}
		}

		constexpr static int NoneEvent = 0;
		constexpr static int ReadEvent = EPOLLIN | EPOLLPRI;
		constexpr static int WriteEvent = EPOLLOUT;

		Reactor& m_reactor;
		int m_fd;
		int m_events;
		int m_revents;
		FdStatus m_fd_state;
		EventCallback m_read_callback;
		EventCallback m_write_callback;
		EventCallback m_error_callback;
		EventCallback m_close_callback;
		EventCallback m_event_callback;
	};

	using IoCall = std::function<CallStatus(int, const std::weak_ptr<Channel>&)>;

	class Epoll : public NonCopyable {
	public:
		Epoll(const std::function<void(const char*, int, int)>& error_callback, const int init_event_list_size = 16)
			: m_epoll_fd_ptr(new int(::epoll_create1(EPOLL_CLOEXEC)), [](int* epoll_fd_ptr) {close(*epoll_fd_ptr);delete epoll_fd_ptr; })
			, m_events(init_event_list_size)
			, m_error_callback(error_callback) {
		}

		std::optional<std::vector<Channel*>> poll(int timeout_ms) {
			int num_events = ::epoll_wait(*m_epoll_fd_ptr, m_events.data(), static_cast<int>(m_events.size()), timeout_ms);
			if (num_events > 0) {
				std::vector<Channel*> active_channels;
				for (int i = 0; i < num_events; ++i) {
					auto channel_ptr = static_cast<Channel*>(m_events[i].data.ptr);
					channel_ptr->setRevents(m_events[i].events);
					active_channels.emplace_back(channel_ptr);
				}
				if (static_cast<size_t>(num_events) == m_events.size())
					m_events.resize(m_events.size() * 2);
				return active_channels;
			}
			else {
				if (m_error_callback)
					m_error_callback(__FILE__, __LINE__, errno);
			}
			return {};
		}

		void update(int operation, Channel* channel) {
			struct epoll_event event;
			memset(&event, 0x00, sizeof(event));
			event.data.ptr = channel;
			int fd = channel->fd();
			event.events = channel->events();
			if (::epoll_ctl(*m_epoll_fd_ptr, operation, fd, &event) < 0) {
				if (m_error_callback)
					m_error_callback(__FILE__, __LINE__, errno);
			}
		}

		void updateChannel(Channel* channel) {
			auto& fd_state = channel->state();
			if (fd_state == FdStatus::New || fd_state == FdStatus::Deleted) {
				channel->setFdStatus(FdStatus::Added);
				update(EPOLL_CTL_ADD, channel);
			}
			else {
				if (channel->isNoneEvent()) {
					update(EPOLL_CTL_DEL, channel);
					channel->setFdStatus(FdStatus::Deleted);
				}
				else
					update(EPOLL_CTL_MOD, channel);
			}
		}

	private:
		std::unique_ptr<int, std::function<void(int*)>> m_epoll_fd_ptr;
		std::vector<struct epoll_event> m_events;
		std::function<void(const char*, int, int)> m_error_callback;
	};

	class TimerFd : public NonCopyable {
	public:
		TimerFd() {
			createTimerfd();
		}

		void resetTimerfd(const std::chrono::system_clock::time_point& expire_time_point) {
			struct itimerspec new_value;
			struct itimerspec old_value;
			memset(&new_value, 0x00, sizeof(new_value));
			memset(&old_value, 0x00, sizeof(old_value));
			auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(expire_time_point - std::chrono::system_clock::now()).count();
			if (microseconds < 100)
				microseconds = 100;
			struct timespec ts;
			ts.tv_sec = static_cast<time_t>(microseconds / 1000000);
			ts.tv_nsec = static_cast<long>((microseconds % 1000000) * 1000);
			new_value.it_value = ts;
			int ret = ::timerfd_settime(*m_timerfd_ptr, 0, &new_value, &old_value);
			if (ret)
				throw std::system_error(errno, std::generic_category(), "can not set TimerFd");
		}

		int& createTimerfd() {
			m_timerfd_ptr = nullptr;
			std::unique_ptr<int, std::function<void(int*)>> timerfd_ptr(new int(::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC)), [](int* timerfd_ptr) {
				close(*timerfd_ptr);
				delete timerfd_ptr;
			});
			m_timerfd_ptr = std::move(timerfd_ptr);
			if (*m_timerfd_ptr == -1)
				throw std::system_error(errno, std::generic_category(), "can not create timerfd");
			return *m_timerfd_ptr;
		}

		std::optional<uint64_t> read() {
			uint64_t expired;
			auto res = ::read(*m_timerfd_ptr, &expired, sizeof(expired));
			if (res == -1)
				return {};
			return expired;
		}

		auto& fd() {
			return *m_timerfd_ptr;
		}

	private:
		std::unique_ptr<int, std::function<void(int*)>> m_timerfd_ptr;
	};

	enum class CallType {
		IoCall,
		TimedCall,
	};

	class CallId {
	public:
		CallId(CallType type = CallType::TimedCall)
			: m_id(m_sequence++)
			, m_call_type(type) {
		}

		bool operator==(CallId rhs) const { return m_id == rhs.m_id; }

		const auto& id() const { return m_id; }
		const auto& type() const { return m_call_type; }

	private:
		uint64_t m_id;
		CallType m_call_type;
		inline static std::atomic<uint64_t> m_sequence{0};
	};

	class Timer : public NonCopyable {
	public:
		Timer(const RepeatCall& timer_callback,
			const std::chrono::system_clock::time_point& time_point,
			const std::chrono::microseconds& interval)
			: m_timer_callback(timer_callback)
			, m_time_point(time_point)
			, m_interval(interval)
			, m_time_opt(std::nullopt) {
		}

		Timer(const RepeatCall&& timer_callback,
			const std::chrono::system_clock::time_point& time_point,
			const std::chrono::microseconds& interval)
			: m_timer_callback(std::move(timer_callback))
			, m_time_point(time_point)
			, m_interval(interval)
			, m_time_opt(std::nullopt) {
		}

		Timer(const RepeatCall& timer_callback, const Time& time)
			: m_timer_callback(timer_callback)
			, m_time_point([&] {
				auto time_point = getTimePoint(time);
				if (std::chrono::system_clock::now() > time_point)
					return time_point + std::chrono::hours(24);
				else
					return time_point;
			}())
			, m_interval(std::chrono::hours(24))
			, m_time_opt(time) {
		}

		Timer(RepeatCall&& timer_callback, Time&& time)
			: m_timer_callback(std::move(timer_callback))
			, m_time_point([&] {
				auto time_point = getTimePoint(time);
				if (std::chrono::system_clock::now() > time_point)
					return time_point + std::chrono::hours(24);
				else
					return time_point;
			}())
			, m_interval(std::chrono::hours(24))
			, m_time_opt(std::move(time)) {
		}

		std::chrono::time_point<std::chrono::system_clock> getTimePoint(const Time& time) {
			auto time_tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
			struct tm work {};
			localtime_r(&time_tt, &work);
			struct tm timeinfo {};
			timeinfo.tm_year = work.tm_year;
			timeinfo.tm_mon = work.tm_mon;
			timeinfo.tm_mday = work.tm_mday;
			timeinfo.tm_hour = time.hour;
			timeinfo.tm_min = time.minute;
			timeinfo.tm_sec = time.second;
			auto time_c = std::mktime(&timeinfo);
			return std::chrono::system_clock::from_time_t(time_c) + std::chrono::microseconds{time.microsecond};
		}

		auto run() {
			return m_timer_callback();
		}

		void reset(const std::chrono::system_clock::time_point& now = std::chrono::system_clock::now()) {
			if (m_time_opt) {
				m_time_point = getTimePoint(m_time_opt.value());
				m_time_point += m_interval;
			}
			else
				m_time_point = now + m_interval;
		}

		auto& id() {
			return m_call_id;
		}

		auto& timePoint() {
			return m_time_point;
		}

	private:
		RepeatCall m_timer_callback;
		std::chrono::system_clock::time_point m_time_point;
		std::chrono::microseconds m_interval;
		CallId m_call_id;
		std::optional<Time> m_time_opt;
	};

	Reactor(const std::function<void(const char*, int, int)>& error_callback = nullptr)
		: m_epoller_ptr(std::make_unique<Epoll>(error_callback))
		, m_wakeup_fd_ptr(new int[2](), [](int* wakeup_fd_ptr) { close(wakeup_fd_ptr[0]); close(wakeup_fd_ptr[1]); delete[] wakeup_fd_ptr; })
		, m_error_callback(error_callback)
		, m_timerfd_ptr(std::make_unique<TimerFd>()) {
		if (::pipe(m_wakeup_fd_ptr.get()) == -1)
			throw std::system_error(errno, std::system_category(), "pipe error, can't constructor reactor");
		::fcntl(m_wakeup_fd_ptr[0], F_SETFL, O_NONBLOCK | O_CLOEXEC);
		::fcntl(m_wakeup_fd_ptr[1], F_SETFL, O_NONBLOCK | O_CLOEXEC);
	}

	~Reactor() {
		callLater([=] {
			throw Shutdown();
		});
		if (m_thread.joinable())
			m_thread.join();
	}

	auto size() const {
		std::lock_guard<std::mutex> lock(m_mtx);
		return m_pending_functors.size();
	}

	void callLater(SingleCall&& single_call) {
		addCallback(std::move(single_call));
	}

	void callLater(const SingleCall& single_call) {
		addCallback(single_call);
	}

	template <typename Callable>
	auto callNow(Callable&& call) -> decltype(call()) {
		auto task_call = [&] {
			std::packaged_task<decltype(call())()> task(std::forward<Callable>(call));
			auto future = task.get_future();
			addCallback([&] { task(); });
			return future.get();
		};
		return (m_thread_id == std::this_thread::get_id()) ? call() : task_call();
	}

	CallId callAt(std::chrono::system_clock::time_point expiry, SingleCall&& single_call) {
		auto repeat_call = [single_call = std::move(single_call)] {
			single_call();
			return CallStatus::Remove;
		};
		auto timer_ptr = std::make_shared<Timer>(std::move(repeat_call), expiry, std::chrono::microseconds{0});
		auto call_id = timer_ptr->id();
		callNow([&] { addTimedCallback(std::move(timer_ptr)); });
		return call_id;
	}

	CallId callAt(std::chrono::system_clock::time_point expiry, const SingleCall& single_call) {
		return callAt(expiry, SingleCall(single_call));
	}

	CallId callAfter(std::chrono::system_clock::duration timeout, SingleCall&& single_call) {
		auto expiry = std::chrono::system_clock::now() + timeout;
		return callAt(expiry, std::move(single_call));
	}

	CallId callAfter(std::chrono::system_clock::duration timeout, const SingleCall& single_call) {
		auto expiry = std::chrono::system_clock::now() + timeout;
		return callAt(expiry, single_call);
	}

	CallId callEvery(std::chrono::system_clock::duration timeout, RepeatCall&& repeat_call) {
		auto expiry = std::chrono::system_clock::now() + timeout;
		auto timer_ptr = std::make_shared<Timer>(std::move(repeat_call), expiry, std::chrono::duration_cast<std::chrono::microseconds>(timeout));
		auto call_id = timer_ptr->id();
		callNow([&] { addTimedCallback(std::move(timer_ptr)); });
		return call_id;
	}

	CallId callEvery(std::chrono::system_clock::duration timeout, const RepeatCall& repeat_call) {
		return callEvery(timeout, RepeatCall(repeat_call));
	}

	CallId callEveryDay(Time&& time, RepeatCall&& repeat_call) {
		auto timer_ptr = std::make_shared<Timer>(std::move(repeat_call), std::move(time));
		auto call_id = timer_ptr->id();
		callNow([&] { addTimedCallback(std::move(timer_ptr)); });
		return call_id;
	}

	CallId callEveryDay(const Time& time, const RepeatCall& repeat_call) {
		return callEveryDay(Time{time}, RepeatCall(repeat_call));
	}

private:
	auto get(const int& fd) {
		auto it = std::find_if(m_work_channels.begin(), m_work_channels.end(), [&](auto& p) {
			return p.second->fd() == fd;
		});
		if (it != m_work_channels.end())
			return it;
		auto channel_ptr = std::make_shared<Channel>(*this, fd);
		CallId call_id{CallType::IoCall};
		auto ret = m_work_channels.emplace(std::move(call_id), std::move(channel_ptr));
		return ret.first;
	}

public:
	CallId callOnRead(int fd, IoCall&& io_call, bool is_enable_reading = true) {
		return callNow([&] {
			auto& [call_id, channel_ptr] = *get(fd);
			std::weak_ptr<Channel> weak_channel_ptr = channel_ptr;
			channel_ptr->setReadCallback([this, weak_channel_ptr = std::move(weak_channel_ptr), call_id, fd, io_call = std::move(io_call)] {
				auto call_status = io_call(fd, weak_channel_ptr);
				if (call_status == fantasy::Reactor::CallStatus::Remove)
					cancel(call_id);
			});
			if (is_enable_reading)
				channel_ptr->enableReading();
			return call_id;
		});
	}

	CallId callOnRead(int fd, const IoCall& call) {
		return callOnRead(fd, IoCall(call));
	}

	CallId callOnWrite(int fd, IoCall&& io_call, bool is_enable_writing = false) {
		return callNow([&] {
			auto& [call_id, channel_ptr] = *get(fd);
			std::weak_ptr<Channel> weak_channel_ptr = channel_ptr;
			channel_ptr->setWriteCallback([this, weak_channel_ptr = std::move(weak_channel_ptr), fd, call_id, io_call = std::move(io_call)] {
				auto call_status = io_call(fd, weak_channel_ptr);
				if (call_status == fantasy::Reactor::CallStatus::Remove)
					cancel(call_id);
			});
			if (is_enable_writing)
				channel_ptr->enableWriting();
			return call_id;
		});
	}

	CallId callOnWrite(int fd, const IoCall& call) {
		return callOnWrite(fd, IoCall(call));
	}

	CallId callOnClose(int fd, IoCall&& io_call) {
		return callNow([&] {
			auto& [call_id, channel_ptr] = *get(fd);
			std::weak_ptr<Channel> weak_channel_ptr = channel_ptr;
			channel_ptr->setCloseCallback([this, weak_channel_ptr = std::move(weak_channel_ptr), fd, call_id, io_call = std::move(io_call)] {
				auto call_status = io_call(fd, weak_channel_ptr);
				if (call_status == fantasy::Reactor::CallStatus::Remove)
					cancel(call_id);
			});
			return call_id;
		});
	}

	CallId callOnClose(int fd, const IoCall& call) {
		return callOnClose(fd, IoCall(call));
	}

	CallId callOnError(int fd, IoCall&& io_call) {
		return callNow([&] {
			auto& [call_id, channel_ptr] = *get(fd);
			std::weak_ptr<Channel> weak_channel_ptr = channel_ptr;
			channel_ptr->setErrorCallback([this, weak_channel_ptr = std::move(weak_channel_ptr), fd, call_id, io_call = std::move(io_call)] {
				auto call_status = io_call(fd, weak_channel_ptr);
				if (call_status == fantasy::Reactor::CallStatus::Remove)
					cancel(call_id);
			});
			return call_id;
		});
	}

	CallId callOnError(int fd, const IoCall& call) {
		return callOnError(fd, IoCall(call));
	}

	void cancel(const CallId& id) {
		switch (id.type()) {
		case CallType::IoCall: {
			callNow([&] {
				if (auto it = m_work_channels.find(id); it != m_work_channels.end()) {
					it->second->disableAll();
					m_release_channel.emplace_back(std::move(it->second));
					m_work_channels.erase(id);
				}
			});
			break;
		}
		case CallType::TimedCall: {
			callNow([&] {
				auto it = std::find_if(m_timed_callbacks.begin(), m_timed_callbacks.end(), [&](auto& callback) {
					return callback.second->id() == id;
				});
				if (it == m_timed_callbacks.end())
					return;
				m_timed_callbacks.erase(it);
			});
			break;
		}
		default:
			break;
		}
	}

	void run() {
		m_thread = std::thread([&] {
			m_thread_id = std::this_thread::get_id();
			auto timer_call_id = callOnRead(m_timerfd_ptr->fd(), [&](int fd, const std::weak_ptr<Channel>&) { handleRead(fd); return CallStatus::Ok; });
			auto call_id = callOnRead(m_wakeup_fd_ptr[0], [&](int fd, const std::weak_ptr<Channel>&) { wakeupRead(fd); return CallStatus::Ok; });
			try {
				for (;;) {
					auto active_channels = m_epoller_ptr->poll(-1);
					if (!active_channels)
						continue;
					for (auto& channel_ptr : active_channels.value())
						channel_ptr->handleEvent();
					m_release_channel.clear();
				}
			} catch (const Shutdown&) {
				std::lock_guard<std::mutex> lk(m_mtx);
				cancel(call_id);
				cancel(timer_call_id);
				for (auto& func : m_pending_functors)
					func();
			}
		});
	}

private:
	template <typename E>
	void addCallback(E&& callback) {
		std::lock_guard<std::mutex> lock(m_mtx);
		if (m_pending_functors.empty()) {
			const char c = '\0';
			if (write(m_wakeup_fd_ptr[1], &c, sizeof(c)) == -1 && m_error_callback)
				m_error_callback(__FILE__, __LINE__, errno);
		}
		m_pending_functors.emplace_back(std::forward<E>(callback));
	}

	void addTimedCallback(std::shared_ptr<Timer>&& timer_ptr) {
		auto time_point = timer_ptr->timePoint();
		auto it = m_timed_callbacks.emplace(time_point, std::move(timer_ptr));
		if (it == m_timed_callbacks.begin())
			m_timerfd_ptr->resetTimerfd(time_point);
	}

	void updateChannel(Channel* channel) {
		m_epoller_ptr->updateChannel(channel);
	}

	void wakeupRead(const int& fd) {
		char c;
		if (read(fd, &c, sizeof(c)) == -1 && m_error_callback)
			m_error_callback(__FILE__, __LINE__, errno);
		std::unique_lock<std::mutex> lk(m_mtx);
		auto pending_functors = std::move(m_pending_functors);
		lk.unlock();
		for (auto& func : pending_functors)
			func();
	}

	void handleRead(int) {
		m_timerfd_ptr->read();
		std::vector<std::shared_ptr<Timer>> repeat_call;
		auto now = std::chrono::system_clock::now();
		while (!m_timed_callbacks.empty() && m_timed_callbacks.begin()->first <= now) {
			auto callback = std::move(m_timed_callbacks.begin()->second);
			m_timed_callbacks.erase(m_timed_callbacks.begin());
			if (callback->run() == CallStatus::Ok)
				repeat_call.emplace_back(std::move(callback));
		}
		for (auto& call_ptr : repeat_call) {
			call_ptr->reset();
			m_timed_callbacks.emplace(call_ptr->timePoint(), std::move(call_ptr));
		}
		if (!m_timed_callbacks.empty())
			m_timerfd_ptr->resetTimerfd(m_timed_callbacks.begin()->first);
	}

	std::unique_ptr<Epoll> m_epoller_ptr;
	std::unique_ptr<int[], std::function<void(int*)>> m_wakeup_fd_ptr;
	std::thread::id m_thread_id;
	std::thread m_thread;
	std::vector<Functor> m_pending_functors;
	mutable std::mutex m_mtx;
	struct KeyHash {
		std::size_t operator()(const CallId& call_id) const {
			return std::hash<uint64_t>()(call_id.id());
		}
	};
	std::unordered_map<CallId, std::shared_ptr<Channel>, KeyHash> m_work_channels;
	std::vector<std::shared_ptr<Channel>> m_release_channel;
	std::function<void(const char*, int, int)> m_error_callback;
	std::unique_ptr<TimerFd> m_timerfd_ptr;
	std::multimap<std::chrono::system_clock::time_point, std::shared_ptr<Timer>> m_timed_callbacks;
};

} // namespace fantasy

#endif // _REACTOR_HPP_INCLUD_ED
