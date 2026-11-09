#include animscripts\utility;
#include animscripts\traverse\zombie_shared;
#using_animtree ("generic_human");

// MapKit traverse "mapkit_jump_up": a jump up of any height and length on BO1's only zombie jump up,
// ai_zombie_jump_up_222 (BO2's zm_jump_up_72 .. zm_jump_up_154). The zombie is noclip while it traverses; each frame
// it is moved so that it follows the animation's own path, its rise scaled to the end node's height and the gap to
// the end node's spot spread over the first 80% of the animation. It lands on the end node, not 222 units up in the
// upper floor's ceiling, nor short of a far end node (Nuketown's attic, 105 units on, where the zombies stayed in the
// edge). A linear height correction also sank them under the floor while the animation crouched before its jump.
main()
{
	if ( !IsDefined( self.is_zombie ) || !self.is_zombie )
	{
		return;
	}
	if ( self.isdog )
	{
		dog_jump_up( 222, 7 );
		return;
	}
	traverseData = [];
	traverseData[ "traverseAnim" ] = %ai_zombie_jump_up_222;
	self thread mapkit_jump_reach( traverseData[ "traverseAnim" ] );
	DoTraverse( traverseData );
}

mapkit_jump_reach( animation )
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
	from = self.origin;
	yaw = start.angles[1];
	c = Cos( yaw );
	s = Sin( yaw );
	total = GetMoveDelta( animation, 0, 1 );
	land = from + ( total[0] * c - total[1] * s, total[0] * s + total[1] * c, total[2] );
	gap = ( end.origin[0] - land[0], end.origin[1] - land[1], 0 );
	scale = 1.0;
	if ( total[2] > 1 )
	{
		scale = ( end.origin[2] - from[2] ) / total[2];
	}
	frames = Int( GetAnimLength( animation ) / 0.05 );
	if ( frames < 1 )
	{
		frames = 1;
	}
	applied = ( 0, 0, 0 );
	for ( i = 1; i <= frames; i++ )
	{
		wait( 0.05 );
		t = i / frames;
		d = GetMoveDelta( animation, 0, t );
		reach = t / 0.8;
		if ( reach > 1 )
		{
			reach = 1;
		}
		want = gap * reach + ( 0, 0, d[2] * ( scale - 1 ) );
		self Teleport( self.origin + want - applied );
		applied = want;
	}
}
