#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <future>
#include <thread>
#include <vector>

#include "server.hpp"

using namespace std::chrono_literals;
using load_balancer::Duration;
using load_balancer::Server;
using load_balancer::ServerCrashed;
using load_balancer::ServerOverloaded;
using load_balancer::Task;

namespace {

constexpr int kThreads = 10;
constexpr int kPerThread = 1000;

}  // namespace

// Нулевой вес недопустим — конструктор бросает исключение.
TEST(ServerBasic, ZeroWeightThrows) {
    EXPECT_THROW(Server(0, 1.0, 1), std::invalid_argument);
}

// Каждый сервер получает уникальный id.
TEST(ServerBasic, IdsAreUnique) {
    Server a(1, 1.0, 1);
    Server b(1, 1.0, 1);
    EXPECT_NE(a.getId(), b.getId());
}

// У свежесозданного сервера нет активных соединений.
TEST(ServerBasic, NoConnectsInitially) {
    Server s(1, 1.0, 1);
    EXPECT_EQ(s.getConnects(), 0u);
}

// getStats отражает параметры, переданные в конструктор.
TEST(ServerBasic, StatsReflectConstructorArgs) {
    Server s(5, 2.0, 3);
    auto stats = s.getStats();

    EXPECT_EQ(stats.id_, s.getId());
    EXPECT_EQ(stats.weight_, 5u);
    EXPECT_DOUBLE_EQ(stats.capacity_, 2.0);
    EXPECT_EQ(stats.max_parallel_requests_, 3u);
    EXPECT_EQ(stats.requests_received_, 0u);
    EXPECT_EQ(stats.successful_, 0u);
    EXPECT_EQ(stats.failed_, 0u);
}

// Одиночная задача выполняется и учитывается как успешная.
TEST(ServerSubmit, SingleTaskCompletes) {
    Server s(1, 1.0, 1);

    auto fut = s.submit(Task(0, 1.0L));
    Duration elapsed = fut.get();

    EXPECT_GT(elapsed, Duration::zero());

    auto stats = s.getStats();
    EXPECT_EQ(stats.successful_, 1u);
    EXPECT_EQ(stats.failed_, 0u);
}

// Пакет из нескольких задач выполняется полностью.
TEST(ServerSubmit, MultipleTasksAllComplete) {
    Server s(4, 4.0, 2);

    std::vector<std::future<Duration>> futures;
    for (int i = 0; i < 20; ++i) {
        futures.push_back(s.submit(Task(static_cast<uint64_t>(i), 0.5L)));
    }
    for (auto& f : futures) {
        EXPECT_NO_THROW(f.get());
    }

    auto stats = s.getStats();
    EXPECT_EQ(stats.successful_, 20u);
}

// Более дорогая задача обрабатывается дольше дешёвой.
TEST(ServerSubmit, HigherCostTakesLonger) {
    Server s(8, 8.0, 1);

    Duration cheap = s.submit(Task(0, 0.5L)).get();
    Duration expensive = s.submit(Task(1, 4.0L)).get();

    EXPECT_GT(expensive, cheap);
}

// Отправка задачи после краша приводит к ServerCrashed.
TEST(ServerCrash, SubmitAfterCrashFails) {
    Server s(1, 1.0, 1);
    s.crash();

    auto fut = s.submit(Task(0, 1.0L));
    EXPECT_THROW(fut.get(), ServerCrashed);
}

// Краш отклоняет задачи, ещё не взятые в работу, через ServerCrashed.
TEST(ServerCrash, PendingTasksRejectedOnCrash) {
    Server s(4, 4.0, 1);

    std::vector<std::future<Duration>> futures;
    for (int i = 0; i < 50; ++i) {
        futures.push_back(s.submit(Task(static_cast<uint64_t>(i), 2.0L)));
    }
    s.crash();

    int completed = 0;
    int crashed = 0;
    int overloaded = 0;
    for (auto& f : futures) {
        try {
            f.get();
            ++completed;
        } catch (const ServerCrashed&) {
            ++crashed;
        } catch (const ServerOverloaded&) {
            ++overloaded;
        }
    }

    EXPECT_EQ(completed + crashed + overloaded, 50);
    EXPECT_GT(crashed, 0);
}

