#include maps\_utility;
#include common_scripts\utility;
#include maps\_zombiemode_utility;

// ADMIN MOD - not part of the original game: the script side of the dedicated server's zm_* commands
// (docs/SERVER.md, "Admin commands"). The engine notifies "zm_admin", <command>, <argument> on the target player's
// entity, at most once per player and server frame (src/server_mp/sv_zm_admin.cpp). Every reply is a LogPrint line
// starting with "admin: ", which the server also prints to its console.
//
// Dvars read here: zm_startpoints (points each player starts with, 0 = the game's own) and zm_startperks (perks each
// player starts with: names or "all").

main_end()
{
	level.admin_round_active = false;
	level thread admin_round_tracker();
	level thread admin_connect_watch();
	admin_log( "loaded on " + GetDvar( #"mapname" ) );
}


admin_log( msg )
{
	LogPrint( "admin: " + msg + "\n" );
}


admin_who()
{
	return self GetEntityNumber() + " (" + self.playername + ")";
}


// as mods/sandbox: round_think() notifies "start_of_round" once round_spawning() runs, "end_of_round" when it is over
admin_round_tracker()
{
	for ( ;; )
	{
		level waittill( "start_of_round" );
		level.admin_round_active = true;
		admin_log( "round " + level.round_number + " started" );
		level waittill( "end_of_round" );
		level.admin_round_active = false;
	}
}


admin_connect_watch()
{
	players = get_players();
	for ( i = 0; i < players.size; i++ )
	{
		players[i] thread admin_player();
	}
	for ( ;; )
	{
		level waittill( "connected", player );
		player thread admin_player();
	}
}


admin_player()
{
	self endon( "disconnect" );
	if ( is_true( self.admin_listening ) )
	{
		return;
	}
	self.admin_listening = true;
	self thread admin_start_kit();
	for ( ;; )
	{
		self waittill( "zm_admin", cmd, arg );
		// a thread per command, so this loop is back in its waittill before the engine's next notify
		self thread admin_run( cmd, arg );
	}
}


admin_run( cmd, arg )
{
	self endon( "disconnect" );
	switch ( cmd )
	{
	case "points":
		self admin_points( int( arg ) );
		break;
	case "setpoints":
		self admin_setpoints( int( arg ) );
		break;
	case "perk":
		self admin_give_perks( arg, "perk" );
		break;
	case "revive":
		self admin_revive();
		break;
	case "respawn":
		self admin_respawn();
		break;
	case "kill":
		self admin_kill();
		break;
	case "round":
		admin_goto_round( int( arg ) );
		break;
	default:
		admin_log( "unknown command '" + cmd + "' for " + self admin_who() );
		break;
	}
}


//-------------------------------------------------------------------------------
// start points and perks (zm_startpoints, zm_startperks)
//-------------------------------------------------------------------------------

admin_start_kit()
{
	self endon( "disconnect" );
	flag_wait( "all_players_spawned" );
	// a player who joins later spectates until the game spawns them
	while ( self.sessionstate != "playing" )
	{
		wait( 0.05 );
	}
	// after the game's own start points (maps\_zombiemode sets them once all players are connected)
	wait( 1 );
	points = GetDvarInt( #"zm_startpoints" );
	if ( points > 0 )
	{
		self admin_setpoints( points );
	}
	perks = GetDvar( #"zm_startperks" );
	if ( perks != "" )
	{
		self admin_give_perks( perks, "start perks" );
	}
}


//-------------------------------------------------------------------------------
// points
//-------------------------------------------------------------------------------

admin_points( amount )
{
	if ( amount >= 0 )
	{
		self maps\_zombiemode_score::add_to_player_score( amount );
	}
	else
	{
		self.score += amount;
		if ( self.score < 0 )
		{
			self.score = 0;
		}
		self maps\_zombiemode_score::set_player_score_hud();
	}
	admin_log( "points " + amount + " for " + self admin_who() + ", now " + self.score );
}


admin_setpoints( amount )
{
	if ( amount < 0 )
	{
		amount = 0;
	}
	self.score = amount;
	self maps\_zombiemode_score::set_player_score_hud();
	admin_log( "points set for " + self admin_who() + ": " + self.score );
}


//-------------------------------------------------------------------------------
// perks
//-------------------------------------------------------------------------------

admin_perk_name( name )
{
	switch ( ToLower( name ) )
	{
	case "jugg":
	case "juggernog":
		return "specialty_armorvest";
	case "revive":
	case "quickrevive":
		return "specialty_quickrevive";
	case "speed":
	case "speedcola":
		return "specialty_fastreload";
	case "doubletap":
		return "specialty_rof";
	case "staminup":
		return "specialty_longersprint";
	case "phd":
	case "flopper":
		return "specialty_flakjacket";
	case "deadshot":
		return "specialty_deadshot";
	case "mule":
	case "mulekick":
		return "specialty_additionalprimaryweapon";
	}
	return ToLower( name );
}


// the perks of this map's machines: only those have their HUD icon and bottle loaded
admin_map_perks()
{
	perks = [];
	vending = GetEntArray( "zombie_vending", "targetname" );
	for ( i = 0; i < vending.size; i++ )
	{
		if ( IsDefined( vending[i].script_noteworthy ) && !is_in_array( perks, vending[i].script_noteworthy ) )
		{
			perks[perks.size] = vending[i].script_noteworthy;
		}
	}
	return perks;
}


// names: "all", or perk names separated by spaces or commas; free, as a perk the game gives (give_random_perk)
admin_give_perks( names, what )
{
	if ( self.sessionstate != "playing" || self maps\_laststand::player_is_in_laststand() )
	{
		admin_log( what + " for " + self admin_who() + ": not playing (down or spectating), none given" );
		return;
	}
	available = admin_map_perks();
	wanted = [];
	if ( ToLower( names ) == "all" )
	{
		wanted = available;
	}
	else
	{
		tokens = StrTok( names, " ," );
		for ( i = 0; i < tokens.size; i++ )
		{
			wanted[wanted.size] = admin_perk_name( tokens[i] );
		}
	}
	given = "";
	for ( i = 0; i < wanted.size; i++ )
	{
		perk = wanted[i];
		if ( !is_in_array( available, perk ) )
		{
			admin_log( what + " for " + self admin_who() + ": " + perk + " has no machine on this map" );
			continue;
		}
		if ( self HasPerk( perk ) )
		{
			continue;
		}
		self maps\_zombiemode_perks::give_perk( perk );
		given = given + " " + perk;
	}
	if ( given == "" )
	{
		given = " none (already owned or not on this map)";
	}
	admin_log( what + " for " + self admin_who() + ":" + given );
}


//-------------------------------------------------------------------------------
// revive and respawn
//-------------------------------------------------------------------------------

admin_revive()
{
	if ( !self maps\_laststand::player_is_in_laststand() )
	{
		admin_log( "revive " + self admin_who() + ": not down" );
		return;
	}
	// auto_revive (as solo Quick Revive) counts a revive for the reviver: here the player itself, whose count is kept
	revives = self.revives;
	stat = self.stats["revives"];
	self maps\_laststand::auto_revive( self );
	self.revives = revives;
	self.stats["revives"] = stat;
	admin_log( "revived " + self admin_who() );
}


// as the game kills a player outside the playable area (maps\_zombiemode, player_out_of_playable_area_monitor): down,
// no life left, bled out at once; the player then spectates until the round ends (or zm_respawn)
admin_kill()
{
	if ( self.sessionstate != "playing" )
	{
		admin_log( "kill " + self admin_who() + ": not playing" );
		return;
	}
	self.lives = 0;
	if ( !self maps\_laststand::player_is_in_laststand() )
	{
		self DoDamage( self.health + 1000, self.origin );
	}
	self.bleedout_time = 0;
	admin_log( "killed " + self admin_who() );
}


// the game respawns spectators between rounds (maps\_zombiemode::spectators_respawn): the same call, now
admin_respawn()
{
	if ( self.sessionstate != "spectator" )
	{
		admin_log( "respawn " + self admin_who() + ": not a spectator" );
		return;
	}
	if ( !IsDefined( level.spawnPlayer ) )
	{
		admin_log( "respawn " + self admin_who() + ": the game has no spawn function yet" );
		return;
	}
	if ( !IsDefined( level.custom_spawnPlayer ) )
	{
		level.custom_spawnPlayer = maps\_zombiemode::spectator_respawn;
	}
	// spectator_respawn spawns at the player's start point; a player who joined later has none
	if ( !IsDefined( self.spectator_respawn ) )
	{
		structs = getstructarray( "initial_spawn_points", "targetname" );
		if ( structs.size > 0 )
		{
			self.spectator_respawn = structs[self GetEntityNumber() % structs.size];
		}
	}
	if ( !IsDefined( self.spectator_respawn ) )
	{
		admin_log( "respawn " + self admin_who() + ": the map has no player start point" );
		return;
	}
	self [[level.spawnPlayer]]();
	admin_log( "respawned " + self admin_who() );
}


//-------------------------------------------------------------------------------
// round (follows mods/sandbox, sandbox_goto_round, itself after zombie_devgui_goto_round)
//-------------------------------------------------------------------------------

admin_goto_round( target_round )
{
	if ( target_round < 1 )
	{
		target_round = 1;
	}
	// between rounds nothing listens for "kill_round" yet: wait for the next round, the latest request wins
	if ( !is_true( level.admin_round_active ) )
	{
		if ( IsDefined( level.admin_goto_pending ) )
		{
			level.admin_goto_pending = target_round;
			admin_log( "round " + target_round + ": replaces the waiting request" );
			return;
		}
		level.admin_goto_pending = target_round;
		admin_log( "round " + target_round + ": waiting for the current round to start" );
		level waittill( "start_of_round" );
		wait( .05 );
		target_round = level.admin_goto_pending;
		level.admin_goto_pending = undefined;
	}
	admin_log( "round " + target_round + " from " + level.round_number );

	level.zombie_total = 0;
	maps\_zombiemode::ai_calculate_health( target_round );
	level.round_number = target_round - 1;

	level notify( "kill_round" );

	wait( 1 );

	zombies = GetAiSpeciesArray( "axis", "all" );
	if ( IsDefined( zombies ) )
	{
		for ( i = 0; i < zombies.size; i++ )
		{
			if ( is_true( zombies[i].ignore_devgui_death ) )
			{
				continue;
			}
			zombies[i] DoDamage( zombies[i].health + 666, zombies[i].origin );
		}
	}
}
