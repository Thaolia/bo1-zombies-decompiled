#include animscripts\utility;
#include animscripts\traverse\zombie_shared;
#using_animtree ("generic_human");

// MapKit traverse "mapkit_jump_up": a jump up of any height on BO1's only zombie jump up, ai_zombie_jump_up_222
// (BO2's zm_jump_up_72 .. zm_jump_up_154). The animation's own rise beyond the end node is taken off during its first
// half (zombie_shared::teleportThreadEx, as retail corrects a traverse's height), so the zombie lands on the end
// node's floor instead of 222 units up, in the upper floor's ceiling.
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
	self thread mapkit_jump_height( traverseData[ "traverseAnim" ] );
	DoTraverse( traverseData );
}

mapkit_jump_height( animation )
{
	self endon( "killanimscript" );
	self endon( "death" );
	end = self GetNegotiationEndNode();
	if ( !IsDefined( end ) )
	{
		return;
	}
	rise = GetMoveDelta( animation, 0, 1 );
	frames = Int( GetAnimLength( animation ) * 0.5 / 0.05 );
	if ( frames < 1 )
	{
		frames = 1;
	}
	self teleportThreadEx( end.origin[2] - self.origin[2] - rise[2], 0, frames );
}
