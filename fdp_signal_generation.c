#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define PI 3.14159265358979323846

int main() {

    const double fs = 1000.0;   
    const int N = 64;           

    double A1, A2;
    double f1, f2;

    printf("Enter Amplitude 1: ");
    scanf("%lf", &A1);

    printf("Enter Frequency 1 (Hz): ");
    scanf("%lf", &f1);

    printf("Enter Amplitude 2: ");
    scanf("%lf", &A2);

    printf("Enter Frequency 2 (Hz): ");
    scanf("%lf", &f2);

    double x[N];

    for (int n = 0; n < N; n++) {
        double t = (double)n / fs;

        double signal1 = A1 * sin(2 * PI * f1 * t);
        double signal2 = A2 * sin(2 * PI * f2 * t);

        x[n] = signal1 + signal2;
    }

    printf("\nGenerated Signal:\n");
    for (int n = 0; n < N; n++) {
        printf("%f\n", n, x[n]);
    }

    return 0;
}
