#include <stdio.h>
#include <math.h>

#define N 32
#define FS 1000.0
#define PI 3.14159265358979323846

typedef struct {
    double real;
    double imag;
} Complex;

unsigned int reverseBits(unsigned int x, int log2n) {
    unsigned int n = 0;
    for (int i = 0; i < log2n; i++) {
        n <<= 1;
        n |= (x & 1);
        x >>= 1;
    }
    return n;
}

void fft(Complex *x) {
    int log2n = 5;

    for (int i = 0; i < N; i++) {
        int j = reverseBits(i, log2n);
        if (j > i) {
            Complex temp = x[i];
            x[i] = x[j];
            x[j] = temp;
        }
    }

    for (int s = 1; s <= log2n; s++) {
        int m = 1 << s;
        int m2 = m >> 1;

        Complex wm;
        wm.real = cos(-2 * PI / m);
        wm.imag = sin(-2 * PI / m);

        for (int k = 0; k < N; k += m) {
            Complex w = {1.0, 0.0};

            for (int j = 0; j < m2; j++) {
                Complex t, u;

                t.real = w.real * x[k+j+m2].real - w.imag * x[k+j+m2].imag;
                t.imag = w.real * x[k+j+m2].imag + w.imag * x[k+j+m2].real;

                u = x[k+j];

                x[k+j].real = u.real + t.real;
                x[k+j].imag = u.imag + t.imag;
                x[k+j+m2].real = u.real - t.real;
                x[k+j+m2].imag = u.imag - t.imag;

                double tempReal = w.real * wm.real - w.imag * wm.imag;
                double tempImag = w.real * wm.imag + w.imag * wm.real;
                w.real = tempReal;
                w.imag = tempImag;
            }
        }
    }
}

void findPeaks(Complex *x) {
    double max1 = 0, max2 = 0;
    int idx1 = 0, idx2 = 0;

    for (int i = 1; i < N/2; i++) {
        double mag = sqrt(x[i].real*x[i].real + x[i].imag*x[i].imag);

        if (mag > max1) {
            max2 = max1;
            idx2 = idx1;
            max1 = mag;
            idx1 = i;
        } else if (mag > max2) {
            max2 = mag;
            idx2 = i;
        }
    }

    double freq1 = idx1 * FS / N;
    double freq2 = idx2 * FS / N;

    double amp1 = (2.0 / N) * max1;
    double amp2 = (2.0 / N) * max2;

    printf("Frequency 1: %.2f Hz  Amplitude: %.2f\n", freq1, amp1);
    printf("Frequency 2: %.2f Hz  Amplitude: %.2f\n", freq2, amp2);
}

int main() {

    Complex x[N];

    for (int i = 0; i < N; i++) {
        scanf("%lf", &x[i].real);
        x[i].imag = 0.0;
    }

    fft(x);
    findPeaks(x);

    return 0;
}
