#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <future>
#include <limits>
#include <stdexcept>

#include "task.hpp"

using load_balancer::Duration;
using load_balancer::Task;
using load_balancer::TaskItem;

namespace {

constexpr long double kCostTolerance = 1e-9L;

void ExpectCostEq(const Task& task, long double expected) {
    EXPECT_NEAR(task.getCost(), expected, kCostTolerance);
}

}  // namespace

// Конструктор сохраняет переданные id и cost.
TEST(TaskConstructor, StoresIdAndCost) {
    Task task(42, 3.5L);

    EXPECT_EQ(task.getId(), 42u);
    ExpectCostEq(task, 3.5L);
}

// Конструктор принимает нулевой id.
TEST(TaskConstructor, AcceptsZeroId) {
    Task task(0, 1.0L);

    EXPECT_EQ(task.getId(), 0u);
    ExpectCostEq(task, 1.0L);
}

// Конструктор принимает максимальный uint64_t id без потери значения.
TEST(TaskConstructor, AcceptsMaxId) {
    constexpr uint64_t kMaxId = std::numeric_limits<uint64_t>::max();
    Task task(kMaxId, 0.0L);

    EXPECT_EQ(task.getId(), kMaxId);
}

// Конструктор принимает нулевую стоимость.
TEST(TaskConstructor, AcceptsZeroCost) {
    Task task(1, 0.0L);

    ExpectCostEq(task, 0.0L);
}

// Конструктор принимает отрицательную стоимость.
TEST(TaskConstructor, AcceptsNegativeCost) {
    Task task(1, -4.25L);

    ExpectCostEq(task, -4.25L);
}

// Конструктор сохраняет дробную стоимость без потери точности.
TEST(TaskConstructor, AcceptsFractionalCost) {
    Task task(1, 0.1L);

    ExpectCostEq(task, 0.1L);
}

// setId меняет id и не затрагивает cost.
TEST(TaskSetId, UpdatesIdWithoutTouchingCost) {
    Task task(1, 3.5L);

    task.setId(7);

    EXPECT_EQ(task.getId(), 7u);
    ExpectCostEq(task, 3.5L);
}

// Повторный setId перезаписывает предыдущее значение id.
TEST(TaskSetId, OverwritesPreviousValue) {
    Task task(1, 0.0L);

    task.setId(7);
    task.setId(99);

    EXPECT_EQ(task.getId(), 99u);
}

// setCost меняет cost и не затрагивает id.
TEST(TaskSetCost, UpdatesCostWithoutTouchingId) {
    Task task(1, 3.5L);

    task.setCost(12.25L);

    EXPECT_EQ(task.getId(), 1u);
    ExpectCostEq(task, 12.25L);
}

// Повторный setCost перезаписывает предыдущее значение cost.
TEST(TaskSetCost, OverwritesPreviousValue) {
    Task task(1, 3.5L);

    task.setCost(7.75L);
    task.setCost(-2.0L);

    ExpectCostEq(task, -2.0L);
}

// TaskItem хранит Task и доставляет Duration через promise/future.
TEST(TaskItemBasic, HoldsTaskAndDeliversDurationThroughPromise) {
    TaskItem item{Task(5, 2.5L), std::promise<Duration>{}};

    EXPECT_EQ(item.task.getId(), 5u);
    ExpectCostEq(item.task, 2.5L);

    std::future<Duration> future = item.promise.get_future();
    item.promise.set_value(Duration(150));

    EXPECT_EQ(future.get(), Duration(150));
}

// TaskItem пробрасывает исключение из promise в future.
TEST(TaskItemBasic, PropagatesExceptionThroughPromise) {
    TaskItem item{Task(1, 0.0L), std::promise<Duration>{}};

    std::future<Duration> future = item.promise.get_future();
    item.promise.set_exception(
        std::make_exception_ptr(std::runtime_error("failed")));

    EXPECT_THROW(future.get(), std::runtime_error);
}
