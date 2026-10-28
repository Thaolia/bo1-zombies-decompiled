#include maps\_utility;
#include common_scripts\utility;
#include maps\_zombiemode_utility;

// ADMIN MOD - not part of the original game: the script side of the dedicated server's zm_* commands
// (docs/SERVER.md, "Admin commands"). The engine notifies "zm_admin", <command>, <argument> on the target player's
// entity, at most once per player and server frame (src/server_mp/sv_zm_admin.cpp). Every reply is a LogPrint line
// starting with "admin: ", which the server also prints to its console.
//
// Dvars read here: zm_startpoints (points each player starts with, 0 = the game's own), zm_startperks (perks each
// player starts with: names or "all"), zm_jip_mode (a player who joins a game in progress: "immediate" plays at once,
// "nextround" spectates until the round ends) and zm_debug_revive (1: while a player is down, log why each other
// player can or cannot revive them).
//
// A dedicated server's players come in one by one, while the retail game sets up its players once, when
// "all_players_connected" is set (maps\_load_common): as soon as the players connected at that time are in. A player
// who joins later misses that set-up; admin_late_join_vars and admin_join_in_progress fill in what the game reads
// later and would fail on.
//
// The game has 4 characters, 4 start points and start points for 1 to 4 players; the server can take more players
// (sv_maxclients). admin_assign_character, admin_player_spawn_placement, admin_late_join_vars and
// admin_pad_coop_difficulty give the 5th player on what the game gives the first 4.

main_start()
{
	// maps\_zombiemode::init_function_overrides sets the default only when the map has not set its own
	level.admin_retail_Player_Spawn_func = level.Player_Spawn_func;
	level.Player_Spawn_func = ::admin_player_spawn_placement;
}


