#include <gtest/gtest.h>

#include <vector>

#include "pick-policy.hpp"
#include "server.hpp"

using load_balancer::ConsistentHashingPick;
using load_balancer::LeastConnectionsPick;
using load_balancer::RoundRobinPick;
using load_balancer::Server;
using load_balancer::ServerPtr;
using load_balancer::WeightRoundRobinPick;

namespace {
ServerPtr makeServer(uint32_t weight = 1) {
    return std::make_shared<Server>(weight, 1.0, 1);
}
}  // namespace

// RoundRobin без серверов возвращает nullopt.
TEST(RoundRobinPick, EmptyReturnsNullopt) {
    RoundRobinPick p;
    std::vector<ServerPtr> servers;
    EXPECT_FALSE(p.pickServer(0, servers).has_value());
}

// RoundRobin перебирает серверы по кругу.
TEST(RoundRobinPick, CyclesThroughServers) {
    RoundRobinPick p;
    std::vector<ServerPtr> servers{makeServer(), makeServer(), makeServer()};

    auto a = p.pickServer(0, servers).value();
    auto b = p.pickServer(0, servers).value();
    auto c = p.pickServer(0, servers).value();
    auto d = p.pickServer(0, servers).value();

    EXPECT_EQ(a->getId(), servers[0]->getId());
    EXPECT_EQ(b->getId(), servers[1]->getId());
    EXPECT_EQ(c->getId(), servers[2]->getId());
    EXPECT_EQ(d->getId(), servers[0]->getId());
}

// WeightRoundRobin отдаёт серверу столько подряд выборов, сколько его вес.
TEST(WeightRoundRobinPick, RespectsWeights) {
    WeightRoundRobinPick p;
    auto s0 = makeServer(2);
    auto s1 = makeServer(1);
    std::vector<ServerPtr> servers{s0, s1};

    EXPECT_EQ(p.pickServer(0, servers).value()->getId(), s0->getId());
    EXPECT_EQ(p.pickServer(0, servers).value()->getId(), s0->getId());
    EXPECT_EQ(p.pickServer(0, servers).value()->getId(), s1->getId());
    EXPECT_EQ(p.pickServer(0, servers).value()->getId(), s0->getId());
}

// LeastConnections выбирает сервер с наименьшим числом соединений.
TEST(LeastConnectionsPick, PicksLeastLoaded) {
    LeastConnectionsPick p;
    std::vector<ServerPtr> servers{makeServer(), makeServer()};
    // У свежих серверов 0 соединений — выбирается первый.
    EXPECT_EQ(p.pickServer(0, servers).value()->getId(), servers[0]->getId());
}

// LeastConnections без серверов возвращает nullopt.
TEST(LeastConnectionsPick, EmptyReturnsNullopt) {
    LeastConnectionsPick p;
    std::vector<ServerPtr> servers;
    EXPECT_FALSE(p.pickServer(0, servers).has_value());
}

// ConsistentHashing без ресета кольца (пустое кольцо) возвращает nullopt.
TEST(ConsistentHashingPick, EmptyRingReturnsNullopt) {
    ConsistentHashingPick<> p;
    std::vector<ServerPtr> servers{makeServer()};
    EXPECT_FALSE(p.pickServer(0, servers).has_value());
}

// ConsistentHashing одинаковому request_id всегда даёт один и тот же сервер.
TEST(ConsistentHashingPick, SameKeyStableServer) {
    ConsistentHashingPick<> p;
    std::vector<ServerPtr> servers{makeServer(), makeServer(), makeServer()};
    p.resetServers(servers);

    auto first = p.pickServer(42, servers);
    ASSERT_TRUE(first.has_value());
    for (int i = 0; i < 10; ++i) {
        auto again = p.pickServer(42, servers);
        ASSERT_TRUE(again.has_value());
        EXPECT_EQ(again.value()->getId(), first.value()->getId());
    }
}
