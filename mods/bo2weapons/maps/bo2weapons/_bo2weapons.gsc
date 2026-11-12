// BO2WEAPONS MOD - not part of the original game. Registers the BO2 weapons that mods/bo2weapons/mod.ff holds, as the
// map's include_weapons() and maps\_zombiemode_weapons::init_weapons() register retail ones: box, wall-buy table entry,
// Pack-a-Punch upgrade. A weapon missing from the loaded zones (no mod.ff, or another fs_game) is skipped, so the map
// stays retail instead of failing on PrecacheItem.

main_start()
{
	level.bo2weapons = [];
	// name, Pack-a-Punch name, hint, cost (BO2's box-only weapons keep their BO2 wall price), weapon VO type
	add( "hamr_zm", "hamr_upgraded_zm", &"ZOMBIE_WEAPON_HAMR", 9500, "mg" );
	if ( !level.bo2weapons.size )
	{
		return;
	}
	// init_weapons() calls level._zombie_custom_add_weapons after the retail list: keep a map's own one
	level._bo2weapons_map_add = level._zombie_custom_add_weapons;
	level._zombie_custom_add_weapons = ::add_weapons;
}

add( name, upgrade, hint, cost, vo )
{
	if ( !bo1_mapkit_weaponloaded( name ) )
	{
		LogPrint( "bo2weapons: " + name + " not loaded (no mods/bo2weapons/mod.ff?), skipped\n" );
		return;
	}
	maps\_zombiemode_weapons::include_zombie_weapon( name, true );
	if ( IsDefined( upgrade ) && bo1_mapkit_weaponloaded( upgrade ) )
	{
		maps\_zombiemode_weapons::include_zombie_weapon( upgrade, false );
	}
	else
	{
		upgrade = undefined;
	}
	w = SpawnStruct();
	w.name = name;
	w.upgrade = upgrade;
	w.hint = hint;
	w.cost = cost;
	w.vo = vo;
	level.bo2weapons[level.bo2weapons.size] = w;
	LogPrint( "bo2weapons: " + name + " in the box\n" );
}

add_weapons()
{
	if ( IsDefined( level._bo2weapons_map_add ) )
	{
		[[ level._bo2weapons_map_add ]]();
	}
	for ( i = 0; i < level.bo2weapons.size; i++ )
	{
		w = level.bo2weapons[i];
		maps\_zombiemode_weapons::add_zombie_weapon( w.name, w.upgrade, w.hint, w.cost, w.vo, "", undefined );
	}
}
