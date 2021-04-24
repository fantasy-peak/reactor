# reactor

A C++17 single-file header-only, based on reactor mode, It can add tasks and timers and file descriptor to reactor(one loop one thread)

Simple examples
---------------

#### add a task
```c++
fantasy::Reactor reactor;
reactor.run();

// It will run on the reactor thread, do not block the current thread
reactor.callLater([&] {
    printf("task");
});

// It will run on the reactor thread, block the current thread
reactor.callNow([&] {
    printf("task");
});

```

#### add/remove a timed task
```c++
fantasy::Reactor reactor;
reactor.run();

// It will run in one second
reactor.callAt(std::chrono::system_clock::now() + std::chrono::seconds(1), [] {
    printf("callAt");
});

// It will run in five second
reactor.callAfter(std::chrono::seconds(5), [] {
    printf("callAfter");
});

// Run every three seconds
reactor.callEvery(std::chrono::seconds(3), [] {
    printf("callEvery");
    return fantasy::Reactor::CallStatus::Ok;
});

// Run every day 05:30:00
auto id = reactor.callEveryDay(fantasy::Time{5, 30, 0, 0}, [] {
    printf("callEveryDay");
    return fantasy::Reactor::CallStatus::Ok;
});

// cancel scheduled tasks
reactor.cancel(id);

```
#### add file descriptor to reactor for read && write
```c++
fantasy::Reactor reactor;
reactor.run();
...
if (listen(servfd, LENGTH_OF_LISTEN_QUEUE) < 0) {
    spdlog::info("call listen failure!");
    exit(1);
}
reactor.callOnRead(servfd, [&](int fd, const std::weak_ptr<fantasy::Reactor::Channel>&) mutable {
    socklen_t length = sizeof(cliaddr);
    int clifd = ::accept4(fd, (struct sockaddr*)&cliaddr, &length, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (clifd < 0) {
        spdlog::error("error comes when call accept!");
        return fantasy::Reactor::CallStatus::Ok;
    }
    reactor.callOnRead(clifd, [&](int fd, const std::weak_ptr<fantasy::Reactor::Channel>& channel_ptr) mutable {
        spdlog::info("call callOnRead");
        char buffer[BUFFER_SIZE] = {};
        auto n = read(fd, buffer, BUFFER_SIZE);
        if (n < 0) {
            perror("read()");
            return fantasy::Reactor::CallStatus::Remove;
        };
        if (n == 0) {
            spdlog::error("client close");
            return fantasy::Reactor::CallStatus::Remove;
        }
        spdlog::info("read: [{}], read buffer len: {}", buffer, n);
        recv_buffer = std::string{buffer};
        if (auto spt = channel_ptr.lock())
            spt->enableWriting();
        return fantasy::Reactor::CallStatus::Ok;
    });
    reactor.callOnWrite(clifd, [&](int fd, const std::weak_ptr<fantasy::Reactor::Channel>& channel_ptr) {
        spdlog::info("callOnWrite");
        char buffer[BUFFER_SIZE] = {};
        memcpy(buffer, recv_buffer.c_str(), recv_buffer.size());
        spdlog::info("buffer: {}", buffer);
        auto n = write(fd, buffer, strlen(buffer));
        if (n < 0) {
            perror("write()");
            exit(1);
        }
        if (auto spt = channel_ptr.lock())
            spt->disableWriting();
        return fantasy::Reactor::CallStatus::Ok;
    });
    return fantasy::Reactor::CallStatus::Ok;
});
```

## Documentation
You can use connection pool and client separately
* See [examples](https://github.com/fantasy-peak/reactor/tree/main/example)

## Maintainers

[@fantasy-peak](https://github.com/fantasy-peak)

