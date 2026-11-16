/* Emit the compiled preview samples and the resulting waveform y coordinates.
 * The independent Python test compares these with the original full table. */
#include <stdint.h>
#include <stdio.h>
#include "src-waves.h"

int main(void)
{
    for (unsigned t = 0; t < 5; ++t)
        for (unsigned f = 0; f < 8; ++f)
            for (unsigned i = 0; i < 74; ++i)
                putchar((uint8_t)canopy_preview_value(t, f, i));
    for (unsigned t = 0; t < 5; ++t)
        for (unsigned pos = 0; pos <= 700; ++pos) {
            unsigned frame = pos / 100, fraction = pos % 100, next = frame < 7 ? frame + 1 : frame;
            int samples[74], peak = 1;
            for (unsigned i = 0; i < 74; ++i) {
                int value = (canopy_preview_value(t, frame, i) * (int)(100 - fraction) +
                             canopy_preview_value(t, next, i) * (int)fraction) /
                            100;
                samples[i] = value;
                int magnitude = value < 0 ? -value : value;
                if (magnitude > peak)
                    peak = magnitude;
            }
            for (unsigned i = 0; i < 74; ++i) {
                int value = samples[i],
                    magnitude = (6 * (value < 0 ? -value : value) + peak / 2) / peak;
                putchar(20 + (value < 0 ? magnitude : -magnitude));
            }
        }
    return ferror(stdout) ? 1 : 0;
}
