#include "../src/Physics/Physics2D.h"
#include <iostream>
#include <random>
#include <set>
#include <stdexcept>
using namespace Physics2D;
int checks = 0;
void Check(bool ok, const char* message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
bool Near(float a, float b, float tolerance = 1e-3f) { return std::abs(a - b) < tolerance; }
WorldShape Box(float x, float y, float w, float h, double angle = 0) {
    return MakeShape(TransformComponent({x, y}, {1, 1}, angle), ColliderComponent(w, h));
}
WorldShape Circle(float x, float y, float r) {
    return MakeShape(TransformComponent({x-r, y-r}), ColliderComponent::Circle(r));
}
int main() {
    try {
        Contact c, reverse;
        Check(Collide(Box(0, 0, 10, 10), Box(8, 0, 10, 10), c) && Near(c.penetration, 2) && c.normal.x > 0, "box overlap");
        Check(!Collide(Box(0, 0, 10, 10), Box(11, 0, 10, 10), c), "box separation");
        Check(Collide(Box(0, 0, 10, 10), Box(10, 0, 10, 10), c) && Near(c.penetration, 0), "touching contact");
        Check(Collide(Box(0, 0, 10, 10), Box(4, 4, 2, 2), c) && Near(c.penetration, 6), "box containment exit depth");
        const auto thin = Box(-5, -0.5f, 10, 1, 45);
        const auto other = Box(-5, 1.5f, 10, 1, 45);
        Check(Overlap(thin.bounds, other.bounds) && !Collide(thin, other, c), "SAT rejects overlapping AABBs");
        Check(Collide(thin, Box(-1, -1, 2, 2, -20), c), "rotated boxes hit");
        const auto scaled = MakeShape(TransformComponent({100, 200}, {2, 3}, 90), ColliderComponent(10, 4, {2, 1}), {10, 10});
        Check(Near(scaled.center.x, 141) && Near(scaled.center.y, 224), "offset scales and rotates around sprite pivot");
        Check(Near(scaled.bounds.max.x - scaled.bounds.min.x, 12), "rotated scaled bounds");
        Check(Collide(Circle(0, 0, 2), Circle(3, 0, 2), c) && Near(c.penetration, 1), "circle overlap");
        Check(Collide(Circle(0, 0, 2), Circle(0, 0, 2), c) && std::isfinite(c.normal.x), "coincident circles");
        Check(!Collide(Circle(0, 0, 2), Circle(5, 0, 2), c), "circle separation");
        Check(Collide(Box(0, 0, 10, 10), Circle(11, 5, 2), c) && Near(c.penetration, 1), "box circle face");
        Check(!Collide(Box(0, 0, 10, 10), Circle(11.5f, 11.5f, 2), c), "box circle corner miss");
        Check(Collide(Box(0, 0, 10, 10), Circle(5, 5, 1), c) && Near(c.penetration, 6), "circle contained in box");
        Collide(Box(0, 0, 10, 10, 25), Circle(10, 5, 2), c);
        Check(Collide(Circle(10, 5, 2), Box(0, 0, 10, 10, 25), reverse) && glm::length(c.normal + reverse.normal) < 1e-3f, "symmetric normals");
        Check(!Collide(Box(0, 0, 0, 10), Circle(0, 0, 2), c), "degenerate shape excluded");
        const auto negativeScale = MakeShape(TransformComponent({0, 0}, {-2, 3}), ColliderComponent(4, 6));
        Check(negativeScale.valid && Near(negativeScale.half.x, 4) && Near(negativeScale.center.x, -4), "negative scale geometry");
        const auto circleScale = MakeShape(TransformComponent({0, 0}, {2, 3}), ColliderComponent::Circle(2));
        Check(Near(circleScale.radius, 6), "nonuniform circle policy");
        ColliderComponent ca(1, 1), cb(1, 1);
        ca.category = 1; ca.mask = 2; cb.category = 2; cb.mask = 1;
        Check(ShouldCollide(ca, cb), "bidirectional mask accepts");
        cb.mask = 0; Check(!ShouldCollide(ca, cb), "bidirectional mask rejects");

        RigidBodyComponent a({10, 0}), b({0, 0});
        a.restitution = b.restitution = 1;
        c = {{1, 0}, 1};
        ResolveVelocity(&a, &b, c);
        Check(Near(a.velocity.x, 0) && Near(b.velocity.x, 10), "equal mass elastic exchange");
        a.velocity = {10, 0}; b.velocity = {0, 0}; a.mass = 2;
        ResolveVelocity(&a, &b, c);
        Check(Near(2*a.velocity.x + b.velocity.x, 20), "momentum conservation");
        Check(Near(2*a.velocity.x*a.velocity.x + b.velocity.x*b.velocity.x, 200, 0.01f), "elastic energy conservation");
        a.mass = 1; a.velocity = {10, 5}; a.restitution = 0;
        ResolveVelocity(&a, nullptr, c);
        Check(Near(a.velocity.x, 0) && a.velocity.y < 5, "static wall and friction");
        a.velocity = {-10, 3}; ResolveVelocity(&a, nullptr, c);
        Check(Near(a.velocity.x, -10) && Near(a.velocity.y, 3), "separating pair receives no impulse");
        b.type = BodyType::Kinematic; b.velocity = {-4, 0}; a.velocity = {0, 0};
        ResolveVelocity(&a, &b, c);
        Check(Near(b.velocity.x, -4) && Near(a.velocity.x, -4), "moving kinematic pushes dynamic");
        TransformComponent ta({0, 0}), tb({0, 0});
        CorrectPositions(ta, tb, &a, nullptr, {{1, 0}, 2});
        Check(ta.position.x < 0 && Near(tb.position.x, 0), "position correction moves only dynamic");
        a = RigidBodyComponent(); a.mass = 2; a.gravityScale = 1; a.force = {4, 0};
        ta = TransformComponent(); Integrate(ta, a, 0.5f, {0, 10});
        Check(Near(a.velocity.x, 1) && Near(a.velocity.y, 5) && Near(ta.position.y, 2.5f), "semi implicit force gravity integration");
        a.type = BodyType::Static; a.velocity = {100, 100};
        auto position = ta.position; Integrate(ta, a, 1, {0, 10});
        Check(ta.position == position, "static does not integrate");
        a.type = BodyType::Kinematic; Integrate(ta, a, 1, {0, 10});
        Check(Near(ta.position.x - position.x, 100) && Near(a.velocity.y, 100), "kinematic ignores forces");

        // Compare broad phase against brute force, including random rotations/circles.
        std::mt19937 random(42); std::uniform_real_distribution<float> dist(-50, 50);
        std::vector<WorldShape> shapes;
        for (int i = 0; i < 200; ++i) shapes.push_back(i % 2 ? Box(dist(random), dist(random), 10, 3, dist(random)) : Circle(dist(random), dist(random), 4));
        std::set<std::pair<std::size_t, std::size_t>> candidates;
        for (auto pair : Candidates(shapes)) candidates.emplace((std::min)(pair.first, pair.second), (std::max)(pair.first, pair.second));
        for (std::size_t i = 0; i < shapes.size(); ++i) for (std::size_t j = i+1; j < shapes.size(); ++j)
            Check(!Collide(shapes[i], shapes[j], c) || candidates.count({i, j}) != 0, "broad phase must not miss narrow phase hit");
        // Same simulated duration under several rendering rates.
        for (int fps : {30, 60, 144}) {
            FixedStepClock clock; RigidBodyComponent body({120, 0}); TransformComponent transform;
            int steps = 0;
            for (int frame = 0; frame < fps; ++frame) steps += clock.Advance(1.0/fps, [&](float dt) { Integrate(transform, body, dt, {0, 0}); });
            Check(steps == 120 && Near(transform.position.x, 120), "fixed step frame rate independence");
        }
        FixedStepClock clock;
        Check(clock.Advance(10, [](float) {}) == 8 && clock.Advance(0, [](float) {}) == 0, "catch-up bound drops overload");
        // Settling under gravity should not sink or gain bounce energy.
        ta = TransformComponent({0, 0}); a = RigidBodyComponent(); a.gravityScale = 1; a.restitution = 0;
        ColliderComponent box(10, 10); const auto floor = Box(-100, 20, 200, 10); tb = TransformComponent();
        for (int tick = 0; tick < 1200; ++tick) {
            Integrate(ta, a, 1.0f/120, {0, 980});
            for (int i = 0; i < 6; ++i) if (Collide(MakeShape(ta, box), floor, c)) {
                ResolveVelocity(&a, nullptr, c); CorrectPositions(ta, tb, &a, nullptr, c);
            }
        }
        Check(Near(ta.position.y, 10, 0.02f) && Near(a.velocity.y, 0), "resting body stable after ten seconds");
        std::cout << "PASS: " << checks << " physics checks\n";
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
