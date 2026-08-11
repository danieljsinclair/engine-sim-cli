// Bridge archive relink anchor (see CMakeLists.txt "RESILIENCE" block).
//
// This translation unit is intentionally empty. It exists for ONE reason: its
// object file is an EXPLICIT input to each consumer's link edge, and its
// OBJECT_DEPENDS are the pre-built bridge archives. The Ninja generator treats
// those object deps as non-order-only, so when any archive is rebuilt this .o is
// recompiled, and because a changed .o is an explicit link input the executable
// (or test) is RELINKED.
//
// Why this is needed: the bridge archive is consumed via an IMPORTED STATIC
// target, which the Ninja generator links ORDER-ONLY. An order-only edge
// rebuilds only when the file is MISSING, never when it is NEWER — so editing
// engine-sim sources rebuilds the archive but the CLI silently keeps running a
// STALE binary. Neither LINK_DEPENDS nor add_dependencies escapes the
// order-only bucket for an IMPORTED-library consumer; an object dependency does.
