#include <stdio.h>
#include <stdlib.h>
#include <math.h>


void delay(int D, float A, double *x, double *y){
    for (int n = 0; n < 20; n++){
        if (n < D)
            y[n] = 0;
        else
            y[n] = A * x[n-D];
    }

}

void output(double *x, double *y, double *z){
    for (int n = 0; n < 20; n++){
        z[n] = x[n] + y[n];
    }
    
}

int main(){
    double x[20], y[20], z[20];
    int D;
    
    printf("Enter Sample array\n");
    
    for (int n = 0; n < 20; n++){
        scanf("%lf", &x[n]);
    }
    
    printf("x[n] after input:\n");
    for (int n = 0; n < 20; n++){
        printf("%2d: %f\n", n, x[n]);
    }
    
    printf("Enter delay\n");
    scanf("%d", &D);

    printf("Enter amplitude scale (0 - 1)");
    scanf("%f", &A);
    
    delay(D, A, x, y);
    output(x, y, z);
    
    printf("\n n\t x[n]\t\t y[n]\t\t\ z[n]\n");
    printf("-----------------------------------------------------------------\n");
    
    for( int n =0; n < 20; n++){
        printf("%2d\t%f\t%f\t%f\n", n, x[n], y[n], z[n]);
        
    }
    
    return 0;
}
