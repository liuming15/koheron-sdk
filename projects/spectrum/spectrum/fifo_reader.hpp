/// FPGA FIFO reader
///
/// (c) Koheron

#ifndef __DRIVERS_CORE_FIFO_READER_HPP__
#define __DRIVERS_CORE_FIFO_READER_HPP__

#include <array>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>

#include <drivers/wr_register.hpp>

// http://www.xilinx.com/support/documentation/ip_documentation/axi_fifo_mm_s/v4_1/pg080-axi-fifo-mm-s.pdf
#define PEAK_RDFR_OFF 0x18
#define PEAK_RDFO_OFF 0x1C
#define PEAK_RDFD_OFF 0x20
#define PEAK_RLR_OFF  0x24

template<size_t N>
class FIFOReader
{
  public:
    FIFOReader();

    void set_address(uintptr_t fifo_addr_);
    void start_acquisition(uint32_t acq_period_);
    void stop_acquisition();
    std::array<uint32_t, N>& get_data();

  private:
    uintptr_t fifo_addr;
    uint32_t acq_period; // Time between two acquisitions (us)

    uint32_t index; // Current index of the ring_buffer
    uint32_t acq_cnt;
    std::array<uint32_t, N> ring_buffer;
    std::array<uint32_t, N> results_buffer;
    std::mutex buff_access_mtx;

    std::atomic<bool> acquire;
    void acquisition_thread_call();
};

// cpp file

template<size_t N>
FIFOReader<N>::FIFOReader()
: fifo_addr(0)
, acq_period(0)
, index(0)
, acq_cnt(0)
{
    acquire.store(false);
}

template<size_t N>
void FIFOReader<N>::set_address(uintptr_t fifo_addr_)
{
    fifo_addr = fifo_addr_;
}

template<size_t N>
void FIFOReader<N>::acquisition_thread_call()
{
    while (acquire.load()) {
        uint32_t fifo_length = Klib::ReadReg32(fifo_addr + PEAK_RLR_OFF);

        buff_access_mtx.lock();
        for (unsigned int i=0; i<fifo_length; i++) {
            acq_cnt++;
            index = (index + 1) % N;
            ring_buffer[index] = Klib::ReadReg32(fifo_addr);
        }
        buff_access_mtx.unlock();

        std::this_thread::sleep_for(std::chrono::microseconds(acq_period));
    }
}

template<size_t N>
void FIFOReader<N>::start_acquisition(uint32_t acq_period_)
{
    acq_period = acq_period_;
    acquire.store(true);
    std::thread acq_thread(&FIFOReader<N>::acquisition_thread_call, this);
    acq_thread.detach();
}

template<size_t N>
void FIFOReader<N>::stop_acquisition()
{
    acquire.store(false);
}

template<size_t N>
std::array<uint32_t, N>& FIFOReader<N>::get_data()
{
    buff_access_mtx.lock();
    for (unsigned int i=0; i<N-index-1; i++)
        results_buffer[i] = ring_buffer[index + 1 + i];
    for (unsigned int i=N-index; i<N; i++)
        results_buffer[i] = ring_buffer[i-N+index];
    buff_access_mtx.unlock();
    return results_buffer;
}

#endif // __DRIVERS_CORE_FIFO_READER_HPP__