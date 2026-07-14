# Controls

Caustic remains fully touch-driven. Physical controls are an optional
accessibility/convenience layer that synthesizes touches into Caustic's native
renderer.

## Toggle

Press **Triangle** to enable or disable physical-control mode. It starts off on
every launch. Disabling the mode releases any held synthetic touch or rack
swipe so it cannot remain stuck.

## Navigation and adjustment

- **D-pad:** move the visible focus cursor in 24-pixel steps.
- **Hold a D-pad direction:** repeat movement after a short delay.
- **Cross:** grab the focused control.
- **D-pad while grabbed:** drag in three-pixel increments for knobs, sliders,
  selectors, and other continuous controls.
- **Cross again:** release the control.

This is geometry-based touch synthesis, not semantic widget navigation. Some
screens may require positioning the cursor manually.

## Transport and racks

- **Start:** alternate between the transport play and stop positions.
- **Select:** open machine management and reposition focus near slot one.
- **Left stick up/down:** create a vertical swipe along the non-interactive
  left rack rail. Long holds re-anchor automatically before reaching an edge.

## Multi-touch

Front-panel hardware touches retain independent pointer IDs. Controller touches
use reserved IDs, so playing multiple keyboard notes remains possible while
the controller layer is enabled.
