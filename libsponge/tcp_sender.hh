#ifndef SPONGE_LIBSPONGE_TCP_SENDER_HH
#define SPONGE_LIBSPONGE_TCP_SENDER_HH

#include "byte_stream.hh"
#include "tcp_config.hh"
#include "tcp_segment.hh"
#include "wrapping_integers.hh"

#include <functional>
#include <queue>



class RTtimer {
  private:
    // 当前用时
    uint16_t now_time = 0;
    // 超时重传的界限
    uint16_t rt_timeout{};
    // 当前超时重传次数
    unsigned int retx_attempts = 0;
    // 是否启动计时
    bool enable = false;

  public:
    RTtimer(const uint16_t retx_timeout):rt_timeout(retx_timeout){}
    // 是否应该重传,时间相等的时候也要重传
    bool should_rt(){ return enable && (now_time >= rt_timeout);}

    void start(){ enable = true;}
    void stop(){ enable = false;}

    // 一切归零
    void restart(const uint16_t retx_timeout){
      now_time = 0;
      rt_timeout = retx_timeout;
      retx_attempts = 0;
      enable = false;
    }

    // 重传,只有当窗口不为0时，rt_timeout才翻倍
    void rt(uint16_t window_size){
      now_time = 0;
      if(window_size>0){
        rt_timeout *= 2;
      }
      retx_attempts ++;
    }

    unsigned int get_retx_attempts() const {return retx_attempts;}

    bool tick(size_t dt){
      if(enable){
        now_time +=dt;
        return should_rt();
      }
      return false;
    }
};
//! \brief The "sender" part of a TCP implementation.

//! Accepts a ByteStream, divides it up into segments and sends the
//! segments, keeps track of which segments are still in-flight,
//! maintains the Retransmission Timer, and retransmits in-flight
//! segments if the retransmission timer expires.
class TCPSender {
  private:
    //! our initial sequence number, the number for our SYN.
    WrappingInt32 _isn;

    //! outbound queue of segments that the TCPSender wants sent
    std::queue<TCPSegment> _segments_out{};

    //! 发送的TCP段，但是没有被确定的段
    std::queue<TCPSegment> _segments_out_nuack{};

    //! 发送但是没有被确认的字节
    uint64_t _bytes_unack{0};

    //! retransmission timer for the connection
    unsigned int _initial_retransmission_timeout;

    //! outgoing stream of bytes that have not yet been sent
    ByteStream _stream;

    //! the (absolute) sequence number for the next byte to be sent
    uint64_t _next_seqno{0};

    //! 最后一次受到的确认号,转换为绝对序列号了
    uint64_t _ack_abs_seqno{0};

    //! 初始窗口大小为1
    uint16_t _window_size{1};

    RTtimer rt;

    //! 是否发送了fin信号
    bool fin{false};

  public:
    //! Initialize a TCPSender
    TCPSender(const size_t capacity = TCPConfig::DEFAULT_CAPACITY,
              const uint16_t retx_timeout = TCPConfig::TIMEOUT_DFLT,
              const std::optional<WrappingInt32> fixed_isn = {});

    //! \name "Input" interface for the writer
    //!@{
    ByteStream &stream_in() { return _stream; }
    const ByteStream &stream_in() const { return _stream; }
    //!@}

    //! \name Methods that can cause the TCPSender to send a segment
    //!@{

    //! \brief A new acknowledgment was received
    void ack_received(const WrappingInt32 ackno, const uint16_t window_size);

    //! \brief Generate an empty-payload segment (useful for creating empty ACK segments)
    void send_empty_segment();

    //! \brief create and send segments to fill as much of the window as possible
    void fill_window();

    //! \brief Notifies the TCPSender of the passage of time
    void tick(const size_t ms_since_last_tick);
    //!@}

    //! \name Accessors
    //!@{

    //! \brief How many sequence numbers are occupied by segments sent but not yet acknowledged?
    //! \note count is in "sequence space," i.e. SYN and FIN each count for one byte
    //! (see TCPSegment::length_in_sequence_space())
    size_t bytes_in_flight() const;

    //! \brief Number of consecutive retransmissions that have occurred in a row
    unsigned int consecutive_retransmissions() const;

    //! \brief TCPSegments that the TCPSender has enqueued for transmission.
    //! \note These must be dequeued and sent by the TCPConnection,
    //! which will need to fill in the fields that are set by the TCPReceiver
    //! (ackno and window size) before sending.
    std::queue<TCPSegment> &segments_out() { return _segments_out; }
    //!@}

    //! \name What is the next sequence number? (used for testing)
    //!@{

    //! \brief absolute seqno for the next byte to be sent
    uint64_t next_seqno_absolute() const { return _next_seqno; }

    //! \brief relative seqno for the next byte to be sent
    WrappingInt32 next_seqno() const { return wrap(_next_seqno, _isn); }
    //!@}
};

#endif  // SPONGE_LIBSPONGE_TCP_SENDER_HH
