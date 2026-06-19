#include <gtest/gtest.h>

#include <vector>

#include "load-balancing.hpp"
#include "pick-policy.hpp"
#include "server.hpp"

using load_balancer::LoadBalancer;
using load_balancer::RoundRobinPick;
using load_balancer::Server;
using load_balancer::ServerPtr;

namespace {
ServerPtr makeServer() {
    return std::make_shared<Server>(1, 1.0, 1);
}
}  // namespace

// Без политики hasPickPolicy() == false, а pickServer возвращает nullptr.
TEST(LoadBalancer, NoPolicyByDefault) {
    LoadBalancer lb;
    std::vector<ServerPtr> servers{makeServer()};
    EXPECT_FALSE(lb.hasPickPolicy());
    EXPECT_EQ(lb.pickServer(0, servers), nullptr);
}

// После setPickPolicy hasPickPolicy() == true.
TEST(LoadBalancer, SetPolicyEnablesPicking) {
    LoadBalancer lb;
    lb.setPickPolicy<RoundRobinPick>();
    EXPECT_TRUE(lb.hasPickPolicy());

    std::vector<ServerPtr> servers{makeServer(), makeServer()};
    EXPECT_NE(lb.pickServer(0, servers), nullptr);
}

// С политикой и пустым списком серверов pickServer возвращает nullptr.
TEST(LoadBalancer, EmptyServersReturnsNull) {
    LoadBalancer lb;
    lb.setPickPolicy<RoundRobinPick>();
    std::vector<ServerPtr> servers;
    EXPECT_EQ(lb.pickServer(0, servers), nullptr);
}

// RoundRobin через LoadBalancer чередует серверы.
TEST(LoadBalancer, RoundRobinAlternates) {
    LoadBalancer lb;
    lb.setPickPolicy<RoundRobinPick>();
    std::vector<ServerPtr> servers{makeServer(), makeServer()};

    auto a = lb.pickServer(0, servers);
    auto b = lb.pickServer(0, servers);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_NE(a->getId(), b->getId());
}
