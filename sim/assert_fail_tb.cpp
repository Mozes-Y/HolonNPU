#include "Vnpu_assert_fail_test_top.h"

#include <memory>

namespace {

void tick(Vnpu_assert_fail_test_top& dut) {
    dut.clk_i = 0;
    dut.eval();
    dut.clk_i = 1;
    dut.eval();
}

}  // namespace

int main() {
    auto dut = std::make_unique<Vnpu_assert_fail_test_top>();

    dut->clk_i = 0;
    dut->rst_ni = 0;
    dut->valid_i = 0;
    dut->ready_i = 0;
    dut->data_i = 0;
    tick(*dut);

    dut->rst_ni = 1;
    tick(*dut);

    dut->valid_i = 1;
    dut->ready_i = 0;
    dut->data_i = 0x11111111U;
    tick(*dut);

    dut->data_i = 0x22222222U;
    tick(*dut);

    return 0;
}
