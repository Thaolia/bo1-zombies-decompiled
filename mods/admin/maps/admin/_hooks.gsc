// ADMIN MOD - not part of the original game. The dedicated zombies server (+set bo1_server 1) always stacks it on
// fs_mods (src/universal/com_files.cpp, FS_ModStackNames); the engine's zm_* commands (src/server_mp/sv_zm_admin.cpp)
// send it what to do. Registered in the fs_mods base hooks (mods/_stack/maps/_modstack.gsc).

register()
{
	// every map: the shared hooks of maps\_zombiemode::main(); main_start runs before its init_function_overrides
	maps\_modstack::add( "_zombiemode_ffotd::main_start", maps\admin\_admin::main_start );
	maps\_modstack::add( "_zombiemode_ffotd::main_end", maps\admin\_admin::main_end );
}
