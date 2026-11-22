#pragma once
#include <pnes>
#include "modes/level/types.hpp"

class Actor {
    public:
    enum class eActorType {
        Player,
        Mushlet,

        End
    };

    struct ActorTypeMetadata_t {
        vec2<u8> collisionShape;
    };

    static constexpr ActorTypeMetadata_t ActorTypeMetadata[static_cast<size_t>(eActorType::End)] = {
        { {16, 16} }, // player
        { {16, 16} }, // mushlet
    };

    enum eActorState {
        Normal,     // default for that object (alive, moving whatever)
    };

    // Collision is a shared composite-metatile window read now, so an actor
    // carries no per-actor cursors: the old four static columns plus a dynamic
    // anchor lived in zero page on every actor.
    vec2<u16> worldSpace;          // top-left origin, sub-pixel: bottom 3 bits = subpx, so >>3 = px
    vec2<oam::oam_t> screen;       // on-screen sprite position (OAM coords)
    vec2<i8> moveForce;            // subpixel movement force (8 subpx = 1px)
    i8 gravity;                    // accumulated downward accel (subpx/frame); 0 when grounded
    eActorType actorType;
    eActorState actorState;
    const u8* animationFrame;
    u8  animationTimer;
    // False right, true left. A bool rather than the sign of moveForce.x, because
    // facing is an intent and velocity is not -- skidding or standing still
    // leaves an actor looking the way it last chose to.
    bool horizontalFacingDir;

    static constexpr vec2<u8> GetSize(const eActorType actorType) {
        return actorType < eActorType::End
            ? ActorTypeMetadata[static_cast<size_t>(actorType)].collisionShape
            : vec2<u8>{16, 16};
    }

    [[nodiscard]] constexpr vec2<u8> GetSize() const {
        return GetSize(this->actorType);
    }

    // Dispatched on actorType rather than per-actor function pointers: no hook
    // storage per actor and no indirect JSR per call.
    void Start();
    void Update();

    void Move(vec2<i8> delta);
};

// Metasprite field providers. `i` is the cell index, 2 wide by 4 tall.
oam::oam_t AdjustSpriteY(const Actor* self, u16 i);
oam::oam_t AdjustSpriteX(const Actor* self, u16 i);
