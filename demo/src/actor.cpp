#include "actor.hpp"
#include "modes/level.hpp"
#include "modes/level/levels.hpp"
#include "modes/level/collision_map.hpp"
#include "banks.hpp"   // ACTORS

#include "../animations/gen/mary_standing.hpp"

static void TranslateWorldSpace(vec2<u16>& worldSpace, vec2<i8> delta);

// ACTORS: Actor is global-namespace, so without this it would fall into the
// resident bank rather than the actor bank.
ACTORS void Actor::Move(const vec2<i8> delta /* sub px */) {
    TranslateWorldSpace(worldSpace, delta);

    const bool inField = !(worldSpace.y & 0x8000)
                       && (worldSpace.y >> 7) < static_cast<u16>(level::levelHeight);

    if (inField) {
        const bool solid = level::Blocked(worldSpace.x >> 3, worldSpace.y >> 3,
                                                GetSize(), /*collectBlocks=*/false);
        (void)solid;   // TODO: collision response
    }
}

static void TranslateWorldSpace(vec2<u16>& worldSpace, const vec2<i8> delta) {
    if (delta.x > 0) {
        if ((worldSpace.x + delta.x < worldSpace.x) || ((worldSpace.x + delta.x) >> 7 >= level::nColumns)) {
            worldSpace.x = (level::nColumns - 1) << 7;
        } else {
            worldSpace.x += delta.x;
        }
    } else if (delta.x < 0) {
        if (worldSpace.x + delta.x > worldSpace.x) {   // underflow past column 0
            worldSpace.x = 0;
        } else {
            worldSpace.x += delta.x;
        }
    }

    worldSpace.y += delta.y;
}

ACTORS void Actor::Start() {
    // Outside the switch, since the field is on Actor rather than the player:
    // an actor type that forgets to set it should still start somewhere defined.
    this->horizontalFacingDir = false;   // right, the way the art is drawn

    switch (this->actorType) {
        case eActorType::Player:
            this->animationFrame = gen::mary_standing::Get();
            this->animationTimer = 0;   // spent: the first step of a walk is immediate
            break;
        case eActorType::Mushlet:
            break;
        case eActorType::End:
            break;
    }
}
ACTORS void Actor::Update() {
    switch (this->actorType) {
        case eActorType::Player:
            break;
        case eActorType::Mushlet:
            break;
        case eActorType::End:
            break;
    }
}

oam::oam_t AdjustSpriteY(const Actor* self, const u16) {
    return static_cast<oam::oam_t>(self->screen.y);
}

oam::oam_t AdjustSpriteX(const Actor* self, const u16 i) {
    return static_cast<oam::oam_t>(self->screen.x + (i & 1) * 8);
}