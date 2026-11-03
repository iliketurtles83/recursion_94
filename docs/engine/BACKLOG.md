# Backlog

Open items not tied to an environment phase. Newest last.

## Flight / ship

- **Engine trail is two disconnected parts.** The flame plume (`shipmodel_flames`) ends and the
  white trail ribbon (`update_trail_mesh` in `src/cybertunnel.c`) carries on from there in a
  different colour and width, which looks silly. Reconcile them into one continuous exhaust: the
  trail should start where the flame starts (or inside it), take the flame colour near the engine
  and fade toward its tail, with matching width at the join. (2026-10-21)
