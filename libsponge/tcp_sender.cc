#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity)
    , rt(retx_timeout) {}

//! \brief How many sequence numbers are occupied by segments sent but not yet acknowledged?
//! \note count is in "sequence space," i.e. SYN and FIN each count for one byte
//! (see TCPSegment::length_in_sequence_space())
uint64_t TCPSender::bytes_in_flight() const {
    return _bytes_unack;
}

//! \brief create and send segments to fill as much of the window as possible
void TCPSender::fill_window() {
    // 有空间能发送,或者窗口为0
    if(_ack_abs_seqno + _window_size >= _next_seqno && !fin){
        size_t send_size = _ack_abs_seqno + (_window_size==0?1:_window_size) - _next_seqno;
        do{
            TCPSegment seg;
            // _next_seqno==0, 说明是客户端发起握手操作，设置SYN信号为true
            seg.header().syn = !_next_seqno;

            // 设置发送的第一个字节的序列号
            seg.header().seqno = wrap(_next_seqno,_isn);

            // 注意SYN占一个序列号
            uint64_t length = seg.length_in_sequence_space();

            if(send_size > TCPConfig::MAX_PAYLOAD_SIZE){
                seg.payload()=Buffer(_stream.read(TCPConfig::MAX_PAYLOAD_SIZE));
            }else if(send_size > length){
                // 这里不取等于
                seg.payload()=Buffer(_stream.read(send_size));
            }

            length = seg.length_in_sequence_space();
            // 注意FIN也占一个序列号
            if(_stream.eof() && length < send_size){
                seg.header().fin = true;
                length += 1;
                fin = true;
            }
            // 这里不应该发空段
            if(length > 0){
                _segments_out.push(seg);
                _segments_out_nuack.push(seg);
                _bytes_unack += length;
                _next_seqno += length;

                send_size -= length;
                // 启动计时器
                rt.start();
            }
            // 空段原因只能是下面两种，没空间了，或者没输入数据了
        }while(send_size>0 && _stream.buffer_size());
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    // 解包得到绝对序列号
    uint64_t new_ackno = unwrap(ackno,_isn,_next_seqno);
    // 等于号记得加上，因为可能存在重复确认，但是有多的窗口空间！！！！
    if(new_ackno == _ack_abs_seqno){
        _window_size = window_size;
    }else if(new_ackno > _ack_abs_seqno && new_ackno <= _next_seqno){
        // 根据重传时间重置重传计时器
        rt.restart(_initial_retransmission_timeout);
        _ack_abs_seqno = new_ackno;
        _window_size = window_size;

        // 剔除已经被确认的段
        while(!_segments_out_nuack.empty()){
            const TCPSegment& seg = _segments_out_nuack.front();
            uint64_t startIndex = unwrap(seg.header().seqno,_isn,_ack_abs_seqno);
            size_t length = seg.length_in_sequence_space();
            if((startIndex+length) <= _ack_abs_seqno){
                // 已经被包括了
                _bytes_unack-=length;
                _segments_out_nuack.pop();
            }else{
                // 没有全部被包括
                break;
            }
        }

        if(_segments_out_nuack.empty()){
            // 关闭计时器
            rt.stop();
        }else{
            // 如果数据还有，就打开
            rt.start();
        }
    }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    if(rt.tick(ms_since_last_tick)){
        // 重传,根据窗口大小来确定是否翻倍RTO
        rt.rt(_window_size);

        if(!_segments_out_nuack.empty()){
            TCPSegment seg = _segments_out_nuack.front();
            _segments_out.push(seg);
        }
    }
}

//! \brief Number of consecutive retransmissions that have occurred in a row
unsigned int TCPSender::consecutive_retransmissions() const { return rt.get_retx_attempts(); }


//! \brief Generate an empty-payload segment (useful for creating empty ACK segments)
void TCPSender::send_empty_segment() {
    TCPSegment seg;
    // 设置发送的第一个字节的序列号
    seg.header().seqno = wrap(_next_seqno,_isn);
    _segments_out.push(seg);
}
