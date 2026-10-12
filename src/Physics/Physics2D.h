#pragma once
#include "../Components/ColliderComponent.h"
#include "../Components/RigidBodyComponent.h"
#include "../Components/TransformComponent.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace Physics2D {
constexpr float Epsilon = 1e-6f;
struct AABB { glm::vec2 min{0}, max{0}; };
struct WorldShape {
    ColliderShape type = ColliderShape::Box;
    glm::vec2 center{0}, half{0}, xAxis{1, 0}, yAxis{0, 1};
    float radius = 0;
    AABB bounds;
    bool valid = false;
};
struct Contact {
    glm::vec2 normal{1, 0}; // From A toward B; move A opposite this normal.
    float penetration = 0;
};
inline glm::vec2 Rotate(glm::vec2 p, glm::vec2 x, glm::vec2 y) { return x * p.x + y * p.y; }
inline glm::vec2 LocalCenter(const ColliderComponent& c) {
    return c.offset + (c.shape == ColliderShape::Circle ? glm::vec2(c.radius) : glm::vec2(c.width, c.height) * 0.5f);
}
// Local-pixel pivot: use sprite center to match SDL_RenderCopyEx.
inline WorldShape MakeShape(const TransformComponent& t, const ColliderComponent& c, glm::vec2 pivot) {
    WorldShape s;
    s.type = c.shape;
    const float angle = static_cast<float>(t.rotation * 3.14159265358979323846 / 180.0);
    s.xAxis = {std::cos(angle), std::sin(angle)};
    s.yAxis = {-s.xAxis.y, s.xAxis.x};
    s.center = t.position + pivot * t.scale + Rotate((LocalCenter(c) - pivot) * t.scale, s.xAxis, s.yAxis);
    s.half = glm::abs(glm::vec2(c.width, c.height) * t.scale) * 0.5f;
    // Nonuniform circle scale uses a conservative enclosing circle, not an ellipse.
    s.radius = c.radius * (std::max)(std::abs(t.scale.x), std::abs(t.scale.y));
    s.valid = c.shape == ColliderShape::Circle ? c.radius > 0 && s.radius > Epsilon
        : c.width > 0 && c.height > 0 && s.half.x > Epsilon && s.half.y > Epsilon;
    const glm::vec2 extent = c.shape == ColliderShape::Circle ? glm::vec2(s.radius)
        : glm::abs(s.xAxis) * s.half.x + glm::abs(s.yAxis) * s.half.y;
    s.bounds = {s.center - extent, s.center + extent};
    return s;
}
inline WorldShape MakeShape(const TransformComponent& t, const ColliderComponent& c) {
    return MakeShape(t, c, LocalCenter(c));
}
inline std::array<glm::vec2, 4> Corners(const WorldShape& s) {
    const auto x = s.xAxis * s.half.x, y = s.yAxis * s.half.y;
    return {s.center - x - y, s.center + x - y, s.center + x + y, s.center - x + y};
}
inline bool Overlap(const AABB& a, const AABB& b) {
    return a.min.x <= b.max.x && a.max.x >= b.min.x && a.min.y <= b.max.y && a.max.y >= b.min.y;
}
inline bool ShouldCollide(const ColliderComponent& a, const ColliderComponent& b) {
    return (a.category & b.mask) != 0 && (b.category & a.mask) != 0;
}
inline bool BoxBox(const WorldShape& a, const WorldShape& b, Contact& contact) {
    const auto delta = b.center - a.center;
    contact.penetration = (std::numeric_limits<float>::max)();
    for (auto axis : {a.xAxis, a.yAxis, b.xAxis, b.yAxis}) {
        const float ra = a.half.x * std::abs(glm::dot(a.xAxis, axis)) + a.half.y * std::abs(glm::dot(a.yAxis, axis));
        const float rb = b.half.x * std::abs(glm::dot(b.xAxis, axis)) + b.half.y * std::abs(glm::dot(b.yAxis, axis));
        const float distance = glm::dot(delta, axis);
        const float depth = ra + rb - std::abs(distance);
        if (depth < 0) return false;
        if (depth < contact.penetration) {
            contact.penetration = depth;
            contact.normal = distance < 0 ? -axis : axis;
        }
    }
    return true;
}
inline bool CircleCircle(const WorldShape& a, const WorldShape& b, Contact& contact) {
    const auto delta = b.center - a.center;
    const float distance2 = glm::dot(delta, delta), radius = a.radius + b.radius;
    if (distance2 > radius * radius) return false;
    const float distance = std::sqrt(distance2);
    contact.normal = distance > Epsilon ? delta / distance : glm::vec2(1, 0);
    contact.penetration = radius - distance;
    return true;
}
inline bool BoxCircle(const WorldShape& box, const WorldShape& circle, Contact& contact) {
    const auto delta = circle.center - box.center;
    const glm::vec2 local(glm::dot(delta, box.xAxis), glm::dot(delta, box.yAxis));
    const auto nearest = glm::clamp(local, -box.half, box.half);
    const auto difference = local - nearest;
    const float distance2 = glm::dot(difference, difference);
    if (distance2 > circle.radius * circle.radius) return false;
    if (distance2 > Epsilon * Epsilon) {
        const float distance = std::sqrt(distance2);
        contact.normal = Rotate(difference / distance, box.xAxis, box.yAxis);
        contact.penetration = circle.radius - distance;
    } else {
        // Containment: select the nearest exit face, not a zero-length normal.
        const auto remaining = box.half - glm::abs(local);
        if (remaining.x < remaining.y) {
            contact.normal = box.xAxis * (local.x < 0 ? -1.0f : 1.0f);
            contact.penetration = circle.radius + remaining.x;
        } else {
            contact.normal = box.yAxis * (local.y < 0 ? -1.0f : 1.0f);
            contact.penetration = circle.radius + remaining.y;
        }
    }
    return true;
}
inline bool Collide(const WorldShape& a, const WorldShape& b, Contact& contact) {
    if (!a.valid || !b.valid || !Overlap(a.bounds, b.bounds)) return false;
    if (a.type == ColliderShape::Box && b.type == ColliderShape::Box) return BoxBox(a, b, contact);
    if (a.type == ColliderShape::Circle && b.type == ColliderShape::Circle) return CircleCircle(a, b, contact);
    if (a.type == ColliderShape::Box) return BoxCircle(a, b, contact);
    const bool hit = BoxCircle(b, a, contact);
    contact.normal = -contact.normal;
    return hit;
}
// Sort-and-sweep on world AABBs. Dense worst case remains O(n^2).
inline std::vector<std::pair<std::size_t, std::size_t>> Candidates(const std::vector<WorldShape>& shapes) {
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < shapes.size(); ++i) if (shapes[i].valid) order.push_back(i);
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return shapes[a].bounds.min.x == shapes[b].bounds.min.x ? a < b : shapes[a].bounds.min.x < shapes[b].bounds.min.x;
    });
    std::vector<std::pair<std::size_t, std::size_t>> pairs;
    for (std::size_t i = 0; i < order.size(); ++i) {
        const auto a = order[i];
        for (std::size_t j = i + 1; j < order.size(); ++j) {
            const auto b = order[j];
            if (shapes[b].bounds.min.x > shapes[a].bounds.max.x) break;
            if (Overlap(shapes[a].bounds, shapes[b].bounds)) pairs.emplace_back(a, b);
        }
    }
    return pairs;
}
inline void Integrate(TransformComponent& t, RigidBodyComponent& b, float dt, glm::vec2 gravity) {
    if (b.type == BodyType::Static || (b.type == BodyType::Dynamic && b.InverseMass() == 0)) return;
    if (b.type == BodyType::Dynamic) {
        b.velocity += (b.force * b.InverseMass() + gravity * b.gravityScale) * dt;
        b.velocity /= 1.0f + (std::max)(0.0f, b.linearDamping) * dt;
    }
    t.position += b.velocity * dt;
}
inline glm::vec2 Velocity(const RigidBodyComponent* b) {
    return b && b->type != BodyType::Static && (b->type != BodyType::Dynamic || b->InverseMass() > 0) ? b->velocity : glm::vec2(0);
}
inline void ResolveVelocity(RigidBodyComponent* a, RigidBodyComponent* b, const Contact& c) {
    const float ia = a ? a->InverseMass() : 0, ib = b ? b->InverseMass() : 0, sum = ia + ib;
    if (sum <= 0) return;
    auto relative = Velocity(b) - Velocity(a);
    const float speed = glm::dot(relative, c.normal);
    if (speed >= 0) return;
    const float restitution = (std::min)(a ? glm::clamp(a->restitution, 0.0f, 1.0f) : 1.0f,
                                        b ? glm::clamp(b->restitution, 0.0f, 1.0f) : 1.0f);
    // Suppress tiny bounces for resting contacts, in pixels/second.
    const float impulse = -(1 + (speed < -1.0f ? restitution : 0.0f)) * speed / sum;
    if (a) a->velocity -= c.normal * (impulse * ia);
    if (b) b->velocity += c.normal * (impulse * ib);
    relative = Velocity(b) - Velocity(a);
    auto tangent = relative - c.normal * glm::dot(relative, c.normal);
    const float length = glm::length(tangent);
    if (length <= Epsilon) return;
    tangent /= length;
    const float friction = std::sqrt((std::max)(0.0f, a ? a->friction : 0.4f) * (std::max)(0.0f, b ? b->friction : 0.4f));
    const float jt = glm::clamp(-glm::dot(relative, tangent) / sum, -impulse * friction, impulse * friction);
    if (a) a->velocity -= tangent * (jt * ia);
    if (b) b->velocity += tangent * (jt * ib);
}
inline void CorrectPositions(TransformComponent& a, TransformComponent& b,
                             const RigidBodyComponent* ba, const RigidBodyComponent* bb, const Contact& c) {
    const float ia = ba ? ba->InverseMass() : 0, ib = bb ? bb->InverseMass() : 0;
    if (ia + ib <= 0) return;
    const auto correction = c.normal * ((std::max)(c.penetration - 0.01f, 0.0f) * 0.8f / (ia + ib));
    a.position -= correction * ia;
    b.position += correction * ib;
}
class FixedStepClock {
    double accumulator = 0;
public:
    static constexpr double Step = 1.0 / 120.0;
    static constexpr int MaxSteps = 8;
    template<class F> int Advance(double frameTime, F&& step) {
        accumulator += (std::max)(0.0, (std::min)(frameTime, 0.25));
        int count = 0;
        while (accumulator + 1e-12 >= Step && count < MaxSteps) {
            step(static_cast<float>(Step));
            accumulator -= Step;
            ++count;
        }
        if (accumulator >= Step) accumulator = std::fmod(accumulator, Step);
        return count;
    }
};
}
