#include <sstream>

#include <spdlog/spdlog.h>

#include <reactor.hpp>

int main() {
	auto thread_pool = std::make_unique<fantasy::ThreadPool>(2);
	auto get_thread_id = [] {
		std::stringstream ss;
		ss << std::this_thread::get_id();
		return ss.str();
	};
	std::future<std::string> ret_1 = thread_pool->enqueue([&] {
		spdlog::info("1 thread_id: {}", get_thread_id());
		std::this_thread::sleep_for(std::chrono::seconds(1));
		return std::string{"hello"};
	});
	std::future<int32_t> ret_2 = thread_pool->enqueue([&] {
		spdlog::info("2 thread_id: {}", get_thread_id());
		std::this_thread::sleep_for(std::chrono::seconds(1));
		return 999;
	});
	std::future<double> ret_3 = thread_pool->enqueue([&] {
		spdlog::info("3 thread_id: {}", get_thread_id());
		std::this_thread::sleep_for(std::chrono::seconds(1));
		return 999.1;
	});
	spdlog::info("--------------------------");
	spdlog::info("ret_1: {}", ret_1.get());
	spdlog::info("ret_2: {}", ret_2.get());
	spdlog::info("ret_3: {}", ret_3.get());
	return 0;
}