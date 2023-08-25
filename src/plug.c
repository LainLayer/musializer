#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "plug.h"

#define N (1<<13)
#define FONT_SIZE 69

typedef struct {
    Music music;
    Font font;
    bool error;
} Plug;

Plug *plug = NULL;

float in_raw[N];
float in_win[N];
float complex out_raw[N];
float out_log[N];
float out_smooth[N];

// Ported from https://rosettacode.org/wiki/Fast_Fourier_transform#Python
void fft(float in[], size_t stride, float complex out[], size_t n)
{
    assert(n > 0);

    if (n == 1) {
        out[0] = in[0];
        return;
    }

    fft(in, stride*2, out, n/2);
    fft(in + stride, stride*2,  out + n/2, n/2);

    for (size_t k = 0; k < n/2; ++k) {
        float t = (float)k/n;
        float complex v = cexp(-2*I*PI*t)*out[k + n/2];
        float complex e = out[k];
        out[k]       = e + v;
        out[k + n/2] = e - v;
    }
}

float amp(float complex z)
{
    float a = crealf(z);
    float b = cimagf(z);
    return logf(a*a + b*b);
}

void callback(void *bufferData, unsigned int frames)
{
    // https://cdecl.org/?q=float+%28*fs%29%5B2%5D
    float (*fs)[2] = bufferData; 

    for (size_t i = 0; i < frames; ++i) {
        memmove(in_raw, in_raw + 1, (N - 1)*sizeof(in_raw[0]));
        in_raw[N-1] = fs[i][0];
    }
}

void plug_init(void)
{
    plug = malloc(sizeof(*plug));
    assert(plug != NULL && "Buy more RAM lol");
    memset(plug, 0, sizeof(*plug));

    plug->font = LoadFontEx("./fonts/Alegreya-Regular.ttf", FONT_SIZE, NULL, 0);
}

Plug *plug_pre_reload(void)
{
    if (IsMusicReady(plug->music)) {
        DetachAudioStreamProcessor(plug->music.stream, callback);
    }
    return plug;
}

void plug_post_reload(Plug *prev)
{
    plug = prev;
    if (IsMusicReady(plug->music)) {
        AttachAudioStreamProcessor(plug->music.stream, callback);
    }
}

void plug_update(void)
{
    if (IsMusicReady(plug->music)) {
        UpdateMusicStream(plug->music);
    }

    if (IsKeyPressed(KEY_SPACE)) {
        if (IsMusicReady(plug->music)) {
            if (IsMusicStreamPlaying(plug->music)) {
                PauseMusicStream(plug->music);
            } else {
                ResumeMusicStream(plug->music);
            }
        }
    }

    if (IsKeyPressed(KEY_Q)) {
        if (IsMusicReady(plug->music)) {
            StopMusicStream(plug->music);
            PlayMusicStream(plug->music);
        }
    }

    if (IsFileDropped()) {
        FilePathList droppedFiles = LoadDroppedFiles();
        if (droppedFiles.count > 0) {
            const char *file_path = droppedFiles.paths[0];

            if (IsMusicReady(plug->music)) {
                StopMusicStream(plug->music);
                UnloadMusicStream(plug->music);
            }

            plug->music = LoadMusicStream(file_path);

            if (IsMusicReady(plug->music)) {
                plug->error = false;
                printf("music.frameCount = %u\n", plug->music.frameCount);
                printf("music.stream.sampleRate = %u\n", plug->music.stream.sampleRate);
                printf("music.stream.sampleSize = %u\n", plug->music.stream.sampleSize);
                printf("music.stream.channels = %u\n", plug->music.stream.channels);
                SetMusicVolume(plug->music, 0.5f);
                AttachAudioStreamProcessor(plug->music.stream, callback);
                PlayMusicStream(plug->music);
            } else {
                plug->error = true;
            }
        }
        UnloadDroppedFiles(droppedFiles);
    }

    int w = GetRenderWidth();
    int h = GetRenderHeight();
    float dt = GetFrameTime();

    BeginDrawing();
    ClearBackground(CLITERAL(Color) {
        0x18, 0x18, 0x18, 0xFF
    });

    if (IsMusicReady(plug->music)) {
        // Apply the Hann Window on the Input - https://en.wikipedia.org/wiki/Hann_function
        for (size_t i = 0; i < N; ++i) {
            float t = (float)i/(N - 1);
            float hann = 0.5 - 0.5*cosf(2*PI*t);
            in_win[i] = in_raw[i]*hann;
        }

        // FFT
        fft(in_win, 1, out_raw, N);

        // "Squash" into the Logarithmic Scale
        float step = 1.06;
        float lowf = 1.0f;
        size_t m = 0;
        float max_amp = 1.0f;
        for (float f = lowf; (size_t) f < N/2; f = ceilf(f*step)) {
            float f1 = ceilf(f*step);
            float a = 0.0f;
            for (size_t q = (size_t) f; q < N/2 && q < (size_t) f1; ++q) {
                float b = amp(out_raw[q]);
                if (b > a) a = b;
            }
            if (max_amp < a) max_amp = a;
            out_log[m++] = a;
        }

        // Normalize Frequencies to 0..1 range
        for (size_t i = 0; i < m; ++i) {
            out_log[i] /= max_amp;
        }

        float smoothness = 8;
        for (size_t i = 0; i < m; ++i) {
            out_smooth[i] += (out_log[i] - out_smooth[i])*smoothness*dt;
        }

        // Display the Frequencies
        float cell_width = (float)w/m;
        for (size_t i = 0; i < m; ++i) {
            float t = out_log[i];
            DrawRectangle(i*cell_width, h - h*2/3*t, cell_width, h*2/3*t, GREEN);
        }
    } else {
        const char *label;
        Color color;
        if (plug->error) {
            label = "Could not load file";
            color = RED;
        } else {
            label = "Drag&Drop Music Here";
            color = WHITE;
        }
        Vector2 size = MeasureTextEx(plug->font, label, plug->font.baseSize, 0);
        Vector2 position = {
            w/2 - size.x/2,
            h/2 - size.y/2,
        };
        DrawTextEx(plug->font, label, position, plug->font.baseSize, 0, color);
    }
    EndDrawing();
}