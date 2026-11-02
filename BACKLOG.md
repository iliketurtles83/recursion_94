# Backlog

Open items not tied to an environment phase. Newest last.

## Flight / ship

- **Lower steering limit is too low.** Moving down, the craft leaves the bottom of the screen.
  The steering box is symmetric (`steerLimit` × path radius on each axis, `src/core/flight.c`),
  but the camera sits above and behind the ship, so the same offset reaches the screen edge sooner
  downward. Give the box its own bottom limit (for example `steerLimitDown`, in steerLimit units)
  or derive it from the camera framing, and keep the soft edge and the crystal keep-out
  (`crystal_keepout`) consistent with the new shape. (2026-10-21)
- **Engine trail is two disconnected parts.** The flame plume (`shipmodel_flames`) ends and the
  white trail ribbon (`update_trail_mesh` in `src/cybertunnel.c`) carries on from there in a
  different colour and width, which looks silly. Reconcile them into one continuous exhaust: the
  trail should start where the flame starts (or inside it), take the flame colour near the engine
  and fade toward its tail, with matching width at the join. (2026-10-21)
