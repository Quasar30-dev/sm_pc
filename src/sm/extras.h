#pragma once
// sm_pc's borrowed play options - a Ratchet, weapons, packs, the wrench, the charge boots and hoverboots, the follow
// camera and the dive read from the other PS2 Ratchet & Clank discs - are built on a PS2 engine that is not part of
// the standalone sm_pc repository. A build with SM_PC_STANDALONE defined has none of them: their classes are there
// (ps2_ratchet.h, ps2_weapons.h, ps2_packs.h, camera.h) but load nothing, and the menus do not offer them.
namespace sm {
#ifdef SM_PC_STANDALONE
inline constexpr bool extras_built = false;
#else
inline constexpr bool extras_built = true;
#endif
} // namespace sm
