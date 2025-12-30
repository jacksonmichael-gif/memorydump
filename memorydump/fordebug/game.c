#include <stdio.h>

int main(){
    int a ;
    float b;
    double c;
    while(1){
        printf("1:write 2:read 3:exit\n");
        int choice;
        scanf("%d", &choice);
        if(choice == 3){
            break;
        }
        else if(choice == 1){
            int sousa;
            printf("switch types 1:int 2:float 3:double\n");
            scanf("%d", &sousa);
            switch(sousa){
                case 1:
                    printf("enter int number:");
                    scanf("%d", &a);
                    break;
                case 2:
                    printf("enter float number:");
                    scanf("%f", &b);
                    break;
                case 3:
                    printf("enter double number:");
                    scanf("%f", &b);
                    break;
                default:
                    printf("invalid option\n");
                    continue;
            }
        }
        else if(choice == 2){
            printf("a=%d, b=%f, c=%f\n", a, b, c);
        }
        else{
            printf("invalid option\n");
        }
    }
    return 0;
}