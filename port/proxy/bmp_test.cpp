
#include "bmp.h"

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    const unsigned pixels[4] = {0xFF0000FFu, 0xFF00FF00u, 0xFFFF0000u, 0xFF102030u};
    return bmp::write_24(argv[1], 2, 2, (const unsigned char*)pixels, 8) ? 0 : 1;
}