// Повторный вызов crash() безопасен.
TEST(ServerCrash, IsIdempotent) {
    Server s(1, 1.0, 1);
    s.crash();
    EXPECT_NO_THROW(s.crash());
}

// id остаются уникальными при параллельном создании серверов.
TEST(ServerConcurrency, IdsAreUniqueAcrossThreads) {
    std::vector<std::vector<uint64_t>> per_thread(kThreads);

    {
        std::vector<std::jthread> threads;
        for (int t = 0; t < kThreads; ++t) {
            threads.emplace_back([&per_thread, t] {
                auto& ids = per_thread[t];
                ids.reserve(kPerThread);
                for (int i = 0; i < kPerThread; ++i) {
                    Server s(1, 1.0, 1);
                    ids.push_back(s.getId());
                }
            });
        }
    }

    std::vector<uint64_t> ids;
    ids.reserve(static_cast<size_t>(kThreads) * kPerThread);
    for (auto& chunk : per_thread) {
        ids.insert(ids.end(), chunk.begin(), chunk.end());
    }

    ASSERT_EQ(ids.size(), static_cast<size_t>(kThreads) * kPerThread);

    std::sort(ids.begin(), ids.end());
    auto dup = std::adjacent_find(ids.begin(), ids.end());
    EXPECT_EQ(dup, ids.end()) << "duplicate server id: " << *dup;
}

// После отклонённой задачи getConnects() должен вернуться к 0.
TEST(ServerConnects, ReturnsToZeroAfterRejectedTask) {
    Server s(1, 1.0, 1);

    auto fut = s.submit(Task(0, 1000.0L));
    EXPECT_THROW(fut.get(), ServerOverloaded);

    EXPECT_EQ(s.getConnects(), 0u);
}

// Стресс: при массовом параллельном submit каждый future разрешается ровно
TEST(ServerStress, ConcurrentSubmitsAllComplete) {
    Server s(8, 16.0, 8);

    std::vector<std::vector<std::future<Duration>>> per_thread(kThreads);

    {
        std::vector<std::jthread> threads;
        for (int t = 0; t < kThreads; ++t) {
            threads.emplace_back([&s, &per_thread, t] {
                auto& futures = per_thread[t];
                futures.reserve(kPerThread);
                for (int i = 0; i < kPerThread; ++i) {
                    auto id = static_cast<uint64_t>(t * kPerThread + i);
                    futures.push_back(s.submit(Task(id, 0.01L)));
                }
            });
        }
    }

    uint64_t resolved = 0;
    uint64_t succeeded = 0;
    for (auto& chunk : per_thread) {
        for (auto& f : chunk) {
            try {
                f.get();
                ++succeeded;
            } catch (const ServerOverloaded&) {
            }
            ++resolved;
        }
    }

    ASSERT_EQ(resolved, static_cast<uint64_t>(kThreads) * kPerThread);
    EXPECT_GT(succeeded, 0u);

    auto stats = s.getStats();
    EXPECT_EQ(stats.successful_ + stats.failed_, resolved);
}

// Стресс: краш во время массовой нагрузки — ни один future не зависает.
TEST(ServerStress, ConcurrentSubmitsSurviveCrash) {
    Server s(8, 16.0, 8);

    std::vector<std::vector<std::future<Duration>>> per_thread(kThreads);

    {
        std::vector<std::jthread> threads;
        for (int t = 0; t < kThreads; ++t) {
            threads.emplace_back([&s, &per_thread, t] {
                auto& futures = per_thread[t];
                futures.reserve(kPerThread);
                for (int i = 0; i < kPerThread; ++i) {
                    auto id = static_cast<uint64_t>(t * kPerThread + i);
                    futures.push_back(s.submit(Task(id, 1.0L)));
                }
            });
        }
    }

    s.crash();

    uint64_t resolved = 0;
    for (auto& chunk : per_thread) {
        for (auto& f : chunk) {
            try {
                f.get();
            } catch (const std::exception&) {
            }
            ++resolved;
        }
    }

    EXPECT_EQ(resolved, static_cast<uint64_t>(kThreads) * kPerThread);
}
