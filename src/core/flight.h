// Ship motion along the path and the chase camera. Simulated at a fixed rate;
// the renderer interpolates between the previous and current state.
#ifndef CT_FLIGHT_H
#define CT_FLIGHT_H

#include <stdbool.h>
#include "dvec.h"
#include "path.h"
#include "params.h"

#define FLIGHT_HZ        120.0
#define FLIGHT_TRAIL_LEN 72      // 0.6 s at 120 Hz
#define FLIGHT_CLEARANCE 0.6f    // min ship distance to PathStyle floor/ceiling (covers banked wings)

typedef struct FlightInput {
    float steerX, steerY;    // -1..1 (right, up)
    bool  boost;
    bool  rollLeft, rollRight;
    // External drift (world units/s, path frame right/up): the lateral velocity
    // settles at the steering velocity plus this. 0 = none. Used for boss gravity.
    float driftX, driftY;
} FlightInput;

// Ship models (Sim parameter shipModel). The courier has articulated nozzles and
// courierScale; the dart and the probe have fixed engines and their own size.
enum { FLIGHT_SHIP_COURIER = 0, FLIGHT_SHIP_DART = 1, FLIGHT_SHIP_PROBE = 2, FLIGHT_SHIP_COUNT };

typedef struct FlightParams {
    float baseSpeed, boostSpeed, speedResponse;
    float steerLimit;        // fraction of R
    float steerSoftEdge;     // cushion band inside the limit, fraction of it (0 = hard stop)
    // Rectangle for open environments, world units from the path centre (0 = steerLimit x R).
    float steerBoxHalfWidth, steerBoxUp, steerBoxDown;
    float steerAccel, steerDamping;
    float vectoringStrength, vectoringAngleDeg;
    float maxBankDeg, maxPitchDeg;
    float camDistance, camHeight, camStiffness, camUpStiffness;
    float camFollowLateral;  // how much the camera follows ship offset
    float lookAhead, lookBlend;
    float fovBase, fovBoost;
    float shakeAmount;
    float cameraRollFollow;  // 0 = tunnel roll only, 1 = follow ship bank
    float barrelRollTime;
    float barrelRollDash;    // lateral speed kick in the roll direction (world units/s, 0 = none)
    float barrelRollCooldown; // seconds after a roll before the next can start
    bool  autopilot;
    int   shipModel;         // FLIGHT_SHIP_*: which model flies (engine positions differ)
    float courierScale;      // uniform size of the courier model and its exhaust anchors
} FlightParams;

typedef struct FlightState {
    double s;
    float  speed;
    float  x, y, vx, vy;     // cross-section offset (right, up) and velocity
    float  bank, tilt;       // radians
    float  controlX, controlY; // damped steering command for articulated surfaces
    float  vectorPower;       // boost-gated, speed-dependent thrust authority
    Vector2 gimbal[2];        // actual left/right nozzle pitch and yaw, radians
    float  barrel;           // extra roll from barrel roll (radians)
    float  barrelT;          // 0..1 progress, <0 idle
    int    barrelDir;
    float  barrelCooldown;   // seconds left before another roll can start
    double time;
    // derived each step
    DVec3  shipPos;
    DVec3  shipFwd, shipUp, shipRight;
    // camera, relative to the path frame at the ship (no lag along the path)
    float  camLocal[3], camLocalVel[3];   // (right, up, fwd) offsets from path center
    DVec3  camUp;
    DVec3  camPos, camTarget;
    float  fov;
    // engine trail history (world space), newest at trailHead-1
    DVec3  trail[2][FLIGHT_TRAIL_LEN];
    int    trailHead, trailCount;
} FlightState;

typedef struct FlightView {
    DVec3 shipPos, shipFwd, shipUp, shipRight;
    DVec3 camPos, camTarget, camUp;
    float fov;
    double s;
    float speed;
    float controlX, controlY;
    float vectorPower;
    Vector2 gimbal[2];
} FlightView;

typedef struct FlightBox { float xMin, xMax, yMin, yMax; } FlightBox;

void flight_register_params(FlightParams *p, ParamRegistry *reg);
// Limits of the ship's centre offset at a path radius: the rectangle in open environments
// (clipped by floor and ceiling), the bounding box of the steering circle in tunnels.
FlightBox flight_steer_box(const FlightParams *p, const PathStyle *style, float radius);
void flight_reset(FlightState *st, const FlightParams *p, const PathSystem *path, double s);
void flight_step(FlightState *st, const FlightParams *p, const PathSystem *path, const FlightInput *in, double dt);
// Fixed-timestep accumulator: advances cur (saving prev) by whole FLIGHT_HZ steps for a
// frame of length dt. One-shot input triggers are cleared once a step consumed them.
// Returns the number of steps taken; *acc keeps the remainder (alpha = *acc * FLIGHT_HZ).
int flight_advance(FlightState *prev, FlightState *cur, const FlightParams *p, const PathSystem *path,
                   FlightInput *in, double *acc, double dt, int maxSteps);
FlightView flight_interpolate(const FlightState *a, const FlightState *b, float alpha);
float flight_speed_norm(const FlightParams *p, float speed);
// Engine attach points in ship-local space (right, up, fwd).
void flight_engine_offsets(int shipModel, Vector3 out[2]);   // dart and probe (fixed nozzles)
// Shared nozzle rotation/attachment math for reaction forces and exhaust wake.
Vector3 flight_gimbal_rotate(Vector3 p, Vector2 angles);
Vector3 flight_exhaust_offset(int engine, Vector2 angles, float plumeLength);

#endif
