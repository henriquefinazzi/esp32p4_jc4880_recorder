// Passa-altas: atenua 50 Hz, deixa passar a voz (300 Hz e 1 kHz), tira o nível contínuo.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "audio_dsp.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FALHOU %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

// Ganho em dB de um tom depois do filtro (mede a segunda metade, já sem o transitório).
static double gain_db(float hz, int dc)
{
    dsp_highpass_t f;
    dsp_highpass_init(&f, 16000, 100);
    static int16_t buf[16000];
    for (int i = 0; i < 16000; i++) buf[i] = (int16_t)(dc + 10000 * sin(2 * M_PI * hz * i / 16000));
    dsp_highpass_run(&f, buf, 16000);
    double in = 0, out = 0, mean = 0;
    for (int i = 8000; i < 16000; i++) {
        double x = 10000 * sin(2 * M_PI * hz * i / 16000);
        in += x * x;
        out += (double)buf[i] * buf[i];
        mean += buf[i];
    }
    if (dc) return mean / 8000; // com nível contínuo: devolve a média que sobrou
    return 10 * log10(out / in);
}

int main(void)
{
    double g50 = gain_db(50, 0), g300 = gain_db(300, 0), g1k = gain_db(1000, 0);
    double dc = gain_db(1000, 3000);
    printf("50 Hz: %.1f dB, 300 Hz: %.1f dB, 1 kHz: %.1f dB, contínuo restante: %.1f\n", g50, g300, g1k, dc);
    CHECK(g50 < -10);           // zumbido grave cai bastante
    CHECK(g300 > -1.0);         // voz quase intacta
    CHECK(fabs(g1k) < 0.3);
    CHECK(fabs(dc) < 50);       // de 3000 para perto de zero
    // Saturação: sinal no limite não pode virar o sinal ao contrário.
    dsp_highpass_t f;
    dsp_highpass_init(&f, 16000, 100);
    int16_t s[4] = {32767, -32768, 32767, -32768};
    dsp_highpass_run(&f, s, 4);
    CHECK(s[0] > 0 && s[1] < 0);
    if (failures) return 1;
    printf("audio_dsp: ok\n");
    return 0;
}
