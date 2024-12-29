#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const {return _sender.stream_in().remaining_capacity();}

size_t TCPConnection::bytes_in_flight() const {return _sender.bytes_in_flight();}

size_t TCPConnection::unassembled_bytes() const {return _receiver.unassembled_bytes();}

size_t TCPConnection::time_since_last_segment_received() const {return time_since_last_segment_received_;}



void TCPConnection::reset_connection() {
   // 发送RST标志
   TCPSegment seg;
   seg.header().rst = true;
   _segments_out.push(seg);

   // 在出站入站流中标记错误，使active返回false
   _receiver.stream_out().set_error();
   _sender.stream_in().set_error();

   active_ = false;
}

void TCPConnection::segment_received(const TCPSegment &seg) {
    // 非启动时不接收
    if(!active_){
        return;
    }

    // 重置连接时间
    time_since_last_segment_received_  = 0;

    TCPHeader header=seg.header();
    if(header.rst){
        // reset信号，就设置inbound和outbound为error状态
        _receiver.stream_out().set_error();
        _sender.stream_in().set_error();
        active_ = false;
    }else if(state() == TCPState::State::LISTEN || state() == TCPState::State::CLOSED){
        // 等待接收SYN信号
        if(header.syn){
            _receiver.segment_received(seg);
            connect();
        }
    }else if(state() == TCPState::State::SYN_SENT){
        // 等待接收对SYN的确认，以及SYN信号
        // 如果只有syn信号，说明是同时开启连接，如果syn+ack是说明是确认信号
        if(header.syn){
            _receiver.segment_received(seg);
            if(header.ack){_sender.ack_received(header.ackno,header.win);}
            _sender.send_empty_segment();
            send_data();
        }
    }else if(state() == TCPState::State::SYN_RCVD){
        // 接收到SYN信号，并且发送了SYN信号
        _receiver.segment_received(seg);
        if(header.ack){
            _sender.ack_received(header.ackno,header.win);
            _sender.fill_window();
            send_data();
        }
    }else if(state() == TCPState::State::ESTABLISHED){
        if(header.ack)_sender.ack_received(header.ackno,header.win);
         _receiver.segment_received(seg);
        // 占序列号的要回复
        _sender.fill_window();
        if(seg.length_in_sequence_space()>0 && _sender.segments_out().empty()){
            _sender.send_empty_segment();
        }
        
        send_data();
    }else if(state() == TCPState::State::FIN_WAIT_1){
        // 已经发出去FIN信号了，等待接收对FIN的确认
        if(header.fin){
            // 收到fin信号，就要进入wait阶段，需要回复一个段确认FIN信号
            _receiver.segment_received(seg);
            if(header.ack)_sender.ack_received(header.ackno,header.win);
            _sender.send_empty_segment();
            send_data();
        }else if(header.ack){
            // 收到确认就不用回！！！！
            _receiver.segment_received(seg);
            if(header.ack)_sender.ack_received(header.ackno,header.win);
            send_data();
        }
    }else if(state() == TCPState::State::FIN_WAIT_2){
        // 发出的FIN已经被确认了，等待接收到FIN信号
        _receiver.segment_received(seg);
        if(header.ack) _sender.ack_received(header.ackno,header.win);
        _sender.send_empty_segment();
        send_data();
    }else if(state() == TCPState::State::TIME_WAIT){
       if (seg.header().fin) {
           // 收到FIN，保持Time-Wait状态
           _sender.ack_received(seg.header().ackno, seg.header().win);
           _receiver.segment_received(seg);
           _sender.send_empty_segment();
           send_data();
       }
    }else if(state() == TCPState::State::CLOSE_WAIT){
        // 已经接收到FIN确认了，但是没有发送FIN信号，等待发送FIN信号
        _sender.ack_received(seg.header().ackno, seg.header().win);
        _receiver.segment_received(seg);
        // 这里可能存在需要继续发的数据，没有数据就需要回一个ACK!!!!!!!!!
        _sender.fill_window();
        if(seg.length_in_sequence_space()>0 && _sender.segments_out().empty()){
            _sender.send_empty_segment();
        }
        send_data();
    }else if(state() == TCPState::State::LAST_ACK){
        // 接收到FIN了，并且发送了FIN，等待收到ACK
        _sender.ack_received(seg.header().ackno, seg.header().win);
        _receiver.segment_received(seg);
        _sender.send_empty_segment();
        send_data();
    }else{
        // cout<<"其他"<<endl;
        _receiver.segment_received(seg);
        if(header.ack)_sender.ack_received(header.ackno,header.win);
        _sender.fill_window();
        send_data();
    }
}

bool TCPConnection::active() const {
    return active_;
}

size_t TCPConnection::write(const string &data) {
   // 在sender中写入数据并发送
   size_t size = _sender.stream_in().write(data);
   _sender.fill_window();
   send_data();
   return size;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    if (!active()) {
       return;
    }
    time_since_last_segment_received_ += ms_since_last_tick;
    _sender.tick(ms_since_last_tick);
    if (_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS) {
        reset_connection();
        return;
    }
    send_data();
}

void TCPConnection::end_input_stream() {
    // 这里记得发数据
    _sender.stream_in().end_input();
    _sender.fill_window();
    send_data();
}

void TCPConnection::connect() {
    // 填充窗口，发送SYN信号，设置序列号seqno
    _sender.fill_window();
    send_data();
}

void TCPConnection::send_data(){
    // sender管SYN，FIN信号，还有seqno,receiver管win和ack标记，ackno
    while(!_sender.segments_out().empty()){
       TCPSegment seg = _sender.segments_out().front();
       _sender.segments_out().pop();
       // 尽量设置ackno和window_size
       if (_receiver.ackno().has_value()) {
           seg.header().ack = true;
           seg.header().ackno = _receiver.ackno().value();
           seg.header().win = _receiver.window_size();
       }
       _segments_out.push(seg);
    }
    if(_receiver.stream_out().input_ended()){
        // 已经接收到FIN信号了
        
        if(!_sender.stream_in().eof()){
            // 但是输出流没有发完
            _linger_after_streams_finish = false;
            // 说明可以不用逗留，是对面先发起的FIN信号
        }else if(_sender.bytes_in_flight()==0){
            // 输出流已经发完了，如果是不用逗留的情况，直接结束，如果是用逗留的情况，超时结束
            if((!_linger_after_streams_finish)||(time_since_last_segment_received() >= (10 * _cfg.rt_timeout))){
                active_ = false;
            }
        }
    }

}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";
            // Your code here: need to send a RST segment to the peer
            reset_connection();
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
