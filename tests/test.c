/* Byte-found v0.4 test */

int add(int a, int b) {
    return a + b;
}

int sum_to(int n) {
    int i = 0;
    int total = 0;
    while (i < n) {
        i = i + 1;
        total = total + i;
    }
    return total;
}

int main() {
    int x = sum_to(5);      // 1+2+3+4+5 = 15
    return add(x, -4);      // expected result: 11
}
