static_assert(sizeof(unsigned) == 4);

#ifdef __cplusplus
extern "C" {
#endif

volatile unsigned input[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
struct result { unsigned values[16], sum, done; };
volatile struct result output;

unsigned transform(unsigned value, unsigned salt) {
    return (value * (salt + 7) + 11) / (salt + 1);
}

void probe(void) {
    unsigned temporary[16];
    for (unsigned i = 0; i < 16; ++i) temporary[i] = transform(input[i], i);
    unsigned sum = 0;
    for (unsigned i = 0; i < 16; ++i) {
        output.values[i] = temporary[i];
        sum += temporary[i];
    }
    output.sum = sum;
    output.done = 0x484f4c4f;
}

#ifdef __cplusplus
}
#endif
