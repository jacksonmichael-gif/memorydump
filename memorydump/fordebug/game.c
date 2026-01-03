#include <stdio.h>

int main(){
    int a ;
    float b;
    double c;
    while(1){
        printf("1:write 2:read 3:exit\n");
        int choice;
        if(!scanf("%d", &choice)){
            printf("invalid input\n");
            // clear the invalid input
            char ch;
            while((ch = getchar()) != '\n' && ch != EOF);
            continue;
        }
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
                    if(!scanf("%d", &a)){
                        printf("invalid input\n");
                        // clear the invalid input
                        char ch;
                        while((ch = getchar()) != '\n' && ch != EOF);
                    }
                    break;
                case 2:
                    printf("enter float number:");
                    if(!scanf("%f", &b)){
                        printf("invalid input\n");
                        // clear the invalid input
                        char ch;
                        while((ch = getchar()) != '\n' && ch != EOF);
                    }
                    break;
                case 3:
                    printf("enter double number:");
                    if(!scanf("%lf", &c)){
                        printf("invalid input\n");
                        // clear the invalid input
                        char ch;
                        while((ch = getchar()) != '\n' && ch != EOF);
                    }
                    break;
                default:
                    printf("invalid option\n");
                    break;
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