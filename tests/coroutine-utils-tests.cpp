#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <chrono>
#include <future>
#include <stdexcept>
#include <thread>

#include "coroutine-utils.hpp"

namespace {

// Гоняем awaitable до конца через io_context и забираем результат.
template <typename Awaitable, typename T>
void runAndCapture(Awaitable&& aw, T& out, std::exception_ptr& err) {
    asio::io_context io;
    asio::co_spawn(io,
                   [&]() -> asio::awaitable<void> {
                       try {
                           out = co_await std::move(aw);
                       } catch (...) {
                           err = std::current_exception();
                       }
                   },
                   asio::detached);
    io.run();
}

}  // namespace

// await_future возвращает значение уже готового future.
TEST(AwaitFuture, ReturnsReadyValue) {
    std::promise<int> p;
    p.set_value(42);

    int result = 0;
    std::exception_ptr err;
    runAndCapture(await_future(p.get_future()), result, err);

    EXPECT_FALSE(err);
    EXPECT_EQ(result, 42);
}

// await_future дожидается значения, выставленного из другого потока.
TEST(AwaitFuture, WaitsForDeferredValue) {
    std::promise<int> p;
    auto fut = p.get_future();

    std::thread setter([&p] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        p.set_value(7);
    });

    int result = 0;
    std::exception_ptr err;
    runAndCapture(await_future(std::move(fut)), result, err);
    setter.join();

    EXPECT_FALSE(err);
    EXPECT_EQ(result, 7);
}

// await_future пробрасывает исключение из future.
TEST(AwaitFuture, PropagatesException) {
    std::promise<int> p;
    p.set_exception(std::make_exception_ptr(std::runtime_error("boom")));

    int result = 0;
    std::exception_ptr err;
    runAndCapture(await_future(p.get_future()), result, err);

    ASSERT_TRUE(err);
    EXPECT_THROW(std::rethrow_exception(err), std::runtime_error);
}
