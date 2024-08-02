#include<stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include<string.h>
int main() {
    pid_t children[10];

    // 使用memset将children数组的所有字节设置为0
    memset(children, 0, sizeof(children));

    // 打印数组内容以确认所有元素都被设置为0
    for (int i = 0; i < 10; i++) {
        printf("children[%d] = %d\n", i, children[i]);
    }

    return 0;
}