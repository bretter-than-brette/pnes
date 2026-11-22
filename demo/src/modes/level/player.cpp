#include "player.hpp"

#include "../../technology.hpp"
#include "../../graphics/metatiles.hpp"
#include "collision_map.hpp"
#include "levels.hpp"
#include "../level.hpp"
#include "../../banks.hpp"   // ACTORS
#include "../../actor/animator.hpp"
#include <pnes/extras/debug/emphasis.hpp>

using namespace demo;
using enum level::eLevelStreamCommands;

namespace level {

// Player physics and the collision queries it drives. Runs three times per
// frame against collision_map's occupancy planes.
#ifdef TARGET_NES
#define pending_cold __attribute__((section(".bss")))
#else
#define pending_cold
#endif

enum class ePendingColumn : u8 { NONE = 0, NEXT, PREV };

pending_cold static ePendingColumn pendingColumnKind;
pending_cold static u16            pendingColumnIndex;

ACTORS static u8 PlayerPort(const Player* p)     { return p == &player1 ? port1     : port2;     }
ACTORS static u8 PlayerLastPort(const Player* p) { return p == &player1 ? lastPort1 : lastPort2; }

ACTORS static void PlayerFollowCamera(Player* p) {
    Actor*       self = &p->actor;
    const Actor* lead = &player1.actor;
    const auto   maxX = static_cast<i16>(video::viewport_px() - 16);

    const i16 delta = static_cast<i16>(self->worldSpace.x - lead->worldSpace.x);
    i16       sx    = static_cast<i16>(lead->screen.x) + static_cast<i16>(delta >> 3);

    if (sx < 0 || sx > maxX) {
        sx = sx < 0 ? 0 : maxX;
        self->worldSpace.x = static_cast<u16>(lead->worldSpace.x
            + static_cast<u16>(static_cast<i16>(sx - static_cast<i16>(lead->screen.x)) << 3));
    }

    self->screen.x = static_cast<oam::oam_t>(sx);
}


ACTORS static u16 ActorTX(const Actor* self) { return self->worldSpace.x >> 3; }

ACTORS static bool IsBlocked(const Actor* self, const u16 wx, const u16 wy) {
    return level::Blocked(wx, wy, self->GetSize(), /*collectBlocks=*/false);
}

ACTORS static void PushCoinVram(const CoinPick& pick) {
    const u8  m  = pick.reveal;
    const u16 tx = static_cast<u16>(pick.col) << 1;
    const u16 ty = static_cast<u16>(2 + (pick.row << 1));
#ifdef TARGET_NES
    const u16 nt_h = static_cast<u16>((tx >> 5) & 1) << 10;
    const u16 base = 0x2000 + nt_h + (static_cast<u16>(ty) << 5) + (tx & 0x1F);
    CoinVramPush(base,      Metatiles_UL[m], Metatiles_UR[m]);
    CoinVramPush(base + 32, Metatiles_BL[m], Metatiles_BR[m]);
#else
    CoinVramPush(ppu::CartesianToAddress({tx,     ty}),     Metatiles_UL[m]);
    CoinVramPush(ppu::CartesianToAddress({static_cast<u16>(tx + 1), ty}),     Metatiles_UR[m]);
    CoinVramPush(ppu::CartesianToAddress({tx,     static_cast<u16>(ty + 1)}), Metatiles_BL[m]);
    CoinVramPush(ppu::CartesianToAddress({static_cast<u16>(tx + 1), static_cast<u16>(ty + 1)}), Metatiles_BR[m]);
#endif
}

ACTORS static void CollectCoinsFor(const Actor* self, const Player* p) {
    const u16 px = ActorTX(self);
    const u16 py = self->screen.y;
    bool got;
    if (p == &player1) {
        if (CoinGateSkips(coinGate1, px, py)) return;
        got = CollectCoins(px, py, self->GetSize());
    } else {
        if (CoinGateSkips(coinGate2, px, py)) return;
        got = CollectCoins2(px, py, self->GetSize());
    }
    if (got) PushCoinVram(coinPick);
}

ACTORS static void ProcessMovement(Player* p, const vec2<i8> moveForce, const u16 tx,
                                   const bool blockedBelow) {
    Actor* self = &p->actor;
    const auto playerMaxXPos = (video::viewport_px() - 16);
    {
        const int rawY = static_cast<int>(self->worldSpace.y) + moveForce.y;
        const i16 dy   = static_cast<i16>(rawY >> 3) - static_cast<i16>(self->worldSpace.y >> 3);
        if (dy) {
            const u16 ny = static_cast<u16>(self->screen.y) + dy;
            const bool hit = dy == 1 ? blockedBelow : IsBlocked(self, tx, ny);
            if (!hit) {
                self->worldSpace.y = static_cast<u16>(rawY) & 0x7ff;
                self->screen.y     = static_cast<oam::oam_t>(self->worldSpace.y >> 3);
            } else {
                self->worldSpace.y &= ~0x7;   // bonk: rest on the pixel, drop sub-px carry
                self->moveForce.y = 0;        // land or head-bonk both kill vertical speed
            }
        } else {
            self->worldSpace.y = static_cast<u16>(rawY) & 0x7ff;
        }
    }

    if (!moveForce.x) return;

    const int rawX = static_cast<int>(self->worldSpace.x) + moveForce.x;
    const i16 dx   = static_cast<i16>(rawX >> 3) - static_cast<i16>(self->worldSpace.x >> 3);
    if (!dx) {
        if (rawX >= 0) self->worldSpace.x = static_cast<u16>(rawX);
        return;
    }

    if (rawX < 0 || (dx < 0 && tx == 0)) { self->moveForce.x = 0; return; }
    if (IsBlocked(self, static_cast<u16>(static_cast<i16>(tx) + dx), self->screen.y)) {
        self->moveForce.x = 0;
        return;
    }

    if (p == &player1) {
        const auto playerScrollPos = (video::viewport_px() >> 1);
        const u16 maxCam = maxCameraX;   // level-constant, hoisted -- see levels.hpp
        i16       sx     = static_cast<i16>(self->screen.x) + dx;
        const i16 excess = sx - static_cast<i16>(playerScrollPos);

        i16 scroll = 0;
        if (excess > 0) {
            const u16 room = cameraX < maxCam ? maxCam - cameraX : 0;
            scroll = static_cast<u16>(excess) <= room ? excess : static_cast<i16>(room);
        } else if (excess < 0) {
            const u16 want = static_cast<u16>(-excess);
            scroll = want <= cameraX ? excess : -static_cast<i16>(cameraX);
        }

        if (scroll) {
            cameraX = static_cast<u16>(static_cast<i16>(cameraX) + scroll);
            sx     -= scroll;

            if (scroll > 0) {
                if (cameraX >= lastXWorldSpace + 16 && lastXWorldSpace + 16 != maxCam) {
                    levelStreamCommand = STREAM_LEVEL_RIGHT;
                    if (lastDeltaScroll < 0)
                        levelStreamCommand = levelStreamCommand | STREAM_LEVEL_SWAP;
                    pendingColumnIndex = ((lastXWorldSpace + 16) >> 4) + viewport_mx();
                    pendingColumnKind  = ePendingColumn::NEXT;
                    lastDeltaScroll = static_cast<i8>(scroll);
                    lastXWorldSpace += 16;
                }
            } else if (cameraX <= lastXWorldSpace - 16 && lastXWorldSpace != 16) {
                levelStreamCommand = STREAM_LEVEL_LEFT;
                if (lastDeltaScroll > 0)
                    levelStreamCommand = levelStreamCommand | STREAM_LEVEL_SWAP;
                lastDeltaScroll = static_cast<i8>(scroll);
                pendingColumnIndex = ((lastXWorldSpace - 16) >> 4) - 1;
                pendingColumnKind  = ePendingColumn::PREV;
                lastXWorldSpace -= 16;
            }
        }

        if (sx < 0) sx = 0;
        else if (sx > static_cast<i16>(playerMaxXPos)) sx = static_cast<i16>(playerMaxXPos);
        self->screen.x = static_cast<oam::oam_t>(sx);
        self->worldSpace.x = static_cast<u16>(((cameraX + self->screen.x) << 3) | (static_cast<u16>(rawX) & 7));
    } else {
        self->worldSpace.x = static_cast<u16>(rawX);
        PlayerFollowCamera(p);
    }
}

ACTORS void Player::Update() {
    if (paused) return;
    Actor* self = &actor;

    debug::SetColour(ppu::mask::RED);
    if (this != &player1) PlayerFollowCamera(this);

    const u16 tx  = ActorTX(self);
    debug::SetColour(ppu::mask::GREEN);
    const bool blockedBelow = IsBlocked(self, tx, static_cast<u16>(self->screen.y + 1));
    bool grounded = blockedBelow;

    const u8   port     = PlayerPort(this);
    const u8   lastPort = PlayerLastPort(this);
    const bool left     = port & input::LEFT;
    const bool right    = port & input::RIGHT;
    const i8   dir      = (right && !left) ? 1 : ((left && !right) ? -1 : 0);

    if (grounded && dir != 0) self->horizontalFacingDir = dir < 0;

    const i8 vx0 = self->moveForce.x;
    i8       vx  = vx0;

    if ((port & input::B) && grounded && dir != 0 && (vx == 0 || (vx > 0) == (dir > 0)))
        runTimer = RunRetain();
    else if (runTimer)
        runTimer--;
    const i8 maxSpeed = runTimer ? MaxRun() : MaxWalk();

    if (dir != 0) {
        const auto dirForce = [dir](i16 k) -> i16 { return dir > 0 ? k : static_cast<i16>(-k); };
        const bool sameDir = vx == 0 || (vx > 0) == (dir > 0);
        if (!sameDir)
            xForce += dirForce(grounded ? Skid() : AirAccel());
        else if (Abs(vx) < maxSpeed)
            xForce += dirForce(grounded ? (runTimer ? RunAccel() : WalkAccel()) : AirAccel());
        else if (Abs(vx) > maxSpeed && grounded)
            xForce += dir > 0 ? -Friction() : Friction();
    } else if (grounded && vx != 0) {
        xForce += vx > 0 ? -Friction() : Friction();
    }

    while (xForce >= 256)  { xForce -= 256; if (vx < MaxRun())  vx++; }
    while (xForce <= -256) { xForce += 256; if (vx > -MaxRun()) vx--; }

    if (dir == 0 && grounded && vx0 != 0 && (vx0 > 0) != (vx > 0)) {
        vx = 0;
        xForce = 0;
    }
    self->moveForce.x = vx;

    if (grounded && (port & ~lastPort & input::A)) {
        self->moveForce.y = static_cast<i8>(-(Abs(vx) >= MaxWalk() ? RunJumpInit() : JumpInit()));
        yForce = 0;
        grounded = false;
    }
    if (grounded) {
        self->moveForce.y = 0;
        yForce = 0;
    }

    const bool rising = self->moveForce.y < 0;
    yForce += (rising && (port & input::A)) ? RiseGravity() : FallGravity();
    i8 vy = self->moveForce.y;
    {
        const i8 ticks = static_cast<i8>(static_cast<u16>(yForce) >> 8);
        yForce &= 0xFF;
        const i8 newVy = static_cast<i8>(static_cast<i16>(vy) + ticks);
        vy = newVy > MaxFall() ? MaxFall() : newVy;
    }
    self->moveForce.y = vy;

    debug::SetColour(ppu::mask::BLUE);
    ProcessMovement(this, self->moveForce, tx, blockedBelow);

    debug::SetColour(ppu::mask::RED | ppu::mask::GREEN);
    CollectCoinsFor(self, this);

    debug::SetColour(ppu::mask::RED | ppu::mask::BLUE);
    demo::actor::NextFrame(self, grounded);
    self->Update();
    debug::SetColour(0);
}

ACTORS void Player::Start() {
    actor.actorType = Actor::eActorType::Player;
    actor.gravity   = 0;
    actor.moveForce = vec2<i8>{0, 0};
    xForce   = 0;
    yForce   = 0;
    runTimer = 0;
    actor.Start();
}

NI ACTORS static void DrainPendingColumn() {
    const bool reversed = static_cast<bool>(levelStreamCommand & eLevelStreamCommands::STREAM_LEVEL_SWAP);
    if (pendingColumnKind == ePendingColumn::NEXT) {
        if (reversed && (pendingColumnIndex & 1))
            BuildNextColumn(TileBuffer, static_cast<u16>(pendingColumnIndex - 1));
        BuildNextColumn(TileBuffer, pendingColumnIndex);
    } else {
        if (reversed && !(pendingColumnIndex & 1))
            BuildPrevColumn(TileBuffer, static_cast<u16>(pendingColumnIndex + 1));
        BuildPrevColumn(TileBuffer, pendingColumnIndex);
    }
    pendingColumnKind = ePendingColumn::NONE;
    levelStreamCommand = levelStreamCommand | STREAM_LEVEL_DONE;
}

ACTORS void UpdateActors() {
    player1.Update();
#ifdef PLAYER2_SUPPORTED
    if (multiplayer) {
        player2.Update();
    }
#endif

    if (pendingColumnKind != ePendingColumn::NONE) {
        DrainPendingColumn();
    }
}

}   // namespace level
