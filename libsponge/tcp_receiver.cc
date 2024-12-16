#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    // DUMMY_CODE(seg);
    //     StreamReassembler _reassembler;
    const TCPHeader& header=seg.header();
    if(header.syn){
        // 是第一次接到数据，记录起始序列号
        _isn=header.seqno;
        hasSyn=true;
    }
    if(hasSyn){
        // 获取数据
        Buffer data=seg.payload();
        uint64_t nextIndex=_reassembler.nextIndex();
        size_t index=unwrap(header.seqno,_isn,nextIndex);
        // printf("----%ld---%ld--\n",index,nextIndex);
        // index为绝对序列号
        // 这里是绝对索引与流索引的映射问题，index=0时，说明是开始信号，index=1时才是数据
        if(index>=1){
            index-=1;
        }else if(!header.syn && index==0 && data.size()>0){
            // 1. 在第一个SYN信号不带数据，第二个段的起始数据从SYN对应序列开始的数据，就需要截断一位。
            // 2. 当然如果当前段就是SYN信号的段，并且带数据，可以不用截断，直接用
            data.remove_prefix(1);
        }

        // 数据，开始的索引，流的索引，是否结束流
        _reassembler.push_substring(data.copy(),index,header.fin);
    }
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    if(hasSyn){
        // 将一个绝对序列号转成WrappingInt32
        // 必须有开始信号，并且如果已经结束了，并且未组装的字节流为空
        // 第一个bit没有接收到的序列号，SYN和FIN都占一个序列号
        uint64_t n = _reassembler.nextIndex() + 1 + (_reassembler.stream_out().input_ended() ? 1 : 0);
        // n为绝对序列号，转成对应的序列号，希望接收到的下一个字节的序列号
        return wrap(n,_isn);
    }
    return {};
}

size_t TCPReceiver::window_size() const { return _reassembler.stream_out().remaining_capacity(); }
