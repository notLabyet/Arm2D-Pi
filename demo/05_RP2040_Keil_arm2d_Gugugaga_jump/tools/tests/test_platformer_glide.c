#define main existing_game_tests
#include "test_platformer_game.c"
#undef main

int main(void)
{
    /* Repeated input events during a hold cannot queue another jump, even
     * across landing. Only a release followed by a new press rearms it. */
    platformer_game_t single=empty_game();
    single.bGlideHeld=true;single.bGlideQualified=true;
    unsigned takeoffs=0;
    for (unsigned n=0;n<400;n++) {
        bool grounded=single.bGrounded;
        advance(&single,1,0,true);
        takeoffs += grounded && !single.bGrounded;
    }
    assert(takeoffs==1 && single.bGrounded && single.hwBufferMs==0);
    single.bGlideHeld=false;advance(&single,1,0,false);
    single.bGlideHeld=true;single.bGlideQualified=true;advance(&single,1,0,true);
    assert(!single.bGrounded && single.lVelocityYQ8<0);
    platformer_game_t normal=empty_game(), glide=normal;
    glide.bGlideHeld=true;glide.bGlideQualified=true;
    advance(&normal,1,180,true);advance(&glide,1,180,true);
    for(unsigned n=0;n<24;n++) {
        assert(!glide.bGliding);
        assert(normal.lFootYQ8==glide.lFootYQ8);
        advance(&normal,1,180,false);advance(&glide,1,180,false);
    }
    advance(&glide,20,180,false);
    assert(glide.bGliding && !glide.bGrounded);
    assert(glide.lVelocityYQ8<=40*256);
    glide.bGlideHeld=false;
    advance(&glide,5,180,false);
    assert(!glide.bGliding && glide.lVelocityYQ8>60*256);
    advance(&glide,100,180,false);
    assert(glide.bGrounded && !glide.bJumpFlight);
    glide=empty_game();glide.bGlideHeld=true;glide.bGlideQualified=true;
    advance(&glide,1,0,true);advance(&glide,240,0,false);
    assert(glide.bGrounded && !glide.bGliding);
    /* Walking off a ledge does not grant a jump-flight glide. */
    glide=empty_game();glide.bGrounded=false;glide.lFootYQ8=100*256;
    glide.bGlideHeld=true;glide.bGlideQualified=true;advance(&glide,25,0,false);
    assert(!glide.bGliding);
    puts("PASS: unchanged ascent, held apex glide, release gravity, landing, no walk-off glide");
}
