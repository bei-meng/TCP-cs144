#include "tcp_state.hh"

using namespace std;

bool TCPState::operator==(const TCPState &other) const {
    return _active == other._active and _linger_after_streams_finish == other._linger_after_streams_finish and
           _sender == other._sender and _receiver == other._receiver;
}

bool TCPState::operator!=(const TCPState &other) const { return not operator==(other); }

string TCPState::name() const {
    return "sender=`" + _sender + "`, receiver=`" + _receiver + "`, active=" + to_string(_active) +
           ", linger_after_streams_finish=" + to_string(_linger_after_streams_finish);
}

TCPState::TCPState(const TCPState::State state) {
    switch (state) {
        case TCPState::State::LISTEN:
            // 接收在监听，发送关闭阶段
            _receiver = TCPReceiverStateSummary::LISTEN;
            _sender = TCPSenderStateSummary::CLOSED;
            break;
        case TCPState::State::SYN_RCVD:
            // 
            _receiver = TCPReceiverStateSummary::SYN_RECV;
            _sender = TCPSenderStateSummary::SYN_SENT;
            break;
        case TCPState::State::SYN_SENT:
            _receiver = TCPReceiverStateSummary::LISTEN;
            _sender = TCPSenderStateSummary::SYN_SENT;
            break;
        case TCPState::State::ESTABLISHED:
            // 发送了SYN信号，同时SYN信号被确认了，连接建立成功
            _receiver = TCPReceiverStateSummary::SYN_RECV;
            _sender = TCPSenderStateSummary::SYN_ACKED;
            break;
        case TCPState::State::CLOSE_WAIT:
            _receiver = TCPReceiverStateSummary::FIN_RECV;
            _sender = TCPSenderStateSummary::SYN_ACKED;
            _linger_after_streams_finish = false;
            break;
        case TCPState::State::LAST_ACK:
            _receiver = TCPReceiverStateSummary::FIN_RECV;
            _sender = TCPSenderStateSummary::FIN_SENT;
            _linger_after_streams_finish = false;
            break;
        case TCPState::State::CLOSING:
            _receiver = TCPReceiverStateSummary::FIN_RECV;
            _sender = TCPSenderStateSummary::FIN_SENT;
            break;
        case TCPState::State::FIN_WAIT_1:
            _receiver = TCPReceiverStateSummary::SYN_RECV;
            _sender = TCPSenderStateSummary::FIN_SENT;
            break;
        case TCPState::State::FIN_WAIT_2:
            _receiver = TCPReceiverStateSummary::SYN_RECV;
            _sender = TCPSenderStateSummary::FIN_ACKED;
            break;
        case TCPState::State::TIME_WAIT:
            _receiver = TCPReceiverStateSummary::FIN_RECV;
            _sender = TCPSenderStateSummary::FIN_ACKED;
            break;
        case TCPState::State::RESET:
            _receiver = TCPReceiverStateSummary::ERROR;
            _sender = TCPSenderStateSummary::ERROR;
            _linger_after_streams_finish = false;
            _active = false;
            break;
        case TCPState::State::CLOSED:
            _receiver = TCPReceiverStateSummary::FIN_RECV;
            _sender = TCPSenderStateSummary::FIN_ACKED;
            _linger_after_streams_finish = false;
            _active = false;
            break;
    }
}

TCPState::TCPState(const TCPSender &sender, const TCPReceiver &receiver, const bool active, const bool linger)
    : _sender(state_summary(sender))
    , _receiver(state_summary(receiver))
    , _active(active)
    , _linger_after_streams_finish(active ? linger : false) {}

string TCPState::state_summary(const TCPReceiver &receiver) {
    if (receiver.stream_out().error()) {
        // error
        return TCPReceiverStateSummary::ERROR;
    } else if (not receiver.ackno().has_value()) {
        // 接收方没有确认号，说明没有收到数据，在LISTEN阶段
        return TCPReceiverStateSummary::LISTEN;
    } else if (receiver.stream_out().input_ended()) {
        // 如果不在LISTEN阶段，收到FIN信号
        return TCPReceiverStateSummary::FIN_RECV;
    } else {
        // 如果不在LISTEN阶段，没有收到FIN信号
        return TCPReceiverStateSummary::SYN_RECV;
    }
}

string TCPState::state_summary(const TCPSender &sender) {
    if (sender.stream_in().error()) {
        // error
        return TCPSenderStateSummary::ERROR;
    } else if (sender.next_seqno_absolute() == 0) {
        // sender没有发送过数据，说明在关闭阶段
        return TCPSenderStateSummary::CLOSED;
    } else if (sender.next_seqno_absolute() == sender.bytes_in_flight()) {
        // 发送了数据，但是没有收到ACK信号，还在SYN_SENT阶段
        return TCPSenderStateSummary::SYN_SENT;
    } else if (not sender.stream_in().eof()) {
        // 收到了SYN的ACK信号，但是数据没有发送完
        return TCPSenderStateSummary::SYN_ACKED;
    } else if (sender.next_seqno_absolute() < sender.stream_in().bytes_written() + 2) {
        // 收到了SYN的ACK信号，没有发送FIN信号
        return TCPSenderStateSummary::SYN_ACKED;
    } else if (sender.bytes_in_flight()) {
        // 已经发送了FIN信号，但是没有被确认
        return TCPSenderStateSummary::FIN_SENT;
    } else {
        // FIN信号被确认了
        return TCPSenderStateSummary::FIN_ACKED;
    }
}
