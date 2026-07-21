/* Byte-found v0.3 test */
int main() {
    int i = 0;
    int sum = 0;

    while (i < 5) {
        i = i + 1;
        sum = sum + i;      // 1+2+3+4+5 = 15
    }

    if (sum == 15) {
        sum = sum - 4;
    } else {
        sum = 0;
    }

    return sum;             // expected result: 11
}
