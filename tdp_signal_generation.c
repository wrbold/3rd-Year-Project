#include <stdio.h>
#include <math.h>
#include <stdlib.h>

#define PI 3.14159265358979323846

int main(){
    
    double A, f;
    double fs = 1000;
    int N = 20;
    
    printf("Enter Amplitude (V): ");
    scanf("%lf", &A);
    printf("Enter frequency (Hz): ");
    scanf("%lf", &f);
    
    double *x = (double *)malloc(N * sizeof(double));
    if (x == NULL){
        printf("Memory allocation failed\n");
        return 1;
    }
    
    for (int n = 0; n < N; n++) {
        double t = n /fs;
        x[n] = A * sin(2* PI * f* t);
        
    }
    
    for (int n = 0; n < N; n++) {
        x[n] = 0.5 * x[n];
    }
    
    printf("\nSignal(V)\n");
    for (int n = 0; n < 20; n++){
        printf("%f\n", x[n]);
    }
    
    free(x);
    return 0;

}
