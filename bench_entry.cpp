#include "bench.hpp"
#include "spsc_fifo_0.hpp"
#include "spsc_fifo_1.hpp"
#include "spsc_fifo_2.hpp"

int main(int argc, char* argv[]) {
	bench<SpscFifo0>("SpscFifo0", argc, argv);
	bench<SpscFifo1>("SpscFifo1", argc, argv);
	bench<SpscFifo2>("SpscFifo2", argc, argv);
	return 0;
}