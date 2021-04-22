#include <iostream>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <spdlog/spdlog.h>

#include <reactor.hpp>

#define SERVER_PORT 20000
#define BUFFER_SIZE 255

int main() {
	auto reactor_ptr = std::make_unique<fantasy::Reactor>();
	reactor_ptr->run();
	std::thread([&] {
		reactor_ptr->callLater([&] {
			reactor_ptr->callLater([] {
				spdlog::info("callLater-2");
			});
			spdlog::info("callLater-1");
		});
	}).detach();
	spdlog::info("start callAt 1s");
	reactor_ptr->callAt(std::chrono::system_clock::now() + std::chrono::seconds(1), [] {
		spdlog::info("callAt");
	});
	spdlog::info("start callAfter 5s");
	reactor_ptr->callAfter(std::chrono::seconds(5), [] {
		spdlog::info("callAfter");
	});
	reactor_ptr->callEvery(std::chrono::seconds(1), [i = 0]() mutable {
		spdlog::info("callEvery: {}", i);
		if (i++ == 5)
			return fantasy::Reactor::CallStatus::Remove;
		return fantasy::Reactor::CallStatus::Ok;
	});
	auto x = fantasy::Time{5, 30, 0, 0};
	reactor_ptr->callEveryDay(x, [] {
		spdlog::info("callEveryDay");
		return fantasy::Reactor::CallStatus::Ok;
	});
	spdlog::info("start callNow");
	reactor_ptr->callNow([&] {
		spdlog::info("callNow sleep 3s");
		std::this_thread::sleep_for(std::chrono::seconds(3));
	});
	spdlog::info("end callNow");
	while (true)
		std::this_thread::sleep_for(std::chrono::seconds(1));
	return 0;
}