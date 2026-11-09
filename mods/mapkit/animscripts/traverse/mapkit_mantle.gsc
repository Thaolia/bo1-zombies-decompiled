#include animscripts\utility;
#include animscripts\traverse\zombie_shared;
#using_animtree ("generic_human");

// MapKit traverse "mapkit_mantle": Five's zombie wall hop (zombie_mantle_over_40's animations) over an obstacle of any
// depth (BO2's zm_mantle_over_40 and garage doors: begin and end nodes up to 84 units apart, where the hop carries a
// zombie 63 to 70 units and up to 18 units up). The gap between where the animation ends and the end node is spread
// over the animation's frames (the actor is noclip while it traverses, as retail moves it with teleportThreadEx), so
// the zombie lands on the end node, not short of it on the obstacle.
main()
{
	if ( !IsDefined( self.is_zombie ) || !self.is_zombie )
	{
		return;
	}
	if ( self.isdog )
	{
		dog_wall_and_window_hop( "wallhop", 40 );
		return;
	}
	traverseData = [];
	traverseData[ "traverseAnim" ] = mapkit_mantle_anim();
	self thread mapkit_mantle_reach( traverseData[ "traverseAnim" ] );
	DoTraverse( traverseData );
}

// zombie_mantle_over_40's choice, one animation picked here so that its move delta is known
mapkit_mantle_anim()
{
	if ( IsDefined( self.animname ) && self.animname == "quad_zombie" )
	{
		return %ai_zombie_traverse_v3;
	}
	if ( !self.has_legs )
	{
		if ( RandomInt( 2 ) )
		{
			return %ai_zombie_traverse_crawl_v1;
		}
		return %ai_zombie_traverse_v4;
	}
	if ( self.zombie_move_speed == "run" )
	{
		return %ai_zombie_traverse_v5;
	}
	if ( self.zombie_move_speed == "sprint" )
	{
		if ( RandomInt( 2 ) )
		{
			return %ai_zombie_traverse_v6;
		}
		return %ai_zombie_traverse_v7;
	}
	if ( RandomInt( 2 ) )
	{
		return %ai_zombie_traverse_v1;
	}
	return %ai_zombie_traverse_v2;
}

mapkit_mantle_reach( animation )
{
	self endon( "killanimscript" );
	self endon( "death" );
	start = self GetNegotiationStartNode();
	end = self GetNegotiationEndNode();
	if ( !IsDefined( start ) || !IsDefined( end ) )
	{
		return;
	}
	// DoTraverse turns the actor to the begin node's yaw; the delta is in the animation's frame (x forward, y left)
	delta = GetMoveDelta( animation, 0, 1 );
	yaw = start.angles[1];
	c = Cos( yaw );
	s = Sin( yaw );
	land = self.origin + ( delta[0] * c - delta[1] * s, delta[0] * s + delta[1] * c, delta[2] );
	frames = Int( GetAnimLength( animation ) * 0.8 / 0.05 );
	if ( frames < 1 )
	{
		frames = 1;
	}
	step = ( end.origin - land ) / frames;
	for ( i = 0; i < frames; i++ )
	{
		wait( 0.05 );
		self Teleport( self.origin + step );
	}
}
