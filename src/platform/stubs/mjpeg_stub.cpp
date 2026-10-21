// The mjpeg/VP8 movie capture (the "mjpeg" console command, built on libvpx) is not part of the offline build:
// asking for a capture prints a warning and nothing is ever encoding.

#include <mjpeg/avi.h>
#include <mjpeg/mjpeg.h>
#include <mjpeg/yuv.h>
#include <qcommon/common.h>

void mjpeg_initonce()
{
}

void __cdecl mjpeg_init()
{
}

void __cdecl mjpeg_create(const char *, int, int, unsigned int)
{
    Com_PrintWarning(0, "Movie capture is not part of this build (BO1_OFFLINE)\n");
}

bool __cdecl mjpeg_is_encoding()
{
    return false;
}

void __cdecl mjpeg_add_samples(__int16 *, int)
{
}

void __cdecl mjpeg_draw()
{
}

void __cdecl mjpeg_close()
{
}

void __cdecl yuv_lost_device()
{
}

void __cdecl yuv_recover_device()
{
}
