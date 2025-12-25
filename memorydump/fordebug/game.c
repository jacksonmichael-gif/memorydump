#include <stdio.h>

int main(){
    int a = 1, b = 0;
    while(1){
        printf("1:write 2:read 3:exit\n");
        int choice;
        scanf("%d", &choice);
        if(choice == 3){
            break;
        }
        else if(choice == 1){
            printf("enter value to write to a:");
            scanf("%d", &a);
            printf("enter value to write to b:");
            scanf("%d", &b);
            printf("wrote a=%d, b=%d\n", a, b);
        }
        else if(choice == 2){
            printf("a=%d, b=%d\n", a, b);
        }
        else{
            printf("invalid option\n");
        }
    }
    return 0;
}