#include <arpa/inet.h>
#include <netinet/in.h>

#include <iostream>

#include <spdlog/spdlog.h>

#include <reactor.hpp>

#define SERVER_PORT 20000
#define LENGTH_OF_LISTEN_QUEUE 256
#define BUFFER_SIZE 255

int main() {
	fantasy::Reactor reactor;
	reactor.run();
	std::string recv_buffer;
	int servfd;
	struct sockaddr_in servaddr, cliaddr;
	if ((servfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		spdlog::info("create socket error!");
		exit(1);
	}
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(SERVER_PORT);
	servaddr.sin_addr.s_addr = htons(INADDR_ANY);
	int opt = 1;
	setsockopt(servfd, SOL_SOCKET, SO_REUSEADDR, (const void*)&opt, sizeof(opt));
	if (bind(servfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
		spdlog::info("bind to port {} failure!", SERVER_PORT);
		exit(1);
	}
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
	while (true)
		std::this_thread::sleep_for(std::chrono::seconds(1));
	return 0;
}