// BO2WEAPONS MOD - not part of the original game. Registers the BO2 weapons that mods/bo2weapons/mod.ff holds, as the
// map's include_weapons() and maps\_zombiemode_weapons::init_weapons() register retail ones: box and Pack-a-Punch
// upgrade. The weapons are listed in mod.ff's mp/bo2weapons.csv (name, Pack-a-Punch name, VO type), written by
// tools/bo2/bo2_weapon.py. A weapon missing from the loaded zones (no mod.ff, or another fs_game) is skipped, so the
// map stays retail instead of failing on PrecacheItem.

main_start()
{
	level.bo2weapons = [];
	table = "mp/bo2weapons.csv";
	for ( row = 0; ; row++ )
	{
		name = TableLookupColumnForRow( table, row, 0 );
		if ( name == "" )
		{
			break;
		}
		add( name, TableLookupColumnForRow( table, row, 1 ), TableLookupColumnForRow( table, row, 2 ) );
	}
	LogPrint( "bo2weapons: " + level.bo2weapons.size + " weapons in the box\n" );
	if ( !level.bo2weapons.size )
	{
		return;
	}
	// init_weapons() calls level._zombie_custom_add_weapons after the retail list: keep a map's own one
	level._bo2weapons_map_add = level._zombie_custom_add_weapons;
	level._zombie_custom_add_weapons = ::add_weapons;
}

add( name, upgrade, vo )
{
	if ( !bo1_mapkit_weaponloaded( name ) )
	{
		LogPrint( "bo2weapons: " + name + " not loaded, skipped\n" );
		return;
	}
	maps\_zombiemode_weapons::include_zombie_weapon( name, true );
	if ( upgrade != "" && bo1_mapkit_weaponloaded( upgrade ) )
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
	w.vo = vo;
	level.bo2weapons[level.bo2weapons.size] = w;
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
		// box weapons: the cost only matters on a wall, where none of them is
		maps\_zombiemode_weapons::add_zombie_weapon( w.name, w.upgrade, &"ZOMBIE_WEAPON_BO2", 1500, w.vo, "", undefined );
	}
}
