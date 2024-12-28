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
    cerr<<"收到信号了吗222\n";
    // 非启动时不接收
    if(!active_){
        return;
    }
    // cout<<"是否有FIN信号2222"<<seg.header().fin<<seg.length_in_sequence_space()<<endl;
    // 重置连接时间
    if(seg.length_in_sequence_space()>0)
    time_since_last_segment_received_  = 0;

    TCPHeader header=seg.header();
    if(header.rst){
        // reset信号，就设置inbound和outbound为error状态
        _receiver.stream_out().set_error();
        _sender.stream_in().set_error();
        active_ = false;
        // 断开连接
    }else if(state() == TCPState::State::CLOSED){
        if(header.syn){
            _receiver.segment_received(seg);
            _sender.fill_window();
            send_data();
        }
    }else if(state() == TCPState::State::LISTEN){
        cerr<<"收到信号了吗\n";
        // cout<<"LISTEN or CLOSED"<<endl;
        // 等待接收SYN信号
        if(header.syn){
            _receiver.segment_received(seg);
            _sender.send_empty_segment();
            send_data();
        }
    }else if(state() == TCPState::State::SYN_SENT){
        
        // 收到了SYN信号，但是没有ACK，说明是同时建立连接
        // 收到SYN信号，有ACK信号，说明是对面对SYN信号的确认
        // 等待接收对SYN的确认
        // cout<<"SYN_SENT"<<header.syn<<"   "<<header.ack<<endl;
        if(header.syn){
            _receiver.segment_received(seg);
            if(header.ack){_sender.ack_received(header.ackno,header.win);}
            // ！！！！！！这里要回复一个空段
            _sender.send_empty_segment();
            send_data();
        }
    }else if(state() == TCPState::State::SYN_RCVD){
        // cout<<"SYN_RCVD"<<endl;
        // 接收到SYN信号，并且发送了SYN信号
        _receiver.segment_received(seg);
        if(header.ack)_sender.ack_received(header.ackno,header.win);
    }else if(state() == TCPState::State::ESTABLISHED){
        // cout<<"ESTABLISHED"<<endl;
        // 正常等待数据的接收和发送
        _receiver.segment_received(seg);
        if(header.ack)_sender.ack_received(header.ackno,header.win);
        // 占序列号的要回复
        // cout<<"为什么22"<<endl;
        if(seg.length_in_sequence_space()>0){
            // cout<<"为什么"<<endl;
            _sender.send_empty_segment();
        }
        _sender.fill_window();
        send_data();
    }else if(state() == TCPState::State::FIN_WAIT_1){
        // 已经发出去FIN信号了，等待接收对FIN的确认
        // cout<<"FIN_WAIT_1"<<endl;
        if(seg.header().fin){
            // 收到Fin，则发送新ACK，进入Closing/Time-Wait
            _receiver.segment_received(seg);
            if(header.ack)_sender.ack_received(header.ackno,header.win);
            _sender.send_empty_segment();
            send_data();
        }else if(seg.header().ack){
            // 收到ACK，进入Fin-Wait-2
            _sender.ack_received(seg.header().ackno, seg.header().win);
           _receiver.segment_received(seg);
           send_data();
        }
    }else if(state() == TCPState::State::FIN_WAIT_2){
        // 发出的FIN已经被确认了，等待接收到FIN信号
        // cout<<"FIN_WAIT_2"<<endl;
        _receiver.segment_received(seg);
        if(header.ack)_sender.ack_received(header.ackno,header.win);
        _sender.send_empty_segment();
        send_data();
    }else if(state() == TCPState::State::TIME_WAIT){
        // cout<<"FIN_WAIT"<<endl;
       if (seg.header().fin) {
           // 收到FIN，保持Time-Wait状态
           _sender.ack_received(seg.header().ackno, seg.header().win);
           _receiver.segment_received(seg);
           _sender.send_empty_segment();
           send_data();
       }
    }else if(state() == TCPState::State::CLOSE_WAIT){
        // 已经接收到FIN伦理，但是没有发送FIN信号，等待发送FIN信号
        _sender.ack_received(seg.header().ackno, seg.header().win);
        _receiver.segment_received(seg);
        _sender.send_empty_segment();
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
    // // if(_receiver.ackno().has_value() && (seg.length_in_sequence_space()==0) && (header.seqno == (_receiver.ackno().value()-1))){
    // //     _sender.send_empty_segment();
    // // }
    // _sender.fill_window();
    // cout<<"下空"<<_sender.segments_out().empty()<<endl;
    // send_data();
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
    // cout<<"过去多久了"<<ms_since_last_tick<<" "<<time_since_last_segment_received_<<endl;
    time_since_last_segment_received_ += ms_since_last_tick;
    _sender.tick(ms_since_last_tick);
    // ！！！！！！！！！！注意，这里是大于
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
    cerr<<"发送信号了没\n";
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
    // 发送完毕要结束链接
    if(_receiver.stream_out().input_ended()){
        // 已经全部接收了
        if(!_sender.stream_in().eof()){
            // 但是没有全部发送过去
            _linger_after_streams_finish = false;
        }else if(_sender.bytes_in_flight() == 0){
            // ！！！！！！！！！！注意，这里是等于
            if (!_linger_after_streams_finish || (time_since_last_segment_received() >= 10 * _cfg.rt_timeout)) {
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
