-- Launch with --physics-demo; D toggles collider outlines, Esc exits.
-- Reuse the existing map/assets, but replace the entities with a physics sandbox.
dofile("./assets/scripts/level1.lua")
Level.entities = {
    [0] = { components = {
        transform = { position = { x = 100, y = 500 }, scale = { x = 1, y = 1 }, rotation = 5 },
        rigidbody = { type = "static", friction = 0.6, restitution = 0.7 },
        collider = { shape = "box", width = 640, height = 24 }
    } },
    [1] = { components = {
        transform = { position = { x = 230, y = 180 }, scale = { x = 1, y = 1 }, rotation = 30 },
        rigidbody = { mass = 2, gravity_scale = 1, restitution = 0.6, friction = 0.4 },
        collider = { shape = "box", width = 40, height = 40 }
    } },
    [2] = { components = {
        transform = { position = { x = 340, y = 100 }, scale = { x = 1, y = 1 }, rotation = 0 },
        rigidbody = { mass = 1, gravity_scale = 1, restitution = 0.7, friction = 0.3 },
        collider = { shape = "circle", radius = 18 }
    } },
    [3] = { components = {
        transform = { position = { x = 440, y = 250 }, scale = { x = 1.5, y = 1 }, rotation = -20 },
        rigidbody = { mass = 3, gravity_scale = 1, restitution = 0.2, friction = 0.7 },
        collider = { shape = "box", width = 40, height = 30 }
    } },
    [4] = { components = {
        transform = { position = { x = 600, y = 100 }, scale = { x = 1, y = 1 }, rotation = 0 },
        collider = { shape = "box", width = 30, height = 380, is_trigger = true }
    } }
}
