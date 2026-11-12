// BO2WEAPONS MOD - not part of the original game. With +set fs_mods "... bo2weapons" (mods/_stack/maps/_modstack.gsc):
// the Black Ops II weapons of the player's own mod.ff (tools/bo2/bo2_weapon.py, docs/BO2_WEAPONS.md) join the box.

register()
{
	// maps\_zombiemode::main() runs this hook before maps\_zombiemode_weapons::init() on every zombie map
	maps\_modstack::add( "_zombiemode_ffotd::main_start", maps\bo2weapons\_bo2weapons::main_start );
}