main_end()
{
	level.admin_round_active = false;
	level.admin_intro_done = false;
	admin_pad_coop_difficulty();
	level thread admin_install_spawn_client();
	level thread admin_intro_watch();
	level thread admin_round_tracker();
	level thread admin_connect_watch();
	level thread admin_late_join_vars();
	admin_log( "loaded on " + GetDvar( #"mapname" ) );
}


// maps\_zombiemode::difficulty_init sets the start points for the player count at that time only, and
// maps\_zombiemode_powerups reads them for the current count ("zombie_score_start_" + players.size + "p"): with one
// more player that var is undefined and the power-up drop thread dies. Set every count the same way (difficulty 1,
// table column 2). mp/zombiemode.csv has rows for 1 to 4 players only: past 4, set_zombie_var falls back to its
// default (3000) and difficulty_init gives everyone 3000. Past 4, take the 4-player values.
admin_late_join_vars()
{
	flag_wait( "all_players_connected" );
	// after post_all_players_connected's difficulty_init, which the same flag runs
	waittillframeend;
	for ( n = 1; n <= 4; n++ )
	{
		if ( !IsDefined( level.zombie_vars["zombie_score_start_" + n + "p"] ) )
		{
			maps\_zombiemode_utility::set_zombie_var( "zombie_score_start_" + n + "p", 3000, false, 2 );
		}
		if ( !IsDefined( level.zombie_vars["zombie_team_score_start_" + n + "p"] ) )
		{
			maps\_zombiemode_utility::set_zombie_var( "zombie_team_score_start_" + n + "p", 2000, false, 2 );
		}
	}
	started = get_players().size;
	given = level.zombie_vars["zombie_score_start_" + started + "p"];
	for ( n = 5; n <= admin_max_players(); n++ )
	{
		level.zombie_vars["zombie_score_start_" + n + "p"] = level.zombie_vars["zombie_score_start_4p"];
		level.zombie_vars["zombie_team_score_start_" + n + "p"] = level.zombie_vars["zombie_team_score_start_4p"];
	}
	if ( started <= 4 )
	{
		return;
	}
	points = level.zombie_vars["zombie_score_start_4p"];
	players = get_players();
	for ( i = 0; i < players.size; i++ )
	{
		if ( IsDefined( given ) && players[i].score == given && players[i].score_total == given )
		{
			players[i].score = points;
			players[i].score_total = points;
			players[i].old_score = points;
			players[i] maps\_zombiemode_score::set_player_score_hud();
		}
	}
	admin_log( started + " players at the start: " + points + " points each, as with 4 (the game would give "
		+ given + ")" );
}


admin_max_players()
{
	return GetDvarInt( #"sv_maxclients" );
}


// maps\_gameskill::getCoopValue reads level.difficultySettings[<name>][<skill>][players.size - 1], which setSkill
// fills for 1 to 4 players: with 5, coop_damage_and_accuracy_scaling and the coop watchers stop with script errors
// and the player damage settings stay unset. Past 4, take the 4-player values.
admin_pad_coop_difficulty()
{
	if ( !IsDefined( level.difficultySettings ) )
	{
		return;
	}
	names = [];
	names[0] = "coopEnemyAccuracyScalar";
	names[1] = "coopFriendlyAccuracyScalar";
	names[2] = "coopFriendlyThreatBiasScalar";
	names[3] = "coopPlayer_deathInvulnerableTime";
	names[4] = "coopPlayerDifficultyHealth";
	skills = [];
	skills[0] = "easy";
	skills[1] = "normal";
	skills[2] = "hardened";
	skills[3] = "veteran";
	count = admin_max_players();
	for ( n = 0; n < names.size; n++ )
	{
		for ( s = 0; s < skills.size; s++ )
		{
			if ( !IsDefined( level.difficultySettings[names[n]] ) )
			{
				continue;
			}
			values = level.difficultySettings[names[n]][skills[s]];
			if ( !IsDefined( values ) || !IsDefined( values[3] ) )
			{
				continue;
			}
			for ( i = 4; i < count; i++ )
			{
				if ( !IsDefined( values[i] ) )
				{
					values[i] = values[3];
				}
			}
			level.difficultySettings[names[n]][skills[s]] = values;
		}
	}
}


//-------------------------------------------------------------------------------
// more than 4 players
//-------------------------------------------------------------------------------

// The game's character is the player's entity number: model, arms (maps\_loadout::give_model or the map's
// zombiemode_give_player_model_override, maps\_zombiemode_load::player_set_viewmodel), voice
// (maps\_zombiemode_weapons::get_player_index) and respawn point (check_for_valid_spawn_near_team) all switch on
// self.entity_num or self.zm_random_char, and do nothing past 3. Give a 5th player the character fewest players
// have, and so a player of slots 0-3 whose character another player already has: a solo player's random character
// (onPlayerSpawned), or a 5th player's. Both fields are read at the first spawn, which follows spawnClient.
admin_assign_character()
{
	num = self GetEntityNumber();
	if ( IsDefined( self.admin_character ) )
	{
		self.entity_num = self.admin_character;
		self.zm_random_char = self.admin_character;
		return;
	}
	counts = [];
	for ( c = 0; c < 4; c++ )
	{
		counts[c] = 0;
	}
	players = get_players();
	for ( i = 0; i < players.size; i++ )
	{
		c = players[i].entity_num;
		if ( players[i] != self && IsDefined( c ) && c >= 0 && c < 4 )
		{
			counts[c]++;
		}
	}
	if ( num < 4 )
	{
		if ( counts[num] == 0 )
		{
			return;
		}
	}
	best = 0;
	for ( c = 1; c < 4; c++ )
	{
		if ( counts[c] < counts[best] )
		{
			best = c;
		}
	}
	self.admin_character = best;
	self.entity_num = best;
	self.zm_random_char = best;
	if ( num < 4 )
	{
		admin_log( self admin_who() + " plays character " + best + " (another player has character " + num + ")" );
	}
	else
	{
		admin_log( self admin_who() + " plays character " + best + " (the game has 4)" );
	}
}


// coop_player_spawn_placement (and the maps' own, zombie_cod5_sumpf, zombie_cod5_asylum) give player i the start
// struct i, and stop with a script error on the 5th player when all of them are in at the start (a map_restart).
// Repeat the structs up to sv_maxclients first: a 5th player starts where the 1st does.
admin_player_spawn_placement()
{
	count = admin_max_players();
	admin_pad_structs( "initial_spawn_points", "targetname", count );
	admin_pad_structs( "north_spawn", "script_noteworthy", count );
	admin_pad_structs( "south_spawn", "script_noteworthy", count );
	if ( IsDefined( level.admin_retail_Player_Spawn_func ) )
	{
		level thread [[level.admin_retail_Player_Spawn_func]]();
	}
	else
	{
		level thread maps\_zombiemode::coop_player_spawn_placement();
	}
}


admin_pad_structs( name, type, count )
{
	if ( !IsDefined( level.struct_class_names ) || !IsDefined( level.struct_class_names[type] ) )
	{
		return;
	}
	structs = level.struct_class_names[type][name];
	if ( !IsDefined( structs ) || structs.size == 0 || structs.size >= count )
	{
		return;
	}
	size = structs.size;
	for ( i = size; i < count; i++ )
	{
		from = structs[i % size];
		s = SpawnStruct();
		s.origin = from.origin;
		s.angles = from.angles;
		s.targetname = from.targetname;
		s.target = from.target;
		s.script_noteworthy = from.script_noteworthy;
		s.script_int = from.script_int;
		structs[i] = s;
	}
	level.struct_class_names[type][name] = structs;
	admin_log( type + " " + name + ": " + size + " structs, repeated to " + count + " for sv_maxclients" );
}


//-------------------------------------------------------------------------------
// joining a game in progress (zm_jip_mode)
//-------------------------------------------------------------------------------

// maps\_callbackglobal::Callback_PlayerConnect runs level.spawnClient once per connection, after "connected"
admin_install_spawn_client()
{
	while ( !IsDefined( level.spawnClient ) )
	{
		wait( 0.05 );
	}
	level.admin_retail_spawnClient = level.spawnClient;
	level.spawnClient = ::admin_spawn_client;
}


// the game set up the players connected at "all_players_connected" (coop_player_spawn_placement gave each a start
// point): a player without one joined after that
admin_spawn_client()
{
	self admin_assign_character();
	if ( flag( "all_players_connected" ) && !IsDefined( self.spectator_respawn ) )
	{
		self admin_join_in_progress();
		return;
	}
	self [[level.admin_retail_spawnClient]]();
}


admin_jip_mode()
{
	mode = ToLower( GetDvar( #"zm_jip_mode" ) );
	if ( mode != "nextround" && mode != "immediate" )
	{
		if ( mode != "" )
		{
			admin_log( "zm_jip_mode '" + mode + "' is neither immediate nor nextround: immediate" );
		}
		mode = "immediate";
	}
	return mode;
}


admin_join_in_progress()
{
	self endon( "disconnect" );
	// the struct spectator_respawn spawns a player at; without one the end-of-round respawn stops with a script error
	self admin_ensure_start_point();
	if ( !IsDefined( self.spectator_respawn ) )
	{
		admin_log( self admin_who() + " joined in progress, but the map has no player start point: the game spawns them" );
		self [[level.admin_retail_spawnClient]]();
		return;
	}
	self thread admin_jip_unfreeze();
	self thread admin_jip_points();
	if ( admin_jip_mode() == "nextround" )
	{
		// as a player who bled out (maps\_zombiemode::zombify_player): the game respawns them when the round ends
		admin_log( self admin_who() + " joined in progress: spectates until the round ends (zm_jip_mode nextround)" );
		self thread maps\_zombiemode::spawnSpectator();
		return;
	}
	// the retail respawn (from round 2 on the game's own level.custom_spawnPlayer): at a free spawn near the other
	// players, where maps\_callbackglobal::spawnPlayer would leave a late player where their entity stands
	if ( !IsDefined( level.custom_spawnPlayer ) )
	{
		level.custom_spawnPlayer = maps\_zombiemode::spectator_respawn;
	}
	admin_log( self admin_who() + " joined in progress: plays now (zm_jip_mode immediate)" );
	self [[level.spawnPlayer]]();
}


// maps\_zombiemode::onPlayerSpawned freezes a player at their first spawn, and the intro's fade_in frees the players
// in at that time: one who spawns after it would stay frozen
admin_intro_watch()
{
	level waittill_any( "fade_in_complete", "start_of_round" );
	level.admin_intro_done = true;
}


admin_jip_unfreeze()
{
	self endon( "disconnect" );
	self waittill( "spawned_player" );
	while ( !is_true( level.admin_intro_done ) )
	{
		wait( 0.05 );
	}
	// after onPlayerSpawned's freeze, which the same notify runs
	wait( 0.05 );
	self FreezeControls( false );
	admin_log( self admin_who() + " spawned at " + self.origin + ", controls free" );
}


// maps\_zombiemode::difficulty_init gives the players in at the start the start points for their count; a player who
// joins later has 0. Give them the same, and at least the 1500 the game's own respawn gives after round 6
// (spectators_respawn). zm_startpoints, when set, replaces both (admin_start_kit).
admin_jip_points()
{
	self endon( "disconnect" );
	self waittill( "spawned_player" );
	// after spectator_respawn's death penalty, which follows its notify
	wait( 0.05 );
	if ( GetDvarInt( #"zm_startpoints" ) > 0 )
	{
		return;
	}
	points = level.zombie_vars["zombie_score_start_" + get_players().size + "p"];
	if ( !IsDefined( points ) )
	{
		return;
	}
	if ( level.round_number > 6 && points < 1500 )
	{
		points = 1500;
	}
	if ( self.score < points )
	{
		self.score = points;
		self.score_total = points;
		self.old_score = points;
		self maps\_zombiemode_score::set_player_score_hud();
	}
	admin_log( "points for " + self admin_who() + " (joined in progress): " + self.score );
}


admin_ensure_start_point()
{
	if ( IsDefined( self.spectator_respawn ) )
	{
		return;
	}
	structs = getstructarray( "initial_spawn_points", "targetname" );
	if ( structs.size == 0 )
	{
		return;
	}
	players = get_players();
	choice = structs[self GetEntityNumber() % structs.size];
	for ( i = 0; i < structs.size; i++ )
	{
		used = false;
		for ( p = 0; p < players.size; p++ )
		{
			if ( players[p] != self && IsDefined( players[p].spectator_respawn ) && players[p].spectator_respawn == structs[i] )
			{
				used = true;
			}
		}
		if ( !used )
		{
			choice = structs[i];
			break;
		}
	}
	self.spectator_respawn = choice;
	admin_log( "start point given to " + self admin_who() + " (joined after the game set up its players)" );
}


admin_log( msg )
{
	LogPrint( "admin: " + msg + "\n" );
}


admin_who()
{
	return self GetEntityNumber() + " (" + self.playername + ")";
}


// as mods/sandbox: round_think() notifies "start_of_round" once round_spawning() runs, "end_of_round" when it is over.
// round_think() reads get_players() before "start_of_round" and, at the round's end, respawns the spectators only when
// that count was not 1: a round that started with one player keeps a player who joined during it spectating for one
// more round. Then start the same retail respawn (maps\_zombiemode::spectators_respawn) here.
admin_round_tracker()
{
	for ( ;; )
	{
		level waittill( "start_of_round" );
		level.admin_round_active = true;
		started_with = get_players().size;
		admin_log( "round " + level.round_number + " started, players " + started_with );
		level waittill( "end_of_round" );
		level.admin_round_active = false;
		if ( started_with == 1 && get_players().size > 1 )
		{
			admin_log( "round " + level.round_number + " started with one player: respawning the spectators" );
			level thread maps\_zombiemode::spectators_respawn();
		}
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
	// spawnClient may have run before admin_install_spawn_client replaced it
	self admin_assign_character();
	self thread admin_start_kit();
	self thread admin_life_log();
	self thread admin_revive_debug_watch();
	for ( ;; )
	{
		self waittill( "zm_admin", cmd, arg );
		// a thread per command, so this loop is back in its waittill before the engine's next notify
		self thread admin_run( cmd, arg );
	}
}


// downs, bleed-outs and revives in the server log: a player's state column changes without saying why otherwise
admin_life_log()
{
	self endon( "disconnect" );
	for ( ;; )
	{
		event = self waittill_any_return( "player_downed", "bled_out", "player_revived" );
		switch ( event )
		{
		case "player_downed":
			// maps\_laststand::refire_player_downed notifies it again a second later for a player with perks
			if ( !is_true( self.admin_down_logged ) )
			{
				self.admin_down_logged = true;
				admin_log( self admin_who() + " is down" );
			}
			break;
		case "bled_out":
			self.admin_down_logged = false;
			admin_log( self admin_who() + " bled out" );
			break;
		case "player_revived":
			self.admin_down_logged = false;
			admin_log( self admin_who() + " was revived" );
			break;
		}
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
	case "down":
		self admin_down();
		break;
	case "power":
		self admin_power();
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
	admin_log( self admin_who() + " is character " + self.entity_num + ", model " + self.model );
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
		if ( self.sessionstate == "spectator" )
		{
			admin_log( "revive " + self admin_who() + ": dead (spectating), zm_respawn brings them back" );
		}
		else
		{
			admin_log( "revive " + self admin_who() + ": not down" );
		}
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


// turns the power on as a player using the map's switch does (Kino's electric_switch waits on use_elec_switch's
// "trigger"); a map with only the "power_on" flag gets the flag, as the devgui does
admin_power()
{
	if ( !level flag_exists( "power_on" ) )
	{
		admin_log( "power: this map has no power switch" );
		return;
	}
	if ( flag( "power_on" ) )
	{
		admin_log( "power: already on" );
		return;
	}
	trig = GetEnt( "use_elec_switch", "targetname" );
	if ( IsDefined( trig ) )
	{
		trig notify( "trigger", self );
		admin_log( "power: switch used by " + self admin_who() );
		return;
	}
	flag_set( "power_on" );
	admin_log( "power: power_on set" );
}


// down without dying: a test for revives (the player bleeds out as usual unless revived)
admin_down()
{
	if ( self.sessionstate != "playing" || self maps\_laststand::player_is_in_laststand() )
	{
		admin_log( "down " + self admin_who() + ": not standing" );
		return;
	}
	self DoDamage( self.health + 1000, self.origin );
	admin_log( "downed " + self admin_who() + ", in last stand " + self maps\_laststand::player_is_in_laststand() );
}


//-------------------------------------------------------------------------------
// revive diagnostics (zm_debug_revive 1): maps\_laststand::revive_trigger_think revives once another player passes
// can_revive and holds use; this logs each step of that test, for every other player, whenever the result changes
//-------------------------------------------------------------------------------

admin_revive_debug_watch()
{
	self endon( "disconnect" );
	for ( ;; )
	{
		self waittill( "player_downed" );
		if ( GetDvarInt( #"zm_debug_revive" ) > 0 )
		{
			self admin_revive_debug();
		}
	}
}


admin_revive_debug()
{
	self endon( "disconnect" );
	admin_log( "revive debug: " + self admin_who() + " is down, trigger " + IsDefined( self.revivetrigger ) + ", radius " + GetDvarInt( #"revive_trigger_radius" ) );
	last = [];
	while ( self maps\_laststand::player_is_in_laststand() )
	{
		players = get_players();
		for ( i = 0; i < players.size; i++ )
		{
			p = players[i];
			if ( p == self )
			{
				continue;
			}
			result = admin_revive_checks( p, self );
			key = "p" + p GetEntityNumber();
			if ( !IsDefined( last[key] ) || last[key] != result )
			{
				last[key] = result;
				admin_log( "revive debug: " + p admin_who() + " -> " + self GetEntityNumber() + ": " + result );
			}
		}
		wait( 0.25 );
	}
	admin_log( "revive debug: " + self admin_who() + " no longer down, session " + self.sessionstate );
}


// the tests of maps\_laststand::can_revive and is_reviving, in their order
admin_revive_checks( p, downed )
{
	result = "dist " + int( Distance( p.origin, downed.origin ) ) + " alive " + IsAlive( p ) + " down " + p maps\_laststand::player_is_in_laststand();
	if ( IsDefined( downed.revivetrigger ) )
	{
		result = result + " touch " + p IsTouching( downed.revivetrigger );
	}
	else
	{
		result = result + " touch (no trigger)";
	}
	result = result + " facing " + p maps\_laststand::is_facing( downed );
	result = result + " sight " + SightTracePassed( p.origin + ( 0, 0, 50 ), downed.origin + ( 0, 0, 30 ), false, undefined );
	result = result + " bullet " + BulletTracePassed( p.origin + ( 0, 0, 50 ), downed.origin + ( 0, 0, 30 ), false, undefined );
	result = result + " zombie " + is_true( p.is_zombie );
	result = result + " use " + p UseButtonPressed();
	result = result + " can_revive " + p maps\_laststand::can_revive( downed );
	return result;
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
	self admin_ensure_start_point();
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
