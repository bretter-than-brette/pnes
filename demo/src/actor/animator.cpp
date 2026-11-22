#include "animator.hpp"

#include "../banks.hpp"   // ACTORS
#include "../types.hpp"   // demo::Framerated

#include "../../animations/gen/mary_standing.hpp"
#include "../../animations/gen/mary_walking.hpp"

namespace demo::actor {

// Walks an actor through its generated animation frames, swapping the metasprite
// and advancing on the frame counts the .anim file carries.
AI static inline u8 WalkCycleBudget() { return demo::Framerated<u8, 80>::ScaleDown(); }

ACTORS void NextFrame(Actor* self, const bool grounded) {
    using walk = gen::mary_walking;

    if (!grounded) {
        self->animationFrame = walk::kTiles_frame_1;
        self->animationTimer = WalkCycleBudget();
        return;
    }

    const i16 vx    = self->moveForce.x;
    const u8  speed = static_cast<u8>(vx < 0 ? -vx : vx);

    if (speed == 0) {
        self->animationFrame = gen::mary_standing::Get();
        self->animationTimer = 0;
        return;
    }

    if (self->animationTimer > speed) {
        self->animationTimer -= speed;
        return;
    }

    self->animationTimer = WalkCycleBudget();

    if (self->animationFrame == walk::kTiles_frame_1)
        self->animationFrame = walk::kTiles_frame_2;
    else if (self->animationFrame == walk::kTiles_frame_2)
        self->animationFrame = walk::kTiles_frame_3;
    else
        self->animationFrame = walk::kTiles_frame_1;
}

} // namespace demo::actor
