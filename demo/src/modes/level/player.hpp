#pragma once
#include "../../actor.hpp"
#include "../../types.hpp"

// Player state: position, velocity and the animation it drives.
#if !defined(TARGET_GBA) && !defined(TARGET_NDS) && !defined(TARGET_CTR)
#  define PLAYER2_SUPPORTED
#endif

namespace level {



AI inline i8  MaxWalk()     { return demo::Framerated<i8, 12>::ScaleUp(); }    // 1.5 px/frame
AI inline i8  MaxRun()      { return demo::Framerated<i8, 20>::ScaleUp(); }    // 2.5 px/frame
AI inline i16 WalkAccel()   { return demo::Framerated<i16, 76>::ScaleUp(); }
AI inline i16 RunAccel()    { return demo::Framerated<i16, 112>::ScaleUp(); }
AI inline i16 Friction()    { return demo::Framerated<i16, 76>::ScaleUp(); }   // coast-down when grounded with no input
AI inline i16 Skid()        { return demo::Framerated<i16, 152>::ScaleUp(); }  // turnaround deceleration when grounded
AI inline i16 AirAccel()    { return demo::Framerated<i16, 76>::ScaleUp(); }   // limited control while airborne, no friction
AI inline u8  RunRetain()   { return demo::Framerated<u8, 10>::ScaleDown(); }  // frame-count duration, not a rate

AI inline i8  JumpInit()    { return demo::Framerated<i8, 32>::ScaleUp(); }    // launch speed off the ground
AI inline i8  RunJumpInit() { return demo::Framerated<i8, 36>::ScaleUp(); }    // taller launch at running speed
AI inline i16 RiseGravity() { return demo::Framerated<i16, 256>::ScaleUp(); }  // ascending with A held -> floaty
AI inline i16 FallGravity() { return demo::Framerated<i16, 896>::ScaleUp(); }  // descending or A released -> heavier
AI inline i8  MaxFall()     { return demo::Framerated<i8, 36>::ScaleUp(); }    // 4.5 px/frame terminal


struct Player {
    Actor actor;
    i16 xForce;    // horizontal acceleration remainder (1/256-subpixel/frame)
    i16 yForce;    // gravity remainder (1/256-subpixel/frame)
    u8  runTimer;
    void Start();
    void Update();
};

}   // namespace level
